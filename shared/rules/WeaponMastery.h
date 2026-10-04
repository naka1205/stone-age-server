// shared/rules/WeaponMastery.h —— 武器专精熟练度与职业进阶纯函数规则层
//
// 依据石器时代 8.0 进阶职业规范与武器熟练度模型设计。
// 严守 01 §4 / 05 §1.5 纯函数约束：零外部依赖，禁止包含 <chrono> / <ctime>，严禁 I/O。

#ifndef __SA_RULES_WEAPON_MASTERY_H__
#define __SA_RULES_WEAPON_MASTERY_H__

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include "rules/Constants.h"
#include "rules/Progression.h"

namespace SA::Rules
{

// 职业进阶阶位 (8.0 职业进阶体系)
enum class ProfessionRank : std::uint8_t
{
	kNovice = 0, // 见习/未转职
	kFirst = 1,  // 一转进阶 (勇士/巫师/猎人)
	kSecond = 2, // 二转狂化 (白狼战狂/大魔导/神射手)
	kMaster = 3  // 三转宗师 (极意战神/元素使徒/逐风巡林)
};

// 职业进阶前置条件判定结果
struct PromotionCheckResult
{
	bool eligible = false;
	const char *reason = "";
};

// 武器专精被动增益
struct WeaponMasteryBonus
{
	float attack_percent = 0.0f;      // 攻击力提升百分比 (+%)
	int hit_bonus = 0;                // 命中补正点数
	float penalty_mitigation = 0.0f;  // 装备负面减敏/减防消除比例 (0.0f ~ 1.0f)
	float crit_bonus_percent = 0.0f;  // 爆击率加成 (+%)
	int initiative_bonus = 0;         // 先攻敏捷加成
	float magic_bonus_percent = 0.0f; // 魔法攻击力加成 (+%)
	float mp_cost_reduction = 0.0f;   // 气力消耗减少比例 (0.0f ~ 1.0f)
};

// 升级所需经验公式 (当前等级升到下一级所需累计经验)
std::int32_t expForNextMasteryLevel(int current_level) noexcept;

// 获取特定职业与阶位在指定武器上的熟练度等级上限
std::int32_t getWeaponMasteryCap(ProfessionClass prof, ProfessionRank rank, WeaponClass weapon) noexcept;

// 职业进阶门禁检查
PromotionCheckResult checkProfessionPromotion(
    int player_level,
    int trans_count,
    int fame,
    ProfessionClass current_prof,
    ProfessionRank current_rank,
    ProfessionClass target_prof,
    ProfessionRank target_rank) noexcept;

// 获取职业与阶位的全称描述
const char *getProfessionTitle(ProfessionClass prof, ProfessionRank rank) noexcept;

// 武器类别的中文名称描述
const char *getWeaponClassName(WeaponClass weapon) noexcept;

// 计算武器熟练度提供的属性加成与职业相性共鸣纯函数
WeaponMasteryBonus computeWeaponMasteryBonus(
    ProfessionClass prof,
    ProfessionRank rank,
    WeaponClass weapon,
    int mastery_level) noexcept;

// 模拟战斗挥击/击中/击杀获取武器熟练度经验
// player_level: 玩家等级, enemy_level: 敌方等级, hit: 是否命中, kill: 是否击倒敌人
std::int32_t calculateMasteryExpGain(
    int player_level,
    int enemy_level,
    bool hit,
    bool kill) noexcept;

// 添加经验并计算升级结果 (返回升级后的等级和剩余经验)
std::pair<std::int32_t, std::int32_t> applyMasteryExp(
    std::int32_t current_level,
    std::int32_t current_exp,
    std::int32_t gained_exp,
    std::int32_t max_cap) noexcept;

} // namespace SA::Rules

#endif // __SA_RULES_WEAPON_MASTERY_H__
