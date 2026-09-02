// shared/rules/battle.cpp —— L3 战斗结算的实现(阶段 0.1/1.1,批次 0)
//
// ★★ 本文件是 DR-TS6「一遍到位」的落地:原版 `battle/*.c` 只作**只读的规格说明书**,
//    不存在"改造后的中间态 C 代码"。四步改造在这里表现为**四条形态要求**
//    (00-architecture.md §9.0.3),而不是四轮编辑:
//
//      ① 原版靠 10 个 `g*` 隐式传参的量 → 显式参数(`RulesConfig` / 函数入参)
//      ② 原版 `CHAR_set*` 写世界状态   → 追加事件,由调用方应用
//      ③ 原版拼串 + 发包               → 追加事件,序列化留给调用方
//      ④ 原版 `RAND()` / `rand()`      → 注入的 `IRandom&`
//
// ⚠️ **四步都不动公式**(05-battle.md §1.2)。本文件里每一处与原版的数值差异
//    都必须有一条 DR 或一条实测注记撑着 —— 没有的一律照原样。
//
// ── 移植来源 ────────────────────────────────────────────────────
//   `stoneage85/…/Serv/gmsv/battle/battle_event.c`
//       :754  BATTLE_DuckCheck      → RollDodge
//       :908  BATTLE_AttrCalc       → ApplyElementMatrix
//       :955  BATTLE_GetAttr        → Combatant::elements + NoneElement()
//       :1031 BATTLE_FieldAttAdjust → FieldPower
//       :1066 BATTLE_AttrAdjust     → ApplyElementMatrix
//       :1162 BATTLE_DamageCalc     → ComputeDamage
//   `stoneage85/…/Serv/gmsv/battle/battle.c`
//       :8059 BATTLE_CanMoveCheck   → CheckCanAct
//
// ── 宏门控的处置(D7:8.0 单基线,不迁移宏)──────────────────────
//   已对 `stoneage-plan/tools/macros_80.json`(641 个 8.0 开启宏)逐条核对:
//   `_BATTLE_NEWPOWER` `_DAMMAGE_CALC` `_MAGIC_SUPERWALL` `_PETSKILL_REGRET`
//   `_EQUIT_NEGLECTGUARD` `_PROFESSION_ADDSKILL` `_ADD_DEAMGEDEFC`
//   `_BATTLE_PROPERTY` `_PSKILL_MDFYATTACK` `_NPCENEMY_ADDPOWER`
//   `_PETSKILL_SETDUCK` `_EQUIT_HITRIGHT` `_PROFESSION_SKILL` **全部为开**
//   ⇒ 一律实现**开启态**,且**不留 `#if`**(02 §1.1 规则 4)。
//   `_PETSKILL_NEW_PASSIVE` `_MULTIPLAYER_` **为关** ⇒ 对应分支不实现
//   (与 DR-BT14「被动宠技 B80 命中 0/12」一致)。

#include "rules/battle.h"

#include <cmath>

