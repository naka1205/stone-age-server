// tests/rules_battle_test.cpp —— L3 黄金用例集(阶段 0.1/1.1,批次 0)
//
// ═══════════════════════════════════════════════════════════════════════════
//  为什么这个文件是硬要求,不是"顺手加的测试"
// ═══════════════════════════════════════════════════════════════════════════
//
// `00-architecture.md` §0 把「能不能还原 8.0」分成四层,其中:
//     ③ 规则与公式 —— **能写出来,无法自证正确**(B80 只判函数存在性、不判函数体;
//                      P2 已排除反汇编;P1 原版不可运行)
// ⇒ **没有任何手段能证明本实现与原版一样。**
//
// 唯一的补偿是 §0 最后一句:
//     「L3 纯函数层是唯一能被充分测试的部分,因此它是 ③ 层的主要补偿手段。」
// 而 DR-TS6(0.1 一遍到位、放弃两段式)把它从「建议做」升级为
// **「与移植同步进行的硬要求」** —— 因为两段式那条"机械改造不易改错语义"的
// 防线已经主动放弃了。
//
// ⇒ 本文件的每条��言必须满足其一:
//     · 指到 `battle_event.c` / `battle.c` 的**行号**;
//     · 指到一条 **DR**;
//     · 是一条**实测**(并在注释里给出实测方法,可被复核)。
//   凭"看起来应该是这样"写下的断言,在 ③ 层是负资产 —— 它会把猜测固化成基线。
//
// ⚠️ 用例一旦录入,**改口径就要全批重录**(`11` §3 卷首)。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "rules/Battle.h"
#include "rules/Status.h"
#include "support/ScriptedRandom.h"

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

using namespace SA::Rules;
using SA::Domain::BattleStatus;
using SA::Domain::CannotActReason;

TEST_CASE("P1:普攻后反击并以真实反击者产出事件")
{
	// SSRC80 battle.c:7794–7807：普通攻击全部结束后才开始交替反击。
	BattleField field{};
	TurnCommands commands{};
	for (int slot : {0, 10})
	{
		auto &c = field.at(slot);
		c.occupied = true;
		c.kind = CombatantKind::kPlayer;
		c.hp = c.max_hp = 10000;
		c.level = 1;
		c.attack = 100;
		c.defense = 50;
		c.mods.no_duck = true;
		c.mods.immune_critical = true;
		commands.present[slot] = true;
		commands.commands[slot].command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		commands.commands[slot].command.attack.target = static_cast<std::uint32_t>(10 - slot);
	}
	field.at(10).mods.counter_bonus = 101; // 玩家没有 100% 上限，源码 :3515–3530。
	ScriptedRandom rng({10000});
	SA::Domain::BattleEvents events{};
	ActionEffects effects{};
	REQUIRE(resolveAction(field, commands, RulesConfig{}, rng, 0, events, effects));
	REQUIRE(events.events.size() == 4);
	CHECK(events.events[0].body.hit.attacker == 0);
	CHECK(events.events[1].body.damage.target == 10);
	CHECK(events.events[2].body.hit.attacker == 10);
	CHECK(events.events[3].body.damage.target == 0);
	CHECK((events.events[3].body.damage.flags & static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_COUNTER)) != 0);
	CHECK(events.events[3].body.damage.hp_delta == static_cast<int>(events.events[1].body.damage.hp_delta * 0.75));
}

TEST_CASE("F01/F03/F16:原表达式的截断边界与道具优先级")
{
	Combatant attacker{}, defender{};
	attacker.occupied = defender.occupied = true;
	attacker.kind = defender.kind = CombatantKind::kPlayer;
	attacker.level = defender.level = 1;
	defender.vital = defender.str = defender.tough = 1000;
	defender.dex = 0;
	ScriptedRandom status_rng({16});
	int per = 0;
	CHECK_FALSE(rollStatusAttack(false, attacker, defender, 1, 30, 30, 1.0, status_rng, &per));
	CHECK(per == 16);
	attacker.quick = 100;
	SA::Domain::BattleCommand command{};
	command.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
	ScriptedRandom high({9999});
	CHECK(computeActionDex(attacker, command, high) == 84); // U01 采用 SSRC80：120 - RAND(0, 36)。
	command.command_kind = SA::Domain::BattleCommand::CommandKind::USE_ITEM;
	ScriptedRandom zero({0});
	CHECK(computeActionDex(attacker, command, zero) == 138);
	attacker.attack = 100;
	defender.defense = 0;
	RulesConfig cfg{};
	cfg.damage_calc_percent = 100;
	ScriptedRandom damage_rng({13});
	CHECK(computeDamage(BattleField{}, attacker, defender, cfg, damage_rng) == 206);
	// 原宏 13.5 的跨度：u=.96 仍为 12，.99 可取到 13；不能改成均匀的 14 格。
	CHECK(scaleOriginalRand(0, 12.5, .96) == 12);
	CHECK(scaleOriginalRand(0, 12.5, .99) == 13);
	CHECK(scaleOriginalRand(10, 5, .99) == 7);
	CHECK(scaleOriginalRand(7, 6, .99) == 7);
}

namespace
{

// 造一个干净的战斗单位。★ 默认全 0 属性 ⇒ NoneElement() == 100(全无属性)。
Combatant makeCombatant(CombatantKind kind, int atk, int def, int quick = 0)
{
	Combatant c{};
	c.occupied = true;
	c.kind = kind;
	c.hp = c.max_hp = 1000;
	c.attack = atk;
	c.defense = def;
	c.quick = quick;
	c.fix_dex = quick;
	return c;
}

BattleField makeField()
{
	BattleField f{};
	f.battle_id = 1;
	f.turn = 1;
	// ★ FieldAttribute::kNone == 0 ⇒ FieldPower 走 default ⇒ 攻守都是 kFieldPowBase
	// ⇒ At/Df 之比恒为 1,伤害不受场地影响。批次 0 的用例一律用无属性场地,
	//   把场地这个自由度从公式验证里摘出去。
	//
	// ⚠️★ 这里有个真踩过的坑:`FieldAttribute::kNone` 与 `Element::kEarth` **都是 0**。
	//    首版 FieldPower 拿 Element 当场地编码 ⇒「无属性场地」被判成「地属性场地」
	//    ⇒ 纯地攻方系数 0.5→1.0、比值翻倍 ⇒ 伤害 ×2,而且只在特定属性组合下出现。
	//    是下面那条相克手算用例(期望 150、实得 300)把它接住的。
	//    见 constants.h 的 `FieldAttribute`。
	f.field_attribute = static_cast<std::uint8_t>(FieldAttribute::kNone);
	return f;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  1. 相克矩阵 —— 对源码逐项核对
// ═══════════════════════════════════════════════════════════════════════════
//
// 来源:`battle_event.c:908-953 BATTLE_AttrCalc`。
// 原式按「攻方属性」分五行,每行对五个守方属性加权。本用例把那五行逐项抄成断言,
// 抄的是**源码**不是文档表格 —— constants.h 的矩阵是按 Element(地水火风)
// **重排过**的,而文档表头是「无火水地风」。★ 重排出错会得到"看起来对、算出来错"
// 的矩阵,且不会有任何外部表现。
TEST_CASE("相克矩阵:与 battle_event.c:922-949 逐项一致")
{
	auto M = [](Element a, Element d)
	{
		return kElementMatrix[static_cast<int>(a)][static_cast<int>(d)];
	};

	// My_Fire(:922-926):Vs_None 1.5 · Vs_Fire 1.0 · Vs_Water 0.6 · Vs_Earth 1.0 · Vs_Wind 1.5
	CHECK(M(Element::kFire, Element::kNone) == doctest::Approx(kAjUp));
	CHECK(M(Element::kFire, Element::kFire) == doctest::Approx(kAjSame));
	CHECK(M(Element::kFire, Element::kWater) == doctest::Approx(kAjDown));
	CHECK(M(Element::kFire, Element::kEarth) == doctest::Approx(kAjSame));
	CHECK(M(Element::kFire, Element::kWind) == doctest::Approx(kAjUp));

	// My_Water(:927-931):Vs_None 1.5 · Vs_Fire 1.5 · Vs_Water 1.0 · Vs_Earth 0.6 · Vs_Wind 1.0
	CHECK(M(Element::kWater, Element::kNone) == doctest::Approx(kAjUp));
	CHECK(M(Element::kWater, Element::kFire) == doctest::Approx(kAjUp));
	CHECK(M(Element::kWater, Element::kWater) == doctest::Approx(kAjSame));
	CHECK(M(Element::kWater, Element::kEarth) == doctest::Approx(kAjDown));
	CHECK(M(Element::kWater, Element::kWind) == doctest::Approx(kAjSame));

	// My_Earth(:933-937):Vs_None 1.5 · Vs_Fire 1.0 · Vs_Water 1.5 · Vs_Earth 1.0 · Vs_Wind 0.6
	CHECK(M(Element::kEarth, Element::kNone) == doctest::Approx(kAjUp));
	CHECK(M(Element::kEarth, Element::kFire) == doctest::Approx(kAjSame));
	CHECK(M(Element::kEarth, Element::kWater) == doctest::Approx(kAjUp));
	CHECK(M(Element::kEarth, Element::kEarth) == doctest::Approx(kAjSame));
	CHECK(M(Element::kEarth, Element::kWind) == doctest::Approx(kAjDown));

	// My_Wind(:939-943):Vs_None 1.5 · Vs_Fire 0.6 · Vs_Water 1.0 · Vs_Earth 1.5 · Vs_Wind 1.0
	CHECK(M(Element::kWind, Element::kNone) == doctest::Approx(kAjUp));
	CHECK(M(Element::kWind, Element::kFire) == doctest::Approx(kAjDown));
	CHECK(M(Element::kWind, Element::kWater) == doctest::Approx(kAjSame));
	CHECK(M(Element::kWind, Element::kEarth) == doctest::Approx(kAjUp));
	CHECK(M(Element::kWind, Element::kWind) == doctest::Approx(kAjSame));

	// My_None(:945-949):Vs_None 1.0,其余四项全 0.6
	CHECK(M(Element::kNone, Element::kNone) == doctest::Approx(kAjSame));
	CHECK(M(Element::kNone, Element::kFire) == doctest::Approx(kAjDown));
	CHECK(M(Element::kNone, Element::kWater) == doctest::Approx(kAjDown));
	CHECK(M(Element::kNone, Element::kEarth) == doctest::Approx(kAjDown));
	CHECK(M(Element::kNone, Element::kWind) == doctest::Approx(kAjDown));
}

// ★ 量纲自洽(`05` §3.4):因 Σatk = Σdef = 100,全无属性时系数恰为 1.0。
//   这条是**可手算**的:100 × 100 × 1.0 / 10000 == 1。
TEST_CASE("相克:全无属性时量纲自洽,系数恰为 1.0")
{
	const auto atk = makeCombatant(CombatantKind::kPlayer, 100, 50);
	const auto def = makeCombatant(CombatantKind::kEnemy, 100, 50);

	REQUIRE(atk.noneElement() == kAttrMax); // 未设四属 ⇒ 无属余量 100
	REQUIRE(def.noneElement() == kAttrMax);
	CHECK(elementCoefficient(atk, def) == doctest::Approx(1.0));

	// 结算路径同样应恒等(全无属性 ⇒ 无放大也无衰减)。
	const auto field = makeField();
	CHECK(applyElementMatrix(field, atk, def, 100) == 100);
	CHECK(applyElementMatrix(field, atk, def, 1) == 1);
}

// ★ 相克的手算基准:地 100 攻 vs 水 100 守,系数 1.5(kElementMatrix[地][水])。
//   手算:at_scaled[地] = 100 × damage;Σ = (100·damage) × 100 × 1.5;
//        /10000 ⇒ damage × 1.5。
TEST_CASE("相克:地攻水守 = 1.5 倍(手算基准)")
{
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 50);
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 50);
	atk.elements[static_cast<int>(Element::kEarth)] = kAttrMax; // 纯地
	def.elements[static_cast<int>(Element::kWater)] = kAttrMax; // 纯水
	REQUIRE(atk.noneElement() == 0);
	REQUIRE(def.noneElement() == 0);

	CHECK(elementCoefficient(atk, def) == doctest::Approx(1.5));

	const auto field = makeField();
	CHECK(applyElementMatrix(field, atk, def, 100) == 150);
	CHECK(applyElementMatrix(field, atk, def, 200) == 300);

	// 反向:水攻地守 = 0.6(被克)。
	CHECK(elementCoefficient(def, atk) == doctest::Approx(kAjDown));
	CHECK(applyElementMatrix(field, def, atk, 100) == 60);
}

// ═══════════════════════════════════════════════════════════════════════════
//  2. ★★ 三分段:没有"窄缝" —— 一条推翻文档的实测,写成回归断言
// ═══════════════════════════════════════════════════════════════════════════
//
// `05-battle.md` §3.1 原称:第二分支上界用浮点 `defense*8.0/7.0`、第三分支下界用
// **整数除法** `defense*8/7` ⇒ 边界处存在**两分支都不命中的窄缝**,damage 保持 0。
//
// 2026-08-31 对源码复核,**两个论断都不成立**:
//   ① `battle_event.c:1164` 是 `float attack, defense;` ⇒ 不是整数除法;
//   ② 穷举实测:float 口径扫 2,862 万组 → **0 组窄缝**;
//      即便按原文的整数除法口径扫 20 万 defense → **0 组窄缝、171,429 组重叠**。
//      整数除法只会让下界**变小** ⇒ 产生的是**重叠**,而 `else if` 让第二分支接管。
//
// ⚠️ 若照原文实现"窄缝返回 0",会引入原版没有的行为。本用例把这条钉死:
//    在阈值附近**没有任何一组** (attack, defense) 会因"两分支都不命中"而返回 0。
TEST_CASE("三分段:无窄缝,阈值边界走第二分支而非返回 0")
{
	const auto field = makeField();
	RulesConfig cfg{};
	cfg.damage_calc_percent = 100; // 摘掉第 7 步,单独看分段

	// 复刻源码 `:1225-1235` 的分支判定(用同样的 float 类型与表达式形状)。
	auto branch_of = [](int a, int d) -> int
	{
		const float attack = static_cast<float>(a);
		const float defense = static_cast<float>(d);
		if (defense <= attack && attack < (defense * 8.0 / 7.0))
			return 2;
		if (defense > attack)
			return 1;
		if (attack >= (defense * 8 / 7))
			return 3;
		return 0; // ← 若"窄缝"存在,会命中这里
	};

	int gap = 0;
	for (int d = 1; d <= 4000; ++d)
	{
		const int lo = d;             // 从 attack == defense 起
		const int hi = d * 8 / 7 + 2; // 扫过阈值
		for (int a = lo; a <= hi; ++a)
		{
			if (branch_of(a, d) == 0)
				++gap;
		}
	}
	CHECK(gap == 0); // ★ 这一条就是"文档说的窄缝不存在"

	// 阈值上的那一格必须产出**第二分支**的分布 RAND(0, attack/16),而不是恒 0。
	// 取 attack = defense 恰好相等的情形:落第二分支,damage ∈ [0, attack/16]。
	auto atk = makeCombatant(CombatantKind::kPlayer, 1000, 0);
	auto def = makeCombatant(CombatantKind::kEnemy, 0, 0);
	// defense = DEF × 0.70;要让 defense == attack,取 DEF = attack / 0.7
	def.defense = static_cast<std::int32_t>(1000 / kDefenseCoefNewPower);

	bool saw_nonzero = false;
	for (std::uint64_t seed = 1; seed <= 64 && !saw_nonzero; ++seed)
	{
		SeededRandom rng(seed);
		if (computeDamage(field, atk, def, cfg, rng) > 0)
			saw_nonzero = true;
	}
	CHECK(saw_nonzero); // 若实现里塞了"窄缝返回 0",这里会恒 0 而失败
}

// ═══════════════════════════════════════════════════════════════════════════
//  3. 伤害主公式的三条分段 —— 手算区间
// ═══════════════════════════════════════════════════════════════════════════

// 第一分支(源码顺序的第二个):`defense > attack` ⇒ damage = RAND(0,1)
// ⇒ 相克全无属性(×1)、第 7 步 ×0.70 ⇒ 最终 ∈ {0}(1×70/100 == 0,整数除法)。
// ★ 这条同时钉住第 7 步是**整数除法**:1 × 70 / 100 == 0,不是 0.7。
TEST_CASE("伤害:防高于攻 ⇒ RAND(0,1),且第 7 步整数除法把 1 压成 0")
{
	const auto field = makeField();
	const RulesConfig cfg{};                // damage_calc_percent 默认 70
	REQUIRE(cfg.damage_calc_percent == 70); // ★ 默认 70 不是 100(`08` / §3.1 第 7 步)

	const auto atk = makeCombatant(CombatantKind::kPlayer, 10, 0);
	const auto def = makeCombatant(CombatantKind::kEnemy, 0, 1000); // defense = 700 > 10

	for (std::uint64_t seed = 1; seed <= 32; ++seed)
	{
		SeededRandom rng(seed);
		CHECK(computeDamage(field, atk, def, cfg, rng) == 0);
	}
}

// 第三分支:`attack >= defense·8/7` ⇒ K0 = RAND(0, attack/8) − attack/16
//                                    damage = (attack − defense)·2.0 + K0
//
// ⚠️★ 守方**必须用非敌人**,否则第 2 步的 `_NPCENEMY_ADDPOWER` 修正会介入:
//    `defense += (defense·(rand()%10) + 2)/100` ⇒ defense 变成一个区间而非定值,
//    手算基准就不成立了。(2026-08-31 首次写这条用例时正是拿敌人当守方,
//    实测最低 1250 而手算下界 1258 —— 差的 8 点恰好是那条修正,
//    ⇒ **失败本身证明了该修正生效**。它现在由下一条用例单独覆盖。)
//
// 手算(attack = 1000,DEF = 100 ⇒ defense = 100 × 0.70 = 70,守方为玩家不加修正):
//     K0 ∈ [0 − 62.5, 125 − 62.5] = [−62.5, +62.5]
//     damage = (1000 − 70)·2.0 + K0 = 1860 + K0 ∈ [1797.5, 1922.5]
//
// ⚠️★ **截断链必��一起算进去,否则边界会差 1 点**:
//     ① 第 3 步 `static_cast<int32_t>(1797.5)` → **1797**(不是四舍五入)
//     ② 相克 ×1.0(全无属性)⇒ 不变
//     ③ 第 7 步 `1797 * 70 / 100` 是**整数除法** → **1257**
//   ⇒ 下界 1257,**不是** 1797.5 × 0.7 = 1258.25 取整的 1258。
//   (2026-08-31 首版就是按后者写的,实测 1257 打脸 —— 差的那 1 点正是 ① 的截断。)
//     上界同理:1922.5 → 1922 → 1922 × 70 / 100 = 1345。
TEST_CASE("伤害:攻远高于防 ⇒ 落第三分支,结果在手算区间内")
{
	const auto field = makeField();
	const RulesConfig cfg{};

	const auto atk = makeCombatant(CombatantKind::kPlayer, 1000, 0);
	const auto def = makeCombatant(CombatantKind::kPlayer, 0, 100); // ★ 非敌人

	for (std::uint64_t seed = 1; seed <= 256; ++seed)
	{
		SeededRandom rng(seed);
		const auto dmg = computeDamage(field, atk, def, cfg, rng);
		CHECK(dmg >= 1257);
		CHECK(dmg <= 1345);
	}
}

// ★ `_NPCENEMY_ADDPOWER`(8.0 开):守方是敌人时 defense 上浮,攻方是敌人时 attack 上浮。
//   `battle_event.c:1200-1207`。
//
// ⚠️★ 这条同时钉住 battle.h 移植注记 ①:该式是**浮点除法**,不是文档说的整数除法。
//    若真是整数除法,`rand()%10 == 0` 时该项为 0;浮点下是 0.02。
//    ⇒ 守方为敌时,defense 的取值范围是 [70.02, 76.32] 而非 [70, 76.3]。
//    在本用例的量级上两者都会让伤害低于非敌基线,故这里断言的是**方向**:
//    敌人守方一定不比玩家守方更脆。
TEST_CASE("伤害:守方是敌人 ⇒ 防御上浮(_NPCENEMY_ADDPOWER 生效)")
{
	const auto field = makeField();
	const RulesConfig cfg{};
	const auto atk = makeCombatant(CombatantKind::kPlayer, 1000, 0);

	const auto def_player = makeCombatant(CombatantKind::kPlayer, 0, 100);
	const auto def_enemy = makeCombatant(CombatantKind::kEnemy, 0, 100);

	// defense 上浮 ⇒ (attack − defense) 变小 ⇒ 伤害下界被拉低。
	// 手算下界:defense 最高 = 70 + (70×9 + 2)/100 = 76.32
	//          damage = (1000 − 76.32)×2 − 62.5 = 1784.86 → 截断 1784
	//                 → 1784 × 70 / 100 = 1248
	int below_player_floor = 0;
	for (std::uint64_t seed = 1; seed <= 512; ++seed)
	{
		SeededRandom rng(seed);
		const auto dmg = computeDamage(field, atk, def_enemy, cfg, rng);
		CHECK(dmg >= 1248); // ★ defense 最高 76.32 时的下界(含两次截断)
		CHECK(dmg <= 1345);
		if (dmg < 1257)
			++below_player_floor; // 跌破玩家守方的下界
	}
	// 至少有一部分样本跌破玩家基线 ⇒ 证明修正确实在动,而不是恒等。
	CHECK(below_player_floor > 0);

	// 同种子下,敌人守方的伤害不应高于玩家守方(防御只增不减)。
	// ⚠️ 不能逐样本比 —— 两者消耗的随机数个数不同(敌人多一次 RandMod),
	//    序列会错开。⇒ 比**均值**。
	auto mean = [&](const Combatant &d)
	{
		long long sum = 0;
		for (std::uint64_t seed = 1; seed <= 512; ++seed)
		{
			SeededRandom rng(seed);
			sum += computeDamage(field, atk, d, cfg, rng);
		}
		return static_cast<double>(sum) / 512;
	};
	CHECK(mean(def_enemy) < mean(def_player));
}

