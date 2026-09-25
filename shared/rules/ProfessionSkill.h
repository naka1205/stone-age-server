// shared/rules/ProfessionSkill.h —— 职业技能映射表与直攻计算 (批次 A-γ2)
//
// ★★ 与 Battle.h / Progression.h 同为 D2 双端共享的纯函数层 (shared/rules)。
//    依据 00-architecture.md §161-171 与原版 profession_skill.c/battle.c 权威实现:
//    64 个原版职技包含:
//      - 53 个同构薄包装 (profession_common_fun -> 指令登记)
//      - 9 个被动技能空指令 (weapon_focus, avoid, deflect, docile 等)
//      - 2 个非战斗真实现 (PROFESSION_track, PROFESSION_escape)
//
// ⚠️ shared/ 只依赖标准库与同一 shared 目录下的头文件。

#ifndef __SA_ProfessionSkill_H__
#define __SA_ProfessionSkill_H__

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "rules/Combatant.h"
#include "rules/Progression.h"
#include "rules/RandomSource.h"

namespace SA::Rules
{

// 职业技能分类
enum class ProfessionSkillCategory : std::uint8_t
{
	kDirectAttack = 0, // 战斗直攻系 (爆击、连环、双重、弱点、混乱、骑乘、濒死、掠夺等)
	kPassive = 1,      // 被动技能 (武器专精、二刀流、回避、格挡、自给自足、驯养、元素熟练度等)
	kNonCombat = 2,    // 非战斗技能 (追寻敌踪、回避战斗)
	kMagic = 3,        // 魔法攻击/咒术系 (火山泉、火球、冰爆术等)
	kUtility = 4       // 辅助/状态/结界系 (自然召唤、结界、状态回复等)
};

// 64 项职业技能 ID 枚举 (对应原版 64 技能函数)
enum class ProfessionSkillId : std::uint32_t
{
	// ── 巫师 (21 项) ──
	kVolcanoSprings = 1,   // 火山泉
	kFireBall = 2,         // 火星球
	kFireSpear = 3,        // 火凤枪
	kSummonThunder = 4,    // 天劫雷
	kCurrent = 5,          // 雷霆附体/奔流
	kStorm = 6,            // 暴风雨
	kIceArrow = 7,         // 冰箭
	kIceCrack = 8,         // 冰爆术
	kIceMirror = 9,        // 冰镜
	kDoom = 10,            // 末日审判
	kBlood = 11,           // 嗜血术
	kBloodWorms = 12,      // 嗜血蛊
	kSign = 13,            // 一针见血
	kFireEnclose = 14,     // 火附体
	kIceEnclose = 15,      // 冰附体
	kThunderEnclose = 16,  // 雷附体
	kFirePractice = 17,    // 火魔法熟练度 (被动)
	kIcePractice = 18,     // 水魔法熟练度 (被动)
	kThunderPractice = 19, // 风魔法熟练度 (被动)
	kEnclose = 20,         // 附身
	kTranspose = 21,       // 移形换位

	// ── 勇士 (18 项) ──
	kChainAtk = 22,      // 连环攻击 (直攻)
	kAvoid = 23,         // 回避 (被动)
	kWeaponFocus = 24,   // 武器专精 (被动)
	kReback = 25,        // 状态回复
	kBrust = 26,         // 爆击 (直攻)
	kChainAtk2 = 27,     // 双重攻击 (直攻)
	kScapegoat = 28,     // 舍己为友
	kEnrage = 29,        // 激怒
	kEnergyCollect = 30, // 蓄力/集气
	kFocus = 31,         // 专注战斗
	kShieldAttack = 32,  // 盾击 (直攻)
	kDualWeapon = 33,    // 二刀流 (被动)
	kDeflect = 34,       // 格挡 (被动)
	kThroughAttack = 35, // 贯通攻击
	kCavalry = 36,       // 骑乘攻击 (直攻)
	kDeadAttack = 37,    // 濒死攻击 (直攻)
	kConvolute = 38,     // 旋风斩
	kChaos = 39,         // 混乱攻击 (直攻)

