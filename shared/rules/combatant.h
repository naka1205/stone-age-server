// shared/rules/combatant.h —— L3 结算的输入视图
//
// ★★ 本文件回答的是 0.1 四步改造的**前置问题**:
//    05-battle.md §1.5 的契约是
//        resolve_turn(BattleSnapshot, Commands, Random&) -> BattleEvents
//    其中 Commands 与 BattleEvents 已由 IDL 给出,而**输入侧一直没有定义**。
//
// ⚠️★ **与 `SA::Domain::BattleSnapshot` 同名不同物,不要混用**:
//
//    | | `Domain::BattleSnapshot`(IDL) | 本文件的 `BattleField` |
//    |---|---|---|
//    | 用途 | **下行显示**快照(原 BC 子命令) | **L3 结算输入** |
//    | 内容 | 名字 / 等级 / HP / 标志位 / 骑宠显示,13 字段 | 攻防 / 敏捷 / 幸运 / 四属 / 装备修正 |
//    | 谁消费 | 客户端渲染 | `resolve_turn` |
//    ⇒ 一个是"给人看的",一个是"拿来算的"。**故意不复用同一个类型。**
//
// ── 三条形状约束(00 §1.2 / 03-domain-model.md §1)──────────────
//   ① 输入是**不可变快照**:L3 是纯函数,不得写回。世界状态的更新由调用方
//      按返回的事件列表应用 —— 这正是四步改造第②步要达到的形态。
//   ② **和类型,不是宽表**:Player / Pet / Enemy 的差异由 `kind` 分派。
//      M2 硬约束;65 条 slot 别名就是"宽表"这个错误在原版留下的疤痕。
//   ③ 底下是**定长数组 + 下标句柄**,不是堆对象图。15 §9.1 三根支柱
//      (运行期零分配 / 活对象数常量 / 单 tick 只触及 5.4% 槽位)依赖这一点。
//
// ⚠️ shared/ 只依赖标准库(01 §4)。

#ifndef SA_SHARED_RULES_COMBATANT_H
#define SA_SHARED_RULES_COMBATANT_H

#include <cstdint>

#include "rules/constants.h"

namespace SA::Rules {

// ── 实体族(M2 的判别键)──────────────────────────────────────
//
// ★ 为什么必须区分而不是一个 bool:回避公式的类型修正是**四条互斥分支**
//   (敌→宠 / 非敌→宠 / 非玩→玩 / 玩→非玩,05 §3.2),
//   且防御修正对"守方是敌人""攻方是敌人"各有一条。两者都需要三态。
enum class CombatantKind : std::uint8_t {
  kPlayer = 0,
  kPet    = 1,
  kEnemy  = 2,
};

// ── 装备与技能带来的、参与结算的修正 ───────────────────────────
//
// ⚠️ 这些在原版散落在装备属性、技能标志与 work 字段里。此处**只收结算真正读的量**,
//    不是装备系统的完整模型。凡未在 05-battle.md §3 出现的,一律不放进来 ——
//    L3 的输入面越小,黄金用例集越可控。
struct CombatModifiers {
  // 攻方「无视防御 N%」⇒ defense × (1 − N/100)。§3.1 第 2 步。
  // ⚠️★ 原版判据是 `> 1` 而不是 `> 0`(`battle_event.c:1218`)
  //    ⇒ **N == 1 时不生效**。照 §3.1 写成 `> 0` 会引入原版没有的 1% 减防。
  int ignore_defense_percent = 0;

  // 守方「必闪」:★ 六道前置否决之一 —— 命中则**直接返回"已回避"**,不走概率。
  bool always_dodge = false;

  // 守方带 NODUCK:同为六道前置否决之一(不可回避)。
  bool no_duck = false;

  // ★ 守方带 ABIO(`CHAR_BATTLEFLG_ABIO`,`battle_event.c:797`)⇒ 同样不可回避。
  // ⚠️ **05 §3.2 的「六道前置否决」清单漏了这一道**(2026-08-31 对源码复核时发现)。
  //    实际是 6 道否决 + 1 道必闪,ABIO 是其中之一。
  bool abio = false;

  // 装备「先攻」⇒ 行动顺序排序键 = dex + sequence。§2.5
  int sequence = 0;

