// src/world/world.cpp —— 最小 tick 与一场战斗的生命周期
//
// 01 §3.1 的 tick 顺序被**原样保留**(连未实现的四步也占位),
// 01 §3.2 的节拍层在这里第一次成为真东西:
//   ★★ 战斗推进速度 **不等于** tick 频率。
//      15 §5.2 实测 8.0 的 _BATTLE_TIME 与 _CHAR_LOOP_TIME 均为关
//      ⇒ 原版战斗速度就是 tick 频率,手感取决于当年的硬件与网络。
//      00 §0 又已认下 ④ 层「表现与手感永远无法验证」
//      ⇒ 节拍是**玩法参数**,必须可配、只能靠人试。

#include "world/Api.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "model/EntityIndex.h"
#include "model/EntityPool.h"
#include "model/Player.h"
#include "rules/CaptureItem.h"
#include "rules/Progression.h"

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
struct WorldWriteContext
{
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
};

// 战斗事件缓冲。★ 每场战斗**复用一个**:domain::BattleEvents 是 7 KB 的 POD,
//   每回合新建一个就是每回合一次 7 KB 的拷贝(shared/rules/battle.h 的原话)。
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
};

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
// ⚠️ 玩家侧**不**在这里补默认指令:L3 已把「无指令 ⇒ 本回合不行动」写死
//   (battle.cpp 的 BuildActionOrder)。「玩家没提交该怎么办」是收集期与超时的
//   问题,属阶段 2,且是玩家可感知的玩法口径 ⇒ 须显式裁定,不由实现者定。
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
//    这两个世界态,L3 纯函数看不到 ⇒ World 在 `resolveTurn` **之前**按每条 USE_ITEM 指令
//    查好、写进攻方 `Combatant::mods.item_heal_power`。
//   ⚠️★★ **只投影基数,不在这里摇 rng** —— 实际恢复量 `RAND(power*0.9, power*1.1)`
//     (battle_magic.c:419)由 L3 在结算时用**战斗 rng** 摇。若在这里摇,取数就落在
//     resolveTurn 之外 ⇒ 战斗 rng 序列错位(同 W.4「遇敌 rng 与战斗 rng 分离」的反面教训)。
//   ⚠️★ **必须在 resolveTurn 前**:同捕获门,值备好 L3 才能纯读。
//   ★ 每回合按本回合指令重算(先归 0)⇒ 上回合的投影不残留;非 USE_ITEM 指令 / 无 L2 玩家 /
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

