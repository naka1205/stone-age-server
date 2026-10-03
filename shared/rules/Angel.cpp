// shared/rules/Angel.cpp —— S11 精灵/天使系统纯函数规则实现
//
// 依据 00 §1.2 / 01 §4: shared/ 纯度规范与零堆分配, 兼容 C++17。

#include "rules/Angel.h"

#include <algorithm>
#include <cstring>

namespace SA::Rules
{

bool isAngelCandidateEligible(int level, bool event_flag_qualified) noexcept
{
	return level >= kMinAngelCandidateLevel && event_flag_qualified;
}

bool isHeroCandidateEligible(int level, bool event_flag_qualified) noexcept
{
	return level >= kMinHeroCandidateLevel && event_flag_qualified;
}

bool isAngelContractExpired(std::int64_t now_sec, const AngelContractRecord &contract) noexcept
{
	if (contract.created_at_sec <= 0 || contract.limit_seconds <= 0)
		return false;
	return now_sec >= (contract.created_at_sec + contract.limit_seconds);
}

std::int64_t angelContractRemainingSeconds(std::int64_t now_sec, const AngelContractRecord &contract) noexcept
{
	if (contract.created_at_sec <= 0 || contract.limit_seconds <= 0)
		return 0;
	const std::int64_t expire_at = contract.created_at_sec + contract.limit_seconds;
	if (now_sec >= expire_at)
		return 0;
	return expire_at - now_sec;
}

AngelRole getPlayerAngelRole(const char *player_name, const AngelContractRecord &contract) noexcept
{
	if (player_name == nullptr || player_name[0] == '\0')
		return AngelRole::kNone;
	if (std::strcmp(player_name, contract.angel_name) == 0)
		return AngelRole::kAngel;
	if (std::strcmp(player_name, contract.hero_name) == 0)
		return AngelRole::kHero;
	return AngelRole::kNone;
}

AngelWarpResult checkAngelTokenWarp(AngelRole user_role, bool is_in_party, bool is_unlaw_floor, bool has_drop_items) noexcept
{
	if (user_role == AngelRole::kNone)
		return AngelWarpResult::kTargetNotPartner;
	if (is_unlaw_floor)
		return AngelWarpResult::kUnlawFloor;
	if (is_in_party)
		return AngelWarpResult::kInPartyBlocked;
	if (has_drop_items)
		return AngelWarpResult::kHasDropItem;
	return AngelWarpResult::kSuccess;
}

int calculateSpiritBlessingDefenseBonus(int base_defense, bool has_spirit_blessing) noexcept
{
	if (!has_spirit_blessing || base_defense <= 0)
		return 0;
	return base_defense * 15 / 100;
}

int calculateSpiritBlessingDamageReduction(int incoming_damage, bool has_spirit_blessing) noexcept
{
	if (!has_spirit_blessing || incoming_damage <= 0)
		return incoming_damage;
	const int reduced = incoming_damage - (incoming_damage * 10 / 100);
	return reduced > 0 ? reduced : 1;
}

bool isEncounterSuppressedByAngel(bool is_angel_equipped, bool is_angel_mode) noexcept
{
	return is_angel_equipped || is_angel_mode;
}

} // namespace SA::Rules
