// shared/rules/PetSkill.h —— 宠技战斗侧与特殊指令 (批次 A-δ)
//
// 来源: 原版 SSRC80 `battle_event.c` 与 `pet_skill.c`
// 涵盖:
//   - 救援 / 舍身 (PETSKILL_Sacrifice / BATTLE_S_Sacrifice)
//   - 自爆 (PETSKILL_SelfExplodeAttack / BATTLE_S_Explode)
//   - 落马术 (PETSKILL_FallGround / BATTLE_S_FallGround)
//   - 大吼 (PETSKILL_Roar / BATTLE_S_Roar)
//   - 状态释放 (Barrier/Nocast/Weaken/Deeppoison)
//   - 状态回复 (PETSKILL_Refresh / BATTLE_S_Refresh)
//   - 增益 / 回复 (PETSKILL_SetMagicPet / PETSKILL_SetDuck)
//   - 属性反转 (BATTLE_AttReverse)
//   - 地球一周遁地 (BATTLE_EarthRoundHide)

#ifndef __SA_PetSkill_H__
#define __SA_PetSkill_H__

#include "rules/Combatant.h"
#include "rules/RandomSource.h"

#include <cstddef>
#include <cstdint>

namespace SA::Rules
{

enum class PetSkillSpecialKind : std::uint8_t
{
	kNone = 0,
	kSacrifice = 1,       // 救援/牺牲 (PETSKILL_Sacrifice)
	kSelfExplode = 2,     // 自爆 (PETSKILL_SelfExplodeAttack)
	kFallGround = 3,      // 落马术 (PETSKILL_FallGround)
	kRoar = 4,            // 大吼 (PETSKILL_Roar)
	kStatusMagic = 5,     // 状态释放 (Barrier/Nocast/Weaken/Deeppoison)
	kStatusRefresh = 6,   // 状态回复 (PETSKILL_Refresh)
	kSetMagicPet = 7,     // 属性增益/HP回复 (PETSKILL_SetMagicPet)
	kSetDuck = 8,         // 回避提升 (PETSKILL_SetDuck)
	kAttReverse = 9,      // 属性反转 (BATTLE_AttReverse)
	kEarthRoundHide = 10, // 地球一周遁地 (BATTLE_EarthRoundHide)
};

struct SacrificeResult
{
	std::int32_t user_loss = 0;
	std::int32_t target_heal = 0;
};

// 救援 HP 计算 (原版 battle_event.c:4715-4722)
// 攻方扣减当前生命值的 50% (至少 1 点)，守方增加相应生命值 (以最大生命值为上限)
SacrificeResult computeSacrifice(std::int32_t user_hp,
                                 std::int32_t target_hp,
                                 std::int32_t target_max_hp) noexcept;

// 自爆伤害计算 (原版 battle_event.c:6127-6132)
// 返回对目标的伤害值 (defender_hp / 2)，自身剩余生命扣至 1
std::int32_t computeSelfExplodeDamage(std::int32_t defender_hp) noexcept;

// 落马术掷骰判定 (原版 battle_event.c:6022-6035)
// 当技能造成有效伤害且守方处于骑乘状态时触发: RAND(1, 100) > 50 + resist
bool rollFallGround(bool victim_has_ride, Random &rng, int resist = 0) noexcept;

// 属性反转纯函数 (原版 battle.c:2880-2895 BATTLE_AttReverse)
// 地(0) 与 火(2) 互换，水(1) 与 风(3) 互换
void applyAttributeReverse(Combatant &c) noexcept;

// 大吼 (年兽判别) 纯函数 (原版 battle_event.c:4773-4815 BATTLE_S_Roar)
// 检查目标宠物 ID 是否在技能 option 规定的被吼退列表中
bool isRoarTarget(int target_pet_id, const int *roar_pet_ids, std::size_t roar_count) noexcept;

// 异常状态释放命中判定 (原版 battle_event.c:7575 / 7642 等 BATTLE_StatusAttackCheck)
bool rollPetStatusSkill(int success_percent, Random &rng) noexcept;

// 异常状态消除判定 (原版 battle_event.c:4737-4770 BATTLE_S_Refresh)
// target_cure_status == 0 时清除所有异常状态(全)，否则仅清除匹配的状态
bool isStatusCuredByRefresh(int current_status, int target_cure_status) noexcept;

} // namespace SA::Rules

#endif // __SA_PetSkill_H__
