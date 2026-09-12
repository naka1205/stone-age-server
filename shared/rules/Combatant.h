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

#ifndef __SA_Combatant_H__
#define __SA_Combatant_H__

#include <cstdint>

#include "rules/Constants.h"

namespace SA::Rules
{

// ── 实体族(M2 的判别键)──────────────────────────────────────
//
// ★ 为什么必须区分而不是一个 bool:回避公式的类型修正是**四条互斥分支**
//   (敌→宠 / 非敌→宠 / 非玩→玩 / 玩→非玩,05 §3.2),
//   且防御修正对"守方是敌人""攻方是敌人"各有一条。两者都需要三态。
enum class CombatantKind : std::uint8_t
{
	kPlayer = 0,
	kPet = 1,
	kEnemy = 2,
};

// ── 装备与技能带来的、参与结算的修正 ───────────────────────────
//
// ⚠️ 这些在原版散落在装备属性、技能标志与 work 字段里。此处**只收结算真正读的量**,
//    不是装备系统的完整模型。凡未在 05-battle.md §3 出现的,一律不放进来 ——
//    L3 的输入面越小,黄金用例集越可控。
struct CombatModifiers
{
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
	int attack_num_min = 1;
	int attack_num_max = 1;

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

	// ★★ 攻方「条件道具已满足」(原 `BATTLE_CaptureItemCheck`,`battle_event.c:3986`)。
	//   ⚠️★ 这是**捕获前置门 ④**(§6.2):原版 `flg = ItemCheck && CaptureCheck`
	//     (`:4101`)—— 某些怪(见 `CaptureItem.h::kNeedItemEnemy`)必须攻方身上带指定
	//     道具才准捕获,没带则**整笔失败、连概率都不摇**(rng 不消耗)。
	//   ★ 它读**攻方背包**,是世界态 ⇒ L3 纯函数看不到 ⇒ 与 `capturable`(读守方世界态)
	//     同款:World 层在 `resolveTurn` **之前**据本回合 capture 指令算好、投影到攻方
	//     这个字段(见 World.cpp)。⇒ L3 只做 `&& capture_item_ok` 的门判定,不读背包。
	//   ★ **默认 true**:无需求怪 / demo foe / PvP / 玩家侧非捕获场景一律满足 ⇒ 门不拦,
	//     现有用例不受影响(同 `isNeedCaptureItem` 返 -1 即视为满足)。
	bool capture_item_ok = true;

	// ★★ 攻方「本回合使用的 HP 恢复药的恢复力基数 `power`」(批次 I.4「战斗内使用道具」)。
	//   ⚠️★ 与 `capture_item_ok` 同款分工:`power` 来自**道具效果表**(世界态,原版
	//     `ITEM_getChar(itemindex, ITEM_ARGUMENT)` → `strstr"体"` + `sscanf`,battle_item.c:244/289),
	//     L3 纯函数看不到 ⇒ World 在 `resolveTurn` **之前**按本回合 USE_ITEM 指令查道具效果表
	//     算好、投影到攻方此字段(见 World.cpp `projectItemUsePower`)。
	//   ⚠️★★ **它是基数不是恢复量** —— 恢复量由 L3 摇:`BATTLE_MultiRecovery` 拿到 power 后
	//     还要 `UpPoint = RAND(power*0.9, power*1.1)`(battle_magic.c:419)⇒ **用道具要消耗
	//     一次 rng**,恢复量在 ±10% 区间内随机。⚠️ 早先本字段名为 `item_heal_hp`、注释断言
	//     「无 rng、恢复量确定」是**错的**:只跟到 `sscanf` 出 power 就下了结论,没跟进
	//     `MultiRecovery`。这条错会让「用了道具之后的所有 rng 消耗整体平移」,而返回值断言
	//     抓不到(同 DR-BT23 退化区间那族)⇒ 已按源码更正,见 `00` §9.0.53 / DR-DT23 ①。
	//   ★ **默认 0** ⇒ 无 USE_ITEM 指令 / 非 HP 恢复药一律不恢复**且不摇 rng**
	//     (对应原版 arg 不含关键字即 `return`、根本进不到 `MultiRecovery`,battle_item.c:284)
	//     ⇒ 现有用例的 rng 序列不受影响。
	std::int32_t item_heal_power = 0;

