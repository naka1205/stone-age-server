// shared/rules/Status.cpp —— L4 状态异常系统的实现(批次 L4.1)
//
// 契约与取证依据见 rules/Status.h 卷首。
//
// ── 宏门控的处置(D7:8.0 单基线,不迁移宏)──────────────────────
//   已逐条回 `StoneAge/gmsv/src/include/version.h` 核实(2026-09-11):
//   `_SUIT_ADDPART4`(带毒装备)· `_SUIT_ADDENDUM`(通用抗性)·
//   `_EQUIT_RESIST`(三种装备抗性)· `_SUIT_ADDPART3`(虚弱第二道)·
//   `_PET_SKILL_SARS`(毒煞)· `_MAGIC_WEAKEN` · `_MAGIC_BARRIER`(计时冻结)·
//   `_MAGIC_DEEPPOISON`(剧毒直死)· `_PROFESSION_SKILL` · `_PROFESSION_ADDSKILL`
//   **全部为开** ⇒ 一律实现开启态,且不留 `#if`(`02` §1.1 规则 4)。
//   `_SUIT_ADDPART2` **为关** ⇒ 对应分支不实现。

#include "rules/Status.h"

#include "rules/Battle.h" // ★ 只为 checkCanAct —— DR-BT5 的唯一真源,不得在此重写一遍

namespace SA::Rules
{
namespace
{

using SA::Domain::BattleStatus;

// 守方对某状态的抵抗值(原 `RegTbl[status]` → `CHAR_getWorkInt`)。
//
// ⚠️★★ 照抄原版的**越界即取 0**(`battle_event.c:5119`):
//        if (status >= arraysizeof(RegTbl) || status < 0) Df_Reg = 0;
//    源码的 `arraysizeof(RegTbl)` 实测是 **31**(见 Status.h 的 kOriginalResistTableLen)
//    ⇒ 状态 31..43 抵抗恒 0。本批只到 11,两个上限都在这里显式表达 ——
//    ★ 别把 `status_resist` 的数组长度当成语义上限:**语义上限是 31**,
//      数组长度只是本批实现到哪。
std::int32_t resistOf(const Combatant &defender, int status) noexcept
{
	if (status < 0 || status >= kOriginalResistTableLen)
		return 0; // 原版越界防护 ⇒ 31..43 无法被抵抗(用户裁定照抄)
	if (status >= kStatusImplementedEnd)
		return 0; // 本批未实现到的状态(12..30)——数组里没有它,同样取 0
	return defender.mods.status_resist[status];
}

// F02: SSRC80 battle_event.c:5142–5151 比较 WORK 枚举 51/53/54，
// 而合法 status < 44。这些分支原本不可达，不能按同名状态重新激活。

} // namespace

// ═══════════════════════════════════════════════════════════════════
//  状态命中判定 —— BATTLE_StatusAttackCheck(battle_event.c:5063)
// ═══════════════════════════════════════════════════════════════════
bool rollStatusAttack(bool is_pvp,
                      const Combatant &attacker,
                      const Combatant &defender,
                      int status,
                      int per_offset,
                      int range,
                      double bai,
                      Random &rng,
                      int *out_per) noexcept
{
	if (out_per != nullptr)
		*out_per = 0;

	// ── 前置 ①:状态号合法(`:5075`)────────────────────────────────
	//
	// ⚠️ 原版判的是 `status >= BATTLE_ST_END`(= 44)。IDL 的 `BattleStatus` 枚举
	//    **没有 END 这个取值**(`battle_status.proto:89` 只在注释里写了 44)
	//    ⇒ 用 Status.h 的 `kBattleStatusEnd` 常量,别去枚举里找它。
	if (status >= kBattleStatusEnd || status <= 0)
		return false;

	// ── 前置 ②:★★★ 全局互斥(`:5076-5079`)──────────────────────
	//
	// 原版扫全部 43 项 `StatusTbl`,任一 > 0 即失败。我们的模型是**单槽**
	// (`Combatant::status`)⇒ 等价条件是「槽非空」。
	// ⚠️★ 这个等价性成立**只因为**单槽建模本身就是全局互斥的产物 ——
	//    若将来有人把 status 改成位图,这一行会**静默地**只挡住一种状态。
	//    ⇒ Combatant.h 的注释与本行是同一条约束的两半,改一处必须改两处。
	// ⚠️★★ 它在**任何 RAND 之前** ⇒ 互斥不过时**不摇 rng**(同捕获的道具门 ④)。
	if (defender.status != static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_NONE))
		return false;

