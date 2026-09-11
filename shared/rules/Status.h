// shared/rules/Status.h —— L4 状态异常系统的契约(批次 L4.1)
//
// ★★ 与 `rules/Battle.h` 同属 D2「一份规则两端编译」的落点,同样的四条不可违反的
//    性质(纯函数 / 事件是返回值 / 随机源只经 rng / 可回放)。依赖边界也一致。
//
// ── 移植来源(8.0 树 `StoneAge/gmsv/src/`,行号即该树)──────────────
//   施加判定  `BATTLE_StatusAttackCheck`         `battle/battle_event.c:5063`
//   职业专用  `PROFESSION_BATTLE_StatusAttackCheck`      `battle_event.c:5164`
//   普攻附带  带毒装备(`_SUIT_ADDPART4`)        `battle_event.c:2903`
//   每回合    `BATTLE_StatusSeq`                 `battle/battle.c:5423`
//   毒伤害    `Compute_Down`                     `battle/battle.c:5242`
//
// ── ★★★ 本模块最重要的单条结构事实:全局互斥(`05` §4.1)──────────
//
//     for (i = 1; i < BATTLE_ST_END; i++)          /* ★ 扫全部 43 种 */
//         if (CHAR_getWorkInt(defindex, StatusTbl[i]) > 0) return FALSE;
//
//   ⇒ 目标身上只要有**任意一种**状态异常,新状态一律施加失败。
//     **不是同类互斥,是全类互斥** ⇒ 状态系统是「**单槽状态机 + 43 个候选**」,
//     不是「多个独立计时器」。⚠️ 按位图/多字段并存建模,行为会与原版完全不同
//     (玩家会同时中毒+麻痹+沉默,而原版永远只有一种)。
//   ⇒ 这正是 `Combatant::status` 是**一个** `uint8` 而不是位图的原因。
//
// ── ⚠️★★ 双轨制:职业技能路径能绕过互斥,但只有 11 种能绕 ─────────
//
//   `05` §4.1 记「职业技能专用的施加路径**没有这个互斥循环**」—— **那是错的**
//   (2026-09-11 回源码核实,纪律 ①)。`PROFESSION_BATTLE_StatusAttackCheck`
//   在 `:5195` **有**同一个互斥循环,只是在它**之前**有两处 `_PROFESSION_ADDSKILL`
//   早退:① 三属抗 `BATTLE_ST_RESIST_F_I_T`(查三个具体抗性槽后 `return 1`)·
//   ② 冰爆术 1–10(直接 `return 1`)。
//   ⇒ **能叠加的只有这 11 种,不是全部职技** —— 按文档写会让所有职技都能叠加。
//   ⇒ 这 11 种全部落在状态 31..43 / 15,属宠技职技域(未移植)⇒ 本批不实现该路径,
//     但把这条判据记在此处,接职技那一批直接用。
//
// ── 本批(L4.1)的覆盖面:状态 1..11(用户 2026-09-11 拍板)────────
//
//   毒 1 / 麻痹 2 / 睡眠 3 / 石化 4 / 酒醉 5 / 混乱 6 / 虚弱 7 / 剧毒 8 /
//   魔障 9 / 沉默 10 / 毒煞 11。
//   ★ 这个边界**不是我划的,是源码的宏块边界**:`StatusTbl` 的基础段 10 项 +
//     `_PET_SKILL_SARS` 的毒煞 1 项,其后才是 `_PROFESSION_SKILL` 的 19 项。
//     ⇒ 余下 32 种属宠技/职技域,随那些域接入。
//   ★ 它们也恰好是 `05` §4.6「关键状态的六维行为」表给全了的那 11 种。

#ifndef __SA_Status_H__
#define __SA_Status_H__

#include "domain/battle_status.sa.h"

#include "rules/Combatant.h"
#include "rules/RandomSource.h"

#include <cstdint>

