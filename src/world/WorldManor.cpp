// src/world/WorldManor.cpp —— 庄园特权体系、金库税收分红与骑乘认证考核 (阶段 13)

#include "WorldImpl.h"
#include "world/Api.h"

#include <algorithm>

namespace SA::World
{

std::uint32_t World::manorTreasury(FamilyManor manor) const
{
	if (manor == FamilyManor::kNone)
		return 0;
	auto it = _impl->manor_treasuries.find(manor);
	if (it == _impl->manor_treasuries.end())
		return 0;
	return it->second.treasury_gold;
}

bool World::depositManorTreasury(FamilyManor manor, std::uint32_t amount)
{
	if (manor == FamilyManor::kNone || amount == 0)
		return false;

	auto &data = _impl->manor_treasuries[manor];
	const std::uint64_t new_total = static_cast<std::uint64_t>(data.treasury_gold) + static_cast<std::uint64_t>(amount);
	data.treasury_gold = static_cast<std::uint32_t>(std::min<std::uint64_t>(new_total, SA::Rules::kMaxManorTreasury));
	return true;
}

bool World::withdrawManorTreasury(FamilyManor manor, std::uint32_t amount)
{
	if (manor == FamilyManor::kNone || amount == 0)
		return false;

	auto it = _impl->manor_treasuries.find(manor);
	if (it == _impl->manor_treasuries.end() || it->second.treasury_gold < amount)
		return false;

	it->second.treasury_gold -= amount;
	return true;
}

int World::manorTaxRate(FamilyManor manor) const
{
	if (manor == FamilyManor::kNone)
		return SA::Rules::kDefaultTaxRatePercent;

	auto it = _impl->manor_treasuries.find(manor);
	if (it == _impl->manor_treasuries.end())
		return SA::Rules::kDefaultTaxRatePercent;
	return it->second.tax_rate_percent;
}

bool World::setManorTaxRate(FamilyManor manor, int rate_percent)
{
	if (manor == FamilyManor::kNone)
		return false;

	if (rate_percent < SA::Rules::kMinTaxRatePercent || rate_percent > SA::Rules::kMaxTaxRatePercent)
		return false;

	_impl->manor_treasuries[manor].tax_rate_percent = rate_percent;
	return true;
}

std::uint32_t World::distributeManorDividends(FamilyManor manor)
{
	if (manor == FamilyManor::kNone)
		return 0;

	const std::uint32_t fid = manorOwnerFamily(manor);
	if (fid == 0)
		return 0;

	auto fit = _impl->families.find(fid);
	if (fit == _impl->families.end() || fit->second.members.empty())
		return 0;

	auto &treasury_data = _impl->manor_treasuries[manor];
	if (treasury_data.treasury_gold == 0)
		return 0;

	// 分红池取金库资金的 50% (留 50% 维系庄园日常运转)
	const std::uint32_t pool = treasury_data.treasury_gold / 2;
	if (pool == 0)
		return 0;

	std::uint32_t total_distributed = 0;
	const int total_members = static_cast<int>(fit->second.members.size());

	for (const auto &member : fit->second.members)
	{
		const std::uint32_t share = SA::Rules::calculateDividendShare(
		    pool,
		    static_cast<SA::Rules::ManorMemberRole>(static_cast<std::int8_t>(member.role)),
		    member.contribution,
		    total_members);

		if (share == 0)
			continue;

		// 检查防溢出
		if (total_distributed + share > pool)
			break;

		total_distributed += share;

		// 若成员在线直接注入钱包，若离线通过系统邮件发放
		if (member.online && member.session > 0)
		{
			const auto pit = _impl->player_of_session.find(member.session);
			if (pit.valid())
			{
				SA::Model::Player *p = _impl->players.resolve(pit);
				if (p != nullptr)
				{
					addGold(*p, GoldReason::kManorWarReward, static_cast<std::int32_t>(share), 0, fid, *_impl);
				}
			}
		}
		else
		{
			sendMail(0, member.charname, "庄园金库分红结算", "感谢阁下为庄园发展所做贡献，附件随函附上家族金库分红石币。", -1, -1, share);
		}
	}

	treasury_data.treasury_gold -= total_distributed;
	return total_distributed;
}

bool World::canBypassRideExam(SA::Net::SessionId session, RideCertType cert_type) const
{
	const std::uint32_t fid = playerFamilyId(session);
	if (fid == 0)
		return false;

	const FamilyManor manor = familyManor(fid);
	if (manor == FamilyManor::kNone)
		return false;

	const auto req = getRideExamRequirement(cert_type);
	return req.associated_manor != FamilyManor::kNone && req.associated_manor == manor;
}

bool World::verifyRideExamQuiz(int question_id, int selected_option) const
{
	return SA::Rules::verifyRideExamAnswer(question_id, selected_option);
}

SA::Rules::ManorAuraBonus World::playerManorAura(SA::Net::SessionId session) const
{
	const std::uint32_t fid = playerFamilyId(session);
	if (fid == 0)
		return {};

	const FamilyManor manor = familyManor(fid);
	if (manor == FamilyManor::kNone)
		return {};

	const auto pit = _impl->player_of_session.find(session);
	if (!pit.valid())
		return {};

	const SA::Model::Player *p = _impl->players.resolve(pit);
	if (p == nullptr)
		return {};

	// 属地判断 (萨姆吉尔: 1000/1001, 玛丽娜斯: 2000/2001, 加加: 3000/3001, 卡鲁它那: 4000/4001)
	bool in_domain = false;
	const int floor = p->floor;
	switch (manor)
	{
	case FamilyManor::kSamo:
		in_domain = (floor == 1000 || floor == 1001);
		break;
	case FamilyManor::kMarina:
		in_domain = (floor == 2000 || floor == 2001);
		break;
	case FamilyManor::kJaja:
		in_domain = (floor == 3000 || floor == 3001);
		break;
	case FamilyManor::kKarutana:
		in_domain = (floor == 4000 || floor == 4001);
		break;
	default:
		break;
	}

	return SA::Rules::computeManorAura(static_cast<SA::Rules::ManorKind>(static_cast<std::uint8_t>(manor)), in_domain);
}

bool World::summonFamilyMembers(SA::Net::SessionId leader_session)
{
	const std::uint32_t fid = playerFamilyId(leader_session);
	if (fid == 0)
		return false;

	const FamilyRole role = playerFamilyRole(leader_session);
	if (role != FamilyRole::kLeader && role != FamilyRole::kElder)
		return false;

	const auto pit = _impl->player_of_session.find(leader_session);
	if (!pit.valid())
		return false;

	const SA::Model::Player *leader = _impl->players.resolve(pit);
	if (leader == nullptr || leader->hp <= 0)
		return false;

	auto fit = _impl->families.find(fid);
	if (fit == _impl->families.end())
		return false;

	// 向本家族全部在线成员广播召集令消息
	const std::string summon_msg = "[庄园召集令] " + std::string(leader->name.c_str()) + " 在 (" +
	                               std::to_string(leader->floor) + ", " +
	                               std::to_string(leader->x) + ", " +
	                               std::to_string(leader->y) + ") 发起家族全员紧急集结！";

	for (const auto &member : fit->second.members)
	{
		if (member.online && member.session > 0 && member.session != leader_session)
		{
			sendChat(leader_session, ChatChannel::kTalkFamily, summon_msg);
			break;
		}
	}

	return true;
}

} // namespace SA::World