  // 反击附加(原 WORKCOUNTER),直接加进反击率。§3.5
  int counter_bonus = 0;

  // ★ 攻方装备暴击值(原 `ITEM_getInt(At_SoubiIndex, ITEM_CRITICAL)`,
  //   `battle_event.c:1302`)—— 进暴击率:`per += equip_critical × 0.5`。§3.3
  //   ⚠️ 玩家/宠物都读它(暴击函数不分玩家/非玩家,§3.3 已注「两分支调同一函数」);
  //     1.5 无装备系统 ⇒ 调用方按 0 兜底,实现处不写死默认。
  int equip_critical = 0;

  // ★★ 守方免疫暴击(原版硬编码判据是**图号** 101813/101814 雷尔,`battle_event.c:1349`)。
  // ⚠️★ **DR-BT11 裁定:保留行为、改数据驱动 —— 免疫标记进敌人数值表,不写图号。**
  //    ⇒ 这里建模成一个**标志位**而不是比对 `image_number`:图号是实现方式,不是玩法;
  //      照抄图号会把"雷尔"这个特定单位焊死进 L3,而免疫的真正语义是"这个单位免暴击"。
  //    1.5 无敌人数值表(L4)⇒ 一律默认 false;L4 建模时由雷尔模板置 true。
  //    命中则 `RollCritical` 强制 per=0(与原版图号命中同效)。与 `capturable` 同处、同理由。
  bool immune_critical = false;

  // ★★ 守方免疫打飞(§3.8,批次 A.4)。原版同样硬编码雷尔图号(101813/101814,
  //   `battle_event.c:2076`)⇒ IsUltimate=0。⚠️ DR-BT11 同一裁定 ⇒ 数据驱动标志、
  //   不写图号,与 `immune_critical` 同处、同理由。1.5 恒 false;L4 由雷尔模板置 true。
  //   ⚠️ 与 `immune_critical` 是**两件事**:雷尔在原版两处都免,但语义独立
  //     (一个免会心、一个免击飞)⇒ 分两个标志,不合并成一个"雷尔标志"。
  bool immune_knockback = false;

  // 攻方武器类,用于反击相性表 CounterTbl。
  WeaponClass weapon = WeaponClass::kNone;

  // 攻方是否持弓 ⇒ 守方回避率 +20。§3.2
  bool wielding_bow = false;

  // ★ 攻方命中率装备(原 `CHAR_WORKHITRIGHT`,`_EQUIT_HITRIGHT` 在 8.0 **开**)。
  //   仅当攻方是玩家时生效:per −= RAND(0.8×hit, 1.2×hit),下限 0。§3.2
  int hit_right = 0;

  // ── 攻击次数(§3.9)──────────────────────────────────────────
  //
  // ★ 两条路径**不同**,判据是"有没有武器",不是武器类型:
  //     有武器 → RAND(attack_num_min, attack_num_max),≤0 则 1
  //     空手   → 分档抽,可达 10 段(DR-BT1 裁定照抄,各段全额)
  //
  // ⚠️★ `unarmed` 单独立一个字段而不是用 `weapon == kNone` 推:原版判据是
  //    **`itemindex` 无效**(手里没东西),而 `WeaponClass::kNone` 是
  //    「武器相性表的第 0 类」—— 两者不是一回事,合并会让"持无类别武器"
  //    被误判成空手,从而获得 10 段连击。
  bool unarmed = true;
  int  attack_num_min = 1;
  int  attack_num_max = 1;

  // ★ 铁壁防御的加成开关(原 `CHAR_MAGICSUPERWALL > 0`,`_MAGIC_SUPERWALL` 8.0 开)。
  //   ⚠️ 与 `other_status_nums` 是两件事:前者决定**是否**加成,后者是加成**基数**。
  //   原版只有 `MAGICSUPERWALL > 0` 时才读 `OTHERSTATUSNUMS`。
  bool super_wall = false;

  // ── 捕获(§6.2,批次 A.2)────────────────────────────────────
  //
  // ⚠️★ 这四个都只在**守方为敌人**时才有意义,但字段落在通用 `CombatModifiers`
  //    里而不是单开一个敌人结构 —— 与 `abio` / `no_duck` 同处,理由一致:
  //    输入面越集中,黄金用例集越可控。玩家侧一律取默认值。