namespace SA::Rules
{

// 本批实现的状态数(索引 1..11 有效,0 = BATTLE_ST_NONE)。
constexpr int kStatusImplementedEnd = 12;

// 原版 `BATTLE_ST_END` = 44(状态号合法性的上界,`battle_event.c:5075`)。
//
// ⚠️★ IDL 的 `BattleStatus` 枚举**没有** END 这个取值 —— `battle_status.proto:89`
//    只在注释里写了「★ BATTLE_ST_END = 44」。⇒ 这里补一个常量,**别去枚举里找它**
//    (那会编译失败,而失败信息指向"枚举缺成员",容易被误修成"给枚举加个 END"——
//     加了就会让 44 变成一个合法的 BattleStatus 值,污染 wire 上的取值集合)。
constexpr int kBattleStatusEnd = 44;

// ⚠️★★ 原版 `RegTbl` 的**真实长度 = 31**,不是 44(实测,`battle_event.c:126`:
//    基础 11 + `_PET_SKILL_SARS` 1 + `_PROFESSION_SKILL` 19,**无** `_PROFESSION_ADDSKILL`)。
//    ⇒ 状态 31..43 抵抗恒 0、无法被抵抗;源码 `:5119` 有越界防护 ⇒ 是玩法事实不是崩溃。
//    ★ 用户 2026-09-11 裁定**照抄此缺陷** ⇒ 扩状态时这个上限**仍是 31**。
constexpr int kOriginalResistTableLen = 31;

// 施加成功时写入的回合数偏移。
//
// ⚠️★★ 原版 `battle_event.c:2918` 是 `CHAR_setWorkInt(..., gBattleStausTurn + 1)`
//    —— **落地的回合数比技能声明的多 1**。带毒装备声明 `gBattleStausTurn = 3`
//    ⇒ 实际写入 **4**。★ 与 DR-BT15 逃跑「首次即 2」同族的 `+1` 陷阱:
//    照「声明值」实现会让每种状态都短一回合,而任何"中了没中"的断言都抓不到。
constexpr int kStatusTurnBonus = 1;

// 带毒装备的状态与回合数(原 `battle_event.c:2904` 硬编码)。
constexpr int kSuitPoisonTurns = 3; // ⇒ 落地 3 + 1 = 4 回合
constexpr int kSuitPoisonRange = 40;
constexpr double kSuitPoisonBai = 2.0;

// 命中率硬上限(`:5153`)。
//
// ⚠️★ 它在源码里位于**通用分支的 `else` 内** ⇒ **麻痹分支不夹这个上限**。
//    麻痹的 per 基数恒为 20 ⇒ 永远 ≤ 80 ⇒ 无可观察后果 ⇒ 属**冗余**,
//    照抄结构但**不声称它要紧**(纪律 ⓪:别给照抄的源码编造理由)。
constexpr int kStatusHitCap = 80;

// 麻痹分支的固定基数(`:5081`)。
//
// ⚠️★ 源码只有 `per = 20; per -= RegTbl[status];` —— **没有装备抗性、没有 fVitalP、
//    没有等级差、没有幸运、没有 `max(per, 0)`**。`05` §4.3 写的「− 装备抗性」与
//    「`max(per, 0)`」在源码里**都不存在**(2026-09-11 核实,纪律 ①)。
//    ★ 缺 clamp 无可观察后果(`RAND(1,100) >= 1` ⇒ per ≤ 0 时恒不命中),
//      同样属冗余 ⇒ 不补,照源码。
constexpr int kParalysisBasePer = 20;

// ── 状态命中判定(`BATTLE_StatusAttackCheck`)────────────────────
//
// ★ 三组调用参数(`05` §4.3,本批只用第一组):
//     普攻附带状态   PerOffset = suit_poison   Range = 40   Bai = 2.0
//     精灵 / 魔法     PerOffset = 技能表 Success  Range = 30   Bai = 1.0
//     魔障 / 沉默技   PerOffset = 技能表 Success  Range = 30   Bai = 1.0
//
// ⚠️★ **摇 rng 的条件**:全局互斥不过 ⇒ **提前返回、不摇**(源码 `:5076-5079`
//    在任何 RAND 之前)。⇒ 与捕获的道具门同族:门不过连骰子都不掷。
//
// `out_per` 回写实际命中率(原版的 `*pPer`,只用于广播文案;保留以便用例断言)。
bool rollStatusAttack(bool is_pvp,
                      const Combatant &attacker,
                      const Combatant &defender,
                      int status,
                      int per_offset,
                      int range,
                      double bai,
                      Random &rng,
                      int *out_per) noexcept;

// ── 毒 / 剧毒的每回合掉血量(`Compute_Down`,`battle.c:5242`)──────
//
//     downs = (((V + S + D + T) / 100) - 20) / 4      ★ 全程整数除法
//     if (downs < 1) downs = 1                        ★ 下限 1
//     if (hp <= downs) downs = hp - 1                 ★★ 留 1 HP,绝不毒死
//
// ⚠️★ 返回值可以是 **0**(hp == 1 时 `downs = 0`)—— 源码 `downs >= 0` 仍成立
//    ⇒ **照旧产出事件**(掉 0 血的 `BD` 串)。⇒ 别把 0 当"什么都没发生"跳过:
//    客户端要演那一下,且它是"毒还在生效"的可观察证据。
// ⚠️ 骑宠独立算一份(源码 `flg != -1` 那半段),同一公式、各自的四维与 HP。
std::int32_t computePoisonDown(std::int32_t vital,
                               std::int32_t str,
                               std::int32_t dex,
                               std::int32_t tough,
                               std::int32_t hp) noexcept;

// ── 每回合状态推进的结果(`BATTLE_StatusSeq` 的纯函数化)──────────
struct StatusTickResult
{
	// 推进后的状态槽与剩余回合(调用方据此写回世界态)。
	std::uint8_t status = 0;
	std::int32_t turns = 0;

