// shared/rules/WeaponMastery.cpp —— 武器专精熟练度与职业进阶纯函数规则层实现

#include "rules/WeaponMastery.h"
#include <algorithm>

namespace SA::Rules
{

std::int32_t expForNextMasteryLevel(int current_level) noexcept
{
	if (current_level < 0)
		return 50;
	if (current_level >= 100)
		return 0; // 满级
	return 50 + current_level * 25;
}

std::int32_t getWeaponMasteryCap(ProfessionClass prof, ProfessionRank rank, WeaponClass weapon) noexcept
{
	if (prof == ProfessionClass::kNone || rank == ProfessionRank::kNovice)
	{
		return 20; // 见习/无职业通用上限
	}

	switch (prof)
	{
	case ProfessionClass::kFighter:
	{
		if (weapon == WeaponClass::kAxe || weapon == WeaponClass::kSpear)
		{
			if (rank == ProfessionRank::kMaster)
				return 100;
			if (rank == ProfessionRank::kSecond)
				return 80;
			return 60;
		}
		if (weapon == WeaponClass::kClaw)
		{
			if (rank == ProfessionRank::kMaster)
				return 80;
			if (rank == ProfessionRank::kSecond)
				return 60;
			return 40;
		}
		if (weapon == WeaponClass::kBow || weapon == WeaponClass::kThrow)
		{
			return 30;
		}
		return 10;
	}
	case ProfessionClass::kHunter:
	{
		if (weapon == WeaponClass::kBow || weapon == WeaponClass::kThrow)
		{
			if (rank == ProfessionRank::kMaster)
				return 100;
			if (rank == ProfessionRank::kSecond)
				return 80;
			return 60;
		}
		if (weapon == WeaponClass::kClaw || weapon == WeaponClass::kSpear)
		{
			if (rank == ProfessionRank::kMaster)
				return 80;
			if (rank == ProfessionRank::kSecond)
				return 60;
			return 40;
		}
		if (weapon == WeaponClass::kAxe)
		{
			return 20;
		}
		return 10;
	}
	case ProfessionClass::kWizard:
	{
		if (weapon == WeaponClass::kRod)
		{
			if (rank == ProfessionRank::kMaster)
				return 100;
			if (rank == ProfessionRank::kSecond)
				return 80;
			return 60;
		}
		if (weapon == WeaponClass::kClaw)
		{
			if (rank == ProfessionRank::kMaster)
				return 80;
			if (rank == ProfessionRank::kSecond)
				return 60;
			return 40;
		}
		return 10;
	}
	default:
		return 20;
	}
}

PromotionCheckResult checkProfessionPromotion(
    int player_level,
    int trans_count,
    int fame,
    ProfessionClass current_prof,
    ProfessionRank current_rank,
    ProfessionClass target_prof,
    ProfessionRank target_rank) noexcept
{
	PromotionCheckResult res{};

	if (target_prof == ProfessionClass::kNone || target_rank == ProfessionRank::kNovice)
	{
		res.eligible = false;
		res.reason = "目标必须为有效进阶职业";
		return res;
	}

	// 晋升一转初职
	if (target_rank == ProfessionRank::kFirst)
	{
		if (current_rank != ProfessionRank::kNovice && current_prof != ProfessionClass::kNone)
		{
			res.eligible = false;
			res.reason = "已有在籍职业，不可重复转职";
			return res;
		}
		if (player_level < 30)
		{
			res.eligible = false;
			res.reason = "等级不足 30 级";
			return res;
		}
		if (fame < 50)
		{
			res.eligible = false;
			res.reason = "个人声望不足 50 点";
			return res;
		}
		res.eligible = true;
		res.reason = "符合一转入门条件";
		return res;
	}

	// 晋升二转进阶
	if (target_rank == ProfessionRank::kSecond)
	{
		if (current_prof != target_prof)
		{
			res.eligible = false;
			res.reason = "不可跨系进阶二转";
			return res;
		}
		if (current_rank != ProfessionRank::kFirst)
		{
			res.eligible = false;
			res.reason = "必须先达成一转资格";
			return res;
		}
		if (player_level < 100)
		{
			res.eligible = false;
			res.reason = "等级不足 100 级";
			return res;
		}
		if (trans_count < 1)
		{
			res.eligible = false;
			res.reason = "转生次数不足 1 转";
			return res;
		}
		if (fame < 500)
		{
			res.eligible = false;
			res.reason = "个人声望不足 500 点";
			return res;
		}
		res.eligible = true;
		res.reason = "符合二转进阶条件";
		return res;
	}

	// 晋升三转宗师
	if (target_rank == ProfessionRank::kMaster)
	{
		if (current_prof != target_prof)
		{
			res.eligible = false;
			res.reason = "不可跨系进阶宗师";
			return res;
		}
		if (current_rank != ProfessionRank::kSecond)
		{
			res.eligible = false;
			res.reason = "必须先达成二转进阶";
			return res;
		}
		if (player_level < 130)
		{
			res.eligible = false;
			res.reason = "等级不足 130 级";
			return res;
		}
		if (trans_count < 5)
		{
			res.eligible = false;
			res.reason = "必须达到 5 转至高转生";
			return res;
		}
		if (fame < 2000)
		{
			res.eligible = false;
			res.reason = "个人声望不足 2000 点";
			return res;
		}
		res.eligible = true;
		res.reason = "符合三转宗师封号条件";
		return res;
	}

	res.eligible = false;
	res.reason = "未知的进阶阶位";
	return res;
}

const char *getProfessionTitle(ProfessionClass prof, ProfessionRank rank) noexcept
{
	switch (prof)
	{
	case ProfessionClass::kFighter:
		switch (rank)
		{
		case ProfessionRank::kNovice:
			return "见习勇士";
		case ProfessionRank::kFirst:
			return "白狼勇士";
		case ProfessionRank::kSecond:
			return "白狼战狂";
		case ProfessionRank::kMaster:
			return "极意战神";
		}
		break;
	case ProfessionClass::kHunter:
		switch (rank)
		{
		case ProfessionRank::kNovice:
			return "见习猎人";
		case ProfessionRank::kFirst:
			return "追猎者";
		case ProfessionRank::kSecond:
			return "神射手";
		case ProfessionRank::kMaster:
			return "逐风巡林";
		}
		break;
	case ProfessionClass::kWizard:
		switch (rank)
		{
		case ProfessionRank::kNovice:
			return "见习巫师";
		case ProfessionRank::kFirst:
			return "暗灵法师";
		case ProfessionRank::kSecond:
			return "大魔导士";
		case ProfessionRank::kMaster:
			return "元素使徒";
		}
		break;
	default:
		break;
	}
	return "初出茅庐冒险者";
}

const char *getWeaponClassName(WeaponClass weapon) noexcept
{
	switch (weapon)
	{
	case WeaponClass::kNone:
		return "空手/徒手";
	case WeaponClass::kClaw:
		return "拳套/利爪";
	case WeaponClass::kAxe:
		return "巨斧";
	case WeaponClass::kRod:
		return "法杖/木棒";
	case WeaponClass::kSpear:
		return "长枪";
	case WeaponClass::kBow:
		return "长弓";
	case WeaponClass::kThrow:
		return "投掷兵装";
	case WeaponClass::kOther:
	default:
		return "特殊兵器";
	}
}

WeaponMasteryBonus computeWeaponMasteryBonus(
    ProfessionClass prof,
    ProfessionRank rank,
    WeaponClass weapon,
    int mastery_level) noexcept
{
	WeaponMasteryBonus bonus{};
	const int cap = getWeaponMasteryCap(prof, rank, weapon);
	const int effective_level = std::max(0, std::min(cap, mastery_level));

	// 1. 基础熟练度属性加成 (每级 +0.25% 攻击力，每 5 级 +1 命中，每 10 级减轻 10% 装备负面)
	bonus.attack_percent = static_cast<float>(effective_level) * 0.25f;
	bonus.hit_bonus = effective_level / 5;
	bonus.penalty_mitigation = std::min(1.0f, static_cast<float>(effective_level) * 0.01f);

	// 2. 职业武器专属相性共鸣 (Affinity Resonance)
	if (prof == ProfessionClass::kFighter)
	{
		if (weapon == WeaponClass::kAxe || weapon == WeaponClass::kSpear)
		{
			bonus.attack_percent += 10.0f;    // 狂暴近战 +10% 伤害
			bonus.crit_bonus_percent += 5.0f; // 爆击率 +5%
		}
	}
	else if (prof == ProfessionClass::kHunter)
	{
		if (weapon == WeaponClass::kBow || weapon == WeaponClass::kThrow)
		{
			bonus.initiative_bonus += 15; // 追猎先攻敏捷 +15
			bonus.hit_bonus += 10;        // 鹰眼射击命中额外 +10
		}
	}
	else if (prof == ProfessionClass::kWizard)
	{
		if (weapon == WeaponClass::kRod)
		{
			bonus.magic_bonus_percent += 15.0f; // 咒术魔法增伤 +15%
			bonus.mp_cost_reduction += 0.10f;   // 气力消耗减少 10%
		}
	}

	return bonus;
}

std::int32_t calculateMasteryExpGain(
    int player_level,
    int enemy_level,
    bool hit,
    bool kill) noexcept
{
	std::int32_t gain = hit ? 5 : 1;
	if (kill)
	{
		gain += 10;
		if (enemy_level > player_level)
		{
			gain += std::min(15, (enemy_level - player_level) * 2);
		}
	}
	return gain;
}

std::pair<std::int32_t, std::int32_t> applyMasteryExp(
    std::int32_t current_level,
    std::int32_t current_exp,
    std::int32_t gained_exp,
    std::int32_t max_cap) noexcept
{
	std::int32_t lvl = current_level;
	std::int32_t exp = current_exp + gained_exp;

	while (lvl < max_cap)
	{
		const std::int32_t needed = expForNextMasteryLevel(lvl);
		if (needed <= 0 || exp < needed)
			break;

		exp -= needed;
		lvl++;
	}

	if (lvl >= max_cap)
	{
		lvl = max_cap;
		exp = 0; // 满级后经验归零封顶
	}

	return {lvl, exp};
}

} // namespace SA::Rules
