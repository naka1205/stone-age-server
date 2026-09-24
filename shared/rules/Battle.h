// shared/rules/battle.h —— L3 战斗结算的契约
//
// ★★ 这是 D2「一份规则两端编译」的落点。本文件与它的实现被**服务端与客户端
//    编译同一份源码**(客户端经 CMake FetchContent + 锁定 tag 引用,DR-TS3)。
//
// 契约(05-battle.md §1.5):
//     resolve_turn(BattleSnapshot, Commands, Random&) -> BattleEvents
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

#ifndef __SA_Battle_H__
#define __SA_Battle_H__

#include "domain/battle_events.sa.h"
#include "domain/battle_status.sa.h"

#include "rules/Combatant.h"
#include "rules/Config.h"
#include "rules/RandomSource.h"

#include <optional>

namespace SA::Rules
{

// 本回合各槽的指令。
//
// ⚠️ 指令的**合法性校验发生在调用之前**(05 §1.5:「已通过合法性校验」)。
//    L3 不做鉴权、不判「这个技能你学过没」—— 那需要读角色的技能表,
//    会把 L3 的输入面撑大到整个角色模型。
struct TurnCommands
{
	SA::Domain::BattleCommand commands[kSlotCount]{};
	bool present[kSlotCount]{}; // 该槽本回合是否有指令(敌方由 AI 填,视为齐备)
};

struct ActionEffects
{
	bool item_used = false;       // 实际执行的资源消耗，不是指令投影。
	bool command_cleared = false; // 状态推进清掉指令，宿主须保留到本回合结束。
	// ── 突击 CHARGE 的**意图回写**(批次 B2b)────────────────────────
	//
	// ★ 蓄力的跨回合状态由宿主(世界侧)所有;L3 是纯函数,拍数的推进按
	//   KnockbackState 同一分工回写:L3 在**行动位**判定这一拍 / 这一击发生了,
	//   宿主据此落地实例状态(World.cpp 的 applyChargeEffects)。
	// ⚠️ 走 ActionEffects 而不是 BattleEvent:集气态是**战斗实例内部态,不上线协议**
	//   (IDL 不动),而这两个标志恰好只需要"本行动发生了什么"这一内部事实。
	bool charge_beat = false;   // 本行动是蓄力拍(NoAction:不摇 rng、不产事件)
	bool charge_strike = false; // 本行动是完成击(已按 charge_ready + 攻%替换走普攻管线)

