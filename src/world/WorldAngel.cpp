// src/world/WorldAngel.cpp —— S11 精灵/天使系统大世界运行时实现
//
// 对应原版 char/char_angel.c (Robin 天使召唤、使者与勇者使命、信物瞬移与神佑庇护)

#include "WorldImpl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace SA::World
{

bool World::registerAngelMission(const SA::Rules::AngelMissionDefinition &mission)
{
	if (mission.id <= 0)
		return false;
	_impl->angel_missions[mission.id] = mission;
	return true;
}

const SA::Rules::AngelMissionDefinition *World::findAngelMission(int mission_id) const
{
	const auto it = _impl->angel_missions.find(mission_id);
	if (it == _impl->angel_missions.end())
		return nullptr;
	return &it->second;
}

std::vector<SA::Rules::AngelMissionDefinition> World::allAngelMissions() const
{
	std::vector<SA::Rules::AngelMissionDefinition> list;
	list.reserve(_impl->angel_missions.size());
	for (const auto &kv : _impl->angel_missions)
		list.push_back(kv.second);
	return list;
}

std::uint64_t World::createAngelContract(SA::Net::SessionId angel_session,
                                         SA::Net::SessionId hero_session,
                                         int mission_id)
{
	if (angel_session == 0 || hero_session == 0 || angel_session == hero_session)
		return 0;

	Impl &s = *_impl;
	auto *angel_player = s.players.resolve(s.player_of_session.find(angel_session));
	auto *hero_player = s.players.resolve(s.player_of_session.find(hero_session));
	if (angel_player == nullptr || hero_player == nullptr)
		return 0;

	// 资格筛查 (对齐 char_angel.c:181/211)
	if (!SA::Rules::isAngelCandidateEligible(angel_player->level, true))
		return 0;
	if (!SA::Rules::isHeroCandidateEligible(hero_player->level, true))
		return 0;

	// 双方不可已有未完结契约
	if (s.session_to_angel_contract.find(angel_session) != s.session_to_angel_contract.end() ||
	    s.session_to_angel_contract.find(hero_session) != s.session_to_angel_contract.end())
	{
		return 0;
	}

	// 任务挑选
	SA::Rules::AngelMissionDefinition chosen_mission{};
	if (mission_id > 0)
	{
		const auto *m = findAngelMission(mission_id);
		if (m == nullptr)
			return 0;
		chosen_mission = *m;
	}
	else if (!s.angel_missions.empty())
	{
		for (const auto &kv : s.angel_missions)
		{
			if (hero_player->level >= kv.second.min_hero_level)
			{
				chosen_mission = kv.second;
				break;
			}
		}
		if (chosen_mission.id <= 0)
			chosen_mission = s.angel_missions.begin()->second;
	}
	else
	{
		// 默认内置使命: 驱逐暗黑魔物
		chosen_mission.id = 1;
		chosen_mission.min_hero_level = SA::Rules::kMinHeroCandidateLevel;
		chosen_mission.limit_seconds = SA::Rules::kDefaultMissionLimitSeconds;
		std::snprintf(chosen_mission.name, sizeof(chosen_mission.name), "驱逐暗黑魔物");
		std::snprintf(chosen_mission.detail, sizeof(chosen_mission.detail), "前往萨姆吉尔郊外清除异动魔物");
	}

	const std::uint64_t contract_id = s.next_angel_contract_id++;
	SA::Rules::AngelContractRecord rec{};
	rec.contract_id = contract_id;
	rec.mission_id = chosen_mission.id;
	rec.stage = SA::Rules::AngelMissionStage::kWaitAnswer;
	rec.created_at_sec = static_cast<std::int64_t>(s.now_ms / 1000);
	rec.limit_seconds = chosen_mission.limit_seconds;

	std::snprintf(rec.angel_name, sizeof(rec.angel_name), "%s", angel_player->name.c_str());
	std::snprintf(rec.hero_name, sizeof(rec.hero_name), "%s", hero_player->name.c_str());
	std::snprintf(rec.angel_cdkey, sizeof(rec.angel_cdkey), "cdkey_%llu", static_cast<unsigned long long>(angel_session));
	std::snprintf(rec.hero_cdkey, sizeof(rec.hero_cdkey), "cdkey_%llu", static_cast<unsigned long long>(hero_session));

	s.angel_contracts[contract_id] = rec;
	s.session_to_angel_contract[angel_session] = contract_id;
	s.session_to_angel_contract[hero_session] = contract_id;

	return contract_id;
}

bool World::acceptAngelContract(SA::Net::SessionId angel_session)
{
	Impl &s = *_impl;
	const auto it = s.session_to_angel_contract.find(angel_session);
	if (it == s.session_to_angel_contract.end())
		return false;

	auto cit = s.angel_contracts.find(it->second);
	if (cit == s.angel_contracts.end())
		return false;

	auto &contract = cit->second;
	if (contract.stage != SA::Rules::AngelMissionStage::kWaitAnswer)
		return false;

	auto *angel_player = s.players.resolve(s.player_of_session.find(angel_session));
	if (angel_player == nullptr)
		return false;

	// 空间栏位检查: 至少要有 2 个空背包位 (char_angel.c:337)
	int free_slots = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (!angel_player->items[i].valid())
			++free_slots;
	}
	if (free_slots < 2)
		return false;

	// 生成使者信物
	SA::Model::Item angel_token{};
	angel_token.item_id = SA::Rules::kAngelTokenItemId;
	angel_token.type = 11; // ITEM_RING 首饰饰品位
	angel_token.name.assign("使者的信物");
	const int slot1 = giveItemIntoPlayer(*angel_player, angel_token, s.items);
	if (slot1 < 0)
		return false;

	// 生成勇者信物
	SA::Model::Item hero_token{};
	hero_token.item_id = SA::Rules::kHeroTokenItemId;
	hero_token.type = 11; // ITEM_RING 首饰饰品位
	hero_token.name.assign("勇者的信物");
	const int slot2 = giveItemIntoPlayer(*angel_player, hero_token, s.items);
	if (slot2 < 0)
	{
		(void)s.items.release(angel_player->items[static_cast<std::size_t>(slot1)]);
		angel_player->clearItemSlot(slot1);
		return false;
	}

	// 状态推进为 kDoing，开始计时
	contract.stage = SA::Rules::AngelMissionStage::kDoing;
	contract.created_at_sec = static_cast<std::int64_t>(s.now_ms / 1000);

	return true;
}