	int per = 0;

	if (status == static_cast<int>(BattleStatus::BATTLE_ST_PARALYSIS))
	{
		// ── 麻痹走独立分支(`:5080-5084`)────────────────────────────
		//
		// ★ **固定基数 20**:不看等级差、不看幸运、不看体力、**没有装备抗性**、
		//   **没有 `max(per, 0)`**、**不夹 80 上限**。
		//   ⚠️ `05` §4.3 写了「− 装备抗性」与「max(per,0)」,源码都没有(纪律 ①)。
		//     缺 clamp 无可观察后果(`RAND(1,100) >= 1`)⇒ 照源码不补。
		per = kParalysisBasePer;
		per -= resistOf(defender, status);
	}
	else
	{
		// ── 通用分支(其余 42 种,`:5085-5155`)──────────────────────

		// ★ 体力占比。源码全程 float,`fVitalP` 先被当"四维和"用、再被覆盖成占比
		//   —— 这里拆成两个名字,数值路径不变。
		const float sum = static_cast<float>(defender.vital + defender.str +
		                                     defender.tough + defender.dex);
		// ⚠️ 原版没有除零保护:四维全 0 时 `templP` 是 inf/nan。
		//    我们的四维来源(rollSpawnStats / deriveBaseStats)恒 > 0,
		//    但 L3 不能假设调用方 —— ⇒ 显式挡 0 并**记明这是与原版的已知差**:
		//    原版在此会产出 nan 并让 `per` 变成未定义值(UB 面),不可复刻。
		float vital_p = 0.0F;
		if (sum > 0.0F)
			vital_p = static_cast<float>(defender.vital) / sum;

		// ★ 毒煞**反过来**取(`_PET_SKILL_SARS`,`:5096`):体力越高越容易中。
		if (status == static_cast<int>(BattleStatus::BATTLE_ST_SARS))
			vital_p = (1.0F - vital_p) * 0.9F;

		// ★ `/ 0.25` 然后 `× 10.0`。
		// ⚠️ 源码在 `× 10.0` 处按 `CHAR_WHICHTYPE` 分了**四条分支,而四条的值完全相同**
		//    (`:5102-5116`:玩家 / 宠物 / 敌人 / 其余 一律 `*= 10.0`)
		//    ⇒ 退化成一条(`05` §4.3 已注明「新实现只需一条」)。
		//    ★ 这是**冗余分支**:照抄成四个 if 不会更忠实,只会让人以为类型要紧。
		vital_p = vital_p / 0.25F * 10.0F;

		// ★ 等级差 × Bai,夹到 ±Range;PvP 时恒 0(`:5127-5134`)。
		int level = 0;
		if (!is_pvp)
		{
			level = attacker.level - defender.level;
			level = static_cast<int>(static_cast<double>(level) * bai);
		}
		if (level > range)
			level = range;
		if (level < -range)
			level = -range;

		// ★ 主式(`_SUIT_ADDENDUM` 开 ⇒ 减通用抗性,`:5136`)。
		// ⚠️★ `vital_p` 是 float 而 `per` 是 int ⇒ **这一行有隐式截断**,
		//    是逐位一致的关键点之一 ⇒ 保留源码的运算顺序与类型,别先聚合再转换。
		per = static_cast<int>(static_cast<float>(per_offset + level + attacker.luck -
		                                          resistOf(defender, status)) -
		                       vital_p -
		                       static_cast<float>(defender.mods.general_resist));

		// ★ 命中率硬上限 80%(`:5153`)—— 在 else 内 ⇒ 麻痹不吃(见 Status.h)。
		if (per > kStatusHitCap)
			per = kStatusHitCap;
	}