	// ── 状态异常(§4,批次 L4.1)──────────────────────────────────
	//
	// ★★ **攻方「带毒装备」**(原 `CHAR_SUITPOISON`,`_SUIT_ADDPART4` 在 8.0 **开**,
	//    `battle_event.c:2903`)—— 这是**净核里唯一的普攻附带状态来源**:
	//        if (gBattleStausChange == -1 && SUITPOISON > 0)
	//            gBattleStausChange = BATTLE_ST_POISON, gBattleStausTurn = 3, suitpoison = SUITPOISON;
	//    ⇒ 攻方装备带毒时,普攻**造成伤害后**摇一次状态命中判定,成功则守方中毒。
	//    ★ 这个字段**一身兼两职**:`> 0` 是开关,值本身是命中率的 `PerOffset`(§4.3 第一组
	//      调用参数 —— 原版把 `suitpoison` 直接传给 `BATTLE_StatusAttackCheck` 的 `PerOffset`)。
	//    ⚠️★ 它由套装系统写(装备域未移植)⇒ 与 `equip_critical`(A.3)/ `capture_item_ok`(I.2)
	//      完全同款分工:字段建在快照面、**默认 0 ⇒ 不附带状态且不摇 rng**,装备域接线后自动生效。
	std::int32_t suit_poison = 0;

	// ★★ **守方逐状态抗性**(原 `RegTbl[status]` → `CHAR_WORKMOD*`,`battle_event.c:126`)。
	//
	// ⚠️★★ **数组长度照抄原版缺陷,这是用户 2026-09-11 的显式裁定**:原版 `RegTbl`
	//    实测只有 **31 项**(基础 11 + `_PET_SKILL_SARS` 1 + `_PROFESSION_SKILL` 19),
	//    **没有 `_PROFESSION_ADDSKILL` 那 13 项** ⇒ 状态 31..43(三属抗 / 水附体 / 附身 /
	//    恐惧 / 冰爆术 2-10)的抵抗值**恒为 0、无法被抵抗**。源码有越界防护
	//    (`:5119` `if (status >= arraysizeof(RegTbl) || status < 0) Df_Reg = 0;`)
	//    ⇒ 这是**可观察的玩法事实,不是崩溃**,照抄(`05` §4.5 要求 ①)。
	//    ⇒ 本批只做 1..11(索引 0 = NONE 不用),数组开到 `kStatusResistCount`;
	//      将来扩到全 44 种时,**上限仍是 31 而不是 44** —— 别"顺手补齐"。
	std::int32_t status_resist[12] = {};

	// ★ 守方「通用抗性」(原 `CHAR_WORKRESIST`,`_SUIT_ADDENDUM` 在 8.0 **开**,`:5136`)。
	//   ⚠️ 与 `status_resist[]` 是两件事:前者对**所有**状态生效,后者逐状态。
	std::int32_t general_resist = 0;

	// 输入字段保留原数据含义。F02：原 status 比较 WORK 枚举 51/53/54，
	// 合法状态号小于 44，因此装备三分支和下方 suit 分支均不生效。
	std::int32_t equip_resist_weaken = 0;
	std::int32_t equip_resist_barrier = 0;
	std::int32_t equip_resist_nocast = 0;

	// 原 CHAR_WORKRENOCAST；同样受不可达的 CHAR_WORKWEAKEN 比较控制。
	std::int32_t suit_resist_weaken = 0;
};

// ── 一个战斗单位 ──────────────────────────────────────────────
struct Combatant
{
	// ── 身份 ──
	bool occupied = false; // 该槽是否有单位;false 时其余字段无意义
	CombatantKind kind = CombatantKind::kEnemy;
	std::uint8_t slot = 0; // 0..9 己方 / 10..19 敌方(kSideOffset)

	// ★ 等级 —— 参与两处:空手连击的 `lv < 10` 门槛(§3.9)、
	//   暴击伤害的 `LV攻 / LV守`(§3.3,批次 A.3 已实现)。
	std::int32_t level = 1;

	// ── 生命 ──
	std::int32_t hp = 0;
	std::int32_t max_hp = 0;
	std::int32_t mp = 0;
	std::int32_t max_mp = 0;
	bool dead = false;

	// ── 参与公式的三围 ──
	//
	// ⚠️ 这里是**本回合已重算完毕的值**,不是基础值。
	//    05 §2.3:回合准备的第 4 件事是「逐角色重算三围」——
	//    `CHAR_complianceParameter` 先把三围**重置为基础值**(✅ 基础段已移植 =
	//    `rules/Progression.h` 的 `deriveBaseStats`,DR-DT9),再由 `BATTLE_TurnParam`
	//    往上加(战斗内临时增益,尚未移植)。
	//    ★ 增益衰减器:`modparam *= 0.8` 每回合衰减 20%,且**只按 1% 折算**
	//      (`最终值 += modparam * 0.01`)⇒ **战斗中的临时增益不会跨回合累积**。
	//    ⇒ 重算发生在**调用 resolve_turn 之前**,L3 拿到的是已经算好的数。
	std::int32_t attack = 0;
	std::int32_t defense = 0;
	std::int32_t quick = 0; // 敏捷,回避与行动顺序都用它
	// WORKFIXDEX：反击读取基础敏捷，不受酒醉等战斗临时 QUICK 修改影响。
	std::int32_t fix_dex = 0;
	std::int32_t luck = 0; // ★ 上限 25(DR-BT1 的量化前提)