SA::Rules::AngelRole World::playerAngelRole(SA::Net::SessionId session) const
{
	const auto *contract = playerAngelContract(session);
	if (contract == nullptr)
		return SA::Rules::AngelRole::kNone;

	const auto *player = _impl->players.resolve(_impl->player_of_session.find(session));
	if (player == nullptr)
		return SA::Rules::AngelRole::kNone;

	return SA::Rules::getPlayerAngelRole(player->name.c_str(), *contract);
}

const SA::Rules::AngelContractRecord *World::playerAngelContract(SA::Net::SessionId session) const
{
	const auto it = _impl->session_to_angel_contract.find(session);
	if (it == _impl->session_to_angel_contract.end())
		return nullptr;

	const auto cit = _impl->angel_contracts.find(it->second);
	if (cit == _impl->angel_contracts.end())
		return nullptr;

	return &cit->second;
}

World::AngelTokenUseResult World::useAngelToken(SA::Net::SessionId session, std::int32_t token_item_id)
{
	Impl &s = *_impl;
	auto *user = s.players.resolve(s.player_of_session.find(session));
	if (user == nullptr)
		return {false, false, SA::Rules::AngelWarpResult::kTargetNotPartner, "角色不存在"};

	// 检查是否携带该信物道具
	bool has_token = false;
	for (std::size_t i = 0; i < SA::Model::kMaxItemHave; ++i)
	{
		if (const auto *item = s.items.resolve(user->items[i]))
		{
			if (item->item_id == token_item_id)
			{
				has_token = true;
				break;
			}
		}
	}
	if (!has_token)
		return {false, false, SA::Rules::AngelWarpResult::kInvalidToken, "未持有该信物道具"};

	const auto it = s.session_to_angel_contract.find(session);
	if (it == s.session_to_angel_contract.end())
		return {false, false, SA::Rules::AngelWarpResult::kTargetNotPartner, "这并不是属于你的信物，不可随便使用喔。"};

	auto &contract = s.angel_contracts[it->second];
	const auto role = SA::Rules::getPlayerAngelRole(user->name.c_str(), contract);
	if (role == SA::Rules::AngelRole::kNone)
		return {false, false, SA::Rules::AngelWarpResult::kTargetNotPartner, "这并不是属于你的信物，不可随便使用喔。"};

	const std::int64_t now_sec = static_cast<std::int64_t>(s.now_ms / 1000);
	if (contract.stage == SA::Rules::AngelMissionStage::kDoing &&
	    SA::Rules::isAngelContractExpired(now_sec, contract))
	{
		contract.stage = SA::Rules::AngelMissionStage::kTimeOver;
	}

	const std::int64_t remain_sec = SA::Rules::angelContractRemainingSeconds(now_sec, contract);
	const int remain_hours = static_cast<int>(remain_sec / 3600);
	const int remain_mins = static_cast<int>((remain_sec % 3600) / 60);

	// 查找搭档的 SessionId
	SA::Net::SessionId partner_session = 0;
	const char *partner_name = (role == SA::Rules::AngelRole::kAngel) ? contract.hero_name : contract.angel_name;
	for (const auto &conn_pair : s.conns)
	{
		const auto sid = conn_pair.first;
		if (const auto *p = s.players.resolve(s.player_of_session.find(sid)))
		{
			if (std::strcmp(p->name.c_str(), partner_name) == 0)
			{
				partner_session = sid;
				break;
			}
		}
	}

	// ── 使者信物 (2884) ──
	if (token_item_id == SA::Rules::kAngelTokenItemId)
	{
		if (role == SA::Rules::AngelRole::kAngel)
		{
			// 使者使用使者信物: 查看任务进度
			if (contract.stage == SA::Rules::AngelMissionStage::kDoing)
			{
				char buf[256];
				std::snprintf(buf, sizeof(buf), "你的使命是将勇者的信物交给 %s 并消灭魔物，时间还剩余 %d小时%d分。",
				              contract.hero_name, remain_hours, remain_mins);
				return {true, false, SA::Rules::AngelWarpResult::kSuccess, buf};
			}
			else if (contract.stage == SA::Rules::AngelMissionStage::kHeroComplete)
			{
				char buf[256];
				std::snprintf(buf, sizeof(buf), "勇者 %s 的使命已经完成了，你可以去领奖了，时间还剩余 %d小时%d分。",
				              contract.hero_name, remain_hours, remain_mins);
				return {true, false, SA::Rules::AngelWarpResult::kSuccess, buf};
			}
			else if (contract.stage == SA::Rules::AngelMissionStage::kTimeOver)
			{
				return {false, false, SA::Rules::AngelWarpResult::kTimeOver, "很可惜，使者和勇者并没有在时限内完成使命。"};
			}
			return {true, false, SA::Rules::AngelWarpResult::kSuccess, "使者契约有效。"};
		}
		else if (role == SA::Rules::AngelRole::kHero)
		{
			// 勇者使用使者信物: 瞬移至使者身边！
			if (partner_session == 0)
				return {false, false, SA::Rules::AngelWarpResult::kTargetOffline, "使者目前不在线上。"};

			const auto user_pos = playerPos(session);
			const auto partner_pos = playerPos(partner_session);
			const bool is_unlaw = (user_pos.floor == 999 || partner_pos.floor == 999);
			const auto check = SA::Rules::checkAngelTokenWarp(role, false, is_unlaw, false);
			if (check != SA::Rules::AngelWarpResult::kSuccess)
			{
				const char *err = (check == SA::Rules::AngelWarpResult::kUnlawFloor) ? "当前所在地点禁止传送。" : "传送条件不符。";
				return {false, false, check, err};
			}

			s.warpPlayer(session, partner_pos.floor, partner_pos.x, partner_pos.y);
			return {true, true, SA::Rules::AngelWarpResult::kSuccess, "传送至使者身边。"};
		}
	}
	// ── 勇者信物 (2885) ──
	else if (token_item_id == SA::Rules::kHeroTokenItemId)
	{
		if (role == SA::Rules::AngelRole::kHero)
		{
			// 勇者使用勇者信物: 查看任务进度
			if (contract.stage == SA::Rules::AngelMissionStage::kDoing)
			{
				char buf[256];
				std::snprintf(buf, sizeof(buf), "你的使命是消灭指定魔物，时间还剩余 %d小时%d分。", remain_hours, remain_mins);
				return {true, false, SA::Rules::AngelWarpResult::kSuccess, buf};
			}
			else if (contract.stage == SA::Rules::AngelMissionStage::kHeroComplete)
			{
				char buf[256];
				std::snprintf(buf, sizeof(buf), "你的使命已经完成了，可以去领奖了，时间还剩余 %d小时%d分。", remain_hours, remain_mins);
				return {true, false, SA::Rules::AngelWarpResult::kSuccess, buf};
			}
			else if (contract.stage == SA::Rules::AngelMissionStage::kTimeOver)
			{
				return {false, false, SA::Rules::AngelWarpResult::kTimeOver, "很可惜，使者和勇者并没有在时限内完成使命。"};
			}
			return {true, false, SA::Rules::AngelWarpResult::kSuccess, "勇者契约有效。"};
		}
		else if (role == SA::Rules::AngelRole::kAngel)
		{
			// 使者使用勇者信物: 瞬移至勇者身边！
			if (partner_session == 0)
				return {false, false, SA::Rules::AngelWarpResult::kTargetOffline, "勇者目前不在线上。"};

			const bool in_party = (s.partyModeOf(session) != PartyMode::kNone);
			const auto user_pos = playerPos(session);
			const auto partner_pos = playerPos(partner_session);
			const bool is_unlaw = (user_pos.floor == 999 || partner_pos.floor == 999);
			const auto check = SA::Rules::checkAngelTokenWarp(role, in_party, is_unlaw, false);
			if (check != SA::Rules::AngelWarpResult::kSuccess)
			{
				const char *err = (check == SA::Rules::AngelWarpResult::kInPartyBlocked) ? "组队中无法传送。" : (check == SA::Rules::AngelWarpResult::kUnlawFloor) ? "当前所在地点禁止传送。"
				                                                                                                                                                   : "传送条件不符。";
				return {false, false, check, err};
			}

			s.warpPlayer(session, partner_pos.floor, partner_pos.x, partner_pos.y);
			return {true, true, SA::Rules::AngelWarpResult::kSuccess, "传送至勇者身边。"};
		}
	}

	return {false, false, SA::Rules::AngelWarpResult::kInvalidToken, "无效信物类型。"};
}