// ★ 第 7 步的系数必须是配置项且默认 70 —— 写成 100 会让全局伤害偏高 43%。
TEST_CASE("伤害:damage_calc_percent 生效且默认 70(偏高 43% 的经典错误)")
{
	const auto field = makeField();
	const auto atk = makeCombatant(CombatantKind::kPlayer, 1000, 0);
	const auto def = makeCombatant(CombatantKind::kPlayer, 0, 100);

	RulesConfig c70{};
	RulesConfig c100{};
	c100.damage_calc_percent = 100;

	SeededRandom r1(12345), r2(12345);
	const auto d70 = computeDamage(field, atk, def, c70, r1);
	const auto d100 = computeDamage(field, atk, def, c100, r2);

	REQUIRE(d70 > 0);
	CHECK(d100 > d70);
	// ⚠️ 不做逐位反推 —— `damage * 70 / 100` 是整数除法,不可逆。
	//    只断言比例关系(容差覆盖那一次截断)。
	CHECK(static_cast<double>(d100) == doctest::Approx(d70 / 0.7).epsilon(0.01));
}

// ═══════════════════════════════════════════════════════════════════════════
//  4. ★★ 可回放 —— D2 的地基
// ═══════════════════════════════════════════════════════════════════════════
//
// `01` §10:「同一随机种子 + 同一输入,结果必须逐位相同。」
// 这是 ③ 层"无法与原版比对"时**唯一**能做的比对:与自己的历史行为比对。
// ⇒ 一旦有人在 L3 里偷偷调了 std::rand / 读了时钟,这条会立刻红。
TEST_CASE("可回放:同种子 + 同输入 ⇒ 逐位相同")
{
	const auto field = makeField();
	const RulesConfig cfg{};
	const auto atk = makeCombatant(CombatantKind::kPlayer, 800, 0, 120);
	const auto def = makeCombatant(CombatantKind::kEnemy, 0, 150, 90);

	for (std::uint64_t seed : {1ull, 7ull, 4242ull, 0xDEADBEEFull})
	{
		SeededRandom a(seed), b(seed);
		for (int i = 0; i < 50; ++i)
		{
			CHECK(computeDamage(field, atk, def, cfg, a) ==
			      computeDamage(field, atk, def, cfg, b));
		}
	}

	// 不同种子应当给出不同序列(否则"注入随机源"名存实亡)。
	SeededRandom s1(1), s2(2);
	bool differs = false;
	for (int i = 0; i < 50 && !differs; ++i)
	{
		if (computeDamage(field, atk, def, cfg, s1) !=
		    computeDamage(field, atk, def, cfg, s2))
			differs = true;
	}
	CHECK(differs);
}

// ═══════════════════════════════════════════════════════════════════════════
//  5. CheckCanAct —— DR-BT5 的唯一真源
// ═══════════════════════════════════════════════════════════════════════════
//
// 来源:`battle.c:8059 BATTLE_CanMoveCheck`,8.0 下 `_MAGIC_BARRIER` /
// `_PROFESSION_SKILL` / `_PROFESSION_ADDSKILL` 全为开 ⇒ 8 项否决全部生效。
//
// ★ DR-BT5 的裁定是**修正**:统一为这 8 项,上行校验与结算共用本函数,
//   并把原因下发给客户端 → 菜单置灰(DR-CP7)。
//   ⇒ 原版那套 `checkErrorStatus` 的 5 项判据**不再存在**,不留第二份实现。
TEST_CASE("CheckCanAct:8 项否决逐条覆盖(DR-BT5)")
{
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 100);

	CHECK(checkCanAct(c) == CannotActReason::CANNOT_ACT_NONE);

	struct Case
	{
		BattleStatus st;
		CannotActReason want;
		const char *name;
	};
	const Case cases[] = {
	    {BattleStatus::BATTLE_ST_PARALYSIS, CannotActReason::CANNOT_ACT_PARALYSIS, "麻痹"},
	    {BattleStatus::BATTLE_ST_STONE, CannotActReason::CANNOT_ACT_STONE, "石化"},
	    {BattleStatus::BATTLE_ST_SLEEP, CannotActReason::CANNOT_ACT_SLEEP, "睡眠"},
	    {BattleStatus::BATTLE_ST_DIZZY, CannotActReason::CANNOT_ACT_DIZZY, "晕眩"},
	    {BattleStatus::BATTLE_ST_DRAGNET, CannotActReason::CANNOT_ACT_DRAGNET, "天罗地网"},
	    // ★ 以下三项是 DR-BT5 修正的核心:原版**上行不拒绝、结算否决**
	    {BattleStatus::BATTLE_ST_BARRIER, CannotActReason::CANNOT_ACT_BARRIER, "魔障"},
	    {BattleStatus::BATTLE_ST_T_ENCLOSE, CannotActReason::CANNOT_ACT_T_ENCLOSE, "雷附体"},
	};
	for (const auto &k : cases)
	{
		CAPTURE(k.name);
		c.status = static_cast<std::uint8_t>(k.st);
		CHECK(checkCanAct(c) == k.want);
	}

	// 第 8 项:世界末日集气 —— ★ 独立字段,不是 status 槽值(见 combatant.h)。
	c.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_NONE);
	c.charging_turns = 3;
	CHECK(checkCanAct(c) == CannotActReason::CANNOT_ACT_CHARGING);

	// ⚠️ 与状态并存时,按源码判定顺序**状态优先**(集气在最后一项)。
	c.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_PARALYSIS);
	CHECK(checkCanAct(c) == CannotActReason::CANNOT_ACT_PARALYSIS);
}

// ⚠️ 不在 8 项里的状态**不得**否决行动 —— 这条挡的是"顺手多加一项"。
// 例:毒 / 混乱 / 沉默 / 遗忘都不影响能否行动。
TEST_CASE("CheckCanAct:8 项之外的状态不否决行动")
{
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 100);
	for (auto st : {BattleStatus::BATTLE_ST_POISON, BattleStatus::BATTLE_ST_CONFUSION,
	                BattleStatus::BATTLE_ST_NOCAST, BattleStatus::BATTLE_ST_OBLIVION,
	                BattleStatus::BATTLE_ST_DRUNK, BattleStatus::BATTLE_ST_WEAKEN})
	{
		c.status = static_cast<std::uint8_t>(st);
		CHECK(checkCanAct(c) == CannotActReason::CANNOT_ACT_NONE);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
//  6. 回避 —— 七道前置(★ 比 05 §3.2 的清单多一道)
// ═══════════════════════════════════════════════════════════════════════════
//
// 来源:`battle_event.c:754 BATTLE_DuckCheck`。
// ⚠️ `05` §3.2 写"六道前置否决",实测源码是 **6 道否决 + 1 道必闪**,
//    且清单**漏了 `CHAR_BATTLEFLG_ABIO`**(`:797`)。
TEST_CASE("回避:七道前置逐条覆盖(★ 含 05 §3.2 漏记的 ABIO)")
{
	const RulesConfig cfg{};
	// 造一个回避率会很高的守方(dex 差极大),这样"没被否决"时几乎必闪 ——
	// 否则无法区分"被否决"与"没闪中"。
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 1);
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 100, 100000);

	// 基线:七道都不命中 ⇒ 应当闪掉(高 dex 差 ⇒ per 打到 75% 上限)。
	{
		int dodged = 0;
		for (std::uint64_t s = 1; s <= 200; ++s)
		{
			SeededRandom rng(s);
			if (rollDodge(atk, def, /*guarding=*/false, /*casting=*/false, cfg, rng))
				++dodged;
		}
		CHECK(dodged > 100); // 75% 上限 ⇒ 200 次里应远多于 100
	}

	auto never_dodges = [&](const Combatant &a, const Combatant &d,
	                        bool guarding, bool casting)
	{
		for (std::uint64_t s = 1; s <= 100; ++s)
		{
			SeededRandom rng(s);
			if (rollDodge(a, d, guarding, casting, cfg, rng))
				return false;
		}
		return true;
	};

	SUBCASE("① 攻方集气完成 ⇒ 不可回避")
	{
		auto a = atk;
		a.charge_ready = true;
		CHECK(never_dodges(a, def, false, false));
	}
	SUBCASE("② 守方防御 ⇒ 不可回避")
	{
		CHECK(never_dodges(atk, def, /*guarding=*/true, false));
	}
	SUBCASE("③ 守方有反应类状态 ⇒ 不可回避")
	{
		auto d = def;
		d.damage_react = 1;
		CHECK(never_dodges(atk, d, false, false));
	}
	SUBCASE("④ 守方不能行动 ⇒ 不可回避")
	{
		auto d = def;
		d.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_SLEEP);
		CHECK(never_dodges(atk, d, false, false));
	}
	SUBCASE("⑤ NODUCK ⇒ 不可回避")
	{
		auto d = def;
		d.mods.no_duck = true;
		CHECK(never_dodges(atk, d, false, false));
	}
	SUBCASE("⑥ ★ ABIO ⇒ 不可回避(05 §3.2 漏记的一道)")
	{
		auto d = def;
		d.mods.abio = true;
		CHECK(never_dodges(atk, d, false, false));
	}
	SUBCASE("⑦ 必闪技 ⇒ 恒回避,且优先于概率")
	{
		// 用一个 dex 差为 0 的守方 —— 正常情况下 per 会被压到下限,几乎闪不掉。
		auto d = makeCombatant(CombatantKind::kEnemy, 100, 100, 1);
		d.mods.always_dodge = true;
		for (std::uint64_t s = 1; s <= 100; ++s)
		{
			SeededRandom rng(s);
			CHECK(rollDodge(atk, d, false, false, cfg, rng));
		}
	}
}

// ★ `_PROFESSION_ADDSKILL`(8.0 开)的例外:**集气中仍可闪避**,
//   除非同时处于天罗地网或晕眩(`battle_event.c:779-788`)。
//   ⚠️ 这条与第 ④ 道否决直接冲突,是原版有意留的口子 —— 照抄。
TEST_CASE("回避:集气中仍可闪(_PROFESSION_ADDSKILL 的例外),但天罗/晕眩时不行")
{
	const RulesConfig cfg{};
	const auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 1);

	auto charging = makeCombatant(CombatantKind::kEnemy, 100, 100, 100000);
	charging.charging_turns = 3; // ⇒ CheckCanAct 判 CANNOT_ACT_CHARGING
	REQUIRE(checkCanAct(charging) == CannotActReason::CANNOT_ACT_CHARGING);

	int dodged = 0;
	for (std::uint64_t s = 1; s <= 200; ++s)
	{
		SeededRandom rng(s);
		if (rollDodge(atk, charging, false, false, cfg, rng))
			++dodged;
	}
	CHECK(dodged > 100); // ★ 集气中照样能闪

	// 但天罗地网 / 晕眩会把这个口子关上。
	for (auto st : {BattleStatus::BATTLE_ST_DRAGNET, BattleStatus::BATTLE_ST_DIZZY})
	{
		auto pinned = charging;
		pinned.status = static_cast<std::uint8_t>(st);
		for (std::uint64_t s = 1; s <= 100; ++s)
		{
			SeededRandom rng(s);
			CHECK_FALSE(rollDodge(atk, pinned, false, false, cfg, rng));
		}
	}
}

// ★ 咒术时更易被闪:gKawashiPara 0.027 vs 0.02 ⇒ 分母大 ⇒ Work 小 ⇒ per 小。
//   ⚠️ 注意方向:参数变大**降低**回避率。这与"更易被闪"是同一件事
//     (守方在念咒 ⇒ 守方**自己**闪避率下降)。
TEST_CASE("回避:守方咒术时回避率更低(0.027 vs 0.02)")
{
	const RulesConfig cfg{};
	const auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 1);
	// dex 差要小到不会撞上 75% 上限,否则两档都被钳平、看不出差别。
	const auto def = makeCombatant(CombatantKind::kEnemy, 100, 100, 30);

	int normal = 0, casting = 0;
	for (std::uint64_t s = 1; s <= 2000; ++s)
	{
		SeededRandom r1(s), r2(s);
		if (rollDodge(atk, def, false, /*casting=*/false, cfg, r1))
			++normal;
		if (rollDodge(atk, def, false, /*casting=*/true, cfg, r2))
			++casting;
	}
	CHECK(casting < normal);
}

// 回避率的硬上限 75%(`KAWASHI_MAX_RATE`,`battle_event.c:871`)。
TEST_CASE("回避:硬上限 75%,极端 dex 差也不会必闪")
{
	const RulesConfig cfg{};
	const auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 0);
	const auto def = makeCombatant(CombatantKind::kEnemy, 100, 100, 1000000000);

	int dodged = 0;
	const int trials = 4000;
	for (std::uint64_t s = 1; s <= static_cast<std::uint64_t>(trials); ++s)
	{
		SeededRandom rng(s);
		if (rollDodge(atk, def, false, false, cfg, rng))
			++dodged;
	}
	const double rate = static_cast<double>(dodged) / trials;
	CHECK(rate > 0.70);
	CHECK(rate < 0.80); // ★ 上限 75% ⇒ 不可能接近 1.0
}

// ═══════════════════════════════════════════════════════════════════════════
//  7. 回合调度(批次 0.5)
// ═══════════════════════════════════════════════════════════════════════════
//
// ⚠️ 本节断言的口径与前六节一致:每条要么指到源码行号,要么指到一条 DR,
//    要么是可复核的实测。★ 暴击与反击**没有用例** —— 因为它们没有实现,
//    而没有实现的理由(文档只给了 per 的构成、没给判定阈)本身就写在
//    `battle.h` 的批次 0.5 注记里。**不为未实现的东西写"预期"用例**:
//    那会把猜测提前固化成基线。

namespace
{

// 用例集专用 rng 存根 ⇒ `tests/support/ScriptedRandom.h`(**唯一一份**)。
// ★★ 此前三个用例集各带一份拷贝,而实测三份已经漂了(`randMod` 的退化分支自相矛盾、
//    `calls()` 只有两份有)⇒ 见该头文件卷首与 DR-BT23。

// 恒取上界的随机源。★ 回避判定是 `RAND(1,10000) <= per` 而 per 硬上限 7500
//   ⇒ 取 10000 时**必不闪避**,把回避这个自由度从调度用例里摘出去。
class MaxRandom final : public Random
{
  public:
	int rand(int lo, int hi) override { return hi > lo ? hi : lo; }
	double randReal(double lo, double hi) override
	{
		return scaleOriginalRand(lo, hi, (2147483647.0 / 2147483648.0));
	}
	int randMod(int n) override { return n > 0 ? n - 1 : 0; }
};

TurnCommands noCommands() { return TurnCommands{}; }

void setAttack(TurnCommands &tc, int slot, int target)
{
	tc.present[slot] = true;
	tc.commands[slot] = SA::Domain::BattleCommand{};
	tc.commands[slot].command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
	tc.commands[slot].command.attack.target = static_cast<std::uint32_t>(target);
}

void setKind(TurnCommands &tc, int slot,
             SA::Domain::BattleCommand::CommandKind kind)
{
	tc.present[slot] = true;
	tc.commands[slot] = SA::Domain::BattleCommand{};
	tc.commands[slot].command_kind = kind;
}

std::size_t countKind(const SA::Domain::BattleEvents &ev,
                      SA::Domain::BattleEvent::BodyKind kind)
{
	std::size_t n = 0;
	for (std::size_t i = 0; i < ev.events.size(); ++i)
		if (ev.events[i].body_kind == kind)
			++n;
	return n;
}

} // namespace

// ── 行动顺序 ───────────────────────────────────────────────────────────────

TEST_CASE("行动顺序:零扰动时 dex = quick + 20,先攻在 dex 下限之后相加")
{
	// `BATTLE_DexCalc` 基数 = WORKQUICK + 20(05 §2.5)。
	// 固定随机值 0，排除扰动；quick == 0 时随机区间仍是 RAND(0, 6)。
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 100, /*quick=*/0);
	SA::Domain::BattleCommand cmd{};
	cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;

	ScriptedRandom rng({0});
	CHECK(computeActionDex(c, cmd, rng) == kDexBase);

	// 装备「先攻」直接加在排序键上。
	c.mods.sequence = 7;
	CHECK(computeActionDex(c, cmd, rng) == kDexBase + 7);

	// SSRC80 battle.c:4312 夹的是 dex；4144 的排序比较之后才加 sequence。
	// 负的合成测试修正不应把下限擅自挪到最终排序键上。
	c.mods.sequence = -100;
	CHECK(computeActionDex(c, cmd, rng) == -80);
}

TEST_CASE("F03/U01:SSRC80 的 0.3 扰动、用药优先级与浮点尾值")
{
	// SSRC80 battle.c:4297–4307。quick=100 时 w=120，RAND(0,36) 后普攻 84..120、用药 102..138。
	for (bool item : {false, true})
	{
		auto c = makeCombatant(CombatantKind::kPlayer, 100, 100, 100);
		SA::Domain::BattleCommand cmd{};
		cmd.command_kind = item ? SA::Domain::BattleCommand::CommandKind::USE_ITEM
		                        : SA::Domain::BattleCommand::CommandKind::ATTACK;
		for (int jitter : {0, 12, 36})
		{
			ScriptedRandom rng({jitter});
			CHECK(computeActionDex(c, cmd, rng) == 120 - jitter + (item ? 18 : 0));
			CHECK(rng.calls() == 1);
		}
		// w=21：原 RAND 的浮点上端 6.3 可产 7，不能提前转成 int(6.3)。
		c.quick = 1;
		ScriptedRandom upper({9999});
		CHECK(computeActionDex(c, cmd, upper) == (item ? 17 : 14));
		CHECK(upper.calls() == 1);
	}
}

TEST_CASE("F03/U01:先完成整数转换再夹 dex 下限，仍消耗一次随机数")
{
	// SSRC80 battle.c:4215 的 int dex，以及 4299/4307 赋值后 4312 的 <=0 门。
	for (bool item : {false, true})
	{
		auto c = makeCombatant(CombatantKind::kPlayer, 100, 100);
		SA::Domain::BattleCommand cmd{};
		cmd.command_kind = item ? SA::Domain::BattleCommand::CommandKind::USE_ITEM
		                        : SA::Domain::BattleCommand::CommandKind::ATTACK;
		for (int quick : {-21, -20, -19})
		{
			c.quick = quick;
			c.fix_dex = quick;
			// w=1、jitter=1 时，用药表达式为 0.15，赋给 int 为 0，随后夹到 1。
			ScriptedRandom rng({9999});
			CHECK(computeActionDex(c, cmd, rng) == 1);
			CHECK(rng.calls() == 1);
		}
		c.quick = -20;
		c.mods.sequence = 7;
		ScriptedRandom rng({0});
		CHECK(computeActionDex(c, cmd, rng) == 8); // clamp(0)=1，再加先攻 7。
		CHECK(rng.calls() == 1);
	}
}

TEST_CASE("F03/U01:0.3 扰动改变实际先后手，用药优先级参与同一排序")
{
	BattleField f = makeField();
	TurnCommands tc = noCommands();
	f.at(0) = makeCombatant(CombatantKind::kPlayer, 100, 100, 100);
	f.at(1) = makeCombatant(CombatantKind::kPlayer, 100, 100, 80);
	setKind(tc, 0, SA::Domain::BattleCommand::CommandKind::ATTACK);
	setKind(tc, 1, SA::Domain::BattleCommand::CommandKind::ATTACK);
	std::uint8_t order[kSlotCount]{};
	ScriptedRandom attack_rng({36, 0});
	REQUIRE(buildActionOrder(f, tc, attack_rng, order) == 2);
	CHECK(order[0] == 1); // 100 > 84；旧 0.1 把 36 夹为 12，错误地让 108 先动。
	CHECK(order[1] == 0);
	CHECK(attack_rng.calls() == 2);
	setKind(tc, 0, SA::Domain::BattleCommand::CommandKind::USE_ITEM);
	ScriptedRandom item_rng({36, 0});
	REQUIRE(buildActionOrder(f, tc, item_rng, order) == 2);
	CHECK(order[0] == 0); // 加用药优先级后 102 > 100。
	CHECK(order[1] == 1);
	CHECK(item_rng.calls() == 2);
}

TEST_CASE("行动顺序:同速按入场位次(DR-BT8)")
{
	// ★★ 这是 DR-BT8 的回归用例。原版 `EsCmp` 是布尔比较器(小于与等于都返回 0),
	//    不满足严格弱序 ⇒ 同速顺序是标准库未定义行为(00 §10.2 六项之一)。
	//    DR-BT8 裁定「按入场位次」⇒ 全员同速时,顺序必须恰好是槽号升序。
	//    ⚠️ 若把实现里的插入排序换成 std::sort,这条就会在某些标准库上失败。
	BattleField f = makeField();
	TurnCommands tc = noCommands();
	for (int i = 0; i < kSlotCount; ++i)
	{
		f.at(i) = makeCombatant(CombatantKind::kPlayer, 100, 100, /*quick=*/0);
		f.at(i).slot = static_cast<std::uint8_t>(i);
		setKind(tc, i, SA::Domain::BattleCommand::CommandKind::WAIT);
	}

	std::uint8_t order[kSlotCount] = {};
	ScriptedRandom rng({0}); // 固定扰动才构成同速输入。
	const int n = buildActionOrder(f, tc, rng, order);
	REQUIRE(n == kSlotCount);
	for (int i = 0; i < kSlotCount; ++i)
		CHECK(order[i] == i);
}

