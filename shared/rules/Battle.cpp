// shared/rules/battle.cpp —— L3 战斗结算的实现(阶段 0.1/1.1,批次 0)
//
// ★★ 本文件是 DR-TS6「一遍到位」的落地:原版 `battle/*.c` 只作**只读的规格说明书**,
//    不存在"改造后的中间态 C 代码"。四步改造在这里表现为**四条形态要求**
//    (00-architecture.md §9.0.3),而不是四轮编辑:
//
//      ① 原版靠 10 个 `g*` 隐式传参的量 → 显式参数(`RulesConfig` / 函数入参)
//      ② 原版 `CHAR_set*` 写世界状态   → 追加事件,由调用方应用
//      ③ 原版拼串 + 发包               → 追加事件,序列化留给调用方
//      ④ 原版 `RAND()` / `rand()`      → 注入的 `Random&`
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

#include "rules/Battle.h"

#include <cmath>

namespace SA::Rules
{
namespace
{

// ★ 原版 `attack` / `defense` 是 **float**(`battle_event.c:1164`),不是 double。
//   三分段的分支边界对精度敏感 ⇒ 用 float 保留原类型语义。
using f32 = float;

// 原版 `D_16` / `D_8`(`battle_event.c` 与 `battle_magic.c` 两处定义一致)。
constexpr double kD16 = 1.0 / 16;
constexpr double kD8 = 1.0 / 8;

// `Combatant::elements` 是**地水火风**顺序(constants.h 的 `Element`)。
// 原版 `T_pow` 同序,但 `BATTLE_AttrCalc` 的形参是**火水地风**序 ——
// ⚠️ 移植时若照抄调用点的参数顺序会整体错位。这里统一用 `Element` 下标,不复刻那次换序。
constexpr int kEarth = static_cast<int>(Element::kEarth);
constexpr int kWater = static_cast<int>(Element::kWater);
constexpr int kFire = static_cast<int>(Element::kFire);
constexpr int kWind = static_cast<int>(Element::kWind);
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
f32 fieldPower(std::uint8_t field_attribute, int att_pow,
               const std::int32_t (&elems)[4]) noexcept
{
	double value = 0.0;
	switch (static_cast<FieldAttribute>(field_attribute))
	{
	case FieldAttribute::kEarth:
		value = elems[kEarth];
		break;
	case FieldAttribute::kWater:
		value = elems[kWater];
		break;
	case FieldAttribute::kFire:
		value = elems[kFire];
		break;
	case FieldAttribute::kWind:
		value = elems[kWind];
		break;
	case FieldAttribute::kNone:
	default:
		return static_cast<f32>(kFieldPowBase);
	}
	return static_cast<f32>(kFieldPowBase +
	                        value * att_pow * 0.01 * 0.01 * kFieldPowBase);
}

} // namespace

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
SA::Domain::CannotActReason checkCanAct(const Combatant &c) noexcept
{
	using SA::Domain::BattleStatus;
	using SA::Domain::CannotActReason;

	const auto st = static_cast<BattleStatus>(c.status);

	// 源码顺序:麻痹 → 石化 → 睡眠 → 魔障 → 晕眩 → 天罗 → 雷附体 → 集气
	if (st == BattleStatus::BATTLE_ST_PARALYSIS)
		return CannotActReason::CANNOT_ACT_PARALYSIS;
	if (st == BattleStatus::BATTLE_ST_STONE)
		return CannotActReason::CANNOT_ACT_STONE;
	if (st == BattleStatus::BATTLE_ST_SLEEP)
		return CannotActReason::CANNOT_ACT_SLEEP;
	if (st == BattleStatus::BATTLE_ST_BARRIER)
		return CannotActReason::CANNOT_ACT_BARRIER;
	if (st == BattleStatus::BATTLE_ST_DIZZY)
		return CannotActReason::CANNOT_ACT_DIZZY;
	if (st == BattleStatus::BATTLE_ST_DRAGNET)
		return CannotActReason::CANNOT_ACT_DRAGNET;
	if (st == BattleStatus::BATTLE_ST_T_ENCLOSE)
		return CannotActReason::CANNOT_ACT_T_ENCLOSE;

	// 第 8 项是独立字段,不是 status 槽值(见 combatant.h 的 charging_turns)。
	if (c.charging_turns > 0)
		return CannotActReason::CANNOT_ACT_CHARGING;

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
std::int32_t applyElementMatrix(const BattleField &field,
                                const Combatant &attacker,
                                const Combatant &defender,
                                std::int32_t damage) noexcept
{
	// 攻守双方的五元属性向量(第 5 项是无属性余量,由 NoneElement() 推导)。
	const std::int32_t at[kElementCount] = {
	    attacker.elements[0], attacker.elements[1], attacker.elements[2],
	    attacker.elements[3], attacker.noneElement()};
	const std::int32_t df[kElementCount] = {
	    defender.elements[0], defender.elements[1], defender.elements[2],
	    defender.elements[3], defender.noneElement()};

	// 场地系数在 damage 乘入之前取(原版 `:1114`,读的是未乘 damage 的 T_pow)。
	const f32 at_field = fieldPower(field.field_attribute, 100, attacker.elements);
	const f32 df_field = fieldPower(field.field_attribute, 100, defender.elements);

	// ★ 原版 `:1120` —— damage 先乘进攻方向量。
	std::int32_t at_scaled[kElementCount];
	for (int i = 0; i < kElementCount; ++i)
		at_scaled[i] = at[i] * damage;

	// ★ 原版 `BATTLE_AttrCalc`:每个攻方分量对全部守方分量加权求和,
	//   **结果赋值回 int ⇒ 每属截断一次**。
	std::int64_t total = 0;
	for (int a = 0; a < kElementCount; ++a)
	{
		double row = 0.0;
		for (int d = 0; d < kElementCount; ++d)
		{
			row += static_cast<double>(at_scaled[a]) * df[d] * kElementMatrix[a][d];
		}
		total += static_cast<std::int32_t>(row); // ← 截断 ①(原版 `My_* = …` 赋回 int)
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
double elementCoefficient(const Combatant &attacker, const Combatant &defender) noexcept
{
	const std::int32_t at[kElementCount] = {
	    attacker.elements[0], attacker.elements[1], attacker.elements[2],
	    attacker.elements[3], attacker.noneElement()};
	const std::int32_t df[kElementCount] = {
	    defender.elements[0], defender.elements[1], defender.elements[2],
	    defender.elements[3], defender.noneElement()};

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
std::int32_t computeDamage(const BattleField &field,
                           const Combatant &attacker,
                           const Combatant &defender,
                           const RulesConfig &config,
                           Random &rng) noexcept
{
	// ── 第 1 步:取攻防 ──────────────────────────────────────────
	//
	// 骑宠合成(§3.1)。⚠️ 近战 0.8/0.8、投掷 1.0/0.4 —— 投掷判据是武器类。
	f32 attack;
	if (!attacker.has_ride)
	{
		attack = static_cast<f32>(attacker.attack);
	}
	else if (attacker.mods.weapon == WeaponClass::kThrow)
	{
		attack = static_cast<f32>(kRideThrowSelf * attacker.attack +
		                          kRideThrowPet * attacker.ride_attack);
	}
	else
	{
		attack = static_cast<f32>(kRideMeleeSelf * attacker.attack +
		                          kRideMeleePet * attacker.ride_attack);
	}

	// ★ `_BATTLE_NEWPOWER` 在 8.0 **开** ⇒ defense = 0.70 × DEF。
	//   ⚠️ 关闭态是完全不同的公式(0.45·DEF + 0.2·QUICK + 0.1·FIXVITAL),
	//     D7 裁定单基线 ⇒ **不为它留分支**(constants.h 已注明)。
	f32 defense;
	if (!defender.has_ride)
	{
		defense = static_cast<f32>(defender.defense * kDefenseCoefNewPower);
	}
	else
	{
		defense = static_cast<f32>((defender.defense + defender.ride_defense) * 0.5 *
		                           kDefenseCoefNewPower);
	}

	// ── 第 2 步:防御修正(顺序即语义,不可重排)──────────────────
	//
	// ⚠️★ 全部是 **float 运算**。05 §3.1 称「`rand()%10 == 0` 时该项 = 2/100 = 0
	//    (整数除法)」——**不成立**,`attack`/`defense` 是 float ⇒ 该项 = 0.02。
	//    见 battle.h 移植注记 ①。

	// 铁壁防御(`_MAGIC_SUPERWALL`,8.0 开)。基数是 OTHERSTATUSNUMS。
	if (defender.mods.super_wall)
	{
		const f32 def = static_cast<f32>(
		    (static_cast<double>(defender.other_status_nums) + rng.randMod(20)) / 100);
		defense += defense * def;
	}
	// 怪物能力值修正(`_NPCENEMY_ADDPOWER`,8.0 开)。守方、攻方各一次。
	//
	// ⚠️★ 显式写 `static_cast<f32>(rng.RandMod(10))` 而不是靠隐式转换:原版是
	//    `defense*(rand()%10)`,C 的通常算术转换把 int 提升到 float。行为一致,
	//    但 `-Wconversion` 会对隐式转换告警 —— 而告警口径正是为了抓
	//    「原版大量 int/float 混算」这类问题(见 cmake/SaWarnings.cmake)。
	//    ⇒ 把"这里的混算是有意的"写成代码,而不是让它混在告警噪声里。
	if (defender.isEnemy())
	{
		defense += (defense * static_cast<f32>(rng.randMod(10)) + 2.0f) / 100.0f;
	}
	if (attacker.isEnemy())
	{
		attack += (attack * static_cast<f32>(rng.randMod(10)) + 2.0f) / 100.0f;
	}

	// 守方石化 ⇒ 防御翻倍。
	if (static_cast<SA::Domain::BattleStatus>(defender.status) ==
	    SA::Domain::BattleStatus::BATTLE_ST_STONE)
	{
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
	if (attacker.mods.ignore_defense_percent > 1)
	{
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
	if (defense <= attack && attack < (defense * 8.0 / 7.0))
	{
		damage = rng.rand(0, static_cast<int>(attack * kD16));
	}
	else if (defense > attack)
	{
		damage = rng.rand(0, 1);
	}
	else if (attack >= (defense * 8 / 7))
	{
		const f32 k0 = static_cast<f32>(rng.rand(0, static_cast<int>(attack * kD8)) -
		                                attack * kD16);
		damage = static_cast<std::int32_t>((attack - defense) * kDamageRate + k0);
	}

	// ── 第 4 步:四属性相克 ─────────────────────────────────────
	damage = applyElementMatrix(field, attacker, defender, damage);

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
	if (damage < 0)
		damage = 0;

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
bool rollDodge(const Combatant &attacker,
               const Combatant &defender,
               bool defender_guarding,
               bool defender_casting_spell,
               const RulesConfig &config,
               Random &rng) noexcept
{
	// ── 六道前置否决 + 一道必闪 ─────────────────────────────────
	//
	// ⚠️ 05 §3.2 的清单**漏了 ABIO**(见 combatant.h 的 mods.abio)。实际是:
	//   ① 攻方集气完成  ② 守方防御  ③ 守方有反应类状态  ④ 守方不能行动
	//   ⑤ NODUCK       ⑥ ABIO      然后 ⑦ 必闪技 ⇒ 直接 true
	//
	// ★ 顺序照源码 `:766-804`。因每道都是 `return`,顺序不影响结果,
	//   但影响**读代码的人对优先级的理解** ⇒ 仍照原样。
	if (attacker.charge_ready)
		return false; // ①
	if (defender_guarding)
		return false; // ②
	if (defender.damage_react > 0)
		return false; // ③

	// ④ 守方不能行动。★ `_PROFESSION_ADDSKILL`(8.0 开)有一处例外:
	//   **集气中仍可闪避**,除非同时处于天罗地网或晕眩(`:779-788`)。
	if (checkCanAct(defender) != SA::Domain::CannotActReason::CANNOT_ACT_NONE)
	{
		const bool charging = defender.charging_turns > 0;
		const auto st = static_cast<SA::Domain::BattleStatus>(defender.status);
		const bool pinned = (st == SA::Domain::BattleStatus::BATTLE_ST_DRAGNET) ||
		                    (st == SA::Domain::BattleStatus::BATTLE_ST_DIZZY);
		if (!charging || pinned)
			return false;
	}

	if (defender.mods.no_duck)
		return false; // ⑤
	if (defender.mods.abio)
		return false; // ⑥
	if (defender.mods.always_dodge)
		return true; // ⑦ 必闪(`_PETSKILL_SETDUCK`,8.0 开)

	// ── 类型修正:四条互斥分支(`:812-828`)────────────────────────
	double at_dex = attacker.quick;
	double df_dex = defender.quick;
	const int df_luck = defender.isPlayer() ? defender.luck : 0;

	if (attacker.isEnemy() && defender.kind == CombatantKind::kPet)
	{
		at_dex *= kTypeModPetVsEnemy;
	}
	else if (!attacker.isEnemy() && defender.kind == CombatantKind::kPet)
	{
		df_dex *= kTypeModPetVsEnemy;
	}
	else if (!attacker.isPlayer() && defender.isPlayer())
	{
		at_dex *= kTypeModPlayerCross;
	}
	else if (attacker.isPlayer() && !defender.isPlayer())
	{
		df_dex *= kTypeModPlayerCross;
	}

	// ── 主式 ────────────────────────────────────────────────────
	double big, small, wari;
	if (df_dex >= at_dex)
	{
		big = df_dex;
		small = at_dex;
		wari = 1.0;
	}
	else
	{
		big = at_dex;
		small = df_dex;
		wari = (big <= 0) ? 0.0 : (small / big); // ★ `big <= 0` 的保护,文档未记
	}

	// ★ 守方指令是咒术时更易被闪(0.027 vs 0.02)。
	const double kawashi_para =
	    defender_casting_spell ? kKawashiParaSpell : kKawashiParaNormal;

	double work = (big - small) / kawashi_para;
	if (work <= 0)
		work = 0;

	double per = std::sqrt(work);
	per *= wari;
	per += df_luck;
	per += config.dodge_modifier; // 原 gBattleDuckModyfy(① g* 参数化)

	if (attacker.drunk)
		per += rng.rand(20, 30); // ★ 酒醉真正生效处
	if (attacker.mods.wielding_bow)
		per += kDodgeBonusBow;

	// ⚠️ `_PETSKILL_NEW_PASSIVE` 在 8.0 **关** ⇒ 被动命中/回避加成不实现
	//    (与 DR-BT14「被动宠技 B80 命中 0/12」一致)。

	per *= 100;
	if (per > kKawashiMaxRate * 100)
		per = kKawashiMaxRate * 100; // 硬上限 75%
	if (per <= 0)
		per = 1;

	// 命中率装备(`_EQUIT_HITRIGHT`,8.0 开)—— **仅攻方是玩家时**(`:876`)。
	if (attacker.isPlayer() && attacker.mods.hit_right != 0)
	{
		const int hit = attacker.mods.hit_right;
		per -= rng.rand(static_cast<int>(hit * 0.8), static_cast<int>(hit * 1.2));
		if (per < 0)
			per = 0;
	}

	// ⚠️ 职业「回避」技(`BATTLE_check_profession_duck`)与「混乱攻击」加成
	//    属职业技能链路(A 批次),不在批次 0 的输入面内 ⇒ 本批次不实现。

	return rng.rand(1, 10000) <= static_cast<int>(per);
}

// ═══════════════════════════════════════════════════════════════════
//  暴击(§3.3,批次 A.3)
// ═══════════════════════════════════════════════════════════════════
//
// 1:1 移植 `BATTLE_CriticalCheckPlayer`(`battle_event.c:1283`)的 per 构成
// + `BATTLE_AttackSeq`(`:1592`)的判定阈 `RAND(1,10000) < perCri`。
//
// ⚠️★ 批次 0.5 曾以「文档缺判定阈」有意留空(§9.0.8)。2026-09-06 回源码核实:
//    判定阈在源码里齐全 —— per 在 `CriticalCheckPlayer` 里已 `*= 100` 并 clamp,
//    `AttackSeq` 直接 `if (RAND(1,10000) < perCri)`。**是文档缺,不是源码缺** ⇒ 实现。
//
// ⚠️★ 与回避(§3.2)一样用 `CHAR_WORKFIXDEX` ⇒ 映射到 `Combatant::quick`,不新增字段。
//
// ⚠️ 三处副作用**有意不实现**,均属世界写或别的链路:
//   ① 暴击命中时的职业技能升级(`:1601`)—— 四步改造第②步剥离,调用方按事件处理;
//   ② 暗月狂狼 `perCri×1.3` + 攻/敏各 +20%(`:1578`)—— 宠技(B 批次)且是世界写;
//   ③ `gCriper` 全局暂存(`:1591`)—— g* 隐式传参,本实现无文件级变量。
bool rollCritical(const Combatant &attacker,
                  const Combatant &defender,
                  Random &rng) noexcept
{
	// ⚠️★ **全程 f32**,与 `RollCapture`(DR-BT16)同一纪律:源码 `:1287` 声明
	//    `float per, Work, Big, Small, wari, divpara` ⇒ 逐位按 float 移植,
	//    不用 double —— 否则中间精度更高、边界不一致,会污染黄金用例集基线。
	//    唯一例外:`sqrt` 原版是 `(float)sqrt((double)Work)`,先升 double 再降回。
	f32 at_dex = static_cast<f32>(attacker.quick);
	f32 df_dex = static_cast<f32>(defender.quick);
	f32 divpara = static_cast<f32>(kCriticalPara);
	bool root = true; // root==1 ⇒ 取平方根

	// ── 类型修正:四条互斥分支(`:1305-1321`,与回避同结构但阈值不同)──
	//   ⚠️ 分支顺序照源码:pet→enemy 用 `IsEnemy(def)`,其余用 `IsPlayer`。
	if (attacker.kind == CombatantKind::kPet && defender.isEnemy())
	{
		df_dex *= static_cast<f32>(kCriticalDexModPetVsEnemy); // 宠→敌:Df_Dex × 0.8
	}
	else if (attacker.isEnemy() && defender.kind == CombatantKind::kPet)
	{
		divpara = static_cast<f32>(kCriticalParaCross);
		root = false; // 敌→宠:分母暴增、不取根
	}
	else if (!attacker.isPlayer() && defender.isPlayer())
	{
		divpara = static_cast<f32>(kCriticalParaCross);
		root = false; // 非玩→玩:同上
	}
	else if (attacker.isPlayer() && !defender.isPlayer())
	{
		df_dex *= static_cast<f32>(kCriticalDexModPlayerCross); // 玩→非玩:Df_Dex × 0.6
	}

	// ★ At_Luck 只在**攻方是玩家**时取(`:1295`),否则 0。
	const int at_luck = attacker.isPlayer() ? attacker.luck : 0;

	f32 big, small, wari;
	if (at_dex >= df_dex)
	{
		big = at_dex;
		small = df_dex;
		wari = 1.0f;
	}
	else
	{
		big = df_dex;
		small = at_dex;
		wari = (big <= 0) ? 0.0f : (small / big);
	}

	f32 work = (big - small) / divpara;
	if (work <= 0)
		work = 0;

	f32 per = root ? static_cast<f32>(std::sqrt(static_cast<double>(work))) : work;
	per += static_cast<f32>(attacker.mods.equip_critical * kCriticalEquipFactor); // + 装备暴击 × 0.5
	per *= wari;
	per += static_cast<f32>(at_luck);
	per *= 100;
	if (per < 0)
		per = static_cast<f32>(kCriticalPerMin); // ★ 原版 `if(per<0) per=1`
	if (per > static_cast<f32>(kCriticalPerMax))
		per = static_cast<f32>(kCriticalPerMax);

	// ★★ 守方免疫暴击 ⇒ per=0。原版按图号硬编码(雷尔 101813/101814,`:1349`),
	//   ⚠️ DR-BT11 裁定**改数据驱动**:判据是 `mods.immune_critical` 标志,不比对图号。
	//   1.5 无敌人数值表 ⇒ 该标志恒 false,免疫分支不可达;L4 建模雷尔模板时置 true。
	if (defender.mods.immune_critical)
	{
		per = 0;
	}

	// ★ 判定用**严格小于**(`:1592`),与回避的 `<=` 不同 —— 逐位照源码。
	//   ⚠️ `perCri` 原版是 `(int)per`(先截断再比),不是拿 float 直接比 ⇒ 保留截断。
	return rng.rand(1, kCriticalRollMax) < static_cast<int>(per);
}

std::int32_t computeCriticalDamage(const BattleField &field,
                                   const Combatant &attacker,
                                   const Combatant &defender,
                                   const RulesConfig &config,
                                   Random &rng) noexcept
{
	// 暴击伤害 = DamageCalc + 守方**原始**防御 × (LVatt / LVdef) × 0.5。(`:1419`)
	//
	// ⚠️★ 这里的守方防御是 `CHAR_WORKDEFENCEPOWER` **原始值** —— 不经 0.70 系数、
	//    不经骑宠合成(那些在 `DamageCalc` 内部,附加项另算)。⇒ 用 `defender.defense`。
	// ⚠️★ **顺序即语义**:先取完整 `ComputeDamage`(含它自己的全局系数、相克等),
	//    再叠加防御附加项 —— 原版 `CriDamageCalc` 也是 `DamageCalc(...)` 之后再 `+=`。
	const std::int32_t base = computeDamage(field, attacker, defender, config, rng);

	// ⚠️ LVdef 由 Combatant::level 保证 ≥ 1(默认值 1)⇒ 不会除零;仍显式记明。
	const f32 add = static_cast<f32>(defender.defense) *
	                static_cast<f32>(attacker.level) /
	                static_cast<f32>(defender.level) *
	                static_cast<f32>(kCriticalDamageDefFactor);
	return base + static_cast<std::int32_t>(add);
}

// ═══════════════════════════════════════════════════════════════════
//  打飞 / 究极一击(批次 A.4)
// ═══════════════════════════════════════════════════════════════════
//
// 1:1 移植 `BATTLE_DamageSub` 的打飞段(`battle_event.c:2060-2081`):
//
//     if( damage >= maxhp * 1.2 + 20 ) {          // 一击打飞
//         IsUltimate = 2;
//     } else if( addpoint > 0 ) {                 // addpoint = 打穿的溢出量
//         addpoint += CHAR_getWorkInt(def, WORKULTIMATE);
//         CHAR_setWorkInt(def, WORKULTIMATE, addpoint);
//         if( addpoint >= maxhp * 1.2 + 20 ) IsUltimate = 1;   // 累积打飞
//     }
//     if(雷尔图号) IsUltimate = 0;                // ★ 在累加之后
//     if( IsUltimate ) CHAR_setWorkInt(def, WORKULTIMATE, 0);
//
// ⚠️★ **门槛用 float**:源码 `maxhp` 处于 `float` 声明域(`:1164`),`maxhp*1.2+20`
//    是浮点运算。用 `static_cast<double>` 算门槛再与 int 比,保留原版语义。
KnockbackKind rollKnockback(std::int32_t damage,
                            std::int32_t overflow,
                            std::int32_t max_hp,
                            std::int32_t accumulator,
                            bool immune_knockback,
                            std::int32_t *out_accumulator) noexcept
{
	// 门槛 = maxhp × 1.2 + 20(float 语义,与源码一致)。
	const double threshold =
	    static_cast<double>(max_hp) * kKnockbackHpMultiplier + kKnockbackHpBonus;

	KnockbackKind kind = KnockbackKind::kNone;
	std::int32_t acc = accumulator;

	if (static_cast<double>(damage) >= threshold)
	{
		// 一击打飞(原 IsUltimate=2)。★ 此路**不动累加器** —— 源码里
		//   一击打飞走 if 分支,addpoint 累加只在 else 分支。
		kind = KnockbackKind::kOneShot;
	}
	else if (overflow > 0)
	{
		// 累积打飞(原 IsUltimate=1)。溢出先累加进持久累加器,再判门槛。
		acc += overflow;
		if (static_cast<double>(acc) >= threshold)
		{
			kind = KnockbackKind::kAccumulated;
		}
	}

	// ★★ 免疫打飞:原版按图号硬编码(雷尔 101813/101814,`:2076`)⇒ IsUltimate=0,
	//   ⚠️ DR-BT11 裁定改数据驱动 ⇒ 判据是 `immune_knockback` 标志,不比对图号。
	//   ★ **在累加之后覆盖**:原版顺序是先 addpoint 累加(else 分支已写回 WORKULTIMATE)、
	//     再按图号把 IsUltimate 清 0 ⇒ 累加器仍留着累加后的值,只是这一次不判为打飞。
	//     逐位照源码顺序:此处不回滚 acc。
	if (immune_knockback)
	{
		kind = KnockbackKind::kNone;
	}

	// 命中打飞(一击或累积)⇒ 累加器清零(源码 `:2079-2081` `if(IsUltimate) ...=0`)。
	//   ⚠️ 免疫已把 kind 归 kNone ⇒ 不清零(与原版一致:免疫时 IsUltimate=0,不进清零分支)。
	if (kind != KnockbackKind::kNone)
	{
		acc = 0;
	}

	if (out_accumulator != nullptr)
		*out_accumulator = acc;
	return kind;
}

// ═══════════════════════════════════════════════════════════════════
//  回合调度(批次 0.5)
// ═══════════════════════════════════════════════════════════════════
//
// 对应原版 `BATTLE_Battling`(`battle.c`,1,983 行)**的第 6 步本身** ——
// 覆盖边界与「暴击 / 反击为何有意留空」见 battle.h 的批次 0.5 注记。
//
// ★ 四步改造在本段的具体表现:
//   ① 原版 `gWeponType` / `gDamageDiv` / `gBattleStausChange` 等 10 个 g* 隐式传参
//      → 这里全是局部量与显式入参,一个文件级变量都没有;
//   ② 原版 `BATTLE_DamageSub` 直接 `CHAR_setInt(HP)` → 这里只 `PushDamage`,
//      ★ **`field` 是 const 引用,连想写都写不了** —— 由类型系统兜底,不靠自觉;
//   ③ 原版 `strcat` 拼 `szAllBattleString` 再 send → 这里只追加事件;
//   ④ 原版 `RAND()` → `rng`。

std::int32_t computeActionDex(const Combatant &c,
                              const SA::Domain::BattleCommand &command,
                              Random &rng) noexcept
{
	// 基数(`BATTLE_DexCalc`):WORKQUICK + 20。
	std::int32_t dex = c.quick + kDexBase;

	// ⚠️ 批次 0.5 只有默认档。★ 但**必须把 command 收进入参**:其余 8 档全部
	//    按指令种类分,签名现在不收、将来就得改所有调用点与全部用例。
	(void)command;
	dex -= rng.rand(0, static_cast<int>(c.quick * kDexJitterRatio));

	// ⚠️★ **不夹下限。** 原版 `if (dex <= 1) dex = 1;` 是被注释掉的 ⇒ dex 可为 0 或负。
	return dex + c.mods.sequence;
}

int buildActionOrder(const BattleField &field,
                     const TurnCommands &commands,
                     Random &rng,
                     std::uint8_t (&order)[kSlotCount]) noexcept
{
	std::int32_t keys[kSlotCount] = {};
	int count = 0;

	// ⚠️★ **必须按槽号升序遍历、逐个抽 dex。** 抽取顺序决定 rng 的消费序列,
	//    换个遍历顺序 ⇒ 同种子给出不同战斗 ⇒ 可回放性失效(黄金用例集全批失败)。
	for (int i = 0; i < kSlotCount; ++i)
	{
		const Combatant &c = field.at(i);
		if (!c.occupied || c.dead)
			continue;
		if (!commands.present[i])
			continue; // 无指令 ⇒ 本回合不行动(敌方由 AI 填齐)
		keys[count] = computeActionDex(c, commands.commands[i], rng);
		order[count] = static_cast<std::uint8_t>(i);
		++count;
	}

	// ★★ DR-BT8:同速按**入场位次** ⇒ 稳定排序,大 dex 在前。
	//
	// ⚠️ 用插入排序而不是 `std::sort`,两条理由缺一不可:
	//   ① `std::sort` **不保证稳定** ⇒ 同 dex 时顺序由实现决定,正是 DR-BT8 要消灭的
	//      那种不可复现(原版 `EsCmp` 不满足严格弱序,同一份输入在两套标准库上会排出
	//      不同结果);`std::stable_sort` 稳定,但**可能分配内存** ⇒ 撞 15 §9.1
	//      「运行期零分配」。
	//   ② n ≤ 20,插入排序在这个规模上本来就不慢。
	for (int i = 1; i < count; ++i)
	{
		const std::int32_t key = keys[i];
		const std::uint8_t slot = order[i];
		int j = i - 1;
		// 严格 `<` ⇒ 相等时不再前移 ⇒ 保持槽号升序 = 入场位次。
		while (j >= 0 && keys[j] < key)
		{
			keys[j + 1] = keys[j];
			order[j + 1] = order[j];
			--j;
		}
		keys[j + 1] = key;
		order[j + 1] = slot;
	}
	return count;
}

int rollAttackCount(const Combatant &attacker,
                    const RulesConfig &config,
                    Random &rng) noexcept
{
	// ── 有武器:RAND(min, max),≤0 则 1 ────────────────────────────
	if (!attacker.mods.unarmed)
	{
		const int n = rng.rand(attacker.mods.attack_num_min, attacker.mods.attack_num_max);
		return n <= 0 ? 1 : n;
	}

	// ── 空手:两道前置(等级 ≥ 10 且是玩家),否则恒 1 段 ────────────
	if (attacker.level < kUnarmedMultihitMinLevel || !attacker.isPlayer())
		return 1;

	// ⚠️ DR-BT1 的开关只管**各段是否全额**(`gDamageDiv`),不管**段数怎么抽**。
	//    ⇒ 关掉它不该让空手变回单段 —— 那是另一个改动。
	(void)config;

	// luckwork = LUCK × 5,上限 25。★ LUCK 自身上限也是 25(combatant.h)。
	int luckwork = attacker.luck * kUnarmedLuckFactor;
	if (luckwork > kUnarmedLuckCap)
		luckwork = kUnarmedLuckCap;

	const int roll = rng.rand(1, kUnarmedRollMax);
	if (roll <= kUnarmedThreshold10 + luckwork)
	{
		return rng.rand(kUnarmedBurstMin, kUnarmedBurstMax); // ★ 可达 10 段
	}
	if (roll <= kUnarmedThreshold3 + luckwork)
		return 3;
	if (roll <= kUnarmedThreshold2 + luckwork)
		return 2;
	return 1;
}

double rollGuardFactor(Random &rng) noexcept
{
	const int roll = rng.rand(1, 100);
	for (const GuardTier &tier : kGuardTiers)
	{
		if (roll <= tier.upper_bound)
			return tier.factor;
	}
	// 不可达(最后一档上界就是 100)。★ 兜底取最强减伤而不是 1.0:
	//   万一表被改窄,宁可少扣血,也不要静默变成"防御无效"。
	return kGuardTiers[0].factor;
}

RideSplit splitRideDamage(std::int32_t damage,
                          std::int32_t my_defense,
                          std::int32_t pet_defense) noexcept
{
	RideSplit split;
	const std::int64_t total_def =
	    static_cast<std::int64_t>(my_defense) + pet_defense;
	if (total_def <= 0)
	{
		// 双方防御都是 0 ⇒ 比例无定义。★ 全部记在主人身上,不是各半 ——
		//   原式在这种情形下会除零,新实现必须显式选一个,且不能让宠物凭空扛伤。
		split.player = damage;
		split.pet = 0;
		return split;
	}
	// ★ DR-BT2 修正:分子是 **myDef**(防御高者多扛),且**无 +1** ⇒ 无损分摊。
	split.player = static_cast<std::int32_t>(
	    static_cast<std::int64_t>(damage) * my_defense / total_def);
	split.pet = damage - split.player;
	return split;
}

// ═══════════════════════════════════════════════════════════════════
//  逃跑(批次 A.1)
// ═══════════════════════════════════════════════════════════════════
//
// 1:1 移植 `BATTLE_EscapeCheck`(`battle_event.c:4236`)。
//
// ⚠️★ **移植期两处文档出入(2026-09-06 对源码复核,以本注记为准)**:
//   ① DR-BT15:escape_cnt 的双重计数 ⇒ 首次尝试 = 2,不是 05 §6.1 的 1。
//      本函数不管递增,调用方传入的 escape_cnt 已含此口径(constants.h)。
//   ② ABIO 敌人贡献 `level − 100` 到等级和(`:4281-4282`)⇒ 05 §6.1 的 `−2ΔLv` 漏记。
//      本函数吃调用方算好的 enemy_level_sum,ABIO 扣减在调用方(它才有逐个敌人的标志位)。
bool rollEscape(bool is_pvp,
                int attacker_luck_tier,
                int escape_cnt,
                int my_level,
                int enemy_level_sum,
                int enemy_alive_count,
                Random &rng,
                int *out_percent) noexcept
{
	// PvP 中必定逃脱(`:4252`)—— 先于一切公式。
	if (is_pvp)
	{
		if (out_percent != nullptr)
			*out_percent = kEscapeNoEnemyRate;
		return true;
	}

	int esc;
	if (enemy_alive_count <= 0)
	{
		// 敌方无存活 ⇒ Esc = 100(`:4289-4291`)。
		esc = kEscapeNoEnemyRate;
	}
	else
	{
		// ★ 整数除法取平均敌方等级(`:4293 enemylevel /= enemycnt`)——
		//   enemy_level_sum 已含 ABIO 的 −100,可能为负,平均后亦然,照原样不夹。
		const int enemy_avg_level = enemy_level_sum / enemy_alive_count;
		const int delta = kEscapeLevelPenalty * (enemy_avg_level - my_level);

		// luck 分档(`:4294-4310`)。★ `>= 5` 与 `else`(luck ≤ 0)都走 95×cnt ——
		//   源码 `:4308-4310` 的 else 分支与 `>=5` 同式,照抄。
		if (attacker_luck_tier >= 5)
		{
			esc = kEscapeCoefLuck5 * escape_cnt;
		}
		else if (attacker_luck_tier >= 4)
		{
			esc = kEscapeCoefLuck4 * escape_cnt - delta;
		}
		else if (attacker_luck_tier >= 3)
		{
			esc = kEscapeCoefLuck3 * escape_cnt - delta;
		}
		else if (attacker_luck_tier >= 2)
		{
			esc = kEscapeCoefLuck2 * escape_cnt - delta;
		}
		else if (attacker_luck_tier >= 1)
		{
			esc = kEscapeCoefLuck1 * escape_cnt - delta;
		}
		else
		{
			esc = kEscapeCoefLuck5 * escape_cnt;
		}
	}

	if (esc < kEscapeMinRate)
		esc = kEscapeMinRate; // `:4313`
	if (out_percent != nullptr)
		*out_percent = esc;

	// ★ 判定阈:`RAND(1,100) < Esc`(`:4317`)—— 严格小于,不是 ≤。
	return rng.rand(1, 100) < esc;
}

// ═══════════════════════════════════════════════════════════════════
//  捕获(批次 A.2)
// ═══════════════════════════════════════════════════════════════════
//
// 1:1 移植 `BATTLE_CaptureCheck`(`battle_event.c:3806`)。
//
// ⚠️★ **全程 float**(源码 `:3812-3819` 逐个变量声明 float)。与逃跑/伤害同族:
//   `05` §6.2 把级差/敏捷差标注为「整数除法」是**错的** —— 这几处是浮点除法,
//   照文档写会引入原版没有的截断。见 constants.h 的移植更正。
//   本实现用 `f32`(与 ComputeDamage 同一别名)保留原类型语义,让边界比较逐位一致。
bool rollCapture(int my_level, int target_level,
                 int my_dex, int target_dex,
                 int my_charm, int my_luck,
                 int target_hp, int target_max_hp,
                 int capture_difficulty,
                 int capture_bonus,
                 bool target_asleep,
                 Random &rng,
                 int *out_percent) noexcept
{
	// ★ MaxHp 兜底(`:3849 if(Df_MaxHp<=0)Df_MaxHp=1`)—— 防二次式除零。
	f32 max_hp = static_cast<f32>(target_max_hp);
	if (max_hp <= 0)
		max_hp = 1;

	// Df_HpPer = 10 − (HP·HP)/MaxHp(`:3852`)★ 二次式:满血 ⇒ 10−MaxHp,深负 ⇒ 几乎抓不到。
	const f32 hp = static_cast<f32>(target_hp);
	const f32 df_hp_per =
	    static_cast<f32>(kCaptureHpBase) - (hp * hp) / max_hp;

	// ★ 级差 / 敏捷差:**浮点除法**(见卷首)。
	const f32 df_level = static_cast<f32>(my_level) / static_cast<f32>(kCaptureLevelDivisor) -
	                     static_cast<f32>(target_level) / static_cast<f32>(kCaptureLevelDivisor);
	const f32 df_dex = static_cast<f32>(my_dex) / static_cast<f32>(kCaptureDexDivisor) -
	                   static_cast<f32>(target_dex) / static_cast<f32>(kCaptureDexDivisor);

	// WorkGet = (Df_HpPer + Df_Level + Df_Dex + (难度 + 幸运)) × 魅力 / 50(`:3855`)。
	f32 work = (df_hp_per + df_level + df_dex +
	            (static_cast<f32>(capture_difficulty) + static_cast<f32>(my_luck))) *
	           static_cast<f32>(my_charm) / static_cast<f32>(kCaptureCharmDivisor);

	// += 捕获率提升(`:3857`)。★ 清零是世界写,在调用方,不在此。
	work += static_cast<f32>(capture_bonus);

	// 目标睡眠 ⇒ +15(`:3859-3861`)。
	if (target_asleep)
		work += static_cast<f32>(kCaptureSleepBonus);

	// min(WorkGet, 99)(`:3863`)。★ 无下限钳位 —— 原版没有 `if(WorkGet<0)`,
	//   负值直接进 `RAND(1,100) < WorkGet` ⇒ 必失败。照抄,不补下限。
	if (work > static_cast<f32>(kCaptureMaxRate))
		work = static_cast<f32>(kCaptureMaxRate);

	// ★ `*pPer = WorkGet`(`:3865`)—— 原版回填的是**未取整的 float**;
	//   out_percent 是 int 视图,按截断给(展示/调试用,不参与判定)。
	if (out_percent != nullptr)
		*out_percent = static_cast<int>(work);

	// ★ 判定阈:`RAND(1,100) < WorkGet`(`:3867`)—— 严格小于,同逃跑。
	//   ⚠️ 比较是 `int < float`:C 把 int 提升为 float 再比。保留该语义,
	//     不要先把 work 截成 int —— 那会改变 `WorkGet` 有小数时的边界。
	return static_cast<f32>(rng.rand(1, 100)) < work;
}

namespace
{

// 事件缓冲的追加器。★ 只有它能写 `out.events`,截断判定收在一处。
//
// ⚠️★ 原版在这里犯过 05 §10.4 那个错(`strncat` 第三参用错 ⇒ 等价无上界 `strcat`,
//    余量 56 字节且无第二道防线)。⇒ 本实现**溢出就置位并停止追加**,
//    由 `ResolveTurn` 返回 false 把它交给调用方分包,绝不静默丢弃。
class EventSink
{
  public:
	explicit EventSink(SA::Domain::BattleEvents &out) noexcept : _out(out) {}

	bool overflowed() const noexcept { return _overflowed; }

	// 追加一个事件槽并返回它;满了返回 nullptr。
	SA::Domain::BattleEvent *push(SA::Domain::BattleEvent::BodyKind kind) noexcept
	{
		SA::Domain::BattleEvent *e = _out.events.push_back();
		if (e == nullptr)
		{
			_overflowed = true;
			return nullptr;
		}
		*e = SA::Domain::BattleEvent{};
		e->body_kind = kind;
		return e;
	}

  private:
	SA::Domain::BattleEvents &_out;
	bool _overflowed = false;
};

bool isGuarding(const SA::Domain::BattleCommand &cmd) noexcept
{
	return cmd.command_kind == SA::Domain::BattleCommand::CommandKind::GUARD;
}

// 守方睡眠(捕获 +15,§6.2 `:3859`)。★ 原版读的是 `CHAR_WORKSLEEP > 0`,
//   与状态槽 `BATTLE_ST_SLEEP` 是两个来源(同 drunk/confusion 那族),但 1.5 尚无
//   独立 sleep work 字段 ⇒ 暂以状态槽近似。⚠️ 实现处记明:睡眠 work 独立字段
//   属状态系统细化(§4),届时改读它,不要长期用状态槽代替。
bool isAsleep(const Combatant &c) noexcept
{
	return static_cast<SA::Domain::BattleStatus>(c.status) ==
	       SA::Domain::BattleStatus::BATTLE_ST_SLEEP;
}

// 守方本回合是否在施咒(§3.2:咒术时 kawashi_para 取 0.027,更易被闪)。
bool isCastingSpell(const TurnCommands &commands, int slot) noexcept
{
	if (!commands.present[slot])
		return false;
	return commands.commands[slot].command_kind ==
	       SA::Domain::BattleCommand::CommandKind::SPELL;
}

// 逃跑的 luck 归档(`battle_event.c:4260-4270`)。★ 放在这里而非 RollEscape:
//   它读 kind/rare/幸运,属输入准备。⚠️ 敌人的 rare 尚未进 `Combatant` 输入面
//   (它属 L2 敌人模型),1.5 无敌方逃跑 ⇒ 敌人暂按 rare=default(luck=5)处理,
//   在实现处记明;玩家走 clamp(幸运, 1, 5)。
int escapeLuckTier(const Combatant &c) noexcept
{
	if (c.isEnemy())
	{
		// ⚠️ rare 未建模 ⇒ 走 default 档(luck=5)。敌方主动逃跑属批次后续 NPC 行为,
		//    届时补 rare 字段并按 0→1/1→3/else→5 归档。此处不猜一个"看起来对"的值。
		return 5;
	}
	int luck = c.luck;
	if (luck > 5)
		luck = 5; // min(5, WORKFIXLUCK)
	if (luck < 1)
		luck = 1; // max(1, ...)
	return luck;
}

// 敌方(相对逃跑者而言的对面)存活单位的等级和 + 存活数,★ 含 ABIO 单位 −100。
//
// `battle_event.c:4277-4293`:遍历对面 Entry,ABIO 单位先给等级和 −100,再累加其等级。
struct EnemyLevelStat
{
	int level_sum = 0;
	int alive_count = 0;
};

EnemyLevelStat collectEnemyLevels(const BattleField &field,
                                  const bool (&dead)[kSlotCount],
                                  int actor_slot) noexcept
{
	EnemyLevelStat stat;
	const bool actor_is_enemy = actor_slot >= kSideOffset;
	for (int i = 0; i < kSlotCount; ++i)
	{
		const bool i_is_enemy = i >= kSideOffset;
		if (i_is_enemy == actor_is_enemy)
			continue; // 只数对面
		const Combatant &c = field.at(i);
		if (!c.occupied || dead[i])
			continue;
		if (c.mods.abio)
			stat.level_sum -= kEscapeAbioLevelPenalty; // ★ :4281-4282
		stat.level_sum += c.level;
		++stat.alive_count;
	}
	return stat;
}

} // namespace

bool resolveTurn(const BattleField &field,
                 const TurnCommands &commands,
                 const RulesConfig &config,
                 Random &rng,
                 SA::Domain::BattleEvents &out) noexcept
{
	out.battle_id = field.battle_id;
	out.turn = field.turn;
	out.events.clear();
	EventSink sink(out);

	// ★ HP 的**本地**镜像。L3 不写世界状态(`field` 是 const)——
	//   但同一回合内的后续攻击必须看到前面造成的伤害,否则一回合里能把同一个
	//   已死目标反复打死。⇒ 在这里维护一份局部账,回合结束即丢弃;
	//   真正的写回由调用方按事件列表执行(四步改造第②步的形态)。
	std::int32_t hp[kSlotCount];
	std::int32_t pet_hp[kSlotCount];
	bool dead[kSlotCount];
	// ★ 打飞累加器的**本地**镜像(同 hp[]):判定要读它、多段之间要看到累加,
	//   但世界写(持久化 + 命中清零)由调用方按事件在 ApplyEvents 做。
	std::int32_t ult_acc[kSlotCount];
	for (int i = 0; i < kSlotCount; ++i)
	{
		hp[i] = field.at(i).hp;
		pet_hp[i] = field.at(i).ride_hp;
		dead[i] = field.at(i).dead;
		ult_acc[i] = field.at(i).ultimate_accumulator;
	}

	std::uint8_t order[kSlotCount] = {};
	const int actor_count = buildActionOrder(field, commands, rng, order);

	for (int n = 0; n < actor_count; ++n)
	{
		const int actor_slot = order[n];
		const Combatant &actor = field.at(actor_slot);

		// 回合内先被打死的单位不再行动(原版同样在派发前查存活)。
		if (dead[actor_slot])
			continue;

		// ★ DR-BT5:能否行动的**唯一**判据。上行校验与结算走同一个函数。
		// ⚠️ 不产事件:不可行动的原因走 `BattleSelfInfo.cannot_act` 在**指令阶段**下发
		//    (DR-CP7 菜单置灰),而不是等结算完再告诉玩家"你刚才动不了"——
		//    那正是 DR-CP6 反对的假交互。
		if (checkCanAct(actor) != SA::Domain::CannotActReason::CANNOT_ACT_NONE)
			continue;

		const SA::Domain::BattleCommand &cmd = commands.commands[actor_slot];

		// ── 指令分发 ─────────────────────────────────────────────
		//
		// ⚠️★ 批次 0.5 只接 ATTACK / GUARD / WAIT 三种。其余七种**显式落到 default**
		//    并被跳过 —— 不是"忘了写",是它们各自绑着未移植的链路(见 battle.h 的表)。
		//    ⇒ 接入时在这里补 case,**不要**在调用方拦截:那会让 L3 之外出现第二处
		//      指令语义,与 DR-BT5「唯一真源」同类的错误。
		// ── 逃跑(§6.1,批次 A.1)──────────────────────────────────
		//
		// ⚠️★ **宠物不能逃**(`battle.c:9746` 的 `!= CHAR_TYPEPET`)—— 在此拦,
		//    不产事件、不递增计数器。它是**指令语义**的一部分,按 DR-BT5 留在 L3。
		if (cmd.command_kind == SA::Domain::BattleCommand::CommandKind::ESCAPE)
		{
			if (actor.kind == CombatantKind::kPet)
				continue;

			const EnemyLevelStat es = collectEnemyLevels(field, dead, actor_slot);
			// ★ DR-BT15:escape_cnt = escape_count + 2 —— 源码 BATTLE_Escape 先 ++
			//   (→ escape_count+1),EscapeCheck 再读 +1(→ escape_count+2)。首次即 2。
			const int escape_cnt = actor.escape_count + 2;
			const bool ok = rollEscape(field.is_pvp, escapeLuckTier(actor), escape_cnt,
			                           actor.level, es.level_sum, es.alive_count, rng);

			SA::Domain::BattleEvent *ev =
			    sink.push(SA::Domain::BattleEvent::BodyKind::ESCAPE);
			if (ev == nullptr)
				break;
			ev->body.escape.actor = static_cast<std::uint32_t>(actor_slot);
			ev->body.escape.succeeded = ok;
			// vanish:成功逃跑者本回合从战场消失(客户端演淡出)。失败则留场。
			ev->body.escape.vanish = ok;
			// ★ 计数器的递增(无论成败)与移出战场由调用方按事件执行 ——
			//   L3 不写世界态(field 是 const)。见 world.cpp 的 ApplyEvents。
			if (sink.overflowed())
				break;
			continue;
		}

		// ── 捕获(§6.2,批次 A.2)──────────────────────────────────
		//
		// ⚠️★ 三道**可在快照里判定**的前置门在此拦(与逃跑「宠物不能逃」同处):
		//    ① 目标是敌人(`:3826`)· ② 目标带可捕获标记(`:3830`)·
		//    ③ 等级门 `myLv + 5 < targetLv`(`:3834`)。
		//    ★ 第 ④ 道(条件道具)读背包,L3 看不到 ⇒ 留调用方,在调本函数之前拦。
		//    任一门不过 ⇒ 产**捕获失败**事件(`flg=0`),不是"什么都不发生" ——
		//    原版 `BATTLE_Capture` 无论成败都发 `BT|a|r|f|`(`:4225`),客户端要演。
		if (cmd.command_kind == SA::Domain::BattleCommand::CommandKind::CAPTURE)
		{
			const int cap_target = static_cast<int>(cmd.command.capture.target);
			if (cap_target < 0 || cap_target >= kSlotCount)
				continue;
			const Combatant &tgt = field.at(cap_target);
			if (!tgt.occupied || dead[cap_target])
				continue;

			bool ok = false;
			// 前置门 ①②③(§6.2)。★ 等级门:`myLv + 5 < targetLv` 直接失败。
			//   ⚠️ 原版有 `PickAllPet`(全收特殊技)可跳过等级门,属技能链路(B 批次)
			//     ⇒ 本批次不接,在此按"无该技"处理,实现处记明、不猜。
			if (tgt.isEnemy() && tgt.mods.capturable &&
			    !(actor.level + kCaptureLevelGate < tgt.level))
			{
				ok = rollCapture(actor.level, tgt.level, actor.quick, tgt.quick,
				                 actor.charm, actor.luck, hp[cap_target],
				                 tgt.max_hp, tgt.mods.capture_difficulty,
				                 actor.mods.capture_bonus,
				                 isAsleep(tgt), rng);
			}

			SA::Domain::BattleEvent *ev =
			    sink.push(SA::Domain::BattleEvent::BodyKind::CAPTURE_ACT);
			if (ev == nullptr)
				break;
			ev->body.capture_act.actor = static_cast<std::uint32_t>(actor_slot);
			ev->body.capture_act.target = static_cast<std::uint32_t>(cap_target);
			ev->body.capture_act.flags = ok ? 1u : 0u; // 原 `f%X`:成功=1
			// ★ 成功后的世界写(生成宠物 / 目标离场 / 删条件道具 DR-BT10 /
			//   capture_bonus 清零)由调用方按事件执行 —— L3 不写世界态。
			if (sink.overflowed())
				break;
			continue;
		}

		if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::ATTACK)
		{
			// GUARD 与 WAIT 本身不产事件:防御的效果体现在**被攻击时**的减伤(§3.5),
			// 由下方攻击链路读 `IsGuarding` 得到。
			// ⚠️ PET_IN / PET_OUT / USE_ITEM / 技能 / 咒术仍落这里被跳过 ——
			//    绑在批次 A.3+ / B / C 的链路上(见 battle.h 的表)。
			//    ★ 换宠(PET_IN/OUT)**不在 A.2** —— 它无判定阈,真实成本是把宠物建模成
			//      独立战斗槽单位入场(BATTLE_PetDefaultEntry),属 L2 实体族(1.2),
			//      1.5 的 Combatant 只有 has_ride 骑乘、没有槽位宠 ⇒ 排在 L2 之后。
			continue;
		}

		const int target_slot = static_cast<int>(cmd.command.attack.target);
		if (target_slot < 0 || target_slot >= kSlotCount)
			continue;
		const Combatant &target = field.at(target_slot);
		if (!target.occupied || dead[target_slot])
			continue;

		// ── 攻击次数(§3.9 / DR-BT1)──────────────────────────────
		const int hits = rollAttackCount(actor, config, rng);

		SA::Domain::BattleEvent *hit_event =
		    sink.push(SA::Domain::BattleEvent::BodyKind::HIT);
		if (hit_event == nullptr)
			break;
		hit_event->body.hit.attacker = static_cast<std::uint32_t>(actor_slot);
		hit_event->body.hit.kind = SA::Domain::AttackKind::ATTACK_KIND_MELEE;
		hit_event->body.hit.skill_id = 0;
		hit_event->body.hit.variant = 0;
		hit_event->body.hit.target_count = 0; // ★ 逐段回填,见下

		const bool guarding = commands.present[target_slot] &&
		                      isGuarding(commands.commands[target_slot]) &&
		                      target.confusion <= 0;
		const bool casting = isCastingSpell(commands, target_slot);

		std::uint32_t emitted = 0;
		for (int h = 0; h < hits; ++h)
		{
			// ★ 目标在多段之间可能被打死 ⇒ 剩余段数作废(原版同样逐段查存活)。
			if (dead[target_slot])
				break;

			SA::Domain::BattleEvent *dmg_event =
			    sink.push(SA::Domain::BattleEvent::BodyKind::DAMAGE);
			if (dmg_event == nullptr)
				break;
			SA::Domain::Damage &d = dmg_event->body.damage;
			d.target = static_cast<std::uint32_t>(target_slot);
			d.hp_delta = 0;
			d.pet_hp_delta = 0;
			d.mp_delta = 0;
			d.flags = 0;
			d.status_applied = SA::Domain::BattleStatus::BATTLE_ST_NONE;
			++emitted;

			// ── 回避(§3.2)───────────────────────────────────────
			//
			// ⚠️ 闪避也要产事件:客户端要演"闪"这个动作(原版 BD 带 BCF_DODGE)。
			//    ★ 而且**必须在这里就产**,不能"闪了就跳过" —— 事件流是演出脚本,
			//      少一条客户端就少一个动作,1.4 的验收口径正是逐条一致。
			if (rollDodge(actor, target, guarding, casting, config, rng))
			{
				d.flags = static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_DODGE);
				continue;
			}

			// ── 暴击(§3.3,批次 A.3)──────────────────────────────
			//
			// ⚠️★ **判定必须在算伤害之前、且无论命中与否都消费同一个 RNG 抽取** ——
			//    原版 `AttackSeq`(`:1590`)先 `RAND(1,10000)` 判暴击,再据结果选伤害源:
			//      暴击 + 非弓 → CriDamageCalc(带防御附加);暴击 + 弓 / 未暴击 → DamageCalc。
			//    ★ 持弓不吃暴击伤害加成(`:1594`),但**仍置暴击标志**(客户端要演"会心")。
			//    ⇒ 顺序即 RNG 序列的一部分,换位置 ⇒ 同种子给出不同战斗。
			const bool is_crit = rollCritical(actor, target, rng);
			std::int32_t damage;
			if (is_crit && !actor.mods.wielding_bow)
			{
				damage = computeCriticalDamage(field, actor, target, config, rng);
			}
			else
			{
				damage = computeDamage(field, actor, target, config, rng);
			}
			if (is_crit)
			{
				d.flags |= static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_CRITICAL);
			}

			// ── 防御减伤:六档随机(§3.5)────────────────────────────
			//
			// ⚠️★ 触发条件是「守方指令 = 防御 **且 混乱值 ≤ 0**」——
			//    两条都在 `guarding` 里,别只判指令。
			if (guarding)
			{
				damage = static_cast<std::int32_t>(damage * rollGuardFactor(rng));
				d.flags |= static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_GUARD);
			}
			else if (!is_crit)
			{
				// ★ NORMAL 与 CRITICAL 互斥(原版 `BCF_NORMAL` / `BCF_KAISHIN` 是 switch(iRet)
				//   的两个分支)⇒ 暴击命中**不**再置 NORMAL。守方防御时置 GUARD(项目自有建模)。
				d.flags |= static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_NORMAL);
			}
			if (damage < 0)
				damage = 0;

			// ── 骑宠分摊(§3.6,DR-BT2 修正式)──────────────────────
			std::int32_t to_player = damage;
			std::int32_t to_pet = 0;
			if (target.has_ride && pet_hp[target_slot] > 0)
			{
				const RideSplit split =
				    splitRideDamage(damage, target.defense, target.ride_defense);
				to_player = split.player;
				to_pet = split.pet;
			}

			// ★ 溢出量(原 `addpoint`,`:2040`):打前 HP 减伤害若为负,取其绝对值。
			//   ⚠️★ 打飞看的是**主人 HP** 的溢出(`BATTLE_DamageSub` 里 `hp` 是守方本体),
			//     不是骑宠;骑宠分摊只改 to_player 的数值 ⇒ 用打到主人身上的 to_player 算。
			const std::int32_t hp_before = hp[target_slot];
			const std::int32_t overflow =
			    (hp_before - to_player < 0) ? (to_player - hp_before) : 0;

			hp[target_slot] -= to_player;
			pet_hp[target_slot] -= to_pet;
			d.hp_delta = -to_player;
			d.pet_hp_delta = -to_pet;

			// ★ 骑宠死亡的连带(§3.6:解除骑乘 + 换回原图 + 置落马标记)在**表现侧**,
			//   由调用方按 `pet_hp_delta` 打完后的 HP 判定并下发 BattleSnapshot。
			//   ⇒ L3 不产 RideState —— 那是快照字段,不是事件。

			// ── 打飞判定(§3.8,批次 A.4)────────────────────────────
			//
			// ⚠️★ **必须在死亡标记之前**:原版 `BATTLE_DamageSub` 先算 IsUltimate、再由外层
			//    据 HP<=0 判死并按打飞标志分流战果 ⇒ 打飞与死亡可同回合并存(一击致死且打飞)。
			//    ★ 判定进 L3,累加器的持久化与清零走事件由调用方在 ApplyEvents 落地。
			const std::int32_t prev_acc = ult_acc[target_slot];
			const KnockbackKind kb = rollKnockback(
			    damage, overflow, target.max_hp, ult_acc[target_slot],
			    target.mods.immune_knockback, &ult_acc[target_slot]);
			if (kb == KnockbackKind::kOneShot)
			{
				d.flags |= static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_ULTIMATE_2);
			}
			else if (kb == KnockbackKind::kAccumulated)
			{
				d.flags |= static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_ULTIMATE_1);
			}
			// ★ 累加器**变化时**才回写(单开低频事件,不塞进热路径的 Damage —— 见
			//   battle_events.proto 的 KnockbackState 注记:塞 Damage 会越过 8 KB 零分配红线)。
			//   ⚠️ 累加后的新值由 L3 给,ApplyEvents 直接写、不重算(免疫+一击角落会分叉)。
			if (ult_acc[target_slot] != prev_acc)
			{
				SA::Domain::BattleEvent *kbev =
				    sink.push(SA::Domain::BattleEvent::BodyKind::KNOCKBACK_STATE);
				if (kbev == nullptr)
					break;
				kbev->body.knockback_state.target = static_cast<std::uint32_t>(target_slot);
				kbev->body.knockback_state.accumulator = ult_acc[target_slot];
			}

			if (hp[target_slot] <= 0)
			{
				dead[target_slot] = true;
				d.flags |= static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_DEATH);
			}

			// ⚠️ 反击(§3.5)在此处插入 —— 批次 0.5 未实现,理由见 battle.h。
		}

		hit_event->body.hit.target_count = emitted;
		if (sink.overflowed())
			break;
	}

	// ★ 返回 false = 被迫截断。调用方**必须**处理(分包),不得当成"成功"。
	return !sink.overflowed();
}

} // namespace SA::Rules