	// 本回合该状态造成的掉血(毒);0 也可能是"生效了但只够掉 0"(见上)。
	// ⚠️ 人物与骑宠**各算一份**,同公式各自的四维与 HP ⇒ 两个字段都要用。
	std::int32_t hp_down = 0;
	std::int32_t pet_hp_down = 0;

	// 剧毒的直接死亡(`_MAGIC_DEEPPOISON`,`battle.c:5536`):
	// HP <= 1 或剩余回合 <= 1 ⇒ **直接死**,不走掉血。
	bool deep_poison_kill = false;

	// 本回合状态**解除**了(计时归零)⇒ 调用方产 StatusChange(applied = false)。
	bool cleared = false;

	// ★★ 本回合的指令被状态**清空**(原 `CHAR_setWorkInt(..., BATTLE_COM_NONE)`)。
	//
	// ⚠️★★ 判据取的是**递减之前**的状态(源码 `StatusSeq` 开头 `:5443` 先判
	//    `BATTLE_CanMoveCheck` 再进递减循环)⇒ **状态在本回合到期解除的角色,
	//    这一回合仍然不能行动** —— 指令在解除之前就已经被清掉了。
	//    ★ 主循环在 `StatusSeq` 之后**又判了一次** `CanMoveCheck`(`battle.c:7090`),
	//      但那次用的是递减**后**的状态、且只是再清一次同一个指令 ⇒ **冗余**
	//      (纪律 ⓪:照抄结构,不声称它要紧)。
	bool command_cleared = false;

	// 酒醉解除时的敏捷回写(`battle.c:5490`)。
	//
	// ⚠️★★ `05` §4.4 只写了「解除时 `CHAR_WORKQUICK *= 2`」,**漏了 ridepet 分支**
	//    (2026-09-11 核实,纪律 ①):骑宠在场时是 `quick += 骑宠的 quick`,
	//    **不是** `× 2`。两者只在「骑宠 quick == 玩家 quick」时才相等。
	//   ★ 净效果仍如 §4.4 所述(施加时从不减半 ⇒ 解除后敏捷变大),但幅度不同。
	bool drunk_quick_restore = false;
};

// 推进一个单位的状态一回合。
//
// ⚠️★★ **虚弱 / 魔障使全部计时器冻结**(`05` §4.2,`battle.c:5449-5456`):
//        CHAR_setWorkInt(idx, StatusTbl[i], --cnt);          /* 正常递减 */
//        if (WORKWEAKEN  > 0) CHAR_setWorkInt(idx, ..., cnt+1);  /* ★ 加回去 */
//        if (WORKBARRIER > 0) CHAR_setWorkInt(idx, ..., cnt+1);  /* ★ 加回去 */
//    ⇒ 中虚弱或魔障期间,身上所有状态的回合数**都不减少**。
//    ★★ 又因 §4.1 全局互斥,"身上所有状态"实际**只有虚弱/魔障自己**
//      ⇒ **虚弱与魔障一旦施加就永不自然解除**,只能靠离场/进场清除、马杀鸡、精灵解除。
//    ⚠️ 这是玩法级强约束(等于"持续到战斗结束"),不是 bug ⇒ 照抄。
//
// `hp` / `pet_hp` 取**回合内镜像**(与 Battle.cpp 的 hp[] 同一份),不是快照原值。
StatusTickResult tickStatus(const Combatant &c,
                            std::int32_t hp,
                            std::int32_t pet_hp,
                            bool has_ride_pet) noexcept;

// 施加成功后落地的回合数(`gBattleStausTurn + 1`,见 kStatusTurnBonus)。
constexpr std::int32_t statusTurnsOnApply(int declared_turns) noexcept
{
	return declared_turns + kStatusTurnBonus;
}

// 该状态一旦施加就当场清空目标本回合指令吗?(`battle_event.c:2932-2937`)
//
// ★ 源码只列了**四种**:麻痹 / 睡眠 / 石化 / 魔障。
// ⚠️ 它比 `checkCanAct` 的 8 项**窄** —— 晕眩 / 天罗 / 雷附体 / 集气不在此列。
//    那不矛盾:那四种由 `checkCanAct` 在派发时否决,只是不在"施加当场"清指令。
bool clearsCommandOnApply(int status) noexcept;

} // namespace SA::Rules

#endif // __SA_Status_H__