  // 守方带「可捕获标记」(原 `CHAR_WORK_PETFLG != 0`,`battle_event.c:3830`)。
  // ⚠️ 与 `kind == kEnemy` 是两件事:并非所有敌人都可捕(BOSS / 事件怪不带此标记)。
  bool capturable = false;

  // 守方的「捕获难度」基数(原 `CHAR_WORKMODCAPTUREDEFAULT`,`:3845`)。
  // ★ 源码里这个变量初值 30,但**立即被 `CHAR_getWorkInt` 覆盖**为敌人模板值
  //   ⇒ 30 只是"读不到时的兜底",不是通用默认。敌人数值表(L4)给真值,
  //     1.5 无敌人模板 ⇒ 调用方按 30 兜底并在实现处记明,不写死在这。
  std::int32_t capture_difficulty = 0;

  // 攻方的「捕获率提升」(原 `CHAR_WORKMODCAPTURE`,`:3857`,直接加进 WorkGet)。
  // ⚠️ 原版在 `BATTLE_Capture` 里用完即清零(`:4121`)—— 那是**一次性道具/技能**
  //    的效果。清零是世界写,属调用方(与逃跑计数器 ++ 同一分工),不进 L3。
  std::int32_t capture_bonus = 0;
};

// ── 一个战斗单位 ──────────────────────────────────────────────
struct Combatant {
  // ── 身份 ──
  bool          occupied = false;  // 该槽是否有单位;false 时其余字段无意义
  CombatantKind kind     = CombatantKind::kEnemy;
  std::uint8_t  slot     = 0;      // 0..9 己方 / 10..19 敌方(kSideOffset)

  // ★ 等级 —— 参与两处:空手连击的 `lv < 10` 门槛(§3.9)、
  //   暴击伤害的 `LV攻 / LV守`(§3.3,批次 A.3 已实现)。
  std::int32_t level = 1;

  // ── 生命 ──
  std::int32_t hp     = 0;
  std::int32_t max_hp = 0;
  std::int32_t mp     = 0;
  std::int32_t max_mp = 0;
  bool         dead   = false;

  // ── 参与公式的三围 ──
  //
  // ⚠️ 这里是**本回合已重算完毕的值**,不是基础值。
  //    05 §2.3:回合准备的第 4 件事是「逐角色重算三围」——
  //    `CHAR_complianceParameter` 先把三围**重置为基础值**,再由 `BATTLE_TurnParam` 往上加。
  //    ★ 增益衰减器:`modparam *= 0.8` 每回合衰减 20%,且**只按 1% 折算**
  //      (`最终值 += modparam * 0.01`)⇒ **战斗中的临时增益不会跨回合累积**。
  //    ⇒ 重算发生在**调用 resolve_turn 之前**,L3 拿到的是已经算好的数。
  std::int32_t attack  = 0;
  std::int32_t defense = 0;
  std::int32_t quick   = 0;   // 敏捷,回避与行动顺序都用它
  std::int32_t luck    = 0;   // ★ 上限 25(DR-BT1 的量化前提)

  // ★ 魅力(原 `CHAR_WORKFIXCHARM`)—— 捕获的**乘性主因子**(§6.2:`× charm / 50`
  //   ⇒ 魅力 50 时系数为 1)。⚠️ 只在捕获判定里用,不参与伤害/回避 ⇒ 默认 0。
  std::int32_t charm = 0;

  // 「舍己」时防御直接取此值(**忽略装备**)。原 WORKFIXTOUGH。§3.1 第 2 步
  std::int32_t fix_tough = 0;

  // 铁壁防御的加成基数。原 OTHERSTATUSNUMS。§3.1 第 2 步
  std::int32_t other_status_nums = 0;

  // ── 四属性 ──
  //
  // ⚠️★ 顺序是 **地水火风**(与 constants.h 的 Element 一致),不是相克表的表头顺序。
  //    elements[kNone] 不单独存储 —— 它是 max(0, 100 − Σ其余),由 NoneElement() 求。
  std::int32_t elements[4] = {0, 0, 0, 0};