	if (out_per != nullptr)
		*out_per = per;

	// ★ 判定:`RAND(1, 100) < per`(`:5159`)—— **严格小于**(同暴击/逃跑/捕获)。
	return rng.rand(1, 100) < per;
}

// ═══════════════════════════════════════════════════════════════════
//  毒 / 剧毒的每回合掉血 —— Compute_Down(battle.c:5242)
// ═══════════════════════════════════════════════════════════════════
std::int32_t computePoisonDown(std::int32_t vital,
                               std::int32_t str,
                               std::int32_t dex,
                               std::int32_t tough,
                               std::int32_t hp) noexcept
{
	// ★ 全程整数除法(源码 `downs` 是 int)——`(((V+S+D+T)/100) - 20) / 4`。
	// ⚠️★ **照抄两步除法的形状,但不声称"合并会算错"**:穷举 Σ∈[0,2e6] 实测,
	//    两式仅在结果为负的区间分叉 495 处,而那一段被下面的「下限 1」全部吃掉
	//    ⇒ **clamp 之后 0 处分叉 = 等价**。照抄是为了与源码逐字一致,不是行为必需。
	//    (等价性要验不能推 —— 本注释的初稿写的是"不可合并",是编造的理由。)
	std::int32_t downs = vital + str + dex + tough;
	downs = ((downs / 100) - 20) / 4;
	if (downs < 1)
		downs = 1; // ★ 下限 1(四维不足时也掉 1 血)

	// ★★ **留 1 HP,毒绝不致死**(`:5258`)。
	// ⚠️ hp == 1 时 `downs = 0` ⇒ 返回 0,而调用方**照旧产出事件**
	//    (源码 `downs >= 0` 成立)⇒ 0 不代表"没生效"。
	if (hp <= downs)
		downs = hp - 1;
	if (downs < 0)
		downs = 0; // hp == 0(已死)时源码 `downs >= 0` 不成立 ⇒ 什么都不写

	return downs;
}

