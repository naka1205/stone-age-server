// src/world/WorldFamily.cpp —— 家族系统与庄园据点据点战实现
//
// 对应原版 family.c, include/family.h, manor.c

#include "WorldImpl.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace SA::World
{

// ══ 家族系统 (Family System, 对齐 family.c / include/family.h) ══════════════
std::uint32_t World::createFamily(SA::Net::SessionId leader, const std::string &family_name,
                                  const std::string &rule)
{
	Impl &s = *_impl;
	if (s.conns.find(leader) == s.conns.end())
		return 0;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(leader));
	if (p == nullptr || p->hp <= 0 || s.inBattle(leader))
		return 0;

	// 等级门禁: FMLEADERLV = 30
	if (p->level < kFamilyCreateLevel)
		return 0;

	// 已有家族检查
	if (playerFamilyId(leader) != 0)
		return 0;

	// 家族名称校验: 非空、最大 32 字节、不可含空格
	if (family_name.empty() || family_name.size() > 32 || family_name.find(' ') != std::string::npos)
		return 0;

	// 家族名称唯一性检查
	for (const auto &kv : s.families)
	{
		if (kv.second.name == family_name)
			return 0;
	}

	// 石币扣除: kFamilyCreateFee (10,000)，严格走 GoldLedger
	GoldTx tx = delGold(*p, GoldReason::kFamilyCreate, kFamilyCreateFee, 0, 0, s);
	if (tx.disposition == GoldDisposition::kRejected)
		return 0;

	const std::uint32_t fid = s.next_family_id++;
	FamilyInfo fam{};
	fam.family_id = fid;
	fam.name = family_name;
	fam.rule = rule.empty() ? "石器家族，团结互助" : rule;
	fam.leader_name = p->name.c_str();
	fam.family_fame = 100;

	FamilyMember m{};
	m.charname = fam.leader_name;
	m.session = leader;
	m.level = p->level;
	m.graphicsno = p->image;
	m.role = FamilyRole::kLeader;
	m.contribution = 100;
	m.online = true;

	fam.members.push_back(m);

	s.families[fid] = std::move(fam);
	s.session_to_family[leader] = fid;
	s.player_to_family_name[m.charname] = fid;

	return fid;
}

bool World::disbandFamily(SA::Net::SessionId leader, std::uint32_t family_id)
{
	Impl &s = *_impl;
	auto fit = s.families.find(family_id);
	if (fit == s.families.end())
		return false;

	if (playerFamilyId(leader) != family_id)
		return false;
	if (playerFamilyRole(leader) != FamilyRole::kLeader)
		return false;

	// 若占领庄园，自动释放据点
	if (fit->second.manor != FamilyManor::kNone)
	{
		s.manor_owners.erase(fit->second.manor);
	}

	// 清理所有成员归属
	for (const auto &m : fit->second.members)
	{
		s.player_to_family_name.erase(m.charname);
		if (m.session != 0)
			s.session_to_family.erase(m.session);
	}

	s.families.erase(fit);
	return true;
}

bool World::applyJoinFamily(SA::Net::SessionId session, std::uint32_t family_id)
{
	Impl &s = *_impl;
	if (playerFamilyId(session) != 0)
		return false;

	auto fit = s.families.find(family_id);
	if (fit == s.families.end())
		return false;

	// [RV-1] 满员门禁: 达到 50 人上限阻断申请
	if (fit->second.members.size() >= kMaxFamilyMembers)
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	const std::string name = p->name.c_str();
	for (const auto &app : fit->second.applicants)
	{
		if (app.charname == name)
			return false; // 重复申请拦截
	}

	FamilyMember app{};
	app.charname = name;
	app.session = session;
	app.level = p->level;
	app.graphicsno = p->image;
	app.role = FamilyRole::kApply;
	app.online = true;

	fit->second.applicants.push_back(app);
	return true;
}