TEST_CASE("行动顺序:快的先动;无指令 / 已死 / 空槽不入列")
{
	BattleField f = makeField();
	TurnCommands tc = noCommands();

	for (int i = 0; i < 4; ++i)
	{
		f.at(i) = makeCombatant(CombatantKind::kPlayer, 100, 100, /*quick=*/0);
		f.at(i).slot = static_cast<std::uint8_t>(i);
	}
	f.at(1).mods.sequence = 50; // 先攻差 50 大于本夹具最大扰动 6，保证先动。
	f.at(2).dead = true;        // 已死不入列
	// slot 3 有单位但**不给指令** ⇒ 不入列(05 §2.2 第 1 步:敌方由 AI 填齐)
	setKind(tc, 0, SA::Domain::BattleCommand::CommandKind::WAIT);
	setKind(tc, 1, SA::Domain::BattleCommand::CommandKind::WAIT);
	setKind(tc, 2, SA::Domain::BattleCommand::CommandKind::WAIT);

	std::uint8_t order[kSlotCount] = {};
	SeededRandom rng(7);
	const int n = buildActionOrder(f, tc, rng, order);
	REQUIRE(n == 2);
	CHECK(order[0] == 1); // sequence +50 ⇒ 先动
	CHECK(order[1] == 0);
}

// ── 攻击次数(§3.9 / DR-BT1)──────────────────────────────────────────────

TEST_CASE("攻击次数:有武器走 RAND(min,max),≤0 则 1")
{
	const RulesConfig cfg{};
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 100);
	c.level = 50;
	c.mods.unarmed = false;
	c.mods.attack_num_min = 2;
	c.mods.attack_num_max = 2;

	SeededRandom rng(1);
	CHECK(rollAttackCount(c, cfg, rng) == 2);

	// 武器数据坏成 0 / 负数时兜底 1 段(原版 `if (n <= 0) n = 1;`)。
	c.mods.attack_num_min = 0;
	c.mods.attack_num_max = 0;
	CHECK(rollAttackCount(c, cfg, rng) == 1);
}

TEST_CASE("攻击次数:空手的两道前置 —— 等级 ≥ 10 且是玩家")
{
	const RulesConfig cfg{};

	// ★ 若漏掉这两道,**所有敌人都可能触发 10 连击**。
	auto low_level = makeCombatant(CombatantKind::kPlayer, 100, 100);
	low_level.level = kUnarmedMultihitMinLevel - 1;
	low_level.luck = 25;
	auto enemy = makeCombatant(CombatantKind::kEnemy, 100, 100);
	enemy.level = 99;
	enemy.luck = 25;

	// 脚本首值 1 ⇒ 落在最优档;若前置失效就会返回多段。
	ScriptedRandom r1({1, 10});
	ScriptedRandom r2({1, 10});
	CHECK(rollAttackCount(low_level, cfg, r1) == 1);
	CHECK(rollAttackCount(enemy, cfg, r2) == 1);
}

TEST_CASE("攻击次数:空手四档逐个边界(DR-BT1 照抄)")
{
	const RulesConfig cfg{};
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 100);
	c.level = kUnarmedMultihitMinLevel;
	c.luck = 0; // ⇒ luckwork = 0,阈值恰为 10 / 30 / 70

	auto count_for = [&](int roll, int burst)
	{
		ScriptedRandom rng({roll, burst});
		return rollAttackCount(c, cfg, rng);
	};

	CHECK(count_for(10, 7) == 7); // ≤ 10 ⇒ RAND(5,10);脚本给 7
	CHECK(count_for(11, 0) == 3); // ≤ 30 ⇒ 3
	CHECK(count_for(30, 0) == 3);
	CHECK(count_for(31, 0) == 2); // ≤ 70 ⇒ 2
	CHECK(count_for(70, 0) == 2);
	CHECK(count_for(71, 0) == 1); // 否则 1

	// ★ RAND(5,10) 的两端被夹住 ⇒ 段数上限确实是 10(「空手连击可达 10 段」)。
	CHECK(count_for(1, 99) == kUnarmedBurstMax);
	CHECK(count_for(1, -99) == kUnarmedBurstMin);

	// luckwork = LUCK × 5 上限 25 ⇒ luck 25 时首档阈值 = 10 + 25 = 35。
	c.luck = 25;
	CHECK(count_for(35, 6) == 6);
	CHECK(count_for(36, 0) == 3); // 越过首档,落到「≤ 30 + 25 = 55」这一档
}

// ── 防御减伤六档(§3.5)──────────────────────────────────────────────────

TEST_CASE("防御减伤:六档逐个边界,期望系数 0.175(★ 文档的 0.155 算错了)")
{
	// 表在 constants.h(kGuardTiers)。★ 逐边界断言,挡"抄错一格"。
	auto factor_at = [](int roll)
	{
		ScriptedRandom rng({roll});
		return rollGuardFactor(rng);
	};
	CHECK(factor_at(1) == doctest::Approx(0.00));
	CHECK(factor_at(25) == doctest::Approx(0.00)); // ★ 25% 概率完全免伤
	CHECK(factor_at(26) == doctest::Approx(0.10));
	CHECK(factor_at(50) == doctest::Approx(0.10));
	CHECK(factor_at(51) == doctest::Approx(0.20));
	CHECK(factor_at(70) == doctest::Approx(0.20));
	CHECK(factor_at(71) == doctest::Approx(0.30));
	CHECK(factor_at(85) == doctest::Approx(0.30));
	CHECK(factor_at(86) == doctest::Approx(0.40));
	CHECK(factor_at(95) == doctest::Approx(0.40));
	CHECK(factor_at(96) == doctest::Approx(0.50));
	CHECK(factor_at(100) == doctest::Approx(0.50));

	// ★★ **一处被本用例当场推翻的文档数字(2026-09-03)**:
	//    `05` §3.5 与 constants.h 都写「期望系数 ≈ 0.155」——**算错了**。
	//    按档宽加权手算(不靠抽样):
	//        (25×0.00 + 25×0.10 + 20×0.20 + 15×0.30 + 10×0.40 + 5×0.50) / 100
	//      = (0 + 2.5 + 4.0 + 4.5 + 4.0 + 2.5) / 100 = **0.175**
	//    ⇒ 六档表本身与源码一致(上面 12 条边界断言),错的只是那个概括值。
	//    ⚠️ 差 0.02 看着小,但它是**全局防御强度**的口径:照 0.155 去调平衡,
	//      会把"防御比预期弱 13%"当成数值问题去改别处。
	const double expected = (25 * 0.00 + 25 * 0.10 + 20 * 0.20 +
	                         15 * 0.30 + 10 * 0.40 + 5 * 0.50) /
	                        100.0;
	CHECK(expected == doctest::Approx(0.175));
}

// ── 骑宠分摊(§3.6 / DR-BT2)─────────────────────────────────────────────

TEST_CASE("骑宠分摊:原普通伤害的分子、两次加一和防御下限")
{
	// SSRC80 battle_event.c:2105–2116；第二式使用已加一的 playerdamage。
	const RideSplit s = splitRideDamage(100, 300, 100);
	CHECK(s.player == 26);
	CHECK(s.pet == 75);
	CHECK(s.player + s.pet == 101);
	const RideSplit s2 = splitRideDamage(100, 100, 300);
	CHECK(s2.player == 76);
	CHECK(s2.pet == 25);
	const RideSplit s3 = splitRideDamage(37, 0, 0);
	CHECK(s3.player == 19);
	CHECK(s3.pet == 19);
	CHECK(splitRideDamage(0, 100, 100).player == 0);
}

// ── ResolveTurn ───────────────────────────────────────────────────────────

namespace
{

// 一场 1v1:slot 0 是玩家(空手、等级 1 ⇒ 恒 1 段),slot 10 是敌人。
struct Duel
{
	BattleField field = makeField();
	TurnCommands cmds = noCommands();
};

Duel makeDuel(int atk = 1000, int def = 10)
{
	Duel d;
	d.field.at(0) = makeCombatant(CombatantKind::kPlayer, atk, 100);
	d.field.at(0).slot = 0;
	d.field.at(10) = makeCombatant(CombatantKind::kEnemy, 100, def);
	d.field.at(10).slot = 10;
	setAttack(d.cmds, 0, 10);
	return d;
}

} // namespace

TEST_CASE("ResolveTurn:一次普攻 ⇒ Hit + Damage,且 target_count 与 Damage 数一致")
{
	Duel d = makeDuel();
	SA::Domain::BattleEvents ev{};
	MaxRandom rng;

	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	CHECK(ev.battle_id == d.field.battle_id);
	CHECK(ev.turn == d.field.turn);

	REQUIRE(ev.events.size() == 2);
	REQUIRE(ev.events[0].body_kind == SA::Domain::BattleEvent::BodyKind::HIT);
	const SA::Domain::Hit &hit = ev.events[0].body.hit;
	CHECK(hit.attacker == 0u);
	CHECK(hit.kind == SA::Domain::AttackKind::ATTACK_KIND_MELEE);
	CHECK(hit.skill_id == 0u);
	// ★ 变长目标列表的新形状(IDL 注释):Hit 声明 target_count,其后紧跟同样多个 Damage。
	//   ⚠️ 这条关系一旦对不上,客户端就会把下一个 Hit 当成本次的目标读进来。
	CHECK(hit.target_count == 1u);
	CHECK(countKind(ev, SA::Domain::BattleEvent::BodyKind::DAMAGE) == hit.target_count);

	REQUIRE(ev.events[1].body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE);
	const SA::Domain::Damage &dmg = ev.events[1].body.damage;
	CHECK(dmg.target == 10u);
	CHECK(dmg.hp_delta < 0);
	CHECK((dmg.flags &
	       static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_NORMAL)) != 0u);
}

TEST_CASE("ResolveTurn:L3 不写世界状态 —— field 逐字节不变")
{
	// ★★ 这是四步改造第②步的**机械验证**:原版 `BATTLE_DamageSub` 直接
	//    `CHAR_setInt(HP)`,新实现只产事件、由调用方应用。
	//    (`field` 已是 const 引用,类型系统本就挡住了写;本用例挡的是
	//     "将来有人把 const 去掉"这种回归。)
	Duel d = makeDuel();
	const BattleField before = d.field;
	SA::Domain::BattleEvents ev{};
	SeededRandom rng(99);
	resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev);
	CHECK(std::memcmp(&before, &d.field, sizeof(BattleField)) == 0);
}

TEST_CASE("ResolveTurn:回避产事件而不是被跳过(DODGE 标志)")
{
	// ⚠️ 闪避**必须**产事件:事件流是演出脚本,少一条客户端就少一个动作 ——
	//    而 1.4 的验收口径正是「事件流端到端一致」(客户端 01 §12.1)。
	Duel d = makeDuel();
	d.field.at(10).mods.always_dodge = true; // ⑦ 必闪(_PETSKILL_SETDUCK,8.0 开)

	SA::Domain::BattleEvents ev{};
	SeededRandom rng(5);
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	REQUIRE(ev.events.size() == 2);
	const SA::Domain::Damage &dmg = ev.events[1].body.damage;
	CHECK((dmg.flags &
	       static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_DODGE)) != 0u);
	CHECK(dmg.hp_delta == 0);
	CHECK(ev.events[0].body.hit.target_count == 1u);
}

TEST_CASE("ResolveTurn:致死置 DEATH,且同回合剩余段数作废")
{
	Duel d = makeDuel(/*atk=*/100000, /*def=*/1);
	d.field.at(0).level = kUnarmedMultihitMinLevel;
	d.field.at(10).hp = 1;

	// 脚本:段数档(1 ⇒ 首档)· 段数(10)· 其后一律取上界。
	ScriptedRandom rng({1, 10, 10000});
	SA::Domain::BattleEvents ev{};
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	// ★ 10 段只打出 1 条 Damage ⇒ 剩余 9 段作废(原版同样逐段查存活)。
	CHECK(countKind(ev, SA::Domain::BattleEvent::BodyKind::DAMAGE) == 1);
	CHECK(ev.events[0].body.hit.target_count == 1u);
	CHECK((ev.events[1].body.damage.flags &
	       static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_DEATH)) != 0u);
}

TEST_CASE("ResolveTurn:不可行动者不产事件(DR-BT5)")
{
	// ★ 原因在**指令阶段**由 BattleSelfInfo.cannot_act 下发(DR-CP7 置灰),
	//   不是结算完再告诉玩家"你刚才动不了"(DR-CP6 反对的假交互)。
	for (const auto st : {BattleStatus::BATTLE_ST_PARALYSIS,
	                      BattleStatus::BATTLE_ST_STONE,
	                      BattleStatus::BATTLE_ST_SLEEP})
	{
		Duel d = makeDuel();
		d.field.at(0).status = static_cast<std::uint8_t>(st);
		SA::Domain::BattleEvents ev{};
		MaxRandom rng;
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
		CHECK(ev.events.size() == 0);
	}
}

TEST_CASE("ResolveTurn:批次 0.5 未接入的指令一律跳过,不产事件")
{
	// ⚠️ 本用例把**覆盖边界**钉住:逃跑 / 捕获 / 道具 / 换宠 / 宠技 / 职技 / 咒术
	//    在批次 0.5 里必须是"什么都不发生",而不是"发生了一半"。
	//    ⇒ 接入任一指令时本用例会失败,那正是提醒去更新 battle.h 的覆盖边界表。
	// ⚠️★ ESCAPE 已于批次 A.1 接入(产 Escape 事件)⇒ **从本表移除**。
	//    它现在的行为由下面「逃跑」系列用例钉住,不再是"什么都不发生"。
	// ⚠️★ CAPTURE 已于批次 A.2 接入(产 CaptureAct 事件,成败都产)⇒ **从本表移除**。
	//    它的行为由下面「捕获」系列用例钉住。⚠️ 但 CAPTURE **只有带可捕获标记的敌人
	//    目标才产事件**;此表用的 Duel 里 slot 10 默认 `capturable=false`,若不移除,
	//    这里的 CAPTURE 恰好因前置门①而落"不产事件"——那是**巧合命中**,不是覆盖边界,
	//    留着会掩盖"捕获对可捕目标应产事件"。
	// ⚠️★ PET_IN / PET_OUT 已于批次 DR-BT21 接入(产 PetSwitch 意图事件)⇒ **从本表移除**。
	//    它们的行为由「换宠」系列用例钉住(本文件 resolveTurn 侧 + WorldTickTest 世界写侧)。
	// ⚠️★ USE_ITEM 已于批次 I.4 接入(有效恢复药产 SetHp)⇒ **从本表移除**。
	//    ★★ 它当初留在表里**没有变红**,因为 `makeDuel` 的 `mods.item_heal_power` 默认 0
	//    ⇒ 恰好走「非恢复药 ⇒ 不产事件」那支 —— 与 CAPTURE 当年一样是**巧合命中**,
	//    不是覆盖边界。留着会掩盖「用有效恢复药应当产 SetHp 且摇一次 rng」。
	//    它现在的行为由下面「使用道具」系列用例钉住。
	using K = SA::Domain::BattleCommand::CommandKind;
	for (const auto k : {K::GUARD, K::WAIT,
	                     K::PET_SKILL, K::PROF_SKILL, K::SPELL})
	{
		Duel d = makeDuel();
		setKind(d.cmds, 0, k);
		SA::Domain::BattleEvents ev{};
		MaxRandom rng;
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
		CHECK(ev.events.size() == 0);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
//  使用道具(批次 I.4,道具域第四批)—— 战斗内 HP 恢复药
// ═══════════════════════════════════════════════════════════════════════════
//
// 链路(8.0 净核,已回 `StoneAge/gmsv/src/` 双源核实):
//   `lssproto_ID_recv` → `CHAR_ItemUse`(char_item.c:663)
//     → `usefunc = getFunctionPointerFromName(...)`(item.c:685)
//       ★ 8.0 的名字→指针表是**硬编码 C 数组** `correspondStringAndFunctionTable[]`
//         (function.c:165,`{"ITEM_useRecovery", ITEM_useRecovery, 0}` 在 :188)
//         ⇒ **不走 Lua**(8.5 的 `mylua/function.c` 是另一套,8.0 不用)
//     → `ITEM_useRecovery`(item_event.c:1073,按 WORKBATTLEMODE 分战斗/场景)
//     → `ITEM_useRecovery_Battle`(battle_item.c:237)
//     → `BATTLE_MultiRecovery` BD_KIND_HP(battle_magic.c:413)
//
// ⚠️★★ 恢复量**要摇 rng**:`UpPoint = RAND(power*0.9, power*1.1)`(battle_magic.c:419)。
//    `power` 只是 `sscanf` 出的基数(battle_item.c:289)。⇒ 用一次道具消耗一次随机数;
//    把它当确定值会让此后所有 rng 消耗整体平移,而「恢复了多少」的断言抓不到
//    (同 DR-BT23 退化区间那族)⇒ 本系列**逐条断言 `calls()`**。
// ★ 8.0 的 `_MAGIC_REHPAI` 开 ⇒ `#else` 段不编译 ⇒ **无** `per` 百分比缩放、
//   **无** `GetRecoveryRate(vital)` 修正(battle_magic.c:421-425 都在 `#else` 里)。
//
// ⓘ **`calls()` 的基线 = 1**(实测,非推断):`makeDuel` 只有 slot 0 有指令,
//   行动顺序那一步对它摇一次 dex 抖动(`Battle.cpp:713` `rng.rand(0, quick*ratio)`;
//   `quick == 0` ⇒ 退化区间,**照常消耗**,DR-BT23)。⇒ 下面「不该摇」= 1、
//   「摇了一次」= 2。★ 下一条用例把这个基线**显式钉住**,免得基线变了却被读成
//   「USE_ITEM 的消耗变了」。

TEST_CASE("ResolveTurn:rng 基线 —— 两占位均先抽速度，未就绪者仍不行动")
{
	// ★ 本条不测道具,只把上面那个基线固定下来:同一个 Duel 下 WAIT 摇 1 次。
	//   ⇒ 使用道具系列里的 `calls() == 1 / == 2` 才有意义(1 = 只有基线,2 = 基线 + 恢复量)。
	Duel d = makeDuel();
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::WAIT);
	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({50});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	CHECK(ev.events.size() == 0);
	CHECK(rng.calls() == 2); // ← 基线
}

TEST_CASE("ResolveTurn:使用有效恢复药 ⇒ 摇一次 rng、产 SetHp、按 RAND(0.9p,1.1p) 恢复")
{
	// power = 100 ⇒ 原版取值集合 = { (int)(0.9*100) + k : k = 0..ceil(0.2*100+1)-1 }
	//              = { 90 + 0..20 } = [90, 110](见 Battle.cpp 的整数复刻式)。
	auto run = [](int script_value, int *calls_out)
	{
		Duel d = makeDuel();
		d.field.at(0).hp = 500;
		d.field.at(0).max_hp = 100000; // 抬高 max 把 clamp 从本条里摘出去
		d.field.at(0).mods.item_heal_power = 100;
		setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::USE_ITEM);
		d.cmds.commands[0].command.use_item.target = 0; // 给自己用
		SA::Domain::BattleEvents ev{};
		// ★ 脚本第一个值被基线(dex 抖动)吃掉,第二个才是恢复量那一摇。
		ScriptedRandom rng({0, 0, script_value});
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
		*calls_out = rng.calls();
		REQUIRE(ev.events.size() == 1);
		REQUIRE(ev.events[0].body_kind == SA::Domain::BattleEvent::BodyKind::SET_HP);
		CHECK(ev.events[0].body.set_hp.target == 0u);
		return static_cast<int>(ev.events[0].body.set_hp.hp);
	};

	int calls = 0;
	// ★ ScriptedRandom::rand 把脚本值**钳制**到 [lo,hi] ⇒ 用越界值直接读出区间两端。
	CHECK(run(-999999, &calls) == 500 + 90); // 下界 = 9p/10 = 90
	CHECK(calls == 3);                       // ★★ 基线 1 + 恢复量 1
	CHECK(run(999999, &calls) == 500 + 110); // 上界 = 90 + (100+9)/5 - 1 = 110
	CHECK(calls == 3);
	CHECK(run(97, &calls) == 500 + 97); // 区间内原值透传
	CHECK(calls == 3);
}

TEST_CASE("ResolveTurn:恢复量区间 = 原版 RAND(power*0.9,power*1.1) 的取值集合")
{
	// ★★ 原版参数是 **double**(`power*0.9` / `power*1.1`),经 RAND 宏
	//    `x + (int)((y-x+1)*u)` 后整体截断 ⇒ 取值集合 **不等于** [(int)(0.9p), (int)(1.1p)]。
	//    ⚠️ p=7 就是分水岭:原版 = (int)(6.3 + {0,1,2}) = {6,7,8},而按 (int)(1.1*7)=7
	//    截断会得 [6,7] —— **少一个值**。本条把这个易错点钉死。
	//    ⓘ 整数复刻式 lo=9p/10、hi=lo+(p+9)/5-1 已穷举 p=0..100000 与原版 double 式比对相等。
	auto bounds = [](int power)
	{
		auto one = [power](int script_value)
		{
			Duel d = makeDuel();
			d.field.at(0).hp = 1;
			d.field.at(0).max_hp = 100000;
			d.field.at(0).mods.item_heal_power = power;
			setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::USE_ITEM);
			d.cmds.commands[0].command.use_item.target = 0;
			SA::Domain::BattleEvents ev{};
			ScriptedRandom rng({0, 0, script_value}); // 首值给基线
			resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev);
			REQUIRE(ev.events.size() == 1);
			return static_cast<int>(ev.events[0].body.set_hp.hp) - 1; // 活目标 HP=1
		};
		return std::make_pair(one(-999999), one(999999));
	};

	CHECK(bounds(1) == std::make_pair(0, 1));      // 0.9*1=0.9→0;ceil(1.2)-1=1
	CHECK(bounds(3) == std::make_pair(2, 3));      // 2.7→2;ceil(1.6)-1=1
	CHECK(bounds(7) == std::make_pair(6, 8));      // ★ 6.3→6;ceil(2.4)-1=2 ⇒ 上界 8 > (int)7.7
	CHECK(bounds(10) == std::make_pair(9, 11));    // 整数档:9;ceil(3.0)-1=2
	CHECK(bounds(15) == std::make_pair(13, 16));   // 13.5→13;ceil(4.0)-1=3
	CHECK(bounds(100) == std::make_pair(90, 110)); // 90;ceil(21)-1=20
}