// ═══════════════════════════════════════════════════════════════════
//  每回合状态推进 —— BATTLE_StatusSeq(battle.c:5423)
// ═══════════════════════════════════════════════════════════════════
StatusTickResult tickStatus(const Combatant &c,
                            std::int32_t hp,
                            std::int32_t pet_hp,
                            bool has_ride_pet) noexcept
{
	StatusTickResult r;
	r.status = c.status;
	r.turns = c.status_turns;

	const int status = static_cast<int>(c.status);

	// ── 第 1 步:不能行动 ⇒ 清指令(`:5443`)────────────────────────
	//
	// ⚠️★★ **判据取递减之前的状态** ⇒ 本回合到期解除的角色,这一回合仍不能行动。
	//    这一步在递减循环**之前**,顺序即语义,不可后移。
	if (checkCanAct(c) != SA::Domain::CannotActReason::CANNOT_ACT_NONE)
		r.command_cleared = true;

	// 槽空 ⇒ 递减循环里 `cnt <= 0 continue` ⇒ 什么都不做。
	if (status == static_cast<int>(BattleStatus::BATTLE_ST_NONE) || c.status_turns <= 0)
		return r;

	// ── 第 2 步:递减 + 冻结(`:5448-5457`)────────────────────────
	//
	// ★★ 虚弱 / 魔障把递减**加回去** ⇒ 净效果不减 ⇒ 永不自然解除(见 Status.h)。
	//    ⚠️ 照抄源码的"先减再加回"而不是"判断后不减" —— 两者在本批等价,
	//      但源码那个形状是「所有状态都减、虚弱/魔障再补回」,而全局互斥让
	//      "所有状态"退化成它自己。★ 若将来职技路径能叠加状态(那 11 种),
	//      这个形状会开始产生差别:被叠加的状态也会被冻结。⇒ 保留源码形状。
	std::int32_t cnt = c.status_turns - 1;
	const bool frozen = (status == static_cast<int>(BattleStatus::BATTLE_ST_WEAKEN)) ||
	                    (status == static_cast<int>(BattleStatus::BATTLE_ST_BARRIER));
	if (frozen)
		cnt = cnt + 1; // ★ 加回去(源码是两个独立的 if,虚弱与魔障互斥 ⇒ 只会命中一个)
	r.turns = cnt;

	// ── 第 3 步:归零 ⇒ 解除(`:5475-5501`)────────────────────────
	if (cnt <= 0)
	{
		r.cleared = true;
		r.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_NONE);
		r.turns = 0;

		// ★ 酒醉解除:敏捷回写(`:5490`)。
		// ⚠️★★ 有 ridepet 分支 —— 骑宠在场时 `quick += 骑宠 quick`,**不是 ×2**
		//    (`05` §4.4 漏了这一半)。★ 具体数值由调用方按 `has_ride_pet` 算
		//    (L3 看不到骑宠的 quick;它在 field 的另一个槽里)⇒ 这里只置标志。
		if (status == static_cast<int>(BattleStatus::BATTLE_ST_DRUNK))
			r.drunk_quick_restore = true;

		// ⚠️ 源码此处 `continue` ⇒ **解除的当回合不再结算该状态的伤害**
		//    (毒在最后一回合不掉血)。顺序即语义。
		return r;
	}

	// ── 第 4 步:未归零 ⇒ 按状态结算(`:5503` 的 switch)────────────
	switch (status)
	{
	case static_cast<int>(BattleStatus::BATTLE_ST_POISON):
	{
		// ★ 毒:**人物与骑宠各算一份**(`Compute_Down`,`battle.c:5251` / `:5267`)——
		//   同一条公式,各自的四维、各自的 HP、各自的"留 1 HP"夹取。
		// ⚠️★ 漏掉骑宠那半边不会有任何报错(骑宠照常在场、照常分摊伤害,只是毒
		//    不掉它的血)⇒ 与 M.1 断线回收漏了宠物同族 ⇒ 两半都算,用例两半都断言。
		r.hp_down = computePoisonDown(c.vital, c.str, c.dex, c.tough, hp);
		if (has_ride_pet)
		{
			r.pet_hp_down = computePoisonDown(c.ride_vital, c.ride_str, c.ride_dex,
			                                  c.ride_tough, pet_hp);
		}
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_DEEPPOISON):
	{
		// ★★ 剧毒(`_MAGIC_DEEPPOISON`,`battle.c:5536-5556`):
		//    **HP <= 1 或剩余回合 <= 1 ⇒ 直接死亡**,不掉血、不留 1 HP。
		// ⚠️ 判据用的是**递减前**的 `CHAR_WORKDEEPPOISON`(源码 `:5546` 读的是
		//    work 值,而 `--cnt` 已经写回去了 ⇒ 实际读到的是**递减后**的值)。
		//    ⇒ 这里用 `cnt`(递减后),与源码一致。
		if (hp <= 1 || cnt <= 1)
			r.deep_poison_kill = true;
		break;
	}
	default:
		// 其余状态本回合无结算后果(麻痹/睡眠/石化只是"不能行动",
		// 由 checkCanAct 在派发时否决;酒醉/混乱的后果落在回避与目标选择里)。
		break;
	}

	return r;
}

bool clearsCommandOnApply(int status) noexcept
{
	// 源码 `battle_event.c:2932-2937` 只列这四种。
	// ⚠️ 比 checkCanAct 的 8 项窄,不矛盾(见 Status.h)。
	return status == static_cast<int>(BattleStatus::BATTLE_ST_PARALYSIS) ||
	       status == static_cast<int>(BattleStatus::BATTLE_ST_SLEEP) ||
	       status == static_cast<int>(BattleStatus::BATTLE_ST_STONE) ||
	       status == static_cast<int>(BattleStatus::BATTLE_ST_BARRIER);
}

} // namespace SA::Rules