// 使用道具成功后扣掉一个(消耗一个 pile),归零则清槽 + 释放实体。批次 I.4「使用道具」。
//
// ★★ 与 `captureItemDelAll`(删道具)同分工:世界写,由调用方在 `applyEvents` 后做。
//    判据 = `atk.mods.item_heal_power > 0`(`projectItemUsePower` 投影过 ⇒ 本回合确实用了
//    有效恢复药)—— 与 L3 的 USE_ITEM 分支同一判据,不重复读道具表 / 不重判 target。
//   ⚠️ 源码 `ITEM_useRecovery_Battle` 末尾 `CHAR_DelItemMess`(battle_item.c:323)删一个;
//     我们按堆叠语义 `--current_pile`,归零才清槽 + 释放(单格即一个道具,行为一致)。
//   ⚠️★ 清槽 + 释放**成对**(同 `captureItemDelAll`:漏一半会让池只增不减 / 槽永久占用,
//     而没有一处报错)。
void consumeUsedItems(BattleInstance &b, PlayerPool &players, ItemPool &items)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		if (!b.commands.present[slot])
			continue;
		const SA::Domain::BattleCommand &cmd = b.commands.commands[slot];
		if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::USE_ITEM)
			continue;
		const SA::Rules::Combatant &atk = b.field.at(slot);
		if (atk.mods.item_heal_power <= 0)
			continue; // 未投影 ⇒ 非有效使用(空槽 / 非恢复药 / 无 L2 玩家)⇒ 不扣

		SA::Model::Player *owner =
		    players.resolve(b.player_of_slot[static_cast<std::size_t>(slot)]);
		if (owner == nullptr)
			continue;
		const int item_slot = static_cast<int>(cmd.command.use_item.item_slot);
		if (item_slot < 0 || item_slot >= static_cast<int>(SA::Model::kMaxItemHave))
			continue;
		const SA::Model::ItemHandle h = owner->items[static_cast<std::size_t>(item_slot)];
		SA::Model::Item *it = items.resolve(h);
		if (it == nullptr)
			continue;
		if (--it->current_pile <= 0)
		{
			owner->clearItemSlot(item_slot);
			(void)items.release(h);
		}
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
void applyEvents(const SA::Domain::BattleEvents &events,
                 SA::Rules::BattleField &field,
                 const WorldWriteContext &ctx)
{
	for (std::size_t i = 0; i < events.events.size(); ++i)
	{
		const SA::Domain::BattleEvent &e = events.events[i];
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
			if (c.hp <= 0)
			{
				c.hp = 0;
				c.dead = true;
			}
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
			const SA::Domain::CaptureAct &cap = e.body.capture_act;
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
				// ⚠️★★ **一处已知的不对称,显式记账而不是掩盖**:
				//    源码里"创建失败"会把 `flg` 改回 0 ⇒ 客户端收到 `f0`(抓失败)。
				//    而我们把判定(L3)与世界写(这里)分成两段,`CaptureAct` 事件
				//    **已经发出去了**且 `flags` 说的是"判定通过" ⇒ 改不回来。
				//    ★ 要对齐就得让 L3 在判定时知道宠物池 / 主人槽的状态,那是把 L2
				//      运行时状态灌进 L3 的纯函数入口 —— 与 D2 冲突,不在本批解。
				//    ⇒ 本批处置:**不写世界**(目标留场、不加捕获计数)+ 落 error 日志。
				//      表现上客户端会演"抓到了"而服务端没给宠物,已登记为欠债。
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
			const SA::Domain::PetSwitch &ps = e.body.pet_switch;
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
				exitPetFromField(field, static_cast<int>(ps.actor));
				owner->default_pet = -1;
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
	}
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
	me.attack = me_stats.attack;   // 322
	me.defense = me_stats.defense; // 88
	me.quick = me_stats.quick;     // 200
	me.max_hp = me_stats.max_hp;   // 860
	me.hp = me.max_hp;             // ★ 满血入场:hp 不再是独立的手填值

	// foe:各维按比例略低 ⇒ 攻防速血全弱一档,但**打得动**(性质 ① 要求它能打死玩家)。
	SA::Rules::Combatant &foe = f.at(SA::Rules::kSideOffset);
	foe.occupied = true;
	foe.kind = SA::Rules::CombatantKind::kEnemy;
	foe.slot = static_cast<std::uint8_t>(SA::Rules::kSideOffset);
	foe.level = 18;
	foe.luck = 5;
	const SA::Rules::DerivedStats foe_stats =
	    SA::Rules::deriveBaseStats(4000, 26000, 2000, 15000);
	foe.attack = foe_stats.attack;   // 273
	foe.defense = foe_stats.defense; // 57
	foe.quick = foe_stats.quick;     // 150
	foe.max_hp = foe_stats.max_hp;   // 590
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
SA::Rules::Combatant makePlayerCombatant()
{
	SA::Rules::Combatant c{};
	c.occupied = true;
	c.kind = SA::Rules::CombatantKind::kPlayer;
	c.slot = 0;
	c.level = 20;
	c.mp = 100;
	c.max_mp = 100;
	c.luck = 10;
	const SA::Rules::DerivedStats st =
	    SA::Rules::deriveBaseStats(8000, 30000, 4000, 20000);
	c.attack = st.attack;
	c.defense = st.defense;
	c.quick = st.quick;
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

struct World::Impl
{
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

	// 世界级遇敌 rng(批次 W.4):遇敌骰子(randMod)+ 遇敌链选怪(pickEnemyGroup/rollEnemyList)用它。
	// ⚠️★ 种子从 `masterSeed` **派生但不调 `nextSeed`** —— `nextSeed` 会消耗战斗种子序列、
	//    使现有战斗的回放种子整体平移(现有用例的 `spawnEnemy` 结果会变)。异或一个盐使它与
	//    任何战斗种子的序列都不同,同时随 `masterSeed` 确定 ⇒ 遇敌本身也可回放。
	SA::Rules::SeededRandom world_rng;

	std::map<SA::Net::ConnectionId, Conn> conns;
	// 1.5 里 SessionId == ConnectionId(见上)。
	std::map<BattleId, BattleInstance> battles;

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
	std::vector<SA::Net::ConnectionId> collectVisible(std::int32_t cx, std::int32_t cy,
	                                                  SA::Net::ConnectionId self) const;
	template <typename M>
	void sendTo(SA::Net::ConnectionId to, const M &msg);
	void appearBetween(SA::Net::ConnectionId a, const SA::Model::Player &pa,
	                   SA::Net::ConnectionId b);
	void broadcastMove(SA::Net::ConnectionId mover, std::int32_t ox, std::int32_t oy,
	                   const SA::Model::Player &p);
	void broadcastSpawn(SA::Net::ConnectionId who, const SA::Model::Player &p);
	void broadcastDespawn(SA::Net::ConnectionId who, std::int32_t x, std::int32_t y);

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
	std::vector<SA::Net::ConnectionId> collectVisiblePlayers(std::int32_t cx,
	                                                         std::int32_t cy) const;
	// 敌人视野广播(★ 单向:敌人无会话、不接收下行,只发给周围玩家;entity_type = ENTITY_ENEMY)。
	void broadcastEnemySpawn(const SA::Model::Enemy &e, std::uint64_t eid);
	void broadcastEnemyMove(const SA::Model::Enemy &e, std::uint64_t eid, std::int32_t ox,
	                        std::int32_t oy);
	void broadcastEnemyDespawn(std::int32_t x, std::int32_t y, std::uint64_t eid);
	// 玩家移动 (ox,oy)→(p.x,p.y) 后,把视野**新进 / 离开**的世界敌人补 appear / disappear 给他。
	//   ★ 这是"玩家看敌人"那一半(broadcastMove 只做了"玩家看玩家")。
	void refreshEnemyView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
	                      std::int32_t ox, std::int32_t oy);
};

World::World(const SA::Platform::ServerConfig &config,
             SA::Platform::Clock &clock, SA::Platform::Logger &logger,
             SA::Platform::RandomSource &random,
             SA::Net::Transport &transport)
    : _impl(std::make_unique<Impl>(config, clock, logger, random, transport))
{
	transport.setEvents(this);
}

World::~World() = default;

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
std::vector<SA::Net::ConnectionId> World::Impl::collectVisible(std::int32_t cx, std::int32_t cy,
                                                               SA::Net::ConnectionId self) const
{
	std::vector<SA::Net::ConnectionId> out;
	for (std::int32_t j = cy - kSeeRadius; j <= cy + kSeeRadius; ++j)
		for (std::int32_t i = cx - kSeeRadius; i <= cx + kSeeRadius; ++i)
		{
			if (!map.inBounds(i, j))
				continue;
			for (const SA::Net::ConnectionId c : olink[map.index(i, j)])
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
	const auto old_vis = collectVisible(ox, oy, mover);
	const auto new_vis = collectVisible(p.x, p.y, mover);

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
	for (const SA::Net::ConnectionId b : collectVisible(p.x, p.y, who))
		appearBetween(who, p, b);
}

// 离场 / 断线:给视野内每个玩家发 CharDisappear(who)。⚠️ 须在 olink 移除**之前**调(要 who 的位置)。
void World::Impl::broadcastDespawn(SA::Net::ConnectionId who, std::int32_t x, std::int32_t y)
{
	SA::Domain::CharDisappear dis{};
	dis.entity_id = who;
	for (const SA::Net::ConnectionId b : collectVisible(x, y, who))
		sendTo(b, dis);
}

// ══ 世界敌人:视野 / 生成 / 游荡(批次 W.2 / W.3)══════════════════════════════

// 收视野内玩家会话(★ 不排除 self)。敌人无会话,没有"自己"要排 —— 与 collectVisible 的唯一区别。
std::vector<SA::Net::ConnectionId> World::Impl::collectVisiblePlayers(std::int32_t cx,
                                                                      std::int32_t cy) const
{
	std::vector<SA::Net::ConnectionId> out;
	for (std::int32_t j = cy - kSeeRadius; j <= cy + kSeeRadius; ++j)
		for (std::int32_t i = cx - kSeeRadius; i <= cx + kSeeRadius; ++i)
		{
			if (!map.inBounds(i, j))
				continue;
			for (const SA::Net::ConnectionId c : olink[map.index(i, j)])
				out.push_back(c);
		}
	return out;
}

// 敌人进入世界 ⇒ 给视野内每个玩家发 CharAppear(★ 单向)。
void World::Impl::broadcastEnemySpawn(const SA::Model::Enemy &e, std::uint64_t eid)
{
	const SA::Domain::CharAppear a = makeEnemyAppear(eid, e);
	for (const SA::Net::ConnectionId b : collectVisiblePlayers(e.x, e.y))
		sendTo(b, a);
}

// 敌人移动一步 ⇒ 扫格 diff(同 broadcastMove 但单向:一直可见→CharMove / 新进→CharAppear / 离开→CharDisappear)。
void World::Impl::broadcastEnemyMove(const SA::Model::Enemy &e, std::uint64_t eid,
                                     std::int32_t ox, std::int32_t oy)
{
	const auto old_vis = collectVisiblePlayers(ox, oy);
	const auto new_vis = collectVisiblePlayers(e.x, e.y);

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
void World::Impl::broadcastEnemyDespawn(std::int32_t x, std::int32_t y, std::uint64_t eid)
{
	SA::Domain::CharDisappear dis{};
	dis.entity_id = eid;
	dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	for (const SA::Net::ConnectionId b : collectVisiblePlayers(x, y))
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
		const bool blocked_or_far =
		    !mapWalkable(map, map_attr, nx, ny) ||
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

			// ★ 敌方 AI 先填指令(见 FillEnemyCommands 卷首:这是 battle.h 指定的分工)。
			fillEnemyCommands(b.field, b.commands);

			// ★★ 捕获前置门 ④:把「攻方条件道具是否齐备」投影进 L3 输入面 —— **必须在
			//    resolveTurn 之前**(门不过则不摇 rng,与原版一致,见 projectCaptureItemGate)。
			projectCaptureItemGate(b, s.players, s.enemies, s.items);

			// ★ 使用道具门:把「本回合 USE_ITEM 指令的 HP 恢复力基数」投影进攻方 mods(批次 I.4)
			//   —— 同捕获门,读道具表 / 背包是世界态,必须在 resolveTurn 前。
			//   ⚠️★ 只投基数,**恢复量由 L3 用战斗 rng 摇**(在这儿摇会让取数落在 resolveTurn
			//     之外 ⇒ 战斗 rng 序列错位)。
			projectItemUsePower(b, s.players, s.items, s.item_effects);

			// 结算一个回合。⚠️ 返回 false = 事件超过 256 条被迫截断。
			//   05 §10.4 记着原版无上界 strcat 的教训 ⇒ **必须处理**,不可当没看见。
			const bool ok = SA::Rules::resolveTurn(b.field, b.commands,
			                                       s.rules_config, b.rng, b.events);
			if (!ok)
			{
				b.stats.truncated_once = true;
				s.logger.log(SA::Platform::LogLevel::kWarn,
				             SA::Platform::LogEvent::kBattleEventsTruncated,
				             {{"battle_id", b.id},
				              {"turn", static_cast<std::uint64_t>(b.field.turn)}});
				// ⚠️ 本批次先如实记账并继续 —— 01 §12 的取向是「宁可分包,不可静默截断」,
				//    而分包要改 BattleEvents 的下发形状(加 seq / more 标志),
				//    那是 IDL 的改动(0.2),不该被一次 world 的实现顺手带过。
				//    ⇒ 已登记为欠债,见 docs/01 §13。
			}

			// ★★ 写回世界状态 —— 见 ApplyEvents 卷首:L3 有意不写,调用方必须写。
			//   ★ 批次 M.1 起带上 L2 落脚点(池 / 主人槽 / 日志),捕获才有地方落。
			WorldWriteContext wctx;
			wctx.players = &s.players;
			wctx.pets = &s.pets;
			wctx.player_of_slot = &b.player_of_slot;
			wctx.enemies = &s.enemies;
			wctx.enemy_of_slot = &b.enemy_of_slot;
			wctx.items = &s.items;
			wctx.logger = &s.logger;
			applyEvents(b.events, b.field, wctx);

			// ★ 使用道具的世界写:扣掉本回合用掉的道具(消耗一个 pile)—— 同捕获删道具的分工,
			//   在 applyEvents 之后(HP 已由 SET_HP 事件落地)。批次 I.4。
			consumeUsedItems(b, s.players, s.items);

			b.stats.events_emitted += static_cast<std::uint32_t>(b.events.events.size());
			++b.stats.turns_resolved;

			// 下发事件流。★ 这就是 1.4 demo 的验收对象:**事件流端到端一致**。
			for (const SA::Net::SessionId sid : b.members)
			{
				const auto it = s.conns.find(sid);
				if (it == s.conns.end())
					continue;
				Impl::Conn &c = it->second;
				if (c.session == nullptr)
					continue;
				(void)c.session->push(b.events, c.outbound);
			}

			s.logger.log(SA::Platform::LogLevel::kDebug,
			             SA::Platform::LogEvent::kBattleTurnResolved,
			             {{"battle_id", b.id},
			              {"turn", static_cast<std::uint64_t>(b.field.turn)},
			              {"events", static_cast<std::uint64_t>(b.events.events.size())}});

			// 本回合的指令用完即清 —— 指令是**本回合**的输入,
			// 留着会让下一回合重放上一回合的动作。
			b.commands = SA::Rules::TurnCommands{};
			++b.field.turn;
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
				// 下一回合开始 —— ready_mask 留 0:1.5 没有"谁已提交指令"的收集期,
				// 指令一到就存下。收集期与超时是阶段 2 的事。
				SA::Domain::BattleTurnBegin begin;
				begin.battle_id = b.id;
				begin.turn = b.field.turn;
				begin.ready_mask = 0;
				for (const SA::Net::SessionId sid : b.members)
				{
					const auto it = s.conns.find(sid);
					if (it == s.conns.end() || it->second.session == nullptr)
						continue;
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

			// ── ★★ 战果结算:经验分配(战果结算批次)──────────────────────────
			//
			// 源码 `BATTLE_AddExp`(`battle.c:5500-5545`)的等级差衰减段。⚠️★ **时机差异
			//   登记**:原版每死一个敌人即结算一次(`AddProfit` 在死亡处理里调),我们在
			//   **战斗结束统一**遍历 `enemy_of_slot` 剩下的敌人 —— 被捕的那只已在
			//   `applyEvents` 释放并清了句柄,所以这里剩的**就是被打死的**。最终 Player.exp
			//   总量与逐死亡结算等价,差别只在结算落在回合末(可接受,登记)。
			// ⚠️ **只结算玩家方胜利**:玩家方全灭(打输)不给经验(源码 proflg 判胜方)。
			// ⚠️ 骑宠经验(源码 `×0.6`)本批不做 —— `Model::Pet` 无 `exp` 字段,且宠物
			//    经验 / 升级属宠物成长域(登记残缺)。
			{
				// 玩家方赢 = 敌方全灭且玩家方未全灭(finished 触发时至少一方全灭)。
				const bool player_won = sideWipedOut(b.field, /*enemy_side=*/true) && !sideWipedOut(b.field, /*enemy_side=*/false);

				// dpbattle(源码 :2267):本场任一敌人 `duelpoint > 0` ⇒ 决斗点怪 ⇒ 走
				// 决斗点、不走经验。★ 决斗点分配本批未做(PvP / saac 域)⇒ 这场谁也不拿战果。
				bool dp_battle = false;
				for (const SA::Model::EntityHandle &eh : b.enemy_of_slot)
				{
					const SA::Model::Enemy *e = s.enemies.resolve(eh);
					if (e != nullptr && e->duelpoint > 0)
					{
						dp_battle = true;
						break;
					}
				}

				// 本场每个玩家槽获得的经验 —— 既累加进 Player.exp,又用于下发 BattleResult。
				std::array<std::int32_t, SA::Rules::kSideOffset> gained{};

				if (player_won && !dp_battle)
				{
					// ── 掉落拾取的暂存与选人范围(源码 `BATTLE_AddExpItem`,批次 I.3)────
					//
					// ★ 原版每死一敌即 `BATTLE_AddExpItem`,把敌人预掉落道具逐件随机分给
					//   一名攻击方 entry(玩家或宠物,宠物折算回主人),暂存到 entry 的
					//   `getitem[≤3]`,战斗结束再灌背包。⚠️★ 我们在**回合末统一**遍历死敌
					//   (同经验的时机差异,已登记),`getitem` 用局部数组、per 玩家槽。
					// ★ **选人范围 `allnum` = 己方在场战斗单位(玩家 + 宠物,源码含宠)**:
					//   `k = RAND(0,allnum-1)` 选第 k 个在场单位,宠位(≥kBattlePlayerMax)
					//   折算回主人(`slot-5`,源码 :6462)⇒ 战利品记给玩家。
					// ⚠️ **rng 用 `b.rng`**:战斗已结束、该 rng 用完即弃 ⇒ 选人的精确分布
					//   不平移任何后续序列,只定"这场掉落归谁"。
					// ⚠️★ **组队/死者边角登记**:当前每会话独立一场(§9.0.16)⇒ 己方通常
					//   单玩家(+宠)⇒ 归属无歧义;精确 `pBidList`(活/全部)与组队掉落分布
					//   待组队玩法落地复核。
					std::array<int, SA::Rules::kSideOffset> present_slots{};
					int allnum = 0;
					for (int sl = 0; sl < SA::Rules::kSideOffset; ++sl)
						if (b.field.at(sl).occupied)
							present_slots[static_cast<std::size_t>(allnum++)] = sl;

					// getitem 暂存:per 玩家槽(0..kBattlePlayerMax-1)3 格,-1 = 空。
					std::array<std::array<std::int32_t, 3>, SA::Rules::kBattlePlayerMax>
					    getitem;
					for (auto &g : getitem)
						g.fill(-1);

					// 外层:每个还挂在 `enemy_of_slot` 的敌人 = 被打死的(被捕的已清句柄)。
					for (int es = SA::Rules::kSideOffset; es < SA::Rules::kSlotCount; ++es)
					{
						const SA::Model::Enemy *e = s.enemies.resolve(
						    b.enemy_of_slot[static_cast<std::size_t>(es)]);
						if (e == nullptr)
							continue;
						const std::int32_t enemy_exp = e->exp;
						const std::int32_t enemy_level = e->level;

						// 内层:每个在场的玩家实体(源码逐个 `charaindex[k]`)。
						for (int ps = 0; ps < SA::Rules::kSideOffset; ++ps)
						{
							SA::Model::Player *p = s.players.resolve(
							    b.player_of_slot[static_cast<std::size_t>(ps)]);
							if (p == nullptr)
								continue;

							// 等级差衰减(源码 `EXPGET_MAXLEVEL=5` / `EXPGET_DIV=15`):
							//   玩家不比怪高 5 级 ⇒ 全额;高 5 级以上 ⇒ 线性衰减、保底 1。
							// ★ 整数运算(源码 `exp * b_level / 15`);玩家等级取**战场
							//   Combatant**(Player 实体不建 level,见 Player.h)。
							const std::int32_t player_level = b.field.at(ps).level;
							std::int32_t b_level = player_level - enemy_level;
							std::int32_t nowexp;
							if (b_level <= 5)
							{
								nowexp = enemy_exp;
							}
							else
							{
								b_level = 5 + 15 - b_level;
								if (b_level > 15)
									b_level = 15;
								if (b_level <= 0)
									nowexp = 1;
								else
									nowexp = enemy_exp * b_level / 15;
								if (nowexp < 1)
									nowexp = 1;
							}
							p->exp += nowexp;
							gained[static_cast<std::size_t>(ps)] += nowexp;
						}

						// ── 掉落拾取(源码 `battle.c:6486-6516`)★ 逐件随机选人入 getitem ──
						//   与经验同在这只死敌的处理里(源码同一函数、同一 entry 循环)。
						for (int d = 0; d < e->drop_count && allnum > 0; ++d)
						{
							const std::int32_t item_id =
							    e->dropped_items[static_cast<std::size_t>(d)];
							// 逐件 `RAND(0,allnum-1)` 选一名在场单位(源码 :6497)。
							const int slot_k = present_slots[static_cast<std::size_t>(
							    b.rng.rand(0, allnum - 1))];
							// 宠位折算回主人(源码 :6462 `subnum-5`)⇒ 战利品记玩家。
							const int owner =
							    slot_k >= SA::Rules::kBattlePlayerMax
							        ? slot_k - SA::Rules::kBattlePlayerMax
							        : slot_k;
							const std::size_t oi = static_cast<std::size_t>(owner);
							// 入主人 getitem 空位;满(3 格)则 50% 覆盖随机格 / 50% 弃(源码 :6504)。
							int gl = 0;
							for (; gl < 3; ++gl)
								if (getitem[oi][static_cast<std::size_t>(gl)] < 0)
								{
									getitem[oi][static_cast<std::size_t>(gl)] = item_id;
									break;
								}
							if (gl >= 3 && b.rng.rand(0, 1)) // 50% 覆盖(源码 :6505 `RAND(0,1)`)
								getitem[oi][static_cast<std::size_t>(b.rng.rand(0, 2))] = item_id;
							// else(gl>=3 且 rand==0):丢弃该道具(源码 :6513);未 makeItem 实体
							//   ⇒ 无需 release,item_id 不记即弃。
						}
					}

					// ── 灌背包(源码 `BATTLE_GetExpGold:4471`)★ 每个玩家 getitem → 背包 ──
					//   有空位进包(`CHAR_addItemSpecificItemIndex`);满则源码销毁 ⇒ 我方丢弃。
					// ⚠️ Item 仅填 `item_id` —— 其余列(name/type/level/cost)待道具表 D 线导入
					//   (同敌人模板 fixture,登记残缺)。满包提示(DR-UX1)属客户端展示,与掉落
					//   下发一并留后续(本批服务端权威,不动 IDL)。
					for (int ps = 0; ps < SA::Rules::kBattlePlayerMax; ++ps)
					{
						SA::Model::Player *p = s.players.resolve(
						    b.player_of_slot[static_cast<std::size_t>(ps)]);
						if (p == nullptr)
							continue;
						for (int gl = 0; gl < 3; ++gl)
						{
							const std::int32_t item_id =
							    getitem[static_cast<std::size_t>(ps)][static_cast<std::size_t>(gl)];
							if (item_id < 0)
								continue;
							SA::Model::Item item{};
							item.item_id = item_id;
							item.current_pile = 1;                       // ★ I.4:掉落回填堆叠数(一件)⇒ 使用侧读得到
							(void)giveItemIntoPlayer(*p, item, s.items); // -1 = 背包满 ⇒ 丢弃
						}
					}
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
					g.exp_gained = gained[static_cast<std::size_t>(ps)];
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
		if (c.session == nullptr || c.walk_seq.empty())
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
		if (decodeDirChar(c.walk_seq.front(), dir, is_turn))
			moved = walkStep(*p, s.map, s.map_attr, dir, is_turn);
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

		// 里程碑②:位置变了 ⇒ 更新 olink(旧格摘、新格挂)+ 视野广播(扫格 diff)。
		if (moved)
		{
			if (s.map.inBounds(ox, oy))
			{
				auto &oldcell = s.olink[s.map.index(ox, oy)];
				oldcell.erase(std::remove(oldcell.begin(), oldcell.end(), kv.first),
				              oldcell.end());
			}
			s.olink[s.map.index(p->x, p->y)].push_back(kv.first);
			s.broadcastMove(kv.first, ox, oy, *p);
			// W.3:玩家移动后补发视野内**世界敌人**的 appear / disappear(玩家看敌人那一半)。
			s.refreshEnemyView(kv.first, *p, ox, oy);

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
	}

	// ── 5b. 角色循环 —— 非玩家段:世界敌人 AI(批次 W.3)──────────────────────
	//   ★ 条数制摊还:每 tick 最多游荡 tempo.enemy_move_num 只世界敌人,游标续跑(wanderWorldEnemies)。
	//   ⚠️★ **不是时间预算制** —— 8.0 的 _CHAR_LOOP_TIME 三证实测关(15 §5.2 C18),走 #else 条数制。
	s.wanderWorldEnemies(s.config.tempo.enemy_move_num);

	// ── 6. 定时业务 ──   ⬜ 阶段 2
	// ── 7. 出站聚合 ──   ⬜ 阶段 2(CA/CD 视野聚合;1.5 无视野)
	//
	// ⚠️ 但**出站字节仍要发出去** —— 上面第 4 步往 outbound 里写了东西。
	//    这不是 §7 说的那种聚合(那是视野 Appear/Disappear 攒批),
	//    只是"把已经生成的字节交给传输层"。别把这里读成 §7 已经做了。
	for (auto &kv : s.conns)
	{
		Impl::Conn &c = kv.second;
		if (c.outbound.empty())
			continue;
		(void)s.transport.send(c.conn_id, c.outbound.data(), c.outbound.size());
		c.outbound.clear();
	}

	// ── 8. 关闭检查 ──
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
			enterPetToField(b.field, slot, *pet);
	}

	cit->second.session->markOnline();

	// ★★ 入场即下发**自己是谁**与**现在是第几回合**,否则客户端无从组指令:
	//    BattleCommand 要带 battle_id 与 turn,而这两样它此刻都还不知道
	//    —— 缺这一步,上行链路在 demo 里根本走不到。
	//
	// ⚠️ 这不是 demo 专用的东西,所以放在 JoinBattle 而不是 OnSessionReady:
	//    任何入场路径(阶段 2 的选角、观战加入)都需要它。
	SA::Domain::BattleSelfInfo self;
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

	SA::Domain::BattleTurnBegin begin;
	begin.battle_id = b.id;
	begin.turn = b.field.turn;
	begin.ready_mask = 0; // 1.5 没有收集期,理由见 Tick 第 4 步
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

void World::loadItemEffects(std::vector<ItemEffect> effects)
{
	_impl->item_effects = std::move(effects);
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

// 遇敌命中后的开战组装(批次 W.4)——移植 `EN_recv`(`callfromcli.c:1249`)清走路串 +
//   `BATTLE_CreateVsEnemy(charaindex,0,-1)` 净核(`battle.c:2528`):
//   遇敌链(`pickEnemyGroup`→`rollEnemyList`)→ 建场 → 玩家入场 → 逐只敌人入场。
// ⚠️★ 遇敌链的 rng 用**世界 rng**(`s.random`)——原版 `ENEMY_getEnemy` 在建 battle **之前**、
//    用全局 `rand()`,不是战斗 rng(战斗此刻还没建;敌人四维生成才用战斗 rng,见 spawnEnemyToField)。
bool World::triggerEncounter(SA::Net::SessionId session, std::int32_t area_row)
{
	Impl &s = *_impl;
	if (area_row < 0 ||
	    static_cast<std::size_t>(area_row) >= s.encount_areas.size())
		return false;
	const EncountArea &area = s.encount_areas[static_cast<std::size_t>(area_row)];

	// ── 选编组(区域 → 编组行下标)──────────────────────────────────────
	//   ⚠️ 道具门用**空背包**快照(道具系统未移植,见 `EnemyGroup::appear_by_item_id`:
	//      对空背包玩家与原版 100% 一致)。-1 ⇒ 无可用编组 ⇒ 本次不遇敌(原版等价)。
	const std::vector<std::int32_t> empty_bag{};
	const std::int32_t grow =
	    pickEnemyGroup(area, s.enemy_groups, empty_bag, s.world_rng);
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
	field.at(0) = makePlayerCombatant();
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
	//   ⚠️ 战场每侧的实体位是 `kBattlePlayerMax`(=5,宠位在其后)⇒ 取前 5 只
	//     (`rollEnemyList` 上界是区域 `enemy_max_num ∈ [1,10]`,可能多于战场敌方位)。
	int placed = 0;
	for (std::size_t i = 0;
	     i < rows.size() && placed < SA::Rules::kBattlePlayerMax; ++i)
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
	field.at(0) = makePlayerCombatant();
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

	// ── 从世界态移除 + 广播消失(原版明雷进战斗态即从地图消失)───────────────────
	//   ⚠️ 先广播(用移除前的世界坐标)再 erase;broadcastEnemyDespawn 单向发给视野内玩家。
	s.broadcastEnemyDespawn(ex, ey, encodeHandle(we.handle));
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
				(void)s.transport.send(id, c.outbound.data(), c.outbound.size());
				c.outbound.clear();
			}
			s.transport.close(id);
			return;
		}
	}
}

void World::onDisconnected(SA::Net::ConnectionId id)
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
	if (SA::Model::Player *p = s.players.resolve(ph); p != nullptr)
	{
		// 里程碑②:先给视野内玩家发 CharDisappear + 从 olink 摘除(都要 p 的位置,须在释放前)。
		s.broadcastDespawn(id, p->x, p->y);
		if (s.map.inBounds(p->x, p->y))
		{
			auto &cell = s.olink[s.map.index(p->x, p->y)];
			cell.erase(std::remove(cell.begin(), cell.end(), id), cell.end());
		}
		for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
		{
			if (!p->pets[i].valid())
				continue;
			// ★ release 对悬空句柄返回 false 且不做事(generation 校验)⇒ 无需先 resolve。
			(void)s.pets.release(p->pets[i]);
			(void)p->clearPetSlot(static_cast<int>(i));
		}
		(void)s.players.release(ph);
	}
	s.player_of_session.erase(id);

	for (auto &kv : s.battles)
	{
		std::vector<SA::Net::SessionId> &m = kv.second.members;
		m.erase(std::remove(m.begin(), m.end(), id), m.end());
		kv.second.slot_of.erase(id);
		// ★ 槽 → Player 的映射一并清:句柄已作废,留着虽不会脏读(resolve 返 nullptr,
		//   M10)但会误导 —— 读代码的人会以为那个槽还有主人。
		for (SA::Model::EntityHandle &h : kv.second.player_of_slot)
		{
			if (h == ph)
				h = SA::Model::kNullHandle;
		}
	}
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
				np->floor = 0;
				np->x = s.map.width / 2;
				np->y = s.map.height / 2;
				// 里程碑②:入 olink + 与视野内玩家双向 CharAppear(原版进图 sendCToArround)。
				s.olink[s.map.index(np->x, np->y)].push_back(id);
				s.broadcastSpawn(id, *np);
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
	if (slot >= SA::Rules::kSlotCount)
		return;

	b.commands.commands[slot] = cmd;
	b.commands.present[slot] = true;
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
	if (!mapWalkable(s.map, s.map_attr, cx, cy))
		return;

	// 排走路串(walk_init:948 → walk_start:891 setWorkChar WALKARRAY)。
	//   ⚠️ FixedStr<32> 已保证 ≤32(原版 walk_init:939 的长度门);实际逐步移动由
	//      kCharLoop 玩家段按 walksendinterval 消费(CHAR_walkcall)。
	it->second.walk_seq = std::string(req.direction.c_str());
	it->second.next_walk_at_ms = 0; // 立即可走第一步(now_ms >= 0)
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

	// 回执(原版 lssproto_EV_send(fd, seqno, rc)):seqno 原样带回,ok = 是否命中并开战。
	//   ★ 靠 seqno 关联(不依赖传输层 corr_id),同原版 EV 的 seqno 机制。
	SA::Domain::EventResult res{};
	res.seqno = req.seqno;
	res.ok = ok;
	s.sendTo(id, res);
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
	return it == _impl->battles.end() ? nullptr : &it->second.stats;
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
	return it == _impl->battles.end() ? nullptr : &it->second.field;
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
	dst.attack = stats.attack;
	dst.defense = stats.defense;
	dst.quick = stats.quick;
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

		// 两道道具门(源码 :1307-1332)。⚠️ 道具系统未移植 ⇒ 调用方传空背包,
		//    对空背包玩家与原版 100% 一致;后果规模见 `EnemyGroup` 那两条注释。
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
	// ── 门 ①:必须落在某一 side 的玩家段(宠位留给 enterPetToField)────────
	if (field_slot < 0 || field_slot >= SA::Rules::kSlotCount)
		return false;
	if (field_slot % SA::Rules::kSideOffset >= SA::Rules::kBattlePlayerMax)
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
	dst.attack = stats.attack;
	dst.defense = stats.defense;
	dst.quick = stats.quick;
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

} // namespace SA::World