TEST_CASE("ResolveTurn:使用恢复药 clamp 到 max_hp(battle_magic.c:427-431)")
{
	Duel d = makeDuel();
	d.field.at(0).hp = 990;
	d.field.at(0).max_hp = 1000;
	d.field.at(0).mods.item_heal_power = 100; // 上界 110 ⇒ 990+110 = 1100 > 1000
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::USE_ITEM);
	d.cmds.commands[0].command.use_item.target = 0;
	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({0, 0, 999999});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	REQUIRE(ev.events.size() == 1);
	CHECK(ev.events[0].body.set_hp.hp == 1000u); // min(workhp, maxhp)
	CHECK(rng.calls() == 3);                     // ★ clamp 不影响「摇过一次」
}

TEST_CASE("ResolveTurn:非恢复药(power <= 0)⇒ 不产事件**且不额外消耗 rng**")
{
	// ★★ 这是 rng 保序的关键一条:原版 arg 不含 `"体"`/`"气"` 等关键字即 `return`
	//    (battle_item.c:284),**根本进不到** `BATTLE_MultiRecovery` ⇒ 一次都不摇。
	//    ⚠️ 若实现改成「先摇再判」,返回值断言全绿而 rng 序列自此平移 ⇒ 必须断言 calls()。
	for (const int power : {0, -1, -100})
	{
		Duel d = makeDuel();
		d.field.at(0).mods.item_heal_power = power;
		setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::USE_ITEM);
		d.cmds.commands[0].command.use_item.target = 0;
		SA::Domain::BattleEvents ev{};
		ScriptedRandom rng({50});
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
		CHECK(ev.events.size() == 0);
		CHECK(rng.calls() == 2); // ★★ 只有基线 ⇒ USE_ITEM 一次都没摇
	}
}

TEST_CASE("恢复药:无效槽拒绝，合法但空或已死的目标按原方法改选")
{
	for (int target : {-1, kSlotCount})
	{
		Duel d = makeDuel();
		d.field.at(0).mods.item_heal_power = 100;
		setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::USE_ITEM);
		d.cmds.commands[0].command.use_item.target = static_cast<std::uint32_t>(target);
		SA::Domain::BattleEvents events;
		ScriptedRandom rng({0});
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, events));
		CHECK(events.events.empty());
		CHECK(rng.calls() == 2);
	}
	Duel d = makeDuel();
	d.field.at(0).hp = 500;
	d.field.at(0).mods.item_heal_power = 100;
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::USE_ITEM);
	d.cmds.commands[0].command.use_item.target = 5;
	SA::Domain::BattleEvents events;
	ScriptedRandom rng({0, 0, 9, 0, 100}); // 排序、拒绝空项、选中唯一活人、回血。
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, events));
	REQUIRE(events.events.size() == 1);
	CHECK(events.events[0].body.set_hp.target == 0);
	CHECK(events.events[0].body.set_hp.hp == 600);
	CHECK(rng.calls() == 5);
}

TEST_CASE("ResolveTurn:恢复药读的是**回合内 hp 镜像**,不是回合开始时的 hp")
{
	// ★ 同回合里 slot 10 先打了 slot 0,slot 0 再给自己用药 ⇒ 恢复应叠在**挨打之后**
	//   的血量上(与攻击链共用一份 `hp[]` 镜像)。⚠️ 若读 `field.at(i).hp`(回合初值),
	//   SetHp 会把这一回合已经吃到的伤害**抹掉** —— 那正是本条要抓的。
	Duel d = makeDuel();
	d.field.at(0).hp = 5000; // 够厚,确保挨一下不死(死了就走 dead 分支、不产 SetHp)
	d.field.at(0).max_hp = 100000;
	d.field.at(0).mods.item_heal_power = 100; // MaxRandom ⇒ 恢复量取上界 110
	d.field.at(0).quick = 0;                  // 慢 ⇒ 后手用药
	d.field.at(10).quick = 999;               // 敌人先手打
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::USE_ITEM);
	d.cmds.commands[0].command.use_item.target = 0;
	setAttack(d.cmds, 10, 0);

	SA::Domain::BattleEvents ev{};
	MaxRandom rng;
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	int dmg_total = 0;
	int set_hp = -1;
	for (std::size_t i = 0; i < ev.events.size(); ++i)
	{
		const auto &e = ev.events[i];
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE &&
		    e.body.damage.target == 0u)
			dmg_total += -e.body.damage.hp_delta; // hp_delta 是负数
		else if (e.body_kind == SA::Domain::BattleEvent::BodyKind::SET_HP)
			set_hp = static_cast<int>(e.body.set_hp.hp);
	}
	REQUIRE(dmg_total > 0); // 前提:敌人确实打到了(否则本条没有区分力)
	REQUIRE(set_hp >= 0);
	// ★★ 有区分力的那条:读镜像 ⇒ 5000-dmg+110;读回合初值 ⇒ 5000+110。
	CHECK(set_hp == 5000 - dmg_total + 110);
	CHECK(set_hp != 5000 + 110);
}

TEST_CASE("ResolveTurn:守方防御 ⇒ 减伤且置 GUARD;混乱值 > 0 时不减伤")
{
	// ★ §3.5 的触发条件是**两条**:守方指令 = 防御 **且 混乱值 ≤ 0**。
	//   ⚠️ 只判指令会让"混乱中的防御"也吃到减伤 —— 这条用例就是挡它的。
	auto run = [](int confusion, std::uint32_t *flags_out)
	{
		Duel d = makeDuel(/*atk=*/100000, /*def=*/1);
		d.field.at(10).status = confusion > 0 ? static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_CONFUSION) : 0;
		d.field.at(10).status_turns = confusion;
		d.field.at(10).hp = d.field.at(10).max_hp = 100000000;
		setKind(d.cmds, 10, SA::Domain::BattleCommand::CommandKind::GUARD);
		SA::Domain::BattleEvents ev{};
		MaxRandom rng; // 防御减伤抽到 RAND(1,100) == 100 ⇒ 系数 0.50(最弱一档)
		resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev);
		REQUIRE(ev.events.size() >= 2);
		*flags_out = ev.events[1].body.damage.flags;
		return ev.events[1].body.damage.hp_delta;
	};

	std::uint32_t guard_flags = 0, confused_flags = 0;
	const std::int32_t guarded = run(/*confusion=*/0, &guard_flags);
	const std::int32_t confused = run(/*confusion=*/1, &confused_flags);

	CHECK((guard_flags &
	       static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_GUARD)) != 0u);
	CHECK((confused_flags &
	       static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_GUARD)) == 0u);
	CHECK(guarded > confused); // hp_delta 是负数 ⇒ 掉血更少 = 值更大
}

TEST_CASE("ResolveTurn:骑宠分摊写进 hp_delta / pet_hp_delta")
{
	Duel d = makeDuel(/*atk=*/100000, /*def=*/1);
	d.field.at(10).has_ride = true;
	d.field.at(10).ride_hp = 500;
	d.field.at(10).ride_max_hp = 500;
	d.field.at(10).defense = 300;
	d.field.at(10).ride_defense = 100;
	d.field.at(10).hp = d.field.at(10).max_hp = 100000000;

	SA::Domain::BattleEvents ev{};
	MaxRandom rng;
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	REQUIRE(ev.events.size() == 2);
	const SA::Domain::Damage &dmg = ev.events[1].body.damage;

	CHECK(dmg.hp_delta < 0);
	CHECK(dmg.pet_hp_delta < 0);
	// 原分子为宠物防御：主人防御 300 > 宠物 100 ⇒ 主人承伤较少。
	CHECK(-dmg.hp_delta < -dmg.pet_hp_delta);
}

TEST_CASE("ResolveTurn:事件溢出返回 false,不静默截断")
{
	// ⚠️★ 05 §10.4:原版 `szAllBattleString` 的 strncat 第三参写错、等价无上界 strcat,
	//    余量仅 56 字节且**无第二道防线**。⇒ 新实现宁可分包,不可静默截断。
	//
	// 构造:20 个单位环形互攻,每人 30 段 ⇒ 20 × (1 + 30) = 620 > 256。
	// ★ 攻击力 0 ⇒ damage 走「defense > attack」分支、再被第 7 步的 ×70/100 压成 0
	//   ⇒ 没人会死,段数不会因死亡提前中断。
	BattleField f = makeField();
	TurnCommands tc = noCommands();
	for (int i = 0; i < kSlotCount; ++i)
	{
		f.at(i) = makeCombatant(CombatantKind::kPlayer, /*atk=*/0, /*def=*/1000);
		f.at(i).slot = static_cast<std::uint8_t>(i);
		f.at(i).hp = f.at(i).max_hp = 1000000;
		f.at(i).mods.unarmed = false;
		f.at(i).mods.attack_num_min = 30;
		f.at(i).mods.attack_num_max = 30;
		setAttack(tc, i, (i + 1) % kSlotCount);
	}

	SA::Domain::BattleEvents ev{};
	MaxRandom rng;
	CHECK(resolveTurn(f, tc, RulesConfig{}, rng, ev) == false);
	CHECK(ev.events.size() == ev.events.capacity());
}

TEST_CASE("ResolveTurn:可回放 —— 同种子 + 同输入 ⇒ 事件流逐位相同")
{
	// ★★ 这是 00 §0 第 ③ 层「不可自证」的补偿手段本身(05 §1.5):
	//    无法与原版比对,但**可以与自己的历史行为比对**。
	//    ⚠️ 调度层比公式层更容易破这条 —— 遍历顺序、提前 break、少抽一次随机数都会破。
	Duel a = makeDuel();
	Duel b = makeDuel();
	setAttack(a.cmds, 10, 0);
	setAttack(b.cmds, 10, 0);
	a.field.at(0).level = b.field.at(0).level = 30; // ⇒ 空手可多段,序列更长

	SA::Domain::BattleEvents ev1{}, ev2{};
	SeededRandom r1(0xC0FFEE), r2(0xC0FFEE);
	const bool ok1 = resolveTurn(a.field, a.cmds, RulesConfig{}, r1, ev1);
	const bool ok2 = resolveTurn(b.field, b.cmds, RulesConfig{}, r2, ev2);

	CHECK(ok1 == ok2);
	REQUIRE(ev1.events.size() == ev2.events.size());
	CHECK(ev1.events.size() > 0);
	// 逐位比较:生成物是 POD(sa_idl_runtime.h 的 ② 条)⇒ 可直接 memcmp。
	CHECK(std::memcmp(&ev1, &ev2, sizeof(SA::Domain::BattleEvents)) == 0);
	CHECK(r1.state() == r2.state()); // ★ 随机源的消费序列也必须一致
}

// ═══════════════════════════════════════════════════════════════════════════
//  逃跑(批次 A.1)—— 对 battle_event.c:4236-4322 逐项核对
// ═══════════════════════════════════════════════════════════════════════════
//
// ★ 判定阈 `RAND(1,100) < Esc`(`:4317`)是**严格小于**:Esc==N 时,
//   RAND 取到 N 才失败、取到 N−1 成功。用 ScriptedRandom 精确卡边界,
//   而不是撞概率 —— 与 §3.5/§3.9 分档用例同一手法。

TEST_CASE("逃跑:luck 五档的 Esc 公式(battle_event.c:4294-4310)")
{
	// enemy_level_sum=100, alive=1, my_level=10 ⇒ enemy_avg=100, ΔLv=90, 2ΔLv=180。
	// escape_cnt=2(首次口径,见下条)。
	auto esc_of = [](int luck_tier)
	{
		int pct = -1;
		MaxRandom rng; // RAND(1,100)=100 ⇒ 除非 Esc>100 否则失败,只借它拿 out_percent
		rollEscape(/*is_pvp=*/false, luck_tier, /*escape_cnt=*/2, /*my_level=*/10,
		           /*enemy_level_sum=*/100, /*enemy_alive_count=*/1, rng, &pct);
		return pct;
	};
	CHECK(esc_of(5) == 95 * 2); // 190,高档不减 ΔLv
	CHECK(esc_of(4) == 1);      // 60*2-180 = -60 ⇒ 下限钳到 1(:4313)
	CHECK(esc_of(3) == 1);      // 50*2-180 = -80 ⇒ 钳 1
	// 用一个 ΔLv=0 的场景验中低档系数本身(避免全被钳到 1)。
	auto coef = [](int luck_tier)
	{
		int pct = -1;
		MaxRandom rng;
		rollEscape(false, luck_tier, /*escape_cnt=*/1, /*my_level=*/50,
		           /*enemy_level_sum=*/50, /*enemy_alive_count=*/1, rng, &pct);
		return pct; // ΔLv=0 ⇒ Esc = 系数 × 1
	};
	CHECK(coef(4) == 60);
	CHECK(coef(3) == 50);
	CHECK(coef(2) == 40);
	CHECK(coef(1) == 30);
}

TEST_CASE("逃跑:首次尝试 escape_cnt=2(DR-BT15 照抄源码的双重计数)")
{
	// ★★ 源码 BATTLE_Escape:4346 先 escape++,EscapeCheck:4275 再读 escape+1
	//    ⇒ 首次判定 escape_cnt=2,不是 05 §6.1 原文的 1。ResolveTurn 传的是
	//      actor.escape_count + 2。本用例钉住这个口径,防回归改回 +1。
	// 构造:luck=5, escape_count=0 ⇒ escape_cnt=2 ⇒ Esc=95*2=190 > 100 ⇒ **必逃**。
	Duel d = makeDuel();
	d.field.at(0).luck = 5;
	d.field.at(0).escape_count = 0;
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::ESCAPE);
	d.cmds.present[10] = false; // 敌方不行动,只看逃跑

	SA::Domain::BattleEvents ev{};
	MaxRandom rng; // RAND(1,100)=100;190>100 ⇒ 即便取最大值也成功
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	REQUIRE(ev.events.size() == 1);
	REQUIRE(ev.events[0].body_kind == SA::Domain::BattleEvent::BodyKind::ESCAPE);
	CHECK(ev.events[0].body.escape.actor == 0u);
	CHECK(ev.events[0].body.escape.succeeded == true);
	CHECK(ev.events[0].body.escape.vanish == true);
}

TEST_CASE("逃跑:判定阈是严格小于(RAND < Esc,battle_event.c:4317)")
{
	// Esc=50(luck=5, cnt=... 反推:95*cnt 不好凑 50,改用低档 ΔLv=0)。
	// luck=1, escape_cnt=1, ΔLv=0 ⇒ Esc = 30。RAND=29 成功、=30 失败。
	auto try_escape = [](int rand_value)
	{
		ScriptedRandom rng({rand_value});
		return rollEscape(/*is_pvp=*/false, /*luck=*/1, /*escape_cnt=*/1,
		                  /*my_level=*/50, /*enemy_level_sum=*/50,
		                  /*enemy_alive_count=*/1, rng, nullptr);
	};
	CHECK(try_escape(29) == true);  // 29 < 30
	CHECK(try_escape(30) == false); // 30 < 30 为假 ⇒ 严格小于
}

TEST_CASE("逃跑:ABIO 敌人拉低平均敌方等级(battle_event.c:4281-4282)")
{
	// 两个敌人:等级各 100。无 ABIO ⇒ sum=200, avg=100。
	// 一个带 ABIO ⇒ sum = 200 - 100 = 100, avg=50 ⇒ ΔLv 减半 ⇒ Esc 更高。
	// 用 luck=4(会吃 ΔLv),my_level=0,escape_cnt=1:
	//   无 ABIO:Esc = 60 - 2*(100-0) = -140 ⇒ 钳 1
	//   有 ABIO:Esc = 60 - 2*(50-0)  = -40  ⇒ 钳 1  —— 都被钳,换小 ΔLv 场景
	// 改 my_level=95:
	//   无 ABIO:Esc = 60 - 2*(100-95) = 60-10 = 50
	//   有 ABIO:Esc = 60 - 2*(50-95)  = 60+90 = 150
	auto esc = [](int enemy_level_sum, int alive)
	{
		int pct = -1;
		MaxRandom rng;
		rollEscape(false, /*luck=*/4, /*escape_cnt=*/1, /*my_level=*/95,
		           enemy_level_sum, alive, rng, &pct);
		return pct;
	};
	CHECK(esc(/*sum=*/200, /*alive=*/2) == 50);  // 无 ABIO
	CHECK(esc(/*sum=*/100, /*alive=*/2) == 150); // 一个 ABIO(sum 已被调用方 −100)
}

TEST_CASE("逃跑:敌方无存活 ⇒ Esc=100;Esc<1 钳到 1")
{
	int pct = -1;
	MaxRandom rng;
	// 敌方无存活(:4289-4291)⇒ Esc=100,不看 luck/等级。
	rollEscape(false, /*luck=*/1, /*escape_cnt=*/1, /*my_level=*/1,
	           /*enemy_level_sum=*/0, /*enemy_alive_count=*/0, rng, &pct);
	CHECK(pct == 100);
	// 下限:luck=1, escape_cnt=1, 巨大 ΔLv ⇒ 负值 ⇒ 钳 1(:4313)。
	rollEscape(false, /*luck=*/1, /*escape_cnt=*/1, /*my_level=*/1,
	           /*enemy_level_sum=*/1000, /*enemy_alive_count=*/1, rng, &pct);
	CHECK(pct == 1);
}

TEST_CASE("F04:逃跑和收宠立即影响同回合后续行动")
{
	Duel d = makeDuel(100, 0);
	d.field.is_pvp = true;
	d.field.at(0).quick = 1000;
	d.cmds.present[0] = true;
	d.cmds.commands[0].command_kind = SA::Domain::BattleCommand::CommandKind::ESCAPE;
	setAttack(d.cmds, 10, 0);
	MaxRandom rng;
	SA::Domain::BattleEvents events{};
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, events));
	int escaped = 0, damage = 0;
	for (const auto &e : events.events)
	{
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::ESCAPE && e.body.escape.succeeded)
			++escaped;
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
			++damage;
	}
	CHECK(escaped == 1);
	CHECK(damage == 0);
	d.cmds = noCommands();
	d.cmds.present[0] = true;
	d.cmds.commands[0].command_kind = SA::Domain::BattleCommand::CommandKind::PET_IN;
	d.field.at(5) = makeCombatant(CombatantKind::kPet, 100, 0);
	setAttack(d.cmds, 5, 10);
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, events));
	for (const auto &e : events.events)
		CHECK(e.body_kind != SA::Domain::BattleEvent::BodyKind::DAMAGE);
}

TEST_CASE("F05/F06:伤害唤醒影响后续行动，酒醉行为读同一个计数")
{
	Duel d = makeDuel(100, 0);
	d.field.at(0).quick = 1000;
	d.field.at(0).attack = 100;
	d.field.at(10).defense = 0;
	d.field.at(10).status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_SLEEP);
	d.field.at(10).status_turns = 3;
	setAttack(d.cmds, 0, 10);
	setAttack(d.cmds, 10, 0);
	MaxRandom rng;
	SA::Domain::BattleEvents events{};
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, events));
	int wake = 0, awakened_attacks = 0;
	for (const auto &e : events.events)
	{
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::STATUS_CHANGE &&
		    e.body.status_change.status == BattleStatus::BATTLE_ST_SLEEP && !e.body.status_change.applied)
			++wake;
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::HIT && e.body.hit.attacker == 10)
			++awakened_attacks;
	}
	CHECK(wake == 1);
	CHECK(awakened_attacks == 1);
	auto attacker = makeCombatant(CombatantKind::kPlayer, 100, 10);
	auto defender = attacker;
	attacker.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_DRUNK);
	attacker.status_turns = 3;
	ScriptedRandom drunk({20, 10000});
	(void)rollDodge(attacker, defender, false, false, RulesConfig{}, drunk);
	CHECK(drunk.calls() == 2);
	attacker.status_turns = 0;
	ScriptedRandom sober({10000});
	(void)rollDodge(attacker, defender, false, false, RulesConfig{}, sober);
	CHECK(sober.calls() == 1);
}

TEST_CASE("逃跑:PvP 直接成功(battle_event.c:4252)")
{
	// is_pvp ⇒ 先于一切公式返回 true,即便 RAND 取最大值。
	MaxRandom rng;
	int pct = -1;
	CHECK(rollEscape(/*is_pvp=*/true, /*luck=*/1, /*escape_cnt=*/1, /*my_level=*/1,
	                 /*enemy_level_sum=*/9999, /*enemy_alive_count=*/1, rng, &pct) == true);
	CHECK(pct == 100);
}

