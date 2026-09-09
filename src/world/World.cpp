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
#include <vector>

#include "model/EntityIndex.h"
#include "model/EntityPool.h"
#include "model/Player.h"
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

using PlayerPool = SA::Model::EntityPool<SA::Model::Player, kMaxPlayers>;
using PetPool = SA::Model::EntityPool<SA::Model::Pet, kMaxPets>;
using EnemyPool = SA::Model::EntityPool<SA::Model::Enemy, kMaxEnemies>;

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
			// ⬜ **第 5 步** `BATTLE_CaptureItemDelAll`(:3540)⇒ 道具系统(DR-BT10「全删」)。
			//    ⚠️ 这一步做不了正是「捕获仍不完整」的最后一环:原版**扣了道具**才给宠物,
			//      我们现在是白给。别把它读成"差不多做完了"。

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
	};

	Impl(const SA::Platform::ServerConfig &cfg, SA::Platform::Clock &clk,
	     SA::Platform::Logger &log, SA::Platform::RandomSource &rnd,
	     SA::Net::Transport &tp)
	    : config(cfg), clock(clk), logger(log), random(rnd), transport(tp) {}

	SA::Platform::ServerConfig config;
	SA::Platform::Clock &clock;
	SA::Platform::Logger &logger;
	SA::Platform::RandomSource &random;
	SA::Net::Transport &transport;

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

	// 会话 → Player 实体。★ 03 §8.2 三条查找路径之一(原 `getCharindexFromFdid`
	//   那族**全表扫** + 每格加解锁,`fdnum=1000` 下每条应答扫 1,000 次)。
	// ⚠️ 索引里的句柄**可能悬空**,这是正常的 —— 验世代是 `EntityPool::resolve` 的活
	//   (EntityIndex.h 卷首的两步分工)。
	SA::Model::ConnIndex player_of_session{};
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

	// ── 3. NPC 生成 ──  ⬜ 阶段 2

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
			wctx.logger = &s.logger;
			applyEvents(b.events, b.field, wctx);

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

	// ── 5. 角色循环 ──   ⬜ 阶段 2
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