bool World::acceptFamilyMember(SA::Net::SessionId operator_session, std::uint32_t family_id,
                               const std::string &applicant_name, bool accept)
{
	Impl &s = *_impl;
	if (playerFamilyId(operator_session) != family_id)
		return false;

	const auto op_role = playerFamilyRole(operator_session);
	if (op_role != FamilyRole::kLeader && op_role != FamilyRole::kElder)
		return false;

	auto fit = s.families.find(family_id);
	if (fit == s.families.end())
		return false;

	auto app_it = fit->second.applicants.end();
	for (auto it = fit->second.applicants.begin(); it != fit->second.applicants.end(); ++it)
	{
		if (it->charname == applicant_name)
		{
			app_it = it;
			break;
		}
	}
	if (app_it == fit->second.applicants.end())
		return false;

	if (!accept)
	{
		fit->second.applicants.erase(app_it);
		return true;
	}

	// [RV-1] 满员门禁: 达到 50 人上限阻断批准
	if (fit->second.members.size() >= kMaxFamilyMembers)
		return false;

	if (s.player_to_family_name.count(applicant_name) > 0)
	{
		fit->second.applicants.erase(app_it);
		return false; // 申请人已加入了其它家族
	}

	FamilyMember m = *app_it;
	fit->second.applicants.erase(app_it);
	m.role = FamilyRole::kMember;
	m.contribution = 10;

	// 检测申请人当前是否在线
	SA::Net::SessionId app_sid = 0;
	for (const auto &kv : s.conns)
	{
		const auto *p = s.players.resolve(s.player_of_session.find(kv.first));
		if (p != nullptr && applicant_name == p->name.c_str())
		{
			app_sid = kv.first;
			break;
		}
	}

	if (app_sid != 0)
	{
		m.session = app_sid;
		m.online = true;
		s.session_to_family[app_sid] = family_id;
	}
	else
	{
		m.session = 0;
		m.online = false;
	}

	fit->second.members.push_back(m);
	s.player_to_family_name[applicant_name] = family_id;
	return true;
}

bool World::kickFamilyMember(SA::Net::SessionId operator_session, std::uint32_t family_id,
                             const std::string &target_name)
{
	Impl &s = *_impl;
	if (playerFamilyId(operator_session) != family_id)
		return false;

	const auto op_role = playerFamilyRole(operator_session);
	if (op_role != FamilyRole::kLeader && op_role != FamilyRole::kElder)
		return false;

	auto fit = s.families.find(family_id);
	if (fit == s.families.end())
		return false;

	auto mem_it = fit->second.members.end();
	for (auto it = fit->second.members.begin(); it != fit->second.members.end(); ++it)
	{
		if (it->charname == target_name)
		{
			mem_it = it;
			break;
		}
	}
	if (mem_it == fit->second.members.end())
		return false;

	// 族长不可被任何人开除
	if (mem_it->role == FamilyRole::kLeader)
		return false;

	// 长老不可开除长老
	if (op_role == FamilyRole::kElder && mem_it->role == FamilyRole::kElder)
		return false;

	if (mem_it->session != 0)
		s.session_to_family.erase(mem_it->session);
	s.player_to_family_name.erase(target_name);
	fit->second.members.erase(mem_it);
	return true;
}

bool World::leaveFamily(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	const auto fid = playerFamilyId(session);
	if (fid == 0)
		return false;

	const auto role = playerFamilyRole(session);
	if (role == FamilyRole::kLeader)
		return false; // 族长不可直接退，需转让或解散

	auto fit = s.families.find(fid);
	if (fit == s.families.end())
		return false;

	const auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	const std::string name = p->name.c_str();
	for (auto it = fit->second.members.begin(); it != fit->second.members.end(); ++it)
	{
		if (it->charname == name)
		{
			fit->second.members.erase(it);
			break;
		}
	}

	s.session_to_family.erase(session);
	s.player_to_family_name.erase(name);
	return true;
}