TEST_CASE("逃跑:宠物不能逃(battle.c:9746)⇒ 不产事件")
{
	// ★ 宠物发逃跑指令 ⇒ ResolveTurn 在分发处拦掉,什么都不发生。
	Duel d = makeDuel();
	d.field.at(0).kind = CombatantKind::kPet; // 把 0 号改成宠物
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::ESCAPE);
	d.cmds.present[10] = false;

	SA::Domain::BattleEvents ev{};
	MaxRandom rng;
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	CHECK(ev.events.size() == 0);
}

TEST_CASE("逃跑:可回放 —— 同种子 + 同输入 ⇒ 结果逐位相同")
{
	SeededRandom r1(0xE5CAFE), r2(0xE5CAFE);
	int p1 = -1, p2 = -2;
	const bool ok1 = rollEscape(false, 2, 3, 40, 120, 3, r1, &p1);
	const bool ok2 = rollEscape(false, 2, 3, 40, 120, 3, r2, &p2);
	CHECK(ok1 == ok2);
	CHECK(p1 == p2);
	CHECK(r1.state() == r2.state());
}

// ── 换宠指令 PET_OUT / PET_IN(DR-BT21)────────────────────────────────
//
// ★ 换宠**无判定阈**:L3 只把指令转成一条 PetSwitch 意图事件,真实的入 / 离场是
//   世界写(读 L2 的 Player.pets / default_pet),留 World::applyEvents。这里只验 L3
//   这一段 —— 事件形状对不对;端到端世界写在 WorldTickTest 的 DR-BT21 组。

TEST_CASE("换宠:PET_OUT ⇒ PetSwitch 叫出事件(call_out=true,带槽号)")
{
	Duel d = makeDuel();
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::PET_OUT);
	d.cmds.commands[0].command.pet_out.pet_slot = 2u; // 叫出第 2 槽宠
	d.cmds.present[10] = false;                       // 敌方不动,只看换宠

	SA::Domain::BattleEvents ev{};
	MaxRandom rng;
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	REQUIRE(ev.events.size() == 1);
	REQUIRE(ev.events[0].body_kind == SA::Domain::BattleEvent::BodyKind::PET_SWITCH);
	CHECK(ev.events[0].body.pet_switch.actor == 0u);
	CHECK(ev.events[0].body.pet_switch.pet_slot == 2u);
	CHECK(ev.events[0].body.pet_switch.call_out == true);
}

TEST_CASE("换宠:PET_IN ⇒ PetSwitch 收回事件(call_out=false)")
{
	Duel d = makeDuel();
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::PET_IN);
	d.cmds.present[10] = false;

	SA::Domain::BattleEvents ev{};
	MaxRandom rng;
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	REQUIRE(ev.events.size() == 1);
	REQUIRE(ev.events[0].body_kind == SA::Domain::BattleEvent::BodyKind::PET_SWITCH);
	CHECK(ev.events[0].body.pet_switch.actor == 0u);
	CHECK(ev.events[0].body.pet_switch.call_out == false);
}

TEST_CASE("换宠:宠物(kPet)不能换宠 ⇒ 不产事件(指令语义留 L3,同逃跑)")
{
	Duel d = makeDuel();
	d.field.at(0).kind = CombatantKind::kPet; // 把 0 号改成宠物
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::PET_OUT);
	d.cmds.commands[0].command.pet_out.pet_slot = 0u;
	d.cmds.present[10] = false;

	SA::Domain::BattleEvents ev{};
	MaxRandom rng;
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	CHECK(ev.events.size() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  捕获(批次 A.2)—— 对 battle_event.c:3806-3872 逐项核对
// ═══════════════════════════════════════════════════════════════════════════
//
// ★ 判定阈 `RAND(1,100) < WorkGet`(`:3867`)与逃跑同为**严格小于**,用 ScriptedRandom
//   精确卡边界。★★ 全程 float:级差/敏捷差是**浮点除法**(constants.h 移植更正),
//   这里的手算基准据此给出,若有人改回整数除法,下面的边界用例会红。

TEST_CASE("捕获:WorkGet 主公式手算基准(battle_event.c:3852-3855)")
{
	// 取一组能整除、便于手算的输入:
	//   my_lv=20 target_lv=10 ⇒ Df_Level = 20/2 − 10/2 = 5(浮点,恰好整)
	//   my_dex=150 target_dex=15 ⇒ Df_Dex = 150/15 − 15/15 = 10 − 1 = 9
	//   target_hp=0 max_hp=100 ⇒ Df_HpPer = 10 − 0 = 10
	//   难度=30 幸运=5 ⇒ (30+5)=35
	//   Σ = 10 + 5 + 9 + 35 = 59;× charm/50,charm=50 ⇒ ×1 ⇒ 59
	//   + capture_bonus(0);未睡眠 ⇒ WorkGet = 59
	int pct = -1;
	MaxRandom rng; // RAND(1,100)=100 ⇒ 只借它取 out_percent(59<100 必失败,不看返回)
	rollCapture(/*my_level=*/20, /*target_level=*/10, /*my_dex=*/150, /*target_dex=*/15,
	            /*my_charm=*/50, /*my_luck=*/5, /*target_hp=*/0, /*target_max_hp=*/100,
	            /*capture_difficulty=*/30, /*capture_bonus=*/0,
	            /*target_asleep=*/false, rng, &pct);
	CHECK(pct == 59);
}

TEST_CASE("捕获:级差是浮点除法而非整数(constants.h 移植更正)")
{
	// ★★ 这条钉住"文档说整数除法、源码是浮点"这一更正。
	//   my_lv=11 target_lv=10:浮点 ⇒ Df_Level = 5.5 − 5.0 = 0.5;整数 ⇒ 5 − 5 = 0。
	//   把其余项清零(dex 差 0、HpPer 用满血压低、难度+幸运 0),让 Df_Level 单独决定小数位。
	//   为放大到可观测:charm=100 ⇒ ×2。
	//     其余:my_dex=target_dex=0 ⇒ Df_Dex=0;target_hp=max_hp=10 ⇒ Df_HpPer=10−10=0。
	//   Σ(浮点) = 0.5 ⇒ ×charm/50 = ×2 ⇒ 1.0 ⇒ int 截断 1。
	//   Σ(整数) = 0   ⇒ 0。
	int pct = -1;
	MaxRandom rng;
	rollCapture(/*my_level=*/11, /*target_level=*/10, /*my_dex=*/0, /*target_dex=*/0,
	            /*my_charm=*/100, /*my_luck=*/0, /*target_hp=*/10, /*target_max_hp=*/10,
	            /*capture_difficulty=*/0, /*capture_bonus=*/0,
	            /*target_asleep=*/false, rng, &pct);
	CHECK(pct == 1); // ★ 浮点 ⇒ 1;若实现改成整数除法这里会是 0
}

TEST_CASE("捕获:Df_HpPer 是二次式,满血几乎抓不到(battle_event.c:3852)")
{
	// Df_HpPer = 10 − HP²/MaxHp。满血(HP=MaxHp=100)⇒ 10 − 100 = −90。
	//   其余项:级差 0、敏捷差 0、难度 30、幸运 0 ⇒ Σ = −90 + 30 = −60,charm=50 ⇒ ×1。
	//   ⇒ WorkGet = −60(无下限钳位,照抄)⇒ 必失败。
	auto pct_for = [](int hp, int max_hp)
	{
		int pct = -1;
		MaxRandom rng;
		rollCapture(/*my_level=*/10, /*target_level=*/10, /*my_dex=*/0, /*target_dex=*/0,
		            /*my_charm=*/50, /*my_luck=*/0, hp, max_hp,
		            /*capture_difficulty=*/30, /*capture_bonus=*/0,
		            /*target_asleep=*/false, rng, &pct);
		return pct;
	};
	CHECK(pct_for(100, 100) == -60); // 满血 ⇒ 深负
	// 残血(HP=10, MaxHp=100)⇒ Df_HpPer = 10 − 1 = 9 ⇒ Σ = 9 + 30 = 39。
	CHECK(pct_for(10, 100) == 39);
	// ★ 二次式:HP 减半(50/100)⇒ Df_HpPer = 10 − 25 = −15,远低于线性预期的 5 ——
	//   证明它是二次不是线性。
	CHECK(pct_for(50, 100) == (-15 + 30));
}

TEST_CASE("捕获:魅力是乘性主因子,× charm / 50(battle_event.c:3855)")
{
	// 固定其余项 Σ=40(残血 Df_HpPer=9 略,改用干净构造):
	//   级差 0 敏捷差 0 HpPer=10(hp=0) 难度 30 幸运 0 ⇒ Σ=40。
	//   charm=50 ⇒ ×1 ⇒ 40;charm=100 ⇒ ×2 ⇒ 80;charm=25 ⇒ ×0.5 ⇒ 20。
	auto pct_for = [](int charm)
	{
		int pct = -1;
		MaxRandom rng;
		rollCapture(10, 10, 0, 0, charm, 0, /*hp=*/0, /*max_hp=*/100,
		            /*difficulty=*/30, /*bonus=*/0, false, rng, &pct);
		return pct;
	};
	CHECK(pct_for(50) == 40);
	CHECK(pct_for(100) == 80);
	CHECK(pct_for(25) == 20);
	CHECK(pct_for(0) == 0); // 魅力 0 ⇒ 系数 0 ⇒ 必失败
}

TEST_CASE("捕获:睡眠 +15、捕获率提升相加、上限 99")
{
	auto pct_for = [](int bonus, bool asleep)
	{
		int pct = -1;
		MaxRandom rng;
		// 基础 Σ=40(同上),charm=50 ⇒ 40。
		rollCapture(10, 10, 0, 0, 50, 0, /*hp=*/0, /*max_hp=*/100,
		            /*difficulty=*/30, bonus, asleep, rng, &pct);
		return pct;
	};
	CHECK(pct_for(0, false) == 40);
	CHECK(pct_for(0, true) == 55);   // + 睡眠 15
	CHECK(pct_for(20, false) == 60); // + 捕获率提升 20(在魅力乘之后相加)
	CHECK(pct_for(20, true) == 75);  // 两者叠加
	// 上限 99:构造一个超 99 的组合(难度 200 ⇒ Σ 巨大)。
	int pct = -1;
	MaxRandom rng;
	rollCapture(10, 10, 0, 0, 50, 0, 0, 100, /*difficulty=*/200, 0, true, rng, &pct);
	CHECK(pct == 99);
}

TEST_CASE("捕获:判定阈严格小于(RAND < WorkGet,battle_event.c:3867)")
{
	// WorkGet = 40(charm=50, Σ=40)。RAND=39 成功、=40 失败。
	auto try_capture = [](int rand_value)
	{
		ScriptedRandom rng({rand_value});
		return rollCapture(10, 10, 0, 0, 50, 0, /*hp=*/0, /*max_hp=*/100,
		                   /*difficulty=*/30, /*bonus=*/0, false, rng, nullptr);
	};
	CHECK(try_capture(39) == true);  // 39 < 40
	CHECK(try_capture(40) == false); // 40 < 40 为假 ⇒ 严格小于
}

TEST_CASE("捕获:可回放 —— 同种子 + 同输入 ⇒ 结果逐位相同")
{
	SeededRandom r1(0xCAB1E), r2(0xCAB1E);
	int p1 = -1, p2 = -2;
	const bool ok1 = rollCapture(30, 20, 120, 60, 60, 8, 50, 300, 30, 5, true, r1, &p1);
	const bool ok2 = rollCapture(30, 20, 120, 60, 60, 8, 50, 300, 30, 5, true, r2, &p2);
	CHECK(ok1 == ok2);
	CHECK(p1 == p2);
	CHECK(r1.state() == r2.state());
}

// ── ResolveTurn 里的捕获派发 ────────────────────────────────────────────────

namespace
{
void setCapture(TurnCommands &tc, int slot, int target)
{
	tc.present[slot] = true;
	tc.commands[slot] = SA::Domain::BattleCommand{};
	tc.commands[slot].command_kind = SA::Domain::BattleCommand::CommandKind::CAPTURE;
	tc.commands[slot].command.capture.target = static_cast<std::uint32_t>(target);
}
} // namespace

TEST_CASE("ResolveTurn:捕获成功 ⇒ CaptureAct(flags=1),敌人可捕、等级门通过")
{
	Duel d = makeDuel();
	d.field.at(0).charm = 100; // 高魅力 ⇒ 乘性放大
	d.field.at(0).level = 50;
	d.field.at(10).level = 10; // 等级门:50+5 < 10 为假 ⇒ 通过
	d.field.at(10).hp = 0;     // 残血 ⇒ Df_HpPer 高
	d.field.at(10).max_hp = 100;
	d.field.at(10).mods.capturable = true;
	d.field.at(10).mods.capture_difficulty = 30;
	setCapture(d.cmds, 0, 10);
	d.cmds.present[10] = false; // 敌方不行动,只看捕获

	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({1}); // RAND(1,100)=1,远小于 WorkGet ⇒ 成功
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	REQUIRE(ev.events.size() == 1);
	REQUIRE(ev.events[0].body_kind == SA::Domain::BattleEvent::BodyKind::CAPTURE_ACT);
	CHECK(ev.events[0].body.capture_act.actor == 0u);
	CHECK(ev.events[0].body.capture_act.target == 10u);
	CHECK(ev.events[0].body.capture_act.flags == 1u);
}

TEST_CASE("ResolveTurn:前置门任一不过 ⇒ 仍产 CaptureAct 但 flags=0(不是无事件)")
{
	// ★ 与"批次未接入指令什么都不发生"不同:捕获无论成败都发 BT(原版 :4225),
	//   客户端要演"抓失败"。三道门逐条验其失败仍产事件、flags=0。
	auto flags_for = [](void (*mut)(Combatant &))
	{
		Duel d = makeDuel();
		d.field.at(0).charm = 100;
		d.field.at(0).level = 50;
		d.field.at(10).level = 10;
		d.field.at(10).hp = 0;
		d.field.at(10).max_hp = 100;
		d.field.at(10).mods.capturable = true;
		d.field.at(10).mods.capture_difficulty = 30;
		mut(d.field.at(10)); // 逐条破坏一道门 / 或改攻方
		setCapture(d.cmds, 0, 10);
		d.cmds.present[10] = false;
		SA::Domain::BattleEvents ev{};
		ScriptedRandom rng({1});
		resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev);
		REQUIRE(ev.events.size() == 1);
		REQUIRE(ev.events[0].body_kind == SA::Domain::BattleEvent::BodyKind::CAPTURE_ACT);
		return ev.events[0].body.capture_act.flags;
	};

	// ① 目标不带可捕获标记 ⇒ flags=0。
	CHECK(flags_for([](Combatant &t)
	                { t.mods.capturable = false; }) == 0u);
	// ② 目标不是敌人(改成宠物)⇒ flags=0。
	CHECK(flags_for([](Combatant &t)
	                { t.kind = CombatantKind::kPet; }) == 0u);
}

TEST_CASE("ResolveTurn:条件道具门 ④ ——攻方 capture_item_ok=false ⇒ flags=0 且不摇捕获 rng")
{
	// ★★ 捕获前置门 ④(`BATTLE_CaptureItemCheck`,§6.2)。原版 `flg = ItemCheck &&
	//    CaptureCheck`(`battle_event.c:4101`):攻方没带所需条件道具 ⇒ 整笔失败,连
	//    概率都不摇。道具门读背包(世界态),由 World 层在 resolveTurn 前投影到攻方
	//    `mods.capture_item_ok`(见 Combatant.h / World.cpp);L3 只做 `&&` 门判定。
	auto run = [](void (*mut)(Duel &)) -> std::pair<std::uint32_t, std::uint64_t>
	{
		Duel d = makeDuel();
		d.field.at(0).charm = 100;
		d.field.at(0).level = 50;
		d.field.at(10).level = 10;
		d.field.at(10).hp = 0;
		d.field.at(10).max_hp = 100;
		d.field.at(10).mods.capturable = true;
		d.field.at(10).mods.capture_difficulty = 30;
		mut(d);
		setCapture(d.cmds, 0, 10);
		d.cmds.present[10] = false;
		SeededRandom rng{0xF00D};
		SA::Domain::BattleEvents ev{};
		resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev);
		REQUIRE(ev.events.size() == 1);
		REQUIRE(ev.events[0].body_kind == SA::Domain::BattleEvent::BodyKind::CAPTURE_ACT);
		return {ev.events[0].body.capture_act.flags, rng.state()};
	};

	// 门 ④ 不过(缺条件道具)⇒ flags=0。
	const auto gate4 = run([](Duel &d)
	                       { d.field.at(0).mods.capture_item_ok = false; });
	CHECK(gate4.first == 0u);

	// ★★ 与门 ②(capturable=false)对比:两者都在 rollCapture **之前**被拦 ⇒ **消耗同样
	//    多的 rng**(都只花在 buildActionOrder 的排序上,没摇捕获骰子)。⇒ 末态相等。
	//    若哪天有人把门 ④ 挪到 rollCapture 之后,这条会红(gate4 会多摇一次)。
	const auto gate2 = run([](Duel &d)
	                       { d.field.at(10).mods.capturable = false; });
	CHECK(gate2.first == 0u);
	CHECK(gate4.second == gate2.second); // ★ rng 末态一致 = 都没摇捕获骰子

	// 对照:两门全开 ⇒ 摇了捕获骰子 ⇒ 成功 + rng 末态与被拦路径不同。
	const auto open = run([](Duel &) {});
	CHECK(open.first == 1u);
	CHECK(open.second != gate4.second); // ★ 多摇了一次 ⇒ 末态不同
}

TEST_CASE("ResolveTurn:等级门 myLv + 5 < targetLv ⇒ 直接失败(battle_event.c:3834)")
{
	Duel d = makeDuel();
	d.field.at(0).charm = 100;
	d.field.at(0).level = 10;
	d.field.at(10).level = 20; // 10 + 5 = 15 < 20 ⇒ 等级门失败
	d.field.at(10).hp = 0;
	d.field.at(10).max_hp = 100;
	d.field.at(10).mods.capturable = true;
	d.field.at(10).mods.capture_difficulty = 30;
	setCapture(d.cmds, 0, 10);
	d.cmds.present[10] = false;

	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({1});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
	REQUIRE(ev.events.size() == 1);
	CHECK(ev.events[0].body.capture_act.flags == 0u); // 等级门挡下,RollCapture 未被调

	// 边界:恰好 myLv + 5 == targetLv ⇒ **不**触发失败(严格小于)。
	d.field.at(0).level = 15; // 15 + 5 = 20,不小于 20 ⇒ 通过
	SA::Domain::BattleEvents ev2{};
	ScriptedRandom rng2({1});
	resolveTurn(d.field, d.cmds, RulesConfig{}, rng2, ev2);
	CHECK(ev2.events[0].body.capture_act.flags == 1u);
}

// ═══════════════════════════════════════════════════════════════════════════
//  暴击(§3.3,批次 A.3)—— 判定阈在源码里齐全,批次 0.5 曾误判为"文档缺=不能做"
// ═══════════════════════════════════════════════════════════════════════════
//
// 来源:`BATTLE_CriticalCheckPlayer`(battle_event.c:1283,算 per)
//     + `BATTLE_AttackSeq`(:1592,判定 `RAND(1,10000) < perCri`)。
// ⚠️ 全程按源码的 `float` 逐位移植(与 RollCapture 同,DR-BT16),不用 double。

TEST_CASE("暴击:per 主公式手算基准(battle_event.c:1283-1352)")
{
	// 玩家攻 dex=100、敌守 dex=0、luck=0、equip=0:
	//   divpara=0.09 root=1;big=100 small=0 wari=1;work=100/0.09=1111.11;
	//   per=sqrt(1111.11)=33.33;×wari=33.33;+luck0;×100=3333(截断)。
	// ⇒ RAND=3333 失败(3333<3333 假)、=3332 成功。★ 严格小于。
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 100);
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 100, 0);
	auto try_crit = [&](int rand_value)
	{
		ScriptedRandom rng({rand_value});
		return rollCritical(atk, def, rng);
	};
	CHECK(try_crit(3332) == true);
	CHECK(try_crit(3333) == false); // 严格小于 ⇒ 恰好等于 per 判失败
}

TEST_CASE("暴击:玩家幸运直接加进 per(× wari 之后、× 100 之前,:1348)")
{
	// 同上但 luck=5:per=(33.33)+5=38.33,×100=3833。
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 100);
	atk.luck = 5;
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 100, 0);
	ScriptedRandom lo({3832}), hi({3833});
	CHECK(rollCritical(atk, def, lo) == true);
	CHECK(rollCritical(atk, def, hi) == false);
}

TEST_CASE("暴击:装备暴击值 × 0.5 加进 sqrt 之后(battle_event.c:1341)")
{
	// equip_critical=10 ⇒ +10*0.5=5,在 sqrt 之后、× wari 之前:
	//   per=(sqrt(1111.11)+5)*1+0=38.33,×100=3833。与 luck5 同值但入口不同。
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 100);
	atk.mods.equip_critical = 10;
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 100, 0);
	ScriptedRandom lo({3832}), hi({3833});
	CHECK(rollCritical(atk, def, lo) == true);
	CHECK(rollCritical(atk, def, hi) == false);
}

TEST_CASE("暴击:类型跨界(敌→玩/敌→宠)分母暴增且不取根 ⇒ 暴击率骤降(:1312-1318)")
{
	// 敌攻 dex=100、玩守 dex=0:divpara=10 root=0(不取根)⇒ work=100/10=10,
	//   per=10(无 sqrt),×wari1,+luck0(敌方非玩不加 luck),×100=1000。
	// ★ 对比同 dex 差的玩→敌(3333):跨界把 3333 压到 1000,量级骤降正是 divpara 111 倍的效果。
	auto enemy = makeCombatant(CombatantKind::kEnemy, 100, 100, 100);
	auto player = makeCombatant(CombatantKind::kPlayer, 100, 100, 0);
	ScriptedRandom lo({999}), hi({1000});
	CHECK(rollCritical(enemy, player, lo) == true);
	CHECK(rollCritical(enemy, player, hi) == false);
}