bool World::completeHeroMission(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	const auto it = s.session_to_angel_contract.find(session);
	if (it == s.session_to_angel_contract.end())
		return false;

	auto &contract = s.angel_contracts[it->second];
	if (contract.stage != SA::Rules::AngelMissionStage::kDoing)
		return false;

	const std::int64_t now_sec = static_cast<std::int64_t>(s.now_ms / 1000);
	if (SA::Rules::isAngelContractExpired(now_sec, contract))
	{
		contract.stage = SA::Rules::AngelMissionStage::kTimeOver;
		return false;
	}

	contract.stage = SA::Rules::AngelMissionStage::kHeroComplete;
	return true;
}

bool World::claimAngelRewards(SA::Net::SessionId session, std::uint64_t /*npc_id*/)
{
	Impl &s = *_impl;
	auto *player = s.players.resolve(s.player_of_session.find(session));
	if (player == nullptr)
		return false;

	const auto it = s.session_to_angel_contract.find(session);
	if (it == s.session_to_angel_contract.end())
		return false;

	auto cit = s.angel_contracts.find(it->second);
	if (cit == s.angel_contracts.end())
		return false;

	auto &contract = cit->second;
	if (contract.stage != SA::Rules::AngelMissionStage::kHeroComplete)
		return false;

	const auto role = SA::Rules::getPlayerAngelRole(player->name.c_str(), contract);
	if (role == SA::Rules::AngelRole::kNone)
		return false;

	if (role == SA::Rules::AngelRole::kAngel && contract.angel_claimed)
		return false;
	if (role == SA::Rules::AngelRole::kHero && contract.hero_claimed)
		return false;

	// 回收对应信物
	const std::int32_t target_token = (role == SA::Rules::AngelRole::kAngel)
	                                      ? SA::Rules::kAngelTokenItemId
	                                      : SA::Rules::kHeroTokenItemId;

	bool item_found = false;
	for (std::size_t i = 0; i < SA::Model::kMaxItemHave; ++i)
	{
		if (const auto *item = s.items.resolve(player->items[i]))
		{
			if (item->item_id == target_token)
			{
				(void)s.items.release(player->items[i]);
				player->clearItemSlot(static_cast<int>(i));
				item_found = true;
				break;
			}
		}
	}
	if (!item_found)
		return false;

	// 发放契约嘉奖
	(void)addGold(*player, GoldReason::kQuestReward, 50000, 0, contract.contract_id, s);
	player->exp += 20000;

	// 契约结算标记
	if (role == SA::Rules::AngelRole::kAngel)
		contract.angel_claimed = true;
	else
		contract.hero_claimed = true;

	if (contract.angel_claimed && contract.hero_claimed)
	{
		contract.stage = SA::Rules::AngelMissionStage::kCompleted;
	}
	s.session_to_angel_contract.erase(session);

	return true;
}

bool World::isAngelModeActive(SA::Net::SessionId session) const
{
	return _impl->isAngelModeActive(session);
}

bool World::hasSpiritBlessing(SA::Net::SessionId session) const
{
	return _impl->hasSpiritBlessing(session);
}

} // namespace SA::World