	// ── 猎人 (22 项) ──
	kTrack = 40,          // 追寻敌踪 (非战斗)
	kEscape = 41,         // 回避战斗 (非战斗)
	kDocile = 42,         // 驯养 (被动)
	kTrap = 43,           // 陷阱
	kEnragePet = 44,      // 激怒宠物
	kDragnet = 45,        // 天罗地网
	kEntwine = 46,        // 树根缠绕
	kAutarky = 47,        // 自给自足 (被动)
	kPlunder = 48,        // 掠夺 (直攻)
	kToxinWeapon = 49,    // 毒刃 (被动)
	kResistFire = 50,     // 抗火
	kResistIce = 51,      // 抗水
	kResistThunder = 52,  // 抗风
	kResistFit = 53,      // 全抗 (自然抗性)
	kCallNature = 54,     // 自然召唤
	kBoundary = 55,       // 结界
	kGResistFire = 56,    // 极·抗火
	kGResistIce = 57,     // 极·抗水
	kGResistThunder = 58, // 极·抗风
	kAttackWeak = 59,     // 弱点攻击 (直攻)
	kInstigate = 60,      // 挑拨
	kOblivion = 61,       // 遗忘

	// ── 共通 (3 项) ──
	kFullMp = 62,     // 补气
	kStrongBack = 63, // 坚毅
	kStrengthen = 64  // 强化
};

// 单项职业技能信息描述
struct ProfessionSkillInfo
{
	std::uint32_t skill_id = 0;
	const char *name = "";
	ProfessionClass profession = ProfessionClass::kNone;
	ProfessionSkillCategory category = ProfessionSkillCategory::kDirectAttack;
	std::int32_t raw_command = 0; // 原版 BATTLE_COM_S_* 指令码
};

// 映射表访问接口
std::size_t getProfessionSkillTableSize() noexcept;
const ProfessionSkillInfo *getProfessionSkillTable() noexcept;
const ProfessionSkillInfo *findProfessionSkillById(std::uint32_t skill_id) noexcept;
const ProfessionSkillInfo *findProfessionSkillByName(std::string_view name) noexcept;

// 直攻系职技参数计算结果
struct ProfSkillDirectParams
{
	bool is_direct = false;
	int hits = 1;             // 攻击段数
	int damage_percent = 100; // 伤害倍率 (100 = 100%)
	int attack_percent = 0;   // 攻击力提升百分比 (+%)
	int quick_percent = 0;    // 敏捷修正百分比 (+/- %)
	int apply_status = 0;     // 状态异常附着 (0 = 无, 7 = 混乱等)
	int status_turns = 0;     // 状态持续回合
};

// 直攻系职技参数纯函数计算。
// 1:1 移植 battle_event.c:8000-8160 (battle_profession_attack_fun)。
//
// 规则:
//   - 爆击 (BRUST): attack_percent = skill_level * 3
//   - 连环攻击 (CHAIN_ATK): 摇号 hit = (skill_level + (skill_level % 10 != 0 ? 1 : 0)) * 5 + 15
//                          若 rand(1, 100) <= hit 则 hits = 2，否则 hits = 1
//   - 双重攻击 (CHAIN_ATK_2): hits = 2, attack_percent = skill_level * 2
//   - 弱点攻击 (ATTACK_WEAK): 若目标为宠物或敌人，attack_percent = skill_level * 2 + 10；quick_percent = -(skill_level + 10)
//   - 混乱攻击 (CHAOS): damage_percent = 70, hits = (skill_level >= 10 ? 5 : (skill_level >= 5 ? 4 : 3)),
//                      apply_status = 7 (CONFUSION), status_turns = 3
//   - 其他直攻系技能 (CAVALRY, DEAD_ATTACK, SHIELD_ATTACK, PLUNDER): 标记 is_direct = true 且默认 1 段 100% 伤害
ProfSkillDirectParams computeProfSkillDirectParams(std::uint32_t skill_id,
                                                   int skill_level,
                                                   CombatantKind target_kind,
                                                   Random &rng) noexcept;

} // namespace SA::Rules

#endif // __SA_ProfessionSkill_H__