TEST_CASE("暴击:敌方攻击方不吃 At_Luck(:1295 仅玩家取幸运)")
{
	// 敌→玩,给敌方 luck=99:若错误地加了 luck,per 会从 1000 抬到 10900→clamp 10000。
	//   正确行为:敌方非玩家 ⇒ At_Luck=0 ⇒ per 仍 1000。
	auto enemy = makeCombatant(CombatantKind::kEnemy, 100, 100, 100);
	enemy.luck = 99;
	auto player = makeCombatant(CombatantKind::kPlayer, 100, 100, 0);
	ScriptedRandom hi({1000});
	CHECK(rollCritical(enemy, player, hi) == false); // 仍是 1000,不是 10000
}

TEST_CASE("暴击:免疫标志(DR-BT11 数据驱动,原图号 101813/101814)⇒ per 强制 0(:1349)")
{
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 100000); // 极高 dex ⇒ per 本会满
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 100, 0);
	// 未免疫:极高 dex 差 ⇒ per 触顶,RAND=1 必暴击。
	{
		ScriptedRandom rng({1});
		CHECK(rollCritical(atk, def, rng) == true);
	}
	// ★ DR-BT11:免疫按**标志位**判(不比对图号)⇒ per=0,RAND=1 也不暴击(1<0 假)。
	{
		auto ler = def;
		ler.mods.immune_critical = true;
		ScriptedRandom rng({1});
		CHECK(rollCritical(atk, ler, rng) == false);
	}
}

TEST_CASE("暴击:可回放 —— 同种子 + 同输入 ⇒ 结果与 rng 消费序列逐位相同")
{
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 100, 130);
	atk.luck = 3;
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 100, 70);
	SeededRandom r1(0xC217), r2(0xC217);
	const bool a = rollCritical(atk, def, r1);
	const bool b = rollCritical(atk, def, r2);
	CHECK(a == b);
	CHECK(r1.state() == r2.state());
}

TEST_CASE("暴击伤害:= ComputeDamage + 守方原始防御 × LVatt/LVdef × 0.5(:1419)")
{
	// 附加项 = defense(200) × LVatt(20)/LVdef(10) × 0.5 = 200。
	//   用 SeededRandom 让两次 ComputeDamage 消费同序列 ⇒ base 相等,差值 == 附加项。
	auto atk = makeCombatant(CombatantKind::kPlayer, 1000, 0, 0);
	atk.level = 20;
	auto def = makeCombatant(CombatantKind::kPlayer, 0, 200, 0); // 非敌人 ⇒ 无 _NPCENEMY 上浮扰动
	def.level = 10;
	const BattleField field = makeField();

	SeededRandom rb(777), rc(777);
	const std::int32_t base = computeDamage(field, atk, def, RulesConfig{}, rb);
	const std::int32_t crit = computeCriticalDamage(field, atk, def, RulesConfig{}, rc);
	// add = 200 * 20/10 * 0.5 = 200(f32 精确)。
	CHECK(crit - base == 200);
	CHECK(rb.state() == rc.state()); // 消费同样多的随机数
}

// ── ResolveTurn 里的暴击派发 ────────────────────────────────────────────────

TEST_CASE("ResolveTurn:暴击命中 ⇒ Damage 带 CRITICAL 标志,且**不**带 NORMAL(互斥)")
{
	// ScriptedRandom 逐个喂本回合的抽取序列(按 ResolveTurn 的消费顺序):
	//   ① 行动顺序 dex 抖动(RandMod) ② 回避 RAND(喂 9999 ⇒ 恒不闪)
	//   ③ 暴击 RAND(喂 1 ⇒ 必暴击,因攻方极高 dex 令 per 触顶)④ 伤害若干。
	// ⚠️ 攻方用空手 + lv<10 ⇒ 攻击次数恒 1 段,去掉多段自由度。
	Duel d = makeDuel(1000, 10);
	d.field.at(0).quick = 100000; // ⇒ 暴击 per 触顶
	d.field.at(0).mods.unarmed = true;
	d.field.at(0).level = 1;
	d.field.at(10).quick = 0;

	SA::Domain::BattleEvents ev{};
	ScriptedRandom srng({/*两槽 dex*/ 0, 0, /*回避*/ 9999, /*暴击*/ 1, /*伤害*/ 500, 500, 500, 500});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, srng, ev));

	const SA::Domain::Damage *dmg = nullptr;
	for (const auto &e : ev.events)
	{
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
		{
			dmg = &e.body.damage;
			break;
		}
	}
	REQUIRE(dmg != nullptr);
	const auto crit_flag = static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_CRITICAL);
	const auto norm_flag = static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_NORMAL);
	CHECK((dmg->flags & crit_flag) != 0u); // 暴击标志置位
	CHECK((dmg->flags & norm_flag) == 0u); // ★ 与 NORMAL 互斥
}

TEST_CASE("ResolveTurn:未暴击命中 ⇒ NORMAL 标志、无 CRITICAL")
{
	Duel d = makeDuel(1000, 10);
	d.field.at(0).quick = 0; // per=0 ⇒ 不可能暴击
	d.field.at(10).quick = 0;
	SA::Domain::BattleEvents ev{};
	ScriptedRandom srng({0, 0, 9999, 5000, 500}); // 暴击抽 5000 也无所谓,per=0
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, srng, ev));
	const SA::Domain::Damage *dmg = nullptr;
	for (const auto &e : ev.events)
	{
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
		{
			dmg = &e.body.damage;
			break;
		}
	}
	REQUIRE(dmg != nullptr);
	const auto crit_flag = static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_CRITICAL);
	const auto norm_flag = static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_NORMAL);
	CHECK((dmg->flags & norm_flag) != 0u);
	CHECK((dmg->flags & crit_flag) == 0u);
}

TEST_CASE("ResolveTurn:持弓暴击 ⇒ 置 CRITICAL 标志但伤害不吃加成(battle_event.c:1594)")
{
	// 两场同种子:唯一差别是 wielding_bow。暴击都命中(标志都在),
	//   但持弓那场伤害应等于**普通** computeDamage(不加防御项)⇒ 伤害更低。
	auto run = [](bool bow)
	{
		Duel d = makeDuel(1000, 200); // 守方有防御 ⇒ 暴击附加项非零,差异才可观测
		d.field.at(0).quick = 100000; // per 触顶
		d.field.at(0).level = 5;      // ★ lv<10 ⇒ 空手恒 1 段,不消费攻击次数的 RNG
		d.field.at(0).mods.unarmed = true;
		d.field.at(0).mods.wielding_bow = bow;
		d.field.at(10).quick = 0;
		d.field.at(10).level = 10; // 附加项 = 200 × 5/10 × 0.5 = 50,持弓省掉
		SA::Domain::BattleEvents ev{};
		ScriptedRandom srng({0, 0, 9999, 1, 500, 500, 500, 500});
		resolveTurn(d.field, d.cmds, RulesConfig{}, srng, ev);
		for (const auto &e : ev.events)
			if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
				return e.body.damage;
		return SA::Domain::Damage{};
	};
	const auto no_bow = run(false);
	const auto bow = run(true);
	const auto crit_flag = static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_CRITICAL);
	// ★ 两者都标 CRITICAL(客户端都要演"会心")。
	CHECK((no_bow.flags & crit_flag) != 0u);
	CHECK((bow.flags & crit_flag) != 0u);
	// ★ 持弓不吃加成 ⇒ 伤害更小(附加项 = 200 × 20/10 × 0.5 = 200 被省掉)。
	//   hp_delta 是负数,持弓伤害更小 ⇒ 其绝对值更小 ⇒ hp_delta 更大(更接近 0)。
	CHECK(bow.hp_delta > no_bow.hp_delta);
}

// ═══════════════════════════════════════════════════════════════════════════
//  打飞 / 究极一击(§3.8,批次 A.4)—— RollKnockback 直接判定
// ═══════════════════════════════════════════════════════════════════════════
//
// 全部锚定 `battle_event.c:2060-2081`。门槛 `maxhp × 1.2 + 20`(float 语义)。

TEST_CASE("打飞:一击打飞 = 本段 damage ≥ maxhp×1.2+20(battle_event.c:2062)")
{
	// maxhp=100 ⇒ 门槛 = 100×1.2+20 = 140。
	std::int32_t acc = 0;
	// damage=140 恰达门槛(≥,严格达到即触发)⇒ 一击打飞,累加器不动、且清零(命中)。
	CHECK(rollKnockback(140, /*overflow=*/0, /*max_hp=*/100, /*acc=*/50, false, &acc) ==
	      KnockbackKind::kOneShot);
	CHECK(acc == 0); // ★ 命中 ⇒ 清零(:2081)

	// damage=139 差 1 ⇒ 不触发一击;overflow=0 ⇒ 也不进累积分支。
	acc = 50;
	CHECK(rollKnockback(139, 0, 100, 50, false, &acc) == KnockbackKind::kNone);
	CHECK(acc == 50); // ★ 未命中且无溢出 ⇒ 累加器原样
}

TEST_CASE("打飞:一击路径不累加(源码 if 分支,addpoint 只在 else,battle_event.c:2062-2069)")
{
	// ★ 即便本段有溢出,只要够一击打飞就走 if 分支,**不碰累加器**(除了命中清零)。
	std::int32_t acc = 30;
	CHECK(rollKnockback(200, /*overflow=*/60, 100, 30, false, &acc) ==
	      KnockbackKind::kOneShot);
	CHECK(acc == 0); // 清零,而不是 30+60
}

TEST_CASE("打飞:累积打飞 = 未一击且累加器+溢出 ≥ 门槛(battle_event.c:2065-2068)")
{
	// 门槛 140。damage=50(<140,不一击),overflow=30。
	std::int32_t acc = 100;
	// 100 + 30 = 130 < 140 ⇒ 仅累加,不触发。
	CHECK(rollKnockback(50, 30, 100, 100, false, &acc) == KnockbackKind::kNone);
	CHECK(acc == 130); // ★ 累加后写回(:2067),未命中 ⇒ 不清零

	// 再来一段:110 + 30 = 140 ≥ 140 ⇒ 累积打飞,命中清零。
	CHECK(rollKnockback(50, 30, 100, 110, false, &acc) == KnockbackKind::kAccumulated);
	CHECK(acc == 0);
}

TEST_CASE("打飞:无溢出则不进累积分支(addpoint > 0 门槛,battle_event.c:2065)")
{
	// overflow=0 ⇒ 即便累加器已很大也不判、不动它(原版 `if(addpoint>0)`)。
	std::int32_t acc = 1000;
	CHECK(rollKnockback(50, 0, 100, 1000, false, &acc) == KnockbackKind::kNone);
	CHECK(acc == 1000); // ★ 一动不动:不累加(溢出为 0)、不清零(未命中)
}

TEST_CASE("打飞:免疫 ⇒ 恒 kNone,但累加仍发生(battle_event.c:2076 在累加之后)")
{
	// ★★ 逐位照源码顺序:先 addpoint 累加并写回 WORKULTIMATE,再按图号把 IsUltimate 清 0。
	//   ⇒ 免疫单位的累加器**照常累加**,只是这次不判为打飞、也不清零。
	std::int32_t acc = 200;
	// 200 + 30 = 230 ≥ 140 本应累积打飞,但免疫 ⇒ kNone;累加器留 230(累加了、没清零)。
	CHECK(rollKnockback(50, 30, 100, 200, /*immune=*/true, &acc) == KnockbackKind::kNone);
	CHECK(acc == 230);

	// 一击路径 + 免疫:一击 if 分支不累加,免疫把结果归 kNone ⇒ 累加器原样(未清零)。
	acc = 55;
	CHECK(rollKnockback(200, 60, 100, 55, true, &acc) == KnockbackKind::kNone);
	CHECK(acc == 55);
}

TEST_CASE("打飞:门槛是 float 运算而非整数(battle_event.c:2062)")
{
	// maxhp=25 ⇒ 门槛 = 25×1.2+20 = 50.0(float)。整数近似 25*12/10+20 = 50 恰好同值,
	//   换 maxhp=21:21×1.2+20 = 45.2。damage=45 < 45.2 ⇒ 不触发;damage=46 ≥ ⇒ 触发。
	//   ★ 若误用整数 21*1.2 会先把 1.2 截成 1 ⇒ 门槛塌成 41,45 就会误判为打飞。
	std::int32_t acc = 0;
	CHECK(rollKnockback(45, 0, 21, 0, false, &acc) == KnockbackKind::kNone);
	CHECK(rollKnockback(46, 0, 21, 0, false, &acc) == KnockbackKind::kOneShot);
}

// ═══════════════════════════════════════════════════════════════════════════
//  打飞 —— ResolveTurn 接入
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ResolveTurn:一击打飞 ⇒ Damage 置 ULTIMATE_2,KnockbackState 回填清零")
{
	// 高攻低防、目标低血 ⇒ 一段就打穿且过门槛。
	Duel d = makeDuel(/*atk=*/10000, /*def=*/1);
	d.field.at(0).level = 5; // lv<10 ⇒ 空手恒 1 段
	d.field.at(0).mods.unarmed = true;
	d.field.at(10).max_hp = 100; // 门槛 = 140
	d.field.at(10).hp = 100;
	d.field.at(10).ultimate_accumulator = 99; // 命中后应被清零
	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({0, 0, 10000, 10000, 0, 0}); // dex 抖动 / 段数 / per(不暴击)
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	const SA::Domain::Damage *dmg = nullptr;
	const SA::Domain::KnockbackState *ks = nullptr;
	for (const auto &e : ev.events)
	{
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
			dmg = &e.body.damage;
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::KNOCKBACK_STATE)
			ks = &e.body.knockback_state;
	}
	REQUIRE(dmg != nullptr);
	CHECK((dmg->flags &
	       static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_ULTIMATE_2)) != 0u);
	// ★ 一击打飞命中 ⇒ 累加器清零(99→0,变化),单开 KnockbackState 回填。
	REQUIRE(ks != nullptr);
	CHECK(ks->target == 10u);
	CHECK(ks->accumulator == 0);
	// 一击致死且打飞可并存(§3.8:打飞判定在死亡标记之前)。
	CHECK((dmg->flags &
	       static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_DEATH)) != 0u);
}

TEST_CASE("ResolveTurn:免疫单位不置打飞标志(DR-BT11 数据驱动,不比对图号)")
{
	Duel d = makeDuel(/*atk=*/10000, /*def=*/1);
	d.field.at(0).level = 5;
	d.field.at(0).mods.unarmed = true;
	d.field.at(10).max_hp = 100;
	d.field.at(10).hp = 100;
	d.field.at(10).mods.immune_knockback = true; // ★ 标志位,非图号
	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({0, 0, 10000, 10000, 0, 0});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	const SA::Domain::Damage *dmg = nullptr;
	for (const auto &e : ev.events)
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
			dmg = &e.body.damage;
	REQUIRE(dmg != nullptr);
	const std::uint32_t ult =
	    static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_ULTIMATE_1) |
	    static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_ULTIMATE_2);
	CHECK((dmg->flags & ult) == 0u); // ★ 免疫 ⇒ 无打飞标志
}

TEST_CASE("ResolveTurn:打穿未过门槛 ⇒ 不打飞但 KnockbackState 记录累加")
{
	// ★ 要走**累积**路径需 damage < 门槛(否则一击打飞、且清零 ⇒ 累加器不变、不产事件)。
	//   ⇒ 门槛抬高:max_hp=100000 ⇒ 门槛 120020;damage(atk=1000)远小于它,但把 hp=1
	//     打穿 ⇒ overflow>0 ⇒ 累加进累加器却不过门槛 ⇒ kNone + 累加器增长。
	Duel d = makeDuel(/*atk=*/1000, /*def=*/1);
	d.field.at(0).level = 5;
	d.field.at(0).mods.unarmed = true;
	d.field.at(10).max_hp = 100000;
	d.field.at(10).hp = 1; // 打穿(damage-1)的溢出,远小于 120020
	d.field.at(10).ultimate_accumulator = 0;
	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({0, 0, 10000, 10000, 0, 0});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	const SA::Domain::Damage *dmg = nullptr;
	const SA::Domain::KnockbackState *ks = nullptr;
	for (const auto &e : ev.events)
	{
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
			dmg = &e.body.damage;
		if (e.body_kind == SA::Domain::BattleEvent::BodyKind::KNOCKBACK_STATE)
			ks = &e.body.knockback_state;
	}
	REQUIRE(dmg != nullptr);
	const std::uint32_t ult1 =
	    static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_ULTIMATE_1);
	CHECK((dmg->flags & ult1) == 0u); // 未过门槛 ⇒ 不打飞
	// ★ 累加器 0→正(变化)⇒ 单开 KnockbackState 记录,accumulator > 0 且未清零。
	REQUIRE(ks != nullptr);
	CHECK(ks->accumulator > 0);
}

TEST_CASE("ResolveTurn:不打穿(有剩血)⇒ 累加器不变,不产 KnockbackState")
{
	// 目标血厚、伤害咬不动到打穿 ⇒ overflow=0 ⇒ 累加器一动不动 ⇒ 不产该事件(变化才产)。
	Duel d = makeDuel(/*atk=*/1000, /*def=*/500);
	d.field.at(0).level = 5;
	d.field.at(0).mods.unarmed = true;
	d.field.at(10).max_hp = 100000;
	d.field.at(10).hp = 100000; // 血远高于单段伤害 ⇒ 不打穿
	d.field.at(10).ultimate_accumulator = 0;
	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({0, 0, 10000, 10000, 0, 0});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	CHECK(countKind(ev, SA::Domain::BattleEvent::BodyKind::KNOCKBACK_STATE) == 0u);
}

// ── ★★ DR-BT23:随机源在退化区间上的消耗语义 ────────────────────────────
//
// ★★ **立案背景比结论重要**(2026-09-09):`SeededRandom::rand` 原写作
//    `if (hi <= lo) return lo;` —— 返回值与原版 `RAND(x,x)` 一致,但**少消耗一次**
//    ⇒ rng 序列自此整体平移。
//
// ⚠️★★ 把它改对之后,**既有 3,300+ 条断言一条都没红**,而原因不是"断言不敏感":
//    全部用例走 `ScriptedRandom`(它从第一天起就无条件取数),
//    **生产实现 `SeededRandom` 的这一支从未被任何用例触及**
//    ⇒ 同一个接口的两个实现语义分叉,跨了三个批次没有任何东西发现。
//    ★ 而 `RulesProgressionTest` 当时就写下了「消费次数是可回放性的一部分」——
//      认识早就有了,缺的是**把它作用到生产实现上的那条断言**。
//
// ⇒ 本组守的正是那个缺口。★ 判据分两层:① 退化区间照常消耗;
//    ② **两个实现在这一点上必须一致** —— 只验其中一个,分叉会再次静默发生。
TEST_CASE("DR-BT23★★:退化区间照常消耗一次 —— 返回值相同而序列平移")
{
	SUBCASE("hi == lo 也消耗(本条是 DR-BT23 的关闭判据)")
	{
		SeededRandom r{42};
		const auto before = r.state();
		CHECK(r.rand(5, 5) == 5);   // 原版 `RAND(5,5)`:系数 1 ⇒ 取整 0 ⇒ 恒为 5
		CHECK(r.state() != before); // ★ 状态必须变 —— 旧实现在此不动
	}

	SUBCASE("hi < lo:返回 lo 且照常消耗")
	{
		// ★ 原版 `RAND(0,-1)`:`y-(x-1)` = `-1-(-1)` = **0** ⇒ 系数 0 ⇒ 取整 0 ⇒ 返回 0。
		// ⚠️ 而 `rand()` 是那个乘法的操作数 ⇒ **系数为 0 也要求值** ⇒ 照样消耗。
		//    ⇒ 这一支在真实数据上会发生:`group1.txt` 有 4 行编组权重和为 0
		//      ⇒ `r_max = Σ − 1 = −1`,其中 3 行被 `encount.txt` 引用(**可达**)。
		SeededRandom r{555};
		const auto before = r.state();
		CHECK(r.rand(0, -1) == 0);
		CHECK(r.state() != before);
	}

	SUBCASE("randMod(n <= 0) 也消耗 —— 原版取模前 rand() 已求值")
	{
		SeededRandom r{1234};
		const auto before = r.state();
		CHECK(r.randMod(0) == 0);
		CHECK(r.state() != before);
	}

	SUBCASE("★ 退化调用恰好占掉序列里的一位(旧实现在此必红)")
	{
		// ★★ 这条比"状态变了没"更硬:它断言**平移量正好是一位**,
		//    而不只是"有变化"。⇒ 若将来有人把退化支改成消耗两次,只有这条会红。
		SeededRandom a{7}, b{7};
		a.rand(3, 3);   // 退化调用:占掉第 1 位
		b.rand(0, 999); // 普通调用:占掉同一位
		CHECK(a.rand(0, 999) == b.rand(0, 999));
	}

	SUBCASE("★★ 两个实现的退化语义一致 —— 防的是「只改了生产、忘了脚本源」")
	{
		SeededRandom s{99};
		ScriptedRandom c({111, 222});
		const auto s0 = s.state();

		CHECK(s.rand(4, 4) == 4);
		CHECK(c.rand(4, 4) == 4); // 脚本值 111 被钳到 [4,4]

		CHECK(s.state() != s0);
		CHECK(c.calls() == 1);
		// ★★ 判据必须落在"下一发"上 —— 这一条是**反向验证逼出来的**:
		//    把 `ScriptedRandom` 也改成早退时,`c.calls() == 1` **照样绿**
		//    (计数器在早退之前就 ++ 了)⇒ ★ `calls()` 数的是**调用次数**,
		//    不是**取数次数**,而分叉恰恰发生在后者。
		//    ⇒ 只有这一条会红:若脚本源没消耗,这里拿到的是 111 而不是 222。
		CHECK(c.rand(0, 1000) == 222);
	}
}