	// ── 状态攻击**施加当场**清掉目标的指令(批次 B3a)────────────────────
	//
	// ★ 原版在施加成功后(`battle_event.c:2932-2937`)对**守方**执行
	//   `CHAR_setWorkInt( defindex, CHAR_WORKBATTLECOM1, BATTLE_COM_NONE )`,
	//   只列麻痹 / 睡眠 / 石化 / 魔障四种(见 `clearsCommandOnApply`)。
	//   槽号走这里而不是事件:指令是 L3 的输入面(非 IDL 载荷),清指令是
	//   **调用方所有的世界写**(同 actor 自己的 command_cleared 那条先例),
	//   事件流(客户端要演的)已有 StatusChange(applied=true) 承载"中了"。
	// ★ -1 = 本行动没有清任何人的指令。
	int status_cleared_target = -1;
};

// order 由宿主每回合只生成一次。本接口不再摇行动速度。
// 成功后宿主提交世界资源并确认事件，之后才能计算下一位的动作。
bool resolveAction(const BattleField &field, const TurnCommands &commands,
                   const RulesConfig &config, Random &rng, int slot,
                   SA::Domain::BattleEvents &out, ActionEffects &effects) noexcept;

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
SA::Domain::CannotActReason checkCanAct(const Combatant &c) noexcept;

// ── 目标判定族(批次 A-β d2)────────────────────────────────────
//
// 1:1 移植原版的目标可用性判定与几个派生选择器。★ 本族是**纯判定**:
//   读快照、不写世界、不摇 rng(唯一例外见 `targetAdjust` 的注释 —— 它也不摇)。
//
// ⚠️★ 原版这些函数读的是 `CHAR_*` 活实体字段(`WORKBATTLEMODE` / `ISDIE` /
//    `ISATTACKED` / `HP`)。本仓 L3 只有快照,对应关系如下(**逐条已回源码核过**):
//      `CHAR_CHECKINDEX(...) == FALSE`  → `!c.occupied`
//      `CHAR_WORKBATTLEMODE == 0`       → `!c.occupied`(见下)
//      `CHAR_ISDIE` / `HP <= 0`         → `c.dead` / `c.hp <= 0`
//      `CHAR_ISATTACKED == FALSE`       → 无对应(见下,恒视为 TRUE)
//      `CHARMODE_RESCUE`                → 无对应(援护入场未移植 ⇒ 恒不成立)
//
// ⚠️★ **三处无对应项的处置(不猜,逐条记明)**:
//   ① `WORKBATTLEMODE == 0`(「不在战斗中」)—— 本仓 `occupied` 就是"这个槽有单位且
//      在场上"的判据,且战斗实例只含在场者 ⇒ 与 `occupied` 同义,**合并进 `occupied`**。
//      ⚠️ 不新开一个恒为真的 `work_mode` 字段:那会让每个消费点都读一个永远相等的比较。
//   ② `CHAR_ISATTACKED` —— 原版由 `BATTLE_Entry` 置 1(`battle.c:1153`)、
//      `CHAR_playerresurrect` 置 1、战死(`char_event.c:501`)与
//      `BATTLE_S_EarthRoundHide`(`battle_event.c:5602`)置 0。它是「本回合是否参与
//      这一场战斗」的标志,**在本仓的入口面上恒为真**(能进 `field` 的槽都已入场;
//      复活 / 土遁两处写入者都未移植)⇒ 判定里**恒视为 TRUE**,不建模。
//      ⚠️★ 后果:若将来移植复活或土遁,必须回来补这一位,否则守护/选目标会多算一个
//      已离场单位 —— 在实现处就地立此记。
//   ③ `CHARMODE_RESCUE`(援护状态入场)—— 援护入场未移植 ⇒ 恒不成立,不建模。
//
// ★ 位次换算 `indexToNo` / `noToIndex` 在原版是 `BATTLE_Index2No` / `BATTLE_No2Index`
//   (`battle.c:954` / `:894`),靠**实体下标**在两半场里线性找。本仓的槽号**就是**
//   位次(`Combatant::slot`,0..9 己方 / 10..19 敌方)⇒ 两条换算退化成同一件事:
//   校验槽号并回读 `Combatant::slot`。保留这两个名字是为了让移植点对得上源码,
//   ⚠️ 不是为了模拟一层本仓不存在的间接。
//
// 入参 `slots` 是**本回合的存活镜像**(与 `resolveOrdered` 内部的 `dead[]` 同源)。
// ⚠️ 为什么不用 `field.at(i).dead`:同一回合里前面的攻击刚打死的单位,`field` 上
//    还没落地(HP/死亡走局部镜像,回合末才由调用方写回)⇒ 用 `field` 会选到刚死的人。
//    传 `nullptr` 表示"以 `field` 的 dead 为准"(供回合外调用)。
bool targetCheck(const BattleField &field, const bool *slots, int no) noexcept;

// 目标是否**已阵亡且仍在场**(原 `BATTLE_TargetCheckDead`,`battle.c:6759`)。
// ⚠️ 与 `targetCheck` 是**互补而非取反**:本函数要求 `dead == true`,而
//    `targetCheck` 要求 `dead == false`;两者都不认 `!occupied`。
bool targetCheckDead(const BattleField &field, int no) noexcept;

// 一侧的**存活人数**(原 `BATTLE_CountAlive`,`battle.c:4852`)。
// ★ 宠物不计入(`CHAR_TYPEPET` 直接 continue,`:4875`)—— 数的是"人",不是"单位"。
// ⚠️★ 判据是 `!ISDIE`,**不是** `HP > 0`:原版这里只看死亡标志。本仓 `dead` 与
//    `hp <= 0` 在结算路径上同步维护(见 `resolveOrdered`),但**离场**(`occupied=false`)
//    不计入 —— 与源码"Entry 还在就数"一致(捕获离场会把 occupied 置 false,
//    那一路原版走的是 `BATTLE_Exit` 且 Entry 仍在;本仓记此差异)。
int countAlive(const BattleField &field, int side) noexcept;

// 默认攻击者(原 `BATTLE_DefaultAttacker`,`battle.c:4770`):在 `side` 一侧的
// **可选目标**里**等概率**抽一个,返回槽号;一个都没有 ⇒ −1。
// ★ **消耗一次 rng**(`RAND(0, cnt-1)`,`:4813`),且是**先收集后抽** ——
//   收集顺序 = 槽号升序,候选集与 rng 消耗都与源码逐位一致。
// ⚠️ 返回 −1 时**不摇 rng**(源码 `if(cnt == 0) return -1;` 在 `RAND` 之前)。
int defaultAttacker(const BattleField &field, const bool *slots, int side,
                    Random &rng) noexcept;

// 目标是否与 `actor_slot` 同侧(原 `BATTLE_CheckSameSide`,`battle_event.c:7851`)。
// ★ 返回值语义照源码:**1 = 同侧 / 0 = 不同侧或无法判定**(不是 bool 的真/假对)。
// ⚠️★ 多目标分支(`toNo >= 20`,走 `MultiList` 展开后逐个判)在本仓**不适用**:
//    多目标展开(`BATTLE_MultiList`)未移植,见 `multiList` 的登记。传入 `toNo >= 20`
//    恒返回 0(源码那一支对空表也返回 0),调用方一律传单体槽号。
int checkSameSide(const BattleField &field, int actor_slot, int to_no) noexcept;

// 攻击目标的**调整**(原 `BATTLE_TargetAdjust`,`battle.c:6786`):
// 指令里的 `toNo` 不可用 ⇒ 换成 `defaultAttacker(1 - myside)`;返回调整后的槽号。
// ★ 原版顺带把结果**写回** `CHAR_WORKBATTLECOM2`(指令载荷)—— 那是世界写,
//   本仓不在此做:返回值即结果,回写由调用方决定(当前 L3 不需要回写,
//   因为目标在每次行动时重新读指令载荷)。
// ⚠️ 目标可用时**原样返回、不摇 rng**;不可用时才走 `defaultAttacker`(摇一次)。
int targetAdjust(const BattleField &field, const bool *slots, int actor_slot,
                 int to_no, int myside, Random &rng) noexcept;

// ── 混乱目标重定向(批次 A-γ1)────────────────────────────────────
//
// 1:1 移植原版混乱状态目标重定向(`battle.c:5646-5664`)。
// ★ 80% 概率触发(rand(1, 100) <= 80),在全场两阵营中随机挑选存活目标(可为己方或敌方,排除自己)。
// 若 20% 未触发,返回 std::nullopt(维持原指令);若触发且找到目标,返回目标槽号;全场无合法目标返回 -1。
std::optional<int> rollConfusionRedirect(const BattleField &field,
                                         const bool *slots,
                                         int actor_slot,
                                         Random &rng) noexcept;

// ── 忠犬守护(批次 A-β d2)──────────────────────────────────────
//
// 1:1 移植 `BATTLE_GuardianCheck`(`battle_event.c:1431-1511`)。
// 返回**守护者的槽号**;无守护或守护者不可用 ⇒ −1。
//
// ★ 语义:攻击方 `attack_slot` 打向 `def_slot` 时,若 `def_slot` 被某单位守护
//   且该守护者此刻**能接管**,则这一击**改由守护者承受**(`AttackSeq` 把 `defindex`
//   换成守护者,`:1567-1572`)。接管后原守方毫发无伤 —— 包括**不进入回避/暴击判定**
//   (判定读的是替换后的 defindex)。
//
// ★★ **七道否决(逐条照源码,顺序也照)** —— 顺序不可换:它决定哪一条先返回,
//    而调用方只看 −1 / 非 −1 这一个结果。
//     ① 该槽没有守护者(`Entry[i].guardian == -1`,`:1448`)⇒ −1;
//     ② **守护者是它自己**(`Guardian == DefNo`,`:1452`,Terry 的修复)⇒ −1;
//        ⚠️ 这条挡的是"技能对自己用"时客户端表现异常,不是逻辑必需;
//     ③ 守护者不在场(实体下标无效,`:1456`)⇒ −1;
//     ④ 守护者已阵亡(`CHAR_ISDIE`,`:1458`)⇒ −1;
//     ⑤ 守护者**没有守护标志**(`CHAR_BATTLEFLG_GUARDIAN` 位,`:1460`)⇒ −1;
//     ⑥ 守护者**自身处于不可接管的状态**(`:1465-1479`):睡眠 / 混乱 / 麻痹 / 石化 /
//        魔障 / 晕眩 / 天罗地网 / 挑拨 / 世界末日集气 / **守护者就是攻击者本人** ⇒ −1;
//     ⑦ 攻击者**持投掷类武器**(`BATTLE_IsThrowWepon`,`:1490`)⇒ −1
//        (回旋镖/弓/投掷武器越过守护者)。判据 = `mods.wielding_bow ||
//        mods.weapon == kThrow`(与 `rollCounter` 的 ranged 判据同源)。
//
// ⚠️★ 本仓**不建模 `CHAR_BATTLEFLG_GUARDIAN` 标志位** —— 但这不是"它恒与字段同步",
//    而是"**在本仓已移植的写入面上**同步"(核实于 2026-09-17):
//      · `PETSKILL_Guardian`(pet_skill.c:735)置位 —— 与写 `guardian` 字段同一函数,
//        本仓在 `applyGuardianPetSkill` 里一次做完两件事;
//      · `BATTLE_PreCommandSeq`(battle.c:3599)清位 —— 与清 `guardian` 字段同循环,
//        本仓在同一处清;
//      · ⚠️★ **第三个写入者不在这个同步面上**:宠物 AI 的 `PETAI_MODE_RANDOMACT`
//        (`battle.c:8480`)只清标志、**不动 `guardian` 字段**。那一支属宠物 AI
//        (`BATTLE_ai_all`),本仓未移植(叫出的宠无指令即不动,见 WorldTickTest
//        的 fixture 注记)⇒ 在当前可观察面上两者仍等价。
//        ⚠️ 若将来移植宠物 AI,必须回来把这一位补上 —— 否则"AI 随机行动过的守护者"
//        会继续接管,而原版此时已经不接管了。在实现处就地立此记。
//
// ★ 第 ⑥ 条的**状态判据用的是"计数 > 0"而不是 `Combatant::status` 槽**:
//   原版读的是 `WORKSLEEP`/`WORKCONFUSION`/... 这一族**独立 work 计数**,而本仓
//    把状态收敛成单槽 `status` + `status_turns`(L4.1,见 Status.h 卷首的取舍)。
//    ⇒ 判据 = `status` 命中该状态**且** `status_turns > 0`(与 `isAsleep` 同款)。
//    ⚠️ 天罗地网 / 挑拨 / 世界末日集气在单槽模型下分别是 `ENTWINE`(缠绕)/
//       `INSTIGATE` / `BARRIER` 的对应位;`DOOMTIME` 无对应槽 ⇒ **不建模**
//       (职业追加技未移植,恒不成立)。
//
// ⚠️★ 第 ④ 条(守护者已阵亡)读的是**本回合的存活镜像** `slots`,不是快照的 `dead`:
//    同回合里先被打死的守护者**不能再接管**(快照上还没落地)。传 nullptr 才回落快照。
int guardianCheck(const BattleField &field, const bool *slots, int attack_slot,
                  int def_slot) noexcept;

// ── 多目标展开:**有意不移植**,登记于此(批次 A-β d2)────────────
//
// 原 `BATTLE_MultiList`(`battle.c:265-560`)在 `_ATTACK_MAGIC` **开**时的完整分支含
// 三族能力:① 单体(0..19)② **整侧/全体**(`TARGET_SIDE_0/1`、`TARGET_ALL`)
// ③ **前后排**(`TARGET_SIDE_*_B_ROW/F_ROW`、`TARGER_THROUGH`),外加
// `SortLoc` 的位次排序与"目标不可用则随机改打活人"的 `while` 重摇。
//
// ⚠️★ **本批不移植,理由是可观察面而非工作量**:多目标展开的**唯一消费者**是
//    多目标指令与咒术(攻击魔法/职业魔法/宠技的 `全` 系),它们**全部**未移植
//    (S19 魔法/精灵术在覆盖台账里是 `⬜`;`__ATTACK_MAGIC` 咒术管线裁定见 roadmap A-ε)。
//    ⇒ 现在移植只有"代码在、无人调用"一种结果 —— 正是欠债 20/25 那族
//      「地基绿而运行时不接,ctest 一样全过」的形态。
// ★ 本仓当前对单体目标的做法是**内联**在 `resolveOrdered` 里的一行
//   (`target_slot` 范围 + `occupied`/`dead` 检查),它**等价于** `MultiList` 在
//   `toNo ∈ [0,19]` 时的净核(源码 `:239-263`:`ToList[0]=toNo; ToList[1]=-1; cnt=1`),
//   即"恒产单体表"。**本批把这个等价关系显式化**为 `targetCheck` 供各处复用,
//   并把上面那段"哪些分支没移植、为什么"记在此处。
// ⚠️★ **目标不可用时的 `while((toNo = nLifeArea[rand()%10]) == -1);` 有意不复刻**
//    (`battle.c:257`):它**消耗不定次数 rng**,且全死时原版 `return -1` 而调用方
//    `BATTLE_MultiRecovery` 不检查返回值、照样遍历未初始化的 `ToList`(原版 UB)。
//    ⇒ 本仓"目标不可用即什么都不发生、不摇 rng"(DR 已登记为已知行为差,
//    见 11-decision-register.md 的 MultiList 条)。照抄它会让 rng 序列不可回放。

// ── 回合结算 ──────────────────────────────────────────────────
//
// `out` 由调用方提供并被完全覆写。
// ★ 用出参而不是返回值:`Domain::BattleEvents` 是 7 KB 的 POD,
//   返回值会带来一次拷贝,与 15 §9.1「运行期零分配」的取向相悖。
//   调用方通常持有一个每场战斗复用的实例。
//
// 返回 false 表示事件数超过 `max_count = 256` 而**被迫截断**。
// ⚠️★ 调用方**必须**处理 false:05 §10.4 记录了原版在状态串上
//    「strncat 第三参用错、等价于无上界 strcat,余量仅 56 字节且无第二道防线」
//    的教训 —— 新实现宁可分包,不可静默截断。
//
// 当前覆盖：普攻/防御、逃跑、捕获、换宠、HP 恢复药、基础异常状态、
// 宠技·直攻系子集（RENZOKU/GBREAK/GBREAK2/MIGHTY/POWERBALANCE，批次 B1；
// 参数由 World 按 `PET_SKILL.skill_id` 查效果表投影到 `CombatModifiers`），
// 宠技·状态攻击子集（毒/猛毒/石化/混乱/泥醉/催眠攻击，批次 B3a：
// 命中且伤害>0 后走 rollStatusAttack，成功发 StatusChange(applied=true)），
// 以及整次普攻后最多五次交替反击。特殊反应和其余技能链路仍另批接入。
// 反击的依据与边界见 docs/journal/16-counterattack.md。
bool resolveTurn(const BattleField &field,
                 const TurnCommands &commands,
                 const RulesConfig &config,
                 Random &rng,
                 SA::Domain::BattleEvents &out) noexcept;

// ── 调度子步骤(供上层与测试直接调用)──────────────────────────

// 行动顺序排序键(§2.5)。`排序键 = dex + sequence`。
//
// 基数 quick+20；普通/用药采用 SSRC80 的 0.3 扰动，用药加 15%。
// 转成整数的 dex 先夹到至少 1，再加 sequence（用户裁定 U01）。
std::int32_t computeActionDex(const Combatant &c,
                              const SA::Domain::BattleCommand &command,
                              Random &rng) noexcept;

// 计算本回合行动顺序,把槽号按先后写进 `order`,返回参与行动的单位数。
//
// ★★ **DR-BT8 的落点**:原版 `EsCmp` 是布尔比较器(小于与等于都返回 0),
//    不满足严格弱序 ⇒ 同速单位的相对顺序在标准库层面是**未定义行为**
//    (`00` §10.2 六项永久不可判定之一)。
//    ✅ DR-BT8 裁定 = **显式定义为「按入场位次」** ⇒ 本函数用**稳定**排序,
//      同键时保持槽号升序。⚠️ 不可换成 `std::sort` —— 它不保证稳定。
int buildActionOrder(const BattleField &field,
                     const TurnCommands &commands,
                     Random &rng,
                     std::uint8_t (&order)[kSlotCount]) noexcept;

// 本次攻击的段数(§3.9)。★ DR-BT1:空手多段**各段全额**(`gDamageDiv` 保持 1.0)。
//
// ⚠️ 空手分档的两道前置缺一不可:**等级 ≥ 10** 且 **是玩家**,否则恒为 1 段。
int rollAttackCount(const Combatant &attacker,
                    const RulesConfig &config,
                    Random &rng) noexcept;

// 基础反击。attacker 是本次反击者，defender 是被反击者。
// 固定敏捷先走 int 截断；玩家按武器表、幸运和装备加成，以 roll < per 判定；
// 宠物/敌人以 roll <= per 判定。弓/投掷任一侧装备时不消耗随机数。
// 指令、存活、ABIO 及连锁边界由 resolveAction 处理，特殊技能反应另批接入。
int computeCounterBase(const Combatant &attacker, const Combatant &defender) noexcept;
bool rollCounter(const Combatant &attacker, const Combatant &defender,
                 Random &rng, int *out_percent = nullptr) noexcept;

// 防御减伤系数(§3.5)。★ **不是固定系数,是 RAND(1,100) 分六档**,
// 期望 ≈ 0.175 且 **25% 概率完全免伤**。
//
// ⚠️ 调用方须自行确认触发条件(守方指令 = 防御 **且** 混乱值 ≤ 0);
//    本函数只负责抽档,不判条件 —— 判条件要读指令,会把它的入参撑大。
double rollGuardFactor(Random &rng) noexcept;

// 骑宠普通伤害：两防御先夹至 1，player=damage*petDef/(myDef+petDef)+1，
// pet=damage-player+1。正伤害总量为 damage+1，保留源码行为（2026-09-12）。
struct RideSplit
{
	std::int32_t player = 0;
	std::int32_t pet = 0;
};
RideSplit splitRideDamage(std::int32_t damage,
                          std::int32_t my_defense,
                          std::int32_t pet_defense) noexcept;

// 逃跑判定(§6.1)。1:1 移植 `BATTLE_EscapeCheck`(`battle_event.c:4236`)。
// true = 逃跑成功。
//
// ★ 与 `RollGuardFactor` 同一纪律:**本函数只做判定,不碰持久计数器、不判"谁能逃"**。
//   ⇒ `escape_cnt` 由调用方传入,已含 DR-BT15 的双重计数口径(= escape_count + 1,
//     且 escape_count 在喂快照前已 ++)⇒ **首次尝试 escape_cnt = 2**(constants.h)。
//   ⇒ luck 归档(敌人按 rare 0→1/1→3/else→5,玩家 clamp(幸运,1,5),`:4260-4270`)
//     也在调用方:它要读 kind/rare/幸运,放进来会撑大 L3 的输入面。
//   ⇒ 宠物不能逃(`battle.c:9746` 的 `!= CHAR_TYPEPET`)同理在调用方拦,不进 L3。
//
// 入参:
//   is_pvp            —— PvP 直接成功(`:4252`)。
//   attacker_luck_tier—— 已归档的 luck(1..5)。
//   escape_cnt        —— = escape_count + 1(见上)。
//   my_level          —— 攻方等级。
//   enemy_level_sum   —— 敌方存活单位的等级和,★ **已含 ABIO 单位 −100**
//                        (`:4281-4282`,constants.h kEscapeAbioLevelPenalty);调用方算好。
//   enemy_alive_count —— 敌方存活数;0 ⇒ Esc=100(`:4289-4291`)。
//   out_percent       —— 回填判定用的 Esc 百分比(供展示/调试;可传 nullptr)。
bool rollEscape(bool is_pvp,
                int attacker_luck_tier,
                int escape_cnt,
                int my_level,
                int enemy_level_sum,
                int enemy_alive_count,
                Random &rng,
                int *out_percent = nullptr) noexcept;

// 捕获判定(§6.2)。1:1 移植 `BATTLE_CaptureCheck`(`battle_event.c:3806`)。
// true = 捕获成功。
//
// ★ 与 `RollEscape` 同一纪律:**本函数只做概率判定,不碰前置门与世界写**。
//   ⇒ 三道**前置门**都在调用方,不进 L3 输入面:
//     ① 目标是敌人(`:3826`)—— `kind == kEnemy`,类型层面已分;
//     ② 目标带可捕获标记(`:3830` `CHAR_WORK_PETFLG`)—— `mods.capturable`;
//     ③ ★ 等级门 `myLv + 5 < targetLv ⇒ 直接失败`(`:3834`,除非 `PickAllPet`)——
//        它读攻方的"全收"特殊标记,属技能链路;
//     ④ 条件道具检查(`BATTLE_CaptureItemCheck`)—— 读背包,是**道具系统**的活。
//   ⇒ 这四道任一不过,调用方**根本不调本函数**,直接产捕获失败事件。
//   ★ 捕获成功后的世界写(生成宠物 `PET_createPetFromCharaIndex` · 目标离场
//     `BATTLE_Exit` · 删条件道具 DR-BT10 · 攻方 `capture_bonus` 清零)全在调用方。
//
// ⚠️★ **`WORKMODCAPTURE`(capture_bonus)是判定的一部分,进本函数**;而它的**清零**
//    是世界写、留在调用方 —— 判定读它、世界改它,分工同逃跑计数器。
//
// 入参(★ 全程 float 语义,见 constants.h 的移植更正:级差/敏捷差是浮点除法):
//   my_level / target_level        —— 攻守等级。
//   my_dex / target_dex            —— 攻守敏捷(原 WORKFIXDEX)。
//   my_charm                       —— 攻方魅力(乘性主因子,`× charm / 50`)。
//   my_luck                        —— 攻方幸运(原 WORKFIXLUCK)。
//   target_hp / target_max_hp      —— 守方当前/最大 HP(★ 二次式,满血几乎抓不到)。
//   capture_difficulty             —— 守方捕获难度基数(原 WORKMODCAPTUREDEFAULT)。
//   capture_bonus                  —— 攻方捕获率提升(原 WORKMODCAPTURE)。
//   target_asleep                  —— 守方睡眠 ⇒ +15(原 WORKSLEEP > 0)。
//   out_percent                    —— 回填 WorkGet 百分比(可传 nullptr)。
bool rollCapture(int my_level, int target_level,
                 int my_dex, int target_dex,
                 int my_charm, int my_luck,
                 int target_hp, int target_max_hp,
                 int capture_difficulty,
                 int capture_bonus,
                 bool target_asleep,
                 Random &rng,
                 int *out_percent = nullptr) noexcept;

// ── 供上层与测试直接调用的子步骤 ──────────────────────────────
//
// ★ 单独暴露不是为了"方便",是因为 07 §11.3 判据 ① 实测
//   **四条主公式(伤害 / 回避 / 暴击 / 相克)已经天然是纯的** ——
//   它们是 L3 里最该被用例覆盖的部分,也是 ③ 层不可自证的主要补偿点。

// 四属性相克 —— ★ 结算路径。**给 damage、返回 damage**,不是返回系数。
//
// ⚠️★ 为什么不做成「返回系数,调用方乘」:原版 `BATTLE_AttrAdjust` 先把 damage
//    乘进攻方属性向量,再调返回 **int** 的 `BATTLE_AttrCalc`
//    ⇒ 链路上有**三次整数截断**。改成系数形式数学上等价,但截断位置变了
//    ⇒ 与原版逐位不同,可回放性失效。**形状也是公式的一部分。**
std::int32_t applyElementMatrix(const BattleField &field,
                                const Combatant &attacker,
                                const Combatant &defender,
                                std::int32_t damage) noexcept;

// 四属性相克系数 —— ⚠️ **纯展示/测试用,结算路径不得调用。**
//
// 它没有 `ApplyElementMatrix` 的三次截断,两者只在数学上等价、逐位不等价。
// 保留它是因为「量纲自洽」这条性质(Σatk = Σdef = 100 ⇒ 全无属性时系数 1.0)
// 值得被独立断言;拿它去算伤害就复活了「同一语义两份实现」这个 bug 类。
double elementCoefficient(const Combatant &attacker, const Combatant &defender) noexcept;

// 伤害主公式(§3.1 七步)。
//
// ⚠️★ 移植注记 —— 三处**必须原样保留**的原版行为(2026-08-31 已逐条对源码复核,
//     其中 ①② 与文档原文不符,以本注记为准):
//   ① 防御修正是 `defense += (defense * (rand()%10) + 2) / 100`。
//      ⚠️ 文档称「`rand()%10 == 0` 时该项 = 2/100 = 0(**整数除法**)」——**不成立**:
//      `battle_event.c:1164` 是 `float attack, defense;` ⇒ 该式是**浮点除法**,
//      `rand()%10 == 0` 时该项 = **0.02**,不是 0。
//   ② ★★ **三分段没有"窄缝"。** 文档称第二分支上界用浮点、第三分支下界用整数除法
//      ⇒ 边界处两分支都不命中、damage 保持 0。实测**两个论断都不成立**:
//      不是整数除法(同 ①),且实测 0 组窄缝 / 171,429 组**重叠** ——
//      整数除法只会让下界变小 ⇒ 产生重叠而非缝,而 `else if` 让**第二分支**接管。
//      ⇒ **边界值走 `RAND(0, attack/16)`,不是 0。**
//      ⚠️ 照原文实现"窄缝返回 0"会引入原版没有的行为。
//   ③ 第 7 步 `× getDamageCalc()/100` 的默认值是 **70 不是 100**
//      (RulesConfig::damage_calc_percent)。
//
// ★ 另有一条不可省的类型语义:原版 `attack` / `defense` 是 **float**(不是 double)。
//   本实现用 `float` 保留,因为分支边界比较对精度敏感。
//
// ⚠️ **批次 0(普攻链路)的三处有意空缺**,均在实现处就地记明、非遗漏:
//   四属结界 · 附加伤害/减免 · 「舍己」忽略装备 —— 三者都属职业/宠物技能链路。
std::int32_t computeDamage(const BattleField &field,
                           const Combatant &attacker,
                           const Combatant &defender,
                           const RulesConfig &config,
                           Random &rng) noexcept;

// 回避判定。true = 已闪避。
//
// ⚠️ **七道前置**(不是文档说的六道)任一命中直接返回:
//    攻方集气完成 / 守方防御 / 守方有反应类状态 / 守方不能行动 /
//    守方带 NODUCK / ★ 守方带 ABIO(**05 §3.2 漏了这一道**)/
//    ★ 守方自带「必闪」技 ⇒ 直接 true。
//
// ⚠️★ 原版在闪避成功时嵌了 `PROFESSION_SKILL_LVEVEL_UP` 副作用
//    (`battle_event.c:899`)—— 正是四步改造第②步要剥离的典型。
//    本函数**只返回判定结果**,技能升级由调用方按事件处理。
bool rollDodge(const Combatant &attacker,
               const Combatant &defender,
               bool defender_guarding,
               bool defender_casting_spell,
               const RulesConfig &config,
               Random &rng) noexcept;

// 暴击判定(§3.3,批次 A.3)。true = 本次命中为暴击。
//
// ⚠️★ 批次 0.5 因「文档缺判定阈」有意留空(§9.0.8);2026-09-06 回源码核实,
//    `BATTLE_CriticalCheckPlayer`(算 per)+ `BATTLE_AttackSeq`(`:1592` 判定)
//    在源码里判定阈齐全 ⇒ 本批次实现。是文档缺、不是源码缺。
//
// ⚠️ 与 `RollDodge` 同一纪律:**只做概率判定,不写世界态**。原版在暴击命中时嵌了
//    职业技能升级副作用(`:1601` `PROFESSION_SKILL_WEAPON_FOCUS_LVEVEL_UP`)——
//    那属四步改造第②步要剥离的世界写,由调用方按事件处理,不进此函数。
//    ⚠️ 暗月狂狼的 `perCri×1.3` + 攻/敏各 +20% 也是世界写(属宠技,B 批次),不在此。
//
// ★ 类型跨界(敌→宠 / 非玩→玩)时分母从 0.09 暴增到 10.0 且不取平方根 ⇒ 暴击率极低。
bool rollCritical(const Combatant &attacker,
                  const Combatant &defender,
                  Random &rng) noexcept;

// 暴击伤害:`ComputeDamage + 守方原始防御 × (LVatt / LVdef) × 0.5`。[8.0] `:1419`
//
// ⚠️★ **持弓时暴击不吃伤害加成**(`:1594` `gWeponType != ITEM_BOW`)—— 那一路只置
//    暴击标志、伤害仍走普通 `ComputeDamage`。该分支由调用方按 `mods.wielding_bow`
//    决定走哪个函数,本函数只算"加成后"的值。
std::int32_t computeCriticalDamage(const BattleField &field,
                                   const Combatant &attacker,
                                   const Combatant &defender,
                                   const RulesConfig &config,
                                   Random &rng) noexcept;

// 打飞判定(§3.8,批次 A.4)。1:1 移植 `BATTLE_DamageSub` 的打飞段(`:2060-2081`)。
//
// ★ 两条独立判定,同一门槛 `maxhp × 1.2 + 20`(★ float 运算,不是整数近似):
//     一击打飞:本段 `damage >= 门槛`            ⇒ kOneShot
//     累积打飞:**未**一击打飞、且本段有溢出时,`累加器 + 溢出 >= 门槛` ⇒ kAccumulated
//   `overflow` = 本段打穿守方的负血绝对值(调用方按"打前 HP − 伤害 < 0"算出,`:2040`)。
//
// ⚠️★ 与 `RollCritical` 同一纪律:**只判定,不写世界态**。累加器的**累加与清零**
//    是世界写,留调用方(同逃跑计数器)——本函数把"累加后的新值"经 `out_accumulator`
//    回给调用方,由 ApplyEvents 落地;命中打飞时调用方负责清零。
//
// ⚠️ 免疫打飞(`mods.immune_knockback`,DR-BT11 数据驱动,不比对图号)⇒ 恒 kNone
//    且不动累加器(原版 `:2076` 命中即 `IsUltimate=0`,在累加之后覆盖结果)。
//    ★ 但**累加仍要发生**(原版是先累加、再按图号清零 IsUltimate),
//      故 out_accumulator 仍返回累加后的值 —— 逐位照源码顺序。
enum class KnockbackKind : std::uint8_t
{
	kNone = 0,        // 未打飞
	kAccumulated = 1, // 累积打飞(原 IsUltimate=1)
	kOneShot = 2,     // 一击打飞(原 IsUltimate=2)
};
KnockbackKind rollKnockback(std::int32_t damage,
                            std::int32_t overflow,
                            std::int32_t max_hp,
                            std::int32_t accumulator,
                            bool immune_knockback,
                            std::int32_t *out_accumulator) noexcept;

} // namespace SA::Rules

#endif // __SA_Battle_H__