	// ★ 魅力(原 `CHAR_WORKFIXCHARM`)—— 捕获的**乘性主因子**(§6.2:`× charm / 50`
	//   ⇒ 魅力 50 时系数为 1)。⚠️ 只在捕获判定里用,不参与伤害/回避 ⇒ 默认 0。
	std::int32_t charm = 0;

	// ── 原始四维(vital / str / tough / dex)——批次 L4.1 加入 ──────
	//
	// ⚠️★★ **为什么四维要进 `Combatant`(战斗输入子集)而不是留在 L2 实体里**:
	//    状态系统的**两个**核心公式**直接读**它们,不是读推导后的三围 ——
	//      ① 命中率的体力占比 `fVitalP = VITAL / (VITAL+STR+TOUGH+DEX)`
	//         (`BATTLE_StatusAttackCheck`,`battle_event.c:5088-5093`)
	//      ② 毒的每回合掉血 `(((V+S+D+T)/100)-20)/4`
	//         (`Compute_Down`,`battle.c:5251-5255`)
	//    ⇒ 若改成由 World 预先算好 `vital_p` 投影进来,等于**把一条公式切成两半**、
	//      一半落在 L3 之外 —— 与 `capture_item_ok`(读背包,L3 够不着)那类投影
	//      **不是一回事**:四维是守方自己的属性,不是世界态。
	//    ★ 分工照 M.4b 既有形状:L2 实体(`Model::Pet` / `Model::Enemy`)持有四维,
	//      World 在投影成 `Combatant` 时**直接拷贝**(不是计算)。
	// ⚠️ 默认 0 ⇒ 未接线的调用方得到 `vital_p = 0`(Status.cpp 显式挡了除零)
	//    与毒伤害下限 1,**不会崩**;但那是登记在案的残缺值,不是真值。
	std::int32_t vital = 0;
	std::int32_t str = 0;
	std::int32_t tough = 0;
	std::int32_t dex = 0;

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
	std::uint8_t status = 0;       // 取值见 idl Domain::BattleStatus(0 = 正常)
	std::int32_t status_turns = 0; // 剩余回合
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

	// 原 StatusTbl 把酒醉/混乱直接映射到各自的 WORK 计数（battle_event.c:90）。
	// 当前单槽覆盖内只由 status/status_turns 表达，不维护第二份无人同步的值。
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
	// DR-BT2 已恢复原普通伤害分摊，公式及边界见 splitRideDamage。
	bool has_ride = false;
	std::int32_t ride_attack = 0;
	std::int32_t ride_defense = 0;
	std::int32_t ride_hp = 0;
	std::int32_t ride_max_hp = 0;

	// ★ 骑宠的原始四维(批次 L4.1)—— 毒的每回合掉血**人物与骑宠各算一份**
	//   (`Compute_Down` 的 `flg != -1` 那半段,`battle.c:5264-5281`:同一条公式,
	//    各自的四维与各自的 HP)。
	// ⚠️★ **不补这四个字段就只能让骑宠不吃毒伤害** —— 那是"漏掉半边"式的缺陷
	//    (同 M.1 断线回收漏了宠物那半、骑宠分摊未回写),而且**没有任何
	//    一处会报错**:骑宠照常在场、照常分摊伤害,只是毒不掉它的血。
	//    ⇒ 宁可撑大快照面也要让公式完整,投影由 World 从 `Model::Pet` 直接拷。
	std::int32_t ride_vital = 0;
	std::int32_t ride_str = 0;
	std::int32_t ride_tough = 0;
	std::int32_t ride_dex = 0;

	CombatModifiers mods{};

	// 无属性余量。★ 不是独立配置项,是推导量:max(0, 100 − Σ四属)。
	constexpr std::int32_t noneElement() const noexcept
	{
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
struct BattleField
{
	std::uint64_t battle_id = 0;
	std::uint32_t turn = 0;

	// ★ 战斗类型是否 PvP —— 逃跑在 PvP 中必定成功(§6.1,`battle_event.c:4252`)。
	//   ⚠️ 这是**结算真正读到**的快照字段,不是表现:逃跑公式的第一道分支就依赖它。
	//   1.4/1.5 的 demo 均为 PvE ⇒ 默认 false;PvP 战斗类型的落位属阶段 2。
	bool is_pvp = false;

	// 场地属性。§3.4:power = 0.5 或 0.5 + 该属值·att_pow·0.0001·0.5,
	// 最终 damage × (At_FieldPow / Df_FieldPow)。★ 分母最小 0.5,不会除零。
	std::uint8_t field_attribute = 0;

	Combatant slots[kSlotCount]{};

	constexpr const Combatant &at(int slot) const noexcept { return slots[slot]; }
	constexpr Combatant &at(int slot) noexcept { return slots[slot]; }

	// 同侧判定:0..9 与 10..19。
	static constexpr bool sameSide(int a, int b) noexcept
	{
		return (a < kSideOffset) == (b < kSideOffset);
	}
};

} // namespace SA::Rules

#endif // __SA_Combatant_H__