// ── L4.1 状态异常 ──────────────────────────────────────────────────────────
//
// ★★ 本系列钉的是 `05` §4 的六条结构事实 + 本批开工取证抓到的**六处文档偏差**
//    (`00` §9.0.55 / DR-DT24)。⚠️ 多数偏差**返回值断言抓不到**,所以下面
//    逐条指明"若按文档实现会怎样"——那才是这些用例存在的理由。

TEST_CASE("状态★★★:全局互斥 —— 身上有任意状态时新状态一律施加失败(05 §4.1)")
{
	// 这是本模块最重要的单条结构事实:**不是同类互斥,是全类互斥**。
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 10);
	atk.level = 10;
	atk.luck = 0;
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 10);
	def.level = 10;
	def.vital = 100;
	def.str = 100;
	def.tough = 100;
	def.dex = 100;

	SUBCASE("槽空 ⇒ 摇骰子")
	{
		ScriptedRandom rng({1}); // RAND(1,100) = 1 < per ⇒ 命中
		int per = 0;
		CHECK(rollStatusAttack(false, atk, def,
		                       static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_POISON),
		                       /*per_offset=*/30, 40, 2.0, rng, &per));
		CHECK(rng.calls() == 1);
	}

	SUBCASE("★★ 槽被别的状态占住 ⇒ 失败,且**一次 rng 都不摇**")
	{
		// ⚠️★ 互斥判定在源码 `:5076-5079`,位于任何 RAND **之前**
		//    ⇒ 与捕获的道具门 ④ 同族:门不过连骰子都不掷。
		//    ★ 若把互斥挪到摇骰之后,返回值断言**照样全绿**(都是 false),
		//      而 rng 序列自此整体平移 ⇒ 只有 calls() 抓得到(DR-BT23 同族)。
		def.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_SLEEP);
		def.status_turns = 3;
		ScriptedRandom rng({1});
		int per = 0;
		CHECK_FALSE(rollStatusAttack(false, atk, def,
		                             static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_POISON),
		                             30, 40, 2.0, rng, &per));
		CHECK(rng.calls() == 0); // ★★ 这一条才是互斥位置的判据
	}

	SUBCASE("★ 互斥是全类的 —— 连「同一种状态再上一次」也失败")
	{
		def.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON);
		def.status_turns = 1;
		ScriptedRandom rng({1});
		int per = 0;
		CHECK_FALSE(rollStatusAttack(false, atk, def,
		                             static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_POISON),
		                             30, 40, 2.0, rng, &per));
		CHECK(rng.calls() == 0);
	}
}

TEST_CASE("状态⚠️★★:麻痹走独立分支 —— 固定基数 20,不看等级/幸运/体力/装备抗性")
{
	// ⚠️ `05` §4.3 写「per = 20 − 抗性 − **装备抗性**」且有 `max(per, 0)`,
	//    **源码两者都没有**(`battle_event.c:5080-5084`,2026-09-11 核实)。
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 10);
	atk.level = 99; // 等级差极大
	atk.luck = 25;  // 幸运拉满
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 10);
	def.level = 1;
	def.vital = 1; // 体力占比极端
	def.str = 999;
	def.tough = 999;
	def.dex = 999;

	const int kParalysis = static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_PARALYSIS);

	SUBCASE("per 恒为 20 —— 等级差与幸运一概不进式子")
	{
		ScriptedRandom rng({19}); // 19 < 20 ⇒ 命中
		int per = 0;
		CHECK(rollStatusAttack(false, atk, def, kParalysis, /*per_offset=*/30, 40, 2.0, rng, &per));
		// ★★ per_offset 传了 30 也不生效,等级差 +98 也不生效 ⇒ 恒 20。
		//    若误走通用分支,per 会是 30+80(夹上限)⇒ 这一条当场红。
		CHECK(per == 20);
	}

	SUBCASE("抗性直减,且**没有** max(per,0) 兜底")
	{
		def.mods.status_resist[kParalysis] = 50; // 20 - 50 = -30
		ScriptedRandom rng({1});
		int per = 0;
		CHECK_FALSE(rollStatusAttack(false, atk, def, kParalysis, 30, 40, 2.0, rng, &per));
		CHECK(per == -30);       // ★ 照源码留负值;按文档补 clamp 会变成 0 ⇒ 本行红
		CHECK(rng.calls() == 1); // ⚠️ 即使必不命中也照常摇(RAND 在判定式里)
	}

	SUBCASE("⚠️ 装备抗性对麻痹**不生效**(文档记宽了)")
	{
		// 给三种装备抗性都填上大值 —— 麻痹分支一个都不读。
		def.mods.equip_resist_weaken = 99;
		def.mods.equip_resist_barrier = 99;
		def.mods.equip_resist_nocast = 99;
		def.mods.suit_resist_weaken = 99;
		ScriptedRandom rng({19});
		int per = 0;
		CHECK(rollStatusAttack(false, atk, def, kParalysis, 30, 40, 2.0, rng, &per));
		CHECK(per == 20); // 一分都没被减
	}
}

TEST_CASE("状态:通用分支 —— 等级差×Bai 夹 ±Range、PvP 归零、80% 硬上限")
{
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 10);
	atk.luck = 0;
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 10);
	// 体力占比 = 25/100 = 0.25 ⇒ /0.25*10 = 10 ⇒ 截断后减 10。
	def.vital = 25;
	def.str = 25;
	def.tough = 25;
	def.dex = 25;
	const int kPoison = static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_POISON);

	SUBCASE("等级差 × Bai 后夹到 +Range")
	{
		atk.level = 100;
		def.level = 1; // 差 99 × 2.0 = 198 ⇒ 夹到 Range=40
		ScriptedRandom rng({1});
		int per = 0;
		rollStatusAttack(false, atk, def, kPoison, /*per_offset=*/30, 40, 2.0, rng, &per);
		// per = 30 + 40 + 0 - 0 - 10 - 0 = 60
		CHECK(per == 60);
	}

	SUBCASE("等级差为负时夹到 −Range")
	{
		atk.level = 1;
		def.level = 100; // −99 × 2.0 = −198 ⇒ 夹到 −40
		ScriptedRandom rng({1});
		int per = 0;
		rollStatusAttack(false, atk, def, kPoison, 30, 40, 2.0, rng, &per);
		CHECK(per == 30 - 40 - 10); // = -20
	}

	SUBCASE("★ PvP ⇒ 等级差恒 0(源码 `:5128` 的 type != P_vs_P 门)")
	{
		atk.level = 100;
		def.level = 1;
		ScriptedRandom rng({1});
		int per = 0;
		rollStatusAttack(/*is_pvp=*/true, atk, def, kPoison, 30, 40, 2.0, rng, &per);
		CHECK(per == 30 - 10); // 等级差那一项没了
	}

	SUBCASE("★ 80% 硬上限")
	{
		atk.level = 100;
		def.level = 1;
		ScriptedRandom rng({1});
		int per = 0;
		rollStatusAttack(false, atk, def, kPoison, /*per_offset=*/200, 40, 2.0, rng, &per);
		CHECK(per == 80); // 200+40-10 = 230 ⇒ 夹到 80
	}

	SUBCASE("★ 判定是**严格小于**(同暴击/逃跑/捕获,与回避的 ≤ 不同)")
	{
		atk.level = 1;
		def.level = 1;
		// per = 30 + 0 + 0 - 0 - 10 - 0 = 20
		{
			ScriptedRandom rng({20}); // 20 < 20 为假 ⇒ 不命中
			int per = 0;
			CHECK_FALSE(rollStatusAttack(false, atk, def, kPoison, 30, 40, 2.0, rng, &per));
			CHECK(per == 20);
		}
		{
			ScriptedRandom rng({19}); // 19 < 20 ⇒ 命中
			int per = 0;
			CHECK(rollStatusAttack(false, atk, def, kPoison, 30, 40, 2.0, rng, &per));
		}
	}
}

TEST_CASE("状态⚠️★★:F02:不同枚举导致三种装备抗性原分支不生效")
{
	// F02: SSRC80 WORK 枚举 51/53/54 均超过 BATTLE_ST_END=44，原分支不可达。
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 10);
	atk.level = 1;
	atk.luck = 0;
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 10);
	def.level = 1;
	def.vital = 25;
	def.str = 25;
	def.tough = 25;
	def.dex = 25; // 体力项恒减 10
	def.mods.equip_resist_weaken = 5;
	def.mods.equip_resist_barrier = 7;
	def.mods.equip_resist_nocast = 11;
	def.mods.suit_resist_weaken = 3;

	auto perOf = [&](SA::Domain::BattleStatus st)
	{
		ScriptedRandom rng({1});
		int per = 0;
		rollStatusAttack(false, atk, def, static_cast<int>(st), 30, 40, 2.0, rng, &per);
		return per;
	};

	// 基线:毒不吃任何装备抗性 ⇒ 30 − 10 = 20。
	CHECK(perOf(SA::Domain::BattleStatus::BATTLE_ST_POISON) == 20);
	CHECK(perOf(SA::Domain::BattleStatus::BATTLE_ST_WEAKEN) == 20);
	CHECK(perOf(SA::Domain::BattleStatus::BATTLE_ST_BARRIER) == 20);
	CHECK(perOf(SA::Domain::BattleStatus::BATTLE_ST_NOCAST) == 20);
	// 睡眠/石化/混乱等一概不吃。
	CHECK(perOf(SA::Domain::BattleStatus::BATTLE_ST_SLEEP) == 20);
	CHECK(perOf(SA::Domain::BattleStatus::BATTLE_ST_STONE) == 20);
}

TEST_CASE("状态★:毒煞的体力占比**反过来**取(_PET_SKILL_SARS,battle_event.c:5096)")
{
	auto atk = makeCombatant(CombatantKind::kPlayer, 100, 10);
	atk.level = 1;
	atk.luck = 0;
	auto def = makeCombatant(CombatantKind::kEnemy, 100, 10);
	def.level = 1;
	// 体力占比 = 10/100 = 0.1
	def.vital = 10;
	def.str = 30;
	def.tough = 30;
	def.dex = 30;

	ScriptedRandom r1({1});
	int per_poison = 0;
	rollStatusAttack(false, atk, def,
	                 static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_POISON),
	                 30, 40, 2.0, r1, &per_poison);
	// 普通:0.1/0.25*10 = 4 ⇒ 30 − 4 = 26
	CHECK(per_poison == 26);

	ScriptedRandom r2({1});
	int per_sars = 0;
	rollStatusAttack(false, atk, def,
	                 static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_SARS),
	                 30, 40, 2.0, r2, &per_sars);
	// 毒煞:(1−0.1)×0.9 = 0.81 ⇒ /0.25*10 = 32.4 ⇒ 截断 32 ⇒ 30 − 32 = −2
	// ★★ 体力**越高越容易中毒煞**,与其余 42 种相反 ⇒ 两个 per 必须不同。
	CHECK(per_sars == -2);
	CHECK(per_sars != per_poison);
}

TEST_CASE("状态⚠️★★:RegTbl 只有 31 项 —— 31..43 的抵抗恒 0(用户裁定照抄原版缺陷)")
{
	// `05` §4.5 + 2026-09-11 实测:`RegTbl` = 基础 11 + SARS 1 + PROFESSION_SKILL 19
	// = **31**,没有 `_PROFESSION_ADDSKILL` 那 13 项 ⇒ 三属抗/水附体/附身/恐惧/
	// 冰爆术 2-10 **无法被抵抗**。源码 `:5119` 有越界防护 ⇒ 是玩法事实不是崩溃。
	CHECK(kOriginalResistTableLen == 31);
	// ★ 这条断言挡的是"顺手把它补齐到 44" —— 那会**静默地**改掉 13 种状态的命中率。
	CHECK(kOriginalResistTableLen != kBattleStatusEnd);
}

TEST_CASE("状态★★:毒的每回合掉血 = ((Σ四维/100)−20)/4,下限 1,且**留 1 HP**")
{
	// `Compute_Down`(battle.c:5242)—— ★ 全程整数除法,两次截断不可合并。
	SUBCASE("主式:四维和 10000 ⇒ ((100)−20)/4 = 20")
	{
		CHECK(computePoisonDown(2500, 2500, 2500, 2500, /*hp=*/1000) == 20);
	}

	SUBCASE("★ 两次整数除法照抄源码形状 —— ⚠️ 但它与合并式**实测等价**,不声称要紧")
	{
		// ⚠️★★ **本条最初的标题是「与 (Σ−2000)/400 不等价」,那是我编的理由**
		//    (纪律 ⓪ 同族,第 N 次)。实测穷举 Σ ∈ [0, 2,000,000]:
		//      · 未经 clamp:两式分叉 495 处(首个 Σ=1,源码 −5 / 合并 −4);
		//      · **经「下限 1」clamp 之后:0 处分叉** ⇒ 在本函数的语义下**完全等价**。
		//    ⇒ 分叉全部落在结果为负的区间,而那一段被 `if (downs < 1) downs = 1` 吃掉。
		// ★ 结论:照抄源码的两步除法**没错**,但**不能说"合并会算错"** ——
		//   它只是与源码逐字一致,不是行为必需。(等价性要验不能推,I.4 的教训。)
		CHECK(computePoisonDown(2500, 2500, 2500, 2599, 1000) == 20); // 10199/100=101 ⇒ 81/4=20
		CHECK(computePoisonDown(2500, 2500, 2500, 2999, 1000) == 21); // 10499/100=104 ⇒ 84/4=21
	}

	SUBCASE("★ 下限 1 —— 四维不足时也掉 1 血")
	{
		CHECK(computePoisonDown(1, 1, 1, 1, 1000) == 1);
		CHECK(computePoisonDown(0, 0, 0, 0, 1000) == 1);
	}

	SUBCASE("★★ 留 1 HP —— 毒绝不致死")
	{
		// 掉血量 20,但 HP 只有 5 ⇒ 夹到 4,打完剩 1。
		CHECK(computePoisonDown(2500, 2500, 2500, 2500, /*hp=*/5) == 4);
		// HP == 1 ⇒ 掉 0(仍产事件,见 Status.h)。
		CHECK(computePoisonDown(2500, 2500, 2500, 2500, /*hp=*/1) == 0);
	}
}

TEST_CASE("状态★★:虚弱/魔障使计时冻结 ⇒ 永不自然解除(05 §4.2)")
{
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 10);
	c.vital = 2500;
	c.str = 2500;
	c.tough = 2500;
	c.dex = 2500;

	SUBCASE("普通状态正常递减")
	{
		c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON);
		c.status_turns = 3;
		const StatusTickResult r = tickStatus(c, 1000, 0, false);
		CHECK(r.turns == 2);
		CHECK_FALSE(r.cleared);
	}

	SUBCASE("★★ 虚弱:递减后又被加回去 ⇒ 回合数不变 ⇒ 永不解除")
	{
		c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_WEAKEN);
		c.status_turns = 1; // ← 正常递减会归零解除
		const StatusTickResult r = tickStatus(c, 1000, 0, false);
		CHECK(r.turns == 1);    // ★ 纹丝不动
		CHECK_FALSE(r.cleared); // ★★ 这才是"持续到战斗结束"的可观察形态
		CHECK(r.status == static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_WEAKEN));
	}

	SUBCASE("★★ 魔障同理,且它还在 CanMoveCheck 的 8 项里 ⇒ 指令恒被清")
	{
		c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_BARRIER);
		c.status_turns = 1;
		const StatusTickResult r = tickStatus(c, 1000, 0, false);
		CHECK(r.turns == 1);
		CHECK_FALSE(r.cleared);
		CHECK(r.command_cleared); // 魔障 ⇒ 不能行动
	}
}

TEST_CASE("状态⚠️★★:指令清空的判据取**递减之前**的状态 ⇒ 解除当回合仍不能行动")
{
	// 源码 `StatusSeq` 开头 `:5443` 先判 CanMoveCheck 再进递减循环 ⇒ 顺序即语义。
	// ★ 若把清指令挪到递减之后,「最后一回合麻痹」的角色会**多行动一次** ——
	//   而 turns / status 的断言全绿(解除结果一样)⇒ 只有本条抓得到。
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 10);
	c.vital = 2500;
	c.str = 2500;
	c.tough = 2500;
	c.dex = 2500;
	c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_PARALYSIS);
	c.status_turns = 1; // ← 本回合到期

	const StatusTickResult r = tickStatus(c, 1000, 0, false);
	CHECK(r.cleared); // 确实解除了
	CHECK(r.turns == 0);
	CHECK(r.status == static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_NONE));
	CHECK(r.command_cleared); // ★★ 但这一回合仍然不能动
}

TEST_CASE("状态★:解除的当回合不再结算伤害(源码 continue,顺序即语义)")
{
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 10);
	c.vital = 2500;
	c.str = 2500;
	c.tough = 2500;
	c.dex = 2500;
	c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON);

	SUBCASE("还剩回合 ⇒ 掉血")
	{
		c.status_turns = 2;
		const StatusTickResult r = tickStatus(c, 1000, 0, false);
		CHECK(r.hp_down == 20);
		CHECK_FALSE(r.cleared);
	}

	SUBCASE("★ 最后一回合 ⇒ 解除且**不掉血**")
	{
		c.status_turns = 1;
		const StatusTickResult r = tickStatus(c, 1000, 0, false);
		CHECK(r.cleared);
		CHECK(r.hp_down == 0); // ★ 源码在解除分支里 continue,走不到 switch
	}
}

TEST_CASE("状态⚠️★★:毒的骑宠那半边 —— 人与骑宠各算一份(漏了不会有任何报错)")
{
	// `Compute_Down` 的 `flg != -1` 那半段(battle.c:5264-5281):同一条公式,
	// 各自的四维、各自的 HP、各自的"留 1 HP"夹取。
	// ⚠️★ 漏掉骑宠那半边时:骑宠照常在场、照常分摊伤害,只是毒不掉它的血
	//    ⇒ 与 M.1 断线回收漏了宠物同族,**没有任何一处会报错**。
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 10);
	c.vital = 2500;
	c.str = 2500;
	c.tough = 2500;
	c.dex = 2500; // 主人掉 20
	c.has_ride = true;
	c.ride_vital = 1000;
	c.ride_str = 1000;
	c.ride_tough = 1000;
	c.ride_dex = 1000; // Σ=4000 ⇒ 40−20=20 ⇒ /4 = 5
	c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON);
	c.status_turns = 3;

	const StatusTickResult r = tickStatus(c, /*hp=*/1000, /*pet_hp=*/500, /*has_ride_pet=*/true);
	CHECK(r.hp_down == 20);
	CHECK(r.pet_hp_down == 5); // ★★ 两半都要算,且用的是**骑宠自己的**四维

	// 无骑宠 ⇒ 那一半恒 0。
	const StatusTickResult r2 = tickStatus(c, 1000, 0, /*has_ride_pet=*/false);
	CHECK(r2.hp_down == 20);
	CHECK(r2.pet_hp_down == 0);
}

TEST_CASE("状态★★:剧毒 —— HP≤1 或剩余回合≤1 直接死(与普通毒的「留 1 HP」相反)")
{
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 10);
	c.vital = 2500;
	c.str = 2500;
	c.tough = 2500;
	c.dex = 2500;
	c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_DEEPPOISON);

	SUBCASE("HP 充足且回合数还多 ⇒ 不死也不掉血")
	{
		c.status_turns = 5;
		const StatusTickResult r = tickStatus(c, 1000, 0, false);
		CHECK_FALSE(r.deep_poison_kill);
		CHECK(r.hp_down == 0); // ★ 剧毒不走 Compute_Down
	}

	SUBCASE("★ HP ≤ 1 ⇒ 直接死")
	{
		c.status_turns = 5;
		const StatusTickResult r = tickStatus(c, /*hp=*/1, 0, false);
		CHECK(r.deep_poison_kill);
	}

	SUBCASE("★ 递减后剩余回合 ≤ 1 ⇒ 直接死(「解不掉就毒发身亡」)")
	{
		c.status_turns = 3; // 递减后 2 ⇒ 还不死
		CHECK_FALSE(tickStatus(c, 1000, 0, false).deep_poison_kill);
		c.status_turns = 2; // 递减后 1 ⇒ 死
		CHECK(tickStatus(c, 1000, 0, false).deep_poison_kill);
	}
}

TEST_CASE("状态⚠️★★:酒醉解除有 ridepet 分支 —— 不是无条件 ×2(05 §4.4 漏了一半)")
{
	// `battle.c:5490`:骑宠在场时 `quick += 骑宠 quick`,**不是** `× 2`。
	// ★ L3 只置标志(骑宠 quick 在 field 的另一个槽),幅度由调用方按标志算。
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 10);
	c.vital = 2500;
	c.str = 2500;
	c.tough = 2500;
	c.dex = 2500;
	c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_DRUNK);
	c.status_turns = 1; // 本回合解除

	const StatusTickResult r = tickStatus(c, 1000, 0, false);
	CHECK(r.cleared);
	CHECK(r.drunk_quick_restore); // ★ 只有酒醉解除会置它

	// 其余状态解除不置。
	c.status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON);
	c.status_turns = 1;
	CHECK_FALSE(tickStatus(c, 1000, 0, false).drunk_quick_restore);
}

