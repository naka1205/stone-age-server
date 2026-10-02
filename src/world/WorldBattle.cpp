// src/world/WorldBattle.cpp —— 战斗子系统实现 (生命周期、指令处理、回合推进与事件结算)
//
// 对应原版 battle/battle.c, battle/battle_command.c, battle/battle_event.c

#include "WorldImpl.h"
#include "world/Api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "model/EntityIndex.h"
#include "model/EntityPool.h"
#include "model/Player.h"
#include "rules/Battle.h"
#include "rules/CaptureItem.h"
#include "rules/ProfessionSkill.h"
#include "rules/Progression.h"
#include "rules/Status.h"

namespace SA::World
{

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
	dst.pet_id = pet.pet_id;
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

namespace
{

std::uint32_t readyMask(const BattleInstance &battle)
{
	std::uint32_t mask = 0;
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
		if (battle.commands.present[slot] && battle.field.at(slot).occupied && !battle.field.at(slot).dead)
			mask |= 1u << slot;
	return mask;
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

// 若战斗宠物未收到专属指令，自动填充基础普攻敌方存活目标 (对齐 BATTLE_AutoAttack / PET_AutoAttack)
void autoFillPetCommands(const SA::Rules::BattleField &field,
                         SA::Rules::TurnCommands &commands)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		const bool is_pet_slot =
		    (slot >= SA::Rules::kBattlePlayerMax && slot < SA::Rules::kSideOffset) ||
		    (slot >= SA::Rules::kSideOffset + SA::Rules::kBattlePlayerMax && slot < SA::Rules::kSlotCount);
		if (!is_pet_slot)
			continue;

		const SA::Rules::Combatant &c = field.at(slot);
		if (!c.occupied || c.dead || c.hp <= 0 || commands.present[slot])
			continue;

		const int opp_start = (slot < SA::Rules::kSideOffset) ? SA::Rules::kSideOffset : 0;
		int opp_target = -1;
		for (int i = opp_start; i < opp_start + SA::Rules::kSideOffset; ++i)
		{
			const SA::Rules::Combatant &opp = field.at(i);
			if (opp.occupied && !opp.dead && opp.hp > 0)
			{
				opp_target = i;
				break;
			}
		}
		if (opp_target < 0)
			continue;

		SA::Domain::BattleCommand cmd{};
		cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd.command.attack.target = static_cast<std::uint8_t>(opp_target);
		commands.commands[slot] = cmd;
		commands.present[slot] = true;
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

// 对账(A-α 批):= 原版 `BATTLE_GetExpGold`(SSRC80 `battle.c:3308`)的
// **经验/掉落一半** —— 结束(finished)或主动离场时,把战斗中暂存的收益
// (WORKGETEXP → pending_exp;拾得道具 → getitem)交付给**存活**玩家。
// 源码 :3327-3329 的 `CHAR_ISDIE` 提前返回(`return 0`)在这里是函数开头的
// `dead` 门 —— 门在 flush 落点,不在击杀瞬间:击杀时记的账,死了就不给。
// ⚠️ 金币**不在本函数**:原版 GetExpGold 里的 `gold += getBattleGold()`
// (8.5 `battle.c:3851-3860`;SSRC80 全树无 getBattleGold 符号)在本实现走
// finished 段的 GoldLedger 产币(见下方「战斗产币」注释)—— 与经验域解耦:
// dp 门只拦金,不拦这里的经验/掉落交付。
void deliverPlayerProfit(BattleInstance &b, int slot, PlayerPool &players, ItemPool &items, PetPool *pets = nullptr)
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

	// ── 玩家经验升级与满血满蓝结算 (批次 P.1) ───────────────────────
	const auto lvl_res = SA::Rules::checkPlayerLevelUp(player->level, player->exp);
	if (lvl_res.levels_gained > 0)
	{
		player->level = lvl_res.new_level;
		player->skillup_points += lvl_res.skillup_points_gained;
		player->charm = std::min(100, player->charm + lvl_res.charm_gained);

		// 升级满血满蓝 (char_data.c:1400 / battle.c:4350)
		SA::Rules::EquipModifiers equip{};
		for (std::size_t i = 0; i < SA::Model::kStartItemArray; ++i)
		{
			if (const auto *it = items.resolve(player->items[i]))
			{
				equip.modify_attack += it->modify_attack;
				equip.modify_defense += it->modify_defense;
				equip.modify_quick += it->modify_quick;
				equip.modify_hp += it->modify_hp;
				equip.modify_mp += it->modify_mp;
			}
		}
		const auto stats = SA::Rules::deriveEquippedStats(player->vital, player->str, player->tough, player->dex, equip);
		player->hp = stats.max_hp;
		player->mp = player->max_mp;
		if (at < static_cast<std::size_t>(SA::Rules::kSlotCount))
		{
			const int i_at = static_cast<int>(at);
			b.field.at(i_at).level = player->level;
			b.field.at(i_at).hp = player->hp;
			b.field.at(i_at).mp = player->mp;
		}
	}

	// ── 宠物经验升级与四维成长 (批次 P.1) ───────────────────────────
	const std::size_t pet_slot = at + SA::Rules::kBattlePlayerMax;
	if (pets != nullptr && pet_slot < static_cast<std::size_t>(SA::Rules::kSlotCount))
	{
		if (auto *pet = pets->resolve(b.pet_of_slot[pet_slot]))
		{
			const int pet_exp = std::max(0, b.pending_exp[pet_slot]);
			b.pending_exp[pet_slot] = 0;
			if (pet_exp > 0)
			{
				pet->exp += pet_exp;
				const auto pet_lvl_res = SA::Rules::checkPlayerLevelUp(pet->level, pet->exp);
				if (pet_lvl_res.levels_gained > 0)
				{
					for (int g = 0; g < pet_lvl_res.levels_gained; ++g)
					{
						const auto roll = SA::Rules::rollPetLevelUp(
						    pet->growth_vital, pet->growth_str, pet->growth_tough, pet->growth_dex,
						    pet->pet_rank, b.rng);
						pet->vital += roll.added_vital;
						pet->str += roll.added_str;
						pet->tough += roll.added_tough;
						pet->dex += roll.added_dex;
						pet->level += 1;
					}
					const auto pet_stats = SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex);
					pet->hp = pet_stats.max_hp;
					pet->mp = pet->max_mp;
					const int i_pet_slot = static_cast<int>(pet_slot);
					b.field.at(i_pet_slot).level = pet->level;
					b.field.at(i_pet_slot).hp = pet->hp;
					b.field.at(i_pet_slot).mp = pet->mp;
					b.field.at(i_pet_slot).vital = pet->vital;
					b.field.at(i_pet_slot).str = pet->str;
					b.field.at(i_pet_slot).tough = pet->tough;
					b.field.at(i_pet_slot).dex = pet->dex;
					b.field.at(i_pet_slot).max_hp = pet_stats.max_hp;
					b.field.at(i_pet_slot).attack = pet_stats.attack;
					b.field.at(i_pet_slot).defense = pet_stats.defense;
					b.field.at(i_pet_slot).quick = pet_stats.quick;
					b.field.at(i_pet_slot).fix_dex = pet_stats.quick;
				}
			}
		}
	}

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
		atk.mods.pet_skill_special_kind = SA::Rules::PetSkillSpecialKind::kNone;
		atk.mods.pet_skill_special_param1 = 0;
		atk.mods.pet_skill_special_param2 = 0;
		atk.mods.pet_skill_special_param3 = 0;