  // ── 状态 ──
  //
  // ⚠️★ 05 §4.1:状态系统是**单槽状态机 + 43 个候选**,不是多个可并存的计时器。
  //    「目标身上只要有任意一种状态异常,新状态一律施加失败」(职业技能例外)。
  //    ⇒ 这里是**一个** status,不是位图。按位图建模会与原版行为完全不同。
  std::uint8_t status       = 0;  // 取值见 idl Domain::BattleStatus(0 = 正常)
  std::int32_t status_turns = 0;  // 剩余回合
  // ⚠️ 虚弱 / 魔障期间,**身上所有状态的回合数都不减少**(§4.3)。
  //    又因 §4.1 全局互斥,"所有状态"实际只有虚弱/魔障自己。

  // ★★ 世界末日集气(原 `CHAR_DOOMTIME`)—— **不是 `status` 的取值,是独立字段**。
  //
  // 理由(2026-08-31 对源码复核):`BATTLE_CanMoveCheck` 的 8 项里,前 7 项查的都是
  // 单槽状态机能表达的量,唯独第 8 项 `CHAR_DOOMTIME`(`battle.c:8108`)是
  // **自己发动技能的集气计时**,可与其他状态并存 ——
  // DR-BT5 的裁定理由原话:「世界末日集气是**自己发动的技能**的集气过程」。
  // ⇒ 压进 `status` 会丢掉"集气中同时被下毒"这种合法组合。
  std::int32_t charging_turns = 0;

  // ★ 本回合是否处于「集气完成」(原指令 `BATTLE_COM_S_CHARGE_OK`)。
  //   攻方带此标志时,守方**一律不可回避**(§3.2 六道否决第一道)。
  bool charge_ready = false;

  // ★ 醉(原 `CHAR_WORKDRUNK > 0`)—— §3.2 「酒醉真正生效处」:
  //   **攻方**酒醉时守方回避率 += RAND(20,30)。
  // ⚠️ 与 `BATTLE_ST_DRUNK` 状态槽是两个来源,原版分别读 ⇒ 此处独立成字段。
  bool drunk = false;

  // ★ 混乱值(原 `CHAR_WORKCONFUSION`)—— §3.5 防御减伤的**第二个条件**:
  //   触发要求「守方指令 = 防御 **且 混乱值 ≤ 0**」。
  // ⚠️ 与 `BATTLE_ST_CONFUSION` 状态槽同样是两个来源 ⇒ 独立成字段,理由同 `drunk`。
  //    用 `status == CONFUSION` 代替会漏掉「混乱值 > 0 但状态槽已被别的状态占住」的情形
  //    —— 而 §4.1 的全局互斥恰恰让这种情形成为常态。
  std::int32_t confusion = 0;

  // ★ 反应类状态计数(原 `BATTLE_GetDamageReact`)> 0 ⇒ 守方不可回避(§3.2 第三道),
  //   且伤害走 §3.7 的六种反应类型分支。
  int damage_react = 0;

  // ★ 逃跑累计次数(原 `BATTLE_ENTRY.escape`)—— **持久、跨回合累积,失败也累积**(§6.1)。
  //
  // ⚠️★ **这是调用方所有的持久态,不是 L3 的输入语义**(与 hp/attack 那类"本回合已算好
  //    的快照值"不同)。L3 的 `RollEscape` 不读它,只吃调用方传入的 `escape_cnt`;
  //    递增由调用方在喂快照前做(源码 `BATTLE_Escape:4346` 先 ++,`EscapeCheck:4275`
  //    再读 `escape+1` ⇒ 首次判定 escape_cnt=2,见 DR-BT15 与 constants.h)。
  //    放在 `Combatant` 里是因为本仓 field 就地 mutate、不逐回合重建,持久态有落脚点。
  std::int32_t escape_count = 0;