TEST_CASE("状态⚠️★★:施加成功落地的回合数 = 声明值 + 1(battle_event.c:2918)")
{
	// ★ 与 DR-BT15「逃跑首次即 2」同族的 +1 陷阱:照「声明值」实现会让每种状态
	//   都短一回合,而任何"中了没中"的断言都抓不到。
	CHECK(statusTurnsOnApply(kSuitPoisonTurns) == 4); // 带毒装备声明 3 ⇒ 落地 4
	CHECK(statusTurnsOnApply(0) == 1);
}

TEST_CASE("状态:施加当场清指令的只有麻痹/睡眠/石化/魔障四种(battle_event.c:2932)")
{
	// ⚠️ 它比 checkCanAct 的 8 项**窄** —— 晕眩/天罗/雷附体/集气不在此列。
	//    那不矛盾:那四种由 checkCanAct 在派发时否决,只是不在"施加当场"清指令。
	CHECK(clearsCommandOnApply(static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_PARALYSIS)));
	CHECK(clearsCommandOnApply(static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_SLEEP)));
	CHECK(clearsCommandOnApply(static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_STONE)));
	CHECK(clearsCommandOnApply(static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_BARRIER)));
	// 毒不在其中 ⇒ 中毒者本回合照常行动。
	CHECK_FALSE(clearsCommandOnApply(static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_POISON)));
	CHECK_FALSE(clearsCommandOnApply(static_cast<int>(SA::Domain::BattleStatus::BATTLE_ST_DIZZY)));
}

TEST_CASE("ResolveTurn★★:带毒装备 —— 普攻命中后附带毒(battle_event.c:2903,_SUIT_ADDPART4)")
{
	// ★★ 这是**净核里唯一的普攻附带状态来源**,与 A.3 暴击 / A.4 打飞同族:
	//    普攻链路自己的机制,不依赖宠技/职技/魔法。
	SUBCASE("★★ 默认 suit_poison == 0 ⇒ 整段不进 ⇒ **不摇 rng**(既有序列不变)")
	{
		Duel d = makeDuel();
		SA::Domain::BattleEvents ev{};
		ScriptedRandom rng({0, 10000, 10000, 0, 0, 0, 0, 0});
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
		const int baseline = rng.calls();

		// 同样的输入,只把 suit_poison 打开 ⇒ 必然多摇一次(状态命中判定)。
		Duel d2 = makeDuel();
		d2.field.at(0).mods.suit_poison = 30;
		SA::Domain::BattleEvents ev2{};
		ScriptedRandom rng2({0, 10000, 10000, 0, 0, 0, 0, 0});
		REQUIRE(resolveTurn(d2.field, d2.cmds, RulesConfig{}, rng2, ev2));
		// ★ 判据落在**取数次数**上:这正是 DR-BT23 那族唯一抓得到的形态。
		CHECK(rng2.calls() == baseline + 1);
	}

	SUBCASE("命中 ⇒ Damage.status_applied = POISON,且不另发 StatusChange")
	{
		Duel d = makeDuel();
		d.field.at(0).mods.suit_poison = 200; // per 夹到 80 ⇒ 好命中
		d.field.at(0).level = 1;
		d.field.at(10).level = 1;
		d.field.at(10).vital = 25;
		d.field.at(10).str = 25;
		d.field.at(10).tough = 25;
		d.field.at(10).dex = 25;
		SA::Domain::BattleEvents ev{};
		// 脚本:dex 抖动 → 回避(取大=不闪) → 暴击(取大=不暴) → 伤害各步 → 状态判定
		ScriptedRandom rng({0, 10000, 10000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

		bool found = false;
		for (std::size_t i = 0; i < ev.events.size(); ++i)
		{
			if (ev.events[i].body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE &&
			    ev.events[i].body.damage.status_applied ==
			        SA::Domain::BattleStatus::BATTLE_ST_POISON)
				found = true;
		}
		CHECK(found);
		// ★ 附带状态走 Damage.status_applied(IDL 为此留的字段),
		//   StatusChange 只在**解除**时发 ⇒ 本回合一条都不该有。
		CHECK(countKind(ev, SA::Domain::BattleEvent::BodyKind::STATUS_CHANGE) == 0);
	}

	SUBCASE("⚠️★ 闪避 ⇒ 整段跳过(走 continue,**够不到** damage 门)")
	{
		// ⚠️★★ **本条对 `damage > 0` 那道门没有区分力,如实记明**:闪避在
		//    `rollDodge` 之后直接 `continue`,连伤害都不算 ⇒ 把门改成 `damage >= 0`
		//    本条**照样 3/3 全绿**(2026-09-11 在**全新构建目录**下单独复核)。
		//    ⚠️ 第一次得出这个结论时用的是被**陈旧 `.o` 污染**的构建目录(make 秒级
		//      mtime 坑第三次发作),当时连「整个用例集不红」都是假象 ——
		//      ★ 结论碰巧对,过程是错的。⇒ 反向验证一律在全新目录下做。
		//    ⇒ 它验的是"闪避不附带状态 + 不摇那一次",**不是**那道门 ——
		//      门由下一条 SUBCASE 验。★ 同 I.4「playerItemSlotsUsed 在 world 层无
		//      区分力」那族:断言的形状与它宣称挡住的缺陷不是一回事。
		Duel d = makeDuel();
		d.field.at(0).mods.suit_poison = 200;
		SA::Domain::BattleEvents ev{};
		// 第 2 发给 1 ⇒ 回避判定 RAND(1,10000) <= per ⇒ 必闪。
		ScriptedRandom rng({0, 1});
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
		CHECK(rng.calls() == 3); // 基线 + 回避;★ 没有第三发 = 状态判定没摇
		for (std::size_t i = 0; i < ev.events.size(); ++i)
		{
			if (ev.events[i].body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
				CHECK(ev.events[i].body.damage.status_applied ==
				      SA::Domain::BattleStatus::BATTLE_ST_NONE);
		}
	}

	SUBCASE("★★ 命中但伤害被算成 0 ⇒ **不进状态判定、不摇**(damage > 0 那道门)")
	{
		// 源码判据 `damage > 0 && gBattleStausChange >= 0`(`:2907`)——
		// ★ 这里才是那道门真正的适用场景:**打中了,但伤害是 0**。
		//   造法:`damage_calc_percent = 0` ⇒ 第 7 步全局系数把伤害归零。
		Duel d = makeDuel();
		d.field.at(0).mods.suit_poison = 200;
		d.field.at(10).vital = 25;
		d.field.at(10).str = 25;
		d.field.at(10).tough = 25;
		d.field.at(10).dex = 25;
		RulesConfig cfg{};
		cfg.damage_calc_percent = 0;
		SA::Domain::BattleEvents ev{};
		ScriptedRandom rng({0, 10000, 10000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
		REQUIRE(resolveTurn(d.field, d.cmds, cfg, rng, ev));

		// ★ 确认前提成立:确实命中了(有 Damage 事件)且伤害是 0。
		bool hit_with_zero = false;
		for (std::size_t i = 0; i < ev.events.size(); ++i)
		{
			const auto &e = ev.events[i];
			if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE &&
			    e.body.damage.target == 10u)
			{
				CHECK(e.body.damage.hp_delta == 0);
				CHECK(e.body.damage.status_applied ==
				      SA::Domain::BattleStatus::BATTLE_ST_NONE);
				hit_with_zero = true;
			}
		}
		REQUIRE(hit_with_zero); // 前提不成立就别信下面的结论
	}
}

TEST_CASE("ResolveTurn★★:中毒后逐回合掉血 ⇒ 4 回合后解除(端到端,含 +1 落地)")
{
	// ★ 把「施加 → 每回合结算 → 解除」串起来跑 —— 这是 L4.1 的闭环证据。
	//   ⚠️ 单看施加或单看结算都可能"绿而不通"(欠债 20 那族)。
	Duel d = makeDuel();
	d.field.at(0).status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON);
	d.field.at(0).status_turns = statusTurnsOnApply(kSuitPoisonTurns); // = 4
	d.field.at(0).hp = 1000;
	d.field.at(0).vital = 2500;
	d.field.at(0).str = 2500;
	d.field.at(0).tough = 2500;
	d.field.at(0).dex = 2500; // ⇒ 每回合掉 20
	setKind(d.cmds, 0, SA::Domain::BattleCommand::CommandKind::WAIT);

	int hp = 1000;
	int turns = 4;
	int poison_events = 0;
	int clear_events = 0;
	for (int t = 0; t < 5; ++t)
	{
		d.field.at(0).hp = hp;
		d.field.at(0).status_turns = turns;
		if (turns <= 0)
			d.field.at(0).status =
			    static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_NONE);

		SA::Domain::BattleEvents ev{};
		ScriptedRandom rng({0});
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

		for (std::size_t i = 0; i < ev.events.size(); ++i)
		{
			if (ev.events[i].body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
			{
				++poison_events;
				hp += ev.events[i].body.damage.hp_delta;
			}
			if (ev.events[i].body_kind == SA::Domain::BattleEvent::BodyKind::STATUS_CHANGE)
			{
				++clear_events;
				CHECK_FALSE(ev.events[i].body.status_change.applied); // 解除
				CHECK(ev.events[i].body.status_change.status ==
				      SA::Domain::BattleStatus::BATTLE_ST_POISON);
			}
		}
		// 回合推进:第 4 回合归零解除。
		turns = (turns > 0) ? turns - 1 : 0;
	}

	// ★ 落地 4 回合 ⇒ 前 3 回合各掉 20(第 4 回合归零解除,**不掉血**)。
	CHECK(hp == 1000 - 60);
	CHECK(poison_events == 3);
	CHECK(clear_events == 1); // ★ 解除只发一次
}

TEST_CASE("ResolveTurn⚠️★★:状态推进在**每个角色行动前**逐个跑,不是回合开始统一跑")
{
	// 源码 `battle.c:7074` 在指令派发之前、且在 actor 循环**内**。
	// ★★ 挪到循环外会改变可观察行为:行动顺序靠后的单位若在本回合先被打死,
	//    就**跑不到**自己那一趟 ⇒ 不掉这一回合的毒血。
	Duel d = makeDuel(/*atk=*/100000); // 一击必杀
	d.field.at(10).status =
	    static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON);
	d.field.at(10).status_turns = 5;
	d.field.at(10).hp = 100;
	d.field.at(10).vital = 2500;
	d.field.at(10).str = 2500;
	d.field.at(10).tough = 2500;
	d.field.at(10).dex = 2500;
	// 敌人也下指令 ⇒ 它会进 actor 循环(但排在玩家之后就轮不到了)。
	setAttack(d.cmds, 10, 0);
	// 玩家 quick 高 ⇒ 先手
	d.field.at(0).quick = 1000;
	d.field.at(10).quick = 0;

	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({0, 0, 10000, 10000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	// 敌人被秒 ⇒ 它那一趟 `dead[] continue` ⇒ 本回合**没有**它的毒伤害事件。
	int enemy_poison_dmg = 0;
	for (std::size_t i = 0; i < ev.events.size(); ++i)
	{
		if (ev.events[i].body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE &&
		    ev.events[i].body.damage.target == 10u &&
		    ev.events[i].body.damage.hp_delta == -20)
			++enemy_poison_dmg;
	}
	CHECK(enemy_poison_dmg == 0); // ★★ 死者不掉这一回合的毒血
}

TEST_CASE("P1:反击固定敏捷的整数截断与类型分支")
{
	// SSRC80 CounterCalc :1413–1463；这里给手算结果，不复制被测公式。
	auto a = makeCombatant(CombatantKind::kPlayer, 1, 1, 100);
	auto b = makeCombatant(CombatantKind::kPlayer, 1, 1, 92);
	CHECK(computeCounterBase(a, b) == 10); // sqrt((100-92)/0.08)
	a.quick = 1;                           // 战斗临时敏捷不改变 WORKFIXDEX。
	CHECK(computeCounterBase(a, b) == 10);
	a.kind = CombatantKind::kPet;
	b.kind = CombatantKind::kEnemy;
	a.fix_dex = b.fix_dex = 6;
	CHECK(computeCounterBase(a, b) == 5); // int(6*0.8)=4 → sqrt(25)
	a.kind = CombatantKind::kPlayer;
	CHECK(computeCounterBase(a, b) == 6); // int(6*0.6)=3 → int(sqrt(37))
	a.kind = CombatantKind::kEnemy;
	b.kind = CombatantKind::kPet;
	a.fix_dex = 100;
	b.fix_dex = 60;
	CHECK(computeCounterBase(a, b) == 4); // /10，不开根
	a.kind = CombatantKind::kPet;
	b.kind = CombatantKind::kPlayer;
	a.fix_dex = 60;
	b.fix_dex = 100;
	CHECK(computeCounterBase(a, b) == 2); // int(4*0.6)
}

TEST_CASE("P1:反击武器表方向、未知武器与严格阈值")
{
	auto a = makeCombatant(CombatantKind::kPlayer, 1, 1, 100);
	auto b = makeCombatant(CombatantKind::kPlayer, 1, 1, 92);
	b.mods.unarmed = false;
	b.mods.weapon = WeaponClass::kAxe;
	int per = 0;
	ScriptedRandom below({699}), edge({700});
	CHECK(rollCounter(a, b, below, &per));
	CHECK(per == 7); // 拳反斧 table[1][2]=7，行是反击者。
	CHECK_FALSE(rollCounter(a, b, edge));
	a.mods.unarmed = false;
	a.mods.weapon = WeaponClass::kAxe;
	b.mods.unarmed = true;
	ScriptedRandom axe({799});
	CHECK(rollCounter(a, b, axe, &per));
	CHECK(per == 8); // 斧反拳 table[2][1]=8。
	for (const auto weapon : {WeaponClass::kOther, WeaponClass::kSpear})
	{
		a.mods.weapon = weapon;
		ScriptedRandom unknown({900});
		CHECK_FALSE(rollCounter(a, b, unknown, &per));
		CHECK(per == 9); // :3453 未映射的类型回落 NONE，无越界第 8 行。
	}
}

TEST_CASE("P1:玩家和宠敌反击概率的下限、上限及取数次数")
{
	auto a = makeCombatant(CombatantKind::kPlayer, 1, 1);
	auto b = a;
	ScriptedRandom player({1});
	CHECK_FALSE(rollCounter(a, b, player)); // 玩家 roll < 1，永不成功。
	CHECK(player.calls() == 1);
	a.kind = CombatantKind::kPet;
	ScriptedRandom pet({1}), pet_miss({2});
	CHECK(rollCounter(a, b, pet)); // 宠敌 roll <= 1，保留万分之一。
	CHECK_FALSE(rollCounter(a, b, pet_miss));
	CHECK(pet.calls() == 1);
	a.kind = CombatantKind::kPlayer;
	a.mods.counter_bonus = 101;
	ScriptedRandom player_high({10000});
	CHECK(rollCounter(a, b, player_high)); // 玩家不夹 100%，不能复用宠物口径。
	a.kind = CombatantKind::kEnemy;
	a.fix_dex = 2000;
	int per = 0;
	ScriptedRandom enemy_high({10000});
	CHECK(rollCounter(a, b, enemy_high, &per));
	CHECK(per == 100);
	for (const auto weapon : {WeaponClass::kBow, WeaponClass::kThrow})
	{
		for (int side : {0, 1})
		{
			a.mods.weapon = b.mods.weapon = WeaponClass::kNone;
			(side == 0 ? a : b).mods.weapon = weapon;
			ScriptedRandom ranged({1});
			CHECK_FALSE(rollCounter(a, b, ranged));
			CHECK(ranged.calls() == 0);
		}
	}
}

namespace
{
Duel counterDuel()
{
	auto d = makeDuel(100, 50);
	for (int slot : {0, 10})
	{
		auto &c = d.field.at(slot);
		c.kind = CombatantKind::kPlayer;
		c.attack = 100;
		c.defense = 50;
		c.hp = c.max_hp = 10000;
		c.mods.no_duck = true;
		c.mods.immune_critical = true;
		c.mods.counter_bonus = 101;
		setAttack(d.cmds, slot, 10 - slot);
	}
	return d;
}
} // namespace

TEST_CASE("P1:反击在多段普攻后发生，连锁上限五次且不附毒或推进计时")
{
	auto d = counterDuel();
	d.field.at(0).mods.unarmed = false;
	d.field.at(0).mods.attack_num_min = 3;
	d.field.at(0).mods.attack_num_max = 3;
	d.field.at(10).mods.suit_poison = 10000;
	d.field.at(10).status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_POISON);
	d.field.at(10).status_turns = 4;
	ScriptedRandom rng({10000});
	SA::Domain::BattleEvents events{};
	ActionEffects effects{};
	REQUIRE(resolveAction(d.field, d.cmds, RulesConfig{}, rng, 0, events, effects));
	REQUIRE(events.events.size() == 14); // 一个 Hit + 三段 + 五个(Hit,Damage)
	CHECK(events.events[0].body.hit.target_count == 3);
	for (std::size_t i = 1; i <= 3; ++i)
		CHECK(events.events[i].body.damage.target == 10);
	for (std::size_t link = 0; link < 5; ++link)
	{
		const auto &hit = events.events[4 + link * 2].body.hit;
		const auto &damage = events.events[5 + link * 2].body.damage;
		CHECK(hit.attacker == (link % 2 == 0 ? 10 : 0));
		CHECK(hit.target_count == 1);
		CHECK(damage.target == (link % 2 == 0 ? 0 : 10));
		CHECK((damage.flags & static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_COUNTER)) != 0);
		CHECK(damage.status_applied == BattleStatus::BATTLE_ST_NONE);
	}
	CHECK(countKind(events, SA::Domain::BattleEvent::BodyKind::STATUS_TICK) == 0);
	CHECK(rng.calls() == 22); // 三段各两次，五反各三次，固定三段抽一次。
}

TEST_CASE("P1:反击链在防御、非攻击指令、ABIO、死亡和会心处终止")
{
	for (const auto kind : {SA::Domain::BattleCommand::CommandKind::GUARD,
	                        SA::Domain::BattleCommand::CommandKind::WAIT,
	                        SA::Domain::BattleCommand::CommandKind::USE_ITEM})
	{
		auto d = counterDuel();
		setKind(d.cmds, 10, kind);
		ScriptedRandom rng({10000});
		SA::Domain::BattleEvents events{};
		ActionEffects effects{};
		REQUIRE(resolveAction(d.field, d.cmds, RulesConfig{}, rng, 0, events, effects));
		CHECK(countKind(events, SA::Domain::BattleEvent::BodyKind::HIT) == 1);
	}
	for (int mode : {0, 1, 2, 3})
	{
		auto d = counterDuel();
		if (mode == 0)
			d.field.at(10).mods.abio = true;
		if (mode == 1)
			d.field.at(10).damage_react = 1;
		if (mode == 2)
			d.field.at(10).hp = 1;
		if (mode == 3)
		{
			d.field.at(10).mods.immune_critical = false;
			d.field.at(0).mods.equip_critical = 1000;
		}
		ScriptedRandom rng({1});
		SA::Domain::BattleEvents events{};
		ActionEffects effects{};
		REQUIRE(resolveAction(d.field, d.cmds, RulesConfig{}, rng, 0, events, effects));
		CHECK(countKind(events, SA::Domain::BattleEvent::BodyKind::HIT) == 1);
	}
	SUBCASE("反击自身会心后不再被反击")
	{
		auto d = counterDuel();
		d.field.at(0).mods.immune_critical = false;
		d.field.at(10).mods.equip_critical = 1000;
		ScriptedRandom rng({1});
		SA::Domain::BattleEvents events{};
		ActionEffects effects{};
		REQUIRE(resolveAction(d.field, d.cmds, RulesConfig{}, rng, 0, events, effects));
		REQUIRE(events.events.size() == 4);
		CHECK((events.events[3].body.damage.flags & static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_CRITICAL)) != 0);
	}
	SUBCASE("反击被闪避仍允许下一次反击")
	{
		auto d = counterDuel();
		d.field.at(0).mods.no_duck = false;
		d.field.at(0).mods.always_dodge = true;
		ScriptedRandom rng({10000});
		SA::Domain::BattleEvents events{};
		ActionEffects effects{};
		REQUIRE(resolveAction(d.field, d.cmds, RulesConfig{}, rng, 0, events, effects));
		CHECK(countKind(events, SA::Domain::BattleEvent::BodyKind::HIT) == 6);
		CHECK((events.events[3].body.damage.flags & static_cast<std::uint32_t>(SA::Domain::DamageFlag::DAMAGE_FLAG_DODGE)) != 0);
	}
}

TEST_CASE("P1:到期麻痹清掉的攻击指令不能在反击时复活")
{
	auto d = counterDuel();
	d.field.at(0).quick = 1000;
	d.field.at(0).status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_PARALYSIS);
	d.field.at(0).status_turns = 1;
	ScriptedRandom action_rng({10000});
	SA::Domain::BattleEvents action{};
	ActionEffects effects{};
	REQUIRE(resolveAction(d.field, d.cmds, RulesConfig{}, action_rng, 0, action, effects));
	CHECK(effects.command_cleared);
	CHECK(countKind(action, SA::Domain::BattleEvent::BodyKind::HIT) == 0);
	ScriptedRandom turn_rng({10000});
	SA::Domain::BattleEvents turn{};
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, turn_rng, turn));
	CHECK(countKind(turn, SA::Domain::BattleEvent::BodyKind::HIT) == 1);
	for (const auto &event : turn.events)
		if (event.body_kind == SA::Domain::BattleEvent::BodyKind::HIT)
			CHECK(event.body.hit.attacker == 10);
}