		if (!b.commands.present[slot])
			continue;
		const SA::Domain::BattleCommand &cmd = b.commands.commands[slot];
		if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::PET_SKILL)
			continue;

		const PetSkillEffect *e =
		    findPetSkillEffect(effects, cmd.command.pet_skill.skill_id);
		if (e == nullptr)
			continue; // 表外技能 / 空表 ⇒ 保持"无技能"⇒ L3 跳过(不退化成普攻)

		// ⚠️ 蓄力行、魔法状态行(铁壁)与非直攻特殊宠技**都不是**直攻系
		if (e->charge_turns == 0 && e->magic_status == 0 &&
		    (e->special_kind == SA::Rules::PetSkillSpecialKind::kNone ||
		     e->special_kind == SA::Rules::PetSkillSpecialKind::kFallGround ||
		     e->special_kind == SA::Rules::PetSkillSpecialKind::kSelfExplode))
			atk.mods.pet_skill_direct = true;

		atk.mods.pet_skill_special_kind = e->special_kind;
		atk.mods.pet_skill_special_param1 = e->special_param1;
		atk.mods.pet_skill_special_param2 = e->special_param2;
		atk.mods.pet_skill_special_param3 = e->special_param3;
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

// 投影职业技能参数至战斗单位快照 (批次 A-γ2)
void projectProfSkill(BattleInstance &b, const PlayerPool &players)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		SA::Rules::Combatant &atk = b.field.at(slot);
		if (!atk.occupied)
			continue;