  // ★ 打飞溢出累加器(原 `CHAR_WORKULTIMATE`,§3.8,批次 A.4)—— **持久、跨回合累积**。
  //
  // ⚠️★ 与 `escape_count` 同族:**调用方所有的持久态,不是 L3 的快照输入**。
  //    每次攻击若把守方打穿(负血),溢出量累加到这里;累加后 `>= maxhp*1.2+20`
  //    即触发**累积打飞**。打飞命中(一击或累积)后**清零**(源码 `:2081`)。
  //    L3 的 `RollKnockback` 只做判定并**返回**新的累加值;累加与清零由调用方
  //    在 ApplyEvents 落地(读 Damage.hp_delta 溢出量 + Damage 的打飞标志)。
  //
  // ⚠️★ **打飞的其余下游后果有意不在 A.4 落地,均非遗漏**(2026-09-06 回源码核实,
  //    以下每条都指到行号):
  //    ① 原 `BENT_FLG_ULTIMATE` 令 `BATTLE_Index2No` 返回 −1(`battle.c:930`)⇒
  //       被打飞者**在本回合剩余的派发中**从目标/连击里掉出去。⚠️★ 这是**回合内的
  //       目标排除**,不是"下回合不能动" —— 该 flg 在下一回合开头(`battle.c:8571`
  //       `flg &= ~BENT_FLG_ULTIMATE`)先于建表清除 ⇒ **下一回合照常行动**。
  //       它依赖尚未移植的多目标 / 连击派发(那需要 L3 之外的目标解析)⇒ 排在其后。
  //    ② 被打飞者若阵亡,走 `BATTLE_UltimateExtra` 而非 `NormalDeadExtra`
  //       (`battle.c:6020/6057`)⇒ 额外战利品 / DP —— 属**战果结算**(阶段 2),不在此。
  //    ⇒ A.4 只交付**判定 + 累加器 + 表现标志**,与 A.1–A.3「判定进 L3、世界写留调用方」
  //      同一分工;①② 因依赖未移植子系统而显式推迟,不猜一个"看起来对"的行动剥夺模型。
  std::int32_t ultimate_accumulator = 0;

  // ── 骑宠 ──
  //
  // 有骑宠时攻击力按 kRideMelee* / kRideThrow* 合成(§3.1 第 1 步),
  // 且伤害在人宠之间分摊。
  // ★ DR-BT2 已裁定**修正**分摊式:分子改 myDef(防御高者多扛),并改无损分摊
  //   —— 原式 `damage · petDef / (myDef + petDef) + 1` 让**宠物防御越高、主人吃得越多**,
  //   反向惩罚「培养骑宠」这一核心养成路径。
  bool         has_ride    = false;
  std::int32_t ride_attack = 0;
  std::int32_t ride_defense = 0;
  std::int32_t ride_hp     = 0;
  std::int32_t ride_max_hp = 0;

  CombatModifiers mods{};

  // 无属性余量。★ 不是独立配置项,是推导量:max(0, 100 − Σ四属)。
  constexpr std::int32_t noneElement() const noexcept {
    const std::int32_t sum = elements[0] + elements[1] + elements[2] + elements[3];
    return sum >= kAttrMax ? 0 : (kAttrMax - sum);
  }

  constexpr bool isEnemy() const noexcept { return kind == CombatantKind::kEnemy; }
  constexpr bool isPlayer() const noexcept { return kind == CombatantKind::kPlayer; }
};

// ── 战场快照 ──────────────────────────────────────────────────
//
// ★ 定长 20 槽(2 side × kBattleEntryMax),下标即 slot 号 —— 与就绪位图的位号一致。
//   不是 vector:15 §9.1 的「运行期零分配」要求整个结算过程不触碰堆。
struct BattleField {
  std::uint64_t battle_id = 0;
  std::uint32_t turn      = 0;

  // ★ 战斗类型是否 PvP —— 逃跑在 PvP 中必定成功(§6.1,`battle_event.c:4252`)。
  //   ⚠️ 这是**结算真正读到**的快照字段,不是表现:逃跑公式的第一道分支就依赖它。
  //   1.4/1.5 的 demo 均为 PvE ⇒ 默认 false;PvP 战斗类型的落位属阶段 2。
  bool is_pvp = false;

  // 场地属性。§3.4:power = 0.5 或 0.5 + 该属值·att_pow·0.0001·0.5,
  // 最终 damage × (At_FieldPow / Df_FieldPow)。★ 分母最小 0.5,不会除零。
  std::uint8_t field_attribute = 0;

  Combatant slots[kSlotCount]{};

  constexpr const Combatant& at(int slot) const noexcept { return slots[slot]; }
  constexpr Combatant& at(int slot) noexcept { return slots[slot]; }

  // 同侧判定:0..9 与 10..19。
  static constexpr bool sameSide(int a, int b) noexcept {
    return (a < kSideOffset) == (b < kSideOffset);
  }
};

}  // namespace SA::Rules

#endif  // SA_SHARED_RULES_COMBATANT_H
