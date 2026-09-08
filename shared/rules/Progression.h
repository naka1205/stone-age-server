// shared/rules/Progression.h —— 成长养成:属性推导(DR-DT9)
//
// ★★ 与 Battle.h 同为 D2 双端共享的纯函数层(shared/rules,客户端经 FetchContent +
//    锁定 tag 编译同一份源码)。本文件回答 Combatant.h「三围是**回合准备阶段已重算
//    完毕的值**」(§169-176)里的**那次重算的基础段**:把角色的原始四维
//    (vital / str / tough / dex)推导成基础战斗三围(attack / defense / quick)+ 最大 HP。
//
// ── 移植来源 ────────────────────────────────────────────────────
//   `CHAR_complianceParameter`(展开视图 `char/char.c:3525`)本身是**编排器** ——
//   它调 `CHAR_initcharWorkInt` 建基础值 → `ITEM_equipEffect` / `Other_DefcharWorkInt`
//   叠装备 → 把 `WORKFIX{STR,TOUGH,DEX}` 拷进 `WORKATTACKPOWER/DEFENCEPOWER/QUICK`。
//   ★ 真正的公式在 `CHAR_initcharWorkInt`(`char/char.c:3419-3487`),本文件移植它的
//     **基础段**。装备 / 套装 / 技能加成(`ITEM_equipEffect` + `Other_DefcharWorkInt`,
//     item.c:1599)对**无装备单位输入全 0 即恒等** ⇒ 本批不含,接口留待装备系统(DR-DT9)。
//
// ⚠️★ **公式里没有 `level`。** 欠债 23 / DR-BT19-21 都转述为「从原始属性 **+ 等级** +
//    装备推导」—— 但源码里三围推导**全程不读等级**。等级的作用在「四维随等级成长」
//    (`PARAM_CAL`,成长率消费,另一批),不在这一步。回源码复核纠正的又一处文档分叉。
//
// ── 契约(比 Battle 更严)──────────────────────────────────────────
//   ① 纯函数:只吃四维、只吐派生值。不读全局 / 时钟 / 随机 —— **连 rng 都不需要**。
//   ② 可回放:同输入逐位相同。依赖 shared/CMakeLists.txt 的 `-ffp-contract=off`
//      (公式含 `*0.01` 浮点,FMA 合并会让两端逐位不同)。
//
// ⚠️ shared/ 只依赖标准库(01 §4);本文件连 domain/ 都不引,是最小依赖面。

#ifndef __SA_Progression_H__
#define __SA_Progression_H__

#include <cstdint>

namespace SA::Rules
{

// 属性推导的产物 —— 基础战斗三围 + 最大 HP。
//
// ⚠️★ **不含 max_mp**:原版 `WORKMAXMP = CHAR_MAXMP`(`char.c:3488`)是**直接读字段**,
//    不推导 ⇒ 建进来会造出第二个真源。调用方直接用实体的 `max_mp`。
struct DerivedStats
{
	std::int32_t attack = 0;  // 原 WORKATTACKPOWER ← WORKFIXSTR
	std::int32_t defense = 0; // 原 WORKDEFENCEPOWER ← WORKFIXTOUGH
	std::int32_t quick = 0;   // 原 WORKQUICK ← WORKFIXDEX
	std::int32_t max_hp = 0;  // 原 WORKMAXHP
};

// 属性推导 —— 原始四维 → 基础战斗三围 + 最大 HP。
// 1:1 移植 `CHAR_initcharWorkInt`(`char/char.c:3419-3487`)的基础段。
//
// ★ DR-DT9:截断语义 = **复刻原版整数截断**(用户 2026-09-08 拍板)。原版每个
//   `CHAR_setWorkInt` 第三参是 `int`,浮点表达式传入即截断一次 ⇒ 本函数每个输出
//   都是「double(或 float)公式算完 `static_cast<std::int32_t>` 截断一次」。
//   ⚠️ 与 DR-DT1(成长率 `E_T_LVUPPOINT` 默认**不**截断、保留浮点设计意图)方向相反,
//     不矛盾:那是连续的成长系数(atoi 截断是解析损失),这是本就为整数的战斗三围。
DerivedStats deriveBaseStats(std::int32_t vital, std::int32_t str,
                             std::int32_t tough, std::int32_t dex) noexcept;

} // namespace SA::Rules

#endif // __SA_Progression_H__