		// 归零 = 回到"无技能"
		atk.mods.prof_skill_direct = false;
		atk.mods.prof_skill_hits = 0;
		atk.mods.prof_skill_damage_percent = 100;
		atk.mods.prof_skill_attack_percent = 0;
		atk.mods.prof_skill_quick_percent = 0;
		atk.mods.prof_skill_apply_status = 0;
		atk.mods.prof_skill_status_turns = 0;
		atk.mods.prof_reback_level = 0;
		atk.mods.prof_avoid_bonus = 0;
		atk.mods.prof_deflect_bonus = 0;
		atk.mods.prof_weapon_focus_attack_percent = 0;
		atk.mods.prof_magic_proficiency = 0;

		// 投影职业被动加成 (BATTLE_ProfessionStatus_init)
		if (slot < SA::Rules::kBattlePlayerMax)
		{
			const auto ph = b.player_of_slot[static_cast<std::size_t>(slot)];
			if (ph.valid())
			{
				if (const auto *player = players.resolve(ph))
				{
					const int p_lvl = player->profession_level > 0 ? player->profession_level : 1;
					if (player->profession_class == SA::Rules::ProfessionClass::kFighter)
					{
						atk.mods.prof_reback_level = p_lvl;
						atk.mods.prof_deflect_bonus = SA::Rules::computeProfessionDeflectBonus(p_lvl);
						atk.mods.prof_weapon_focus_attack_percent = SA::Rules::computeProfessionWeaponFocusBonus(p_lvl);
					}
					else if (player->profession_class == SA::Rules::ProfessionClass::kHunter)
					{
						atk.mods.prof_avoid_bonus = SA::Rules::computeProfessionAvoidBonus(p_lvl);
					}
					else if (player->profession_class == SA::Rules::ProfessionClass::kWizard)
					{
						atk.mods.prof_magic_proficiency = SA::Rules::computeProfessionPracticeBonus(p_lvl);
					}
				}
			}
		}

		if (!b.commands.present[slot])
			continue;
		const SA::Domain::BattleCommand &cmd = b.commands.commands[slot];
		if (cmd.command_kind != SA::Domain::BattleCommand::CommandKind::PROF_SKILL)
			continue;

		const auto skill_id = cmd.command.prof_skill.skill_id;
		const auto *info = SA::Rules::findProfessionSkillById(skill_id);
		if (info == nullptr)
			continue;

		// 目标槽单位族
		const int target_slot = static_cast<int>(cmd.command.prof_skill.target);
		const SA::Rules::CombatantKind target_kind =
		    (target_slot >= 0 && target_slot < SA::Rules::kSlotCount && b.field.at(target_slot).occupied)
		        ? b.field.at(target_slot).kind
		        : SA::Rules::CombatantKind::kEnemy;

