// src/world/world.cpp —— 最小 tick 与一场战斗的生命周期
//
// 01 §3.1 的 tick 顺序被**原样保留**(连未实现的四步也占位),
// 01 §3.2 的节拍层在这里第一次成为真东西:
//   ★★ 战斗推进速度 **不等于** tick 频率。
//      15 §5.2 实测 8.0 的 _BATTLE_TIME 与 _CHAR_LOOP_TIME 均为关
//      ⇒ 原版战斗速度就是 tick 频率,手感取决于当年的硬件与网络。
//      00 §0 又已认下 ④ 层「表现与手感永远无法验证」
//      ⇒ 节拍是**玩法参数**,必须可配、只能靠人试。

#include "data/Json.h"
#include "world/Api.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "model/EntityIndex.h"
#include "model/EntityPool.h"
#include "model/Player.h"
#include "rules/CaptureItem.h"
#include "rules/Progression.h"
#include "rules/Status.h"

namespace SA::World
{
namespace
{

// ── L2 实体池的容量(批次 M.1)─────────────────────────────────────────
//
// ★★ 取值有源码依据,不是拍的:`15` §2 的配置表实测 `csa8.0/setup.cf` ——
//     fdnum = 100(角色数组**玩家段**维度,兼 fd 表长度与 accept 硬边界,C1/C6)
//     petnum = 2,000(角色数组**宠物段**维度)
//   ⚠️ C7:真实玩家上限 = fdnum − 系统占用(fd 表里混着 acfd/bindedfd/mfd/npcfd)
//     ⇒ 100 是**上界**,不是可达并发数。
//
// ★ 一处很值得记的印证:`15` C4 实测原版角色是**三段式单一数组**
//   `CHAR_chara[fdnum + petnum + othercharnum]`,段边界 `initCharCounter[0..2]`,
//   **按 `CHAR_WHICHTYPE` 选段轮转分配** ——
//   ⇒ 原版本来就是"按族分段的池",我们只是把段换成**独立强类型池**(M2)。
//     和类型不是我们发明的抽象,是把它疤痕化的实现还原成本来的形状。
//
// ⚠️★ **这两个数不能配置化,而这是 EntityPool 定长设计的代价**:
//    `EntityPool<T, Capacity>` 的 Capacity 是**模板参数** ⇒ 必须编译期常量。
//    要让容量可配就得改成运行期容量的池,而那会丢掉 `std::array` 存储 ——
//    正是 `15` §9.1 三根支柱第 ① 条「运行期零分配」的前提。
//    ⇒ 原版 `fdnum` 是配置项,我们这里是常量,**这是有意的取舍,不是遗漏**。
inline constexpr std::size_t kMaxPlayers = 100;
inline constexpr std::size_t kMaxPets = 2000;

// 敌人池容量(批次 M.4b)。
//
// ★ 同样有源码依据:`15` §2 实测 `csa8.0/setup.cf` 的 **othercharnum = 10000**
//   —— 三段式角色数组 `CHAR_chara[fdnum + petnum + othercharnum]` 的第三段。
//
// ⚠️★★ **但这个数不能照抄进第二个族**:原版第三段是**敌人与 NPC 共用**的
//    (`03` §2.1 的 kEnemy + kNpc 两族都落在里面,按 `CHAR_WHICHTYPE` 轮转分配)。
//    ⇒ 我们按族拆成独立强类型池之后,**若将来给 NPC 也开一个 10000,两池之和
//      就超过了原版的上界** —— 而没有任何一处会报错,只会多占内存。
//    ⇒ 建 NPC 族那一批必须**按族切分这 10000 的预算**,不是各取 10000。
//      本批只有敌人一族 ⇒ 暂取全额,这条留在这里等那一批来读。
inline constexpr std::size_t kMaxEnemies = 10000;

// 道具池容量(批次 I.1)。
//
// ★ 源码依据:`csa8.0/setup.cf:318` **itemnum = 10000** —— 原版全局道具池
//   `ITEM_item[itemnum]`(`item.c:486` 的 `ITEM_itemnum`)的维度,同时是运行期硬边界
//   (`ITEM_CHECKINDEX`,`15` §2 C5)。照 M.1 / M.4b 取 setup.cf 实测值,不可配置化
//   (Capacity 是模板参数,见 kMaxPlayers 那条)。
// ⚠️ 它是背包 + 地面道具**共用**的全局池(原版所有道具实例都在这一个数组里)⇒ 与
//   kMaxEnemies「敌人+NPC 共用第三段」同族:将来若地面道具也来抢这 10000,不另开一池,
//   共用本池;本批只有背包一个写入面(且还没接写入)⇒ 暂取全额。
inline constexpr std::size_t kMaxItems = 10000;

using PlayerPool = SA::Model::EntityPool<SA::Model::Player, kMaxPlayers>;
using PetPool = SA::Model::EntityPool<SA::Model::Pet, kMaxPets>;
using EnemyPool = SA::Model::EntityPool<SA::Model::Enemy, kMaxEnemies>;
using ItemPool = SA::Model::EntityPool<SA::Model::Item, kMaxItems>;

// 世界写的落脚点集合(批次 M.1)。
//
// ★ 传一个结构而不是四个参数:捕获一步要写宠物池、写主人的槽、读攻方句柄、
//   还要在池满时落日志 —— 参数列表会随每个新落地的事件继续变长。
// ⚠️ 全是**指针且允许为空**:`ApplyEvents` 的既有用例(纯 HP / 逃跑 / 打飞)不需要
//    任何 L2 落脚点,而给它们造一套空池只是为了填参数 ⇒ 空 = "这一批世界写做不了",
//    分支里显式判、显式记账,不静默跳过。
struct BattleInstance;

struct WorldWriteContext
{
	BattleInstance *battle = nullptr;
	PlayerPool *players = nullptr;
	PetPool *pets = nullptr;
	const std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> *player_of_slot =
	    nullptr;
	// ── 敌人侧的落脚点(批次 M.4b)────────────────────────────────
	//
	// ★ 捕获要从**被捕目标的 L2 `Enemy` 实体**拷四维(源码 `pet.c:343-346`),
	//   而事件里只有槽号 ⇒ 需要「槽 → 敌人实体」这条映射,同 `player_of_slot`。
	// ⚠️★ 非 const:捕获成功后要**释放**那只敌人的实体(源码 `battle.c:1114-1115`:
	//    敌人离场即 `CHAR_endCharOneArray`)⇒ 池要可写,映射也要可清。
	EnemyPool *enemies = nullptr;
	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> *enemy_of_slot = nullptr;

	// ── 道具侧的落脚点(批次「捕获扣道具」)────────────────────────
	//
	// ★ 捕获成功后按 `NeedEnemy[]` 表**全删**攻方背包里的所需道具(DR-BT10,
	//   源码 `BATTLE_CaptureItemDelAll` `battle_event.c:4028`)⇒ 要读写玩家背包槽 +
	//   释放 Item 实体。⚠️ 非 const:删道具要 `items->release` + `Player::clearItemSlot`。
	ItemPool *items = nullptr;
	SA::Platform::Logger *logger = nullptr;

	// ── 本行动的行动者(批次 B3a)────────────────────────────────
	//
	// ★ applyEvents 落 `StatusChange(applied = true)` 时要算**落地的回合数**
	//   (原版 `StatusTbl[st] = gBattleStausTurn + 1`,回合数随施加者而变:
	//   毒攻击 3 / 猛毒 5 / 带毒装备 3)。事件本身不带回合数(IDL 不动)⇒
	//   由**施加者在本行动的投影参数**推出 —— applyEvents 与 resolveAction
	//   同在一个行动循环体内先后执行,此刻投影未被覆盖,读它即得参数。
	//   ⚠️ -1 = 无行动者上下文(理论上不发生;此时按带毒装备的声明值兜底)。
	int actor = -1;
};

// 战斗事件缓冲。★ 每场战斗**复用一个**:domain::BattleEvents 是 7 KB 的 POD,
//   每回合新建一个就是每回合一次 7 KB 的拷贝(shared/rules/battle.h 的原话)。
//
// ── 集气态(批次 B2b)────────────────────────────────────────
// ★ 原版 CHARGE 的跨回合存活靠**工作槽**:COM1 = S_CHARGE 经 `BATTLE_AllCharaCWaitSet`
//   的 `BATTLE_IsCharge` 豁免(battle.c:668)熬过回合末的 COM 清零,COM3 low(剩余拍数)
//   / high(攻%)随行。我们是「L3 纯函数 + 事件回写」⇒ 这份跨回合状态必须落在
//   **战斗实例**(世界侧)上,由 World 每行动投影进 Combatant 快照、按 L3 的
//   ActionEffects 回写推进 —— 与 KnockbackState「L3 判定、世界累加」同一分工。
// ⚠️ 不上线协议(IDL 不动):它是战斗内部态,客户端的表现(蓄力回合不动、完成击
//   掉血)由既有事件流承载。
struct ChargeState
{
	// 剩余蓄力拍。**-1 = 无集气态**;>0 = 已开蓄、还有这么多拍(每拍 NoAction);
	// ==0 = 下一行动是完成击(×1.9 + 守方不可回避)。
	// ⚠️ 与 `Combatant::charging_turns`(世界末日 CHAR_DOOMTIME)是**两件事**:那是
	//   「自己发动技能的集气计时」的另一族(DR-BT5),本结构只装宠技 CHARGE。
	std::int32_t beats = -1;
	std::int32_t percent = 0;  // 完成击的 攻%(COM3 high,option `攻%+P`)
	std::int32_t skill_id = 0; // 完成击合成指令要带的 skill_id
	std::uint32_t target = 0;  // 目标槽(原 COM2 在蓄力期间原样保留)
};

// ── 魔法状态(批次 B3b)────────────────────────────────────────
//
// ★ 原版 `CHAR_MAGICSUPERWALL` 等 **MagicTbl 族**是 StatusTbl 之外的**另一族 work
//   槽**(`battle_event.c:61-66`),与 StatusTbl 的单槽异常状态**互不干扰**:
//   施加端的互斥只在族内扫(`BATTLE_MultiMagicStatusChange`,`battle_magic.c:2019-2026`),
//   消费端(防御加成)也只认族内字段 ⇒ 不能压进 `Combatant::status`(那会把它卷进
//   StatusTbl 的全局互斥,行为分叉)。⇒ 落在战斗实例(世界侧内部态,同 ChargeState
//   的形状与理由:不上线协议,IDL 不动;消费面由逐行动投影进 Combatant 快照)。
//   客户端表现:原版该施加只有动画串(`Bm|` 回显在 8.0 里已注释掉)⇒ 无事件也照抄。
// ⚠️ 单槽建模 = 族内互斥的直接产物(原版施加前扫全族,任一 > 0 即跳过)——
//   与 StatusTbl 单槽同一条结构事实的另一份实例。
struct MagicStatusState
{
	std::int32_t status = 0; // MagicStatus 序号(2 = 铁壁 MAGICSUPERWALL);0 = 无
	std::int32_t turns = 0;  // 剩余回合(原 MagicTbl[i] 的 work 值)
	std::int32_t nums = 0;   // OTHERSTATUSNUMS(防御加成基数;过期不单独清,消费端有开关)
};
// `MagicStatus[]` 的序号常量(battle_event.c:59-66;B3 只接铁壁这一个消费面)。
constexpr std::int32_t kMagicSuperWall = 2;
struct BattleInstance
{
	BattleId id = 0;
	SA::Rules::BattleField field{};
	SA::Rules::TurnCommands commands{};
	SA::Rules::SeededRandom rng{1};
	SA::Domain::BattleEvents events{};
	std::uint64_t seed = 0;
	SA::Platform::Millis next_turn_at_ms = 0;
	BattleStats stats{};
	std::vector<SA::Net::SessionId> members{}; // 订阅事件流的会话
	std::map<SA::Net::SessionId, std::uint8_t> slot_of{};

	// ★ 槽号 → 该槽背后的 L2 `Player` 实体(批次 M.1)。
	//   捕获要把新宠物挂进**攻方主人**的宠物槽,而事件里只有槽号 ⇒ 这条映射是必需的。
	// ⚠️ 空句柄 = 该槽没有 L2 实体 —— **这是正常状态**,不是错误:
	//    1.5 的敌人本来就没有 `Player` 实体,观战席位也不会有。
	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> player_of_slot{};

	// ★ 槽号 → 该槽背后的 L2 `Enemy` 实体(批次 M.4b)。
	//
	// ⚠️ 与 `player_of_slot` 是**两条独立映射**,不是一条带 kind 的:同一个槽在
	//    同一时刻只可能是其中一族,但"哪一族"在编译期就该分开(M2 的和类型)——
	//    合成一条 `{handle, kind}` 会让每个消费点都得先判 kind 再转型,
	//    而判错 kind 就是 M1 那类静默错误。
	// ⚠️ 空句柄 = 该槽没有敌人实体 —— **正常状态**:玩家槽、观战席位、
	//    以及 demo 里手填的那只 foe(见 `makeDemoField`)都没有。
	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> enemy_of_slot{};
	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> pet_of_slot{};
	// 集气态(批次 B2b,见上方 ChargeState)。★ 按**下标句柄**活:单位死亡 / 离场后
	//   槽位守卫(`occupied && !dead`)让残余状态不再触发;新指令到达即清
	//   (onBattleCommand,对应原版「蓄力中宠物菜单关闭」—— 新指令不可能,
	//   我们的槽位一体 ⇒ 以"替换"表意)。
	std::array<ChargeState, SA::Rules::kSlotCount> charge_of_slot{};
	// 魔法状态(批次 B3b,见上方 MagicStatusState)。★ 同为实例内部态:
	//   过期按回合在 tickMagicStatus 递减;施加走 applyMagicStatusPetSkill。
	std::array<MagicStatusState, SA::Rules::kSlotCount> magic_status_of_slot{};
	std::array<std::optional<int>, SA::Rules::kSlotCount> quick_to_restore{};
	std::array<bool, SA::Rules::kSlotCount> profit_settled{};
	std::array<std::int32_t, SA::Rules::kSlotCount> pending_exp{};
	std::array<std::int32_t, SA::Rules::kSlotCount> gained{};
	std::array<std::array<std::int32_t, 3>, SA::Rules::kBattlePlayerMax> getitem{};
	bool aborted = false;
	bool dp_battle = false;
	bool demo = false;
	SA::Platform::Millis started_sec = 0;
	SA::Platform::Millis command_deadline_sec = 0;

	BattleInstance()
	{
		for (auto &items : getitem)
			items.fill(-1);
	}
};

std::uint32_t readyMask(const BattleInstance &battle)
{
	std::uint32_t mask = 0;
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
		if (battle.commands.present[slot] && battle.field.at(slot).occupied && !battle.field.at(slot).dead)
			mask |= 1u << slot;
	return mask;
}

// 已定义的 BC 快照，HP 来自此提交点的权威战场（F12）。
SA::Domain::BattleSnapshot makeBattleSnapshot(const SA::Rules::BattleField &field)
{
	SA::Domain::BattleSnapshot snapshot{};
	snapshot.battle_id = field.battle_id;
	snapshot.field_attribute = static_cast<std::uint32_t>(field.field_attribute);
	constexpr std::uint32_t status_flags[] = {0, 8, 16, 32, 64, 128, 256, 2048, 4096, 8192, 16384, 32768};
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		const auto &unit = field.at(slot);
		if (!unit.occupied)
			continue;
		auto *state = snapshot.combatants.push_back();
		state->slot = static_cast<std::uint32_t>(slot);
		state->level = static_cast<std::uint32_t>(unit.level);
		state->hp = std::max(0, unit.hp);
		state->max_hp = std::max(0, unit.max_hp);
		state->flags = (unit.isPlayer() ? 4u : 0u) | (unit.dead ? 2u : 0u);
		if (unit.status < sizeof(status_flags) / sizeof(status_flags[0]) && unit.status_turns > 0)
			state->flags |= status_flags[unit.status];
		if (unit.has_ride)
		{
			state->ride = SA::Domain::RideState::RIDE_STATE_RIDING;
			state->pet_hp = std::max(0, unit.ride_hp);
			state->pet_max_hp = std::max(0, unit.ride_max_hp);
		}
	}
	return snapshot;
}

// 一侧是否已全灭。★ 这是**战斗结束**的判据,不是 L3 的事 ——
//   L3 只结算一个回合,"还要不要打下一回合"是世界的生命周期问题。
bool sideWipedOut(const SA::Rules::BattleField &field, bool enemy_side)
{
	bool any_alive = false;
	for (int i = 0; i < SA::Rules::kSlotCount; ++i)
	{
		const SA::Rules::Combatant &c = field.at(i);
		if (!c.occupied)
			continue;
		const bool is_enemy = i >= SA::Rules::kSideOffset;
		if (is_enemy != enemy_side)
			continue;
		if (!c.dead && c.hp > 0)
			any_alive = true;
	}
	return !any_alive;
}

// 敌方 AI 的指令填充。★ 这不是新玩法,是 shared/rules/battle.h 里
//   `TurnCommands::present` 的原话所指定的分工:「该槽本回合是否有指令
//   (**敌方由 AI 填,视为齐备**)」—— L3 只结算,不替谁做决定。
//
// ⚠️★ **1.5 只填最简策略:普攻 + 打对面第一个活着的。**
//    真正的 NPC 行为(仇恨、技能选择、宠物指令)绑在批次 A 的指令分发链路上,
//    此处**有意不猜** —— 与批次 0.5 对暴击/反击「构成式齐全但判定入口缺失
//    就停在实现之前」是同一条纪律(00 §9.0.8 ②)。
//
// 玩家侧不在这里补默认指令。正常战斗由 tick 的指令收集期等待或按原版时限退出；
// demo 与无会话测试战场仍允许无人输入推进，不能用它们代表正常玩家回合。
void fillEnemyCommands(const SA::Rules::BattleField &field,
                       SA::Rules::TurnCommands &commands)
{
	// 目标:玩家侧第一个活着的。
	int target = -1;
	for (int i = 0; i < SA::Rules::kSideOffset; ++i)
	{
		const SA::Rules::Combatant &c = field.at(i);
		if (c.occupied && !c.dead && c.hp > 0)
		{
			target = i;
			break;
		}
	}
	if (target < 0)
		return; // 对面全灭 ⇒ 本回合无需行动

	for (int i = SA::Rules::kSideOffset; i < SA::Rules::kSlotCount; ++i)
	{
		const SA::Rules::Combatant &c = field.at(i);
		if (!c.occupied || c.dead || c.hp <= 0)
			continue;
		if (commands.present[i])
			continue; // 已有指令(如被玩家操控的宠物)不覆盖

		SA::Domain::BattleCommand cmd{};
		cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd.command.attack.target = static_cast<std::uint8_t>(target);
		commands.commands[i] = cmd;
		commands.present[i] = true;
	}
}

// 从被捕目标造一只宠物并挂进主人的宠物槽(批次 M.1;M.4b 补上敌人 L2 实体这一半)。
//
// ★★ **照抄** `PET_createPetFromCharaIndex`(展开视图 `char/pet.c:325-399`)的
//    「门 → 拷字段 → 挂槽」三段结构。返回值对应源码的 `pindex != -1`。
//
// 源码三条失败路径,逐条对应:
//   ① `:333` `CHAR_getCharPetElement() < 0`      ⇒ 主人宠物槽满
//   ② `:336` `CHAR_getDefaultChar(&, 31010)` 失败 ⇒ ★ 本批**不适用且不伪造**。
//      ⚠️★ **M.4b 补上了这条的第二半证据**:回源码核实 `CHAR_getDefaultChar`
//        (`char/char_data.c:153-207`)—— 它**只有一条 `return TRUE`**,没有任何
//        FALSE 出口 ⇒ **原版那道门本身也恒不触发**。而且 `31010` 在
//        `CHAR_defaultCharacterGet[]` 里**查不到**(表里全是 `SPR_*` 图号,
//        实测 `SPR_001em == 100000`,六位数)⇒ 走的是兜底分支:数值取**表末**那条
//        (`include/defaultPlayer.h` 的 `player`),而 `CHAR_IMAGETYPE` 取**表首**那条
//        (`defcharaindex` 仍是 0)—— 两者来自不同模板行,不报错。
//      ⇒ 原结论(不写一个永假的判断)不变,但依据从"我们不适用"升级为"它本来就不判"。
//   ③ `:378` `PET_initCharOneArray() < 0`        ⇒ 全局池满 = `allocate()` 返 kNullHandle
//
// ⚠️★★ **顺序要紧,而这正是本批最值得记的一点**:先找槽(可失败)、再 allocate
//    (可失败)、**最后**才写主人的槽 —— 这是源码的形状,也恰好是「预留 → 提交」:
//    两个可失败的动作都排在任何不可回退的写之前
//    ⇒ 失败时世界状态**一个字节都没动**,不需要补偿逻辑。
//    ★ 00 §6 那条「gmsv 进程内也需要工作单元边界」在本批第一次有了具体形状,
//      而它不是我们设计出来的 —— **回源码核实时它已经在那里了**。
//
// ── ★★ 两个数据源的分工(M.4b 的核心,别合并)──────────────────────────
//   原版里 `enemyindex` 是**一个完整的 `Char`**,`PET_createPetFromCharaIndex` 从它
//   一处拷全部。我们把敌人拆成了**两半**,于是拷的时候要各取权威的那一半:
//
//   | 字段 | 取自 | 为什么不能取另一个 |
//   |---|---|---|
//   | hp / mp / max_mp | `tgt`(战场 `Combatant`) | 战斗中的血量写在战场投影上;L2 实体的 `hp` 是**入场时**的满血值,拿它会让"抓一只残血怪"变成"抓一只满血怪" |
//   | vital/str/tough/dex · 成长率 · 名字 · 评级 · 图号 · mod_ai | `src_enemy`(L2 `Enemy`) | ★ `Combatant` 是**战斗输入子集**,里面根本没有这些(它存的是已推导完的三围) |
//   | level · 四属 | 两边都有且一致 | 取 `tgt` —— 战场值是"此刻生效的",若将来有变身 / 属性变更,权威在战场侧 |
//
// ⚠️ `src_enemy == nullptr` = 该槽没有 L2 敌人实体(demo 手填的 foe / PvP 的玩家目标)
//    ⇒ 上表第二行整组**留 0 / 留空**,并由调用方落一条日志。★ 这不是回退到"旧行为",
//    是**显式记账**:抓一个没有 L2 实体的目标本来就抓不到四维,而那种目标只出现在
//    脚手架里(`makeDemoField`)⇒ 真玩法路径上不会走到。
bool createPetFromCapture(const SA::Rules::Combatant &tgt,
                          const SA::Model::Enemy *src_enemy,
                          SA::Model::EntityHandle owner_handle,
                          PlayerPool &players, PetPool &pets)
{
	SA::Model::Player *owner = players.resolve(owner_handle);
	// ★ 主人已下线 / 句柄悬空 ⇒ M10 让它当场变成空指针,而不是脏读一个被复用的槽。
	if (owner == nullptr)
		return false;

	// ── 门 ①:主人的宠物槽 ────────────────────────────────────────
	const int pet_slot = owner->findFreePetSlot();
	if (pet_slot < 0)
		return false;

	// ── 门 ③:全局宠物池 ──────────────────────────────────────────
	const SA::Model::EntityHandle pet_handle = pets.allocate();
	if (!pet_handle.valid())
		return false;
	SA::Model::Pet *pet = pets.resolve(pet_handle);
	if (pet == nullptr)
	{
		// ★ 走不到:刚 allocate 成功的句柄必然 resolve 得到。留这一判是因为若它真的
		//   发生,静默继续就是往空指针上写 —— 与"永假的判断"不同类:那条(门 ②)是
		//   源码有而我们不适用,这条是我们自己的不变量,守它零成本。
		return false;
	}

	// ── 从战场投影拷:生命与"此刻生效"的那些(源码 :340-342, :348-351, :356)──
	pet->hp = tgt.hp;
	pet->mp = tgt.mp;
	pet->max_mp = tgt.max_mp;
	pet->level = tgt.level;

	// ⚠️★★ 四属**按具名下标取,绝不按位置拷** —— 三套顺序两两不同
	//    (原版 `CHAR_*AT` 火水地风 / `Rules::Element` 地水火风 / 相克表头 无火水地风),
	//    详见 Pet.h 的顺序陷阱注释。本项目已在这一类上栽过两次。
	pet->earth = tgt.elements[static_cast<int>(SA::Rules::Element::kEarth)];
	pet->water = tgt.elements[static_cast<int>(SA::Rules::Element::kWater)];
	pet->fire = tgt.elements[static_cast<int>(SA::Rules::Element::kFire)];
	pet->wind = tgt.elements[static_cast<int>(SA::Rules::Element::kWind)];

	// ── 从 L2 敌人实体拷:`Combatant` 里根本没有的那些(M.4b)───────────
	if (src_enemy != nullptr)
	{
		// 原始四维(源码 :343-346)★ **这就是欠债 23 要的那个非 0 来源**。
		pet->vital = src_enemy->vital;
		pet->str = src_enemy->str;
		pet->tough = src_enemy->tough;
		pet->dex = src_enemy->dex;

		// 成长率(源码 :374,`CHAR_ALLOCPOINT` 整个 int 直接拷)。
		// ⚠️ 一处夹取都没有,理由见 Pet.h 的 `growth_*`。
		pet->growth_vital = src_enemy->growth_vital;
		pet->growth_str = src_enemy->growth_str;
		pet->growth_tough = src_enemy->growth_tough;
		pet->growth_dex = src_enemy->growth_dex;

		// 名字(源码 :375-377)。★ M.1 时"留空"是因为 `Combatant` 没有名字;现在有源了。
		pet->name = src_enemy->name;

		// 评级与 AI 模式(源码 :364, :355)。⚠️ 同槽异义:`mod_ai == CHAR_CHARM`。
		pet->pet_rank = src_enemy->pet_rank;
		pet->mod_ai = src_enemy->mod_ai;

		// 图号(源码 :337-338:两个槽同值 = 敌人的**当前**图号)。
		pet->origin_image = src_enemy->base_image;
		pet->base_image = src_enemy->base_image;

		// ── 宠技槽整组拷(源码 :375-377;原始 8.5 树 `pet.c:375-377`,批次 B2a)──
		// ★ `for(i) CharNew.unionTable.indexOfPetskill[i] = CHAR_getPetSkill(enemyindex, i)`
		//   —— 7 槽全拷、不清洗:0(无技能)/ -1(空槽)/ 表外死引用照存,与四维同一来源。
		//   ⚠️ `src_enemy == nullptr`(demo foe / PvP)⇒ 下面整组留 0(结构默认值),
		//     那是"抓不到数据源"的既定记账,不是新偏差(同四维那一组的 else 注记)。
		for (std::size_t i = 0; i < SA::Model::Pet::kPetSkillSlots; ++i)
			pet->pet_skills[i] = src_enemy->pet_skills[i];

		// ⚠️★★ **`luck` 照抄 `variable_ai`,而这看着像 bug 却是原版行为**:
		//    源码 :347 是 `CharNew.data[CHAR_LUCK] = CHAR_getInt(enemyindex, CHAR_LUCK)`,
		//    而 `CHAR_LUCK == CHAR_VARIABLEAI`(同槽,`char_base.h:637`)——
		//    敌人那个槽被 `enemy.c:1076` 写成了 **0**(以"AI 变量"的名义)。
		//    ⇒ 拷过来必然是 0。★ 写成 `= src_enemy->variable_ai` 而不是 `= 0`,
		//      是为了让这条**同槽异义在代码里看得见** —— 写 0 会让下一个人以为
		//      "幸运没实现",写这一行他会顺着 `variable_ai` 找到 `Enemy.h` 卷首那条。
		pet->luck = src_enemy->variable_ai;
	}
	// ⚠️ else:上面这一组**留 0 / 留空**(结构默认值)。不写 else 分支去"填点什么" ——
	//    那正是 00 §10.4 第一类静默错误的做法。调用方落 `no_l2_enemy` 日志。

	// 捕获等级(源码 `battle_event.c:3519`:`PETGETLV` 取的是**新宠**的 `CHAR_LV`)。
	// ⚠️ 同槽异义:`CHAR_PETGETLV` == `CHAR_CHATVOLUME`(音量),见 Pet.h 卷首。
	pet->capture_level = pet->level;

	// 主人反向引用(源码 :390 `WORKPLAYERINDEX` / :394-395 `OWNERCHARANAME`)。
	// ★ 存句柄而不是下标:带 generation ⇒ 主人换人后旧引用作废(M10)。
	pet->owner = owner_handle;
	pet->owner_char_name = owner->name;

	// `VARIABLEAI = 0`(源码 `battle_event.c:3547`)。★ 这一条不依赖任何未移植的东西,
	//   照做。⚠️ 紧跟其后的 AI 修正段(`CHAR_DEFAULTMAXAI − WORKFIXAI`,`:3548-3553`)
	//   需要 `WORKFIXAI`,而那个字段本批未建 ⇒ 不做,见下方 applyEvents 第 8 步的记账。
	// ⚠️★ **它在时序上晚于上面那次 `luck` 拷贝**(`pet.c:347` 拷 → `:3547` 清),
	//    而两者是同一个物理槽 ⇒ 原版的净效果是"幸运被清零"。我们分成两个字段 ⇒
	//    `luck` 保留拷来的值(恒 0)、`variable_ai` 独立置 0。★ 数值上等价,
	//    而**语义分开了** —— 这正是 M1 要求"别名展开成独立字段"的收益。
	pet->variable_ai = 0;

	// ── Y 五项:初值快照(源码 :384-389,批次 M.4b)──────────────────
	//
	// ★ 顺序照源码::384 先推导(`CHAR_complianceParameter`),:385-389 再取 WORK 值
	//   ⇒ Y 是**推导后**的快照,不是四维本身。
	// ⚠️★ 用**新宠自己的四维**推,不是拷敌人的 Y —— 源码 `getWorkInt(newindex, ...)`
	//    读的是 `newindex`(新宠)。⚠️ 敌人侧也有一份 Y(`enemy.c:1154-1158`),
	//    数值上通常相同(四维刚拷过来),但**源不同** ⇒ 照源码走新宠,
	//    否则将来若捕获路径上出现任何属性修正,两者就会分叉而没有一处报错。
	const SA::Rules::DerivedStats snap =
	    SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex);
	pet->y_hp = snap.max_hp;
	pet->y_atk = snap.attack;
	pet->y_def = snap.defense;
	pet->y_quick = snap.quick;
	pet->y_lv = pet->level;

	// ── 提交:挂进主人的槽(源码 :391 `CHAR_setCharPet`)──────────────
	// ★ 到这里已经没有可失败的动作 ⇒ 不会留下"宠物造好了却没挂上"的半成品。
	owner->pets[static_cast<std::size_t>(pet_slot)] = pet_handle;
	return true;
}

// 捕获成功后按 `NeedEnemy[]` 表**全删**攻方背包里的所需道具(批次「捕获扣道具」)。
//
// ★★ 1:1 移植 `BATTLE_CaptureItemDelAll`(展开视图 `battle_event.c:4028-4079`,
//    `_CAPTURE_FREES` 分支):`ti = IsNeedCaptureItem(pet_id)` → 对该行每个非 -1 的
//    item_id,遍历攻方背包命中即删 —— **不 break,同 id 多个都删**(源码 :4074 那句被
//    注释掉的 break 就是这个意思:"抓一只只删一个道具(会员还是决定全删)")。
//   ⚠️ `_NEED_ITEM_ENEMY` 关 ⇒ 8.0 **无条件删**,不看 `getDelNeedItem()` 配置门(那门属
//     `_NEED_ITEM_ENEMY` 段)。⇒ 命中即删,不加开关。
//   ⚠️ **不复刻** `ITEM_DETACHFUNC` 函数指针 + `RunItemDetachEvent` Lua 回调(:4060-4070)
//     —— 8.0 无 Lua(Item.h 文末 ⑤ / 04 §3.3.3 同结论)。
//   ⬜ `CHAR_complianceParameter`(源码 :4073,删道具后重算属性)⇒ 装备加成未移植
//     ⇒ 本批**无可观察后果**,不调(同"永假判断不伪造"取向);属装备域,接装备时一并落。
//
// 前提:`pet_id` 取自被捕目标的 L2 `Enemy` 实体(= 模板号,见 Enemy.h::pet_id)。
//   `src_enemy == nullptr`(demo foe / PvP)⇒ 无 pet_id ⇒ 由调用方跳过本函数。
void captureItemDelAll(SA::Model::Player &owner, std::int32_t pet_id, ItemPool &items)
{
	const int ti = SA::Rules::isNeedCaptureItem(pet_id);
	if (ti < 0)
		return; // 这只怪不需要条件道具,无删除

	const SA::Rules::CaptureNeedItem &row =
	    SA::Rules::kNeedItemEnemy[static_cast<std::size_t>(ti)];
	for (std::size_t k = 0; k < SA::Rules::kMaxCaptureFreeItems; ++k)
	{
		const std::int32_t need_id = row.item_ids[k];
		if (need_id == -1)
			break; // -1 = 该行道具列表结束(源码 :4037)

		// ★ 只扫背包段 `[kStartItemArray, kMaxItemHave)` —— 源码循环从
		//   `CHAR_STARTITEMARRAY` 起(:4038 的 `CheckCharMaxItem` 上界、下界那条链)。
		//   装备位段不是"持有的可扣道具",与 findFreeItemSlot 同一边界。
		for (std::size_t j = SA::Model::kStartItemArray; j < SA::Model::kMaxItemHave; ++j)
		{
			const SA::Model::ItemHandle h = owner.items[j];
			SA::Model::Item *it = items.resolve(h);
			if (it == nullptr)
				continue; // 空槽 / 悬空句柄(源码 `ITEM_CHECKINDEX == FALSE` 跳过,:4040)
			if (it->item_id != need_id)
				continue;

			// 命中 ⇒ 删:清槽 + 释放实体**成对**(M.1 那条纪律的第四处兑现:
			//   漏一半会让池只增不减或槽永久占用,而没有一处报错)。
			owner.clearItemSlot(static_cast<int>(j));
			(void)items.release(h);
			// ⚠️ **不 break**:同一 need_id 的多个道具全删(源码 :4074)。
		}
	}
}

// ── 批次 W.10: ExChangeMan 道具与宠物交付/奖励解析辅助 ─────────
struct ExchangeItemEntry
{
	std::int32_t item_id = 0;
	std::int32_t count = 1;
};

inline std::vector<ExchangeItemEntry> parseExchangeItems(std::string_view str)
{
	std::vector<ExchangeItemEntry> result;
	std::size_t start = 0;
	while (start < str.size())
	{
		const std::size_t comma = str.find(',', start);
		std::string_view part =
		    (comma == std::string_view::npos) ? str.substr(start) : str.substr(start, comma - start);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
			part.remove_prefix(1);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
			part.remove_suffix(1);
		if (!part.empty() && part != "EVDEL")
		{
			const std::size_t star = part.find('*');
			if (star != std::string_view::npos)
			{
				const int id = std::atoi(std::string(part.substr(0, star)).c_str());
				const int cnt = std::atoi(std::string(part.substr(star + 1)).c_str());
				if (id > 0)
					result.push_back({id, std::max(1, cnt)});
			}
			else
			{
				const int id = std::atoi(std::string(part).c_str());
				if (id > 0)
					result.push_back({id, 1});
			}
		}
		if (comma == std::string_view::npos)
			break;
		start = comma + 1;
	}
	return result;
}

struct ExchangePetEntry
{
	std::int32_t pet_id = 0;
	std::int32_t count = 1;
};

inline std::vector<ExchangePetEntry> parseExchangePets(std::string_view str)
{
	std::vector<ExchangePetEntry> result;
	std::size_t start = 0;
	while (start < str.size())
	{
		const std::size_t comma = str.find(',', start);
		std::string_view part =
		    (comma == std::string_view::npos) ? str.substr(start) : str.substr(start, comma - start);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
			part.remove_prefix(1);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
			part.remove_suffix(1);
		if (!part.empty() && part != "EVDEL")
		{
			const std::size_t star = part.find('*');
			if (star != std::string_view::npos)
			{
				const int id = std::atoi(std::string(part.substr(0, star)).c_str());
				const int cnt = std::atoi(std::string(part.substr(star + 1)).c_str());
				if (id > 0)
					result.push_back({id, std::max(1, cnt)});
			}
			else
			{
				const int id = std::atoi(std::string(part).c_str());
				if (id > 0)
					result.push_back({id, 1});
			}
		}
		if (comma == std::string_view::npos)
			break;
		start = comma + 1;
	}
	return result;
}

inline std::vector<ExchangeItemEntry> resolveDelItems(const ExChangeBlock &blk, int branch_idx)
{
	auto dels = parseExchangeItems(blk.del_item);
	const std::string_view cond{blk.condition};
	if (blk.del_item.find("EVDEL") != std::string_view::npos && !cond.empty())
	{
		std::size_t start = 0;
		int cur_branch = 1;
		std::string_view matched_branch{};
		while (start < cond.size())
		{
			const std::size_t comma = cond.find(',', start);
			const std::string_view branch =
			    (comma == std::string_view::npos) ? cond.substr(start)
			                                      : cond.substr(start, comma - start);
			if (cur_branch == branch_idx)
			{
				matched_branch = branch;
				break;
			}
			++cur_branch;
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
		if (!matched_branch.empty())
		{
			std::size_t astart = 0;
			while (astart < matched_branch.size())
			{
				const std::size_t amp = matched_branch.find('&', astart);
				const std::string_view atom =
				    (amp == std::string_view::npos) ? matched_branch.substr(astart)
				                                    : matched_branch.substr(astart, amp - astart);
				if (atom.find("ITEM") != std::string_view::npos &&
				    atom.find('=') != std::string_view::npos)
				{
					const std::size_t eq = atom.find('=');
					const std::string_view val = atom.substr(eq + 1);
					const std::size_t star = val.find('*');
					if (star != std::string_view::npos)
					{
						const int id = std::atoi(std::string(val.substr(0, star)).c_str());
						const int cnt = std::atoi(std::string(val.substr(star + 1)).c_str());
						if (id > 0)
							dels.push_back({id, std::max(1, cnt)});
					}
					else
					{
						const int id = std::atoi(std::string(val).c_str());
						if (id > 0)
							dels.push_back({id, 1});
					}
				}
				if (amp == std::string_view::npos)
					break;
				astart = amp + 1;
			}
		}
	}
	return dels;
}

inline std::vector<ExchangePetEntry> resolveDelPets(const ExChangeBlock &blk, int branch_idx)
{
	auto dels = parseExchangePets(blk.del_pet);
	const std::string_view cond{blk.condition};
	if (blk.del_pet.find("EVDEL") != std::string_view::npos && !cond.empty())
	{
		std::size_t start = 0;
		int cur_branch = 1;
		std::string_view matched_branch{};
		while (start < cond.size())
		{
			const std::size_t comma = cond.find(',', start);
			const std::string_view branch =
			    (comma == std::string_view::npos) ? cond.substr(start)
			                                      : cond.substr(start, comma - start);
			if (cur_branch == branch_idx)
			{
				matched_branch = branch;
				break;
			}
			++cur_branch;
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
		if (!matched_branch.empty())
		{
			std::size_t astart = 0;
			while (astart < matched_branch.size())
			{
				const std::size_t amp = matched_branch.find('&', astart);
				const std::string_view atom =
				    (amp == std::string_view::npos) ? matched_branch.substr(astart)
				                                    : matched_branch.substr(astart, amp - astart);
				if (atom.find("PET") != std::string_view::npos)
				{
					const std::size_t hyphen = atom.find('-');
					if (hyphen != std::string_view::npos)
					{
						const std::string_view right = atom.substr(hyphen + 1);
						const std::size_t star = right.find('*');
						if (star != std::string_view::npos)
						{
							const int id = std::atoi(std::string(right.substr(0, star)).c_str());
							const int cnt = std::atoi(std::string(right.substr(star + 1)).c_str());
							if (id > 0)
								dels.push_back({id, std::max(1, cnt)});
						}
						else
						{
							const int id = std::atoi(std::string(right).c_str());
							if (id > 0)
								dels.push_back({id, 1});
						}
					}
					else if (atom.find('=') != std::string_view::npos)
					{
						const std::size_t eq = atom.find('=');
						const int id = std::atoi(std::string(atom.substr(eq + 1)).c_str());
						if (id > 0)
							dels.push_back({id, 1});
					}
				}
				if (amp == std::string_view::npos)
					break;
				astart = amp + 1;
			}
		}
	}
	return dels;
}

// 往玩家背包放一件道具(三门:无空槽 / 池满 ⇒ 返回 -1 且不写)。道具域第三批 I.3 抽出,
// 供掉落灌包(战果结算段)与 `World::giveItemToPlayer`(注入 seam)共用,避免双份实现(DR-BT5)。
// ★ 与 `spawnEnemyToField` 同性质:三门全过才写 ⇒ 失败不留孤儿。
int giveItemIntoPlayer(SA::Model::Player &owner, const SA::Model::Item &item, ItemPool &items)
{
	const int slot = owner.findFreeItemSlot(); // 门 ①:背包段有空槽(只在背包段找)
	if (slot < 0)
		return -1;
	const SA::Model::ItemHandle h = items.allocate(); // 门 ②:道具池有空位
	if (!h.valid())
		return -1;
	SA::Model::Item *slot_item = items.resolve(h);
	if (slot_item == nullptr)
		return -1; // 走不到(刚 allocate),守零成本
	*slot_item = item;
	owner.items[static_cast<std::size_t>(slot)] = h;
	return slot;
}

// F08: 以稳定宠物句柄回写，槽位复用不会改到上一只宠物。
void syncPetState(BattleInstance &b, PetPool &pets)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
		if (auto *pet = pets.resolve(b.pet_of_slot[static_cast<std::size_t>(slot)]))
		{
			pet->hp = std::max(0, b.field.at(slot).hp);
			pet->mp = std::max(0, b.field.at(slot).mp);
		}
}

// 对账(A-α 批):= 原版 `BATTLE_GetExpGold`(SSRC80 `battle.c:3308`)的
// **经验/掉落一半** —— 结束(finished)或主动离场时,把战斗中暂存的收益
// (WORKGETEXP → pending_exp;拾得道具 → getitem)交付给**存活**玩家。
// 源码 :3327-3329 的 `CHAR_ISDIE` 提前返回(`return 0`)在这里是函数开头的
// `dead` 门 —— 门在 flush 落点,不在击杀瞬间:击杀时记的账,死了就不给。
// ⚠️ 金币**不在本函数**:原版 GetExpGold 里的 `gold += getBattleGold()`
// (8.5 `battle.c:3851-3860`;SSRC80 全树无 getBattleGold 符号)在本实现走
// finished 段的 GoldLedger 产币(见下方「战斗产币」注释)—— 与经验域解耦:
// dp 门只拦金,不拦这里的经验/掉落交付。
void deliverPlayerProfit(BattleInstance &b, int slot, PlayerPool &players, ItemPool &items)
{
	if (slot < 0 || slot >= SA::Rules::kBattlePlayerMax || b.field.at(slot).dead)
		return;
	auto *player = players.resolve(b.player_of_slot[static_cast<std::size_t>(slot)]);
	if (player == nullptr)
		return;
	const auto at = static_cast<std::size_t>(slot);
	const int exp = std::max(0, b.pending_exp[at]);
	player->exp += exp;
	b.gained[at] += exp;
	b.pending_exp[at] = 0;
	for (auto &item_id : b.getitem[at])
	{
		if (item_id < 0)
			continue;
		SA::Model::Item item{};
		item.item_id = item_id;
		item.current_pile = 1;
		(void)giveItemIntoPlayer(*player, item, items);
		item_id = -1;
	}
}

// 等级差衰减经验 —— 逐值照抄 SSRC80 `battle.c:5040-5062`(树基 = SSRC80 原始,
// `StoneAge/gmsv/src/battle/battle.c`;`EXPGET_MAXLEVEL 5` 在 :5038、`EXPGET_DIV 15`
// 在 :5039 —— 本仓不引入这两个宏,以字面量 5 / 15 钉住):
//
//   差 = 攻方等级 − 敌方等级;
//   差 ≤ 5 ⇒ 全额;
//   差 > 5 ⇒ b = 20 − 差,再钳到 15(源码:`b = 5+15−差` 与 `20−差` 同值,
//             `if(b>15) b=15` —— std::min 一句等价);
//   b ≤ 0 ⇒ 1;否则 exp × b / 15,再 max(1)。
//
// 纯函数:零 rng、零世界态读写(settleDeaths 的 rng 消耗只在掉落段,见下)。
int expForKill(int actor_level, int enemy_level, int enemy_exp) noexcept
{
	int delta = actor_level - enemy_level;
	int exp = enemy_exp;
	if (delta > 5) // EXPGET_MAXLEVEL
	{
		delta = std::min(15, 20 - delta); // EXPGET_DIV
		exp = delta <= 0 ? 1 : std::max(1, exp * delta / 15);
	}
	return exp;
}

// F18: battle.c:7051/8900 的本次行动者列表；当前没有合击，列表只有 actor。
// 下一位行动前结算新死亡；单归属也必须消耗 RAND(0,0)。
//
// ── 对账(A-α 批):本函数 = 原版战果分配的对应物 ──────────────────────────
// 原版分发器 `BATTLE_AddProfit`(SSRC80 `battle.c:5171-5179`)按 dpbattle 二分:
// 决斗点怪走 `BATTLE_AddDuelPoint`(:4779-4880),其余走 `BATTLE_AddExpItem`
// (:4946-5110,掉落→经验→骑宠→AI→死亡标记)。本实现没有独立的分发函数:
// 这里的 `eligible`(`!b.dp_battle` 门)与 finished 产币段的 `!b.dp_battle`
// (见 deliverPlayerProfit / 战斗产币两处)合起来就是那个二分。
// ⚠️ 金币**不在** AddProfit / AddExpItem 里 —— 原版金在结束 flush
// `BATTLE_GetExpGold`(:3308),对应物 = deliverPlayerProfit(暂存经验/掉落交付)
// + finished 段的 GoldLedger 产币(见 :2830 起的「战斗产币」注释)。
//
// 已登记的良性偏离(维持,不实现 —— 逐条点名,对账用):
//   ① 骑宠经验 ×0.6(:5078 `nowexp *= 0.6`)不复刻 —— Pet 实体无 exp 字段,
//      宠物经验未接持久化(见下方「经验属于实际行动单位」注);
//   ② 多 winner:原版把经验记给**本次行动者列表**里的每一个人(:5040 起
//      `charaindex[]` 循环,合击时多人)—— 当前没有合击,列表只有 actor,
//      单归属全额(profit_actor 见调用点 :2715 的 Hit 归属);
//   ③ 决斗点分配(AddDuelPoint :4779-4880)不复刻 —— Player 实体无 dp 字段,
//      属 dp 域批次;本实现只在 finished 产币段拦金(exp 域由 eligible 同源拦下);
//   ④ `CHAR_setMaxExp(enemy, 0)`(:5096)不复刻 —— 敌实体整只回池
//      (EntityPool 释放即清),防重复结算由 profit_settled 承担,不需要那个记号。
//   另:死亡侧钩子 `Pet_Check_Die` / CHAR_DEADCOUNT / Ultimate·NormalDead
//   Extra(:5098-5108)各属其域(宠物 / 统计 / 掉落扩展),不在战果域复刻。
void settleDeaths(BattleInstance &b, int actor, const EnemyPool &enemies)
{
	const bool eligible = actor >= 0 && actor < SA::Rules::kSideOffset &&
	                      b.field.at(actor).occupied && !b.field.is_pvp && !b.dp_battle;
	for (int slot = SA::Rules::kSideOffset; slot < SA::Rules::kSlotCount; ++slot)
	{
		const auto at = static_cast<std::size_t>(slot);
		if (!b.field.at(slot).dead || b.profit_settled[at])
			continue;
		b.profit_settled[at] = true;
		const auto *enemy = enemies.resolve(b.enemy_of_slot[at]);
		if (!eligible || enemy == nullptr)
			continue;
		const int owner = b.field.at(actor).kind == SA::Rules::CombatantKind::kPet
		                      ? actor - SA::Rules::kBattlePlayerMax
		                      : actor;
		if (owner < 0 || owner >= SA::Rules::kBattlePlayerMax)
			continue;
		auto &bag = b.getitem[static_cast<std::size_t>(owner)];
		for (int drop = 0; drop < enemy->drop_count; ++drop)
		{
			(void)b.rng.rand(0, 0);
			const int item_id = enemy->dropped_items[static_cast<std::size_t>(drop)];
			auto free = std::find(bag.begin(), bag.end(), -1);
			if (free != bag.end())
				*free = item_id;
			else if (b.rng.rand(0, 1))
				bag[static_cast<std::size_t>(b.rng.rand(0, 2))] = item_id;
		}
		// 经验属于实际行动单位；宠物经验未接持久化，不能转赠主人。
		// (等级差衰减公式 = 上方 expForKill,SSRC80 :5040-5062 逐值。)
		b.pending_exp[static_cast<std::size_t>(actor)] +=
		    expForKill(b.field.at(actor).level, enemy->level, enemy->exp);
	}
}

// 攻方背包里是否**齐备**捕获这只怪所需的全部条件道具(捕获前置门 ④)。
//
// ★★ 1:1 移植 `BATTLE_CaptureItemCheck`(展开视图 `battle_event.c:3986-4013`,
//    `_CAPTURE_FREES` 分支):对 `NeedEnemy[ti]` 行的每个非 -1 道具,背包里都得找到
//    至少一个 ⇒ 任一缺失即返回 false(源码 :4011 `if(j >= max) return FALSE`)。
//   ⚠️ 与删除同用一张表、同一背包扫描边界;这里只**读不写**(判定,纯查询)。
//   ⚠️ 不需要 = 该怪不在表内(`ti < 0`)⇒ 返回 true(源码 :3994 `if(ti<0) return TRUE`)。
bool hasCaptureItems(const SA::Model::Player &owner, std::int32_t pet_id,
                     const ItemPool &items)
{
	const int ti = SA::Rules::isNeedCaptureItem(pet_id);
	if (ti < 0)
		return true; // 无需求怪 ⇒ 门自动满足

	const SA::Rules::CaptureNeedItem &row =
	    SA::Rules::kNeedItemEnemy[static_cast<std::size_t>(ti)];
	for (std::size_t k = 0; k < SA::Rules::kMaxCaptureFreeItems; ++k)
	{
		const std::int32_t need_id = row.item_ids[k];
		if (need_id == -1)
			break; // 该行道具列表结束(源码 :3998)

		bool found = false;
		for (std::size_t j = SA::Model::kStartItemArray;
		     j < SA::Model::kMaxItemHave; ++j)
		{
			const SA::Model::Item *it = items.resolve(owner.items[j]);
			if (it != nullptr && it->item_id == need_id)
			{
				found = true;
				break;
			}
		}
		if (!found)
			return false; // 缺任一所需道具 ⇒ 整笔不准捕获(源码 :4011)
	}
	return true;
}

// 把「攻方条件道具是否齐备」投影到 L3 输入面(捕获前置门 ④)。
//
// ★★ 与 `capturable`(守方世界态投影,见 makeCombatantFromEnemy)同款分工:门 ④ 读的是
//    **攻方背包**这个世界态,L3 纯函数看不到 ⇒ World 在 `resolveTurn` **之前**按本回合
//    每条 CAPTURE 指令算好、写进攻方 `Combatant::mods.capture_item_ok`。
//   ⚠️★ **必须在 resolveTurn 前**:门不过则 L3 不进 rollCapture ⇒ 不摇 rng(与原版一致:
//     没道具连骰子都不掷)。若放到 resolveTurn 后于世界写阶段补判,rng 序列会与原版分叉。
//   ★ 默认 `capture_item_ok = true`(Combatant.h)⇒ 非捕获指令 / 无 L2 敌人 / 无需求怪
//     一律不改,门自动满足,现有用例不受影响。
void projectCaptureItemGate(BattleInstance &b, PlayerPool &players,
                            const EnemyPool &enemies, const ItemPool &items)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		if (!b.commands.present[slot])
			continue;
		const SA::Domain::BattleCommand &cmd = b.commands.commands[slot];
		if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::CAPTURE)
			continue;

		SA::Rules::Combatant &atk = b.field.at(slot);
		if (!atk.occupied)
			continue;
		atk.mods.capture_item_ok = true;

		// 被捕目标的 L2 敌人实体 ⇒ 取其 pet_id 作为需求表匹配键。
		const int tgt_slot = static_cast<int>(cmd.command.capture.target);
		if (tgt_slot < 0 || tgt_slot >= SA::Rules::kSlotCount)
			continue;
		const SA::Model::Enemy *tgt_enemy =
		    enemies.resolve(b.enemy_of_slot[static_cast<std::size_t>(tgt_slot)]);
		if (tgt_enemy == nullptr)
			continue; // demo foe / PvP:无 pet_id ⇒ 门保持默认 true(满足)

		SA::Model::Player *owner =
		    players.resolve(b.player_of_slot[static_cast<std::size_t>(slot)]);
		if (owner == nullptr)
			continue; // 攻方无 L2 玩家实体(不该发生在真捕获路径)⇒ 门保持默认

		atk.mods.capture_item_ok = hasCaptureItems(*owner, tgt_enemy->pet_id, items);
	}
}

// 道具效果表按 item_id 线性查 HP 恢复力基数 power(批次 I.4)。表内无此道具 ⇒ 0(非恢复药)。
//   ★ 线性查同 `findEnemyEncounter` —— 表小(只装恢复药),不值当上哈希。
std::int32_t findItemHealPower(const std::vector<ItemEffect> &effects, std::int32_t item_id)
{
	for (const ItemEffect &e : effects)
		if (e.item_id == item_id)
			return e.heal_power;
	return 0;
}

// 把「本回合 USE_ITEM 指令的 HP 恢复力基数」投影到 L3 输入面(批次 I.4「使用道具」)。
//
// ★★ 与 `projectCaptureItemGate`(捕获门 ④)同款分工:基数要读**道具效果表 + 攻方背包**
//    这两个世界态,L3 纯函数看不到 ⇒ World 在每次 `resolveAction` 之前按 USE_ITEM 指令
//    查好、写进攻方 `Combatant::mods.item_heal_power`。
//   ⚠️★★ **只投影基数,不在这里摇 rng** —— 实际恢复量 `RAND(power*0.9, power*1.1)`
//     (battle_magic.c:419)由 L3 在结算时用**战斗 rng** 摇。若在这里摇,取数就落在
//     resolveAction 之外 ⇒ 战斗 rng 序列错位。
//   每次行动前重新投影，前一步删除物品后不能沿用旧基数。
//   按本回合指令重算(先归 0)⇒ 上次投影不残留;非 USE_ITEM 指令 / 无 L2 玩家 /
//     空槽 / 非恢复药一律保持 0 ⇒ L3 分支跳过且不摇 rng(现有用例的 rng 序列不受影响)。
void projectItemUsePower(BattleInstance &b, PlayerPool &players, const ItemPool &items,
                         const std::vector<ItemEffect> &effects)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		if (!b.commands.present[slot])
			continue;
		const SA::Domain::BattleCommand &cmd = b.commands.commands[slot];
		if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::USE_ITEM)
			continue;

		SA::Rules::Combatant &atk = b.field.at(slot);
		if (!atk.occupied)
			continue;
		atk.mods.item_heal_power = 0; // 每回合重算

		// 攻方 L2 玩家 + 其指定背包槽的道具 ⇒ item_id ⇒ 查效果表得恢复力基数。
		SA::Model::Player *owner =
		    players.resolve(b.player_of_slot[static_cast<std::size_t>(slot)]);
		if (owner == nullptr)
			continue; // 敌人 / demo(无 L2 玩家)用道具本批不支持 ⇒ 保持 0
		const int item_slot = static_cast<int>(cmd.command.use_item.item_slot);
		if (item_slot < 0 || item_slot >= static_cast<int>(SA::Model::kMaxItemHave))
			continue;
		const SA::Model::Item *it =
		    items.resolve(owner->items[static_cast<std::size_t>(item_slot)]);
		if (it == nullptr || it->current_pile <= 0)
			continue; // 空槽 / 悬空句柄 / 无堆叠 ⇒ 不能用
		const std::int32_t power = findItemHealPower(effects, it->item_id);
		if (power > 0)
			atk.mods.item_heal_power = power;
	}
}

// 宠技效果表按 skill_id 线性查一行(批次 B1)。表内无此技能 ⇒ nullptr(= 表外技能)。
//   ★ 线性查同 `findItemHealPower` —— 表小(只装直攻系),不值当上哈希。
const PetSkillEffect *findPetSkillEffect(const std::vector<PetSkillEffect> &effects,
                                         std::uint32_t skill_id)
{
	for (const PetSkillEffect &e : effects)
		if (e.skill_id == static_cast<std::int32_t>(skill_id))
			return &e;
	return nullptr;
}

// 把「本回合 PET_SKILL 指令的技能参数」投影到 L3 输入面(批次 B1 直攻系宠技)。
//
// ★★ 与 `projectItemUsePower`(I.4)/ `projectCaptureItemGate`(A.2)同款分工:参数来自
//    **宠技效果表**这个世界态(数据表;`Model::Pet` 尚无宠技槽 ⇒ 直接按指令里的
//    `skill_id` 查表,不做"宠位 → 技能槽"二次寻址 —— 该槽属后续批),L3 纯函数看不到
//    ⇒ World 在每次 `resolveAction` 之前按 PET_SKILL 指令查好、写进攻方 `CombatModifiers`。
//   ⚠️★ **必须在 resolveAction 前**:技能参数决定 L3 走哪条结算分支(连击段数 / 破除防御 /
//     倍率);放到结算后补判等于 L3 先按"无技能"跑完一遍,rng 与伤害都已错位。
//   ⚠️★ **也只投影参数,不在这里替 L3 算伤害** —— 与原版分工一致:`PETSKILL_*`
//     只把参数塞进 COM3 / 写工作值(`pet_skill.c`),结算读参数(`battle.c` / `battle_event.c`)。
//
// ★ 两个"归一化"在本处(指令语义,同原版在 PETSKILL_* 里做的那两下):
//    ① RENZOKU 段数 `if(N < 1 || N > 10) N = 1;`(`pet_skill.c:605-606`)——
//       ★ **越界归 1,不是夹到边界**:N=11 / 0 / −3 一律 1。⚠️ 别"顺手"夹成 10。
//    ② 表外 skill_id ⇒ 六个字段保持默认(`pet_skill_direct=false`)⇒ L3 整次行动跳过。
//
// ★ 每轮对**所有槽**先归零再按需写入(同 `projectItemUsePower` 的"每回合重算"取向):
//   上次投影不残留 —— 宠物换了指令 / 技能被状态清空后都立刻回到"无技能"。
//   ⚠️★ **登记一处与原版的角落差异**:原版 POWERBALANCE 在**选指令**时就写死了工作值,
//     即便该行动随后被状态清空/打断,减防也留到本回合结束;本实现逐行动重算 ⇒
//     指令被清空后回到原值。差异只在"宠物本回合选了背水、随后被麻痹/混乱清掉指令"
//     这一角落,且方向是"少扣一次防",不产生新玩法。不为此保留跨行动脏态。
//   ⚠️ 不改 `mods` 之外的东西:世界态(MP / 背包 / 宠位)一个字节都不动。
void projectPetSkill(BattleInstance &b, const std::vector<PetSkillEffect> &effects)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		SA::Rules::Combatant &atk = b.field.at(slot);
		if (!atk.occupied)
			continue;

		// 归零 = 回到"无技能"(Combatant.h 里每个字段的默认值)。
		atk.mods.pet_skill_direct = false;
		atk.mods.pet_skill_hits = 0;
		atk.mods.pet_skill_damage_percent = 100;
		atk.mods.pet_skill_duck_bonus = 0;
		atk.mods.pet_skill_guard_break = 0;
		atk.mods.pet_skill_attack_percent = 0;
		atk.mods.pet_skill_defense_percent = 0;
		atk.mods.pet_skill_charge_turns = 0;
		atk.mods.pet_skill_charge_percent = 0;
		atk.mods.pet_skill_apply_status = 0; // 批次 B3a:0 = 非状态技
		atk.mods.pet_skill_status_turns = 0;

		if (!b.commands.present[slot])
			continue;
		const SA::Domain::BattleCommand &cmd = b.commands.commands[slot];
		if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::PET_SKILL)
			continue;

		const PetSkillEffect *e =
		    findPetSkillEffect(effects, cmd.command.pet_skill.skill_id);
		if (e == nullptr)
			continue; // 表外技能 / 空表 ⇒ 保持"无技能"⇒ L3 跳过(不退化成普攻)

		// ⚠️ 蓄力行与魔法状态行(铁壁)**都不是**直攻系:第一拍由 L3 的集气分支接管
		//   (先于"表外 ⇒ 跳过"判定);铁壁在原版是独立 case(battle.c:8410,
		//   不落 :7512 的普攻执行组)⇒ L3 整次行动跳过(不摇 rng、不产事件),
		//   施加由世界侧在行动位做(applyMagicStatusPetSkill)。
		if (e->charge_turns == 0 && e->magic_status == 0)
			atk.mods.pet_skill_direct = true;
		// ① RENZOKU 段数归一:`if(N < 1 || N > 10) N = 1;`(pet_skill.c:605-606)——
		//   ★ 越界**归 1,不是夹到边界**,也不是归"无技能":原版此时仍是 RENZOKU
		//     (COM1 = S_RENZOKU、gDamageDiv = 1)⇒ 依然跳过段数那笔 rng。
		//   ⚠️ 表里 `renzoku_hits == 0` 的行是**非连击技能**(该列不适用)⇒ 不写、
		//     保持 0(= 不覆盖段数),否则会把 GBREAK/MIGHTY 也误当成 1 段覆盖。
		if (e->renzoku_hits != 0)
			atk.mods.pet_skill_hits =
			    (e->renzoku_hits < 1 || e->renzoku_hits > 10) ? 1 : e->renzoku_hits;
		atk.mods.pet_skill_damage_percent = e->damage_mult_percent;
		atk.mods.pet_skill_duck_bonus = e->duck_bonus;
		atk.mods.pet_skill_guard_break = e->guard_break;
		atk.mods.pet_skill_attack_percent = e->attack_percent;
		atk.mods.pet_skill_defense_percent = e->defense_percent;
		// ③ 状态攻击参数(批次 B3a):原版在 `PETSKILL_StatusChange` 里塞 COM3
		//   low/high(pet_skill.c:819-820),无归一化(回合缺省 3 已由表解析保证)
		//   ⇒ 原样投影,回合数的 +1(酒醉再折半)发生在施加落地那一步。
		if (e->apply_status > 0)
		{
			atk.mods.pet_skill_apply_status = e->apply_status;
			atk.mods.pet_skill_status_turns = e->status_turns;
		}
		// ② CHARGE 蓄力拍数归一(批次 B2b):`N<1 || N>10 ⇒ 1`(pet_skill.c:630-634,
		//   与 RENZOKU 同款)。⚠️ **归 1 仍是蓄力指令**(原版 COM1=S_CHARGE 照设),
		//   与"表外"不同;`charge_turns == 0` 的行才是非蓄力技能(不写、保持 0)。
		if (e->charge_turns != 0)
		{
			atk.mods.pet_skill_charge_turns =
			    (e->charge_turns < 1 || e->charge_turns > 10) ? 1 : e->charge_turns;
			atk.mods.pet_skill_charge_percent = e->charge_attack_percent;
		}
	}
}

// ── 突击 CHARGE 的三段世界侧接线(批次 B2b)──────────────────────────────
//
// ★★ 状态机落点:**战斗实例的 `charge_of_slot[slot]`**(`BattleInstance`,世界侧)。
//   三段分工与 KnockbackState 同形 —— L3 判定、世界落地:
//     ① `projectChargeState`  读实例态 ⇒ 写当行动者的 Combatant 快照(拍 / 击);
//     ② `injectChargeCommands` 实例态非空 ⇒ 给该槽**注入合成指令**(集气单位
//        无需新指令即自动行动,原版靠 COM1=S_CHARGE 存活 + AI/C_OK 豁免);
//     ③ `applyChargeEffects`  按 L3 的 ActionEffects 推进/清空实例态。
//
// ⚠️★ 原版凭据(逐条复核 2026-09-15,原始 8.5 树):
//   · 跨回合存活:`BATTLE_AllCharaCWaitSet` 对 `BATTLE_IsCharge` 单位**不**清 COM1
//     (battle.c:645-661/668)⇒ 集气者是唯一在回合末保住指令的单位;
//   · 集气中**不可重发指令**:宠物菜单置灰(battle_command.c:945
//     `BATTLE_IsCharge ⇒ BP_FLG_PET_MENU_OFF`)⇒ 我们的"新指令替换集气"是
//     槽位一体模型下的替代表意(onBattleCommand 处记明);
//   · 敌人侧集气自动就绪:battle_ai.c:45 `IsCharge ⇒ C_OK`(本批敌人不用宠技);
//   · 状态清指令者丢集气:`BATTLE_StatusSeq` 对 `CanMoveCheck == FALSE` 无条件
//     `COM1 = NONE`(battle.c:5440,无 IsCharge 豁免)⇒ 与 L3「状态门在集气分支
//     之前」的次序一致。

// ① 把实例集气态投影进该行动者的快照。★ 必须在 `projectPetSkill` **之后**调用:
//   完成击要覆盖表投影(direct=false / attack_percent=0),改用完成击的那套参数。
void projectChargeState(BattleInstance &b, int slot)
{
	SA::Rules::Combatant &c = b.field.at(slot);
	// 先归零:上一行动的投影不残留(charge_ready 原版在攻击段末即清,`:7729`)。
	c.pet_charge_beats = -1;
	c.charge_ready = false;

	const ChargeState &st = b.charge_of_slot[static_cast<std::size_t>(slot)];
	if (st.beats < 0)
		return; // 无集气态

	c.pet_charge_beats = st.beats;
	if (st.beats != 0)
		return; // 蓄力拍:L3 只读拍数,参数面保持"无技能"

	// ── 完成击:原 `BATTLE_Charge` 的 `iWork <= 0` 支(battle_event.c:5036-5045)──
	//   `pow = WORKFIXSTR; pow += pow * N * 0.01;`(N = COM3 high = 攻%)写入
	//   WORKATTACKPOWER,COM1 置 S_CHARGE_OK ⇒ 本回合落普攻执行组
	//   (battle.c:7510 fall-through)。
	// ★ 攻%替换**复用 POWERBALANCE 的落点**(`mods.pet_skill_attack_percent`
	//   → Battle.cpp 的 `effectiveAttack`):两者都是"从 FIXSTR 重算工作值"的替换式,
	//   而 `(int)(str + str·p) == str + (int)(str·p)`(p ≥ 0,截断向零)
	//   ⇒ 与源码的 double 一步式数值等价,不为它另开一条伤害路径。
	c.charge_ready = true;                        // 守方不可回避(rollDodge 门①)
	c.mods.pet_skill_direct = true;               // 落普攻管线
	c.mods.pet_skill_attack_percent = st.percent; // WORKATTACKPOWER 替换
}

// ② 集气单位无需新指令即自动行动 ⇒ 在等待指令之前给它注入合成指令。
//   ⚠️ 必须在"回合是否等该槽指令"的判断**之前**跑,否则集气槽会被当成未就绪,
//     战斗卡到指令超时(原版该单位由 IsCharge 豁免直接 C_OK)。
//   拍回合注入 WAIT(L3 由 pet_charge_beats 接管,指令本身不执行);
//   完成击回合注入带原目标的 PET_SKILL(原 COM2 = toindex 在蓄力期间保留)。
//   ⚠️ 只在**该槽本回合还没有指令**时注入 —— 有真实指令意味着集气已被
//     onBattleCommand 取消(新指令替换),不该再替它行动。
void injectChargeCommands(BattleInstance &b)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		const ChargeState &st = b.charge_of_slot[static_cast<std::size_t>(slot)];
		if (st.beats < 0)
			continue;
		const SA::Rules::Combatant &c = b.field.at(slot);
		if (!c.occupied || c.dead)
			continue; // 离场 / 阵亡:残余状态不再触发(无效槽,不必清)
		if (b.commands.present[slot])
			continue;

		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = b.id;
		cmd.turn = b.field.turn;
		if (st.beats > 0)
		{
			cmd.command_kind = SA::Domain::BattleCommand::CommandKind::WAIT;
		}
		else
		{
			cmd.command_kind = SA::Domain::BattleCommand::CommandKind::PET_SKILL;
			cmd.command.pet_skill.skill_id = static_cast<std::uint32_t>(st.skill_id);
			cmd.command.pet_skill.target = st.target;
		}
		b.commands.commands[slot] = cmd;
		b.commands.present[slot] = true;
	}
}

// ③ 按 L3 的行动结果推进 / 清空实例集气态(原 COM3 low 的减一与 `:7729` 的
//   COM1 = NONE)。⚠️ 只有 L3 真在**行动位**处理了这一拍 / 这一击才会置这两个标志:
//   被状态清指令 / 不可行动的单位走不到那里 ⇒ 收不到 charge_beat/charge_strike,
//   集气在那条路上由 `command_cleared` 一并清掉(见 tick 循环处的注记,
//   对应原版 `StatusSeq` 对不能行动者无条件 `COM1 = NONE`,battle.c:5440)。
void applyChargeEffects(BattleInstance &b, int slot, const SA::Rules::ActionEffects &effects)
{
	if (effects.charge_beat)
	{
		ChargeState &st = b.charge_of_slot[static_cast<std::size_t>(slot)];
		if (st.beats < 0)
		{
			// 首拍(新发指令):N 拍的拍 #1 已在本行动位发生 ⇒ 剩 N-1
			//   (原 `BATTLE_Charge`:COM3 low = N > 0 ⇒ 减一 + NoAction)。
			//   参数从**本行动的投影面**取(此刻尚未被下一次投影覆盖)。
			const SA::Rules::Combatant &c = b.field.at(slot);
			st.beats = c.mods.pet_skill_charge_turns - 1;
			st.percent = c.mods.pet_skill_charge_percent;
			st.skill_id =
			    static_cast<std::int32_t>(b.commands.commands[slot].command.pet_skill.skill_id);
			st.target = b.commands.commands[slot].command.pet_skill.target;
		}
		else
		{
			--st.beats; // 续拍:每拍减一(原 COM3 low--)
		}
	}
	if (effects.charge_strike)
	{
		// 完成击已消费:清态(原 `:7729` `COM1 = NONE`)。
		b.charge_of_slot[static_cast<std::size_t>(slot)] = ChargeState{};
	}
}

// ── 魔法状态(铁壁)的世界侧三段接线(批次 B3b)────────────────────────
//
// ★★ 状态机落点:**战斗实例的 `magic_status_of_slot[slot]`**,与集气态同形 ——
//   原版凭据(2026-09-15 回源码核实,原始 8.5 树 `StoneAge/gmsv/src/battle/`):
//   · 施加:`PETSKILL_MagicStatusChange`(pet_skill.c:1726)置 COM1=S_SUPERWALL ⇒
//     `battle.c:8410-8416`(独立 case,**不落** :7512 普攻执行组)⇒
//     `PETSKILL_MagicStatusChange_Battle`(battle_event.c:7203)解析 option
//     `铁壁|3|30|全` ⇒ `BATTLE_MultiMagicStatusChange`(battle_magic.c:2001):
//     目标已有**任一** MagicTbl 状态则整笔跳过(:2019-2026),否则
//     `MAGICSUPERWALL = turn; OTHERSTATUSNUMS = nums`。**施加端无 rng、无攻击、
//     无事件**(动画串 `Bm|` 在 8.0 已注释,:2027-2031)。
//   · 消费:防御公式读 `MAGICSUPERWALL > 0` + `OTHERSTATUSNUMS`
//     (battle_event.c:1195-1200)= L3 既有消费面(mods.super_wall / other_status_nums)。
//   · 过期:`BATTLE_MagicStatusSeq`(battle.c:9059-9078)在该单位**行动位**每回合
//     `--cnt`、归零即清 —— 调用点 `battle.c:7077`,先于指令派发(:7240);阵亡单位
//     在循环头 :7051 被跳过 ⇒ **计时暂停**(复活不复活都无所谓,边角照抄);
//     离场清理由 `BATTLE_BadStatusAllClr`(battle.c:86-99)承担。
//   ⚠️ **与原版的登记差异(有意,缩小范围)**:原版递减发生在**每个单位自己的
//     行动位**(行动序靠后的单位在被攻击前还没减);本实现每回合在行动循环前
//     统一递减一次 —— 差别只在"到期那一回合里、先于该单位行动的攻击者"看到的
//     值(原版还是旧值,本实现已清)。方向是"早半回合过期",且只影响到期当回合。
//     另:原版只递减"到达 C_OK 的单位",本实现对**有状态的在场单位**一律递减
//     (我们的宠物槽不产指令,照原版口径它们永远 tick 不到 ⇒ 铁壁永不过期,
//     那是更大的偏差;两害取轻,记明在案)。

// ① 投影:把实例魔法状态写进所有在场槽的快照消费面(每次 resolveAction 前)。
//   ★ 全槽投影(不只行动者):被攻击的是**任意**槽 —— 与 capture_item_ok 那类
//   "读世界态才能算出的门"同一分工,世界态在世界侧算好、L3 只读快照。
//   ⚠️ 无条件覆写 `super_wall` / `other_status_nums`:全仓无第二个写者
//   (实测:仅有用例手填,RulesBattleTest 直调 L3 不过这里)⇒ 归零语义干净。
void projectMagicStatus(BattleInstance &b)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		SA::Rules::Combatant &c = b.field.at(slot);
		const MagicStatusState &st = b.magic_status_of_slot[static_cast<std::size_t>(slot)];
		// 目前 MagicTbl 族只接了铁壁这一个消费面;其余序号(魔抗/火抗…)投影为无,
		// 与"效果表没有该消费面"一致(不猜)。
		const bool active = st.status != 0 && st.turns > 0 && st.status == kMagicSuperWall;
		c.mods.super_wall = active;
		c.other_status_nums = active ? st.nums : 0;
	}
}

// ② 过期推进:每回合行动阶段前跑一次(见上"登记差异")。
//   阵亡 ⇒ 跳过(原版 :7051 在 Seq 之前 continue ⇒ 计时暂停,照抄);
//   离场 ⇒ 清(原版 BadStatusAllClr;空槽留着也会被投影挡住,但清了才没有
//   "新单位进场继承旧状态"的角落)。
void tickMagicStatus(BattleInstance &b)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		MagicStatusState &st = b.magic_status_of_slot[static_cast<std::size_t>(slot)];
		const SA::Rules::Combatant &c = b.field.at(slot);
		if (!c.occupied)
		{
			st = MagicStatusState{};
			continue;
		}
		if (c.dead || st.turns <= 0)
			continue;
		--st.turns; // 原版 `--cnt`(battle.c:9068)
		if (st.turns <= 0)
			st = MagicStatusState{}; // 原版 `cnt <= 0 ⇒ 置 0`(:9071)
	}
}

// ③ 施加:行动者本回合的指令是**魔法状态系宠技** ⇒ 在其行动位把状态落到目标槽。
//
// ★ 调用时机:resolveAction **之后**(与 applyChargeEffects 同位)。理由:
//   原版的施加发生在行动位的指令派发(case S_SUPERWALL),而 L3 的状态推进/
//   checkCanAct 门可能清掉本行动(`effects.command_cleared`)⇒ 原版此刻
//   COM 已被清成 NONE、走不到那个 case ⇒ 门必须是"没被清"而不是"行动前"。
//   施加后本行动已结束 ⇒ 首个受益/受击者是下一次 resolveAction(它的
//   projectMagicStatus 会把状态投影进快照)⇒ 与原版"同回合后手攻击者可见"一致。
// ⚠️ 目标 = 指令载荷的 `pet_skill.target`(原 COM2 = toindex)。原版对 0..19 的
//   toNo 经 `BATTLE_MultiList` 恒产**单体**表(battle.c:239-263)⇒ `全` 在宠技
//   路径不产生全体展开(数据描述"己方全体"与实际 mechanics 不符,照抄 mechanics)。
// ⚠️ 不摇 rng、不产事件(见上,原版施加端就是静默的)。
void applyMagicStatusPetSkill(BattleInstance &b, int actor,
                              const std::vector<PetSkillEffect> &effects)
{
	if (actor < 0 || actor >= SA::Rules::kSlotCount || !b.commands.present[actor])
		return;
	const SA::Domain::BattleCommand &cmd = b.commands.commands[actor];
	if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::PET_SKILL)
		return;
	const PetSkillEffect *e = findPetSkillEffect(effects, cmd.command.pet_skill.skill_id);
	if (e == nullptr || e->magic_status == 0)
		return; // 非魔法状态系 ⇒ 什么都不做(L3 也已把它当表外跳过)

	const int target = static_cast<int>(cmd.command.pet_skill.target);
	if (target < 0 || target >= SA::Rules::kSlotCount)
		return;
	const SA::Rules::Combatant &tgt = b.field.at(target);
	if (!tgt.occupied || tgt.dead)
		return; // 原版 MultiList 的目标存活检查(battle.c:247-262 的 TargetCheck)

	MagicStatusState &st = b.magic_status_of_slot[static_cast<std::size_t>(target)];
	if (st.status != 0 && st.turns > 0)
		return; // ★ 魔法状态族**单槽**(battle_magic.c:2019-2026:已有任一 ⇒ 整笔跳过)

	st.status = e->magic_status;
	st.turns = e->magic_turns;
	st.nums = e->magic_nums;
}

// 忠犬守护的**链接建立**(批次 A-β d2;`PETSKILL_Guardian`,pet_skill.c:699-770)。
//
// ★★ **调用时机 = 指令接收时**(`onBattleCommand` 存下指令之后),**不是行动位**。
//   回源码复核(battle_command.c:361):原版在 `BattleCommandDispach` 的 `W|` 分支里
//   当场调 `PETSKILL_Use` → `PETSKILL_Guardian` 建立链接;清除发生在**下一回合**的
//   `BATTLE_PreCommandSeq`(battle.c:3596-3598)。⇒ 链接自"交指令"起活到本回合结算
//   结束,**整回合有效** —— 主人在宠物行动位**之前**挨打,守护照样接管。
//   ⚠️★ 因此这里**没有** `command_cleared` 那道门(第一版照铁壁抄了那道门,错):
//     原版指令中途被状态清掉**不会**撤销已建立的链接;中途被睡/被麻痹由
//     `guardianCheck` 的第 ⑤ 条在接管**当场**挡(这正是那条判据存在的理由)。
//
// ★★ **写的是被守护者的 `guardian` 字段、值 = 守护者的槽号**(pet_skill.c:744-766),
//    方向与字段名相反 —— 读的时候是"这一槽被谁守护",写的时候写在**被守护者**身上。
//
// ★ 槽号换算:原版 `pos = BATTLE_Index2No(...)` 取的是**宠物**的槽号,主人槽 = `pos - 5`
//   (pet_skill.c:757 的 `ownerpos = pos - 5;`,再减 `side*SIDE_OFFSET`)。
//   本仓宠物恒占 `主人槽 + kBattlePlayerMax`(见 `exitPetFromField`),且 PET_SKILL 指令
//   落在**主人槽**(B1 的槽位一体模型)⇒ `actor` 就是主人槽、宠物槽 = `actor + 5`。
//   ⚠️★ 这正是本函数第一版的错处:把 `actor` 当宠物槽去反解主人槽,得负数 ⇒
//      **恒越界、从不建立链接**(而当时没有用例能看见它 —— 见 journal §9.0.69)。
//
// ★ 两条分支(逐行照源码):
//     ① option 含 `COM:` + `防御` ⇒ 写**指令目标槽**:`Entry[toNo].guardian = pos`
//        (pet_skill.c:744-753;那里的 `side` 由 `toNo` 反推 ⇒ 落点就是全局槽 `toNo`)。
//        ⚠️ 这一支同时把宠物自己的 COM1 改成 `BATTLE_COM_GUARD`(原地防御)—— 那条
//        **指令改写**本仓不复刻(PET_SKILL 指令不就地改写),登记为已知差异。
//     ② 否则 ⇒ 写**主人槽**(pet_skill.c:755-766)。★ 投产数据走这一支:
//        petskill2.txt 的忠犬行 option 是 `攻%-20  COM:攻击`(实测,`COM:` 后是"攻击")。
//
// ⚠️★ **不摇 rng、不产事件**:原版这一步是静默的(表现由宠技动画承担,本批无表现面)。
// ⚠️ 宠物**不在场** ⇒ 不写:原版 `BATTLE_Index2No` 对不在战斗中的宠物返回 -1,
//    `ownerpos = pos - 5 - side*SIDE_OFFSET` 随即越界 ⇒ 落进那个空 else(:760-761)。
void applyGuardianPetSkill(BattleInstance &b, int actor,
                           const std::vector<PetSkillEffect> &effects)
{
	if (actor < 0 || actor >= SA::Rules::kSlotCount || !b.commands.present[actor])
		return;
	const SA::Domain::BattleCommand &cmd = b.commands.commands[actor];
	if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::PET_SKILL)
		return;
	const PetSkillEffect *e = findPetSkillEffect(effects, cmd.command.pet_skill.skill_id);
	if (e == nullptr || e->guardian_mode == 0)
		return; // 非守护技 ⇒ 什么都不做(L3 也已把它当表外跳过)

	// 宠物槽 = 主人槽 + kBattlePlayerMax;不在场 ⇒ 原版 pos == -1 ⇒ 不写。
	const int pet_slot = actor + SA::Rules::kBattlePlayerMax;
	if (pet_slot >= SA::Rules::kSlotCount || !b.field.at(pet_slot).occupied)
		return;

	if (e->guardian_mode == 2)
	{
		// ① `COM:` + `防御` ⇒ 写**指令目标槽**(pet_skill.c:744-753)。
		const int tgt = static_cast<int>(cmd.command.pet_skill.target);
		if (tgt < 0 || tgt >= SA::Rules::kSlotCount)
			return;
		b.field.at(tgt).guardian = pet_slot;
		return;
	}

	// ② 守护主人:写主人槽、值 = 宠物槽号(pet_skill.c:755-766)。
	b.field.at(actor).guardian = pet_slot;
}

// F07: 只在 resolveAction 返回 item_used 后提交一次。
void consumeUsedItem(BattleInstance &b, int slot, PlayerPool &players, ItemPool &items)
{
	const auto &cmd = b.commands.commands[slot];
	SA::Model::Player *owner =
	    players.resolve(b.player_of_slot[static_cast<std::size_t>(slot)]);
	if (owner == nullptr)
		return;
	const int item_slot = static_cast<int>(cmd.command.use_item.item_slot);
	if (item_slot < 0 || item_slot >= static_cast<int>(SA::Model::kMaxItemHave))
		return;
	const SA::Model::ItemHandle h = owner->items[static_cast<std::size_t>(item_slot)];
	SA::Model::Item *it = items.resolve(h);
	if (it == nullptr)
		return;
	if (--it->current_pile <= 0)
	{
		owner->clearItemSlot(item_slot);
		(void)items.release(h);
	}
}

// ★★ 按事件列表把结果写回世界状态。
//
// 这一步**必须由调用方做**,不是 world 多管闲事:批次 0.5 的裁定
// (00 §9.0.8)是「L3 不写世界状态(`field` 是 const),回合内 HP 走**局部镜像**,
//  局部账回合结束即丢弃,**真正写回由调用方按事件列表执行**」——
// 那里还立了「`field` 逐字节不变」的 memcmp 回归断言来守住它。
//
// ⚠️★ 少了这一步的症状很有欺骗性:结算照跑、事件照发、回合数照涨,
//    但**没有任何人掉血** ⇒ 战斗永远打不完,而没有一处会报错。
//    (2026-09-04 src/ 首次接入构建时就是这样暴露的。)
//
// ⚠️ 覆盖面(截至批次 M.4b):HP / MP · 死亡 · 逃跑 · 打飞累加器 · ★ 骑宠 HP ·
//    ★ 捕获(生成宠物 + **从敌人 L2 实体拷四维 / 成长率 / 名字** + Y 五项 + 挂主人槽 +
//    捕获计数 + 目标离场 + **敌人实体回池**)· ★ 换宠(PET_IN / PET_OUT)。
//    **仍不写**:状态附加(§4.3)· 换装 · 变身 —— 绑在批次 A–D 的链路上,
//    L3 此刻也不产它们的事件 ⇒ **不猜**,与批次 0.5 对暴击/反击的处置同一条纪律。
//
// ★ `ctx` 是世界写的落脚点集合(见 WorldWriteContext):**允许全空** ——
//   HP / 逃跑 / 打飞那几类不需要任何 L2 实体,给它们造一套空池只为填参数是本末倒置。
//   需要落脚点的分支自己判、判不到就显式记账(捕获那一段是唯一的例子)。
void applyEvents(SA::Domain::BattleEvents &events,
                 SA::Rules::BattleField &field,
                 const WorldWriteContext &ctx)
{
	const SA::Domain::BattleEvents pending = events;
	events.events.clear();
	for (std::size_t i = 0; i < pending.events.size(); ++i)
	{
		SA::Domain::BattleEvent e = pending.events[i];
		bool publish = true;
		switch (e.body_kind)
		{
		case SA::Domain::BattleEvent::BodyKind::DAMAGE:
		{
			const SA::Domain::Damage &d = e.body.damage;
			if (d.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount))
				break;
			SA::Rules::Combatant &c = field.at(static_cast<int>(d.target));
			if (!c.occupied)
				break;
			c.hp += d.hp_delta; // hp_delta 是**负数**(L3 侧的约定)
			c.mp += d.mp_delta;
			if (c.mp < 0)
				c.mp = 0;

			// ── 骑宠分摊后的宠物侧落地(批次 M.1)──────────────────────
			//
			// ⚠️★ 这一条此前记作「pet_hp_delta 暂不落地:1.5 的 Combatant 还没有骑宠的
			//    独立 HP 槽(那属 1.2 L2 实体族)」—— **那是错的**:`Combatant::ride_hp` /
			//    `ride_max_hp` 自 `b4670d8`(2026-08-31 阶段 1.0 骨架)就存在,比写下那条
			//    注释的 `d7a5754`(2026-09-04)早三天。
			//    ⇒ pet_hp_delta 的落地**从来不依赖 L2 实体池**。★ 记这一笔是因为它把一件
			//      当时就能做的事记成了被阻塞的事 —— 与 00 §9.0.12 那族「报告印的不是观测」
			//      同源,只是这次分叉的两头是**注释与它自己仓里的字段**。
			//
			// pet_hp_delta 同样是负数(Battle.cpp 的 `d.pet_hp_delta = -to_pet`)。
			// ⚠️ L3 只在 `has_ride && pet_hp > 0` 时才分摊 ⇒ 无骑宠时恒 0,无条件加不会误伤。
			c.ride_hp += d.pet_hp_delta;
			if (c.ride_hp < 0)
				c.ride_hp = 0;
			// ★ **不**夹上界:`to_pet >= 0` ⇒ pet_hp_delta 恒 ≤ 0 ⇒ ride_hp 只会减。
			//   写一个永不触发的上界分支就是"无人触发的清零"那一类(见下方 CAPTURE_ACT)。
			//
			// ⚠️ **骑宠死亡的连带**(解除骑乘 / 换回原图 / 置落马标记)仍不做,但推迟理由
			//    要改准:不是"没有字段",而是 Battle.cpp:1354 已裁定它在**表现侧** ——
			//    由调用方按打完后的 HP 判定并下发 BattleSnapshot,而 BattleSnapshot 本身
			//    尚未下发(world/Api.h 卷首边界 ③)。
			// ── ★★ 附带状态的世界写回(批次 L4.1)──────────────────────
			//
			// ★ 普攻附带状态走 `Damage.status_applied`(原版 `BATTLE_StatusAttackCheck`
			//   成功后 `CHAR_setWorkInt(defindex, StatusTbl[st], turn + 1)`,
			//   `battle_event.c:2918`)⇒ 与伤害同一条事件落地,不另发 StatusChange。
			// ⚠️★ **回合数由世界侧按 `statusTurnsOnApply` 算,不是 L3 传过来的** ——
			//    L3 只告诉"中了哪一种";落地 `+1` 是原版写 work 值那一步的语义。
			//    ★ 本批唯一的施加者是带毒装备(声明 3 ⇒ 落地 4)。
			// ⚠️ 不判"目标身上已有状态" —— 全局互斥已在 L3 的 `rollStatusAttack` 里挡过
			//    (`Status.cpp` 前置 ②);在这里再判一次就是 DR-BT5 反对的双份实现。
			if (d.status_applied != SA::Domain::BattleStatus::BATTLE_ST_NONE)
			{
				c.status = static_cast<std::uint8_t>(d.status_applied);
				c.status_turns =
				    SA::Rules::statusTurnsOnApply(SA::Rules::kSuitPoisonTurns);
			}

			if (c.hp <= 0)
			{
				c.hp = 0;
				c.dead = true;
			}
			break;
		}
		case SA::Domain::BattleEvent::BodyKind::STATUS_TICK:
		{
			// ── 批次 L4.1:状态计时的世界写回 ────────────────────────────
			//
			// ★ L3 已算好递减(或被虚弱/魔障冻结)之后的新值,**直接写,不在此重算** ——
			//   与 A.4 的 KnockbackState 同一条理由:重算等于把 §4.2 的冻结规则实现
			//   第二遍(DR-BT5),而分叉点恰是「虚弱/魔障永不自然解除」这条强约束。
			// ⚠️ 只有值**变化**时 L3 才发本事件 ⇒ 冻结中的单位收不到,值自然保持不变。
			const SA::Domain::StatusTick &st = e.body.status_tick;
			if (st.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount))
				break;
			SA::Rules::Combatant &c = field.at(static_cast<int>(st.target));
			if (!c.occupied)
				break;
			c.status_turns = st.turns;
			break;
		}
		case SA::Domain::BattleEvent::BodyKind::STATUS_CHANGE:
		{
			// ── 批次 L4.1:状态**解除**的世界写回(原版 `BM` 子命令)────────
			//
			// ★ L3 的 `tickStatus` 在计时归零时产 `StatusChange(applied = false)`。
			// ⚠️★ `applied == true` 这一支**本批不产**(附带状态走 Damage.status_applied)
			//    ⇒ 但仍照事件语义写,因为技能 / 魔法施加路径接入后会用它。
			const SA::Domain::StatusChange &sc = e.body.status_change;
			if (sc.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount))
				break;
			SA::Rules::Combatant &c = field.at(static_cast<int>(sc.target));
			if (!c.occupied)
				break;

			if (sc.applied)
			{
				// ★★ 批次 B3a 起,这一支有了真实生产者:宠技·状态攻击
				//   (Battle.cpp 的 strike 成功支)。回合数按**施加者**的投影参数算
				//   (原版 `StatusTbl[st] = gBattleStausTurn + 1`,`:2918`;声明回合
				//   随技能而变:毒攻击 3 / 猛毒 5 / 泥醉 3)—— 事件不带回合数
				//   (IDL 不动)⇒ 从 ctx.actor 在本行动的投影面读回。
				//   ⚠️ 酒醉的"落地再折半"封在共享纯函数 statusWorkOnApply 里,
				//     与 L3 侧镜像同一份实现(DR-BT5:不实现第二遍)。
				//   ⚠️ 匹配不中(状态号 ≠ 投影的状态技参数)⇒ 视同带毒装备口径
				//     (kSuitPoisonTurns);那是本通道 applied=true 唯一的另一来源。
				int declared = SA::Rules::kSuitPoisonTurns;
				if (ctx.actor >= 0 && ctx.actor < SA::Rules::kSlotCount)
				{
					const SA::Rules::Combatant &atk = field.at(ctx.actor);
					if (atk.occupied && atk.mods.pet_skill_apply_status > 0 &&
					    static_cast<int>(sc.status) == atk.mods.pet_skill_apply_status)
						declared = atk.mods.pet_skill_status_turns;
				}
				c.status = static_cast<std::uint8_t>(sc.status);
				c.status_turns =
				    SA::Rules::statusWorkOnApply(static_cast<int>(sc.status), declared);
				break;
			}

			// ── 解除 ──────────────────────────────────────────────────
			// ⚠️★★ **酒醉解除要回写敏捷**(`battle.c:5490`)—— 两支,幅度不同:
			//      有骑宠:`quick += 骑宠的 quick`   无骑宠:`quick × 2`
			//    ★ `05` §4.4 只写了后者(2026-09-11 回源码核出,纪律 ①)。
			//    ⚠️ 这一步**必须在世界侧**:L3 看不到骑宠的 quick(它在另一个槽),
			//      所以 `tickStatus` 只置 `drunk_quick_restore` 标志。
			//    ★ 净效果 = 中酒醉再解除,敏捷变大(施加时从未减半)——
			//      原版就是这样,`05` §4.4 已认下"不能照抄代码要照抄意图"。
			//    ⚠️ 可观测窗口只有解除当回合的剩余结算:回合准备会把敏捷重置回基础值
			//      (`BATTLE_TurnParam`,未移植)⇒ 现在它会**多留一会儿**,记明在案。
			if (c.status == static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_DRUNK))
			{
				// ⚠️★★ **只实现无骑宠那一支,有骑宠那一支有据地不做**:
				//    原版有骑宠时是 `quick += 骑宠的 quick`(`battle.c:5492`),而
				//    ① `Combatant` 没有 `ride_quick` 字段(骑宠只投了 attack/defense/hp);
				//    ② 更要紧的是 **`has_ride` 在世界侧从未被写入过**(全仓实测:只有
				//       用例在设)⇒ 骑乘系统整个未移植 ⇒ 那一支**运行时不可达**。
				//    ⇒ 现在写它就是在猜一个没有输入能验证的实现(纪律 ⓪)。
				// ⚠️★ **但它是一颗会静默引爆的雷**:骑乘系统接上之后 `has_ride` 变真,
				//    这里会**照旧走 ×2** 而不报任何错 ⇒ 敏捷幅度悄悄错掉。
				//    ⇒ 已在 `01` §13 欠债登记,并由 `Status.h` 的 `drunk_quick_restore`
				//      注释指回本处(同 A.4 打飞下游「写下就是定时炸弹」的处置取向)。
				if (ctx.battle != nullptr)
					ctx.battle->quick_to_restore[sc.target] = c.quick;
				c.quick *= 2;
			}

			c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_NONE);
			c.status_turns = 0;
			break;
		}
		case SA::Domain::BattleEvent::BodyKind::SET_HP:
		{
			const SA::Domain::SetHp &h = e.body.set_hp;
			if (h.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount))
				break;
			SA::Rules::Combatant &c = field.at(static_cast<int>(h.target));
			if (!c.occupied)
				break;
			c.hp = h.hp;
			if (c.hp <= 0)
			{
				c.hp = 0;
				c.dead = true;
			}
			break;
		}
		case SA::Domain::BattleEvent::BodyKind::ESCAPE:
		{
			// 批次 A.1:逃跑事件的世界写回。
			const SA::Domain::Escape &esc = e.body.escape;
			if (esc.actor >= static_cast<std::uint32_t>(SA::Rules::kSlotCount))
				break;
			SA::Rules::Combatant &c = field.at(static_cast<int>(esc.actor));
			if (!c.occupied)
				break;
			// ★★ 计数器**无论成败都 +1**(源码 BATTLE_Escape:4346 无条件 ++;§6.1
			//    「失败也累积」)。这就是 L3 读 escape_count+2 里的那个 +1 的落地处 ——
			//    下一回合再逃时基数已抬高,DR-BT15 的线性放大器由此生效。
			++c.escape_count;
			if (esc.succeeded)
			{
				// ★ 逃跑成功 ⇒ 移出战场,**不是战死**:置 occupied=false 让 SideWipedOut
				//   把它当作"已不在场"。⚠️ 不置 dead=true —— 逃跑者没被击败,
				//   把它记成阵亡会污染战果/经验结算(阶段 2)。
				c.occupied = false;
				if (ctx.battle != nullptr && ctx.players != nullptr && ctx.items != nullptr)
					deliverPlayerProfit(*ctx.battle, static_cast<int>(esc.actor), *ctx.players, *ctx.items);
				if (ctx.battle != nullptr && ctx.pets != nullptr)
					syncPetState(*ctx.battle, *ctx.pets);
				if (c.isPlayer() && esc.actor % SA::Rules::kSideOffset < SA::Rules::kBattlePlayerMax)
				{
					exitPetFromField(field, static_cast<int>(esc.actor));
					if (ctx.battle != nullptr)
						ctx.battle->pet_of_slot[esc.actor + SA::Rules::kBattlePlayerMax] = SA::Model::kNullHandle;
				}
			}
			break;
		}
		case SA::Domain::BattleEvent::BodyKind::KNOCKBACK_STATE:
		{
			// 批次 A.4:打飞溢出累加器的世界写回。★ L3 已算好累加后的新值,**直接写**,
			//   不在此重算门槛 —— 重算等于把 RollKnockback 实现第二遍(DR-BT5 反对的双份
			//   实现),且在免疫+一击的角落会分叉(见 battle_events.proto 的 KnockbackState)。
			//   累加(打穿 += 溢出)与清零(命中打飞归 0)的语义都封在 L3,这里只落值。
			const SA::Domain::KnockbackState &ks = e.body.knockback_state;
			if (ks.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount))
				break;
			SA::Rules::Combatant &c = field.at(static_cast<int>(ks.target));
			if (!c.occupied)
				break;
			c.ultimate_accumulator = ks.accumulator;
			break;
		}
		case SA::Domain::BattleEvent::BodyKind::CAPTURE_ACT:
		{
			// ── 批次 A.2 / M.1:捕获事件的世界写回 ──────────────────────
			//
			// ★★ 顺序**照抄** `BATTLE_Capture`(展开视图 `battle_event.c:3480-3567`)。
			//    源码那个顺序本身就是答案:唯一可失败的一步(生成宠物)排在最前,
			//    其后全是不可失败的写 ⇒ 失败即整笔不做,客户端收到 `BT|…|f0`。
			//    ★ 这让 00 §6「gmsv 进程内也需要工作单元边界」有了具体形状,
			//      而且**不需要我们另设一个** —— 回源码核实时它已经在那里了。
			SA::Domain::CaptureAct &cap = e.body.capture_act;
			if (cap.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount))
				break;
			SA::Rules::Combatant &tgt = field.at(static_cast<int>(cap.target));
			if (!tgt.occupied)
				break;

			// ── 第 0 步(源码 :3510):`WORKMODCAPTURE = 0`,**无条件,成败都清** ──
			//
			// ⚠️★ 此前这一条记作「按理应清…待道具/技能能设置它时一并落地」——
			//    **位置与条件都记错了**:它不在成功分支里,而是在 flg 判定**之后**、
			//    创建宠物**之前**无条件执行。
			//    ⇒ 原结论(现在清了没有可观察效果,因为当前无路径设置 capture_bonus)
			//      仍然成立,但那是「无可观察效果」,不是「应该推迟」。
			//    ★ 差别在于:照源码位置写下来,等到有人开始设置 capture_bonus 的那天
			//      它自动是对的;推迟则留下一个要靠人记得回来补的洞。
			if (cap.actor < static_cast<std::uint32_t>(SA::Rules::kSlotCount))
			{
				SA::Rules::Combatant &atk = field.at(static_cast<int>(cap.actor));
				if (atk.occupied)
					atk.mods.capture_bonus = 0;
			}

			// 判定失败 ⇒ 无其余世界写(目标留场),事件仅供客户端演出"抓失败"。
			if (cap.flags == 0u)
				break;

			// ── 第 1 步(源码 :3512):★ **门** —— 生成宠物,−1 即整笔失败 ────
			bool created = false;
			const bool has_l2 = ctx.players != nullptr && ctx.pets != nullptr &&
			                    ctx.player_of_slot != nullptr &&
			                    cap.actor < static_cast<std::uint32_t>(
			                                    SA::Rules::kSlotCount);

			// ★ 被捕目标的 L2 `Enemy` 实体(批次 M.4b)—— 四维 / 成长率 / 名字的源头。
			// ⚠️ 可以为空,那是**登记在案的状态**而不是错误:demo 手填的 foe、
			//    PvP 里的玩家目标都没有 Enemy 实体。⇒ 下面按 `no_l2_enemy` 记账。
			SA::Model::Enemy *src_enemy = nullptr;
			if (ctx.enemies != nullptr && ctx.enemy_of_slot != nullptr)
			{
				src_enemy =
				    ctx.enemies->resolve((*ctx.enemy_of_slot)[cap.target]);
			}
			if (has_l2)
			{
				created = createPetFromCapture(
				    tgt, src_enemy, (*ctx.player_of_slot)[cap.actor],
				    *ctx.players, *ctx.pets);
			}
			if (created && src_enemy == nullptr && ctx.logger != nullptr)
			{
				// ⚠️★ **抓到了,但四维是 0** —— 这一条必须响,而且是 warn 不是 debug:
				//    M.4b 之后"捕获宠四维为 0"不再是正常状态,而是"目标没有 L2 实体"
				//    这条脚手架路径的症状。★ 不落日志的话,欠债 23 会在
				//    demo 上悄悄复活而 `ctest` 全绿(那正是欠债 20/25 那一族的形态)。
				ctx.logger->log(
				    SA::Platform::LogLevel::kWarn,
				    SA::Platform::LogEvent::kCaptureCommitFailed,
				    {{"battle_id", field.battle_id},
				     {"actor", static_cast<std::uint64_t>(cap.actor)},
				     {"target", static_cast<std::uint64_t>(cap.target)},
				     {"reason", std::string_view("no_l2_enemy")}});
			}

			if (!created)
			{
				cap.flags = 0; // 提交失败尚未下发，对外必须是失败。
				if (ctx.logger != nullptr)
				{
					ctx.logger->log(
					    SA::Platform::LogLevel::kError,
					    SA::Platform::LogEvent::kCaptureCommitFailed,
					    {{"battle_id", field.battle_id},
					     {"actor", static_cast<std::uint64_t>(cap.actor)},
					     {"target", static_cast<std::uint64_t>(cap.target)},
					     {"reason", std::string_view(has_l2 ? "pet_create_failed"
					                                        : "no_l2_context")}});
				}
				break;
			}

			// ── 第 2 步(源码 :3519):`PETGETLV` ⇒ 已在 createPetFromCapture 内 ──
			//
			// ⬜ **第 3 步** `LogPet(...)`(:3527)⇒ 挂阶段 2 的 **2.2 审计事件模型**
			//    (00 §9 阶段 2 表;`08` 的 GoldLedger 第三步依赖同一个模型)。
			//    ⚠️ 上面那条 error 日志**不是**它的替代:一条记的是失败,一条是成功审计。
			// ⬜ **第 4 步** `CaptureOkFunction`(:3538)⇒ NPC 行为绑定。
			//    ★ 03 §2.3 已裁定**不复刻**字符串→函数指针的运行期绑定
			//      (原版三处可断且全部静默,18+16+6 例)⇒ 届时用接口 / 函数值直接注册。
			// ── ✅ **第 5 步** `BATTLE_CaptureItemDelAll`(:3540,DR-BT10「全删」)────
			//    原版**扣了道具才给宠物**;此前是白给,本批(捕获扣道具)补上。
			//    ★ 前置门 `CaptureItemCheck`(§6.2 门 ④)已在 resolveTurn **之前**由
			//      `projectCaptureItemGate` 投影到攻方 `capture_item_ok`,门不过则根本不产
			//      成功事件 ⇒ 走到这里的成功捕获,道具必然齐备,这里只管**删**。
			//    ⚠️ `src_enemy == nullptr`(demo foe / PvP)⇒ 无 pet_id ⇒ captureItemDelAll
			//      内 `isNeedCaptureItem` 返 -1、无删,与门那侧默认满足一致。
			if (ctx.items != nullptr && src_enemy != nullptr)
			{
				if (SA::Model::Player *owner = ctx.players->resolve(
				        (*ctx.player_of_slot)[cap.actor]);
				    owner != nullptr)
				{
					captureItemDelAll(*owner, src_enemy->pet_id, *ctx.items);
				}
			}

			// ── 第 6 步(源码 :3542):捕获计数 +1 ────────────────────────
			if (SA::Model::Player *owner =
			        ctx.players->resolve((*ctx.player_of_slot)[cap.actor]);
			    owner != nullptr)
			{
				++owner->capture_count;
			}

			// ── 第 7 步(源码 :3545):`BATTLE_Exit` —— 目标离场 ────────────
			//
			// ★ 置 occupied=false 让 sideWipedOut 视其"已不在场";**不置 dead** ——
			//   被捕不是战死,记成阵亡会污染战果/经验结算(同逃跑成功那一条)。
			// ⚠️ 必须在 createPetFromCapture **之后**:那一步要读 tgt 与 src_enemy 的字段。
			tgt.occupied = false;

			// ★★ **敌人 L2 实体随离场一并释放(批次 M.4b)** ——
			//    这不是我们加的清理,是源码的行为:`_BATTLE_Exit`(`battle.c:1114-1115`)
			//    对 `CHAR_TYPEENEMY` 直接调 `CHAR_endCharOneArray`(销毁实体)。
			//    ⇒ 敌人离场即销毁,与"玩家离场只是退出战斗"截然不同。
			// ⚠️★ 漏掉这一步的后果与 M.1 那条一模一样:**池只增不减,而没有一处报错**,
			//    跑够久之后表现为"刷怪突然失败"(池满),那时离真正的原因已经很远。
			//    ⇒ 与 `onDisconnected` 释放宠物是同一条纪律的第三处兑现。
			if (ctx.enemies != nullptr && ctx.enemy_of_slot != nullptr)
			{
				(void)ctx.enemies->release((*ctx.enemy_of_slot)[cap.target]);
				(*ctx.enemy_of_slot)[cap.target] = SA::Model::kNullHandle;
			}

			// ── 第 8 步(源码 :3546-3553)—— 三条里两条已做、一条仍不做 ────────
			//
			// ✅ `CHAR_complianceParameter(pindex)`(:3546)⇒ **批次 M.4b 已落地**:
			//    推导本身在 `createPetFromCapture` 里(算 Y 五项那次),而"宠物的战斗
			//    三围"在它**入场**时由 `enterPetToField` 推(DR-DT9)。
			//    ⚠️★ 分两处不是重复:一处是**存下来的初值快照**(Y 五项),
			//      一处是**每次入场的战斗投影**;原版共用一个 WORK 面,我们分了两层。
			// ✅ `VARIABLEAI = 0`(:3547)⇒ 已在 `createPetFromCapture` 内。
			// ⬜ AI 修正段(`CHAR_DEFAULTMAXAI − WORKFIXAI`,:3548-3553)⇒ 需要未建的
			//    `WORKFIXAI`(宠物 AI 值,属宠物养成面)⇒ 仍不做。
			break;
		}
		case SA::Domain::BattleEvent::BodyKind::PET_SWITCH:
		{
			// ── 批次 DR-BT21:换宠指令的世界写回 ────────────────────────
			//
			// ★ L3 只发了「换宠意图」(PetSwitch);真实的入 / 离场在这里落地 —— 读 L2 的
			//   Player.pets / default_pet,调 M.2 的 enterPetToField / exitPetFromField。
			// ⚠️★ **不复刻源码 BATTLE_PetOut 的反推缺陷**(DR-BT20 陷阱①):原版
			//   `PetDefaultEntry` 恒返 0、靠「入场后 DEFAULTPET 是否 <0」反推成败,而宠位
			//   被占时它**不清** DEFAULTPET ⇒ 误判「叫出成功」却没入场。这里用
			//   `enterPetToField` 的**真实返回值**判成败,失败时 default_pet 保持原值。
			const SA::Domain::PetSwitch ps = e.body.pet_switch;
			publish = false; // 意图不下发；成功后转为已提交的 Enter/Quit。
			if (ps.actor >= static_cast<std::uint32_t>(SA::Rules::kSlotCount))
				break;

			// 换宠要读写主人的 L2 Player 实体。观战 / 敌人 / 尚未接 L2 的槽没有实体 ⇒
			// 跳过 + 记账(同捕获 no_l2_context;敌人 AI 换宠尚未移植,此路径正常不触发)。
			const bool has_l2 = ctx.players != nullptr && ctx.pets != nullptr &&
			                    ctx.player_of_slot != nullptr;
			SA::Model::Player *owner =
			    has_l2 ? ctx.players->resolve((*ctx.player_of_slot)[ps.actor])
			           : nullptr;
			if (owner == nullptr)
			{
				if (ctx.logger != nullptr)
					ctx.logger->log(
					    SA::Platform::LogLevel::kError,
					    SA::Platform::LogEvent::kPetSwitchFailed,
					    {{"battle_id", field.battle_id},
					     {"actor", static_cast<std::uint64_t>(ps.actor)},
					     {"reason", std::string_view("no_owner")}});
				break;
			}

			if (!ps.call_out)
			{
				// ── 收回(PET_IN):宠物离场 + 清出战宠 ────────────────────
				const auto pet_slot = ps.actor + SA::Rules::kBattlePlayerMax;
				if (pet_slot >= SA::Rules::kSlotCount || !field.at(static_cast<int>(pet_slot)).occupied)
					break;
				if (ctx.battle != nullptr)
				{
					syncPetState(*ctx.battle, *ctx.pets);
					ctx.battle->pet_of_slot[pet_slot] = SA::Model::kNullHandle;
				}
				exitPetFromField(field, static_cast<int>(ps.actor));
				owner->default_pet = -1;
				e = SA::Domain::BattleEvent{};
				e.body_kind = SA::Domain::BattleEvent::BodyKind::QUIT;
				e.body.quit.actor = pet_slot;
				publish = true;
				break;
			}

			// ── 叫出(PET_OUT):第 pet_slot 槽宠入场 ──────────────────────
			if (ps.pet_slot >= SA::Model::kMaxPetHave)
				break;
			SA::Model::Pet *pet = ctx.pets->resolve(owner->pets[ps.pet_slot]);
			if (pet == nullptr)
			{
				// 该槽空 / 悬空句柄 ⇒ 叫不出,default_pet 不变。
				if (ctx.logger != nullptr)
					ctx.logger->log(
					    SA::Platform::LogLevel::kError,
					    SA::Platform::LogEvent::kPetSwitchFailed,
					    {{"battle_id", field.battle_id},
					     {"actor", static_cast<std::uint64_t>(ps.actor)},
					     {"pet_slot", static_cast<std::uint64_t>(ps.pet_slot)},
					     {"reason", std::string_view("no_pet")}});
				break;
			}
			if (enterPetToField(field, static_cast<int>(ps.actor), *pet))
			{
				owner->default_pet = static_cast<int>(ps.pet_slot);
				const auto pet_slot = ps.actor + SA::Rules::kBattlePlayerMax;
				if (ctx.battle != nullptr)
				{
					ctx.battle->pet_of_slot[pet_slot] = owner->pets[ps.pet_slot];
					ctx.battle->quick_to_restore[pet_slot].reset();
				}
				e = SA::Domain::BattleEvent{};
				e.body_kind = SA::Domain::BattleEvent::BodyKind::ENTER;
				e.body.enter.actor = pet_slot;
				publish = true;
			}
			else if (ctx.logger != nullptr)
			{
				// 宠位被占 / 宠物已死(enterPetToField 门②③)⇒ default_pet 不变。
				ctx.logger->log(
				    SA::Platform::LogLevel::kError,
				    SA::Platform::LogEvent::kPetSwitchFailed,
				    {{"battle_id", field.battle_id},
				     {"actor", static_cast<std::uint64_t>(ps.actor)},
				     {"pet_slot", static_cast<std::uint64_t>(ps.pet_slot)},
				     {"reason", std::string_view("enter_failed")}});
			}
			break;
		}
		default:
			// 其余事件是**表现**(HIT / TEXT_BOX / …)或未移植链路的占位,
			// 对世界状态无影响 ⇒ 显式落到这里,不是遗漏。
			break;
		}
		if (publish)
			(void)events.events.push_back(e);
	}
	if (ctx.battle != nullptr && ctx.pets != nullptr)
		syncPetState(*ctx.battle, *ctx.pets);
}

// ── 1.4 demo 的战场(脚手架,见 platform/api.h 的 DemoBattleConfig)────
//
// ⚠️★ **这些数字不是内容数据,也不假装是。**
//   00 §0 已认下 ③ 层「规则不可自证」与 ④ 层「表现与手感永远无法验证」,
//   真正的敌人数值属 L4 内容导入(D 线),不在 1.4 的路径上。
//   ⇒ 此处只需满足两条**可判定**的性质,它们都由用例钉着:
//     ① 客户端一条指令不发,战斗也要在有限回合内结束 ——
//        否则 demo 挂起时分不清是"敌人打不动"还是"事件流断了";
//     ② 客户端正常出招时,战斗**更快**结束 ⇒ 指令确实被采纳了。
//        ★ 这一条才是 1.4 真正要证明的东西:上行链路是通的。
//
// ★★ **批次 M.3 起,三围不再硬编码,改由四维经 `deriveBaseStats` 推出**(DR-DT9)。
//    欠债 23 把「demo 玩家三围硬编码」与「捕获宠 / 换宠入场宠三围恒 0」列为
//    `complianceParameter` 的**三个共同依赖处** ⇒ 这里接上,那条欠债的上半才真正关闭。
//    ⚠️ 换的是**数值的来源**,不是那两条性质:四维仍是脚手架取值(见下),
//      但它们经过的是原版公式,而不再是我凭手感填的三围。
//
// ⚠️★ 一处顺带被证伪的东西:**原来硬编码的 `attack=300 / max_hp=400` 在原版公式下
//    根本不可达** —— 300 的攻击需要 str≈30,000 量级,而那个量级的四维经
//    `(vital*4+str+tough+dex)*0.01` 至少推出 300+ 的 max_hp,不可能只有 400。
//    ⇒ 旧数字不只是"未推导",它是一组**自相矛盾**的三围。
SA::Rules::BattleField makeDemoField()
{
	SA::Rules::BattleField f{};

	// ★ 四维取值的判据只有两条(即上面 ① ②),**不是**"原版 20 级玩家该有多少" ——
	//   那属 L4 内容导入。量级参照 `06` §3.1:原版四维是几千 ~ 几万,`*0.01` 后
	//   三围才落到几十 ~ 几百的观感。
	//   me:力量为主(攻高)· 体力垫底给出血量余量 · 速度 20,000 ⇒ quick 200 先手。
	SA::Rules::Combatant &me = f.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.mp = 100;
	me.max_mp = 100;
	me.luck = 10;
	const SA::Rules::DerivedStats me_stats =
	    SA::Rules::deriveBaseStats(8000, 30000, 4000, 20000);
	me.vital = 8000;
	me.str = 30000;
	me.tough = 4000;
	me.dex = 20000;
	me.attack = me_stats.attack;   // 322
	me.defense = me_stats.defense; // 88
	me.quick = me_stats.quick;     // 200
	me.fix_dex = me_stats.quick;
	me.max_hp = me_stats.max_hp; // 860
	me.hp = me.max_hp;           // ★ 满血入场:hp 不再是独立的手填值

	// foe:各维按比例略低 ⇒ 攻防速血全弱一档,但**打得动**(性质 ① 要求它能打死玩家)。
	SA::Rules::Combatant &foe = f.at(SA::Rules::kSideOffset);
	foe.occupied = true;
	foe.kind = SA::Rules::CombatantKind::kEnemy;
	foe.slot = static_cast<std::uint8_t>(SA::Rules::kSideOffset);
	foe.level = 18;
	foe.luck = 5;
	const SA::Rules::DerivedStats foe_stats =
	    SA::Rules::deriveBaseStats(4000, 26000, 2000, 15000);
	foe.vital = 4000;
	foe.str = 26000;
	foe.tough = 2000;
	foe.dex = 15000;
	foe.attack = foe_stats.attack;   // 273
	foe.defense = foe_stats.defense; // 57
	foe.quick = foe_stats.quick;     // 150
	foe.fix_dex = foe_stats.quick;
	foe.max_hp = foe_stats.max_hp; // 590
	foe.hp = foe.max_hp;
	return f;
}

// 遇敌骰子分母系数 —— 移植 `getEnemyAction`(`configfile.c:2669`):clamp 到 [1,100]。
//   ★ 原版 `config.enemyact` 未配(`csa8.0/setup.cf` 无 `ENEMYACTION`)时为 0 ⇒ clamp 返回 1。
int clampEnemyAction(std::uint32_t enemy_action)
{
	if (enemy_action > 100)
		return 100;
	if (enemy_action < 1)
		return 1;
	return static_cast<int>(enemy_action);
}

// 遇敌开战时玩家进场的 `Combatant`(批次 W.4,玩家侧 Side[0] 首位)。
//
// ⚠️★ 玩家四维 / 等级**无真实来源**:1.5 无选角(同 `onSessionReady` 名字留空、
//    `makeDemoField` 手填)⇒ 用占位四维,**登记为无选角来源那族残缺**,阶段 2 接选角后由存档取代。
// ★ 占位量级照 `makeDemoField` 的 me(力量为主):让占位玩家能打动遇敌链产出的真实弱怪,
//   使「打赢拿经验」闭环有意义 —— 与欠债 25「真实模板 18 级弱 demo 约 16 倍」同一量级考量。
SA::Rules::Combatant makePlayerCombatant(const SA::Model::Player *player = nullptr)
{
	SA::Rules::Combatant c{};
	c.occupied = true;
	c.kind = SA::Rules::CombatantKind::kPlayer;
	c.slot = 0;
	if (player)
	{
		c.level = player->level;
		c.hp = player->hp;
		c.mp = player->mp;
		c.max_mp = player->max_mp;
		c.charm = player->charm;
		c.luck = player->luck;
		c.vital = player->vital;
		c.str = player->str;
		c.tough = player->tough;
		c.dex = player->dex;
		const auto stats = SA::Rules::deriveBaseStats(c.vital, c.str, c.tough, c.dex);
		c.max_hp = stats.max_hp;
		c.attack = stats.attack;
		c.defense = stats.defense;
		c.quick = stats.quick;
		c.fix_dex = stats.quick;
		c.dead = c.hp <= 0;
		c.elements[0] = player->earth;
		c.elements[1] = player->water;
		c.elements[2] = player->fire;
		c.elements[3] = player->wind;
		return c;
	}
	c.level = 20;
	c.mp = 100;
	c.max_mp = 100;
	c.luck = 10;
	const SA::Rules::DerivedStats st =
	    SA::Rules::deriveBaseStats(8000, 30000, 4000, 20000);
	c.vital = 8000;
	c.str = 30000;
	c.tough = 4000;
	c.dex = 20000;
	c.attack = st.attack;
	c.defense = st.defense;
	c.quick = st.quick;
	c.fix_dex = st.quick;
	c.max_hp = st.max_hp;
	c.hp = c.max_hp;
	return c;
}

} // namespace

// ★★ config.cpp 把 demo_battle.slot 的上限写死成 9,因为 L0 够不着 L3
//    (platform 不依赖 rules,那是分层的硬约束)。⇒ 两处一致性由这里守。
//    ⚠️ 少了它,某天 kSideOffset 改了、配置校验照旧,表现是玩家被放进敌方半场
//      而没有任何一处报错 —— 00 §10.4 那类静默错误。
static_assert(SA::Rules::kSideOffset == 10,
              "demo_battle.slot 的配置上限(config.cpp 里的 9)是按 "
              "kSideOffset == 10 写死的;kSideOffset 变了就要同步改那里");

struct World::Impl : GoldAuditSink
{
	// ── GoldLedger 第 ③ 步的出口(world/Api.h 的 GoldLedger 节)──────────────
	//
	// ★ 账本把审计事件交到这里,这里转进**现有**日志通道(kGoldChanged)——
	//   事件不另开一条通道,但**也不可能没有通道**:add/del 无条件调 sink,
	//   而生产路径上 sink 就是本 Impl。
	void onGoldTx(const GoldTx &tx) const override
	{
		logger.log(SA::Platform::LogLevel::kInfo, SA::Platform::LogEvent::kGoldChanged,
		           {{"corr", tx.correlation},
		            {"delta", static_cast<std::int64_t>(tx.delta)},
		            {"before", static_cast<std::int64_t>(tx.before)},
		            {"after", static_cast<std::int64_t>(tx.after)},
		            {"overflow", static_cast<std::int64_t>(tx.overflow)},
		            {"pre_clamped", static_cast<std::int64_t>(tx.pre_clamped)},
		            {"disposition", std::string_view(goldDispositionName(tx.disposition))},
		            {"reason", std::string_view(goldReasonName(tx.reason))}});
	}

	// 一条连接上的全部状态。★ Connection 与 Session 在 1.5 是 1:1,
	//   但类型是分开的 —— 01 §5.2 明写两者生命周期不同,
	//   压在一起正是原版 LoginType 的毛病。重连窗口留到阶段 2。
	struct Conn
	{
		SA::Net::ConnectionId conn_id = 0;
		SA::Net::FrameReader reader{};
		std::unique_ptr<SA::Net::Session> session;
		std::vector<std::uint8_t> outbound{};

		// ── 走路的运行时态(批次 W.1。原 work 区 CHAR_WORKWALKARRAY / WORKWALKSTARTSEC)──
		//   方向串:每字符一步(小写移动 / 大写转身,CHAR_ctodirmode),首字符是下一步;
		//   kCharLoop 玩家段按 walksendinterval 间隔逐字符消费(CHAR_walkcall)。
		std::string walk_seq{};
		// 下次可走一步的最早时刻(节拍判断,原 WORKWALKSTARTSEC/MSEC + walksendinterval)。
		//   ⚠️ 用"下次可走"而非"上次走过"的时刻:ManualClock 从 0 起,后者无法区分
		//      "尚未走过"与"在 t=0 走过"(同 BattleInstance::next_turn_at_ms 的取向)。
		SA::Platform::Millis next_walk_at_ms = 0;

		// ── 遇敌累积值(批次 W.4。原 `CONNECT_CEP`,char_walk.c:538/594/610)──────
		//   走一格判遇敌:`temp = cep`(先夹在 `[prob_min, prob_max]`),骰子命中后 `cep = prob_min`。
		//   ⚠️★ 原版 `cep++` 累积在**战斗态**分支(`char_walk.c:607`),而玩家在 kCharLoop
		//     走路时恒**非战斗态**(战斗中 walk_seq 已被清、且不 tick 走路)⇒ 那条累积路径
		//     在本实现走不到 ⇒ 照抄源码结构但不硬接一个到不了的分支(同 M.6/M.7 的等价/冗余处置)。
		std::int32_t cep = 0;
		bool logged_in = false;
		bool pending = false;
		bool detached = false;
		bool save_failed = false;
		bool deferred_logout = false;
		std::uint64_t deferred_correlation = 0;
		std::uint64_t char_id = 0;
		std::uint64_t revision = 0;
		SA::Platform::Millis retry_at = 0;
		SA::Platform::Millis login_after = 0;

		// ── NPC 对话与窗口会话(批次 W.8。原 lssproto_WN_send / WN_recv)───────────
		std::uint32_t active_window_id = 0;
		std::uint64_t active_window_npc_id = 0;
		std::string last_window_text{};

		// ── ExChangeMan 待决任务交互 (批次 W.9) ──────────────────────
		struct PendingExChange
		{
			std::uint64_t npc_id = 0;
			int block_index = -1;
			int branch_idx = 0;
		};
		PendingExChange pending_exchange{};

		// ── ShopMan 待决商店交互 (批次 W.12) ─────────────────────────
		struct PendingShop
		{
			std::uint64_t npc_id = 0;
		};
		PendingShop pending_shop{};

		// ── PetShop 待决宠物商店交互 (批次 W.13) ─────────────────────
		struct PendingPetShop
		{
			std::uint64_t npc_id = 0;
		};
		PendingPetShop pending_pet_shop{};

		// ── PetSkillShop 待决技能导师交互 (批次 W.13) ─────────────────
		struct PendingPetSkillShop
		{
			std::uint64_t npc_id = 0;
		};
		PendingPetSkillShop pending_pet_skill_shop{};

		// ── WarpMan 待决传送员交互 (批次 W.14) ───────────────────────
		struct PendingWarpMan
		{
			std::uint64_t npc_id = 0;
			int dest_idx = -1;
		};
		PendingWarpMan pending_warpman{};
	};

	Impl(const SA::Platform::ServerConfig &cfg, SA::Platform::Clock &clk,
	     SA::Platform::Logger &log, SA::Platform::RandomSource &rnd,
	     SA::Net::Transport &tp)
	    : config(cfg), clock(clk), logger(log), random(rnd), transport(tp),
	      world_rng(rnd.masterSeed() ^ 0x9E3779B97F4A7C15ull)
	{
		// olink 按 fixture 地图尺寸分配(map 已在成员初始化中建好,声明在 olink 之前)。
		olink.assign(static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height),
		             {});
	}

	SA::Platform::ServerConfig config;
	SA::Platform::Clock &clock;
	SA::Platform::Logger &logger;
	SA::Platform::RandomSource &random;
	SA::Net::Transport &transport;
	SA::SessionStorage::Service *storage = nullptr;
	std::string content_version;
	SA::Domain::CharacterRecord character_defaults{};
	SA::Domain::CharacterRecord snapshot(SA::Net::SessionId id) const;
	bool install(SA::Net::SessionId id, const SA::Domain::CharacterRecord &record);

	// 世界级遇敌 rng(批次 W.4):遇敌骰子(randMod)+ 遇敌链选怪(pickEnemyGroup/rollEnemyList)用它。
	// ⚠️★ 种子从 `masterSeed` **派生但不调 `nextSeed`** —— `nextSeed` 会消耗战斗种子序列、
	//    使现有战斗的回放种子整体平移(现有用例的 `spawnEnemy` 结果会变)。异或一个盐使它与
	//    任何战斗种子的序列都不同,同时随 `masterSeed` 确定 ⇒ 遇敌本身也可回放。
	SA::Rules::SeededRandom world_rng;
	std::uint32_t next_window_id = 0;

	std::map<SA::Net::ConnectionId, Conn> conns;
	// 1.5 里 SessionId == ConnectionId(见上)。
	std::map<BattleId, BattleInstance> battles;
	struct FinishedBattle
	{
		BattleStats stats;
		SA::Rules::BattleField field; // 仅供已有观察接口，脱离实体/会话/RNG。
	};
	static constexpr std::size_t kFinishedBattleLimit = 128;
	std::map<BattleId, FinishedBattle> finished_battles;

	bool inBattle(SA::Net::SessionId sid) const
	{
		for (const auto &entry : battles)
		{
			const auto &battle = entry.second;
			const auto slot = battle.slot_of.find(sid);
			if (!battle.stats.finished && slot != battle.slot_of.end() &&
			    battle.field.at(slot->second).occupied)
				return true;
		}
		return false;
	}

	void retireBattle(BattleId id)
	{
		const auto it = battles.find(id);
		if (it == battles.end())
			return;
		auto &battle = it->second;
		syncPetState(battle, pets);
		for (auto handle : battle.enemy_of_slot)
			(void)enemies.release(handle);
		battle.stats.finished = true;
		finished_battles.emplace(id, FinishedBattle{battle.stats, battle.field});
		while (finished_battles.size() > kFinishedBattleLimit)
			finished_battles.erase(finished_battles.begin());
		battles.erase(it);
	}
	void pushBattleSnapshot(const BattleInstance &battle)
	{
		auto snapshot = makeBattleSnapshot(battle.field);
		for (auto &unit : snapshot.combatants)
		{
			if (const auto *player = players.resolve(battle.player_of_slot[unit.slot]); player && storage)
			{
				unit.name = player->name;
				unit.image_id = static_cast<std::uint32_t>(player->image);
			}
			else if (const auto *pet = pets.resolve(battle.pet_of_slot[unit.slot]))
			{
				unit.name = pet->name;
				unit.image_id = static_cast<std::uint32_t>(pet->base_image);
			}
			else if (const auto *enemy = enemies.resolve(battle.enemy_of_slot[unit.slot]))
			{
				unit.name = enemy->name;
				unit.image_id = static_cast<std::uint32_t>(enemy->base_image);
			}
		}
		for (auto sid : battle.members)
			if (auto conn = conns.find(sid); conn != conns.end() && conn->second.session != nullptr)
				if (!conn->second.session->push(snapshot, conn->second.outbound))
					conn->second.session->close();
	}

	SA::Rules::RulesConfig rules_config{};
	BattleId next_battle_id = 1;
	std::uint64_t ticks = 0;
	SA::Platform::Millis now_ms = 0;
	bool shutdown_requested = false;
	bool stopped = false;

	// ── L2 实体池与索引(批次 M.1,01 §13 欠债 20 的 ①)────────────────
	//
	// ★★ 这是「地基」变成「运行时」的那一步:`shared/model/` 的三个纯头此前
	//    **没有任何一个实例挂在 World 上** ⇒ `ModelPoolTest` 全绿而世界里没有实体,
	//    与 §9.0.16 那条「`OnSessionReady` 只打日志」是同一族静默(欠债 20 的原话)。
	//
	// ⚠️ 池按值内嵌:`EntityPool` 的存储是 `std::array` ⇒ 这两个成员就是那 2,100 个槽
	//    本身,不是指针。★ Impl 自己在 `unique_ptr` 里(pimpl)⇒ 它们落在堆上一次分配完,
	//    此后运行期零分配(15 §9.1 支柱 ①)。
	PlayerPool players{};
	PetPool pets{};

	// ★ 敌人池(批次 M.4b)。⚠️ 它比前两个大一个数量级(10,000 槽,见 kMaxEnemies)
	//    ⇒ Impl 的 sizeof 随之涨,而 Impl 在 `unique_ptr` 里 ⇒ 仍是**启动期一次**堆分配,
	//    运行期零分配不变(15 §9.1 支柱 ①)。
	EnemyPool enemies{};

	// ★ 道具池(批次 I.1)。⚠️ 与 enemies 同为 10,000 槽 ⇒ Impl 的 sizeof 再涨一档
	//    (Item 含三个 64B 名字槽,单槽约 200B ⇒ 本池约 2MB),但仍是**启动期一次**堆分配
	//    (Impl 在 unique_ptr 里),运行期零分配不变(15 §9.1 支柱 ①)。
	// ⚠️★ **本批不接任何写入者** —— 没有捡起 / 掉落 / 捕获扣道具会 allocate 它 ⇒
	//    `itemCount()` 恒 0,是**登记在案的留白**(池地基先于写入链路落地,同 M.1 的分层)。
	ItemPool items{};

	// 会话 → Player 实体。★ 03 §8.2 三条查找路径之一(原 `getCharindexFromFdid`
	//   那族**全表扫** + 每格加解锁,`fdnum=1000` 下每条应答扫 1,000 次)。
	// ⚠️ 索引里的句柄**可能悬空**,这是正常的 —— 验世代是 `EntityPool::resolve` 的活
	//   (EntityIndex.h 卷首的两步分工)。
	SA::Model::ConnIndex player_of_session{};

	// ── 地图(批次 W.1)。fixture,真实地图(LS2MAP,1,235 图)走 D 线导入(见 Api.h 地图节)──
	//   ⚠️ 单张 fixture:1.4/demo 只有一个场景;多 floor 表留到内容导入(那时按 floor 索引)。
	GridMap map = makeFixtureMap(64, 64);
	TileAttrTable map_attr = makeFixtureAttr();

	// ── 多地图与视野索引 (批次 D.2) ──────────────────────────────────
	struct FloorState
	{
		std::int32_t floor_id = 0;
		GridMap map{};
		std::vector<std::vector<SA::Net::ConnectionId>> olink{};
	};
	std::unordered_map<std::int32_t, FloorState> floors{};

	FloorState *getFloor(std::int32_t floor) noexcept
	{
		auto it = floors.find(floor);
		if (it != floors.end())
			return &it->second;
		return nullptr;
	}
	const FloorState *getFloor(std::int32_t floor) const noexcept
	{
		auto it = floors.find(floor);
		if (it != floors.end())
			return &it->second;
		return nullptr;
	}

	// ── 遇敌数据表(批次 W.4)────────────────────────────────────────────
	//   ★ 默认空 ⇒ `findEncountArea` 恒返 -1 ⇒ 永不遇敌(现有走路用例不受影响)。
	//     由 `loadEncounterTables` 注入(1.5 fixture / 阶段 2 D 线导入)。
	//   坐标 ─encount_areas→ 区域 ─enemy_groups→ 编组 ─encounters→ 敌人行 ─enemy_templates→ 模板。
	std::vector<EncountArea> encount_areas{};
	std::vector<EnemyGroup> enemy_groups{};
	std::vector<EnemyEncounter> encounters{};
	std::vector<EnemyTemplate> enemy_templates{};

	// ── 道具效果表(批次 I.4「使用道具」)──────────────────────────────────
	//   ★ 默认空 ⇒ 任何道具 heal 投影恒 0 ⇒ L3 的 USE_ITEM 分支跳过(现有用例不受影响)。
	//     由 `loadItemEffects` 注入(fixture / 阶段 2 D 线导入)。按 item_id 线性查(表小)。
	std::vector<ItemEffect> item_effects{};

	// ── 宠技·直攻系效果表(批次 B1)────────────────────────────────────────
	//   ★ 与 item_effects 同款:默认空 ⇒ PET_SKILL 一律"表外技能"(L3 整次行动跳过、
	//     不摇 rng),现有用例不受影响。由 `loadPetSkillEffects` 注入。按 skill_id 线性查。
	std::vector<PetSkillEffect> pet_skill_effects{};

	// ── WARP 传送点表(批次 W.6)─────────────────────────────────────────
	std::vector<WarpPoint> warp_points{};
	const WarpPoint *findWarpPoint(std::int32_t floor, std::int32_t x,
	                               std::int32_t y) const noexcept
	{
		for (const auto &wp : warp_points)
		{
			if (wp.src_floor == floor && wp.src_x == x && wp.src_y == y)
				return &wp;
		}
		return nullptr;
	}

	// ── 世界 NPC 实体(批次 W.7)─────────────────────────────────────────
	std::vector<NpcEntity> npc_entities{};
	std::size_t npcAt(std::int32_t floor, std::int32_t x,
	                  std::int32_t y) const noexcept
	{
		for (std::size_t i = 0; i < npc_entities.size(); ++i)
		{
			if (npc_entities[i].floor == floor && npc_entities[i].x == x &&
			    npc_entities[i].y == y)
				return i;
		}
		return npc_entities.size();
	}
	void refreshNpcView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
	                    std::int32_t ox, std::int32_t oy);

	// ── NPC 巡逻与漫游游荡 (批次 W.11) ──────────────────────────────────
	std::size_t npc_charloop_cursor = 0;
	void wanderNpcs(std::size_t max_this_tick);
	void broadcastNpcMove(const NpcEntity &npc, std::int32_t ox, std::int32_t oy);
	bool isNpcEngagedInDialog(std::uint64_t npc_id) const;

	// ── 世界刷怪点与世界态敌人(批次 W.2 / W.3)──────────────────────────────
	//   spawn_points:注入的刷怪点(loadSpawnPoints,默认空 ⇒ 世界无常驻怪);
	//   world_enemies:当前在地图上的敌人。★ 与战斗态敌人**共用 `enemies` 池但分开跟踪** ——
	//     战斗态在 `b.enemy_of_slot`,世界态在这里 ⇒ `enemyCount()` 数全池、`worldEnemyCount()`
	//     只数这里(两条静默各有探针,同欠债 25 的观察面纪律)。
	std::vector<SpawnPoint> spawn_points{};
	struct WorldEnemy
	{
		SA::Model::EntityHandle handle;
		std::size_t spawn_point; // 来自 spawn_points 的哪个点(取游荡中心 / 半径 / 间隔)
	};
	std::vector<WorldEnemy> world_enemies{};

	// kCharLoop 非玩家段的**条数制**摊还游标(批次 W.3。原 CHAR_Loop 的 static charcnt)。
	//   ★ 每 tick 处理够 `tempo.enemy_move_num` 只即停、记位下 tick 续、遍历完绕回 0。
	//   ⚠️★★ **条数制不是时间预算制** —— 8.0 的 `_CHAR_LOOP_TIME` 三证实测**关**(15 §5.2 C18),
	//      unifdef_80 展开视图把它误当时间预算是选错分支(见 §9.0.45)。
	//   ⚠️ 原版游标绕回 `playernum` 跳过玩家段;我们分池 ⇒ 游标只在 world_enemies 上绕,
	//      天然不含玩家(玩家段每 tick 全扫)⇒ 语义等价、更简单。
	std::size_t charloop_cursor = 0;

	// ── 视野的运行时对象索引(里程碑②。原版 Map::olink,10 §3.1)──────────────
	//   每格挂着**当前在该格的玩家会话**;视野广播扫 529 格遍历它(§5.2)。
	//   ⚠️ size == map.width*height,构造时按 fixture 尺寸分配(见构造函数体)。
	//   ★ 本批只放玩家(kEnemy/NPC 未进场);真实大地图时换稀疏结构(现 64×64 够)。
	std::vector<std::vector<SA::Net::ConnectionId>> olink;

	// ── 视野广播(里程碑②)。★ 作为成员而非自由函数:要访问 olink/conns/players 等私有状态,
	//   而 conns 的 value(Conn)是 Impl 私有嵌套 ⇒ context struct(如 WorldWriteContext)装不下,
	//   只能做成员。声明在此,定义在文件后半(「World::Impl 的视野广播方法」一节)。
	std::vector<SA::Net::ConnectionId> collectVisible(std::int32_t floor, std::int32_t cx, std::int32_t cy,
	                                                  SA::Net::ConnectionId self) const;
	template <typename M>
	void sendTo(SA::Net::ConnectionId to, const M &msg);
	void appearBetween(SA::Net::ConnectionId a, const SA::Model::Player &pa,
	                   SA::Net::ConnectionId b);
	void broadcastMove(SA::Net::ConnectionId mover, std::int32_t ox, std::int32_t oy,
	                   const SA::Model::Player &p);
	void broadcastSpawn(SA::Net::ConnectionId who, const SA::Model::Player &p);
	void broadcastDespawn(SA::Net::ConnectionId who, std::int32_t floor, std::int32_t x, std::int32_t y);

	// ── 世界敌人:生成 / 游荡 / 视野(批次 W.2 / W.3)──────────────────────────
	//   ★ 都是 Impl 成员:要碰 enemies 池 / world_enemies / olink / 视野下行,自由函数装不下。
	//
	// 找站在 (floor,x,y) 的世界敌人在 world_enemies 的下标;无则返回 world_enemies.size()(批次 W.5)。
	//   ★ 退回(kCharLoop 撞明雷)与开战(onEvent 面前格)共用:两处都问"这格有没有明雷"。
	//   ⚠️ 线性扫(world_enemies 数量小,同项目对内容表线性扫的取向,见 findEncountArea)。
	std::size_t worldEnemyAt(std::int32_t floor, std::int32_t x, std::int32_t y);
	// kNpcSpawn:据 spawn_points 把世界态敌人补齐到各点的 count(不足则 spawnEnemy 生成 + 入池)。
	void spawnWorldEnemies();
	// kCharLoop 非玩家段:条数制摊还,本 tick 最多处理 max_this_tick 只(到期的游荡一步)。
	void wanderWorldEnemies(std::size_t max_this_tick);
	// 收视野内玩家会话(★ 不排除 self)—— 敌人广播用(敌人无会话,无 self 可排)。
	std::vector<SA::Net::ConnectionId> collectVisiblePlayers(std::int32_t floor, std::int32_t cx,
	                                                         std::int32_t cy) const;
	// 敌人视野广播(★ 单向:敌人无会话、不接收下行,只发给周围玩家;entity_type = ENTITY_ENEMY)。
	void broadcastEnemySpawn(const SA::Model::Enemy &e, std::uint64_t eid);
	void broadcastEnemyMove(const SA::Model::Enemy &e, std::uint64_t eid, std::int32_t ox,
	                        std::int32_t oy);
	void broadcastEnemyDespawn(std::int32_t floor, std::int32_t x, std::int32_t y, std::uint64_t eid);
	// 玩家移动 (ox,oy)→(p.x,p.y) 后,把视野**新进 / 离开**的世界敌人补 appear / disappear 给他。
	//   ★ 这是"玩家看敌人"那一半(broadcastMove 只做了"玩家看玩家")。
	void refreshEnemyView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
	                      std::int32_t ox, std::int32_t oy);

	// ── ExChangeMan 道具与宠物交付/奖励 (批次 W.10) ───────────────────
	std::int32_t countPlayerItems(const SA::Model::Player &p, std::int32_t item_id) const;
	std::int32_t countPlayerPets(const SA::Model::Player &p, std::int32_t pet_id, std::int32_t min_lvl) const;
	std::int32_t countFreeItemSlots(const SA::Model::Player &p) const;
	std::int32_t countFreePetSlots(const SA::Model::Player &p) const;
	void sendExChangeWindow(SA::Net::SessionId id, std::uint64_t npc_id,
	                        const std::string &raw_text, std::uint32_t buttons);
	bool checkExChangePreconditions(const SA::Model::Player &p, const ExChangeBlock &blk, int branch_idx, std::string &msg_out);
	void applyExChangeEffects(SA::Net::SessionId id, SA::Model::Player &p, const ExChangeBlock &blk, int branch_idx);

	// ── 瞬移与传送底层 (批次 W.6 / W.14: 移除旧 olink, 视野广播, 更新坐标, 挂接新 olink) ──
	void warpPlayer(SA::Net::SessionId id, std::int32_t dst_floor, std::int32_t dst_x, std::int32_t dst_y);
};

World::World(const SA::Platform::ServerConfig &config,
             SA::Platform::Clock &clock, SA::Platform::Logger &logger,
             SA::Platform::RandomSource &random,
             SA::Net::Transport &transport, SA::SessionStorage::Service *storage)
    : _impl(std::make_unique<Impl>(config, clock, logger, random, transport))
{
	_impl->storage = storage;
	transport.setEvents(this);
}

World::~World() = default;

std::int32_t World::Impl::countPlayerItems(const SA::Model::Player &p, std::int32_t item_id) const
{
	std::int32_t cnt = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (p.items[i].valid())
		{
			if (const auto *it = items.resolve(p.items[i]))
			{
				if (it->item_id == item_id)
				{
					cnt += std::max(1, it->current_pile);
				}
			}
		}
	}
	return cnt;
}

std::int32_t World::Impl::countPlayerPets(const SA::Model::Player &p, std::int32_t pet_id,
                                          std::int32_t min_lvl) const
{
	std::int32_t cnt = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (p.pets[i].valid())
		{
			if (const auto *pet = pets.resolve(p.pets[i]))
			{
				if (pet->pet_id == pet_id && pet->level >= min_lvl)
				{
					++cnt;
				}
			}
		}
	}
	return cnt;
}

std::int32_t World::Impl::countFreeItemSlots(const SA::Model::Player &p) const
{
	std::int32_t free_cnt = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (!p.items[i].valid())
			++free_cnt;
	}
	return free_cnt;
}

std::int32_t World::Impl::countFreePetSlots(const SA::Model::Player &p) const
{
	std::int32_t free_cnt = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (!p.pets[i].valid())
			++free_cnt;
	}
	return free_cnt;
}

void World::Impl::sendExChangeWindow(SA::Net::SessionId id, std::uint64_t npc_id,
                                     const std::string &raw_text, std::uint32_t buttons)
{
	auto it = conns.find(id);
	if (it == conns.end())
		return;

	SA::Domain::WindowOpen win{};
	win.window_id = ++next_window_id;
	win.kind = SA::Domain::WindowKind::WINDOW_KIND_MESSAGE;
	win.buttons = buttons;
	win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
	win.source.entity_id = static_cast<std::uint32_t>(npc_id);
	win.body_kind = SA::Domain::WindowOpen::BodyKind::MESSAGE;
	win.body.message.wide = false;

	std::size_t lstart = 0;
	while (lstart < raw_text.size() && win.body.message.lines.size() < 16)
	{
		const std::size_t nl = raw_text.find('\n', lstart);
		std::string line = (nl == std::string::npos) ? raw_text.substr(lstart)
		                                             : raw_text.substr(lstart, nl - lstart);
		if (line.size() > 255)
			line.resize(255);
		if (auto *slot = win.body.message.lines.push_back())
			slot->assign(line.data(), line.size());
		if (nl == std::string::npos)
			break;
		lstart = nl + 1;
	}
	if (win.body.message.lines.empty())
	{
		std::string line = raw_text;
		if (line.size() > 255)
			line.resize(255);
		if (auto *slot = win.body.message.lines.push_back())
			slot->assign(line.data(), line.size());
	}

	it->second.active_window_id = win.window_id;
	it->second.active_window_npc_id = static_cast<std::uint64_t>(npc_id);
	it->second.last_window_text = raw_text;
	sendTo(id, win);
}

void World::Impl::warpPlayer(SA::Net::SessionId id, std::int32_t dst_floor, std::int32_t dst_x, std::int32_t dst_y)
{
	const auto it = conns.find(id);
	SA::Model::Player *p = players.resolve(player_of_session.find(id));
	if (it == conns.end() || p == nullptr)
		return;

	Conn &c = it->second;
	c.walk_seq.clear(); // 清空剩余未走路径串 (CHAR_WORKWALKARRAY, char.c:4671)

	const std::int32_t ofloor = p->floor;
	const std::int32_t ox = p->x;
	const std::int32_t oy = p->y;

	// 1. 从旧格 olink 移除 (玩家刚从 ofloor 的 ox, oy 走来)
	auto *old_fl = getFloor(ofloor);
	const auto &old_map = old_fl ? old_fl->map : map;
	auto &old_olink = old_fl ? old_fl->olink : olink;
	if (old_map.inBounds(ox, oy))
	{
		const auto idx = old_map.index(ox, oy);
		if (idx < old_olink.size())
		{
			auto &oldcell = old_olink[idx];
			oldcell.erase(std::remove(oldcell.begin(), oldcell.end(), id),
			              oldcell.end());
		}
	}

	// 2. 旧视野广播 Disappear (旧视野内其他玩家看到 id 消失, id 看到旧视野玩家消失)
	const auto old_vis = collectVisible(ofloor, ox, oy, id);
	for (const SA::Net::ConnectionId b : old_vis)
	{
		SA::Domain::CharDisappear dis{};
		dis.entity_id = id;
		dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_PLAYER);
		sendTo(b, dis);
		SA::Domain::CharDisappear dis2{};
		dis2.entity_id = b;
		dis2.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_PLAYER);
		sendTo(id, dis2);
	}

	// 3. 更新玩家坐标
	p->floor = dst_floor;
	p->x = dst_x;
	p->y = dst_y;

	// 4. 新格 olink 挂接
	auto *new_fl = getFloor(dst_floor);
	const auto &new_map = new_fl ? new_fl->map : map;
	auto &new_olink = new_fl ? new_fl->olink : olink;
	if (new_map.inBounds(p->x, p->y))
	{
		const auto idx = new_map.index(p->x, p->y);
		if (idx < new_olink.size())
		{
			new_olink[idx].push_back(id);
		}
	}

	// 5. 新视野广播 Appear + 敌人/NPC 视野刷新 (跨图传送时旧坐标设为 -1000 以全量刷新)
	broadcastSpawn(id, *p);
	const std::int32_t ref_ox = (ofloor == dst_floor) ? ox : -1000;
	const std::int32_t ref_oy = (ofloor == dst_floor) ? oy : -1000;
	refreshEnemyView(id, *p, ref_ox, ref_oy);
	refreshNpcView(id, *p, ref_ox, ref_oy);

	// 6. 给玩家自身下发坐标同步 (CharMove)
	SA::Domain::CharMove self_mv{};
	self_mv.entity_id = id;
	self_mv.x = p->x;
	self_mv.y = p->y;
	self_mv.dir = static_cast<std::uint32_t>(p->dir);
	self_mv.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_PLAYER);
	sendTo(id, self_mv);
}

bool World::Impl::checkExChangePreconditions(const SA::Model::Player &p,
                                             const ExChangeBlock &blk, int branch_idx,
                                             std::string &msg_out)
{
	// 1. 石币不足门 (DelStone vs p.gold)
	if (blk.del_stone > 0 && p.gold < blk.del_stone)
	{
		msg_out = !blk.stone_less_msg.empty() ? blk.stone_less_msg : "石币不足。";
		return false;
	}

	// 2. 石币超限门 (GetStone vs maxHaveGold)
	if (blk.get_stone > 0 && (p.gold + blk.get_stone > maxHaveGold(0)))
	{
		if (!blk.stone_full_msg.empty())
		{
			msg_out = blk.stone_full_msg;
			return false;
		}
	}

	// 3. 背包容量门 (ItemFullCheck, 09 §4)
	if (!blk.get_item.empty())
	{
		const auto gets = parseExchangeItems(blk.get_item);
		std::int32_t get_slots = 0;
		for (const auto &g : gets)
			get_slots += g.count;

		const auto dels = resolveDelItems(blk, branch_idx);
		std::int32_t del_slots = 0;
		for (const auto &d : dels)
		{
			std::int32_t rem = d.count;
			for (std::size_t i = SA::Model::kStartItemArray;
			     i < SA::Model::kMaxItemHave && rem > 0; ++i)
			{
				if (p.items[i].valid())
				{
					if (const auto *item = items.resolve(p.items[i]))
					{
						if (item->item_id == d.item_id)
						{
							++del_slots;
							--rem;
						}
					}
				}
			}
		}

		const std::int32_t free_slots = countFreeItemSlots(p);
		if (free_slots + del_slots < get_slots)
		{
			msg_out = !blk.item_full_msg.empty() ? blk.item_full_msg : "道具栏已满。";
			return false;
		}
	}

	// 4. 宠物槽容量门 (PetFullCheck, 09 §4)
	if (!blk.get_pet.empty())
	{
		const auto gets = parseExchangePets(blk.get_pet);
		std::int32_t get_pet_slots = 0;
		for (const auto &g : gets)
			get_pet_slots += g.count;

		const auto dels = resolveDelPets(blk, branch_idx);
		std::int32_t del_pet_slots = 0;
		for (const auto &d : dels)
		{
			std::int32_t rem = d.count;
			for (std::size_t i = 0; i < SA::Model::kMaxPetHave && rem > 0; ++i)
			{
				if (p.pets[i].valid())
				{
					if (const auto *pet = pets.resolve(p.pets[i]))
					{
						if (pet->pet_id == d.pet_id)
						{
							++del_pet_slots;
							--rem;
						}
					}
				}
			}
		}

		const std::int32_t free_pet_slots = countFreePetSlots(p);
		if (free_pet_slots + del_pet_slots < get_pet_slots)
		{
			msg_out = !blk.pet_full_msg.empty() ? blk.pet_full_msg : "宠物栏已满。";
			return false;
		}
	}

	return true;
}

void World::Impl::applyExChangeEffects(SA::Net::SessionId id, SA::Model::Player &p,
                                       const ExChangeBlock &blk, int branch_idx)
{
	// ① 扣除石币 (必须走 delGold, 守卫 check_gold_writes)
	if (blk.del_stone > 0)
	{
		(void)delGold(p, GoldReason::kQuestFee, blk.del_stone, /*trans=*/0, /*corr=*/0, *this);
	}

	// ② 给予石币 (必须走 addGold, 守卫 check_gold_writes)
	if (blk.get_stone > 0)
	{
		(void)addGold(p, GoldReason::kQuestReward, blk.get_stone, /*trans=*/0, /*corr=*/0, *this);
	}

	// ③ 扣除道具
	const auto dels = resolveDelItems(blk, branch_idx);
	for (const auto &d : dels)
	{
		std::int32_t remaining = d.count;
		for (std::size_t i = SA::Model::kStartItemArray;
		     i < SA::Model::kMaxItemHave && remaining > 0; ++i)
		{
			if (p.items[i].valid())
			{
				auto *item = items.resolve(p.items[i]);
				if (item != nullptr && item->item_id == d.item_id)
				{
					if (item->current_pile > remaining)
					{
						item->current_pile -= remaining;
						remaining = 0;
					}
					else
					{
						remaining -= std::max(1, item->current_pile);
						const auto h = p.items[i];
						p.clearItemSlot(static_cast<int>(i));
						items.release(h);
					}
				}
			}
		}
	}

	// ④ 给予道具
	if (!blk.get_item.empty())
	{
		const auto gets = parseExchangeItems(blk.get_item);
		for (const auto &g : gets)
		{
			for (int c = 0; c < g.count; ++c)
			{
				SA::Model::Item new_item{};
				new_item.uid = ++next_window_id;
				new_item.item_id = g.item_id;
				new_item.current_pile = 1;
				new_item.use_pile_nums = 1;
				(void)giveItemIntoPlayer(p, new_item, items);
			}
		}
	}

	// ⑤ 扣除宠物
	const auto del_pets = resolveDelPets(blk, branch_idx);
	for (const auto &d : del_pets)
	{
		std::int32_t remaining = d.count;
		for (std::size_t i = 0; i < SA::Model::kMaxPetHave && remaining > 0; ++i)
		{
			if (p.pets[i].valid())
			{
				const auto *pet = pets.resolve(p.pets[i]);
				if (pet != nullptr && pet->pet_id == d.pet_id)
				{
					const auto h = p.pets[i];
					p.clearPetSlot(static_cast<int>(i));
					pets.release(h);
					--remaining;
				}
			}
		}
	}

	// ⑥ 给予宠物
	if (!blk.get_pet.empty())
	{
		const auto gets = parseExchangePets(blk.get_pet);
		for (const auto &g : gets)
		{
			for (int c = 0; c < g.count; ++c)
			{
				const int slot = p.findFreePetSlot();
				if (slot >= 0)
				{
					const SA::Model::EntityHandle ph = pets.allocate();
					if (ph.valid())
					{
						if (auto *pet_dst = pets.resolve(ph))
						{
							pet_dst->uid = ++next_window_id;
							pet_dst->pet_id = g.pet_id;
							pet_dst->level = 1;
							pet_dst->hp = 100;
							pet_dst->mp = 100;
							pet_dst->max_mp = 100;
							pet_dst->owner = player_of_session.find(id);
							p.pets[static_cast<std::size_t>(slot)] = ph;
						}
					}
				}
			}
		}
	}

	// ⑦ 旗标副作用
	if (!blk.end_set_flg.empty())
	{
		std::size_t start = 0;
		while (start < blk.end_set_flg.size())
		{
			const std::size_t comma = blk.end_set_flg.find(',', start);
			const std::string s_flag =
			    (comma == std::string::npos) ? blk.end_set_flg.substr(start)
			                                 : blk.end_set_flg.substr(start, comma - start);
			const int f = std::atoi(s_flag.c_str());
			p.setEndEvent(f);
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
	}
	if (!blk.clean_flg.empty())
	{
		std::size_t start = 0;
		while (start < blk.clean_flg.size())
		{
			const std::size_t comma = blk.clean_flg.find(',', start);
			const std::string s_flag =
			    (comma == std::string::npos) ? blk.clean_flg.substr(start)
			                                 : blk.clean_flg.substr(start, comma - start);
			const int f = std::atoi(s_flag.c_str());
			p.clearNowEvent(f);
			p.clearEndEvent(f);
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
	}
	if (blk.event_no != -1)
	{
		if (!blk.end_set_flg.empty())
			p.clearNowEvent(blk.event_no);
		else
			p.setNowEvent(blk.event_no);
	}
}

namespace
{

// ── 走路辅助(批次 W.1)────────────────────────────────────────────────
//
// 走路间隔:原版 CHAR_walk_check(char.c:4590)判 `time_diff_us >= walksendinterval*100`,
//   csa8.0 setup.cf `walkinterval=2500` ⇒ 2500 × 100us = 250ms 一格。
constexpr SA::Platform::Millis kWalkIntervalMs = 250;

// 方向 0-7 → 坐标增量。★ 照抄 CHAR_dxdy[8](char.c:2325):北起顺时针,含四斜向。
struct DirDelta
{
	std::int32_t dx;
	std::int32_t dy;
};
constexpr DirDelta kDirDelta[8] = {
    {0, -1},
    {1, -1},
    {1, 0},
    {1, 1},
    {0, 1},
    {-1, 1},
    {-1, 0},
    {-1, -1},
};

// 方向字符解码 —— 移植 CHAR_ctodirmode(char_walk.c:1398):
//   小写 'a'-'h' ⇒ 移动(is_turn=false);其余(大写 'A'-'H')⇒ 转身;dir = tolower-'a'。
// 返回 false = 非法字符(dir 越界),调用方跳过该字符。
bool decodeDirChar(char moji, std::uint8_t &dir, bool &is_turn)
{
	is_turn = !(moji >= 'a' && moji <= 'h'); // 小写 a-h 才是移动(:1401)
	const char lower =
	    (moji >= 'A' && moji <= 'Z') ? static_cast<char>(moji - 'A' + 'a') : moji;
	const int d = lower - 'a';
	if (d < 0 || d > 7)
		return false;
	dir = static_cast<std::uint8_t>(d);
	return true;
}

// 走一步 —— 移植 CHAR_walk_move(char_walk.c:195)的**地图碰撞 + 坐标更新**核心。
// ⚠️ 本批不做(各有归属):目标格对象碰撞(notover,:350,需 olink)· 进出格 on/off 事件
//    (RunCharOverlapEvent,:281-449,依赖 NPC/Lua)· 视野广播(:469,视野批次)· 遇敌(:585,遇敌批次)。
// 返回 true = 位置真的变了(供视野批次决定是否广播)。
bool walkStep(SA::Model::Player &p, const GridMap &map, const TileAttrTable &attr,
              std::uint8_t dir, bool is_turn)
{
	// 转向或移动都先落朝向(原版 :218/:250/:259 一律 CHAR_setInt(CHAR_DIR,dir))。
	p.dir = dir;
	if (is_turn)
		return false; // 大写 ⇒ 只转身,不移动(ctodirmode mode==1)

	const std::int32_t fx = p.x + kDirDelta[dir].dx;
	const std::int32_t fy = p.y + kDirDelta[dir].dy;

	// 直线:看目标格(:258)。斜向:额外看 x/y 两分量,墙角不穿(:263-278)。
	if (!mapWalkable(map, attr, fx, fy))
		return false; // 撞墙,朝向已落(:259)
	if (kDirDelta[dir].dx != 0 && kDirDelta[dir].dy != 0)
	{
		if (!mapWalkable(map, attr, p.x + kDirDelta[dir].dx, p.y) ||
		    !mapWalkable(map, attr, p.x, p.y + kDirDelta[dir].dy))
			return false; // 墙角
	}
	p.x = fx;
	p.y = fy;
	return true;
}

// ── 视野广播辅助(里程碑②)────────────────────────────────────────────────
//
// 视野常量。★ 决策取 23(10 §9 决策1 / 05 §5.1),⚠️ 而展开视图 unifdef_80 的
//   CHAR_DEFAULTSEESIZ 是 **20**(char_base.h:58,8.5 血统 —— unifdef 不改 #define 字面值);
//   8.0 血统源码取 23、与数据基线一致,但 **#define 不进符号表 ⇒ 无二进制证据**
//   (00 §10.2 六项不可判定之一)⇒ 取 23 是裁定不是观测。
// 扫格公式 (2*(c/2)+1)² = 23² = 529(10 §5.1;c/2 是整数除,c 为奇数时 ≠ (c+1)²)。
constexpr std::int32_t kSeeSize = 23;
constexpr std::int32_t kSeeRadius = kSeeSize / 2; // 11

bool visContains(const std::vector<SA::Net::ConnectionId> &v, SA::Net::ConnectionId c)
{
	return std::find(v.begin(), v.end(), c) != v.end();
}

// (tx,ty) 是否落在以 (cx,cy) 为心的 529 格视野框内(与 collectVisible 的方框一致)。批次 W.3。
bool inSee(std::int32_t cx, std::int32_t cy, std::int32_t tx, std::int32_t ty) noexcept
{
	return tx >= cx - kSeeRadius && tx <= cx + kSeeRadius && ty >= cy - kSeeRadius &&
	       ty <= cy + kSeeRadius;
}

SA::Domain::CharAppear makeAppear(SA::Net::ConnectionId who, const SA::Model::Player &p)
{
	SA::Domain::CharAppear a{};
	a.entity_id = who;
	a.floor = p.floor;
	a.x = p.x;
	a.y = p.y;
	a.dir = static_cast<std::uint32_t>(p.dir);
	a.image = p.image;
	return a;
}

// 敌人 EntityHandle → uint64(视野 entity_id / 观察面)。★ (index<<32)|generation:
//   与玩家的 ConnectionId 是**两个独立 id 空间**,数值会撞 ⇒ 靠 CharAppear.entity_type 区分
//   (world_map.proto:客户端按 (entity_type, entity_id) 二元组跟踪对象)。
constexpr std::uint64_t encodeHandle(SA::Model::EntityHandle h) noexcept
{
	return (static_cast<std::uint64_t>(h.index) << 32) |
	       static_cast<std::uint64_t>(h.generation);
}

// 从世界态敌人造 CharAppear(entity_type = ENTITY_ENEMY,带真图号 base_image;玩家版 image 恒 0)。
SA::Domain::CharAppear makeEnemyAppear(std::uint64_t eid, const SA::Model::Enemy &e)
{
	SA::Domain::CharAppear a{};
	a.entity_id = eid;
	a.floor = e.floor;
	a.x = e.x;
	a.y = e.y;
	a.dir = static_cast<std::uint32_t>(e.dir);
	a.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	a.image = e.base_image;
	return a;
}

} // namespace

// ══ World::Impl 的视野广播方法(里程碑②)══════════════════════════════════

// 扫 (cx,cy) 周围 529 格的 olink,收集其中的玩家会话(除 self)。
//   ★ 10 §5.3 决策5:先扫格 + 聚合,不做订阅(视野连续变化,订阅维护成本可能更高,
//     留到有实测数据之后)。
std::vector<SA::Net::ConnectionId> World::Impl::collectVisible(std::int32_t floor, std::int32_t cx, std::int32_t cy,
                                                               SA::Net::ConnectionId self) const
{
	std::vector<SA::Net::ConnectionId> out;
	const auto *fl = getFloor(floor);
	const auto &m = fl ? fl->map : map;
	const auto &ol = fl ? fl->olink : olink;
	for (std::int32_t j = cy - kSeeRadius; j <= cy + kSeeRadius; ++j)
		for (std::int32_t i = cx - kSeeRadius; i <= cx + kSeeRadius; ++i)
		{
			if (!m.inBounds(i, j))
				continue;
			const auto idx = m.index(i, j);
			if (idx >= ol.size())
				continue;
			for (const SA::Net::ConnectionId c : ol[idx])
				if (c != self)
					out.push_back(c);
		}
	return out;
}

// 给一个会话 push 一条下行消息(找不到 / 无 session 则跳过;字节聚合由 kOutboundFlush 统一发)。
template <typename M>
void World::Impl::sendTo(SA::Net::ConnectionId to, const M &msg)
{
	const auto it = conns.find(to);
	if (it == conns.end() || it->second.session == nullptr)
		return;
	(void)it->second.session->push(msg, it->second.outbound);
}

// 某会话进入视野 ⇒ 双向 CharAppear(视野对称:我看到你出现,你也看到我出现,char.c:4100)。
void World::Impl::appearBetween(SA::Net::ConnectionId a, const SA::Model::Player &pa,
                                SA::Net::ConnectionId b)
{
	const SA::Model::Player *pb = players.resolve(player_of_session.find(b));
	if (pb == nullptr)
		return;
	sendTo(b, makeAppear(a, pa));  // b 看到 a 出现
	sendTo(a, makeAppear(b, *pb)); // a 看到 b 出现
}

// A 移动 (ox,oy)→(p.x,p.y) 后的视野广播(扫格 diff,10 §5.2)。⚠️ olink 须**已更新到新位置**。
void World::Impl::broadcastMove(SA::Net::ConnectionId mover, std::int32_t ox, std::int32_t oy,
                                const SA::Model::Player &p)
{
	const auto old_vis = collectVisible(p.floor, ox, oy, mover);
	const auto new_vis = collectVisible(p.floor, p.x, p.y, mover);

	SA::Domain::CharMove mv{};
	mv.entity_id = mover;
	mv.x = p.x;
	mv.y = p.y;
	mv.dir = static_cast<std::uint32_t>(p.dir);

	for (const SA::Net::ConnectionId b : new_vis)
	{
		if (visContains(old_vis, b))
			sendTo(b, mv); // 一直可见 ⇒ b 看到 a 移动
		else
			appearBetween(mover, p, b); // 新进入 ⇒ 双向出现
	}
	for (const SA::Net::ConnectionId b : old_vis)
	{
		if (visContains(new_vis, b))
			continue;
		SA::Domain::CharDisappear dis{}; // 离开 ⇒ 双向消失
		dis.entity_id = mover;
		sendTo(b, dis); // b 看到 a 消失
		SA::Domain::CharDisappear dis2{};
		dis2.entity_id = b;
		sendTo(mover, dis2); // a 看到 b 消失
	}
}

// 出生 / 进图:与视野内每个玩家双向 CharAppear(原版进图 CHAR_sendCToArroundCharacter)。
void World::Impl::broadcastSpawn(SA::Net::ConnectionId who, const SA::Model::Player &p)
{
	for (const SA::Net::ConnectionId b : collectVisible(p.floor, p.x, p.y, who))
		appearBetween(who, p, b);
}

// 离场 / 断线:给视野内每个玩家发 CharDisappear(who)。⚠️ 须在 olink 移除**之前**调(要 who 的位置)。
void World::Impl::broadcastDespawn(SA::Net::ConnectionId who, std::int32_t floor, std::int32_t x, std::int32_t y)
{
	SA::Domain::CharDisappear dis{};
	dis.entity_id = who;
	for (const SA::Net::ConnectionId b : collectVisible(floor, x, y, who))
		sendTo(b, dis);
}

// ══ 世界敌人:视野 / 生成 / 游荡(批次 W.2 / W.3)══════════════════════════════

// 收视野内玩家会话(★ 不排除 self)。敌人无会话,没有"自己"要排 —— 与 collectVisible 的唯一区别。
std::vector<SA::Net::ConnectionId> World::Impl::collectVisiblePlayers(std::int32_t floor, std::int32_t cx,
                                                                      std::int32_t cy) const
{
	std::vector<SA::Net::ConnectionId> out;
	const auto *fl = getFloor(floor);
	const auto &m = fl ? fl->map : map;
	const auto &ol = fl ? fl->olink : olink;
	for (std::int32_t j = cy - kSeeRadius; j <= cy + kSeeRadius; ++j)
		for (std::int32_t i = cx - kSeeRadius; i <= cx + kSeeRadius; ++i)
		{
			if (!m.inBounds(i, j))
				continue;
			const auto idx = m.index(i, j);
			if (idx >= ol.size())
				continue;
			for (const SA::Net::ConnectionId c : ol[idx])
				out.push_back(c);
		}
	return out;
}

// 敌人进入世界 ⇒ 给视野内每个玩家发 CharAppear(★ 单向)。
void World::Impl::broadcastEnemySpawn(const SA::Model::Enemy &e, std::uint64_t eid)
{
	const SA::Domain::CharAppear a = makeEnemyAppear(eid, e);
	for (const SA::Net::ConnectionId b : collectVisiblePlayers(e.floor, e.x, e.y))
		sendTo(b, a);
}

// 敌人移动一步 ⇒ 扫格 diff(同 broadcastMove 但单向:一直可见→CharMove / 新进→CharAppear / 离开→CharDisappear)。
void World::Impl::broadcastEnemyMove(const SA::Model::Enemy &e, std::uint64_t eid,
                                     std::int32_t ox, std::int32_t oy)
{
	const auto old_vis = collectVisiblePlayers(e.floor, ox, oy);
	const auto new_vis = collectVisiblePlayers(e.floor, e.x, e.y);

	SA::Domain::CharMove mv{};
	mv.entity_id = eid;
	mv.x = e.x;
	mv.y = e.y;
	mv.dir = static_cast<std::uint32_t>(e.dir);
	mv.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	const SA::Domain::CharAppear ap = makeEnemyAppear(eid, e);

	for (const SA::Net::ConnectionId b : new_vis)
	{
		if (visContains(old_vis, b))
			sendTo(b, mv); // 一直可见 ⇒ 移动
		else
			sendTo(b, ap); // 新进入视野 ⇒ 出现
	}
	SA::Domain::CharDisappear dis{};
	dis.entity_id = eid;
	dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	for (const SA::Net::ConnectionId b : old_vis)
		if (!visContains(new_vis, b))
			sendTo(b, dis); // 离开视野 ⇒ 消失
}

// 敌人离开世界(被拉进战斗 / 死亡)⇒ 给视野内每个玩家发 CharDisappear。⚠️ 须在改位置**之前**调。
void World::Impl::broadcastEnemyDespawn(std::int32_t floor, std::int32_t x, std::int32_t y, std::uint64_t eid)
{
	SA::Domain::CharDisappear dis{};
	dis.entity_id = eid;
	dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	for (const SA::Net::ConnectionId b : collectVisiblePlayers(floor, x, y))
		sendTo(b, dis);
}

// 玩家从 (ox,oy) 走到 (p.x,p.y) 后,补发**世界敌人**的 appear / disappear(★ 玩家看敌人那一半)。
//   对每只世界敌人:旧位置可见→新不可见 ⇒ CharDisappear;旧不可见→新可见 ⇒ CharAppear;
//   两者都可见 ⇒ 不发(敌人自身移动由 broadcastEnemyMove 覆盖)。
void World::Impl::refreshEnemyView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
                                   std::int32_t ox, std::int32_t oy)
{
	for (const WorldEnemy &we : world_enemies)
	{
		const SA::Model::Enemy *e = enemies.resolve(we.handle);
		if (e == nullptr || e->floor != p.floor)
			continue;
		const bool saw = inSee(ox, oy, e->x, e->y);
		const bool sees = inSee(p.x, p.y, e->x, e->y);
		if (sees == saw)
			continue;
		const std::uint64_t eid = encodeHandle(we.handle);
		if (sees)
			sendTo(viewer, makeEnemyAppear(eid, *e));
		else
		{
			SA::Domain::CharDisappear dis{};
			dis.entity_id = eid;
			dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
			sendTo(viewer, dis);
		}
	}
}

// 玩家从 (ox,oy) 走到 (p.x,p.y) 后,补发世界 NPC 的 appear / disappear(批次 W.7)。
void World::Impl::refreshNpcView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
                                 std::int32_t ox, std::int32_t oy)
{
	for (const NpcEntity &npc : npc_entities)
	{
		if (npc.floor != p.floor)
			continue;
		const bool saw = inSee(ox, oy, npc.x, npc.y);
		const bool sees = inSee(p.x, p.y, npc.x, npc.y);
		if (sees == saw)
			continue;
		if (sees)
		{
			SA::Domain::CharAppear a{};
			a.entity_id = npc.id;
			a.floor = npc.floor;
			a.x = npc.x;
			a.y = npc.y;
			a.dir = static_cast<std::uint32_t>(npc.dir);
			a.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
			a.image = npc.image;
			sendTo(viewer, a);
		}
		else
		{
			SA::Domain::CharDisappear dis{};
			dis.entity_id = npc.id;
			dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
			sendTo(viewer, dis);
		}
	}
}

// 检查是否有在线玩家当前正在与该 NPC 打开窗口对话 (批次 W.11)
bool World::Impl::isNpcEngagedInDialog(std::uint64_t npc_id) const
{
	for (const auto &kv : conns)
	{
		if (kv.second.active_window_id > 0 && kv.second.active_window_npc_id == npc_id)
			return true;
	}
	return false;
}

// NPC 移动一步广播 (批次 W.11, 视野扫格 diff: 一直可见→CharMove / 新进→CharAppear / 离开→CharDisappear)
void World::Impl::broadcastNpcMove(const NpcEntity &npc, std::int32_t ox, std::int32_t oy)
{
	const auto old_vis = collectVisiblePlayers(npc.floor, ox, oy);
	const auto new_vis = collectVisiblePlayers(npc.floor, npc.x, npc.y);

	SA::Domain::CharMove mv{};
	mv.entity_id = npc.id;
	mv.x = npc.x;
	mv.y = npc.y;
	mv.dir = static_cast<std::uint32_t>(npc.dir);
	mv.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);

	SA::Domain::CharAppear ap{};
	ap.entity_id = npc.id;
	ap.floor = npc.floor;
	ap.x = npc.x;
	ap.y = npc.y;
	ap.dir = static_cast<std::uint32_t>(npc.dir);
	ap.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ap.image = npc.image;

	for (const SA::Net::ConnectionId b : new_vis)
	{
		if (visContains(old_vis, b))
			sendTo(b, mv); // 一直可见 ⇒ 移动或转身
		else
			sendTo(b, ap); // 新进入视野 ⇒ 出现
	}
	SA::Domain::CharDisappear dis{};
	dis.entity_id = npc.id;
	dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	for (const SA::Net::ConnectionId b : old_vis)
	{
		if (!visContains(new_vis, b))
			sendTo(b, dis); // 离开视野 ⇒ 消失
	}
}

// kCharLoop 非玩家段: 世界 NPC 巡逻与漫游游荡 (条数制摊还, 批次 W.11)
void World::Impl::wanderNpcs(std::size_t max_this_tick)
{
	const std::size_t n = npc_entities.size();
	if (n == 0 || max_this_tick == 0)
		return;

	std::size_t moved = 0;
	std::size_t scanned = 0;

	while (scanned < n && moved < max_this_tick)
	{
		if (npc_charloop_cursor >= n)
			npc_charloop_cursor = 0;

		const std::size_t idx = npc_charloop_cursor++;
		++scanned;

		NpcEntity &npc = npc_entities[idx];
		if (npc.wander_interval_ms <= 0)
			continue;
		if (now_ms < npc.next_wander_at_ms)
			continue;

		// 对话锁定: 若有玩家正在与该 NPC 打开窗口对话, 本节拍不移动
		if (isNpcEngagedInDialog(npc.id))
		{
			npc.next_wander_at_ms = now_ms + npc.wander_interval_ms;
			continue;
		}

		std::int32_t dir = -1;
		std::int32_t nx = npc.x;
		std::int32_t ny = npc.y;

		if (!npc.route.empty())
		{
			// ── 模式 1: 巡逻路线模式 (沿着 route 路点逐步行进) ─────────
			if (npc.route_index >= npc.route.size())
				npc.route_index = 0;

			const NpcPoint &target = npc.route[npc.route_index];
			if (npc.x == target.x && npc.y == target.y)
			{
				npc.route_index = (npc.route_index + 1) % npc.route.size();
			}

			const NpcPoint &next_target = npc.route[npc.route_index];
			std::int32_t difx = next_target.x - npc.x;
			std::int32_t dify = next_target.y - npc.y;
			if (difx < 0)
				difx = -1;
			else if (difx > 0)
				difx = 1;
			if (dify < 0)
				dify = -1;
			else if (dify > 0)
				dify = 1;

			// 移植原版 NPC_Util_getDirFromTwoPoint dirtable[dify+1][difx+1]
			static constexpr int dirtable[3][3] = {
			    {7, 0, 1},
			    {6, -1, 2},
			    {5, 4, 3},
			};
			dir = dirtable[dify + 1][difx + 1];
			if (dir >= 0)
			{
				nx = npc.x + kDirDelta[dir].dx;
				ny = npc.y + kDirDelta[dir].dy;
			}
		}
		else if (npc.wander_radius > 0)
		{
			// ── 模式 2: 自由漫游模式 (以 born_x, born_y 为中心随机游荡) ───
			dir = static_cast<std::int32_t>(world_rng.randMod(8));
			nx = npc.x + kDirDelta[dir].dx;
			ny = npc.y + kDirDelta[dir].dy;

			std::int32_t adx = nx - npc.born_x;
			adx = adx < 0 ? -adx : adx;
			std::int32_t ady = ny - npc.born_y;
			ady = ady < 0 ? -ady : ady;

			if (adx > npc.wander_radius || ady > npc.wander_radius)
			{
				// 超出游荡半径，不位移但可转向
				dir = -1;
			}
		}

		if (dir >= 0)
		{
			npc.dir = static_cast<std::uint8_t>(dir);

			// ── 通行与碰撞守卫 ───────────────────────────────────────
			bool blocked = false;

			// ① 地图通行门 (含斜向墙角保护)
			const auto *npc_fl = getFloor(npc.floor);
			const auto &npc_map = npc_fl ? npc_fl->map : map;
			if (!mapWalkable(npc_map, map_attr, nx, ny))
			{
				blocked = true;
			}
			else if (kDirDelta[dir].dx != 0 && kDirDelta[dir].dy != 0)
			{
				if (!mapWalkable(npc_map, map_attr, npc.x + kDirDelta[dir].dx, npc.y) ||
				    !mapWalkable(npc_map, map_attr, npc.x, npc.y + kDirDelta[dir].dy))
				{
					blocked = true;
				}
			}

			// ② 实体碰撞门 1: 撞其他 NPC (CHAR_ISOVERED=0)
			if (!blocked)
			{
				const std::size_t other_npc = npcAt(npc.floor, nx, ny);
				if (other_npc != npc_entities.size() && other_npc != idx)
				{
					blocked = true;
				}
			}

			// ③ 实体碰撞门 2: 撞玩家实体 (CHAR_ISOVERED=0)
			if (!blocked)
			{
				for (const auto &kv : conns)
				{
					const auto *p = players.resolve(player_of_session.find(kv.first));
					if (p != nullptr && p->floor == npc.floor && p->x == nx && p->y == ny)
					{
						blocked = true;
						break;
					}
				}
			}

			// ④ 实体碰撞门 3: 撞世界敌人 (明雷)
			if (!blocked && worldEnemyAt(npc.floor, nx, ny) != world_enemies.size())
			{
				blocked = true;
			}

			if (!blocked)
			{
				const std::int32_t ox = npc.x;
				const std::int32_t oy = npc.y;
				npc.x = nx;
				npc.y = ny;
				broadcastNpcMove(npc, ox, oy);
			}
			else
			{
				// 阻挡未位移: 原地更新朝向并向视野内广播转身
				broadcastNpcMove(npc, npc.x, npc.y);
			}
		}

		npc.next_wander_at_ms = now_ms + npc.wander_interval_ms;
		++moved;
	}
}

// (floor,x,y) 上的世界敌人在 world_enemies 的下标;无则返回 world_enemies.size()(批次 W.5)。
//   ★ 撞明雷退回(kCharLoop)与明雷开战(onEvent 面前格)共用这一个「这格有没有明雷」查询。
std::size_t World::Impl::worldEnemyAt(std::int32_t floor, std::int32_t x, std::int32_t y)
{
	for (std::size_t i = 0; i < world_enemies.size(); ++i)
	{
		const SA::Model::Enemy *e = enemies.resolve(world_enemies[i].handle);
		if (e != nullptr && e->floor == floor && e->x == x && e->y == y)
			return i;
	}
	return world_enemies.size();
}

// kNpcSpawn:据刷怪点把世界态敌人补齐到各点 count。★ 不阻塞 D6 的注入式刷怪(见 Api.h SpawnPoint)。
void World::Impl::spawnWorldEnemies()
{
	for (std::size_t pi = 0; pi < spawn_points.size(); ++pi)
	{
		const SpawnPoint &sp = spawn_points[pi];
		const std::size_t target = sp.count < 0 ? 0 : static_cast<std::size_t>(sp.count);
		std::size_t alive = 0;
		for (const WorldEnemy &we : world_enemies)
			if (we.spawn_point == pi)
				++alive;
		while (alive < target)
		{
			// 敌人来源:enemy_id → 敌人表行 → 模板行(单一真源,复用 M.7 find + M.4b spawnEnemy)。
			const std::int32_t erow = findEnemyEncounter(encounters, sp.enemy_id);
			if (erow < 0)
			{
				logger.log(SA::Platform::LogLevel::kWarn,
				           SA::Platform::LogEvent::kWorldEnemySpawnFailed,
				           {{"enemy_id", static_cast<std::uint64_t>(sp.enemy_id)},
				            {"reason", std::string_view("no_encounter")}});
				break; // 敌人表查不到(多半没 loadEncounterTables)⇒ 该点整个刷不出
			}
			const EnemyEncounter &enc = encounters[static_cast<std::size_t>(erow)];
			const std::int32_t trow = findEnemyTemplate(enemy_templates, enc.temp_no);
			if (trow < 0)
			{
				logger.log(SA::Platform::LogLevel::kWarn,
				           SA::Platform::LogEvent::kWorldEnemySpawnFailed,
				           {{"enemy_id", static_cast<std::uint64_t>(sp.enemy_id)},
				            {"temp_no", static_cast<std::uint64_t>(enc.temp_no)},
				            {"reason", std::string_view("no_template")}});
				break;
			}
			const EnemyTemplate &tmpl = enemy_templates[static_cast<std::size_t>(trow)];
			const SA::Model::EntityHandle eh = enemies.allocate();
			if (!eh.valid())
			{
				logger.log(SA::Platform::LogLevel::kError,
				           SA::Platform::LogEvent::kEntityPoolExhausted,
				           {{"pool", std::string_view("enemy")},
				            {"capacity", static_cast<std::uint64_t>(kMaxEnemies)}});
				break;
			}
			SA::Model::Enemy *e = enemies.resolve(eh);
			if (e == nullptr)
			{
				(void)enemies.release(eh);
				break;
			}
			// ★ 生成用**世界 rng**(世界态敌人不在战斗内;同遇敌链)。⚠️ 消耗 world_rng ⇒
			//   刷怪时机影响遇敌骰子 / 选怪序列(原版刷怪也在全局 rand;可回放前提是刷怪调用序重现)。
			*e = spawnEnemy(tmpl, enc, sp.level, world_rng, rules_config);
			e->floor = sp.floor;
			e->x = sp.x;
			e->y = sp.y;
			e->dir = 0;
			e->next_wander_at_ms = now_ms + sp.wander_interval_ms;
			world_enemies.push_back({eh, pi});
			broadcastEnemySpawn(*e, encodeHandle(eh));
			logger.log(SA::Platform::LogLevel::kDebug,
			           SA::Platform::LogEvent::kWorldEnemySpawned,
			           {{"enemy_id", static_cast<std::uint64_t>(sp.enemy_id)},
			            {"floor", static_cast<std::uint64_t>(sp.floor)},
			            {"x", static_cast<std::uint64_t>(sp.x)},
			            {"y", static_cast<std::uint64_t>(sp.y)}});
			++alive;
		}
	}
}

// kCharLoop 非玩家段:**条数制**摊还(原 CHAR_Loop 的 #else 分支,`_CHAR_LOOP_TIME` 8.0 关)。
//   从 charloop_cursor 起,本 tick 最多**游荡** max_this_tick 只(对应原版 movecnt >= EnemyMoveNum),
//   最多**检查** world_enemies.size() 只(对应原版 for 的迭代上限,防没一个到期时空转),游标记位下 tick 续。
void World::Impl::wanderWorldEnemies(std::size_t max_this_tick)
{
	const std::size_t n = world_enemies.size();
	if (n == 0 || max_this_tick == 0)
		return;
	std::size_t moved = 0;   // 真跑了 AI 的只数(对应原版 movecnt:节拍到期即计,不论走没走成)
	std::size_t scanned = 0; // 检查的只数(对应原版 for 迭代上限,防空转)
	while (scanned < n && moved < max_this_tick)
	{
		if (charloop_cursor >= n)
			charloop_cursor = 0; // 绕回(原版 charcnt >= charnum ⇒ playernum)
		const WorldEnemy we = world_enemies[charloop_cursor];
		++charloop_cursor;
		++scanned;
		SA::Model::Enemy *e = enemies.resolve(we.handle);
		if (e == nullptr)
			continue; // 悬空(世界态理论上不会;守零成本)
		if (now_ms < e->next_wander_at_ms)
			continue; // 未到游荡节拍(对应 CHAR_callLoop 返回 FALSE ⇒ movecnt 不增)
		// 到期 ⇒ 游荡一步(随机方向 + 通行门 + 刷怪点半径门)。
		const SpawnPoint &sp = spawn_points[we.spawn_point];
		const std::uint8_t dir = static_cast<std::uint8_t>(world_rng.randMod(8));
		const std::int32_t nx = e->x + kDirDelta[dir].dx;
		const std::int32_t ny = e->y + kDirDelta[dir].dy;
		e->dir = dir; // 面向选中方向(即使没走成 = 转身,同原版 ctodirmode 大写转身)
		std::int32_t adx = nx - sp.x;
		adx = adx < 0 ? -adx : adx;
		std::int32_t ady = ny - sp.y;
		ady = ady < 0 ? -ady : ady;
		const auto *e_fl = getFloor(e->floor);
		const auto &e_map = e_fl ? e_fl->map : map;
		const bool blocked_or_far =
		    !mapWalkable(e_map, map_attr, nx, ny) ||
		    (sp.wander_radius >= 0 && (adx > sp.wander_radius || ady > sp.wander_radius));
		if (!blocked_or_far)
		{
			const std::int32_t ox = e->x;
			const std::int32_t oy = e->y;
			e->x = nx;
			e->y = ny;
			broadcastEnemyMove(*e, encodeHandle(we.handle), ox, oy);
		}
		// ★ 无论走没走成,节拍到了就顺延(对应原版 loopfunc 执行后记 now)⇒ moved 计数(movecnt)。
		e->next_wander_at_ms = now_ms + sp.wander_interval_ms;
		++moved;
	}
}

// ══ tick(01 §3.1)═══════════════════════════════════════════════
void World::tick()
{
	Impl &s = *_impl;
	if (s.stopped)
		return;
	++s.ticks;

	// ── 1. 时钟推进 ──
	// ★ 统一时钟源、单调时钟。整个 tick 内**只取一次** ——
	//   同一 tick 里两处取到不同的"现在"会让节拍判断出现自相矛盾的结果。
	s.now_ms = s.clock.nowMs();

	// ── 2. 网络入站 ──
	// 从传输层取已到达的字节,派发到会话。★ 不阻塞(01 §2)。
	s.transport.poll();
	processStorage();

	// ── 3. NPC 生成(批次 W.2)──
	//   据刷怪点把世界态敌人补齐到各点 count(默认无刷怪点 ⇒ 空操作,现有用例不受影响)。
	//   ★ 最小切法(不阻塞 D6 脚本层);真玩法接脚本层后由脚本产出刷怪参数,见 Api.h SpawnPoint。
	s.spawnWorldEnemies();

	// ── 4. 战斗推进 ──  ★ 受节拍层控制,不等于 tick 频率(01 §3.2)
	{
		std::vector<BattleId> finished;
		for (auto &kv : s.battles)
		{
			BattleInstance &b = kv.second;
			if (b.stats.finished)
				continue;
			if (s.now_ms < b.next_turn_at_ms)
				continue;

			// 已有回合契约：实际玩家还在 C_WAIT 时不能消耗下一回合。
			// SSRC80 BATTLE_CommandWait (3021–3090)、TimeOutCheck (3881–3919)。
			// demo 的无人输入演示仍显式隔离；未加入会话的测试战场没有输入收集者。
			// ★ 集气槽的合成指令注入必须先于等待判定:集气单位无需新指令即自动
			//   行动(原版 IsCharge 豁免),不注入会让战斗卡到指令超时(批次 B2b)。
			injectChargeCommands(b);
			if (!b.demo)
			{
				std::vector<SA::Net::SessionId> waiting;
				for (auto sid : b.members)
				{
					const auto slot = b.slot_of.at(sid);
					const auto &unit = b.field.at(slot);
					if (unit.occupied && !unit.dead && unit.hp > 0 && !b.commands.present[slot])
						waiting.push_back(sid);
				}
				if (!waiting.empty())
				{
					const auto now_sec = s.now_ms / 1000;
					const bool timeout = now_sec > b.started_sec + 3600 ||
					                     (b.command_deadline_sec > 0 && now_sec > b.command_deadline_sec);
					if (!timeout)
						continue;
					syncPetState(b, s.pets);
					for (auto sid : waiting)
					{
						const auto slot = b.slot_of.at(sid);
						deliverPlayerProfit(b, slot, s.players, s.items);
						b.field.at(slot).occupied = false;
						if (slot % SA::Rules::kSideOffset < SA::Rules::kBattlePlayerMax)
						{
							exitPetFromField(b.field, slot);
							b.pet_of_slot[slot + SA::Rules::kBattlePlayerMax] = SA::Model::kNullHandle;
						}
						b.player_of_slot[slot] = SA::Model::kNullHandle;
						b.slot_of.erase(sid);
						b.members.erase(std::remove(b.members.begin(), b.members.end(), sid), b.members.end());
						SA::Domain::BattleLeave leave{};
						leave.battle_id = b.id;
						leave.reason = 1;
						if (auto conn = s.conns.find(sid); conn != s.conns.end() && conn->second.session)
							(void)conn->second.session->push(leave, conn->second.outbound);
					}
					s.pushBattleSnapshot(b);
					if (sideWipedOut(b.field, false))
					{
						b.stats.finished = true;
						finished.push_back(b.id);
						continue;
					}
				}
			}

			// ★ 敌方 AI 先填指令(见 FillEnemyCommands 卷首:这是 battle.h 指定的分工)。
			fillEnemyCommands(b.field, b.commands);

			// 魔法状态的回合推进(批次 B3b):原版 BATTLE_MagicStatusSeq 在每个单位
			// 的行动位跑(battle.c:7077);本实现每回合在行动循环前统一跑一次,
			// 时点差异见 tickMagicStatus 卷首的登记差异。
			tickMagicStatus(b);

			WorldWriteContext wctx;
			wctx.battle = &b;
			wctx.players = &s.players;
			wctx.pets = &s.pets;
			wctx.player_of_slot = &b.player_of_slot;
			wctx.enemies = &s.enemies;
			wctx.enemy_of_slot = &b.enemy_of_slot;
			wctx.items = &s.items;
			wctx.logger = &s.logger;
			// 排序一次，按原行动边界提交；下一位只能看见已提交的世界态。
			std::uint8_t order[SA::Rules::kSlotCount]{};
			const int count = SA::Rules::buildActionOrder(b.field, b.commands, b.rng, order);
			b.events.battle_id = b.id;
			b.events.turn = b.field.turn;
			b.events.events.clear();
			const auto flush = [&]()
			{
				for (auto sid : b.members)
				{
					auto conn = s.conns.find(sid);
					if (conn != s.conns.end() && conn->second.session != nullptr &&
					    !conn->second.session->push(b.events, conn->second.outbound))
						conn->second.session->close();
				}
				b.events.events.clear();
			};
			std::uint32_t turn_events = 0;
			for (int index = 0; index < count; ++index)
			{
				const int actor = order[index];
				if (!b.field.at(actor).occupied || b.field.at(actor).dead)
					continue;
				// 前一步可能删掉背包物品，不能复用回合开始时的门投影。
				projectCaptureItemGate(b, s.players, s.enemies, s.items);
				projectItemUsePower(b, s.players, s.items, s.item_effects);
				// 宠技参数同样逐行动重投影(B1):宠物换了指令 / 指令被状态清空后,
				// 旧技能参数不能残留(见 projectPetSkill 卷首的"逐行动重算"注记)。
				projectPetSkill(b, s.pet_skill_effects);
				// 集气态投影(B2b)在宠技表投影**之后**:完成击要覆盖表投影的
				// direct=false / attack_percent=0(见 projectChargeState 卷首)。
				projectChargeState(b, actor);
				// 魔法状态投影(B3b):任意槽都可能是被攻击者 ⇒ 全槽投影。
				projectMagicStatus(b);
				// applyEvents 落施加事件时要用**本行动**的投影参数算回合数(B3a)。
				wctx.actor = actor;
				SA::Domain::BattleEvents action{};
				SA::Rules::ActionEffects effects;
				const auto before_rng = b.rng;
				if (!SA::Rules::resolveAction(b.field, b.commands, s.rules_config,
				                              b.rng, actor, action, effects))
				{
					// 单动作输出超界：不提交其前缀、不重摇重试、不宣布战斗成功。
					b.rng = before_rng;
					b.stats.truncated_once = true;
					b.aborted = true;
					s.logger.log(SA::Platform::LogLevel::kError,
					             SA::Platform::LogEvent::kBattleEventsTruncated,
					             {{"battle_id", b.id}, {"turn", static_cast<std::uint64_t>(b.field.turn)}});
					break;
				}
				applyEvents(action, b.field, wctx);
				applyChargeEffects(b, actor, effects);
				// 魔法状态系宠技的施加(B3b):在行动位、且本行动**没被状态/不可行动
				// 清掉**才发生(原版 COM 被清 ⇒ 走不到 case S_SUPERWALL,battle.c:5440)。
				if (!effects.command_cleared)
					applyMagicStatusPetSkill(b, actor, s.pet_skill_effects);
				// 状态攻击施加当场清掉**目标**的指令(B3a;原版 battle_event.c:2932-2937
				// 对守方 `COM1 = NONE`,只列麻痹/睡眠/石化/魔障)—— 与上面 actor 自己
				// 的 command_cleared 同款:改写成 WAIT,L3 对 WAIT 无动作。
				// ⚠️ 若目标本回合尚未行动,后续派发由 checkCanAct 再挡一道
				//   (field.status 已落地)⇒ 双保险同源于一个状态位,不冲突。
				if (effects.status_cleared_target >= 0 &&
				    effects.status_cleared_target < SA::Rules::kSlotCount &&
				    b.commands.present[effects.status_cleared_target] &&
				    !b.field.at(effects.status_cleared_target).dead)
				{
					b.commands.commands[effects.status_cleared_target].command_kind =
					    SA::Domain::BattleCommand::CommandKind::WAIT;
				}
				if (effects.item_used)
					consumeUsedItem(b, actor, s.players, s.items);
				if (effects.command_cleared)
				{
					b.commands.commands[actor].command_kind = SA::Domain::BattleCommand::CommandKind::WAIT;
					// ★ 集气态一并清掉(批次 B2b):原版 StatusSeq 对不能行动者无条件
					//   `COM1 = NONE`(battle.c:5440,无 IsCharge 豁免)⇒ 集气夭折,
					//   不是暂停 —— 与 L3「状态门在集气分支之前」的次序配套。
					b.charge_of_slot[static_cast<std::size_t>(actor)] = ChargeState{};
				}
				// 基础反击链在死亡时终止；最后一条 Hit 标记实际击杀方。
				// 无 Hit 时仍按原行动者结算状态死亡，不把反击战果记给先攻者。
				int profit_actor = actor;
				for (const auto &event : action.events)
					if (event.body_kind == SA::Domain::BattleEvent::BodyKind::HIT)
						profit_actor = static_cast<int>(event.body.hit.attacker);
				settleDeaths(b, profit_actor, s.enemies);
				bool changed_entries = false;
				for (const auto &event : action.events)
				{
					if (b.events.events.size() == b.events.events.capacity())
						flush();
					(void)b.events.events.push_back(event);
					++turn_events;
					using Kind = SA::Domain::BattleEvent::BodyKind;
					changed_entries = changed_entries || event.body_kind == Kind::ENTER ||
					                  event.body_kind == Kind::QUIT ||
					                  (event.body_kind == Kind::ESCAPE && event.body.escape.succeeded) ||
					                  (event.body_kind == Kind::CAPTURE_ACT && event.body.capture_act.flags != 0);
				}
				if (changed_entries)
				{
					flush(); // 快照覆盖此前事件，后续增量在它之后。
					s.pushBattleSnapshot(b);
				}
			}
			flush();
			s.pushBattleSnapshot(b);
			b.stats.events_emitted += turn_events;
			if (b.aborted)
			{
				for (auto sid : b.members)
					if (auto conn = s.conns.find(sid); conn != s.conns.end() && conn->second.session)
						conn->second.session->close();
				b.stats.finished = true;
				finished.push_back(b.id);
				continue;
			}
			if (s.storage)
				for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
					if (auto *player = s.players.resolve(b.player_of_slot[static_cast<std::size_t>(slot)]))
					{
						player->hp = std::max(0, b.field.at(slot).hp);
						player->mp = std::max(0, b.field.at(slot).mp);
					}
			++b.stats.turns_resolved;
			// 原 TurnParam 在下一轮准备重算临时敏捷；此处恢复同一个来源值。
			for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
				if (auto &base = b.quick_to_restore[static_cast<std::size_t>(slot)]; base)
				{
					b.field.at(slot).quick = *base;
					base.reset();
				}
			s.logger.log(SA::Platform::LogLevel::kDebug,
			             SA::Platform::LogEvent::kBattleTurnResolved,
			             {{"battle_id", b.id}, {"turn", static_cast<std::uint64_t>(b.field.turn)}, {"events", static_cast<std::uint64_t>(turn_events)}});

			// 本回合的指令用完即清 —— 指令是**本回合**的输入,
			// 留着会让下一回合重放上一回合的动作。
			b.commands = SA::Rules::TurnCommands{};
			b.command_deadline_sec = 0;
			++b.field.turn;
			// ★ 守护链接**每回合整体清空**(批次 A-β d2;原 `BATTLE_PreCommandSeq`,
			//   battle.c:3578-3600:遍历两 side 全部 Entry 置 `guardian = -1`)。
			//   ⇒ 守护只持续**一个指令回合**;要续必须下回合重新用忠犬。
			//   ⚠️ 位置在 `++turn` 之后、下一回合的指令收集之前 —— 与源码在
			//     「指令收集期开始前」清空同相位(源码在 PreCommandSeq 里清,
			//     该函数正在建立指令等待态 `BATTLE_AllCharaCWaitSet`)。
			for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
				b.field.at(slot).guardian = -1;
			b.next_turn_at_ms =
			    s.now_ms + static_cast<SA::Platform::Millis>(
			                   s.config.tempo.battle_turn_interval_ms);

			if (sideWipedOut(b.field, true) || sideWipedOut(b.field, false))
			{
				b.stats.finished = true;
				finished.push_back(b.id);
			}
			else
			{
				// 下一回合开始；先刷新本人行动限制，SelfInfo 不会清除客户端血量。
				SA::Domain::BattleTurnBegin begin{};
				begin.battle_id = b.id;
				begin.turn = b.field.turn;
				begin.ready_mask = readyMask(b);
				for (const SA::Net::SessionId sid : b.members)
				{
					const auto it = s.conns.find(sid);
					if (it == s.conns.end() || it->second.session == nullptr)
						continue;
					SA::Domain::BattleSelfInfo info{};
					info.battle_id = b.id;
					info.slot = b.slot_of.at(sid);
					info.mp = b.field.at(static_cast<int>(info.slot)).mp;
					info.cannot_act = SA::Rules::checkCanAct(b.field.at(static_cast<int>(info.slot)));
					(void)it->second.session->push(info, it->second.outbound);
					(void)it->second.session->push(begin, it->second.outbound);
				}
			}
		}
		for (const BattleId id : finished)
		{
			const auto it = s.battles.find(id);
			if (it == s.battles.end())
				continue;

			BattleInstance &b = it->second;

			// F18: 抽签/资格已在逐行动边界处理；这里仅最终交付。
			syncPetState(b, s.pets);
			if (!b.aborted)
			{
				const bool player_won = sideWipedOut(b.field, true) && !sideWipedOut(b.field, false);
				for (int slot = 0; slot < SA::Rules::kBattlePlayerMax; ++slot)
					deliverPlayerProfit(b, slot, s.players, s.items);

				// ── 战斗产币(经济地基批,DR-EC6;接点 = 战果结算,exp 分配旁)──────────
				//
				// ★ 对账(A-α 批):原版把金放在**结束 flush**,不在 AddProfit/ AddExpItem 里。
				//   `BATTLE_Finish`(SSRC80 `battle.c:3558`)对**两侧**全部入场单位逐个调
				//   `BATTLE_GetProfit`(:3598;`BATTLE_Stop`:3639 同构,:3651;
				//   胜负只影响 WinFunc/掉落,不影响这条),非决斗点怪经 `:3540-3546` 的
				//   `dpbattle` 分支进 `BATTLE_GetExpGold`(:3308),其中
				//   `gold += getBattleGold()` 钳到随身上限(8.5 `battle.c:3851-3860`;
				//   8.0 公式不可判定 —— 证据边界见 world/Api.h 的 GoldLedger 节)。
				//   ⚠️ 本实现的分工:**金在本段走 GoldLedger**,**经验/掉落暂存在
				//   deliverPlayerProfit 交付**(上方)—— 两半合起来才是 GetExpGold 的对应物。
				//   ⇒ 三个门都在这里,账本只管记账:
				//   ① **死亡不给**(源码 `CHAR_ISDIE` 提前返回:SSRC80 `:3327-3329` /
				//      8.5 `:4254-4256`;逃跑/超时离场的玩家已不在 player_of_slot 里,
				//      resolve 不到 ⇒ 自然跳过);
				//   ② **决斗点怪不给金**(dpbattle ⇒ 走 `BATTLE_GetDuelPoint`(SSRC80 :4779)
				//      不走 GetExpGold;⚠️ 决斗点的**记账**本身不复刻 —— Player 实体无
				//      dp 字段,属 dp 域批次,即 settleDeaths 对账注里的良性偏离 ③);
				//   ③ **每场一次**:本段只在战斗 finished 时走一遍,battles 随即 retire
				//      (原版逐单位 flush 同样每场一次,两者等价)。
				if (!b.dp_battle)
					for (int slot = 0; slot < SA::Rules::kBattlePlayerMax; ++slot)
					{
						// 源码 CHAR_ISDIE 门(死亡不给;SSRC80 :3327-3329 / 8.5 :4254-4256)。
						if (b.field.at(slot).dead)
							continue;
						// 逃跑 / 超时离场的玩家已不在场(源码里 BATTLE_Exit 把他们清出
						// Entry,:3596 的 CHAR_CHECKINDEX 即 continue ⇒ 战果结算没有他们;
						// exp 走的是本实现的"离场即领暂存"适配,金没有暂存态 ⇒ 不给)。
						if (!b.field.at(slot).occupied)
							continue;
						SA::Model::Player *p =
						    s.players.resolve(b.player_of_slot[static_cast<std::size_t>(slot)]);
						if (p == nullptr)
							continue;
						(void)addGold(*p, GoldReason::kBattleReward, kBattleGold,
						              /*trans=*/0, static_cast<std::uint64_t>(b.id), s);
					}

				// ── 下发 BattleResult(战斗结束都发,告知胜负 + 经验)战果结算批次 ──────
				//
				// ★ 每个**在场玩家实体**一条 ExpGain(本场 gained + 累计 exp_total),
				//   即使 gained == 0(打输 / 决斗点怪 / demo 无 L2 敌人)也发 —— 客户端要能
				//   显示"本场结果"。⚠️ 独立顶层消息,不进事件流(理由见 battle_events.proto)。
				SA::Domain::BattleResult result{};
				result.battle_id = b.id;
				result.player_won = player_won;
				for (int ps = 0; ps < SA::Rules::kSideOffset; ++ps)
				{
					const SA::Model::Player *p = s.players.resolve(
					    b.player_of_slot[static_cast<std::size_t>(ps)]);
					if (p == nullptr)
						continue;
					SA::Domain::ExpGain g{};
					g.slot = static_cast<std::uint32_t>(ps);
					g.exp_gained = b.gained[static_cast<std::size_t>(ps)];
					g.exp_total = p->exp;
					(void)result.exp_gains.push_back(g);
				}

				for (const SA::Net::SessionId sid : b.members)
				{
					const auto cit = s.conns.find(sid);
					if (cit == s.conns.end())
						continue;
					Impl::Conn &c = cit->second;
					if (c.session == nullptr)
						continue;
					(void)c.session->push(result, c.outbound);
				}
			}

			// ★★ **战斗结束 ⇒ 该场剩下的敌人 L2 实体全部回池(批次 M.4b)**。
			//
			// ⚠️★ 这一步不是"顺手清理",它有源码依据也有前车之鉴:
			//    · 源码依据:敌人离场即销毁(`battle.c:1114-1115` 的 `CHAR_endCharOneArray`),
			//      而战斗结束是所有剩余单位一起离场;
			//    · 前车之鉴:M.1 漏了"主人下线时一并释放宠物",症状是**池只增不减、
			//      没有一处报错**,跑够久才表现为"捕获突然失败"。
			//      ⇒ 敌人池同族,而它每场战斗都会分配 ⇒ 漏了泄漏得比宠物快得多。
			// ⚠️ 被捕获的那只已在 `applyEvents` 里释放并把句柄清空 ⇒ 这里 `release`
			//    对空句柄返回 false 且不做事(generation 校验),不会重复释放。
			for (SA::Model::EntityHandle &h : it->second.enemy_of_slot)
			{
				(void)s.enemies.release(h);
				h = SA::Model::kNullHandle;
			}

			s.logger.log(SA::Platform::LogLevel::kInfo,
			             SA::Platform::LogEvent::kBattleFinished,
			             {{"battle_id", id},
			              {"turns", static_cast<std::uint64_t>(
			                            it->second.stats.turns_resolved)}});
			const auto members = b.members;
			s.retireBattle(id);
			if (s.storage)
				for (auto sid : members)
					saveCharacter(sid, false, 0);
		}
	}

	// ── 5. 角色循环 —— 玩家段(批次 W.1。原 CHAR_Loop:4667 玩家 for + CHAR_walk_check:4583)──
	//   ★ 全扫在线玩家:走路串非空 且距上次走够 walksendinterval ⇒ 走一步(CHAR_walkcall)。
	//   ⚠️ 非玩家段(世界敌人 AI 摊还)见下方 5b —— **条数制**(EnemyMoveNum 上限 + 游标),
	//      **不是** CHAR_Loop:4712 那个时间预算 while(那是 _CHAR_LOOP_TIME,8.0 三证关,见 §9.0.45);
	//      组队跟随各留其批。
	for (auto &kv : s.conns)
	{
		Impl::Conn &c = kv.second;
		if (s.inBattle(kv.first))
		{
			c.walk_seq.clear();
			continue;
		}
		if (c.session == nullptr || c.walk_seq.empty() || (s.storage && (c.pending || c.detached || c.save_failed)))
			continue;
		// 间隔门(CHAR_walk_check:4590):到点才走一步,走完把下次时刻推后 kWalkIntervalMs。
		if (s.now_ms < c.next_walk_at_ms)
			continue;
		SA::Model::Player *p = s.players.resolve(s.player_of_session.find(kv.first));
		if (p == nullptr)
		{
			c.walk_seq.clear(); // 无实体 ⇒ 丢弃走路串(不会再有落点)
			continue;
		}
		// 消费首字符(CHAR_walkcall:730 ctodirmode + :849 &tmp[1])。
		std::uint8_t dir = 0;
		bool is_turn = false;
		const std::int32_t ox = p->x;
		const std::int32_t oy = p->y;
		bool moved = false;
		auto *fl = s.getFloor(p->floor);
		const auto &cur_map = fl ? fl->map : s.map;
		auto &cur_olink = fl ? fl->olink : s.olink;
		if (decodeDirChar(c.walk_seq.front(), dir, is_turn))
			moved = walkStep(*p, cur_map, s.map_attr, dir, is_turn);
		// ⚠️ 非法字符也消费掉,不卡住整串(原版 ctodirmode 不校验,越界由 VALIDATEDIR 兜)。
		c.walk_seq.erase(c.walk_seq.begin());
		c.next_walk_at_ms = s.now_ms + kWalkIntervalMs;

		// ── W.5:撞明雷退回(移植 char_walk.c:585-593)────────────────────────
		//   走到的新格若有世界敌人(明雷)⇒ 弹回原格,不占敌人格(朝向已落,保留)。
		//   ★ 与开战解耦:开战靠玩家主动发 EV(onEvent);走路撞上只退回 —— 原版两条独立机制。
		//   ⚠️ 客户端坐标纠正(原版 XYD_send:588)划出:W.1 未建 XYD 下行,同其走路同步残缺。
		if (moved && s.worldEnemyAt(p->floor, p->x, p->y) != s.world_enemies.size())
		{
			p->x = ox;
			p->y = oy;
			moved = false;
		}

		// ── W.7:撞 NPC 实体退回(CHAR_ISOVERED=0 阻挡不可穿透)────────────
		if (moved && s.npcAt(p->floor, p->x, p->y) != s.npc_entities.size())
		{
			p->x = ox;
			p->y = oy;
			moved = false;
		}

		// ── W.6: WARP 传送点触发(移植 npc_warp.c / char.c:4594-4675)────────────
		//   玩家走入新格(moved)若命中 WarpPoint,且目标格合法可通行,则触发瞬移:
		//   清空剩余路径串 + 旧视野 Disappear + 瞬移新坐标 + 新视野 Appear + 自身 CharMove 同步。
		bool warped = false;
		if (moved)
		{
			const WarpPoint *wp = s.findWarpPoint(p->floor, p->x, p->y);
			if (wp != nullptr)
			{
				const bool same_floor = (wp->dst_floor == p->floor);
				const auto *dst_fl = s.getFloor(wp->dst_floor);
				const auto &dst_map = dst_fl ? dst_fl->map : s.map;
				if (dst_fl != nullptr)
				{
					if (dst_map.inBounds(wp->dst_x, wp->dst_y) &&
					    mapWalkable(dst_map, s.map_attr, wp->dst_x, wp->dst_y))
					{
						warped = true;
						s.warpPlayer(kv.first, wp->dst_floor, wp->dst_x, wp->dst_y);
					}
				}
				else if (!same_floor || (s.map.inBounds(wp->dst_x, wp->dst_y) &&
				                         mapWalkable(s.map, s.map_attr, wp->dst_x, wp->dst_y)))
				{
					warped = true;
					s.warpPlayer(kv.first, wp->dst_floor, wp->dst_x, wp->dst_y);
				}
			}
		}

		// 里程碑②:位置变了且未传送 ⇒ 更新 olink(旧格摘、新格挂)+ 视野广播(扫格 diff)。
		if (moved && !warped)
		{
			if (cur_map.inBounds(ox, oy))
			{
				const auto idx = cur_map.index(ox, oy);
				if (idx < cur_olink.size())
				{
					auto &oldcell = cur_olink[idx];
					oldcell.erase(std::remove(oldcell.begin(), oldcell.end(), kv.first),
					              oldcell.end());
				}
			}
			if (cur_map.inBounds(p->x, p->y))
			{
				const auto idx = cur_map.index(p->x, p->y);
				if (idx < cur_olink.size())
					cur_olink[idx].push_back(kv.first);
			}
			s.broadcastMove(kv.first, ox, oy, *p);
			// W.3:玩家移动后补发视野内**世界敌人**的 appear / disappear(玩家看敌人那一半)。
			s.refreshEnemyView(kv.first, *p, ox, oy);
			// W.7:玩家移动后补发视野内**世界 NPC** 的 appear / disappear。
			s.refreshNpcView(kv.first, *p, ox, oy);

			// ── 遇敌判定(批次 W.4。原 char_walk.c:585,展开视图基准)────────────
			//   ★ 只在真移动(moved)后判:转身 / 撞墙不触发(原版遇敌在 walk_move 成功后)。
			//   ⚠️ 数据表空(未 loadEncounterTables)⇒ findEncountArea 恒 -1 ⇒ 不遇敌
			//      (现有走路用例不注入即不受影响)。
			const std::int32_t arow =
			    findEncountArea(s.encount_areas, p->floor, p->x, p->y);
			if (arow >= 0)
			{
				const EncountArea &area =
				    s.encount_areas[static_cast<std::size_t>(arow)];
				// cep 夹在 [prob_min, prob_max](char_walk.c:553-554),temp = cep
				//   (无技能 ⇒ p_cep=0 ⇒ temp=cep)。min/max 写反自动纠正(encount.c:245-253,
				//   与敌人表 lv_min/max 同族)——载入期做,这里防御性纠一次不改行为。
				std::int32_t lo = area.prob_min;
				std::int32_t hi = area.prob_max;
				if (lo > hi)
				{
					const std::int32_t t = lo;
					lo = hi;
					hi = t;
				}
				if (c.cep < lo)
					c.cep = lo;
				if (c.cep > hi)
					c.cep = hi;
				// 遇敌骰子 rand()%(120*getEnemyAction()) < temp(char_walk.c:585)。
				//   ★ 用**世界 rng**(遇敌是世界事件,不是战斗内可回放序列)。
				const int denom = 120 * clampEnemyAction(s.config.enemy_action);
				if (s.world_rng.randMod(denom) < c.cep)
				{
					// 命中 ⇒ 清走路串(EN_recv:WALKARRAY="")+ cep 重置 prob_min(:594)+ 开战。
					//   ⚠️ 清串后本 conn 剩余方向作废(原版遇敌即中断走路);triggerEncounter
					//      只动 s.battles / 池,不增删 s.conns ⇒ 本遍历的引用 c 仍有效。
					c.walk_seq.clear();
					c.cep = lo;
					(void)triggerEncounter(kv.first, arow);
				}
			}
		}
		if (s.storage && !s.inBattle(kv.first))
			saveCharacter(kv.first, false, 0);
	}

	// ── 5b. 角色循环 —— 非玩家段:世界敌人 AI(批次 W.3)──────────────────────
	//   ★ 条数制摊还:每 tick 最多游荡 tempo.enemy_move_num 只世界敌人,游标续跑(wanderWorldEnemies)。
	//   ⚠️★ **不是时间预算制** —— 8.0 的 _CHAR_LOOP_TIME 三证实测关(15 §5.2 C18),走 #else 条数制。
	s.wanderWorldEnemies(s.config.tempo.enemy_move_num);

	// ── 5c. 角色循环 —— 非玩家段:世界 NPC 巡逻与漫游 AI(批次 W.11) ──────────
	//   ★ 条数制摊还:每 tick 最多游荡 tempo.enemy_move_num 只世界 NPC,游标续跑(wanderNpcs)。
	s.wanderNpcs(s.config.tempo.enemy_move_num);

	// ── 6. 定时业务 ──   ⬜ 阶段 2
	// ── 7. 出站聚合 ──   ⬜ 阶段 2(CA/CD 视野聚合;1.5 无视野)
	//
	// ⚠️ 但**出站字节仍要发出去** —— 上面第 4 步往 outbound 里写了东西。
	//    这不是 §7 说的那种聚合(那是视野 Appear/Disappear 攒批),
	//    只是"把已经生成的字节交给传输层"。别把这里读成 §7 已经做了。
	// send/close 可能同步触发断线回调，不能持有 conns 迭代器或其缓冲再继续使用。
	std::vector<SA::Net::ConnectionId> flushing;
	for (const auto &entry : s.conns)
		flushing.push_back(entry.first);
	for (auto id : flushing)
	{
		auto entry = s.conns.find(id);
		if (entry == s.conns.end())
			continue;
		bool closing = entry->second.session != nullptr && entry->second.session->closed();
		std::vector<std::uint8_t> bytes;
		bytes.swap(entry->second.outbound);
		if (!bytes.empty() && !s.transport.send(id, bytes.data(), bytes.size()))
			closing = true;
		bytes.clear();
		entry = s.conns.find(id);
		if (entry != s.conns.end() && entry->second.outbound.empty())
			bytes.swap(entry->second.outbound);
		if (closing)
			s.transport.close(id);
	}

	// ── 8. 关闭检查 ──
	if (s.shutdown_requested && s.storage)
	{
		std::vector<SA::Net::SessionId> sessions;
		for (const auto &entry : s.conns)
			if (!entry.second.detached)
				sessions.push_back(entry.first);
		for (auto id : sessions)
		{
			onDisconnected(id);
			s.transport.close(id);
		}
		s.stopped = s.conns.empty() && s.storage->idle();
		return;
	}
	if (s.shutdown_requested)
	{
		// ⚠️ 01 §11.2 的完整停服流程(拒绝新连接 → 广播倒计时 → 逐会话保存
		//    → 等在途请求收敛 → 落盘确认)在 1.5 **做不了也不该做**:
		//    没有 storage、没有跨模块请求。这里只做能做的那部分。
		s.logger.log(SA::Platform::LogLevel::kInfo,
		             SA::Platform::LogEvent::kServerStopping,
		             {{"connections", static_cast<std::uint64_t>(s.conns.size())},
		              {"battles", static_cast<std::uint64_t>(s.battles.size())}});
		// ⚠️★ **不能边遍历 s.conns 边关**:transport.Close() 会**同步**回调
		//    OnDisconnected,而它做的第一件事就是 s.conns.erase(it)
		//    ⇒ 迭代器当场失效。首次运行本用例即 SIGSEGV(2026-09-04)。
		//    ⇒ 先取快照,再逐个关。
		std::vector<SA::Net::ConnectionId> closing;
		closing.reserve(s.conns.size());
		for (const auto &kv : s.conns)
			closing.push_back(kv.first);
		for (const SA::Net::ConnectionId cid : closing)
		{
			const auto it = s.conns.find(cid);
			if (it != s.conns.end() && it->second.session != nullptr)
			{
				it->second.session->close();
			}
			s.transport.close(cid);
		}
		s.stopped = true;
	}
}

// ══ 战斗生命周期 ═════════════════════════════════════════════════
BattleId World::startBattle(const SA::Rules::BattleField &field)
{
	Impl &s = *_impl;
	const BattleId id = s.next_battle_id++;

	BattleInstance b;
	b.id = id;
	b.field = field;
	b.field.battle_id = id;
	b.started_sec = s.now_ms / 1000;
	b.seed = s.random.nextSeed();
	b.rng = SA::Rules::SeededRandom(b.seed);
	b.next_turn_at_ms =
	    s.now_ms +
	    static_cast<SA::Platform::Millis>(s.config.tempo.battle_turn_interval_ms);

	const std::uint64_t seed = b.seed;
	s.battles.emplace(id, std::move(b));

	s.logger.log(SA::Platform::LogLevel::kInfo,
	             SA::Platform::LogEvent::kBattleStarted, {{"battle_id", id}});
	// ★★ 种子单独一条,级别 info:它是可回放的**唯一**凭据。
	//    调低成 debug 就等于在生产上关掉了可回放性。
	s.logger.log(SA::Platform::LogLevel::kInfo,
	             SA::Platform::LogEvent::kBattleSeed,
	             {{"battle_id", id},
	              {"seed", seed},
	              {"master_seed", s.random.masterSeed()}});
	return id;
}

bool World::joinBattle(BattleId battle, SA::Net::SessionId session,
                       std::uint8_t slot)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;
	const auto bit = s.battles.find(battle);
	if (bit == s.battles.end())
		return false;
	if (slot >= SA::Rules::kSlotCount)
		return false;

	const auto cit = s.conns.find(session);
	if (cit == s.conns.end() || cit->second.session == nullptr)
		return false;
	// ⚠️ 只有握手过的会话能入场 —— 否则一条没握手的连接就能拿到事件流。
	if (cit->second.session->state() == SA::Net::SessionState::kAnonymous ||
	    cit->second.session->closed())
	{
		return false;
	}

	BattleInstance &b = bit->second;
	if (std::find(b.members.begin(), b.members.end(), session) !=
	    b.members.end())
	{
		return false;
	}
	b.members.push_back(session);
	b.slot_of[session] = slot;
	cit->second.walk_seq.clear();
	// ★ 槽号 → L2 `Player` 实体(批次 M.1)。捕获要把新宠物挂进**攻方主人**的宠物槽,
	//   而事件里只有槽号 ⇒ 入场时就把这条映射建起来,不到用时再去反查 `slot_of`
	//   (反查是 O(n) 且要在 applyEvents 里拿到 Impl,那会把 L2 落脚点越铺越宽)。
	// ⚠️ 查不到就留空句柄 —— 观战席位、以及尚未接 L2 的槽本来就没有实体,
	//    那是正常状态,不是错误(见 BattleInstance::player_of_slot 的注释)。
	b.player_of_slot[slot] = s.player_of_session.find(session);

	// ★ DR-BT21:入场自动带出出战宠(读 L2 的 `default_pet`)。
	//   ⚠️ M.2(§9.0.27 ⑤)时这是**死路径** —— `default_pet` 无写者;本批 PET_OUT 补上
	//     写者后激活。demo 玩家无预设 `default_pet` ⇒ demo 不触发(正常),由单元测覆盖。
	//   ★ 复用 M.2 的 enterPetToField:门②③(宠物存活 / 宠位空)在其内,失败即不入场。
	if (SA::Model::Player *p = s.players.resolve(b.player_of_slot[slot]);
	    p != nullptr && p->default_pet >= 0 &&
	    p->default_pet < static_cast<int>(SA::Model::kMaxPetHave))
	{
		if (SA::Model::Pet *pet =
		        s.pets.resolve(p->pets[static_cast<std::size_t>(p->default_pet)]))
			if (enterPetToField(b.field, slot, *pet))
				b.pet_of_slot[slot + SA::Rules::kBattlePlayerMax] =
				    p->pets[static_cast<std::size_t>(p->default_pet)];
	}

	cit->second.session->markOnline();

	// ★★ 入场即下发**自己是谁**与**现在是第几回合**,否则客户端无从组指令:
	//    BattleCommand 要带 battle_id 与 turn,而这两样它此刻都还不知道
	//    —— 缺这一步,上行链路在 demo 里根本走不到。
	//
	// ⚠️ 这不是 demo 专用的东西,所以放在 JoinBattle 而不是 OnSessionReady:
	//    任何入场路径(阶段 2 的选角、观战加入)都需要它。
	SA::Domain::BattleSelfInfo self{};
	self.battle_id = b.id;
	self.slot = slot;
	self.mp = b.field.at(slot).mp;
	// ⚠️ menu_flags 留 0:菜单构成(观战加入 / 先制 / 宠物菜单开关)属阶段 2 的
	//    UI 契约,此处**不猜** —— 与批次 0.5 对暴击/反击的处置同一条纪律。
	self.menu_flags = 0;
	// ★ 但 cannot_act **不留 0**:DR-BT5 把「能否行动」定为 Rules::CheckCanAct
	//   这一个真源,而它已经在 L3 里 ⇒ 照真源填,不是硬编码一个"可以行动"。
	self.cannot_act = SA::Rules::checkCanAct(b.field.at(slot));
	(void)cit->second.session->push(self, cit->second.outbound);
	s.pushBattleSnapshot(b);

	SA::Domain::BattleTurnBegin begin{};
	begin.battle_id = b.id;
	begin.turn = b.field.turn;
	begin.ready_mask = readyMask(b);
	(void)cit->second.session->push(begin, cit->second.outbound);

	// ⚠️ 入场日志放在这里而不是调用方:任何入场路径都该留痕,
	//   而"谁在哪场的哪个槽"是排查战斗问题时第一个要问的东西。
	s.logger.log(SA::Platform::LogLevel::kInfo,
	             SA::Platform::LogEvent::kBattleJoined,
	             {{"battle_id", b.id},
	              {"session_id", session},
	              {"slot", static_cast<std::uint64_t>(slot)}});
	return true;
}

// ══ 敌人生成入场(批次 M.4b)══════════════════════════════════════
//
// 详注见 world/Api.h 的声明处。★ 三道门,顺序 = 预留 → 提交:
//   两个可失败的动作(池 / 入场)都排在任何不可回退的写之前
//   ⇒ 失败时世界状态一个字节都没动(同 `createPetFromCapture` 的形状)。
bool World::spawnEnemyToField(BattleId battle, std::uint8_t slot,
                              const EnemyTemplate &tmpl, const EnemyEncounter &enc,
                              std::int32_t baselevel)
{
	Impl &s = *_impl;

	// ── 门 ①:战斗与槽号 ──────────────────────────────────────────
	const auto bit = s.battles.find(battle);
	if (bit == s.battles.end())
		return false;
	if (slot >= SA::Rules::kSlotCount)
		return false;
	BattleInstance &b = bit->second;

	// ⚠️★ 该槽已有敌人实体 ⇒ 拒绝。**不是**因为槽被占(那是门 ③ 的事),
	//    而是因为覆盖掉旧句柄就等于泄漏一个池槽 —— 与 M.1 那条漏释放同族。
	if (b.enemy_of_slot[slot].valid())
		return false;

	// ── 门 ②:敌人池 ──────────────────────────────────────────────
	const SA::Model::EntityHandle eh = s.enemies.allocate();
	if (!eh.valid())
	{
		// ⚠️ 池满必须报出来(同 M.1 的 Player 池):容量是硬上限,`allocate` 不会扩容。
		s.logger.log(SA::Platform::LogLevel::kError,
		             SA::Platform::LogEvent::kEntityPoolExhausted,
		             {{"battle_id", battle},
		              {"pool", std::string_view("enemy")},
		              {"capacity", static_cast<std::uint64_t>(kMaxEnemies)}});
		return false;
	}
	SA::Model::Enemy *enemy = s.enemies.resolve(eh);
	if (enemy == nullptr)
	{
		// ★ 走不到(刚 allocate 成功)。守它零成本,理由同 createPetFromCapture。
		return false;
	}

	// ★ 生成:消耗**该场战斗的 rng**(可回放的凭据是战斗种子,见 Api.h 声明处)。
	// ⚠️★ 消耗次数**取决于分支**:`baselevel > 0` ⇒ 14 次;`<= 0` ⇒ 15 次
	//    (多的那次是等级摇号,且在最前面)。改动这里的调用序会改变回放。
	*enemy = spawnEnemy(tmpl, enc, baselevel, b.rng, s.rules_config);

	// ── 门 ③:入场投影 ────────────────────────────────────────────
	if (!enterEnemyToField(b.field, static_cast<int>(slot), *enemy))
	{
		// ⚠️★ **失败要把刚分配的实体还回去** —— 否则每次入场失败都泄漏一个槽,
		//    而"入场失败"是完全正常的(槽被占 / 槽在宠位)⇒ 泄漏会累积得很快。
		//    ★ 这一步就是"预留 → 提交"里的**回滚**:门 ② 的预留可撤销,所以能这样写。
		(void)s.enemies.release(eh);
		return false;
	}

	// ── 提交:记下「槽 → 敌人实体」的映射 ────────────────────────────
	// ★ 到这里没有可失败的动作了。捕获要靠这条映射找到四维的源头。
	b.enemy_of_slot[slot] = eh;
	b.dp_battle = b.dp_battle || enemy->duelpoint > 0;
	s.pushBattleSnapshot(b);

	// ⚠️★ 记的是 `enemy->level`(**实际生效**的等级)而不是入参 `baselevel` ——
	//    摇号分支下入参是 0,记它等于什么都没记。★ 同时记 `enemy_id`,
	//    否则"这只怪是哪一行配出来的"在日志里无从追溯(敌人表 44 处引用都靠它)。
	s.logger.log(SA::Platform::LogLevel::kDebug,
	             SA::Platform::LogEvent::kBattleJoined,
	             {{"battle_id", battle},
	              {"slot", static_cast<std::uint64_t>(slot)},
	              {"level", static_cast<std::uint64_t>(enemy->level)},
	              {"enemy_id", static_cast<std::uint64_t>(enc.enemy_id)},
	              {"kind", std::string_view("enemy")}});
	return true;
}

void World::loadEncounterTables(std::vector<EncountArea> areas,
                                std::vector<EnemyGroup> groups,
                                std::vector<EnemyEncounter> encounters,
                                std::vector<EnemyTemplate> templates)
{
	Impl &s = *_impl;
	s.encount_areas = std::move(areas);
	s.enemy_groups = std::move(groups);
	s.encounters = std::move(encounters);
	s.enemy_templates = std::move(templates);
}

void World::loadSpawnPoints(std::vector<SpawnPoint> points)
{
	_impl->spawn_points = std::move(points);
}

void World::loadWarpPoints(std::vector<WarpPoint> points)
{
	_impl->warp_points = std::move(points);
}

void World::loadNpcEntities(std::vector<NpcEntity> npcs)
{
	for (auto &npc : npcs)
	{
		if (npc.born_x == 0 && npc.born_y == 0)
		{
			npc.born_x = npc.x;
			npc.born_y = npc.y;
		}
		if (npc.wander_interval_ms > 0 && npc.next_wander_at_ms == 0)
		{
			npc.next_wander_at_ms = _impl->now_ms + npc.wander_interval_ms;
		}
	}
	_impl->npc_entities = std::move(npcs);
}

void World::loadItemEffects(std::vector<ItemEffect> effects)
{
	_impl->item_effects = std::move(effects);
}

void World::loadPetSkillEffects(std::vector<PetSkillEffect> effects)
{
	_impl->pet_skill_effects = std::move(effects);
}

int World::giveItemToPlayer(SA::Net::SessionId session, const SA::Model::Item &item)
{
	Impl &s = *_impl;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return -1; // 门 ①:无 L2 玩家实体
	// 门 ②③(背包空槽 / 池满)抽到 `giveItemIntoPlayer`,与掉落灌包共用(道具域第三批 I.3)。
	return giveItemIntoPlayer(*p, item, s.items);
}

// 批次 B2 的注入 seam(声明见 Api.h):三门全过才写 ⇒ 失败不留孤儿。
// ⚠️ 刻意**不写 `default_pet`** —— 它的唯一写者是换宠指令 PET_OUT(DR-BT21),
//   本 seam 在它旁边开第二扇门就会把"换宠语义在世界侧只有一处"打破。
int World::givePetToPlayer(SA::Net::SessionId session, const SA::Model::Pet &pet)
{
	Impl &s = *_impl;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return -1; // 门 ①:无 L2 玩家实体
	const int pet_slot = p->findFreePetSlot();
	if (pet_slot < 0)
		return -1; // 门 ②:宠物槽满(悬空句柄算占用,两步分工见 Player.h)
	const SA::Model::EntityHandle handle = s.pets.allocate();
	if (!handle.valid())
		return -1; // 门 ③:宠物池满
	SA::Model::Pet *dst = s.pets.resolve(handle);
	if (dst == nullptr)
	{
		(void)s.pets.release(handle); // 走不到(刚 allocate 成功);守它零成本
		return -1;
	}
	*dst = pet; // 整只落池(含 pet_skills 七槽 —— 模拟"这只宠从模板带技")
	// 主人反向引用(捕获路径同款;句柄带 generation ⇒ 主人换人后旧引用作废,M10)。
	dst->owner = s.player_of_session.find(session);
	// ── 提交:挂进主人的槽(捕获路径 `:391 CHAR_setCharPet` 同位)──
	p->pets[static_cast<std::size_t>(pet_slot)] = handle;
	return pet_slot;
}

bool World::setPlayerStatsForTest(SA::Net::SessionId session, std::int32_t hp,
                                  std::int32_t mp, std::int32_t vital,
                                  std::int32_t str, std::int32_t tough,
                                  std::int32_t dex)
{
	SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return false;
	p->hp = hp;
	p->mp = mp;
	p->vital = vital;
	p->str = str;
	p->tough = tough;
	p->dex = dex;
	return true;
}

bool World::giveGoldToPlayerForTest(SA::Net::SessionId session, std::int32_t amount)
{
	SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr || amount <= 0)
		return false;
	const auto tx = addGold(*p, GoldReason::kBattleReward, amount, /*trans=*/0, 0, *_impl);
	return tx.disposition != GoldDisposition::kRejected;
}

SA::Model::Player *World::playerForTest(SA::Net::SessionId session) noexcept
{
	return _impl->players.resolve(_impl->player_of_session.find(session));
}

void World::warpPlayerForTest(SA::Net::SessionId session, std::int32_t floor, std::int32_t x, std::int32_t y)
{
	_impl->warpPlayer(session, floor, x, y);
}

std::size_t World::battleCount() const noexcept { return _impl->battles.size(); }

std::size_t World::worldEnemyCount() const noexcept { return _impl->world_enemies.size(); }

std::vector<WorldEnemyPos> World::worldEnemies() const
{
	Impl &s = *_impl;
	std::vector<WorldEnemyPos> out;
	out.reserve(s.world_enemies.size());
	for (const auto &we : s.world_enemies)
	{
		const SA::Model::Enemy *e = s.enemies.resolve(we.handle);
		if (e == nullptr)
			continue; // 悬空(世界态理论上不会;守零成本)
		WorldEnemyPos p{};
		p.entity_id = encodeHandle(we.handle);
		p.floor = e->floor;
		p.x = e->x;
		p.y = e->y;
		p.dir = e->dir;
		p.image = e->base_image;
		out.push_back(p);
	}
	return out;
}

std::size_t World::warpPointCount() const noexcept
{
	return _impl->warp_points.size();
}

std::size_t World::npcCount() const noexcept
{
	return _impl->npc_entities.size();
}

const NpcEntity *World::findNpc(std::uint64_t id) const noexcept
{
	for (const auto &npc : _impl->npc_entities)
	{
		if (npc.id == id)
			return &npc;
	}
	return nullptr;
}

bool World::playerHasActiveWindow(SA::Net::SessionId id) const noexcept
{
	const auto it = _impl->conns.find(id);
	return it != _impl->conns.end() && it->second.active_window_id != 0;
}

std::uint32_t World::playerActiveWindowId(SA::Net::SessionId id) const noexcept
{
	const auto it = _impl->conns.find(id);
	if (it != _impl->conns.end())
		return it->second.active_window_id;
	return 0;
}

std::string World::playerLastWindowText(SA::Net::SessionId id) const
{
	const auto it = _impl->conns.find(id);
	if (it != _impl->conns.end())
		return it->second.last_window_text;
	return {};
}

bool World::playerHasNowEvent(SA::Net::SessionId id, int flag) const noexcept
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(id));
	if (p == nullptr)
		return false;
	return p->hasNowEvent(flag);
}

bool World::playerHasEndEvent(SA::Net::SessionId id, int flag) const noexcept
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(id));
	if (p == nullptr)
		return false;
	return p->hasEndEvent(flag);
}

// 遇敌命中后的开战组装(批次 W.4)——移植 `EN_recv`(`callfromcli.c:1249`)清走路串 +
//   `BATTLE_CreateVsEnemy(charaindex,0,-1)` 净核(`battle.c:2528`):
//   遇敌链(`pickEnemyGroup`→`rollEnemyList`)→ 建场 → 玩家入场 → 逐只敌人入场。
// ⚠️★ 遇敌链的 rng 用**世界 rng**(`s.random`)——原版 `ENEMY_getEnemy` 在建 battle **之前**、
//    用全局 `rand()`,不是战斗 rng(战斗此刻还没建;敌人四维生成才用战斗 rng,见 spawnEnemyToField)。
bool World::triggerEncounter(SA::Net::SessionId session, std::int32_t area_row)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;
	if (area_row < 0 ||
	    static_cast<std::size_t>(area_row) >= s.encount_areas.size())
		return false;
	const EncountArea &area = s.encount_areas[static_cast<std::size_t>(area_row)];

	// SSRC80 enemy.c:1421–1445 扫全部持有槽，包含装备位（不是仅背包段）。
	std::vector<std::int32_t> inventory;
	if (const auto *player = s.players.resolve(s.player_of_session.find(session)))
		for (auto handle : player->items)
			if (const auto *item = s.items.resolve(handle))
				inventory.push_back(item->item_id);
	const std::int32_t grow =
	    pickEnemyGroup(area, s.enemy_groups, inventory, s.world_rng);
	if (grow < 0)
		return false;

	// ── 选敌人列表(编组 → 敌人表行下标序列,含大怪布阵顺序)────────────────
	const std::vector<std::int32_t> rows =
	    rollEnemyList(s.enemy_groups[static_cast<std::size_t>(grow)], s.encounters,
	                  s.enemy_templates, area.enemy_max_num, s.world_rng);
	if (rows.empty())
		return false; // 无候选 ⇒ 本次不遇敌

	// ── 建场 + 玩家入场(Side[0] 首位)─────────────────────────────────
	SA::Rules::BattleField field{};
	field.at(0) = makePlayerCombatant(s.storage ? s.players.resolve(s.player_of_session.find(session)) : nullptr);
	const BattleId battle = startBattle(field);
	if (!joinBattle(battle, session, 0))
	{
		// ⚠️ 进不去 ⇒ 刚建的 battle 成孤儿(同 onSessionReady demo 分支):报出来。
		s.logger.log(SA::Platform::LogLevel::kError,
		             SA::Platform::LogEvent::kBattleJoinFailed,
		             {{"battle_id", battle},
		              {"session_id", session},
		              {"reason", std::string_view("encounter_join_failed")}});
		return false;
	}

	// ── 逐只敌人入场(Side[1] 起,baselevel=-1 野外摇号)──────────────────
	//   ★ `rollEnemyList` 已含大怪布阵顺序 ⇒ 第 i 只落敌方槽 `kSideOffset + i`。
	//   敌人按类型可用完整 10 格；玩家的 5 格上限不适用于敌人（F14）。
	int placed = 0;
	for (std::size_t i = 0;
	     i < rows.size() && placed < SA::Rules::kSideOffset; ++i)
	{
		const std::int32_t erow = rows[i];
		if (erow < 0 || static_cast<std::size_t>(erow) >= s.encounters.size())
			continue;
		const EnemyEncounter &enc = s.encounters[static_cast<std::size_t>(erow)];
		const std::int32_t trow = findEnemyTemplate(s.enemy_templates, enc.temp_no);
		if (trow < 0)
			continue; // 模板查不到 ⇒ 整只不放(同 rollEnemyList 内大怪布阵的处置)
		const EnemyTemplate &tmpl = s.enemy_templates[static_cast<std::size_t>(trow)];
		const std::uint8_t slot =
		    static_cast<std::uint8_t>(SA::Rules::kSideOffset + placed);
		if (spawnEnemyToField(battle, slot, tmpl, enc, /*baselevel=*/-1))
			++placed;
	}

	if (placed == 0)
	{
		// ⚠️ 选出了怪却一只都没落地(全被模板/槽门挡)⇒ 空战斗;tick 会判空侧结束,
		//    但这是异常路径,先记一笔(同 §10.4 那族"看起来做了、其实没写")。
		s.logger.log(SA::Platform::LogLevel::kWarn,
		             SA::Platform::LogEvent::kBattleJoinFailed,
		             {{"battle_id", battle},
		              {"session_id", session},
		              {"reason", std::string_view("encounter_no_enemy_placed")}});
	}
	// ★ 成功路径不额外 log:startBattle(kBattleStarted+kBattleSeed)/joinBattle/
	//   spawnEnemyToField 已各自记账,遇敌只是它们的调用者。
	return placed > 0;
}

// ══ 明雷开战(批次 W.5)═══════════════════════════════════════════════
//   移植 EV 事件链 EVENT_main → NPC_NPCEnemy_Encount → NPC_NPCEnemy_BattleIn →
//   BATTLE_CreateVsEnemy(player,_,enemy)(npc_npcenemy.c:672/674)的净核。
//   ★ 与暗雷 triggerEncounter 的关键区别见 Api.h 声明处:用世界态**已存在**的敌人实体,
//     转移 handle 所有权,不 allocate / 不 spawnEnemy / 不耗战斗 rng。
bool World::triggerNpcEnemyBattle(SA::Net::SessionId session, std::size_t world_enemy_idx)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;
	if (world_enemy_idx >= s.world_enemies.size())
		return false;
	// ★ 拷一份 WorldEnemy:下面要 erase(world_enemies),持有引用会失效。
	const Impl::WorldEnemy we = s.world_enemies[world_enemy_idx];
	SA::Model::Enemy *enemy = s.enemies.resolve(we.handle);
	if (enemy == nullptr)
		return false;
	// 记世界坐标:广播消失要在 erase 前用它(enterEnemyToField 只改战场投影,不动 Enemy 的 x/y)。
	const std::int32_t ex = enemy->x;
	const std::int32_t ey = enemy->y;

	// ── 建场 + 玩家入场(Side[0] 首位,同 triggerEncounter)──────────────────
	SA::Rules::BattleField field{};
	field.at(0) = makePlayerCombatant(s.storage ? s.players.resolve(s.player_of_session.find(session)) : nullptr);
	const BattleId battle = startBattle(field);
	if (!joinBattle(battle, session, 0))
	{
		s.logger.log(SA::Platform::LogLevel::kError,
		             SA::Platform::LogEvent::kBattleJoinFailed,
		             {{"battle_id", battle},
		              {"session_id", session},
		              {"reason", std::string_view("npcenemy_join_failed")}});
		return false;
	}

	// ── 明雷入场(敌方首槽 kSideOffset)——★ 用**已存在**的敌人实体,转移所有权 ──────────
	const auto bit = s.battles.find(battle);
	if (bit == s.battles.end())
		return false; // 走不到(startBattle 刚建),守它零成本(同 spawnEnemyToField)。
	BattleInstance &b = bit->second;
	const std::uint8_t slot = static_cast<std::uint8_t>(SA::Rules::kSideOffset);
	if (b.enemy_of_slot[slot].valid())
		return false; // 敌方首槽被占(空场刚建,不该发生)——守它。
	if (!enterEnemyToField(b.field, static_cast<int>(slot), *enemy))
		return false; // 入场门(槽越界/被占):空战斗留 tick 收尾,同 triggerEncounter。
	// ★★ 所有权转移:同一 EntityHandle 从世界态挪到战斗态(enemy_of_slot),不 allocate/不 release
	//    ⇒ 战斗结束按暗雷同一路径回池;enemyCount() 守恒(不是新建一只)。
	b.enemy_of_slot[slot] = we.handle;
	b.dp_battle = b.dp_battle || enemy->duelpoint > 0;
	s.pushBattleSnapshot(b);

	// ── 从世界态移除 + 广播消失(原版明雷进战斗态即从地图消失)───────────────────
	//   ⚠️ 先广播(用移除前的世界坐标)再 erase;broadcastEnemyDespawn 单向发给视野内玩家。
	s.broadcastEnemyDespawn(enemy->floor, ex, ey, encodeHandle(we.handle));
	s.world_enemies.erase(s.world_enemies.begin() +
	                      static_cast<std::ptrdiff_t>(world_enemy_idx));
	return true;
}

// ══ TransportEvents ═════════════════════════════════════════════
void World::onConnected(SA::Net::ConnectionId id)
{
	Impl &s = *_impl;
	Impl::Conn c;
	c.conn_id = id;
	// 1.5:SessionId == ConnectionId。⚠️ 阶段 2 加重连窗口时这条要断开 ——
	//    那正是 01 §5.2 把两者分开的理由。
	c.session = std::make_unique<SA::Net::Session>(
	    id, s.config.protocol_version, s.config.heartbeat_interval_ms, this);
	s.conns.emplace(id, std::move(c));

	s.logger.log(SA::Platform::LogLevel::kDebug,
	             SA::Platform::LogEvent::kConnectionAccepted, {{"conn_id", id}});
}

void World::onBytes(SA::Net::ConnectionId id, const std::uint8_t *data,
                    std::size_t n)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	if (it == s.conns.end())
		return;
	Impl::Conn &c = it->second;

	if (!c.reader.push(data, n))
	{
		s.logger.log(SA::Platform::LogLevel::kWarn,
		             SA::Platform::LogEvent::kFrameRejected,
		             {{"conn_id", id}, {"reason", std::string_view("buffer_limit")}});
		s.transport.close(id);
		return;
	}

	for (;;)
	{
		const std::uint8_t *payload = nullptr;
		std::uint32_t len = 0;
		const SA::Net::FrameStatus st = c.reader.next(&payload, &len);
		if (st == SA::Net::FrameStatus::kNeedMore)
			break;
		if (st != SA::Net::FrameStatus::kOk)
		{
			// ⚠️ kTooLarge / kEmpty 不可恢复:字节流已无法对齐(见 net/api.h)。
			s.logger.log(SA::Platform::LogLevel::kWarn,
			             SA::Platform::LogEvent::kFrameRejected,
			             {{"conn_id", id},
			              {"reason", std::string_view(
			                             st == SA::Net::FrameStatus::kTooLarge
			                                 ? "frame_too_large"
			                                 : "frame_empty")}});
			s.transport.close(id);
			return;
		}

		const bool ok = c.session->handleFrame(payload, len, c.outbound);
		c.reader.pop();
		if (!ok)
		{
			s.logger.log(SA::Platform::LogLevel::kWarn,
			             SA::Platform::LogEvent::kHandshakeRejected,
			             {{"conn_id", id},
			              {"msg_id", static_cast<std::uint64_t>(
			                             c.session->lastRejectMsgId())},
			              {"state", std::string_view(SA::Net::sessionStateName(
			                            c.session->state()))}});
			// ★ 先把已生成的出站字节发出去(可能含 HandshakeRejected),再关。
			if (!c.outbound.empty())
			{
				std::vector<std::uint8_t> bytes;
				bytes.swap(c.outbound);
				// send 失败可能同步释放 c；缓冲须由本次调用持有。
				(void)s.transport.send(id, bytes.data(), bytes.size());
			}
			s.transport.close(id);
			return;
		}
	}
}

void World::detachBattles(SA::Net::SessionId id)
{
	Impl &s = *_impl;
	const auto ph = s.player_of_session.find(id);
	std::vector<BattleId> abandoned;
	for (auto &entry : s.battles)
	{
		auto &battle = entry.second;
		const bool member = battle.slot_of.erase(id) != 0;
		if (member)
			syncPetState(battle, s.pets);
		for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
			if (ph.valid() && battle.player_of_slot[static_cast<std::size_t>(slot)] == ph)
			{
				battle.field.at(slot).occupied = false;
				battle.commands.present[slot] = false;
				battle.player_of_slot[static_cast<std::size_t>(slot)] = SA::Model::kNullHandle;
				if (slot % SA::Rules::kSideOffset < SA::Rules::kBattlePlayerMax)
				{
					exitPetFromField(battle.field, slot);
					battle.pet_of_slot[static_cast<std::size_t>(slot + SA::Rules::kBattlePlayerMax)] = SA::Model::kNullHandle;
				}
			}
		battle.members.erase(std::remove(battle.members.begin(), battle.members.end(), id), battle.members.end());
		if (member && battle.members.empty())
			abandoned.push_back(battle.id);
		else if (member)
			s.pushBattleSnapshot(battle);
	}
	for (auto battle : abandoned)
		s.retireBattle(battle);
}

void World::removeSession(SA::Net::ConnectionId id)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	if (it == s.conns.end())
		return;
	if (it->second.session != nullptr)
		it->second.session->close();

	// ── L2:释放该会话的 Player 实体及其宠物(批次 M.1)────────────────────
	//
	// ⚠️★★ **宠物必须一起释放**,否则 Pet 池只增不减:主人走了,它的宠物槽再没人看,
	//    而那些槽在池里仍然占用。★ 这不会有任何一处报错 —— 只会在跑够久之后表现为
	//    「捕获突然开始失败」(池满),而那时离真正的原因(这里没释放)已经很远。
	//    ⇒ 与 Player.h 里 `pets` 的注释是同一条的两半:那边说"释放 Pet 时要清槽",
	//      这边是唯一真正执行它的地方。
	const SA::Model::EntityHandle ph = s.player_of_session.find(id);
	detachBattles(id);

	if (SA::Model::Player *p = s.players.resolve(ph); p != nullptr)
	{
		// 里程碑②:先给视野内玩家发 CharDisappear + 从 olink 摘除(都要 p 的位置,须在释放前)。
		s.broadcastDespawn(id, p->floor, p->x, p->y);
		auto *fl = s.getFloor(p->floor);
		const auto &cur_map = fl ? fl->map : s.map;
		auto &cur_olink = fl ? fl->olink : s.olink;
		if (cur_map.inBounds(p->x, p->y))
		{
			const auto idx = cur_map.index(p->x, p->y);
			if (idx < cur_olink.size())
			{
				auto &cell = cur_olink[idx];
				cell.erase(std::remove(cell.begin(), cell.end(), id), cell.end());
			}
		}
		for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
		{
			if (!p->pets[i].valid())
				continue;
			// ★ release 对悬空句柄返回 false 且不做事(generation 校验)⇒ 无需先 resolve。
			(void)s.pets.release(p->pets[i]);
			(void)p->clearPetSlot(static_cast<int>(i));
		}
		for (std::size_t slot = 0; slot < p->items.size(); ++slot)
		{
			(void)s.items.release(p->items[slot]);
			(void)p->clearItemSlot(static_cast<int>(slot));
		}
		(void)s.players.release(ph);
	}
	s.player_of_session.erase(id);

	s.conns.erase(it);

	s.logger.log(SA::Platform::LogLevel::kDebug,
	             SA::Platform::LogEvent::kConnectionClosed, {{"conn_id", id}});
}

// ══ SessionHost ═════════════════════════════════════════════════
void World::onSessionReady(SA::Net::SessionId id)
{
	Impl &s = *_impl;
	s.logger.log(SA::Platform::LogLevel::kInfo,
	             SA::Platform::LogEvent::kHandshakeAccepted,
	             {{"session_id", id}});

	if (s.storage)
		return;

	// ── L2:会话就绪 ⇒ 该会话有了一个 Player 实体(批次 M.1)───────────────
	//
	// ⚠️★ **这是一处有意的临时形态,与 demo_battle 同族**:真玩法里 Player 实体是
	//    **选角**的产物(阶段 2,要 storage),握手只做认证。此处握手后就建,是为了让
	//    捕获的世界写有一个主人可挂 —— 而**不是**因为「握手 == 有角色」这句话是对的。
	//    ⇒ 阶段 2 接上选角时,这一段移到选角完成的回调里(与欠债 17 删 demo_battle 同期)。
	//
	// ★ 但它**不放在下面的 demo 分支里**:`demo_battle` 关掉时会话照样该有实体 ——
	//   「这条会话背后有个玩家」是会话事实,与要不要进 demo 战斗无关。
	if (!s.player_of_session.find(id).valid())
	{
		const SA::Model::EntityHandle ph = s.players.allocate();
		if (!ph.valid())
		{
			// ⚠️ 池满必须报出来,理由见 LogEvent::kEntityPoolExhausted。
			//   ★ 不 return:没有 L2 实体不该挡住会话本身(战斗事件流仍然能跑,
			//     只是捕获会在提交阶段失败并落 capture_commit_failed)。
			s.logger.log(SA::Platform::LogLevel::kError,
			             SA::Platform::LogEvent::kEntityPoolExhausted,
			             {{"session_id", id},
			              {"pool", std::string_view("player")},
			              {"capacity", static_cast<std::uint64_t>(kMaxPlayers)}});
		}
		else
		{
			s.player_of_session.insert(id, ph);
			// ── 出生点(批次 W.1)──────────────────────────────────────
			//   ⚠️★ 与名字同族的临时形态:真出生点来自存档 / 登录点(阶段 2)。
			//     1.5 没有 ⇒ 给 fixture 地图中心,让玩家有个能走的合法落点;
			//     ⇒ 阶段 2 接选角时由登录点坐标取代(与下面名字留空同期删/换)。
			if (SA::Model::Player *np = s.players.resolve(ph); np != nullptr)
			{
				if (!s.content_version.empty())
				{
					np->floor = s.character_defaults.player.floor;
					np->x = s.character_defaults.player.x;
					np->y = s.character_defaults.player.y;
					np->image = s.character_defaults.player.image;
					np->level = s.character_defaults.player.level;
					np->hp = s.character_defaults.player.hp;
					np->mp = s.character_defaults.player.mp;
					np->max_mp = s.character_defaults.player.max_mp;
				}
				else
				{
					np->floor = 0;
					np->x = s.map.width / 2;
					np->y = s.map.height / 2;
				}
				// 里程碑②:入 olink + 与视野内玩家双向 CharAppear(原版进图 sendCToArround)。
				auto *fl = s.getFloor(np->floor);
				const auto &cur_map = fl ? fl->map : s.map;
				auto &cur_olink = fl ? fl->olink : s.olink;
				if (cur_map.inBounds(np->x, np->y))
				{
					const auto idx = cur_map.index(np->x, np->y);
					if (idx < cur_olink.size())
						cur_olink[idx].push_back(id);
				}
				s.broadcastSpawn(id, *np);
				s.refreshEnemyView(id, *np, -1000, -1000);
				s.refreshNpcView(id, *np, -1000, -1000);
			}
			// ⚠️★ 名字**留空**:1.5 没有选角 ⇒ 没有名字的来源。
			//    ★ 不编一个 "player_1" 之类的占位 —— 那会让「名字是哪来的」看起来
			//      已经有答案了。11 §14 记的 DR-TS5 正是这么被撞出来的:定长 POD 强制
			//      回答「名字能多长」,而「名字从哪来」是同一族问题,同样该由裁定回答。
		}
	}

	// ── 1.4 demo 的入场装配(默认关,见 platform/api.h 的 DemoBattleConfig)──
	//
	// ⚠️★ 这是**脚手架**:真玩法里「握手完进哪里」是选角与登录点的结果(阶段 2,
	//    要 storage)。此处走捷径是为了让 1.4 有一条能被客户端走通的路径,
	//    而不是因为这条捷径是对的。⇒ 阶段 2 接上选角时整块删掉。
	if (!s.config.demo_battle.enabled)
		return;

	// ★ 每条会话开**自己的**一场,不共用:多会话共用一场就要回答
	//   "第二个人落在哪个槽""先来的打到一半后来的怎么进",那是组队/观战的玩法口径
	//   (阶段 2),不该由一段 demo 脚手架顺手定下来。
	const BattleId battle = startBattle(makeDemoField());
	s.battles.at(battle).demo = true;
	const std::uint8_t slot = s.config.demo_battle.slot;
	if (!joinBattle(battle, id, slot))
	{
		// ⚠️ 进不去要**报出来**。这条路径上 JoinBattle 的每一个 false 都意味着
		//    上面刚建的战斗成了没人看的孤儿,而客户端会停在"连上了但什么都没发生"
		//    —— 那正是 00 §10.4 说的静默错误。
		s.logger.log(SA::Platform::LogLevel::kError,
		             SA::Platform::LogEvent::kBattleJoinFailed,
		             {{"battle_id", battle},
		              {"session_id", id},
		              {"reason", std::string_view("demo_join_failed")}});
		return;
	}
	s.logger.log(SA::Platform::LogLevel::kDebug,
	             SA::Platform::LogEvent::kSessionStateChanged,
	             {{"session_id", id},
	              {"state", std::string_view("online")},
	              {"demo", true}});
}

void World::onBattleCommand(SA::Net::SessionId id,
                            const SA::Domain::BattleCommand &cmd)
{
	Impl &s = *_impl;
	const auto bit = s.battles.find(cmd.battle_id);
	if (bit == s.battles.end())
		return;
	BattleInstance &b = bit->second;

	// ⚠️ 指令必须指向**当前**回合。02 §1.3 的取向:不靠"下一个到达的包就是回复",
	//    这里同理 —— 迟到的上一回合指令若被采纳,会在新回合里执行一个过期的决定。
	if (cmd.turn != b.field.turn)
		return;

	const auto sit = b.slot_of.find(id);
	if (sit == b.slot_of.end())
		return;
	const std::uint8_t slot = sit->second;
	if (slot >= SA::Rules::kSlotCount || b.stats.finished || !b.field.at(slot).occupied || b.field.at(slot).dead)
		return;

	// ── I| 入口校验(原版 BattleCommandDispach 的 "I|" 分支,battle_command.c:395-422)──
	//
	// 原版在**指令接收时**就校验道具指令,不过的两处都把指令**降级为 WAIT**(:410-414,
	// BATTLE_COM_WAIT + C_OK)—— 不是丢包:单位照样就绪,回合不会等一个永远不来的指令。
	//   ① 持有(:408 `ITEM_CHECKINDEX`):槽下标空间 [0, kMaxItemHave) **含装备位**
	//      (`CHAR_CHECKITEMINDEX` char_base.c:987-991)且道具实体在池(悬空句柄同空槽)。
	//   ② 目标(:409 `ITEM_isTargetValid`,item.c:2091-2112):0..19 单体**恒过**
	//      (原版对单体目标连 itemtarget 都不看);20/21/22(全体侧/全场)要按道具表
	//      `ITEM_TARGET` 与本方侧别判 —— 效果表没有该列(区域道具属 D 线导入,见下)⇒
	//      本实现一律判无效。★ 已核实 itemset6.txt 的恢复药 itemtarget=OTHER
	//      (如小块肉 1234)⇒ 原版对它们同样拒 20/21/22 ⇒ 该降级对全部现有可用药一致;
	//      其余目标值(含 23..27 排段)原版即拒(:2113 return -1)。
	// ⚠️ 无效⇒降级 WAIT、**不产事件、不摇 rng、不扣道具**:L3 的 USE_ITEM 分支本就
	//    会跳过 power<=0(I.4),这里只是把「不执行」提前到指令语义层,行为并集不变。
	SA::Domain::BattleCommand stored = cmd;
	if (stored.command_kind == SA::Domain::BattleCommand::CommandKind::USE_ITEM)
	{
		bool valid = false;
		if (SA::Model::Player *owner = s.players.resolve(b.player_of_slot[slot]); owner != nullptr)
		{
			const std::uint32_t item_slot = stored.command.use_item.item_slot;
			const std::uint32_t target = stored.command.use_item.target;
			// ① 持有:槽下标空间含装备位;越界 / 空槽 / 悬空句柄都算不持有。
			if (item_slot < SA::Model::kMaxItemHave &&
			    s.items.resolve(owner->items[item_slot]) != nullptr)
			{
				// ② 目标:单体 0..19;20/21/22 与其余一律无效(见上 ② 的登记)。
				valid = target < static_cast<std::uint32_t>(SA::Rules::kSlotCount);
			}
		}
		if (!valid)
			stored.command_kind = SA::Domain::BattleCommand::CommandKind::WAIT;
	}

	// ── W| 入口校验(原版 BattleCommandDispach 的 "W|" 分支,battle_command.c:269-335,
	//    宠技指令的**持有门**;批次 B2)─────────────────────────────────────
	//
	// ⚠️★★ 原版 W| 的发起者身份(回源码复核 2026-09-15):`charaindex` 是**主人**
	//   (fd 对应的角色),`petnum = CHAR_getInt(charaindex, CHAR_DEFAULTPET)`、
	//   `petindex = CHAR_getCharPet(charaindex, petnum)` ⇒ 校验对象是**默认宠**,
	//   技能参数也取自它的宠技槽(`PETSKILL_GetArray(petindex, iNum)` →
	//   `CHAR_getPetSkill`)。我们的 PET_SKILL 载荷带的是 skill_id(不带槽位 iNum,
	//   IDL 不动)⇒ 持有门等价化为「默认宠的七槽里**存在**该 skill_id」。
	// ⚠️ 原版的门与降级(失败即 `CHAR_setWorkInt(petindex, WORKBATTLEMODE, C_OK)`,
	//   宠物按"无指令"处理 —— 我们等价化为**整条指令降级 WAIT**,与 I| 同款:
	//   不产事件、不摇 rng;L3 对 WAIT 本就无动作,行为并集不变):
	//   ① `CHAR_CHECKINDEX(petindex) == FALSE`(无默认宠 / 槽空 / 悬空句柄);
	//   ② `iNum < 0 || iNum >= CHAR_MAXPETSKILLHAVE`(槽下标域)—— 载荷无 iNum,
	//     由"七槽扫描"天然覆盖(不存在的槽位自然匹配不到);
	//   ③ `_PETSKILLBUG`(8.0 开)的主人生死门(ISDIE / HP<=0)—— 本函数入口已拒
	//     死槽指令(`b.field.at(slot).dead` ⇒ return),等价;
	//   ④ `checkErrorStatus(petindex)`(宠物状态门)⇒ 复用 `checkCanAct` 的宠物侧:
	//     宠物**在场**(宠位槽被占)时读其战场快照判;不在场时无快照面,
	//     与原版场外 WORK 值干净同理,门通过;
	//   ⑤ `_PETSKILLBUG` 的 CHAR_SLOT 转生门(`CHAR_TRANSMIGRATION < 1 &&
	//     iNum >= CHAR_SLOT`)—— ★ **有意不复刻并就地记明**:转生系统未移植
	//     (CHAR_SLOT 的语义本身也属 03 §11 欠债 1 的 693 字段清单),域缺失,
	//     不猜一个替代表达;
	//   ⑥ `_FIXWOLF` 的 id 600 狼人变身特判 —— ★ **有意不复刻**:8.0 的
	//     petskill2.txt(147 行,最大 id 652)里**不存在 id 600**(实测),
	//     该分支在投产数据上不可达。
	// ⚠️★ 不在此查宠技**效果表**:表外 id 的"指令不成立"落在结算面
	//   (projectPetSkill 查不到 ⇒ L3 整次跳过,B1 的既有语义)—— 持有门管
	//   「这只宠会不会」,效果表管「这招怎么算」,两道门各司其职。
	if (stored.command_kind == SA::Domain::BattleCommand::CommandKind::PET_SKILL)
	{
		bool valid = false;
		if (SA::Model::Player *owner = s.players.resolve(b.player_of_slot[slot]); owner != nullptr)
		{
			// 门 ①(持有):主人有默认宠,且其七槽里有该 skill_id。
			//   ⚠️ `default_pet` 越界 / 槽空 / 悬空句柄 ⇒ 无宠,门不过(同 CHECKINDEX)。
			const int dp = owner->default_pet;
			if (dp >= 0 && dp < static_cast<int>(SA::Model::kMaxPetHave))
			{
				const SA::Model::Pet *pet = s.pets.resolve(owner->pets[static_cast<std::size_t>(dp)]);
				if (pet != nullptr)
				{
					for (std::size_t i = 0; i < SA::Model::Pet::kPetSkillSlots; ++i)
					{
						if (pet->pet_skills[i] ==
						    static_cast<std::int32_t>(stored.command.pet_skill.skill_id))
						{
							valid = true;
							break;
						}
					}
				}
			}
			// 门 ④(宠物状态):宠物在场 ⇒ 复用 checkCanAct(DR-BT5 唯一真源)。
			if (valid)
			{
				const std::size_t pet_slot = static_cast<std::size_t>(slot) + SA::Rules::kBattlePlayerMax;
				if (pet_slot < SA::Rules::kSlotCount &&
				    b.field.at(static_cast<int>(pet_slot)).occupied)
				{
					valid = SA::Rules::checkCanAct(b.field.at(static_cast<int>(pet_slot))) ==
					        SA::Domain::CannotActReason::CANNOT_ACT_NONE;
				}
			}
		}
		if (!valid)
			stored.command_kind = SA::Domain::BattleCommand::CommandKind::WAIT;
	}

	// ★★ 集气中**不接受新指令**(批次 B2b):原版蓄力中宠物菜单置灰
	//   (battle_command.c:945 `BATTLE_IsCharge ⇒ BP_FLG_PET_MENU_OFF`)⇒ 集气单位
	//   在原版就不可能被重发指令,拍 / 完成击由存续的 COM1 自动走完。本仓 B1 起
	//   PET_SKILL 与主人共用指令槽 ⇒ 以"丢弃来令"表意:集气态存续期间
	//   (beats >= 0)到达的任何指令都被忽略,该槽按注入的合成指令行动。
	//   ⚠️ **有意偏差,如实登记**:原版主人自己的指令与宠指令是两条流,集气中
	//     主人照常行动;我们的槽位一体 ⇒ 集气中主人的来令也被丢弃
	//     (真实数据 N=1 ⇒ 蓄力一拍 + 完成击,至多两回合)。将来指令面
	//     拆出"宠物指令通道"后此处应随之收窄。
	//   ⚠️ 走到这里槽必然 occupied 且非 dead(函数入口已拒),残余集气态
	//     不会困住离场 / 阵亡的槽(它们的 slot_of 已被清除,进不到这里)。
	if (b.charge_of_slot[static_cast<std::size_t>(slot)].beats >= 0)
		return;

	b.commands.commands[slot] = stored;
	b.commands.present[slot] = true;
	// ── 忠犬守护的链接建立(批次 A-β d2)────────────────────────────────
	// ★★ **在这里,不在行动位** —— 原版在指令派发当场调 `PETSKILL_Use` →
	//    `PETSKILL_Guardian`(battle_command.c:361)建立链接,清除要等**下一回合**的
	//    `BATTLE_PreCommandSeq`(battle.c:3596-3598)⇒ 链接整回合有效,主人在宠物
	//    行动位**之前**挨打也照样被接管。见 `applyGuardianPetSkill` 卷首。
	// ⚠️ 只对**通过校验后真正存下**的指令建立(降级成 WAIT 的走不到这里,同源码:
	//    `PETSKILL_Use` 返回 FALSE 时原版根本不调它)。
	// ⚠️ 不摇 rng、不产事件 ⇒ 放在指令面不破坏"结算面只由 resolveAction 产事件"。
	applyGuardianPetSkill(b, static_cast<int>(slot), s.pet_skill_effects);
	if (b.command_deadline_sec == 0)
		b.command_deadline_sec = s.now_ms / 1000 + 120; // 首个 C_OK 后才启动，严格超时退出而非自动防御。
	SA::Domain::BattleTurnBegin ready{};
	ready.battle_id = b.id;
	ready.turn = b.field.turn;
	ready.ready_mask = readyMask(b);
	for (auto member : b.members)
		if (auto conn = s.conns.find(member); conn != s.conns.end() && conn->second.session)
			(void)conn->second.session->push(ready, conn->second.outbound);
}

void World::onWalk(SA::Net::SessionId id, const SA::Domain::WalkRequest &req)
{
	// 移植 lssproto_W_recv(callfromcli.c:503)的净核:防瞬移 + 碰撞预检 + 排走路串。
	//   ⚠️ 划外(各有归属):nuke 反作弊(:517,自由服魔改)· 交易模式门(:513,交易系统)·
	//      组队分支(walk_init:947,组队系统)。
	Impl &s = *_impl;
	const auto it = s.conns.find(id); // 1.5:SessionId == ConnectionId
	if (it == s.conns.end())
		return;
	if (s.inBattle(id))
	{
		it->second.walk_seq.clear();
		return;
	}
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (p == nullptr)
		return;

	// (0,0) 门(lssproto_W_recv:532):照抄 —— 原版历史调试门,坐标(0,0)直接忽略。
	//   ⚠️ 纪律⓪「照抄不声称要紧」:fixture 出生点在地图中心、用例避开(0,0),
	//      真实地图出生点也不在(0,0)。
	if (req.x == 0 && req.y == 0)
		return;

	// 防瞬移(:543):客户端声明坐标离服务端当前 >1 格 ⇒ 不信,按当前坐标处理。
	std::int32_t cx = req.x;
	std::int32_t cy = req.y;
	const std::int32_t ddx = p->x - cx;
	const std::int32_t ddy = p->y - cy;
	if (ddx > 1 || ddx < -1 || ddy > 1 || ddy < -1)
	{
		cx = p->x;
		cy = p->y;
	}
	// 碰撞预检(:552):声明的目标格不可走 ⇒ 忽略本次请求。
	//   ⚠️ 原版拉回当前后仍排串(direction 串会走回合法处);我们更严:目标非法直接不排,
	//      理由是 fixture 期无预测回滚需求,严格拒绝更好定位问题。真实客户端预测接入时再放宽。
	const auto *fl = s.getFloor(p->floor);
	const auto &cur_map = fl ? fl->map : s.map;
	if (!mapWalkable(cur_map, s.map_attr, cx, cy))
		return;

	// 排走路串(walk_init:948 → walk_start:891 setWorkChar WALKARRAY)。
	//   ⚠️ FixedStr<32> 已保证 ≤32(原版 walk_init:939 的长度门);实际逐步移动由
	//      kCharLoop 玩家段按 walksendinterval 消费(CHAR_walkcall)。
	it->second.walk_seq = std::string(req.direction.c_str());
	// Preserve the last step's deadline across requests; one-character packets
	// must obey the same walk interval as a multi-character route.
}

void World::onEvent(SA::Net::SessionId id, const SA::Domain::EventRequest &req)
{
	// 移植 lssproto_EV_recv(callfromcli.c:1405)→ EVENT_main(event.c:37)净核:
	//   算面前格 → 扫该格事件对象 → 命中明雷则开战。★ 本批只接 ENTITY_ENEMY 一路
	//   (原版 functbl[event] 是通用派发,传送点 warppoint 等其他事件族为后续预留)。
	Impl &s = *_impl;
	bool ok = false;

	// 只处理明雷(ENTITY_ENEMY);其他 event_type ⇒ ok=false(未接的事件族,不报错、不断连)。
	if (req.event_type ==
	    static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY))
	{
		const auto it = s.conns.find(id); // 1.5:SessionId == ConnectionId
		SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
		if (it != s.conns.end() && p != nullptr && req.dir < 8)
		{
			// 面前格 = 玩家**权威**坐标朝 dir 前一格(不信 req.x/y,同 onWalk 防瞬移;原版
			//   callfromcli.c:1402 CHAR_getCoordinationDir(dir, CHAR_X, CHAR_Y, 1, &fx, &fy))。
			const std::int32_t fx = p->x + kDirDelta[req.dir].dx;
			const std::int32_t fy = p->y + kDirDelta[req.dir].dy;
			const std::size_t we = s.worldEnemyAt(p->floor, fx, fy);
			if (we != s.world_enemies.size())
				ok = triggerNpcEnemyBattle(id, we);
		}
	}
	else if (req.event_type ==
	         static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC))
	{
		const auto it = s.conns.find(id);
		SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
		if (it != s.conns.end() && p != nullptr && req.dir < 8)
		{
			const std::int32_t fx = p->x + kDirDelta[req.dir].dx;
			const std::int32_t fy = p->y + kDirDelta[req.dir].dy;
			const std::size_t ni = s.npcAt(p->floor, fx, fy);
			if (ni != s.npc_entities.size())
			{
				const NpcEntity &npc = s.npc_entities[ni];
				if (npc.type == NpcType::kHealer)
				{
					// 1. 检查并扣除费用 (唯一写入口 GoldLedger, DR-EC3 余额不足拒绝)
					bool can_pay = true;
					if (npc.cost > 0)
					{
						if (p->gold < npc.cost)
						{
							can_pay = false;
						}
						else
						{
							const GoldTx tx = delGold(*p, GoldReason::kHealerFee, npc.cost,
							                          /*trans=*/0, static_cast<std::uint64_t>(id), s);
							if (tx.disposition != GoldDisposition::kApplied)
								can_pay = false;
						}
					}

					// 2. 满状态恢复 (原版 NPC_HealerAllHeal, npc_healer.c:109-141)
					if (can_pay)
					{
						// 玩家自身满血满蓝
						const auto p_stats = SA::Rules::deriveBaseStats(p->vital, p->str,
						                                                p->tough, p->dex);
						p->hp = p_stats.max_hp > 0 ? p_stats.max_hp : std::max(p->hp, 1);
						p->mp = p->max_mp;

						// 随行宠物满血满蓝
						for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
						{
							if (p->pets[i].valid())
							{
								SA::Model::Pet *pet = s.pets.resolve(p->pets[i]);
								if (pet != nullptr)
								{
									const auto pet_stats = SA::Rules::deriveBaseStats(
									    pet->vital, pet->str, pet->tough, pet->dex);
									pet->hp = pet_stats.max_hp > 0 ? pet_stats.max_hp : std::max(pet->hp, 1);
									pet->mp = pet->max_mp;
								}
							}
						}
						ok = true;
					}
				}
				else if (npc.type == NpcType::kTownPeople)
				{
					// 城镇居民对话 (原版 npc_townpeople.c:28-52)
					// 1. 切分逗号分隔的文案候选
					std::vector<std::string> candidates;
					std::size_t start = 0;
					while (start < npc.message.size())
					{
						const std::size_t comma = npc.message.find(',', start);
						if (comma == std::string::npos)
						{
							candidates.push_back(npc.message.substr(start));
							break;
						}
						candidates.push_back(npc.message.substr(start, comma - start));
						start = comma + 1;
					}
					if (candidates.empty() && !npc.message.empty())
						candidates.push_back(npc.message);

					// 2. 选择文案(多条文案按 world_rng 随机摇选，对应原版 rand() % tokennum + 1)
					std::string chosen;
					if (!candidates.empty())
					{
						if (candidates.size() == 1)
						{
							chosen = candidates[0];
						}
						else
						{
							const std::size_t idx = static_cast<std::size_t>(
							    s.world_rng.randMod(static_cast<int>(candidates.size())));
							chosen = candidates[idx];
						}
					}

					// 3. 组装并下发 WindowOpen 消息 (kind = MESSAGE, buttons = OK)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_MESSAGE;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::MESSAGE;
					win.body.message.wide = false;

					// 按换行符切分为多行 (MessageBody.lines 最多 16 行，每行最大 255 字符)
					std::size_t lstart = 0;
					while (lstart < chosen.size() && win.body.message.lines.size() < 16)
					{
						const std::size_t nl = chosen.find('\n', lstart);
						std::string line = (nl == std::string::npos)
						                       ? chosen.substr(lstart)
						                       : chosen.substr(lstart, nl - lstart);
						if (line.size() > 255)
							line.resize(255);
						if (auto *slot = win.body.message.lines.push_back())
							slot->assign(line.data(), line.size());
						if (nl == std::string::npos)
							break;
						lstart = nl + 1;
					}
					if (win.body.message.lines.empty())
					{
						std::string line = chosen;
						if (line.size() > 255)
							line.resize(255);
						if (auto *slot = win.body.message.lines.push_back())
							slot->assign(line.data(), line.size());
					}

					// 记录窗口会话状态 (DR-PR3)
					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = chosen;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kExChangeMan)
				{
					// ExChangeMan 任务事件 NPC (原版 npc_exchangeman.c, 09 §4)
					int matched_block_idx = -1;
					int matched_branch_idx = 0;

					auto count_item_cb = [](const SA::Model::Player &pl, std::int32_t item_id,
					                        void *userdata) -> std::int32_t
					{
						return static_cast<const World::Impl *>(userdata)->countPlayerItems(
						    pl, item_id);
					};
					auto count_pet_cb = [](const SA::Model::Player &pl, std::int32_t pet_id,
					                       std::int32_t min_lvl, void *userdata) -> std::int32_t
					{
						return static_cast<const World::Impl *>(userdata)->countPlayerPets(
						    pl, pet_id, min_lvl);
					};
					auto count_free_items_cb = [](const SA::Model::Player &pl,
					                              void *userdata) -> std::int32_t
					{
						return static_cast<const World::Impl *>(userdata)->countFreeItemSlots(pl);
					};
					auto count_free_pets_cb = [](const SA::Model::Player &pl,
					                             void *userdata) -> std::int32_t
					{
						return static_cast<const World::Impl *>(userdata)->countFreePetSlots(pl);
					};
					const EventCheckContext check_ctx{*p, count_item_cb, count_pet_cb,
					                                  count_free_items_cb, count_free_pets_cb, &s};

					for (std::size_t bi = 0; bi < npc.exchange_blocks.size(); ++bi)
					{
						const auto &blk = npc.exchange_blocks[bi];
						// 前置门: 若 event_no != -1 且已完成, 跳过该块 (C21)
						if (blk.event_no != -1 && p->hasEndEvent(blk.event_no))
							continue;

						const int branch = evaluateEventCondition(blk.condition, check_ctx);
						if (branch > 0)
						{
							matched_block_idx = static_cast<int>(bi);
							matched_branch_idx = branch;
							break;
						}
					}

					if (matched_block_idx >= 0)
					{
						const auto &blk = npc.exchange_blocks[static_cast<std::size_t>(matched_block_idx)];
						std::string door_msg;
						if (!s.checkExChangePreconditions(*p, blk, matched_branch_idx, door_msg))
						{
							it->second.pending_exchange = {};
							s.sendExChangeWindow(id, npc.id, door_msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							ok = true;
						}
						else if (blk.type == ExChangeType::kMessage)
						{
							// 立即结算全部副作用 (石币/道具/宠物/旗标)
							s.applyExChangeEffects(id, *p, blk, matched_branch_idx);

							std::string msg = blk.nomal_window_msg;
							if (msg.empty())
								msg = blk.nomal_msg;
							if (msg.empty())
								msg = blk.thanks_msg;

							it->second.pending_exchange = {};
							s.sendExChangeWindow(id, npc.id, msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							ok = true;
						}
						else if (blk.type == ExChangeType::kAccept)
						{
							// 弹出接取/交付确认窗
							std::string msg = blk.accept_msg;
							if (msg.empty())
								msg = blk.nomal_window_msg;
							if (msg.empty())
								msg = blk.nomal_msg;

							const std::uint32_t buttons =
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
							s.sendExChangeWindow(id, npc.id, msg, buttons);
							it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
							ok = true;
						}
					}
					else if (!npc.nomal_main_msg.empty())
					{
						// 全部块不满足 ⇒ 随机选择兜底对白
						std::vector<std::string> candidates;
						std::size_t start = 0;
						while (start < npc.nomal_main_msg.size())
						{
							const std::size_t comma = npc.nomal_main_msg.find(',', start);
							if (comma == std::string::npos)
							{
								candidates.push_back(npc.nomal_main_msg.substr(start));
								break;
							}
							candidates.push_back(npc.nomal_main_msg.substr(start, comma - start));
							start = comma + 1;
						}
						std::string chosen;
						if (candidates.size() == 1)
							chosen = candidates[0];
						else if (!candidates.empty())
						{
							const std::size_t idx = static_cast<std::size_t>(
							    s.world_rng.randMod(static_cast<int>(candidates.size())));
							chosen = candidates[idx];
						}
						else
							chosen = npc.nomal_main_msg;

						it->second.pending_exchange = {};
						s.sendExChangeWindow(id, npc.id, chosen,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						ok = true;
					}
				}
				else if (npc.type == NpcType::kShop)
				{
					// 商店 NPC 交互 (批次 W.12, 移植 npc_itemshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_ITEM_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "道具商店" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "欢迎光临！请选择你要购买的道具。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.item_full_msg.empty() ? "道具栏已满！" : npc.item_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充在售道具列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.shop_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.shop_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.item_id);
							entry->image_id = prod.image_id;
							entry->level = prod.level;
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kPetShop)
				{
					// 宠物商店 NPC 交互 (批次 W.13, 移植 npc_petshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_ITEM_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "宠物商店" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "欢迎光临宠物商店！请挑选您心仪的宠物。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.pet_full_msg.empty() ? "宠物栏已满！" : npc.pet_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充在售宠物列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.pet_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.pet_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.pet_id);
							entry->image_id = static_cast<std::uint32_t>(prod.image);
							entry->level = static_cast<std::uint32_t>(prod.level);
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_pet_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kPetSkillShop)
				{
					// 宠物技能导师 NPC 交互 (批次 W.13, 移植 npc_petskillshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_PET_SKILL_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "宠物技能导师" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "你好！我可以传授你的宠物强大的技能。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.skill_full_msg.empty() ? "宠物技能栏已满！" : npc.skill_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充教授技能列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.pet_skill_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.pet_skill_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.skill_id);
							entry->image_id = 0;
							entry->level = static_cast<std::uint32_t>(prod.level);
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_pet_skill_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kSignBoard)
				{
					// 告示牌 NPC 交互 (批次 W.14, 移植 npc_signboard.c)
					std::string title = npc.sign_title.empty() ? "＜　看板　＞\n" : (npc.sign_title + "\n");
					std::string body = npc.message.empty() ? npc.name : npc.message;
					std::string sign_text = title + body;
					s.sendExChangeWindow(id, npc.id, sign_text,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kWarpMan)
				{
					// 传送员 NPC 交互 (批次 W.14, 移植 npc_warpman.c)
					if (npc.warp_destinations.empty())
					{
						std::string msg = npc.warp_msg.empty() ? "暂无可以前往的目的地。" : npc.warp_msg;
						s.sendExChangeWindow(id, npc.id, msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						ok = true;
					}
					else if (npc.warp_destinations.size() == 1)
					{
						// 单目的地: 弹出确认窗口 (YES / NO)
						const auto &dest = npc.warp_destinations[0];
						std::string msg = npc.warp_msg;
						if (msg.empty())
						{
							msg = "确定要前往 " + dest.name + " 吗？需要花费 " + std::to_string(dest.cost) + " 石币。";
						}
						it->second.pending_warpman.npc_id = npc.id;
						it->second.pending_warpman.dest_idx = 0;
						s.sendExChangeWindow(id, npc.id, msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
						                         static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO));
						ok = true;
					}
					else
					{
						// 多目的地: 弹出 SELECT 窗口
						SA::Domain::WindowOpen win{};
						win.window_id = ++s.next_window_id;
						win.kind = SA::Domain::WindowKind::WINDOW_KIND_SELECT;
						win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
						win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
						win.source.entity_id = static_cast<std::uint32_t>(npc.id);
						win.body_kind = SA::Domain::WindowOpen::BodyKind::SELECT;

						std::string msg = npc.warp_msg.empty() ? "请选择你想前往的目的地：" : npc.warp_msg;
						if (msg.size() > 255)
							msg.resize(255);
						if (auto *slot = win.body.select.lines.push_back())
							slot->assign(msg.data(), msg.size());

						const std::size_t limit = std::min<std::size_t>(npc.warp_destinations.size(), 32);
						for (std::size_t i = 0; i < limit; ++i)
						{
							const auto &dest = npc.warp_destinations[i];
							if (auto *choice = win.body.select.choices.push_back())
							{
								choice->choice_id = static_cast<std::uint32_t>(i + 1); // 1-based choice_id
								std::string item_text = dest.name;
								if (dest.cost > 0)
								{
									item_text += " (" + std::to_string(dest.cost) + "石币)";
								}
								if (item_text.size() > 255)
									item_text.resize(255);
								choice->text.assign(item_text.data(), item_text.size());
								choice->enabled = (p->gold >= dest.cost && p->level >= dest.level);
							}
						}

						it->second.active_window_id = win.window_id;
						it->second.active_window_npc_id = npc.id;
						it->second.last_window_text = msg;
						it->second.pending_warpman.npc_id = npc.id;
						it->second.pending_warpman.dest_idx = -1;

						s.sendTo(id, win);
						ok = true;
					}
				}
			}
		}
	}

	// 回执(原版 lssproto_EV_send(fd, seqno, rc)):seqno 原样带回,ok = 是否命中并开战。
	//   ★ 靠 seqno 关联(不依赖传输层 corr_id),同原版 EV 的 seqno 机制。
	SA::Domain::EventResult res{};
	res.seqno = req.seqno;
	res.ok = ok;
	s.sendTo(id, res);
}

void World::onWindowReply(SA::Net::SessionId id, const SA::Domain::WindowReply &reply)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return;

	// 校验 window_id 是否匹配当前会话开启的活动窗口 (DR-PR3 / DR-PR8)
	if (it->second.active_window_id != 0 && it->second.active_window_id == reply.window_id)
	{
		// 检查是否存在待决 ExChange 上下文 (批次 W.9)
		if (it->second.pending_exchange.npc_id != 0 &&
		    it->second.pending_exchange.npc_id == reply.source.entity_id)
		{
			const auto pending = it->second.pending_exchange;
			it->second.pending_exchange = {};

			const NpcEntity *npc = findNpc(pending.npc_id);
			if (npc != nullptr && pending.block_index >= 0 &&
			    static_cast<std::size_t>(pending.block_index) < npc->exchange_blocks.size())
			{
				const auto &blk = npc->exchange_blocks[static_cast<std::size_t>(pending.block_index)];
				const bool is_yes = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES)) != 0 ||
				                    reply.button == static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);

				if (is_yes)
				{
					std::string door_msg;
					if (!s.checkExChangePreconditions(*p, blk, pending.branch_idx, door_msg))
					{
						s.sendExChangeWindow(id, npc->id, door_msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}

					// 执行全部副作用 (石币/道具/宠物/旗标)
					s.applyExChangeEffects(id, *p, blk, pending.branch_idx);

					std::string thanks = blk.thanks_msg;
					if (thanks.empty())
						thanks = blk.nomal_window_msg;

					if (!thanks.empty())
					{
						s.sendExChangeWindow(id, npc->id, thanks,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return; // 保持活动新窗口
					}
				}
			}
		}

		// 检查是否存在待决 Shop 商店交互 (批次 W.12)
		if (it->second.pending_shop.npc_id != 0 &&
		    it->second.pending_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_shop.npc_id;
			it->second.pending_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				const NpcEntity *npc = findNpc(shop_npc_id);
				const std::uint32_t entry_id = reply.result.entry_id;
				if (npc != nullptr && entry_id >= 1 && entry_id <= npc->shop_products.size())
				{
					const auto &prod = npc->shop_products[entry_id - 1];
					const std::int32_t price = std::max(1, static_cast<std::int32_t>(prod.cost * npc->buy_rate));

					// 门 ①: 石币是否充足 (DR-EC3 余额不足拒绝)
					if (p->gold < price)
					{
						std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
						s.sendExChangeWindow(id, npc->id, less_msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}

					// 门 ②: 背包是否有空槽
					if (p->findFreeItemSlot() < 0)
					{
						std::string full_msg = npc->item_full_msg.empty() ? "道具栏已满！" : npc->item_full_msg;
						s.sendExChangeWindow(id, npc->id, full_msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}

					// 门 ③: 道具池分配
					const auto h = s.items.allocate();
					if (h.valid())
					{
						auto *new_item = s.items.resolve(h);
						if (new_item != nullptr)
						{
							// 扣除石币 (走 GoldLedger, 汇 kShopBuy)
							(void)delGold(*p, GoldReason::kShopBuy, price,
							              /*trans=*/0, static_cast<std::uint64_t>(id), s);

							// 填充道具信息并入包
							new_item->uid = ++s.next_window_id;
							new_item->item_id = prod.item_id;
							new_item->name.assign(prod.name.c_str());
							new_item->cost = prod.cost;
							new_item->level = static_cast<std::int32_t>(prod.level);
							new_item->current_pile = 1;
							new_item->use_pile_nums = 1;

							const int slot = p->findFreeItemSlot();
							if (slot >= 0)
							{
								p->items[static_cast<std::size_t>(slot)] = h;
								s.sendExChangeWindow(id, npc->id, "购买成功！欢迎下次光临。",
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
								return;
							}
							else
							{
								s.items.release(h);
							}
						}
						else
						{
							s.items.release(h);
						}
					}
				}
			}
		}

		// 检查是否存在待决 PetShop 宠物商店交互 (批次 W.13)
		if (it->second.pending_pet_shop.npc_id != 0 &&
		    it->second.pending_pet_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_pet_shop.npc_id;
			it->second.pending_pet_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				(void)buyPetFromShop(id, shop_npc_id, reply.result.entry_id);
				return;
			}
		}

		// 检查是否存在待决 PetSkillShop 技能导师交互 (批次 W.13)
		if (it->second.pending_pet_skill_shop.npc_id != 0 &&
		    it->second.pending_pet_skill_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_pet_skill_shop.npc_id;
			it->second.pending_pet_skill_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				const NpcEntity *npc = findNpc(shop_npc_id);
				const std::uint32_t entry_id = reply.result.entry_id;
				if (npc != nullptr && entry_id >= 1 && entry_id <= npc->pet_skill_products.size())
				{
					const auto &prod = npc->pet_skill_products[entry_id - 1];
					int chosen_pet_slot = -1;
					for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
					{
						if (p->pets[i].valid())
						{
							chosen_pet_slot = static_cast<int>(i);
							break;
						}
					}
					if (chosen_pet_slot >= 0)
					{
						(void)learnPetSkill(id, shop_npc_id, chosen_pet_slot, prod.skill_id, /*skill_slot=*/-1);
						return;
					}
				}
			}
		}

		// 检查是否存在待决 WarpMan 传送员交互 (批次 W.14)
		if (it->second.pending_warpman.npc_id != 0 &&
		    it->second.pending_warpman.npc_id == reply.source.entity_id)
		{
			const auto pending = it->second.pending_warpman;
			it->second.pending_warpman = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel)
			{
				int target_idx = -1;
				if (pending.dest_idx >= 0)
				{
					// 单目的地 (Yes/No 确认弹窗)
					const bool is_yes = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES)) != 0 ||
					                    reply.button == static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
					if (is_yes)
					{
						target_idx = pending.dest_idx;
					}
				}
				else if (reply.result_kind == SA::Domain::WindowReply::ResultKind::CHOICE_ID)
				{
					// 多目的地 (SELECT 选项列表)
					if (reply.result.choice_id >= 1)
					{
						target_idx = static_cast<int>(reply.result.choice_id - 1);
					}
				}

				if (target_idx >= 0)
				{
					(void)warpPlayerByNpc(id, pending.npc_id, static_cast<std::size_t>(target_idx));
					return;
				}
			}
		}

		it->second.active_window_id = 0;
		it->second.active_window_npc_id = 0;
	}
}

bool World::sellItemToShop(SA::Net::SessionId id, std::uint64_t npc_id, int slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性与道具存在性检查
	if (slot < static_cast<int>(SA::Model::kStartItemArray) ||
	    slot >= static_cast<int>(SA::Model::kMaxItemHave))
		return false;

	const auto h = p->items[static_cast<std::size_t>(slot)];
	if (!h.valid())
		return false;

	auto *item = s.items.resolve(h);
	if (item == nullptr)
		return false;

	// 计算回购价格: 道具原价 * sell_rate (保底 1 石币)
	std::int32_t base_cost = item->cost;
	if (base_cost <= 0)
	{
		for (const auto &prod : npc->shop_products)
		{
			if (prod.item_id == item->item_id && prod.cost > 0)
			{
				base_cost = prod.cost;
				break;
			}
		}
	}
	if (base_cost <= 0)
		base_cost = 1;

	const std::int32_t unit_price = std::max(1, static_cast<std::int32_t>(base_cost * npc->sell_rate));
	const std::int32_t total_price = unit_price * std::max(1, item->current_pile);

	// 门: 随身石币上限检查 (DR-EC3 拒绝, 零改动)
	const std::int32_t cap = maxHaveGold(0);
	if (static_cast<std::int64_t>(p->gold) + total_price > cap)
	{
		std::string full_msg = npc->stone_full_msg.empty() ? "钱包装不下这么多石币！" : npc->stone_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行出售原子操作: 扣除并释放道具 + 增加石币 (走 GoldLedger, 源 kShopSell)
	p->clearItemSlot(slot);
	s.items.release(h);

	(void)addGold(*p, GoldReason::kShopSell, total_price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);
	return true;
}

bool World::buyPetFromShop(SA::Net::SessionId id, std::uint64_t npc_id, std::uint32_t entry_id)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	if (entry_id < 1 || entry_id > npc->pet_products.size())
		return false;

	const auto &prod = npc->pet_products[entry_id - 1];
	const std::int32_t price = std::max(1, static_cast<std::int32_t>(prod.cost * npc->buy_rate));

	// 门 ①: 石币是否充足 (DR-EC3 余额不足拒绝)
	if (p->gold < price)
	{
		std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
		s.sendExChangeWindow(id, npc->id, less_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ②: 宠物栏是否有空槽
	const int pet_slot = p->findFreePetSlot();
	if (pet_slot < 0)
	{
		std::string full_msg = npc->pet_full_msg.empty() ? "宠物栏已满！" : npc->pet_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ③: 宠物池分配
	const auto h = s.pets.allocate();
	if (!h.valid())
		return false;

	auto *dst = s.pets.resolve(h);
	if (dst == nullptr)
	{
		s.pets.release(h);
		return false;
	}

	// 扣除石币 (走 GoldLedger, 汇 kPetShopBuy)
	(void)delGold(*p, GoldReason::kPetShopBuy, price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);

	// 填充新宠物数据并落池
	dst->uid = ++s.next_window_id;
	dst->pet_id = prod.pet_id;
	dst->name.assign(prod.name.c_str());
	dst->level = prod.level;
	dst->hp = prod.hp;
	dst->mp = prod.mp;
	dst->max_mp = prod.mp;
	dst->vital = prod.vital;
	dst->str = prod.str;
	dst->tough = prod.tough;
	dst->dex = prod.dex;
	dst->origin_image = prod.image;
	dst->base_image = prod.image;
	dst->owner = s.player_of_session.find(id);
	dst->owner_char_name = p->name;

	p->pets[static_cast<std::size_t>(pet_slot)] = h;

	s.sendExChangeWindow(id, npc->id, "购买宠物成功！好好照顾它哦。",
	                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	return true;
}

bool World::sellPetToShop(SA::Net::SessionId id, std::uint64_t npc_id, int pet_slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性与宠物存在性检查
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;

	const auto h = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!h.valid())
		return false;

	auto *pet = s.pets.resolve(h);
	if (pet == nullptr)
		return false;

	// 计算回购价格: 宠物原价 * sell_rate (保底 1 石币)
	std::int32_t base_cost = 0;
	for (const auto &prod : npc->pet_products)
	{
		if (prod.pet_id == pet->pet_id && prod.cost > 0)
		{
			base_cost = prod.cost;
			break;
		}
	}
	if (base_cost <= 0)
	{
		base_cost = std::max(1, pet->level * 100);
	}

	const std::int32_t unit_price = std::max(1, static_cast<std::int32_t>(base_cost * npc->sell_rate));

	// 门: 随身石币上限检查 (DR-EC3 拒绝, 零改动)
	const std::int32_t cap = maxHaveGold(0);
	if (static_cast<std::int64_t>(p->gold) + unit_price > cap)
	{
		std::string full_msg = npc->stone_full_msg.empty() ? "钱包装不下这么多石币！" : npc->stone_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行出售原子操作: 扣除并释放宠物 + 增加石币 (走 GoldLedger, 源 kPetShopSell)
	p->clearPetSlot(pet_slot);
	s.pets.release(h);

	(void)addGold(*p, GoldReason::kPetShopSell, unit_price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);
	return true;
}

bool World::learnPetSkill(SA::Net::SessionId id, std::uint64_t npc_id, int pet_slot,
                          std::int32_t skill_id, int skill_slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetSkillShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性检查
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;

	const auto h = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!h.valid())
		return false;

	auto *pet = s.pets.resolve(h);
	if (pet == nullptr)
		return false;

	// 查找导师技能条目
	const PetSkillProduct *target_prod = nullptr;
	for (const auto &prod : npc->pet_skill_products)
	{
		if (prod.skill_id == skill_id)
		{
			target_prod = &prod;
			break;
		}
	}
	if (target_prod == nullptr)
		return false;

	// 门 ①: 宠物等级是否达标 (移植 npc_petskillshop.c 门槛)
	if (pet->level < target_prod->level)
	{
		std::string low_msg = npc->level_low_msg.empty() ? "宠物等级不足以学习此技能！" : npc->level_low_msg;
		s.sendExChangeWindow(id, npc->id, low_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ②: 是否已习得该技能 (不可重复学同一技能)
	for (std::size_t i = 0; i < SA::Model::Pet::kPetSkillSlots; ++i)
	{
		if (pet->pet_skills[i] == skill_id)
			return false;
	}

	// 门 ③: 目标技能槽位解析
	int target_slot = -1;
	if (skill_slot >= 0 && static_cast<std::size_t>(skill_slot) < SA::Model::Pet::kPetSkillSlots)
	{
		target_slot = skill_slot;
	}
	else
	{
		for (std::size_t i = 0; i < SA::Model::Pet::kPetSkillSlots; ++i)
		{
			if (pet->pet_skills[i] <= 0) // 0 或 -1 表示空槽
			{
				target_slot = static_cast<int>(i);
				break;
			}
		}
	}
	if (target_slot < 0)
	{
		std::string full_msg = npc->skill_full_msg.empty() ? "宠物技能栏已满！" : npc->skill_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ④: 石币是否充足
	const std::int32_t price = std::max(1, static_cast<std::int32_t>(target_prod->cost * npc->buy_rate));
	if (p->gold < price)
	{
		std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
		s.sendExChangeWindow(id, npc->id, less_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行扣除石币 (走 GoldLedger, 汇 kPetSkillFee)
	(void)delGold(*p, GoldReason::kPetSkillFee, price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);

	// 写入宠物技能槽
	pet->pet_skills[static_cast<std::size_t>(target_slot)] = skill_id;

	s.sendExChangeWindow(id, npc->id, "宠物成功学会了新技能！",
	                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	return true;
}

bool World::warpPlayerByNpc(SA::Net::SessionId id, std::uint64_t npc_id, std::size_t dest_idx)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kWarpMan)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 目的地有效性检查
	if (dest_idx >= npc->warp_destinations.size())
		return false;

	const auto &dest = npc->warp_destinations[dest_idx];

	// 等级门禁检查 (移植 npc_warpman.c)
	if (p->level < dest.level)
	{
		std::string low_msg = npc->level_low_msg.empty() ? "你的等级不足，无法前往该区域！" : npc->level_low_msg;
		s.sendExChangeWindow(id, npc->id, low_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 石币门禁检查 (路费, 移植 npc_warpman.c)
	if (p->gold < dest.cost)
	{
		std::string stone_msg = npc->stone_less_msg.empty() ? "你的石币不足以支付路费！" : npc->stone_less_msg;
		s.sendExChangeWindow(id, npc->id, stone_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 目标坐标可通行门禁检查 (同层时校验目标格通行性, 跨层时由目标地图管辖, 移植 npc_warpman.c)
	const bool same_floor = (dest.floor == p->floor);
	const auto *dst_fl = s.getFloor(dest.floor);
	const auto &dst_map = dst_fl ? dst_fl->map : s.map;
	if (dst_fl != nullptr)
	{
		if (!dst_map.inBounds(dest.x, dest.y) || !mapWalkable(dst_map, s.map_attr, dest.x, dest.y))
		{
			s.sendExChangeWindow(id, npc->id, "目标地点暂时无法通行！",
			                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
			return false;
		}
	}
	else if (same_floor && (!s.map.inBounds(dest.x, dest.y) || !mapWalkable(s.map, s.map_attr, dest.x, dest.y)))
	{
		s.sendExChangeWindow(id, npc->id, "目标地点暂时无法通行！",
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 扣除路费 (走 GoldLedger, 汇 kWarpFee)
	if (dest.cost > 0)
	{
		const GoldTx tx = delGold(*p, GoldReason::kWarpFee, dest.cost, /*trans=*/0,
		                          static_cast<std::uint64_t>(id), s);
		if (tx.disposition != GoldDisposition::kApplied)
		{
			return false;
		}
	}

	// 执行瞬移与视野同步
	s.warpPlayer(id, dest.floor, dest.x, dest.y);

	it->second.active_window_id = 0;
	it->second.active_window_npc_id = 0;
	it->second.pending_warpman = {};

	return true;
}

void World::onSessionClosed(SA::Net::SessionId id)
{
	_impl->logger.log(SA::Platform::LogLevel::kDebug,
	                  SA::Platform::LogEvent::kSessionStateChanged,
	                  {{"session_id", id},
	                   {"state", std::string_view("closed")}});
}

// ══ 观察面 ═══════════════════════════════════════════════════════
void World::requestShutdown() noexcept { _impl->shutdown_requested = true; }

bool World::stopped() const noexcept { return _impl->stopped; }

std::uint64_t World::ticks() const noexcept { return _impl->ticks; }

std::size_t World::sessionCount() const noexcept
{
	return _impl->conns.size();
}

const BattleStats *World::stats(BattleId id) const
{
	const auto it = _impl->battles.find(id);
	if (it != _impl->battles.end())
		return &it->second.stats;
	const auto done = _impl->finished_battles.find(id);
	return done == _impl->finished_battles.end() ? nullptr : &done->second.stats;
}

SA::Net::SessionState World::sessionState(SA::Net::SessionId id) const
{
	const auto it = _impl->conns.find(id);
	if (it == _impl->conns.end() || it->second.session == nullptr)
	{
		return SA::Net::SessionState::kClosed;
	}
	return it->second.session->state();
}

// ── L2 实体池的观察面(批次 M.1)─────────────────────────────────
std::size_t World::playerCount() const noexcept { return _impl->players.size(); }

std::size_t World::petCount() const noexcept { return _impl->pets.size(); }

// ── 敌人池的观察面(批次 M.4b)────────────────────────────────────
std::size_t World::enemyCount() const noexcept { return _impl->enemies.size(); }

// ── 道具池的观察面(批次 I.1)。⚠️ 本批恒 0(无写入者),见 Api.h 声明处。──
std::size_t World::itemCount() const noexcept { return _impl->items.size(); }

const SA::Model::Enemy *World::battleEnemyAt(BattleId id, std::uint8_t slot) const
{
	if (slot >= SA::Rules::kSlotCount)
		return nullptr;
	const auto it = _impl->battles.find(id);
	if (it == _impl->battles.end())
		return nullptr;
	// ⚠️ 句柄可能悬空(敌人已被捕 / 战斗已结束回池)⇒ resolve 返 nullptr,
	//    与"该槽本来就没有敌人"给出同一个答案。★ 这是有意的:调用方要区分
	//    两者的话该看 `enemyCount()` 或战场投影,而不是让本函数返回两种空值。
	return _impl->enemies.resolve(it->second.enemy_of_slot[slot]);
}

const SA::Model::Pet *World::playerPetAt(SA::Net::SessionId session,
                                         int pet_slot) const
{
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return nullptr;
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return nullptr;
	// ⚠️ 槽里的句柄可能悬空(宠物已回池)⇒ resolve 返 nullptr,与空槽同一个答案。
	return _impl->pets.resolve(p->pets[static_cast<std::size_t>(pet_slot)]);
}

int World::playerCaptureCount(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->capture_count);
}

int World::playerDefaultPet(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : p->default_pet;
}

int World::playerExp(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->exp);
}

int World::playerGold(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->gold);
}

int World::playerHp(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->hp);
}

int World::playerMp(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->mp);
}

World::PlayerPos World::playerPos(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return PlayerPos{}; // valid == false
	return PlayerPos{true, p->floor, p->x, p->y, p->dir};
}

int World::playerPetSlotsUsed(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return -1;
	int used = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (p->pets[i].valid())
			++used;
	}
	return used;
}

int World::playerItemSlotsUsed(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return -1;
	// ★ 只数背包段(装备位段不是"拿到的道具",两步分工同 findFreeItemSlot)。
	int used = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (p->items[i].valid())
			++used;
	}
	return used;
}

const SA::Model::Item *World::playerItemAt(SA::Net::SessionId session, int slot) const
{
	if (slot < 0 || static_cast<std::size_t>(slot) >= SA::Model::kMaxItemHave)
		return nullptr;
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return nullptr;
	// ⚠️ 槽里的句柄可能悬空(道具已回池)⇒ resolve 返 nullptr,与空槽同一个答案。
	return _impl->items.resolve(p->items[static_cast<std::size_t>(slot)]);
}

std::int32_t World::playerItemPile(SA::Net::SessionId session, int slot) const
{
	if (slot < 0 || static_cast<std::size_t>(slot) >= SA::Model::kMaxItemHave)
		return -1;
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return -1;
	const SA::Model::Item *it =
	    _impl->items.resolve(p->items[static_cast<std::size_t>(slot)]);
	return it == nullptr ? -1 : it->current_pile; // 空槽 / 悬空句柄 ⇒ -1
}

const SA::Rules::BattleField *World::battleField(BattleId id) const
{
	const auto it = _impl->battles.find(id);
	if (it != _impl->battles.end())
		return &it->second.field;
	const auto done = _impl->finished_battles.find(id);
	return done == _impl->finished_battles.end() ? nullptr : &done->second.field;
}

// ── 战场态宠物入场 / 离场(批次 M.2)──────────────────────────────
//
// 详注见 world/Api.h 的声明处:纯投影 + 站位判定,三道门照 `BATTLE_PetDefaultEntry`
// (展开视图 `battle.c:1402`)+ `BATTLE_NewEntry` 的 `CHAR_TYPEPET` 分支(`:932`)。
bool enterPetToField(SA::Rules::BattleField &field, int owner_field_slot,
                     const SA::Model::Pet &pet)
{
	// ── 门 ①:owner 必须在玩家段(每 side 前 kBattlePlayerMax 槽)──────────
	// ⚠️ 宠位(owner % kSideOffset >= kBattlePlayerMax)不能再带宠 —— 否则 owner+5
	//    会落到敌方半场或越界。
	if (owner_field_slot < 0 || owner_field_slot >= SA::Rules::kSlotCount)
		return false;
	if (owner_field_slot % SA::Rules::kSideOffset >= SA::Rules::kBattlePlayerMax)
		return false;

	// ── 门 ②:宠物存活(源码 battle.c:1418-1420 有效 && !ISDIE && HP>0)──────
	//    ISDIE 依赖未移植的状态系统 ⇒ 本批以 hp>0 为准,见 Api.h 声明处。
	if (pet.hp <= 0)
		return false;

	// ── 门 ③:目标宠位未被占(源码 NewEntry:975 ⇒ ENTRYMAX)──────────────
	const int pet_field_slot = owner_field_slot + SA::Rules::kBattlePlayerMax;
	SA::Rules::Combatant &dst = field.at(pet_field_slot);
	if (dst.occupied)
		return false;

	// ── 投影 Pet → Combatant(拿得到的字段)────────────────────────────
	// ★ 先清成干净单位,不留前一个占据该槽者的脏值 —— 离场只置 occupied=false,
	//   其余字段是旧的(同 EntityPool::allocate 发干净槽的理由)。
	dst = SA::Rules::Combatant{};
	dst.occupied = true;
	dst.kind = SA::Rules::CombatantKind::kPet;
	dst.slot = static_cast<std::uint8_t>(pet_field_slot);
	dst.level = pet.level;
	dst.hp = pet.hp;
	dst.mp = pet.mp;
	dst.max_mp = pet.max_mp;
	dst.luck = pet.luck;

	// ⚠️★★ 四属**按具名下标取,绝不按位置拷** —— 三套顺序两两不同(原版 CHAR_*AT
	//    火水地风 / Rules::Element 地水火风 / 相克表头 无火水地风),本项目已栽过两次。
	//    与 createPetFromCombatant 的反向映射同源。
	dst.elements[static_cast<int>(SA::Rules::Element::kEarth)] = pet.earth;
	dst.elements[static_cast<int>(SA::Rules::Element::kWater)] = pet.water;
	dst.elements[static_cast<int>(SA::Rules::Element::kFire)] = pet.fire;
	dst.elements[static_cast<int>(SA::Rules::Element::kWind)] = pet.wind;

	// ── 属性推导:四维 → 基础三围 + max_hp(DR-DT9,批次 M.3)────────────
	// ★ complianceParameter 已移植:Pet 的原始四维经 deriveBaseStats 推出战斗三围。
	//   装备加成不含(Pet 无装备,原版 ITEM_equipEffect 对宠物输入全 0 即恒等,DR-DT9)。
	const SA::Rules::DerivedStats stats =
	    SA::Rules::deriveBaseStats(pet.vital, pet.str, pet.tough, pet.dex);

	// ── ★★ 原始四维直拷(批次 L4.1)────────────────────────────────────
	// ⚠️★ **拷的是原始四维,不是推导出的三围** —— 状态系统的两条公式直接读四维:
	//    命中率的体力占比 `VITAL/(V+S+T+D)`(`battle_event.c:5088`)与毒的每回合
	//    掉血 `((Σ/100)-20)/4`(`battle.c:5251`)。⇒ 用 attack/defense 代入会得到
	//    **完全不同的数**,而两者都是"看起来合理"的正数 ⇒ 不会有任何一处报错。
	//  ★ 这是**拷贝不是计算**:四维的来源是 L2 实体(M.4a `rollSpawnStats` 已落),
	//    World 只负责搬过去(同 elements 那几行的分工)。
	dst.vital = pet.vital;
	dst.str = pet.str;
	dst.tough = pet.tough;
	dst.dex = pet.dex;

	dst.attack = stats.attack;
	dst.defense = stats.defense;
	dst.quick = stats.quick;
	dst.fix_dex = stats.quick;
	dst.max_hp = stats.max_hp;
	// ⚠️★ **HP 夹取(原版 char.c:3555 `HP = min(HP, WORKMAXHP)`)本批不做**,dst.hp 保留
	//    上面投影的 pet.hp。★★ **M.4b 后这条的理由换了,两条都记下来**:
	//    · M.2/M.3 时的理由(**已不再成立**):宠物四维无非 0 来源 ⇒ max_hp 恒 0 ⇒
	//      夹取 = 把血清零 =「叫得出即死」。M.4b 让捕获宠的四维有了源(从 `Model::Enemy` 拷)
	//      ⇒ 那种单位再夹血不会清零。⚠️ 但**一般宠创建**(`PET_createPet`)仍未移植 ⇒
	//      经那条路来的宠物四维照旧 0,所以旧风险只是缩小、没有消失。
	//    · ★ **现在的主理由**:夹取根本不属于"入场投影"这一步 —— 它在
	//      `CHAR_complianceParameter` 里,而那个函数在原版是**每回合准备阶段**
	//      逐角色重算三围时调的(`05` §2.3 第 4 件事 = `BATTLE_TurnParam`,**未移植**)。
	//      ⇒ 塞进入场是把回合准备的动作挪错了位置;等那一批落地时夹取跟它一起来。
	//      ★ `enterEnemyToField` 同处、同理由(那边连旧理由都不适用,只有这一条)。
	return true;
}

void exitPetFromField(SA::Rules::BattleField &field, int owner_field_slot)
{
	if (owner_field_slot < 0 || owner_field_slot >= SA::Rules::kSlotCount)
		return;
	if (owner_field_slot % SA::Rules::kSideOffset >= SA::Rules::kBattlePlayerMax)
		return;
	const int pet_field_slot = owner_field_slot + SA::Rules::kBattlePlayerMax;
	// ★ 只清占位,不置 dead(撤下不是战死)。
	field.at(pet_field_slot).occupied = false;
}

// ── 遇敌:坐标 → 区域 → 编组(批次 M.6)──────────────────────────────────
//
// 详注见 world/Api.h 的声明处。移植来源 `ENCOUNT_getEncountAreaArray`
// (`char/encount.c:370-392`)· `GROUP_getGroupArray`(`char/enemy.c:745-755`)·
// `ENEMY_getEnemy` 的前两段(`char/enemy.c:1289-1355`)。

std::int32_t findEncountArea(const std::vector<EncountArea> &areas, std::int32_t floor,
                             std::int32_t x, std::int32_t y)
{
	std::int32_t index = -1;
	for (std::size_t i = 0; i < areas.size(); ++i)
	{
		const EncountArea &a = areas[i];
		if (a.floor != floor)
			continue;

		// 闭区间 —— `PointInRect`(`util.c:1363`)是 `x <= px && px <= x + width`,
		// 而载入期的 width 不 +1 ⇒ 两者配对后语义是「x1..x2 两端都含」。
		// ⚠️ 写成 `px < a.x + a.width` 会让单点区域(width==0)永不匹配。
		if (x < a.x || x > a.x + a.width)
			continue;
		if (y < a.y || y > a.y + a.height)
			continue;

		// ⚠️★★ `zorder <= 0` 整行跳过 —— 那一列兼任启用开关(源码 :378)。
		//    实测 1050 行全部 > 0 ⇒ 本判据一次都不触发,仍然移植(理由见 Api.h)。
		if (a.zorder <= 0)
			continue;

		// ★ 严格 `>` ⇒ zorder 相等时保留**先遇到**的(源码 :382)。
		//   ⚠️ 顺序敏感,而表的顺序就是文件行序 ⇒ D 线入库时**不得重排行**。
		if (index < 0 || a.zorder > areas[static_cast<std::size_t>(index)].zorder)
			index = static_cast<std::int32_t>(i);
	}
	return index;
}

std::int32_t findEnemyGroup(const std::vector<EnemyGroup> &groups, std::int32_t group_id)
{
	for (std::size_t i = 0; i < groups.size(); ++i)
		if (groups[i].group_id == group_id)
			return static_cast<std::int32_t>(i);
	return -1;
}

std::int32_t pickEnemyGroup(const EncountArea &area, const std::vector<EnemyGroup> &groups,
                            const std::vector<std::int32_t> &player_item_ids,
                            SA::Rules::Random &rng)
{
	// 候选三元组:编组槽号 / 该槽权重 / 已解析的 group 行下标。
	// ★ 缓存行下标是**有意偏离**源码:原版抽中后又调一次 `GROUP_getGroupArray`
	//   (`:1354`)⇒ 第二次线性扫 1220 行。缓存不改变任何行为,只省那一次扫。
	std::array<std::int32_t, kEncountGroupMaxNum> weight{};
	std::array<std::int32_t, kEncountGroupMaxNum> row{};
	int found = 0;
	std::int32_t total = 0;

	const auto holds = [&player_item_ids](std::int32_t item_id)
	{
		return std::find(player_item_ids.begin(), player_item_ids.end(), item_id) != player_item_ids.end();
	};

	for (std::size_t i = 0; i < static_cast<std::size_t>(kEncountGroupMaxNum); ++i)
	{
		const std::int32_t gid = area.group_id[i];
		if (gid == -1)
			continue;

		const std::int32_t g = findEnemyGroup(groups, gid);
		if (g < 0)
			continue; // ★ 不照抄原版"坏组仍入选"那条路径,理由见 Api.h 声明处 ③

		const EnemyGroup &grp = groups[static_cast<std::size_t>(g)];

		// 两道道具门，调用方从全部有效持有槽（含装备）提供实际 ID。
		if (grp.appear_by_item_id != -1 && !holds(grp.appear_by_item_id))
			continue;
		if (grp.not_appear_by_item_id != -1 && holds(grp.not_appear_by_item_id))
			continue;

		weight[static_cast<std::size_t>(found)] = area.group_prob[i];
		row[static_cast<std::size_t>(found)] = g;
		total += area.group_prob[i];
		++found;
	}

	// ★ 源码在 `RAND` **之前**就 `return NULL`(`:1342`)⇒ 无候选时**不消耗 rng**。
	if (found <= 0)
		return -1;

	// 抽签:`r = RAND(0, Σ − 1)`(源码 :1340 的 `r_max--` + :1346)。
	// ⚠️★ 本表实测权重和恒 ≥ 1(min=1)⇒ 不会走到 DR-BT23 的退化区间;
	//    ★ 而下一批的 group 侧**会**(4 行权重和为 0)—— 那是 R.1 排在本批前的理由。
	const std::int32_t r = rng.rand(0, total - 1);

	// ⚠️★★ 上界是 `found - 1`:最后一个候选不参与判定,落空即取它兜底(源码 :1347)。
	// ★★ **实测这两处细节都是「等价写法」而不是行为判据**(2026-09-09 穷举验证,
	//    1..4 个槽 × 权重 {−1,0,1,2,3} × r 遍历 [0, Σ−1],共 2,580 组):
	//      · 上界写 `found - 1` 还是 `found` ⇒ **差异 0 组**
	//        (因为 `r <= Σ−1 < acc(最后)` ⇒ 最后一个必然命中);
	//      · `weight != 0` 这半个条件 ⇒ **差异 0 组**(它是**冗余**的:权重 0 的槽
	//        不会让 acc 增长,而 `r < acc_prev` 若成立,前一轮就已经 break 了)。
	//    ⇒ ★ 两者**照抄源码**(它们是源码原文),但**不要声称它们要紧** ——
	//      ⚠️ 本注释初稿写的是「`weight != 0` 那半个条件要紧:权重 0 的槽不该被选中,
	//      少了它就会选中它」,**那句话是错的**,由反向验证 + 穷举当场揭穿(`00` §9.0.37 ⑥)。
	int pick = found - 1;
	std::int32_t acc = 0;
	for (int i = 0; i < found - 1; ++i)
	{
		acc += weight[static_cast<std::size_t>(i)];
		if (weight[static_cast<std::size_t>(i)] != 0 && r < acc)
		{
			pick = i;
			break;
		}
	}

	return row[static_cast<std::size_t>(pick)];
}

// ── 遇敌:编组 → 敌人列表(批次 M.7)──────────────────────────────────────
//
// 详注见 world/Api.h 的声明处。移植来源 `ENEMY_getEnemy` 第三、四段
// (`char/enemy.c:1356-1466`)· `ENEMY_getEnemyArrayFromId`(`:519-528`)·
// `ENEMYTEMP_getEnemyTempArrayFromTempNo`(`:337-347`)。

std::int32_t findEnemyEncounter(const std::vector<EnemyEncounter> &encounters,
                                std::int32_t enemy_id)
{
	for (std::size_t i = 0; i < encounters.size(); ++i)
		if (encounters[i].enemy_id == enemy_id)
			return static_cast<std::int32_t>(i);
	return -1;
}

std::int32_t findEnemyTemplate(const std::vector<EnemyTemplate> &templates,
                               std::int32_t temp_no)
{
	for (std::size_t i = 0; i < templates.size(); ++i)
		if (templates[i].temp_no == temp_no)
			return static_cast<std::int32_t>(i);
	return -1;
}

std::vector<std::int32_t> rollEnemyList(const EnemyGroup &group,
                                        const std::vector<EnemyEncounter> &encounters,
                                        const std::vector<EnemyTemplate> &templates,
                                        std::int32_t enemy_max_num, SA::Rules::Random &rng)
{
	// ── 第三段:收候选(源码 :1356-1401)──────────────────────────────
	// work[] = 候选敌人表行下标;wr[] = 各自权重(CREATEPROB);
	// createenemynum = Σ CREATEMAXNUM(出场数上界的一半)。
	// ⚠️ NPC 事件改组(:1367-1383)与 ENEMY_RandomEnemyArray(:1385)均不做,理由见 Api.h 卷首。
	std::array<std::int32_t, kEnemyGroupSlotMaxNum> work{};
	std::array<std::int32_t, kEnemyGroupSlotMaxNum> wr{};
	int found = 0;
	std::int32_t total = 0;
	std::int32_t createenemynum = 0;

	for (std::size_t s = 0; s < static_cast<std::size_t>(kEnemyGroupSlotMaxNum); ++s)
	{
		const std::int32_t eid = group.enemy_id[s];
		if (eid == -1)
			continue;
		// ENEMY_ID → 敌人表行下标(原版运行期读载入缓存,我们现扫)。找不到 ⇒ 跳过该槽,
		// 等价原版载入期把该槽置 -1(`enemy.c:690-710`)。
		const std::int32_t e = findEnemyEncounter(encounters, eid);
		if (e < 0)
			continue;
		work[static_cast<std::size_t>(found)] = e;
		wr[static_cast<std::size_t>(found)] = group.create_prob[s];
		total += group.create_prob[s];
		createenemynum += encounters[static_cast<std::size_t>(e)].create_max_num;
		++found;
	}

	// ★ 源码在 RAND 之前就 return(:1399)⇒ 无候选时不消耗 rng(同 pickEnemyGroup)。
	if (found <= 0)
		return {};

	// 出场数上界 = min(区域上限, Σ CREATEMAXNUM);出场数 = RAND(1, 上界)(源码 :1400-1401)。
	// ★ 实测 CREATEMAXNUM min=1 ⇒ createenemynum ≥ 1 ⇒ 上界 ≥ 1 ⇒ 不触发退化区间。
	const std::int32_t cap =
	    enemy_max_num < createenemynum ? enemy_max_num : createenemynum;
	std::int32_t entrymax = rng.rand(1, cap);

	// ── 第四段:逐只抽 + 同族上限门 + 大怪布阵(源码 :1402-1465)──────────────
	// ⚠️ 产出保留定长 16 槽(kEnemyIndexTableMaxSize)+ -1 空位 —— 大怪换位要按位置
	//    读写(见 Api.h),vector 一路 push 做不到。末尾再裁成紧凑序列。
	std::array<std::int32_t, kEnemyIndexTableMaxSize> indextable{};
	indextable.fill(-1);
	const std::int32_t r_max = total - 1; // 源码 :1397 的 r_max--
	int bigcnt = 0;
	int i = 0;
	for (int loopcounter = 0; i < entrymax && loopcounter < 100; ++loopcounter)
	{
		// 权重抽签 —— 与 pickEnemyGroup 同构(found-1 兜底 + wr!=0,等价/冗余见 DR-DT13 ④)。
		const std::int32_t r = rng.rand(0, r_max);
		int pick = found - 1;
		std::int32_t acc = 0;
		for (int j = 0; j < found - 1; ++j)
		{
			acc += wr[static_cast<std::size_t>(j)];
			if (wr[static_cast<std::size_t>(j)] != 0 && r < acc)
			{
				pick = j;
				break;
			}
		}
		const std::int32_t row = work[static_cast<std::size_t>(pick)];

		// 同族上限门(源码 :1418-1428):
		//   cnt       = 索引表里已放入几只该行;
		//   samecount = 候选 work[] 里该行出现几次。
		//   cnt >= CREATEMAXNUM * samecount ⇒ 不再放它(i 不推进,loopcounter 推进)。
		int cnt = 0;
		for (int j = 0;
		     j < kEnemyIndexTableMaxSize && indextable[static_cast<std::size_t>(j)] != -1; ++j)
			if (indextable[static_cast<std::size_t>(j)] == row)
				++cnt;
		int samecount = 0;
		for (int k = 0; k < found; ++k)
			if (work[static_cast<std::size_t>(k)] == row)
				++samecount;
		if (cnt >= encounters[static_cast<std::size_t>(row)].create_max_num * samecount)
			continue;

		// 大怪布阵(源码 :1430-1464):查模板 E_T_SIZE。
		// ★ 模板查不到 ⇒ 整只不放(源码 i++ 在 ENEMYTEMP_CHECKINDEX 块内 ⇒ 此处 continue)。
		const std::int32_t t = findEnemyTemplate(
		    templates, encounters[static_cast<std::size_t>(row)].temp_no);
		if (t < 0)
			continue;

		if (templates[static_cast<std::size_t>(t)].size == kEnemySizeBig)
		{
			if (bigcnt >= 5)
			{
				// 前 5 位已满大怪 ⇒ 减少总出场数并跳过(源码 :1433-1436)。
				--entrymax;
				continue;
			}
			if (i > 4)
			{
				// 要放到第 6 位起 ⇒ 去前 5 位找第一只 NORMAL,与之交换(源码 :1437-1454)。
				int j = 0;
				bool swap_ready = false;
				for (; j < 5; ++j)
				{
					const std::int32_t front = indextable[static_cast<std::size_t>(j)];
					if (front == -1)
						break; // 对应 ENEMY_CHECKINDEX 失败
					const std::int32_t ft = findEnemyTemplate(
					    templates, encounters[static_cast<std::size_t>(front)].temp_no);
					if (ft < 0)
						break; // 对应 ENEMYTEMP_CHECKINDEX 失败
					if (templates[static_cast<std::size_t>(ft)].size == kEnemySizeNormal)
					{
						swap_ready = true;
						break;
					}
				}
				if (!swap_ready)
					continue; // 前 5 位无 NORMAL 可换出 ⇒ 本轮不放
				indextable[static_cast<std::size_t>(i)] = indextable[static_cast<std::size_t>(j)];
				indextable[static_cast<std::size_t>(j)] = row;
			}
			else
			{
				indextable[static_cast<std::size_t>(i)] = row;
			}
			++bigcnt;
		}
		else
		{
			indextable[static_cast<std::size_t>(i)] = row;
		}

		++i; // ★ 只在模板有效(t >= 0)时推进 —— 源码 :1463 在 CHECKINDEX 块内。
	}

	// 裁成实际长度(原版返回 int* + -1 结尾,我们返回紧凑 vector)。
	std::vector<std::int32_t> out;
	out.reserve(static_cast<std::size_t>(i));
	for (int k = 0; k < i; ++k)
		out.push_back(indextable[static_cast<std::size_t>(k)]);
	return out;
}

// ── 敌人生成与入场(批次 M.4b · 等级摇号 M.5)──────────────────────────
//
// 详注见 world/Api.h 的声明处。移植来源 `ENEMY_createEnemy`(展开视图
// `char/enemy.c:994-1180`),建 / 不建逐条见 `shared/model/Enemy.h` 文末。

// 等级摇号 —— 源码 :1034 的 `RAND(LV_MIN, LV_MAX)` + 载入期归一 :479-486。
//
// ★ 归一在此而非载入期的三条等价判据(幂等 / 不耗 rng / 只碰这两列)见 Api.h 声明处。
std::int32_t rollEncounterLevel(const EnemyEncounter &enc, SA::Rules::Random &rng)
{
	// ── 归一 ①(源码 :483):`lv_min == 0` ⇒ **取 lv_max**,不是"从 0 级起" ────
	//
	// ⚠️★ 这一条最容易被漏掉,而漏掉它的表现是**整批低等级怪变弱**而不报错:
	//    配 `0,18` 的行原版给固定 18 级,漏了归一就变成 `RAND(0,18)`。
	//    ★ 实测 `enemy1.txt` 2154 行里这条**一次都不触发**(`lv_min == 0` 为 0 行)——
	//      正因为如此,它只能靠手造数据的用例钉住;而"真数据跑得过"证明不了它。
	std::int32_t lo = enc.lv_min;
	const std::int32_t hi_raw = enc.lv_max;
	if (lo == 0)
		lo = hi_raw;

	// ── 归一 ②(源码 :484-485):写反了自动纠正 ──────────────────────────
	//
	// ⚠️★ **这一步的理由在 2026-09-09 换过一次(DR-BT23)**:原先写的是
	//    「它是 `Rules::Random::rand` 的 `lo <= hi` 前提的唯一保证者」——
	//    而退化区间现在**有定义**(返回 `lo` 且照常消耗一次)⇒ 那个理由失效。
	// ✅ 现判据:**原版在载入期就做了这两条归一**(`enemy.c:479-486`)
	//    ⇒ 删掉它就不是"引入 UB",而是**与原版行为不等价** —— 后者一样不可接受,
	//      且它是源码事实,不会再随接口口径变化(`00` §9.0.36 ④)。
	const std::int32_t lv_min = lo < hi_raw ? lo : hi_raw;
	const std::int32_t lv_max = lo < hi_raw ? hi_raw : lo;

	// ★ 闭区间 —— 原版 `RAND(x,y)` 展开后是 `x + (int)((y-x+1)*rand()/(RAND_MAX+1))`
	//   (`include/util.h:79`)⇒ 取得到 y。`Random::rand` 同语义,不必换算。
	// ⚠️ `lv_min == lv_max` 时**照样摇一次**(实测 1142/2154 行是这种):
	//    结果恒等于它,但**rng 被消耗了一次** ⇒ 不能"优化"成直接 return,
	//    那会让固定等级的怪与区间等级的怪走出不同长度的随机序列 ⇒ 回放对不上。
	return rng.rand(lv_min, lv_max);
}

// ── 敌人基础经验表(源码 `include/enemyexptbl.h` 的 `enemybaseexptbl[]`)──────────
//
// ★ 200 个硬编码值,下标 = level − 1(`kEnemyBaseExpTbl[0]` = 1 级基础经验)。
//   `enemyExp()` 里 `level--` 后取它,level < 1 或 > 200 越界 ⇒ 返 0(源码 :780)。
// ⚠️★★ **74 级是一处递减异常**:73 级 959 → 74 级 **956**(比前一个小),75 级又跳到 1012。
//    这是**原版数据的毛刺**(否则整表单调递增),照抄不修 —— 改成插值(如 985)是把猜测
//    固化,同「实测异常照抄」纪律。将来这表若走 D 线入库,导入器**不得**顺手纠正它。
constexpr std::array<std::int32_t, 200> kEnemyBaseExpTbl = {
    1,
    2,
    3,
    4,
    5,
    6,
    9,
    12,
    15,
    18, // level   1-10
    22,
    26,
    30,
    35,
    40,
    46,
    52,
    58,
    65,
    72, //        11-20
    79,
    87,
    95,
    104,
    113,
    122,
    131,
    141,
    151,
    162, //        21-30
    173,
    184,
    196,
    208,
    220,
    233,
    246,
    260,
    274,
    288, //        31-40
    303,
    318,
    333,
    348,
    365,
    381,
    398,
    415,
    432,
    450, //        41-50
    468,
    486,
    506,
    525,
    545,
    564,
    585,
    606,
    627,
    648, //        51-60
    670,
    692,
    714,
    737,
    760,
    784,
    808,
    832,
    857,
    882, //        61-70
    907,
    933,
    959,
    956, // ★★ 74 级递减异常:比上一个(73 级 959)小 —— 原版数据毛刺,照抄不修
    1012,
    1040,
    1067,
    1095,
    1123,
    1152, //        71-80
    1181,
    1210,
    1240,
    1270,
    1300,
    1331,
    1362,
    1394,
    1426,
    1458, //        81-90
    1490,
    1524,
    1557,
    1590,
    1625,
    1659,
    1694,
    1729,
    1764,
    1800, //       91-100
    1836,
    1872,
    1909,
    1946,
    1983,
    2021,
    2059,
    2097,
    2136,
    2175, //      101-110
    2214,
    2254,
    2294,
    2334,
    2374,
    2414,
    2455,
    2496,
    2537,
    2578, //      111-120
    2619,
    2661,
    2703,
    2745,
    2787,
    2829,
    2872,
    2915,
    2958,
    3000, //      121-130
    3043,
    3088,
    3132,
    3176,
    3220,
    3264,
    3309,
    3354,
    3399,
    3444, //      131-140
    3489,
    3535,
    3581,
    3627,
    3673,
    3719,
    3765,
    3812,
    3859,
    3906, //      141-150
    3953,
    4000,
    4047,
    4095,
    4143,
    4191,
    4239,
    4287,
    4335,
    4384, //      151-160
    4433,
    4482,
    4531,
    4580,
    4629,
    4679,
    4729,
    4779,
    4829,
    4879, //      161-170
    4929,
    4980,
    5031,
    5082,
    5133,
    5184,
    5235,
    5287,
    5339,
    5391, //      171-180
    5443,
    5495,
    5547,
    5599,
    5652,
    5705,
    5758,
    5811,
    5864,
    5917, //      181-190
    5970,
    6024,
    6078,
    6132,
    6186,
    6240,
    6295,
    6350,
    6405,
    6460, //      191-200
};

// 敌人身上的经验值 —— 1:1 移植 `ENEMY_getExp`(展开视图 `char/enemy.c:761-799`)。
//
// ★★ **签名不收 `EnemyEncounter`**:生效行(:796)只读模板 `tp` + 入参 level / rank,
//    敌人表行 `p` 只出现在 :795 那条被注释掉的旧式里(校正见 `Enemy.h` 文末 ⑥)。
// 公式(源码逐行):
//   :779  level--;                              ← 下标是「等级 − 1」
//   :780  越界(含 level<=0)返 0;
//   :786  rank<0||rank>5 ⇒ 归 0 档;
//   :787  rankBonus = ranktbl[rank].rank;        ← {2.5,2.0,1.5,1.0,0.5,0.0}(num 列不用,不建)
//   :789  alpha = (critical+counter+GET+poison+paralysis+sleep+stone+drunk+confusion)
//               / 100.0 + rare;                  ← ★ GET = capture_difficulty(一列两用)
//   :796  ret  = base[level] + (rankBonus + alpha) * (level+1);  ← level 已--,+1 复原成原等级
//   :797  return ret < 1 ? 1 : ret;              ← 保底 1
// ⚠️ 浮点类型贴源码:`/100.0` 是 double 除;最终整段在 float 域求值后截断成 int。
//    逐位一致依赖 sa_world 的 `-ffp-contract=off`(见 CMakeLists,与 sa_shared 同源)。
std::int32_t enemyExp(const EnemyTemplate &tmpl, std::int32_t level, std::int32_t rank)
{
	level -= 1;
	if (level < 0 || level >= static_cast<std::int32_t>(kEnemyBaseExpTbl.size()))
		return 0;

	static constexpr float kRankBonus[6] = {2.5f, 2.0f, 1.5f, 1.0f, 0.5f, 0.0f};
	if (rank < 0 || rank > 5)
		rank = 0;
	const float rank_bonus = kRankBonus[static_cast<std::size_t>(rank)];

	// ★ E_T_GET 项就是 `capture_difficulty`(Api.h 的 EnemyTemplate 已一列两用)。
	const std::int32_t resist_sum =
	    tmpl.critical + tmpl.counter + tmpl.capture_difficulty + tmpl.poison + tmpl.paralysis + tmpl.sleep + tmpl.stone + tmpl.drunk + tmpl.confusion;
	const float alpha =
	    static_cast<float>(static_cast<double>(resist_sum) / 100.0 + tmpl.rare);

	const float raw =
	    static_cast<float>(kEnemyBaseExpTbl[static_cast<std::size_t>(level)]) + (rank_bonus + alpha) * static_cast<float>(level + 1);
	const std::int32_t ret = static_cast<std::int32_t>(raw);
	return ret < 1 ? 1 : ret;
}

SA::Model::Enemy spawnEnemy(const EnemyTemplate &tmpl, const EnemyEncounter &enc,
                            std::int32_t baselevel, SA::Rules::Random &rng,
                            const SA::Rules::RulesConfig &cfg)
{
	SA::Model::Enemy out{};

	// ── 图号(源码 :1023-1024):两个槽同值 ──────────────────────────
	out.origin_image = tmpl.image;
	out.base_image = tmpl.image;

	// ── 等级(源码 :1030-1035)★ 两个分支都是原版 ─────────────────────
	//
	// ⚠️★★ **摇号必须在 `rollSpawnStats` 之前**(源码 :1034 早于 :1045 的 ±2 扰动)——
	//    顺序即语义:调换会让同种子下的四维整体变化,而**没有一处会报错**
	//    (同 M.4b 成长率取"扰动后、撒点前"那一刻的理由)。
	// ★ 因此这一段放在这里而不是函数开头:它必须在四维之前、图号之后无所谓。
	out.level = baselevel > 0 ? baselevel : rollEncounterLevel(enc, rng);

	// ── 四维 + 成长率(源码 :1045-1070)= DR-DT10 的 `rollSpawnStats` ───
	//
	// ★★ **这一行是欠债 25 的关闭点**:公式自 M.4a 起就在 `shared/rules`,
	//    但在此之前**没有任何调用方**。
	// ⚠️ 四步顺序(±2 → 打包成长率 → 撒 10 点 → PARAM_CAL)整个封在那个纯函数里,
	//    这里不得拆开或重排 —— 详见 `Progression.cpp` 的四步说明。
	// ⚠️★ 喂给它的是 `out.level`(**已决定的**等级)而不是 `baselevel` ——
	//    ★★ 传后者的后果**不是"算出 0 或负数"而是"算小一个数量级"**:
	//      coef = (level − 1) × lvup + init ⇒ level 0 时 = −4.5 + 10 = **5.5**(仍为正)
	//      ⇒ 乌力的 vital 会是 165 而不是 840。⚠️ 因此「四维 > 0」这种量级断言
	//      **抓不到这个错**,必须逐值 —— 这一条是 M.5 反向验证逼出来的
	//      (注入它时七条断言一条都没红,详见 `WorldTickTest` 同名用例)。
	const SA::Rules::SpawnStats rolled =
	    SA::Rules::rollSpawnStats(tmpl.stats, out.level, rng, cfg);
	out.vital = rolled.vital;
	out.str = rolled.str;
	out.tough = rolled.tough;
	out.dex = rolled.dex;
	out.growth_vital = rolled.growth_vital;
	out.growth_str = rolled.growth_str;
	out.growth_tough = rolled.growth_tough;
	out.growth_dex = rolled.growth_dex;

	// ── 四属性(源码 :1071-1074)────────────────────────────────────
	// ★ 模板侧已按 **地水火风** 具名(见 EnemyTemplate),此处逐字段对拷 ⇒
	//   顺序陷阱在类型层面就没有发生的余地。
	out.earth = tmpl.earth;
	out.water = tmpl.water;
	out.fire = tmpl.fire;
	out.wind = tmpl.wind;

	// ── AI(源码 :1075-1076)────────────────────────────────────────
	out.mod_ai = tmpl.mod_ai;
	// ★ `VARIABLEAI = 0` 照抄。⚠️ 它与"幸运"是同一个物理槽 —— 捕获会以幸运的名义
	//   把这个 0 拷进宠物(见 `Enemy.h` 卷首与 `createPetFromCapture` 同处)。
	out.variable_ai = 0;

	// ── 评级(源码 :1096-1097)──────────────────────────────────────
	// ★ 判据是**模板原始基数之和**,与本次摇号无关 ⇒ 传 `tmpl.stats` 而不是 `rolled`。
	//   ⚠️ 传 rolled 会让同模板摇出不同 rank 而没有一处报错,详见 `enemyRank` 声明处。
	out.pet_rank = SA::Rules::enemyRank(tmpl.stats);

	// ── 名字(源码 :1108-1110)──────────────────────────────────────
	out.name = tmpl.name;

	// ── 宠技槽(源码 :1092-1094;原始 8.5 树 `enemy.c:1204-1206`,批次 B2a)──
	// ★ 1:1 那个整组拷循环:`for(i) CharNew.unionTable.indexOfPetskill[i] =
	//   *(tp + E_T_PETSKILL1 + i)` —— 一处夹取 / 清洗都没有(0 = 无技能、-1 = 空槽、
	//   表外死引用全部照存,取值域讨论见 EnemyTemplate::pet_skills)。
	// ⚠️ 消费方:① 捕获时整组拷给宠物(pet.c:375-377 同款);② 敌人侧本批**不用**
	//   (fillEnemyCommands 只填普攻,敌人 AI 属后续批)。
	for (std::size_t i = 0; i < SA::Model::Enemy::kPetSkillSlots; ++i)
		out.pet_skills[i] = tmpl.pet_skills[i];

	// ── 模板号(源码 :1200 `CHAR_PETID = *(tp + E_T_TEMPNO)`)──────────
	// ★ `CHAR_PETID` 的值 = 模板号,捕获扣道具 `IsNeedCaptureItem` 据它查 `NeedEnemy[]` 表。
	//   ⚠️ 别与 `ENEMY_ID`(遇敌表,归 D 线)混,见 `Enemy.h` 的 `pet_id` 注释。
	out.pet_id = tmpl.temp_no;

	// ── 捕获相关(源码 :1165-1166)★ 两个 WORK 字段,来源两张表 ─────────
	//
	// ⚠️★★ **左右两边的来源不同,这一行是那个区分的落点**(M.5 把它接对了):
	//      `capturable`         ← 敌人表 `ENEMY_PETFLG`(c14,源码 :1165)
	//      `capture_difficulty` ← 模板表 `E_T_GET`     (源码 :1166)
	//    ⇒ 同一只怪在不同敌人表配置下可捕 / 不可捕,而难度跟着模板走。
	//    ★ M.4b 时 `capturable` 权宜地挂在 `EnemyTemplate` 上(敌人表未移植),
	//      现在归位。⚠️ 别把 `enc` 换成 `tmpl` —— 模板表 c38 也有个叫 `E_T_PETFLG`
	//      的列,而 `ENEMY_createEnemy` **从不读它**(见 Api.h 那条)。
	out.capturable = enc.capturable;
	out.capture_difficulty = tmpl.capture_difficulty;

	// ── 生命(源码 :1153 推导 → :1159 满血)──────────────────────────
	//
	// ★ `hp = deriveBaseStats(四维).max_hp` —— **不存 max_hp**(不造第二真源,
	//   同 `Model::Pet`);投影到战场时再推一次(`enterEnemyToField`)。
	// ⚠️★ 推导只吃四维、不吃等级(DR-DT9:公式里没有 level)⇒ 等级的作用**全部**
	//    发生在上面 `rollSpawnStats` 那一步。这条已在 M.3 纠正过一次文档分叉,别再写反。
	out.hp = SA::Rules::deriveBaseStats(out.vital, out.str, out.tough, out.dex).max_hp;

	// ⚠️ `mp` / `max_mp` 留 0 —— 源码从不写它们,默认模板里也是 0(见 `Enemy.h`)。

	// ── 战果:经验值 / 决斗点判定树(源码 :1028 + :1101-1107)★ 三值一棵树,不拆开 ────
	//
	// `duelpoint` 无条件写(源码 :1101);仅当 `duelpoint <= 0` 才给 `exp`(源码 :1102):
	//   `enc.exp != -1` 用敌人表值 · `enc.exp == -1` 哨兵 ⇒ 走 `enemyExp()`(源码 :1103-1107)。
	// ⚠️★ `duelpoint > 0` 的「决斗点怪」`exp` 保持默认 0 —— 那场结算走决斗点、不走经验
	//    (`battle.c:2267` 的 `dpbattle`),而决斗点分配本批未做(登记残缺,见 `Enemy.h`)。
	// ★ `enemyExp` 的 rank 传刚算好的 `out.pet_rank`(与源码 :1106 传 `enemyrank` 同一个值)。
	out.duelpoint = enc.duelpoint;
	if (enc.duelpoint <= 0)
	{
		out.exp = enc.exp != -1 ? enc.exp : enemyExp(tmpl, out.level, out.pet_rank);
	}

	// ── 预掉落道具(源码 :1210-1224)★ 千分率摇进敌人预掉落槽,道具域第三批 I.3 ──────
	//
	// ⚠️★ **必须在 `rollSpawnStats`(上方四维)之后摇** —— 源码顺序四维 :1067 → 掉落 :1210,
	//    其间无 rng 消耗 ⇒ 放这里(rank/name/hp/exp 之后)与源码同种子下逐位一致。
	// ★ 只在 `item_prob != 0` 的槽摇(源码 :1211 `if(ITEMPROB != 0)`)⇒ prob=0 不耗 rng
	//   ⇒ 未配掉落的敌人 rng 序列与本批之前一致(现有用例不受影响,同 I.1「默认恒 0」)。
	// ★ 千分率 `RAND(0,999) < prob`(源码 :1213,`_FIX_ITEMPROB` ON);紧凑存 item_id、
	//   保持摇号顺序(见 `Enemy.h` dropped_items 注释)。
	for (int i = 0; i < SA::Model::Enemy::kMaxDrops; ++i)
	{
		if (enc.item_prob[i] == 0)
			continue;
		if (rng.rand(0, 999) < enc.item_prob[i])
		{
			out.dropped_items[static_cast<std::size_t>(out.drop_count)] = enc.item[i];
			++out.drop_count;
		}
	}

	return out;
}

bool enterEnemyToField(SA::Rules::BattleField &field, int field_slot,
                       const SA::Model::Enemy &enemy)
{
	// ── 门 ①:敌人可使用任一侧完整的 10 格；只拒绝越界 ────────────
	if (field_slot < 0 || field_slot >= SA::Rules::kSlotCount)
		return false;

	// ── 门 ②:目标槽未被占(源码 `NewEntry:975` ⇒ ENTRYMAX)────────────
	SA::Rules::Combatant &dst = field.at(field_slot);
	if (dst.occupied)
		return false;

	// ── 投影 Enemy → Combatant ────────────────────────────────────────
	// ★ 先清成干净单位,不留前一个占据该槽者的脏值(同 enterPetToField)。
	dst = SA::Rules::Combatant{};
	dst.occupied = true;
	dst.kind = SA::Rules::CombatantKind::kEnemy;
	dst.slot = static_cast<std::uint8_t>(field_slot);
	dst.level = enemy.level;
	dst.hp = enemy.hp;
	dst.mp = enemy.mp;
	dst.max_mp = enemy.max_mp;

	// ⚠️★ `luck` **留 0** —— 敌人没有幸运这个属性(同槽异义,`Enemy.h` 卷首)。
	//    ★ 不写 `dst.luck = 0;` 这一行:结构默认就是 0,写出来反而像"我们决定填 0"。
	//    ⚠️ 后果是可观察的:`luck` 参与 DR-BT1 的量化前提(上限 25)⇒ 敌人在那些
	//      公式里恒取幸运 0。这是原版行为,不是我们省事。

	// ⚠️★★ 四属**按具名下标写,绝不按位置拷**(三套顺序两两不同,已栽过两次)。
	dst.elements[static_cast<int>(SA::Rules::Element::kEarth)] = enemy.earth;
	dst.elements[static_cast<int>(SA::Rules::Element::kWater)] = enemy.water;
	dst.elements[static_cast<int>(SA::Rules::Element::kFire)] = enemy.fire;
	dst.elements[static_cast<int>(SA::Rules::Element::kWind)] = enemy.wind;

	// ── 属性推导:四维 → 基础三围 + max_hp(DR-DT9)────────────────────
	const SA::Rules::DerivedStats stats =
	    SA::Rules::deriveBaseStats(enemy.vital, enemy.str, enemy.tough, enemy.dex);

	// ── ★★ 原始四维直拷(批次 L4.1,理由同 enterPetToField)──────────────
	dst.vital = enemy.vital;
	dst.str = enemy.str;
	dst.tough = enemy.tough;
	dst.dex = enemy.dex;

	dst.attack = stats.attack;
	dst.defense = stats.defense;
	dst.quick = stats.quick;
	dst.fix_dex = stats.quick;
	dst.max_hp = stats.max_hp;
	// ⚠️ HP 不夹取 —— 理由**与 enterPetToField 不同**,见 Api.h 声明处:
	//    夹取属回合准备阶段的 complianceParameter(`BATTLE_TurnParam`,未移植)。

	// ── ★★ 捕获修正:两个字段第一次有了真数据(源码 :1165-1166)──────────
	//
	// `Combatant.h` 里写着「1.5 无敌人数值表 ⇒ 调用方按 30 兜底 / 一律 false」,
	// 本函数就是那个"将来的调用方"。⇒ 兜底值从此不该再出现在这条路径上。
	dst.mods.capturable = enemy.capturable;
	dst.mods.capture_difficulty = enemy.capture_difficulty;

	// ⚠️ `immune_critical` / `immune_knockback`(DR-BT11 的数据驱动标志)**仍留 false**:
	//    它们的来源是敌人数值表里的免疫标记,而那属 L4 内容导入(D 线)——
	//    ★ 与 `capturable` 不同,后者在 `enemy.txt` / `enemybase1.txt` 里有明确的列
	//    (`ENEMY_PETFLG` / `E_T_GET`),前者在原版**根本没有列**(原版硬编码图号)
	//    ⇒ 那一列是 DR-BT11 要求**新造**的,得等内容表定型,不是从模板里读出来的。
	return true;
}

SA::Domain::CharacterRecord World::Impl::snapshot(SA::Net::SessionId id) const
{
	SA::Domain::CharacterRecord record{};
	const auto found = conns.find(id);
	const auto *player = players.resolve(player_of_session.find(id));
	if (found == conns.end() || !player)
		return record;
	record.schema_ver = 1;
	record.char_id = found->second.char_id;
	record.revision = found->second.revision;
	SA::Domain::copyPlayerData(*player, record.player);
	for (std::size_t slot = 0; slot < player->pets.size(); ++slot)
		if (const auto *pet = pets.resolve(player->pets[slot]))
		{
			SA::Domain::PetSlot value{};
			value.uid = pet->uid;
			value.slot = static_cast<std::uint32_t>(slot);
			SA::Domain::copyPetData(*pet, value.value);
			(void)record.pets.push_back(value);
		}
	for (std::size_t slot = 0; slot < player->items.size(); ++slot)
		if (const auto *item = items.resolve(player->items[slot]))
		{
			SA::Domain::ItemSlot value{};
			value.uid = item->uid;
			value.slot = static_cast<std::uint32_t>(slot);
			SA::Domain::copyItemData(*item, value.value);
			(void)record.items.push_back(value);
		}
	return record;
}

bool World::Impl::install(SA::Net::SessionId id, const SA::Domain::CharacterRecord &record)
{
	if (!SA::SessionStorage::validRecord(record) || record.player.floor != character_defaults.player.floor ||
	    !mapWalkable(map, map_attr, record.player.x, record.player.y) || player_of_session.find(id).valid())
		return false;
	const auto handle = players.allocate();
	auto *player = players.resolve(handle);
	if (!player)
		return false;
	SA::Domain::copyPlayerData(record.player, *player);
	const auto rollback = [&]
	{
		for (auto value : player->pets)
			(void)pets.release(value);
		for (auto value : player->items)
			(void)items.release(value);
		(void)players.release(handle);
	};
	for (const auto &value : record.pets)
	{
		const auto allocated = pets.allocate();
		auto *pet = pets.resolve(allocated);
		if (!pet)
		{
			rollback();
			return false;
		}
		SA::Domain::copyPetData(value.value, *pet);
		pet->uid = value.uid;
		pet->owner = handle;
		player->pets[value.slot] = allocated;
	}
	for (const auto &value : record.items)
	{
		const auto allocated = items.allocate();
		auto *item = items.resolve(allocated);
		if (!item)
		{
			rollback();
			return false;
		}
		SA::Domain::copyItemData(value.value, *item);
		item->uid = value.uid;
		item->owner = handle;
		player->items[value.slot] = allocated;
	}
	player_of_session.insert(id, handle);
	auto &conn = conns.at(id);
	conn.char_id = record.char_id;
	conn.revision = record.revision;
	conn.session->markOnline();
	auto *fl = getFloor(player->floor);
	const auto &cur_map = fl ? fl->map : map;
	auto &cur_olink = fl ? fl->olink : olink;
	if (cur_map.inBounds(player->x, player->y))
	{
		const auto idx = cur_map.index(player->x, player->y);
		if (idx < cur_olink.size())
			cur_olink[idx].push_back(id);
	}
	broadcastSpawn(id, *player);
	refreshEnemyView(id, *player, -1000, -1000);
	refreshNpcView(id, *player, -1000, -1000);
	return true;
}

void World::configurePlayable(GridMap map, TileAttrTable attributes, std::string version,
                              SA::Domain::CharacterRecord defaults)
{
	auto &s = *_impl;
	if (!s.conns.empty() || !s.world_enemies.empty() || map.width <= 0 || map.height <= 0 ||
	    map.width > 2048 || map.height > 2048 ||
	    map.tile.size() != static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height) ||
	    map.obj.size() != map.tile.size() || !mapWalkable(map, attributes, defaults.player.x, defaults.player.y) ||
	    version.empty() || version.size() > 63)
		throw std::invalid_argument("invalid playable content");
	s.map = std::move(map);
	s.map_attr = std::move(attributes);
	s.olink.assign(s.map.tile.size(), {});
	s.content_version = std::move(version);
	s.character_defaults = defaults;
}

void World::onLogin(SA::Net::SessionId id, const SA::Transport::LoginRequest &login, std::uint64_t corr)
{
	auto &s = *_impl;
	auto &conn = s.conns.at(id);
	SA::SessionStorage::Request request{};
	request.operation = SA::SessionStorage::Operation::kLogin;
	request.session = id;
	request.correlation = corr;
	request.login = login;
	if (s.storage && !s.shutdown_requested && s.now_ms >= conn.login_after && !conn.pending && s.storage->submit(std::move(request)))
	{
		conn.pending = true;
		conn.login_after = s.now_ms + 1000;
		return;
	}
	SA::Transport::LoginResult result{};
	result.code = SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE;
	(void)SA::Net::encodeFramed(corr, result, conn.outbound);
	conn.session->awaitLogin();
}

void World::onCreateCharacter(SA::Net::SessionId id, const SA::Transport::CreateCharacterRequest &req, std::uint64_t corr)
{
	auto &s = *_impl;
	auto &conn = s.conns.at(id);
	const std::int64_t points = static_cast<std::int64_t>(req.vital) + req.str + req.tough + req.dex;
	const std::int64_t elements = static_cast<std::int64_t>(req.earth) + req.water + req.fire + req.wind;
	bool valid = !req.name.empty() && req.name.size() <= 31 && req.image == s.character_defaults.player.image &&
	             SA::Data::Json::validUtf8(std::string_view(req.name.data, req.name.size()));
	for (std::size_t i = 0; i < req.name.size(); ++i)
		if (static_cast<unsigned char>(req.name.data[i]) < 0x20 || req.name.data[i] == 0x7f)
			valid = false;
	for (auto point : {req.vital, req.str, req.tough, req.dex})
		if (point < 0 || point > 20)
			valid = false;
	for (auto element : {req.earth, req.water, req.fire, req.wind})
		if (element < 0 || element > 10)
			valid = false;
	valid = valid && points == 20 && elements == 10 && !(req.earth && req.fire) && !(req.water && req.wind);
	SA::SessionStorage::Request request{};
	request.operation = SA::SessionStorage::Operation::kCreate;
	request.session = id;
	request.correlation = corr;
	request.character = s.character_defaults;
	auto &player = request.character.player;
	player.name = req.name;
	if (valid)
	{
		player.vital = req.vital * 100;
		player.str = req.str * 100;
		player.tough = req.tough * 100;
		player.dex = req.dex * 100;
		player.earth = req.earth * 10;
		player.water = req.water * 10;
		player.fire = req.fire * 10;
		player.wind = req.wind * 10;
		player.hp = SA::Rules::deriveBaseStats(player.vital, player.str, player.tough, player.dex).max_hp;
		for (auto &pet : request.character.pets)
			pet.value.owner_char_name = player.name;
	}
	if (s.storage && conn.logged_in && !conn.pending && valid && s.storage->submit(std::move(request)))
	{
		conn.pending = true;
		return;
	}
	SA::Transport::CharacterResult result{};
	result.code = valid ? SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE : SA::Transport::AccountCode::ACCOUNT_INVALID;
	(void)SA::Net::encodeFramed(corr, result, conn.outbound);
	conn.session->selectCharacter();
}

void World::onSelectCharacter(SA::Net::SessionId id, const SA::Transport::SelectCharacterRequest &req, std::uint64_t corr)
{
	auto &s = *_impl;
	auto &conn = s.conns.at(id);
	SA::SessionStorage::Request request{};
	request.operation = SA::SessionStorage::Operation::kSelect;
	request.session = id;
	request.correlation = corr;
	request.character.char_id = req.char_id;
	if (s.storage && conn.logged_in && !conn.pending && req.char_id && s.storage->submit(std::move(request)))
	{
		conn.pending = true;
		return;
	}
	SA::Transport::CharacterResult result{};
	result.code = SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE;
	(void)SA::Net::encodeFramed(corr, result, conn.outbound);
	conn.session->selectCharacter();
}

void World::onSave(SA::Net::SessionId id, const SA::Transport::SaveRequest &req, std::uint64_t corr)
{
	auto &s = *_impl;
	auto &conn = s.conns.at(id);
	if (s.inBattle(id))
	{
		SA::Transport::SaveResult result{};
		result.code = SA::Transport::AccountCode::ACCOUNT_IN_BATTLE;
		result.logout = req.logout;
		(void)SA::Net::encodeFramed(corr, result, conn.outbound);
		return;
	}
	saveCharacter(id, req.logout, corr);
}

void World::saveCharacter(SA::Net::SessionId id, bool logout, std::uint64_t correlation)
{
	auto &s = *_impl;
	auto found = s.conns.find(id);
	if (!s.storage || found == s.conns.end() || !found->second.logged_in)
		return;
	auto &conn = found->second;
	if (conn.pending)
	{
		if (correlation || logout)
		{
			conn.deferred_correlation = correlation;
			conn.deferred_logout = logout;
		}
		return;
	}
	SA::SessionStorage::Request request{};
	request.operation = conn.char_id ? SA::SessionStorage::Operation::kSave : SA::SessionStorage::Operation::kRelease;
	request.session = id;
	request.correlation = correlation;
	request.logout = logout;
	if (conn.char_id)
		request.character = s.snapshot(id);
	conn.retry_at = s.now_ms + 1000;
	if (s.storage->submit(std::move(request)))
	{
		conn.pending = true;
		conn.save_failed = false;
		if (correlation || logout)
			conn.session->saving();
	}
	else
	{
		conn.save_failed = true;
		conn.session->saving();
		SA::Transport::SaveResult result{};
		result.code = SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE;
		result.logout = logout;
		(void)SA::Net::encodeFramed(correlation, result, conn.outbound);
	}
}

void World::onDisconnected(SA::Net::ConnectionId id)
{
	auto &s = *_impl;
	auto found = s.conns.find(id);
	if (found == s.conns.end())
		return;
	if (!s.storage)
	{
		removeSession(id);
		return;
	}
	auto &conn = found->second;
	if (conn.detached)
		return;
	conn.detached = true;
	conn.walk_seq.clear();
	conn.session->close();
	detachBattles(id);
	if (conn.pending)
	{
		conn.deferred_logout = true;
		return;
	}
	if (conn.logged_in)
		saveCharacter(id, true, 0);
	else
		removeSession(id);
}

void World::processStorage()
{
	auto &s = *_impl;
	if (!s.storage)
		return;
	using Op = SA::SessionStorage::Operation;
	using Code = SA::Transport::AccountCode;
	for (auto &completion : s.storage->poll())
	{
		auto found = s.conns.find(completion.session);
		if (found == s.conns.end())
			continue;
		auto &conn = found->second;
		conn.pending = false;
		if (completion.operation == Op::kLeaseLost || completion.code == Code::ACCOUNT_LEASE_LOST)
		{
			SA::Transport::SaveResult result{};
			result.code = Code::ACCOUNT_LEASE_LOST;
			(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
			conn.logged_in = false;
			conn.session->close();
			if (conn.detached)
				removeSession(completion.session);
			continue;
		}
		if (completion.operation == Op::kLogin)
		{
			conn.logged_in = completion.code == Code::ACCOUNT_OK;
			if (conn.detached)
			{
				if (conn.logged_in)
					saveCharacter(completion.session, true, 0);
				else
					removeSession(completion.session);
				continue;
			}
			SA::Transport::LoginResult result{};
			result.code = completion.code;
			result.characters = completion.characters;
			(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
			if (conn.logged_in)
				conn.session->selectCharacter();
			else
				conn.session->awaitLogin();
		}
		else if (completion.operation == Op::kCreate || completion.operation == Op::kSelect)
		{
			if (conn.detached)
			{
				saveCharacter(completion.session, true, 0);
				continue;
			}
			SA::Transport::CharacterResult result{};
			result.code = completion.code;
			if (result.code == Code::ACCOUNT_OK)
			{
				if (s.install(completion.session, completion.character))
					result.character = completion.character;
				else
					result.code = Code::ACCOUNT_INVALID;
			}
			(void)result.content_version.assign(s.content_version.c_str());
			(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
			if (result.code != Code::ACCOUNT_OK)
			{
				conn.session->selectCharacter();
				if (completion.code == Code::ACCOUNT_OK)
					saveCharacter(completion.session, true, 0);
			}
		}
		else if (completion.operation == Op::kSave || completion.operation == Op::kRelease)
		{
			SA::Transport::SaveResult result{};
			result.code = completion.code;
			result.revision = completion.character.revision;
			result.logout = completion.logout;
			if (completion.code == Code::ACCOUNT_OK)
			{
				conn.revision = completion.character.revision;
				if (auto *player = s.players.resolve(s.player_of_session.find(completion.session)))
				{
					for (const auto &value : completion.character.pets)
						if (auto *pet = s.pets.resolve(player->pets[value.slot]))
							pet->uid = value.uid;
					for (const auto &value : completion.character.items)
						if (auto *item = s.items.resolve(player->items[value.slot]))
							item->uid = value.uid;
				}
				if (completion.logout || completion.operation == Op::kRelease)
				{
					conn.logged_in = false;
					if (conn.detached)
					{
						removeSession(completion.session);
						continue;
					}
					(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
					conn.session->close();
				}
				else
				{
					conn.session->markOnline();
					SA::Transport::CharacterState state{};
					state.character = s.snapshot(completion.session);
					(void)conn.session->push(state, conn.outbound);
					if (completion.correlation)
						(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
				}
			}
			else
			{
				conn.save_failed = true;
				conn.session->saving();
				(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
			}
			if (completion.code == Code::ACCOUNT_OK && conn.logged_in && (conn.detached || conn.deferred_logout || conn.deferred_correlation))
			{
				const auto corr = conn.deferred_correlation;
				const bool logout = conn.detached || conn.deferred_logout;
				conn.deferred_correlation = 0;
				conn.deferred_logout = false;
				saveCharacter(completion.session, logout, corr);
			}
		}
	}
	std::vector<SA::Net::SessionId> retry;
	for (const auto &entry : s.conns)
		if (entry.second.detached && entry.second.logged_in && !entry.second.pending && s.now_ms >= entry.second.retry_at)
			retry.push_back(entry.first);
}

void World::loadFloorMap(std::int32_t floor_id, GridMap map)
{
	auto &s = *_impl;
	Impl::FloorState state;
	state.floor_id = floor_id;
	state.olink.assign(map.tile.size(), {});
	state.map = std::move(map);
	s.floors[floor_id] = std::move(state);
}

const GridMap *World::findFloorMap(std::int32_t floor_id) const noexcept
{
	const auto &s = *_impl;
	const auto *fl = s.getFloor(floor_id);
	return fl ? &fl->map : nullptr;
}

std::size_t World::floorMapCount() const noexcept
{
	const auto &s = *_impl;
	return s.floors.size();
}

} // namespace SA::World