namespace sg::rules {
namespace {

// ★ 原版 `attack` / `defense` 是 **float**(`battle_event.c:1164`),不是 double。
//   三分段的分支边界对精度敏感 ⇒ 用 float 保留原类型语义。
using f32 = float;

// 原版 `D_16` / `D_8`(`battle_event.c` 与 `battle_magic.c` 两处定义一致)。
constexpr double kD16 = 1.0 / 16;
constexpr double kD8  = 1.0 / 8;

// `Combatant::elements` 是**地水火风**顺序(constants.h 的 `Element`)。
// 原版 `T_pow` 同序,但 `BATTLE_AttrCalc` 的形参是**火水地风**序 ——
// ⚠️ 移植时若照抄调用点的参数顺序会整体错位。这里统一用 `Element` 下标,不复刻那次换序。
constexpr int kEarth = static_cast<int>(Element::kEarth);
constexpr int kWater = static_cast<int>(Element::kWater);
constexpr int kFire  = static_cast<int>(Element::kFire);
constexpr int kWind  = static_cast<int>(Element::kWind);
// ⚠️ 没有 kNone 常量:无属性余量不是 `elements[]` 的一项,是 `NoneElement()` 推导量
//    (见 combatant.h)。取 `elements[4]` 会越界。

// 场地属性系数(`BATTLE_FieldAttAdjust`,`battle_event.c:1031`)。
//
// ★ 原式:power = AJ_BOTTOM + T_pow[该属] · att_pow · 0.01 · 0.01 · AJ_PLUS
//   `AJ_BOTTOM == AJ_PLUS == 0.5`(= kFieldPowBase)⇒ 分母最小 0.5,不会除零。
//
// ⚠️⚠️ **`field_attribute` 用的是 `FieldAttribute` 编码,不是 `Element`。**
//   `BATTLE_ATTR_NONE == 0` 而 `Element::kEarth == 0` —— 两者的 0 含义相反。
//   混用会让「无属性场地」被当成「地属性场地」⇒ 伤害在特定属性组合下 ×2。
//   (这不是假想:2026-08-31 移植时正是这么错的,被相克手算用例接住。)
f32 FieldPower(std::uint8_t field_attribute, int att_pow,
               const std::int32_t (&elems)[4]) noexcept {
  double value = 0.0;
  switch (static_cast<FieldAttribute>(field_attribute)) {
    case FieldAttribute::kEarth: value = elems[kEarth]; break;
    case FieldAttribute::kWater: value = elems[kWater]; break;
    case FieldAttribute::kFire:  value = elems[kFire];  break;
    case FieldAttribute::kWind:  value = elems[kWind];  break;
    case FieldAttribute::kNone:
    default:
      return static_cast<f32>(kFieldPowBase);
  }
  return static_cast<f32>(kFieldPowBase +
                          value * att_pow * 0.01 * 0.01 * kFieldPowBase);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════
//  CheckCanAct —— DR-BT5 的唯一真源
// ═══════════════════════════════════════════════════════════════════
//
// 1:1 移植 `BATTLE_CanMoveCheck`(`battle.c:8059`),8.0 下全部门控宏为开
// ⇒ 8 项否决全部生效。
//
// ⚠️★ **判定顺序按源码,不按枚举值顺序。** 返回的是**第一个命中**的原因,
//    而 IDL 里 `CannotActReason` 的编号顺序(晕眩 4 / 天罗 5 / 魔障 6)与
//    源码的判定顺序(魔障在晕眩之前)**不同**。
//    因 §4.1 全局互斥,同时命中多项的情形只可能出现在
//    「集气中 + 某状态」或「天罗 + 某状态」上,但顺序仍须固定 —— 否则不可回放。
sg::domain::CannotActReason CheckCanAct(const Combatant& c) noexcept {
  using sg::domain::BattleStatus;
  using sg::domain::CannotActReason;

  const auto st = static_cast<BattleStatus>(c.status);

  // 源码顺序:麻痹 → 石化 → 睡眠 → 魔障 → 晕眩 → 天罗 → 雷附体 → 集气
  if (st == BattleStatus::BATTLE_ST_PARALYSIS) return CannotActReason::CANNOT_ACT_PARALYSIS;
  if (st == BattleStatus::BATTLE_ST_STONE)     return CannotActReason::CANNOT_ACT_STONE;
  if (st == BattleStatus::BATTLE_ST_SLEEP)     return CannotActReason::CANNOT_ACT_SLEEP;
  if (st == BattleStatus::BATTLE_ST_BARRIER)   return CannotActReason::CANNOT_ACT_BARRIER;
  if (st == BattleStatus::BATTLE_ST_DIZZY)     return CannotActReason::CANNOT_ACT_DIZZY;
  if (st == BattleStatus::BATTLE_ST_DRAGNET)   return CannotActReason::CANNOT_ACT_DRAGNET;
  if (st == BattleStatus::BATTLE_ST_T_ENCLOSE) return CannotActReason::CANNOT_ACT_T_ENCLOSE;

  // 第 8 项是独立字段,不是 status 槽值(见 combatant.h 的 charging_turns)。
  if (c.charging_turns > 0) return CannotActReason::CANNOT_ACT_CHARGING;

  return CannotActReason::CANNOT_ACT_NONE;
}

// ═══════════════════════════════════════════════════════════════════
//  四属性相克
// ═══════════════════════════════════════════════════════════════════
//
// ★★ **为什么是「给 damage、返回 damage」而不是「返回一个系数」**:
//
// 原版 `BATTLE_AttrAdjust`(`:1119`)先把 damage 乘进攻方属性向量
//     for(i=0;i<5;i++) At_pow[i] *= damage;
// 再调 `BATTLE_AttrCalc`,而后者**返回 int**(`:952 return (iRet * D_ATTR)`)
// ⇒ 链路上有**两次整数截断**(每属一次 + 返回一次)。
//
// 若改成「返回 double 系数,由调用方乘 damage」,数学上等价但**截断位置变了**
// ⇒ 与原版逐位不同。D2 的可回放性要求逐位一致 ⇒ 保留原形状。
//
// ⚠️ 这正是「四步不动公式」的具体含义:形状也算公式的一部分。
std::int32_t ApplyElementMatrix(const BattleField& field,
                                const Combatant& attacker,
                                const Combatant& defender,
                                std::int32_t damage) noexcept {
  // 攻守双方的五元属性向量(第 5 项是无属性余量,由 NoneElement() 推导)。
  const std::int32_t at[kElementCount] = {
      attacker.elements[0], attacker.elements[1], attacker.elements[2],
      attacker.elements[3], attacker.NoneElement()};
  const std::int32_t df[kElementCount] = {
      defender.elements[0], defender.elements[1], defender.elements[2],
      defender.elements[3], defender.NoneElement()};

  // 场地系数在 damage 乘入之前取(原版 `:1114`,读的是未乘 damage 的 T_pow)。
  const f32 at_field = FieldPower(field.field_attribute, 100, attacker.elements);
  const f32 df_field = FieldPower(field.field_attribute, 100, defender.elements);

  // ★ 原版 `:1120` —— damage 先乘进攻方向量。
  std::int32_t at_scaled[kElementCount];
  for (int i = 0; i < kElementCount; ++i) at_scaled[i] = at[i] * damage;

  // ★ 原版 `BATTLE_AttrCalc`:每个攻方分量对全部守方分量加权求和,
  //   **结果赋值回 int ⇒ 每属截断一次**。
  std::int64_t total = 0;
  for (int a = 0; a < kElementCount; ++a) {
    double row = 0.0;
    for (int d = 0; d < kElementCount; ++d) {
      row += static_cast<double>(at_scaled[a]) * df[d] * kElementMatrix[a][d];
    }
    total += static_cast<std::int32_t>(row);   // ← 截断 ①(原版 `My_* = …` 赋回 int)
  }

  // ★ 原版 `:952 return (iRet * D_ATTR)` —— 函数返回 int ⇒ 截断 ②。
  //   D_ATTR = 1.0/(ATTR_MAX*ATTR_MAX) = 1/10000 = 1/kElementDivisor。
  damage = static_cast<std::int32_t>(static_cast<double>(total) / kElementDivisor);

  // ★ 原版 `:1129 damage *= (At_FieldPow / Df_FieldPow)` —— damage 是 int ⇒ 截断 ③。
  damage = static_cast<std::int32_t>(static_cast<double>(damage) *
                                     (static_cast<double>(at_field) / df_field));
  return damage;
}

// 纯系数视图 —— **仅供测试与客户端展示,结算路径不得调用**。
//
// ⚠️★ 它与 `ApplyElementMatrix` 不是同一条路径:本函数没有那三次整数截断。
//    两者只在"数学上"等价,逐位并不等价。
//    ⇒ 谁要是拿它去算伤害,就复活了「同一语义两份实现」这个 bug 类
//      (与 §1.1 协议号两端漂移同源)。测试里断言的是**量纲**,不是逐位一致。
double ElementCoefficient(const Combatant& attacker, const Combatant& defender) noexcept {
  const std::int32_t at[kElementCount] = {
      attacker.elements[0], attacker.elements[1], attacker.elements[2],
      attacker.elements[3], attacker.NoneElement()};
  const std::int32_t df[kElementCount] = {
      defender.elements[0], defender.elements[1], defender.elements[2],
      defender.elements[3], defender.NoneElement()};

  double sum = 0.0;
  for (int a = 0; a < kElementCount; ++a)
    for (int d = 0; d < kElementCount; ++d)
      sum += static_cast<double>(at[a]) * df[d] * kElementMatrix[a][d];
  return sum / kElementDivisor;
}

// ═══════════════════════════════════════════════════════════════════
//  伤害主公式
// ═══════════════════════════════════════════════════════════════════
//
// 1:1 移植 `BATTLE_DamageCalc`(`battle_event.c:1162`)。
// 与 05-battle.md §3.1 的三处出入已在 battle.h 的移植注记中逐条记明。
std::int32_t ComputeDamage(const BattleField& field,
                           const Combatant& attacker,
                           const Combatant& defender,
                           const RulesConfig& config,
                           IRandom& rng) noexcept {
  // ── 第 1 步:取攻防 ──────────────────────────────────────────
  //
  // 骑宠合成(§3.1)。⚠️ 近战 0.8/0.8、投掷 1.0/0.4 —— 投掷判据是武器类。
  f32 attack;
  if (!attacker.has_ride) {
    attack = static_cast<f32>(attacker.attack);
  } else if (attacker.mods.weapon == WeaponClass::kThrow) {
    attack = static_cast<f32>(kRideThrowSelf * attacker.attack +
                              kRideThrowPet * attacker.ride_attack);
  } else {
    attack = static_cast<f32>(kRideMeleeSelf * attacker.attack +
                              kRideMeleePet * attacker.ride_attack);
  }

  // ★ `_BATTLE_NEWPOWER` 在 8.0 **开** ⇒ defense = 0.70 × DEF。
  //   ⚠️ 关闭态是完全不同的公式(0.45·DEF + 0.2·QUICK + 0.1·FIXVITAL),
  //     D7 裁定单基线 ⇒ **不为它留分支**(constants.h 已注明)。
  f32 defense;
  if (!defender.has_ride) {
    defense = static_cast<f32>(defender.defense * kDefenseCoefNewPower);
  } else {
    defense = static_cast<f32>((defender.defense + defender.ride_defense) * 0.5 *
                               kDefenseCoefNewPower);
  }

  // ── 第 2 步:防御修正(顺序即语义,不可重排)──────────────────
  //
  // ⚠️★ 全部是 **float 运算**。05 §3.1 称「`rand()%10 == 0` 时该项 = 2/100 = 0
  //    (整数除法)」——**不成立**,`attack`/`defense` 是 float ⇒ 该项 = 0.02。
  //    见 battle.h 移植注记 ①。

  // 铁壁防御(`_MAGIC_SUPERWALL`,8.0 开)。基数是 OTHERSTATUSNUMS。
  if (defender.mods.super_wall) {
    const f32 def = static_cast<f32>(
        (static_cast<double>(defender.other_status_nums) + rng.RandMod(20)) / 100);
    defense += defense * def;
  }
  // 怪物能力值修正(`_NPCENEMY_ADDPOWER`,8.0 开)。守方、攻方各一次。
  //
  // ⚠️★ 显式写 `static_cast<f32>(rng.RandMod(10))` 而不是靠隐式转换:原版是
  //    `defense*(rand()%10)`,C 的通常算术转换把 int 提升到 float。行为一致,
  //    但 `-Wconversion` 会对隐式转换告警 —— 而告警口径正是为了抓
  //    「原版大量 int/float 混算」这类问题(见 cmake/SgWarnings.cmake)。
  //    ⇒ 把"这里的混算是有意的"写成代码,而不是让它混在告警噪声里。
  if (defender.IsEnemy()) {
    defense += (defense * static_cast<f32>(rng.RandMod(10)) + 2.0f) / 100.0f;
  }
  if (attacker.IsEnemy()) {
    attack += (attack * static_cast<f32>(rng.RandMod(10)) + 2.0f) / 100.0f;
  }

  // 守方石化 ⇒ 防御翻倍。
  if (static_cast<sg::domain::BattleStatus>(defender.status) ==
      sg::domain::BattleStatus::BATTLE_ST_STONE) {
    defense *= 2.0f;
  }

  // 攻方用「舍己」(`_PETSKILL_REGRET`,8.0 开)⇒ 防御直接取 WORKFIXTOUGH,忽略装备。
  //
  // ⚠️★ **本批次不实现,且这是个有意的空缺,不是遗漏。**
  //    原版判据是攻方指令码 `== BATTLE_COM_S_REGRET / REGRET2`(`:1212`),
  //    而「舍己」是**宠物技能**(B 批次)。批次 0 只覆盖普攻链路 ⇒ 该分支不可达。
  //    ⇒ 等 B 批次把宠技指令接进来时,以「攻方指令」作为入参补上,
  //      **不要**在这里用 `defender.fix_tough > 0` 之类间接判据代替 ——
  //      那会让「守方恰好有 fix_tough」误触发忽略装备。
  //    `Combatant::fix_tough` 字段现已存在但**无人写**,保持 0。

  // 无视防御 N%(`_EQUIT_NEGLECTGUARD`,8.0 开)。
  // ⚠️★ 原版判据是 `> 1`(`:1218`),不是 `> 0` ⇒ N == 1 时**不生效**。
  if (attacker.mods.ignore_defense_percent > 1) {
    const f32 defp =
        static_cast<f32>(1.0 - attacker.mods.ignore_defense_percent / 100.0);
    defense = defense * defp;
  }

  // ── 第 3 步:三分段主公式 ────────────────────────────────────
  //
  // ⚠️★ **分支顺序照源码 `:1225-1235`,不照文档。** 文档把 `defense > attack`
  //    列在最前,源码是第二个。三条件互斥时结果相同,但 `else if` 的短路顺序
  //    是可回放性的一部分 —— 照源码。
  //
  // ★★ **没有"窄缝"。** 见 battle.h 移植注记 ②:实测 0 组窄缝 / 171,429 组重叠,
  //    重叠区间由**第二分支**接管 ⇒ 边界值走 `RAND(0, attack/16)`,不是 0。
  std::int32_t damage = 0;
  if (defense <= attack && attack < (defense * 8.0 / 7.0)) {
    damage = rng.Rand(0, static_cast<int>(attack * kD16));
  } else if (defense > attack) {
    damage = rng.Rand(0, 1);
  } else if (attack >= (defense * 8 / 7)) {
    const f32 k0 = static_cast<f32>(rng.Rand(0, static_cast<int>(attack * kD8)) -
                                    attack * kD16);
    damage = static_cast<std::int32_t>((attack - defense) * kDamageRate + k0);
  }

  // ── 第 4 步:四属性相克 ─────────────────────────────────────
  damage = ApplyElementMatrix(field, attacker, defender, damage);

  // ── 第 5 步:四属结界 ───────────────────────────────────────
  //
  // ⚠️ 原版按**地水火风顺序 `else if` 串联** ⇒ **只有第一个命中的结界生效,不叠加**
  //    (`:1243-1255`)。这是原版行为,不是 bug ⇒ 保留短路。
  //    ⚠️ 结界的强度字段(`CHAR_WORKFIX*AT_BOUNDARY` 的高 16 位)尚未进入
  //    `Combatant` 的输入面 —— 它属职业技能链路(A 批次),不在批次 0 内。
  //    ⇒ **本批次不实现,留待 A 批次**;此处显式记明,避免被读成"已覆盖"。

  // ── 第 6 步:附加伤害 / 减免(`_ADD_DEAMGEDEFC`,8.0 开)──────
  //   同上,`CHAR_WORKOTHERDMAGE` / `CHAR_WORKOTHERDEFC` 属装备扩展面,
  //   不在批次 0 的输入面内。⇒ 本批次不实现。
  if (damage < 0) damage = 0;

  // ── 第 7 步:全局伤害系数(`_DAMMAGE_CALC`,8.0 开)────────────
  //
  // ★★ 默认 **70 不是 100** ⇒ 8.0 投产下全部物理伤害统一乘 0.70。
  //    写成 100 会让全局伤害偏高 43%。
  return damage * config.damage_calc_percent / 100;
}

// ═══════════════════════════════════════════════════════════════════
//  回避
// ═══════════════════════════════════════════════════════════════════
//
// 1:1 移植 `BATTLE_DuckCheck`(`battle_event.c:754`)。
//
// ⚠️★ 原版在**闪避成功时**嵌了 `PROFESSION_SKILL_LVEVEL_UP` 副作用(`:899`)——
//    这正是四步改造第②步要剥离的典型:**判定函数不得写世界状态**。
//    ⇒ 本实现只返回判定结果;技能升级由调用方按事件处理。
bool RollDodge(const Combatant& attacker,
               const Combatant& defender,
               bool defender_guarding,
               bool defender_casting_spell,
               const RulesConfig& config,
               IRandom& rng) noexcept {
  // ── 六道前置否决 + 一道必闪 ─────────────────────────────────
  //
  // ⚠️ 05 §3.2 的清单**漏了 ABIO**(见 combatant.h 的 mods.abio)。实际是:
  //   ① 攻方集气完成  ② 守方防御  ③ 守方有反应类状态  ④ 守方不能行动
  //   ⑤ NODUCK       ⑥ ABIO      然后 ⑦ 必闪技 ⇒ 直接 true
  //
  // ★ 顺序照源码 `:766-804`。因每道都是 `return`,顺序不影响结果,
  //   但影响**读代码的人对优先级的理解** ⇒ 仍照原样。
  if (attacker.charge_ready) return false;   // ①
  if (defender_guarding) return false;       // ②
  if (defender.damage_react > 0) return false;  // ③

  // ④ 守方不能行动。★ `_PROFESSION_ADDSKILL`(8.0 开)有一处例外:
  //   **集气中仍可闪避**,除非同时处于天罗地网或晕眩(`:779-788`)。
  if (CheckCanAct(defender) != sg::domain::CannotActReason::CANNOT_ACT_NONE) {
    const bool charging = defender.charging_turns > 0;
    const auto st = static_cast<sg::domain::BattleStatus>(defender.status);
    const bool pinned = (st == sg::domain::BattleStatus::BATTLE_ST_DRAGNET) ||
                        (st == sg::domain::BattleStatus::BATTLE_ST_DIZZY);
    if (!charging || pinned) return false;
  }

  if (defender.mods.no_duck) return false;      // ⑤
  if (defender.mods.abio) return false;         // ⑥
  if (defender.mods.always_dodge) return true;  // ⑦ 必闪(`_PETSKILL_SETDUCK`,8.0 开)

  // ── 类型修正:四条互斥分支(`:812-828`)────────────────────────
  double at_dex = attacker.quick;
  double df_dex = defender.quick;
  const int df_luck = defender.IsPlayer() ? defender.luck : 0;

  if (attacker.IsEnemy() && defender.kind == CombatantKind::kPet) {
    at_dex *= kTypeModPetVsEnemy;
  } else if (!attacker.IsEnemy() && defender.kind == CombatantKind::kPet) {
    df_dex *= kTypeModPetVsEnemy;
  } else if (!attacker.IsPlayer() && defender.IsPlayer()) {
    at_dex *= kTypeModPlayerCross;
  } else if (attacker.IsPlayer() && !defender.IsPlayer()) {
    df_dex *= kTypeModPlayerCross;
  }

  // ── 主式 ────────────────────────────────────────────────────
  double big, small, wari;
  if (df_dex >= at_dex) {
    big = df_dex; small = at_dex; wari = 1.0;
  } else {
    big = at_dex; small = df_dex;
    wari = (big <= 0) ? 0.0 : (small / big);   // ★ `big <= 0` 的保护,文档未记
  }

  // ★ 守方指令是咒术时更易被闪(0.027 vs 0.02)。
  const double kawashi_para =
      defender_casting_spell ? kKawashiParaSpell : kKawashiParaNormal;

  double work = (big - small) / kawashi_para;
  if (work <= 0) work = 0;

  double per = std::sqrt(work);
  per *= wari;
  per += df_luck;
  per += config.dodge_modifier;                       // 原 gBattleDuckModyfy(① g* 参数化)

  if (attacker.drunk) per += rng.Rand(20, 30);        // ★ 酒醉真正生效处
  if (attacker.mods.wielding_bow) per += kDodgeBonusBow;

  // ⚠️ `_PETSKILL_NEW_PASSIVE` 在 8.0 **关** ⇒ 被动命中/回避加成不实现
  //    (与 DR-BT14「被动宠技 B80 命中 0/12」一致)。

  per *= 100;
  if (per > kKawashiMaxRate * 100) per = kKawashiMaxRate * 100;  // 硬上限 75%
  if (per <= 0) per = 1;

  // 命中率装备(`_EQUIT_HITRIGHT`,8.0 开)—— **仅攻方是玩家时**(`:876`)。
  if (attacker.IsPlayer() && attacker.mods.hit_right != 0) {
    const int hit = attacker.mods.hit_right;
    per -= rng.Rand(static_cast<int>(hit * 0.8), static_cast<int>(hit * 1.2));
    if (per < 0) per = 0;
  }

  // ⚠️ 职业「回避」技(`BATTLE_check_profession_duck`)与「混乱攻击」加成
  //    属职业技能链路(A 批次),不在批次 0 的输入面内 ⇒ 本批次不实现。

  return rng.Rand(1, 10000) <= static_cast<int>(per);
}

}  // namespace sg::rules