		// 技能等级：优先读玩家实体的 profession_level，未设置时兜底为 10
		int skill_level = 10;
		if (slot < SA::Rules::kBattlePlayerMax)
		{
			const auto ph = b.player_of_slot[static_cast<std::size_t>(slot)];
			if (ph.valid())
			{
				if (const auto *player = players.resolve(ph))
				{
					if (player->profession_level > 0)
						skill_level = player->profession_level;
				}
			}
		}

		const auto params = SA::Rules::computeProfSkillDirectParams(skill_id, skill_level, target_kind, b.rng);
		if (params.is_direct)
		{
			atk.mods.prof_skill_direct = true;
			atk.mods.prof_skill_hits = params.hits;
			atk.mods.prof_skill_damage_percent = params.damage_percent;
			atk.mods.prof_skill_attack_percent = params.attack_percent;
			atk.mods.prof_skill_quick_percent = params.quick_percent;
			atk.mods.prof_skill_apply_status = params.apply_status;
			atk.mods.prof_skill_status_turns = params.status_turns;
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
				if (ctx.battle != nullptr)
					ctx.battle->quick_to_restore[sc.target] = c.quick;
				if (c.has_ride)
				{
					// 原版有骑宠时是 quick += 骑宠的 quick (battle.c:5492)
					// Combatant::ride_dex 存放了骑宠敏捷属性
					c.quick += c.ride_dex;
				}
				else
				{
					c.quick *= 2;
				}
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
					deliverPlayerProfit(*ctx.battle, static_cast<int>(esc.actor), *ctx.players, *ctx.items, ctx.pets);
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

} // namespace

void World::advanceBattles()
{
	Impl &s = *_impl;
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
					deliverPlayerProfit(b, slot, s.players, s.items, &s.pets);
					b.field.at(slot).occupied = false;
					if (!b.field.at(slot).has_ride || b.field.at(slot).ride_hp <= 0)
					{
						dismountPet(sid);
					}
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
		if (!b.is_pvp)
			fillEnemyCommands(b.field, b.commands);
		autoFillPetCommands(b.field, b.commands);

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
			for (auto sid : b.spectators)
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
			// 职技参数逐行动重投影 (A-γ2): 查表投影直攻系参数
			projectProfSkill(b, s.players);
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
			for (auto sid : b.spectators)
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
					player->hp = b.is_pvp ? std::max(1, b.field.at(slot).hp) : std::max(0, b.field.at(slot).hp);
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
			for (const SA::Net::SessionId sid : b.spectators)
			{
				const auto it = s.conns.find(sid);
				if (it == s.conns.end() || it->second.session == nullptr)
					continue;
				SA::Domain::BattleSelfInfo info{};
				info.battle_id = b.id;
				info.slot = SA::Rules::kSlotCount;
				info.mp = 0;
				info.menu_flags = 0;
				info.cannot_act = SA::Domain::CannotActReason::CANNOT_ACT_NONE;
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
			if (!b.is_pvp)
			{
				for (int slot = 0; slot < SA::Rules::kBattlePlayerMax; ++slot)
					deliverPlayerProfit(b, slot, s.players, s.items, &s.pets);
			}
			else
			{
				for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
				{
					if (auto *p = s.players.resolve(b.player_of_slot[static_cast<std::size_t>(slot)]))
					{
						if (p->hp <= 0)
							p->hp = 1;
					}
				}
			}

			// ── 宠物忠诚度战后生命周期结算 (批次 §9.0.101, 对齐 char.c/battle.c) ──
			for (int slot = 0; slot < SA::Rules::kBattlePlayerMax; ++slot)
			{
				// 出战宠战果结算
				if (auto *pet = s.pets.resolve(b.pet_of_slot[static_cast<std::size_t>(slot + SA::Rules::kBattlePlayerMax)]))
				{
					int cur = s.pet_loyalty.find(pet->uid) != s.pet_loyalty.end() ? s.pet_loyalty[pet->uid] : 100;
					if (b.field.at(slot + SA::Rules::kBattlePlayerMax).dead || pet->hp <= 0)
					{
						s.pet_loyalty[pet->uid] = std::max(0, cur - 5);
					}
					else if (player_won)
					{
						s.pet_loyalty[pet->uid] = std::min(100, cur + 1);
					}
				}
				// 骑乘宠战损结算
				if (auto *rpet = s.pets.resolve(b.ride_pet_of_slot[static_cast<std::size_t>(slot)]))
				{
					int cur = s.pet_loyalty.find(rpet->uid) != s.pet_loyalty.end() ? s.pet_loyalty[rpet->uid] : 100;
					if (b.field.at(slot).ride_hp <= 0 || rpet->hp <= 0)
					{
						s.pet_loyalty[rpet->uid] = std::max(0, cur - 5);
					}
					else if (player_won)
					{
						s.pet_loyalty[rpet->uid] = std::min(100, cur + 1);
					}
				}
			}

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
			for (const SA::Net::SessionId sid : b.spectators)
			{
				const auto cit = s.conns.find(sid);
				if (cit == s.conns.end())
					continue;
				Impl::Conn &c = cit->second;
				if (c.session == nullptr)
					continue;
				(void)c.session->push(result, c.outbound);
			}
			b.spectators.clear();
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
		for (const auto sid : b.members)
		{
			const auto slot_it = b.slot_of.find(sid);
			if (slot_it != b.slot_of.end())
			{
				const int slot = static_cast<int>(slot_it->second);
				if (!b.field.at(slot).has_ride || b.field.at(slot).ride_hp <= 0)
				{
					dismountPet(sid);
				}
			}
		}
		const auto members = b.members;
		s.retireBattle(id);
		if (s.storage)
			for (auto sid : members)
				saveCharacter(sid, false, 0);
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

	if (s.getRidingPet(session) != nullptr)
	{
		const auto rit = s.player_rides.find(session);
		if (SA::Model::Player *p = s.players.resolve(b.player_of_slot[slot]))
		{
			b.ride_pet_of_slot[slot] = p->pets[static_cast<std::size_t>(rit->second.pet_slot)];
		}
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

std::size_t World::battleCount() const noexcept { return _impl->battles.size(); }

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
		battle.spectators.erase(std::remove(battle.spectators.begin(), battle.spectators.end(), id), battle.spectators.end());
		if (member && battle.members.empty())
			abandoned.push_back(battle.id);
		else if (member)
			s.pushBattleSnapshot(battle);
	}
	for (auto battle : abandoned)
		s.retireBattle(battle);
}

void World::onBattleCommand(SA::Net::SessionId id,
                            const SA::Domain::BattleCommand &cmd)
{
	Impl &s = *_impl;
	const auto bit = s.battles.find(cmd.battle_id);
	if (bit == s.battles.end())
		return;
	BattleInstance &b = bit->second;

	// 观战者不占用战斗槽位，其 ESCAPE 命令为即时离场，不依赖当前回合号对齐
	if (std::find(b.spectators.begin(), b.spectators.end(), id) != b.spectators.end())
	{
		if (cmd.command_kind == SA::Domain::BattleCommand::CommandKind::ESCAPE)
		{
			(void)leaveSpectate(id);
		}
		return;
	}

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

const BattleStats *World::stats(BattleId id) const
{
	const auto it = _impl->battles.find(id);
	if (it != _impl->battles.end())
		return &it->second.stats;
	const auto done = _impl->finished_battles.find(id);
	return done == _impl->finished_battles.end() ? nullptr : &done->second.stats;
}

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

const SA::Rules::BattleField *World::battleField(BattleId id) const
{
	const auto it = _impl->battles.find(id);
	if (it != _impl->battles.end())
		return &it->second.field;
	const auto done = _impl->finished_battles.find(id);
	return done == _impl->finished_battles.end() ? nullptr : &done->second.field;
}

bool World::spectateBattle(BattleId battle, SA::Net::SessionId session)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;
	const auto bit = s.battles.find(battle);
	if (bit == s.battles.end())
		return false;

	BattleInstance &b = bit->second;
	if (b.stats.finished)
		return false;

	const auto cit = s.conns.find(session);
	if (cit == s.conns.end() || cit->second.session == nullptr)
		return false;

	if (cit->second.session->state() == SA::Net::SessionState::kAnonymous ||
	    cit->second.session->closed())
	{
		return false;
	}

	if (std::find(b.members.begin(), b.members.end(), session) != b.members.end() ||
	    std::find(b.spectators.begin(), b.spectators.end(), session) != b.spectators.end())
	{
		return false;
	}

	// 观战席位限制 (最多 20 人)
	if (b.spectators.size() >= 20)
		return false;

	b.spectators.push_back(session);
	cit->second.walk_seq.clear();

	// 下发观战者身份 (slot 20 为观战槽位, 行动限制为 CANNOT_ACT_WAIT)
	SA::Domain::BattleSelfInfo self{};
	self.battle_id = b.id;
	self.slot = SA::Rules::kSlotCount;
	self.mp = 0;
	self.menu_flags = 0;
	self.cannot_act = SA::Domain::CannotActReason::CANNOT_ACT_NONE;
	(void)cit->second.session->push(self, cit->second.outbound);

	s.pushBattleSnapshot(b);

	SA::Domain::BattleTurnBegin begin{};
	begin.battle_id = b.id;
	begin.turn = b.field.turn;
	begin.ready_mask = readyMask(b);
	(void)cit->second.session->push(begin, cit->second.outbound);

	s.logger.log(SA::Platform::LogLevel::kInfo,
	             SA::Platform::LogEvent::kBattleJoined,
	             {{"battle_id", b.id},
	              {"session_id", static_cast<std::uint64_t>(session)},
	              {"slot", static_cast<std::uint64_t>(SA::Rules::kSlotCount)}});

	return true;
}

bool World::leaveSpectate(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	for (auto &entry : s.battles)
	{
		auto &b = entry.second;
		auto it = std::find(b.spectators.begin(), b.spectators.end(), session);
		if (it != b.spectators.end())
		{
			b.spectators.erase(it);
			SA::Domain::BattleLeave leave{};
			leave.battle_id = b.id;
			leave.reason = 0;
			if (auto cit = s.conns.find(session); cit != s.conns.end() && cit->second.session)
				(void)cit->second.session->push(leave, cit->second.outbound);
			return true;
		}
	}
	return false;
}

bool World::spectatePlayer(SA::Net::SessionId spectator, SA::Net::SessionId target_player)
{
	Impl &s = *_impl;
	if (spectator == target_player)
		return false;
	if (s.inBattle(spectator))
		return false;

	BattleId target_battle = 0;
	for (const auto &entry : s.battles)
	{
		const auto &b = entry.second;
		if (!b.stats.finished && b.slot_of.find(target_player) != b.slot_of.end())
		{
			target_battle = b.id;
			break;
		}
	}
	if (target_battle == 0)
		return false;

	const auto *sp_player = s.players.resolve(s.player_of_session.find(spectator));
	const auto *tg_player = s.players.resolve(s.player_of_session.find(target_player));
	if (sp_player == nullptr || tg_player == nullptr)
		return false;
	if (sp_player->floor != tg_player->floor)
		return false;
	if (std::abs(sp_player->x - tg_player->x) > 5 || std::abs(sp_player->y - tg_player->y) > 5)
		return false;

	return spectateBattle(target_battle, spectator);
}

bool World::inBattle(SA::Net::SessionId session) const noexcept
{
	return _impl->inBattle(session);
}

bool World::isSpectating(SA::Net::SessionId session) const noexcept
{
	return _impl->isSpectating(session);
}

std::size_t World::spectatorCount(BattleId battle) const noexcept
{
	const auto it = _impl->battles.find(battle);
	if (it == _impl->battles.end())
		return 0;
	return it->second.spectators.size();
}

} // namespace SA::World
