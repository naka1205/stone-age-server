// shared/rules/battle.h —— L3 战斗结算的契约
//
// ★★ 这是 D2「一份规则两端编译」的落点。本文件与它的实现被**服务端与客户端
//    编译同一份源码**(客户端经 CMake FetchContent + 锁定 tag 引用,DR-TS3)。
//
// 契约(05-battle.md §1.5):
//     resolve_turn(BattleSnapshot, Commands, IRandom&) -> BattleEvents
//
// ── 四条不可违反的性质 ────────────────────────────────────────
//   ① **纯函数**:除 `out` 与 `rng` 外不写任何东西。不读全局、不读时钟、不做 I/O。
//   ② **事件是返回值,不是副作用**:世界状态由调用方按事件列表应用。
//      ⇒ 这正是 0.1 四步改造第②③步要达到的形态。
//   ③ **随机源只经 `rng`**:见 random.h。一处漏网,黄金用例集失去意义。
//   ④ **可回放**:同种子 + 同输入 ⇒ 输出逐位相同。
//
// ⚠️ ①–④ 不是编码风格,是 00 §0 中 ③ 层「规则能写出来但无法自证正确」的
//    **唯一补偿手段**的前提。破坏任何一条,补偿就不成立。
//
// ── 依赖边界(02 §9)──────────────────────────────────────────
//   ✅ 可依赖:`idl/generated/cpp/domain/**`(事件与领域值对象)
//   ❌ 不得依赖:`idl/generated/cpp/transport/**`(信封、握手、错误面)、
//               socket / MySQL / Redis / 日志 / 任何 src/ 下的头
//   ⇒ 由 tools/check_shared_purity.py 强制(CI 必跑)。

#ifndef SG_SHARED_RULES_BATTLE_H
#define SG_SHARED_RULES_BATTLE_H

#include "domain/battle_events.sg.h"
#include "domain/battle_status.sg.h"

#include "rules/combatant.h"
#include "rules/config.h"
#include "rules/random.h"

namespace sg::rules {

// 本回合各槽的指令。
//
// ⚠️ 指令的**合法性校验发生在调用之前**(05 §1.5:「已通过合法性校验」)。
//    L3 不做鉴权、不判「这个技能你学过没」—— 那需要读角色的技能表,
//    会把 L3 的输入面撑大到整个角色模型。
struct TurnCommands {
  sg::domain::BattleCommand commands[kSlotCount]{};
  bool present[kSlotCount]{};   // 该槽本回合是否有指令(敌方由 AI 填,视为齐备)
};

// ── DR-BT5:唯一的「能否行动」判定 ─────────────────────────────
//
// ★★ 原版有**两套判据**:
//      checkErrorStatus(上行校验)  5 项
//      CanMoveCheck   (结算否决)  8 项
//    差集是**魔障 / 雷附体 / 世界末日集气** —— 只被结算否决,上行不拒绝
//    ⇒ 玩家「能点但必然作废」,是假交互(与 DR-CP6 直接冲突)。
//
// ✅ 裁定 = **修正**:统一为 CanMoveCheck 的 8 项,**本函数是唯一真源**,
//    上行校验与结算都调它;并把原因下发给客户端 → 菜单置灰(DR-CP7)。
//
// ⚠️ 原版注释掉那三项是有动机的(魔障**永不自然解除** ⇒ 硬拒绝会让玩家从中魔障
//    那一刻起到战斗结束彻底失去交互)。**动机成立,手段选错了** ——
//    正确做法不在"上行拒不拒绝"里选,而是置灰 + 告知原因。
//
// 返回 CANNOT_ACT_NONE 表示可行动。
sg::domain::CannotActReason CheckCanAct(const Combatant& c) noexcept;

// ── 回合结算 ──────────────────────────────────────────────────
//
// `out` 由调用方提供并被完全覆写。
// ★ 用出参而不是返回值:`domain::BattleEvents` 是 7 KB 的 POD,
//   返回值会带来一次拷贝,与 15 §9.1「运行期零分配」的取向相悖。
//   调用方通常持有一个每场战斗复用的实例。
//
// 返回 false 表示事件数超过 `max_count = 256` 而**被迫截断**。
// ⚠️★ 调用方**必须**处理 false:05 §10.4 记录了原版在状态串上
//    「strncat 第三参用错、等价于无上界 strcat,余量仅 56 字节且无第二道防线」
//    的教训 —— 新实现宁可分包,不可静默截断。
bool ResolveTurn(const BattleField& field,
                 const TurnCommands& commands,
                 const RulesConfig& config,
                 IRandom& rng,
                 sg::domain::BattleEvents& out) noexcept;

// ── 供上层与测试直接调用的子步骤 ──────────────────────────────
//
// ★ 单独暴露不是为了"方便",是因为 07 §11.3 判据 ① 实测
//   **四条主公式(伤害 / 回避 / 暴击 / 相克)已经天然是纯的** ——
//   它们是 L3 里最该被用例覆盖的部分,也是 ③ 层不可自证的主要补偿点。

// 四属性相克系数。结果已含 §3.4 的 /10000。
double ElementCoefficient(const Combatant& attacker, const Combatant& defender) noexcept;

// 伤害主公式(§3.1 七步)。
//
// ⚠️★ 移植注记 —— 三处**必须原样保留**的原版行为:
//   ① 防御修正的括号是 `(defense × rand + 2) / 100`,**不是** `defense × (rand + 2)/100`。
//      `rand()%10 == 0` 时该项 = 2/100 = 0(整数除法)。两版一致 ⇒ 不要"修正"括号。
//   ② 三分段主公式的第二分支上界用**浮点** `defense*8.0/7.0`、
//      第三分支下界用**整数除法** `defense*8/7` ⇒ 边界处有**两分支都不命中的窄缝**,
//      此时 damage 保持初值 0。两版一致 ⇒ **保留窄缝**。
//   ③ 第 7 步 `× getDamageCalc()/100` 的默认值是 **70 不是 100**
//      (RulesConfig::damage_calc_percent)。
std::int32_t ComputeDamage(const BattleField& field,
                           const Combatant& attacker,
                           const Combatant& defender,
                           const RulesConfig& config,
                           IRandom& rng) noexcept;

// 回避判定。true = 已闪避。
//
// ⚠️ **六道前置否决**任一命中直接返回(不走概率):
//    攻方集气完成 / 守方防御 / 守方有反应类状态 / 守方不能行动 /
//    守方带 NODUCK / ★ 守方自带「必闪」技 ⇒ 直接 true。
bool RollDodge(const Combatant& attacker,
               const Combatant& defender,
               bool defender_casting_spell,
               const RulesConfig& config,
               IRandom& rng) noexcept;

}  // namespace sg::rules

#endif  // SG_SHARED_RULES_BATTLE_H