bool World::setFamilyMemberRole(SA::Net::SessionId operator_session, std::uint32_t family_id,
                                const std::string &target_name, FamilyRole new_role)
{
	Impl &s = *_impl;
	if (playerFamilyId(operator_session) != family_id)
		return false;
	if (playerFamilyRole(operator_session) != FamilyRole::kLeader)
		return false; // 仅族长可调整职位

	auto fit = s.families.find(family_id);
	if (fit == s.families.end())
		return false;

	FamilyMember *target_m = nullptr;
	for (auto &m : fit->second.members)
	{
		if (m.charname == target_name)
		{
			target_m = &m;
			break;
		}
	}
	if (target_m == nullptr)
		return false;

	if (new_role == FamilyRole::kLeader)
	{
		// 转让族长
		const auto *op_p = s.players.resolve(s.player_of_session.find(operator_session));
		if (op_p != nullptr)
		{
			for (auto &m : fit->second.members)
			{
				if (m.charname == op_p->name.c_str())
				{
					m.role = FamilyRole::kElder; // 原族长变为长老
					break;
				}
			}
		}
		target_m->role = FamilyRole::kLeader;
		fit->second.leader_name = target_name;
		return true;
	}
	else if (new_role == FamilyRole::kElder || new_role == FamilyRole::kMember)
	{
		if (target_m->role == FamilyRole::kLeader)
			return false; // 族长不可降职自己
		target_m->role = new_role;
		return true;
	}

	return false;
}

bool World::setFamilyRule(SA::Net::SessionId operator_session, std::uint32_t family_id,
                          const std::string &new_rule)
{
	Impl &s = *_impl;
	if (playerFamilyId(operator_session) != family_id)
		return false;

	const auto op_role = playerFamilyRole(operator_session);
	if (op_role != FamilyRole::kLeader && op_role != FamilyRole::kElder)
		return false;

	if (new_rule.size() > 256)
		return false;

	auto fit = s.families.find(family_id);
	if (fit == s.families.end())
		return false;

	fit->second.rule = new_rule;
	return true;
}

bool World::depositFamilyGold(SA::Net::SessionId session, std::uint32_t amount)
{
	Impl &s = *_impl;
	if (amount == 0)
		return false;

	const auto fid = playerFamilyId(session);
	if (fid == 0)
		return false;

	auto fit = s.families.find(fid);
	if (fit == s.families.end())
		return false;

	// [RV-2] 家族金库上限检查 (上限 1 亿石币)
	if (static_cast<std::uint64_t>(fit->second.family_gold) + amount > kMaxFamilyGold)
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || p->hp <= 0)
		return false;

	// 严格通过 GoldLedger 扣除随身石币
	GoldTx tx = delGold(*p, GoldReason::kFamilyDeposit, static_cast<std::int32_t>(amount), 0, fid, s);
	if (tx.disposition == GoldDisposition::kRejected)
		return false;

	fit->second.family_gold += tx.applied;
	for (auto &m : fit->second.members)
	{
		if (m.session == session)
		{
			m.contribution += tx.applied / 1000;
			break;
		}
	}
	fit->second.family_fame += tx.applied / 1000;
	return true;
}

bool World::withdrawFamilyGold(SA::Net::SessionId session, std::uint32_t amount)
{
	Impl &s = *_impl;
	if (amount == 0)
		return false;

	const auto fid = playerFamilyId(session);
	if (fid == 0)
		return false;

	const auto role = playerFamilyRole(session);
	if (role != FamilyRole::kLeader && role != FamilyRole::kElder)
		return false; // 仅族长与长老可取款

	auto fit = s.families.find(fid);
	if (fit == s.families.end())
		return false;

	if (fit->second.family_gold < static_cast<std::int32_t>(amount))
		return false; // 金库余额不足

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || p->hp <= 0)
		return false;

	// [RV-2] 玩家随身容量预检: 随身石币上限防溢出
	if (static_cast<std::int64_t>(p->gold) + amount > maxHaveGold(0))
		return false;

	fit->second.family_gold -= static_cast<std::int32_t>(amount);
	// 严格通过 GoldLedger 增加随身石币
	addGold(*p, GoldReason::kFamilyWithdraw, static_cast<std::int32_t>(amount), 0, fid, s);
	return true;
}

bool World::occupyManor(std::uint32_t family_id, FamilyManor manor)
{
	Impl &s = *_impl;
	if (manor == FamilyManor::kNone)
		return false;

	auto fit = s.families.find(family_id);
	if (fit == s.families.end())
		return false;

	// 若该家族此前已占有其它庄园，先解除旧庄园归属
	if (fit->second.manor != FamilyManor::kNone && fit->second.manor != manor)
	{
		s.manor_owners.erase(fit->second.manor);
	}

	// 若该庄园此前已被其它家族占领，剥夺旧家族的据点
	auto oit = s.manor_owners.find(manor);
	if (oit != s.manor_owners.end())
	{
		auto prev_fit = s.families.find(oit->second);
		if (prev_fit != s.families.end())
		{
			prev_fit->second.manor = FamilyManor::kNone;
		}
	}

	s.manor_owners[manor] = family_id;
	fit->second.manor = manor;

	// 同步庄园战防守方状态
	auto &war = s.manor_wars[manor];
	war.manor = manor;
	war.defender_family_id = family_id;
	war.challenger_family_id = 0;
	war.state = ManorWarState::kIdle;
	war.challenge_deposit = 0;
	war.defender_score = 0;
	war.challenger_score = 0;

	syncManorStateToCrossServer(manor);
	return true;
}

FamilyManor World::familyManor(std::uint32_t family_id) const
{
	const auto fit = _impl->families.find(family_id);
	if (fit == _impl->families.end())
		return FamilyManor::kNone;
	return fit->second.manor;
}

std::uint32_t World::manorOwnerFamily(FamilyManor manor) const
{
	const auto it = _impl->manor_owners.find(manor);
	if (it == _impl->manor_owners.end())
		return 0;
	return it->second;
}

ManorChallengeResult World::challengeManor(SA::Net::SessionId session, FamilyManor manor, std::uint32_t deposit)
{
	Impl &s = *_impl;
	if (manor == FamilyManor::kNone)
		return ManorChallengeResult::kInvalidManor;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || p->hp <= 0)
		return ManorChallengeResult::kPlayerDead;

	if (s.inBattle(session))
		return ManorChallengeResult::kInBattle;

	if (isPlayerVending(session))
		return ManorChallengeResult::kVending;

	const auto fid = playerFamilyId(session);
	if (fid == 0)
		return ManorChallengeResult::kNotLeader;

	const auto role = playerFamilyRole(session);
	if (role != FamilyRole::kLeader)
		return ManorChallengeResult::kNotLeader;

	// 挑战方家族自身不能已占领庄园
	if (familyManor(fid) != FamilyManor::kNone)
		return ManorChallengeResult::kAlreadyOwnManor;

	if (deposit < kManorChallengeMinDeposit)
		return ManorChallengeResult::kDepositInsufficient;

	auto &war = s.manor_wars[manor];
	war.manor = manor;
	if (war.state != ManorWarState::kIdle)
		return ManorChallengeResult::kManorNotIdle;

	const auto current_owner = manorOwnerFamily(manor);
	if (current_owner == 0)
	{
		// 无守方庄园，直接占领
		occupyManor(fid, manor);
		return ManorChallengeResult::kNoDefender;
	}

	// 守方不能是自己
	if (current_owner == fid)
		return ManorChallengeResult::kAlreadyOwnManor;

	// [RV-2] 原子扣除挑战押金
	const GoldTx tx = delGold(*p, GoldReason::kManorChallengeFee, static_cast<std::int32_t>(deposit), 0, fid, s);
	if (tx.disposition == GoldDisposition::kRejected)
		return ManorChallengeResult::kGoldInsufficient;

	war.state = ManorWarState::kScheduled;
	war.defender_family_id = current_owner;
	war.challenger_family_id = fid;
	war.challenge_deposit = deposit;
	war.defender_score = 0;
	war.challenger_score = 0;
	war.scheduled_time_ms = s.clock.nowMs();
	war.war_end_time_ms = 0;

	syncManorWarScheduleToCrossServer(manor);
	return ManorChallengeResult::kSuccess;
}

bool World::startManorWar(FamilyManor manor)
{
	Impl &s = *_impl;
	auto it = s.manor_wars.find(manor);
	if (it == s.manor_wars.end())
		return false;

	if (it->second.state != ManorWarState::kScheduled)
		return false;

	it->second.state = ManorWarState::kInWar;
	it->second.war_end_time_ms = s.clock.nowMs() + kManorWarDurationMs;
	return true;
}

bool World::recordManorDuelScore(FamilyManor manor, std::uint32_t winning_family_id, std::uint32_t score_points)
{
	Impl &s = *_impl;
	auto it = s.manor_wars.find(manor);
	if (it == s.manor_wars.end())
		return false;

	if (it->second.state != ManorWarState::kInWar)
		return false;

	if (winning_family_id == it->second.defender_family_id)
	{
		it->second.defender_score += score_points;
		syncManorDuelScoreToCrossServer(manor, winning_family_id, score_points);
		return true;
	}
	else if (winning_family_id == it->second.challenger_family_id)
	{
		it->second.challenger_score += score_points;
		syncManorDuelScoreToCrossServer(manor, winning_family_id, score_points);
		return true;
	}
	return false;
}

bool World::concludeManorWar(FamilyManor manor, std::uint32_t victorious_family_id)
{
	Impl &s = *_impl;
	auto it = s.manor_wars.find(manor);
	if (it == s.manor_wars.end())
		return false;

	if (it->second.state != ManorWarState::kInWar)
		return false;

	auto &war = it->second;
	if (victorious_family_id == war.challenger_family_id)
	{
		// 挑战方胜出：庄园易主！
		const auto challenger_id = war.challenger_family_id;
		const auto deposit = war.challenge_deposit;
		occupyManor(challenger_id, manor);

		// 挑战金退回并全额奖励挑战家族金库
		auto c_fit = s.families.find(challenger_id);
		if (c_fit != s.families.end())
		{
			c_fit->second.family_gold = static_cast<std::int32_t>(
			    std::min<std::int64_t>(kMaxFamilyGold, static_cast<std::int64_t>(c_fit->second.family_gold) + deposit));
			c_fit->second.family_fame += 500; // 攻下庄园增加 500 家族声望
		}
	}
	else if (victorious_family_id == war.defender_family_id)
	{
		// 守方卫冕成功：庄园归属保持
		const auto defender_id = war.defender_family_id;
		const auto deposit = war.challenge_deposit;

		// [RV-2] 挑战方没收之押金 100% 注资守方家族金库
		auto d_fit = s.families.find(defender_id);
		if (d_fit != s.families.end())
		{
			d_fit->second.family_gold = static_cast<std::int32_t>(
			    std::min<std::int64_t>(kMaxFamilyGold, static_cast<std::int64_t>(d_fit->second.family_gold) + deposit));
			d_fit->second.family_fame += 200; // 卫冕成功增加 200 家族声望
		}
	}
	else
	{
		return false;
	}

	// 战后休战保护期
	war.state = ManorWarState::kCooldown;
	war.challenger_family_id = 0;
	war.challenge_deposit = 0;
	war.war_end_time_ms = s.clock.nowMs() + kManorWarCooldownMs;
	syncManorWarConclusionToCrossServer(manor, victorious_family_id);
	return true;
}

ManorWarInfo World::getManorWarInfo(FamilyManor manor) const
{
	const auto it = _impl->manor_wars.find(manor);
	if (it == _impl->manor_wars.end())
	{
		ManorWarInfo info{};
		info.manor = manor;
		info.defender_family_id = manorOwnerFamily(manor);
		info.state = ManorWarState::kIdle;
		return info;
	}
	return it->second;
}

std::optional<FamilyInfo> World::getFamilyInfo(std::uint32_t family_id) const
{
	const auto fit = _impl->families.find(family_id);
	if (fit == _impl->families.end())
		return std::nullopt;
	return fit->second;
}

std::uint32_t World::playerFamilyId(SA::Net::SessionId session) const
{
	const auto it = _impl->session_to_family.find(session);
	if (it != _impl->session_to_family.end())
		return it->second;

	const auto *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p != nullptr)
	{
		auto nit = _impl->player_to_family_name.find(p->name.c_str());
		if (nit != _impl->player_to_family_name.end())
			return nit->second;
	}
	return 0;
}

FamilyRole World::playerFamilyRole(SA::Net::SessionId session) const
{
	const auto fid = playerFamilyId(session);
	if (fid == 0)
		return FamilyRole::kNone;

	const auto fit = _impl->families.find(fid);
	if (fit == _impl->families.end())
		return FamilyRole::kNone;

	const auto *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return FamilyRole::kNone;

	const std::string name = p->name.c_str();
	for (const auto &m : fit->second.members)
	{
		if (m.charname == name)
			return m.role;
	}
	for (const auto &app : fit->second.applicants)
	{
		if (app.charname == name)
			return app.role;
	}
	return FamilyRole::kNone;
}

std::size_t World::familyCount() const
{
	return _impl->families.size();
}

} // namespace SA::World
