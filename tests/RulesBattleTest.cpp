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

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

using namespace SA::Rules;
using SA::Domain::BattleStatus;
using SA::Domain::CannotActReason;

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

// 可编排的随机源 —— 用来把"抽到第几档"从概率变成断言。
//
// ★ 为什么不用 SeededRandom 撞运气:分档边界(§3.5 的 25/50/70/85/95/100、
//   §3.9 的 10/30/70)必须**逐个边界值**验,而不是"跑一万次看分布像不像"。
//   分布用例挡不住"档位表抄错一格"这种最常见的移植错误。
class ScriptedRandom final : public Random
{
  public:
	explicit ScriptedRandom(std::vector<int> script) : _script(std::move(script)) {}

	int rand(int lo, int hi) override
	{
		const int v = next();
		if (v < lo)
			return lo;
		if (v > hi)
			return hi;
		return v;
	}
	int randMod(int n) override
	{
		if (n <= 0)
			return 0;
		const int v = next();
		return v % n;
	}

  private:
	int next()
	{
		if (_script.empty())
			return 0;
		// ★ 用尽后**重复最后一个值**,不回卷:回卷会让"多消费了一次随机数"这种
		//   偏差在长序列里自愈,从而掩盖 rng 消费序列的变化。
		if (_cursor >= _script.size())
			return _script.back();
		return _script[_cursor++];
	}
	std::vector<int> _script;
	std::size_t _cursor = 0;
};

// 恒取上界的随机源。★ 回避判定是 `RAND(1,10000) <= per` 而 per 硬上限 7500
//   ⇒ 取 10000 时**必不闪避**,把回避这个自由度从调度用例里摘出去。
class MaxRandom final : public Random
{
  public:
	int rand(int lo, int hi) override { return hi > lo ? hi : lo; }
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

TEST_CASE("行动顺序:排序键 = quick + 20 + sequence,且不夹下限")
{
	// `BATTLE_DexCalc` 基数 = WORKQUICK + 20(05 §2.5)。
	// quick == 0 ⇒ 抖动项 RAND(0, 0) == 0 ⇒ dex 恒等于基数,可以精确断言。
	auto c = makeCombatant(CombatantKind::kPlayer, 100, 100, /*quick=*/0);
	SA::Domain::BattleCommand cmd{};
	cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;

	SeededRandom rng(1);
	CHECK(computeActionDex(c, cmd, rng) == kDexBase);

	// 装备「先攻」直接加在排序键上。
	c.mods.sequence = 7;
	CHECK(computeActionDex(c, cmd, rng) == kDexBase + 7);

	// ⚠️★ 原版 `if (dex <= 1) dex = 1;` **是被注释掉的** ⇒ 结果可以 ≤ 1 甚至为负。
	//    这里用负 sequence 逼出该情形:若有人"顺手加个下限",这条会失败。
	c.mods.sequence = -100;
	CHECK(computeActionDex(c, cmd, rng) < 0);
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
	SeededRandom rng(12345);
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
	f.at(1).mods.sequence = 50; // 用 sequence 制造确定的速度差(quick=0 ⇒ 无抖动)
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

TEST_CASE("骑宠分摊:DR-BT2 修正 —— 无损,且防御高者多扛")
{
	// ⚠️ 原式 `player = damage·petDef/(myDef+petDef) + 1` 有两处 `+1`(总伤多 2),
	//    且 petDef 在分子 ⇒ **宠物防御越高、主人吃得越多**,反向惩罚养骑宠。
	//    DR-BT2 裁定 = 修正:分子改 myDef、去掉 +1。
	const RideSplit s = splitRideDamage(100, /*myDef=*/300, /*petDef=*/100);
	CHECK(s.player + s.pet == 100); // ① 无损(IDL Damage 的注释按此写)
	CHECK(s.player == 75);          // ② 主人防御 3 倍于宠物 ⇒ 主人扛 75%
	CHECK(s.pet == 25);

	// 反向确认:宠物防御高时宠物多扛 —— 这正是原式做不到的。
	const RideSplit s2 = splitRideDamage(100, /*myDef=*/100, /*petDef=*/300);
	CHECK(s2.player == 25);
	CHECK(s2.pet == 75);

	// 双方防御都是 0 ⇒ 原式除零。新实现全部记在主人身上,且仍然无损。
	const RideSplit s3 = splitRideDamage(37, 0, 0);
	CHECK(s3.player == 37);
	CHECK(s3.pet == 0);
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
	using K = SA::Domain::BattleCommand::CommandKind;
	for (const auto k : {K::GUARD, K::WAIT, K::PET_IN,
	                     K::PET_OUT, K::USE_ITEM, K::PET_SKILL, K::PROF_SKILL, K::SPELL})
	{
		Duel d = makeDuel();
		setKind(d.cmds, 0, k);
		SA::Domain::BattleEvents ev{};
		MaxRandom rng;
		REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
		CHECK(ev.events.size() == 0);
	}
}

TEST_CASE("ResolveTurn:守方防御 ⇒ 减伤且置 GUARD;混乱值 > 0 时不减伤")
{
	// ★ §3.5 的触发条件是**两条**:守方指令 = 防御 **且 混乱值 ≤ 0**。
	//   ⚠️ 只判指令会让"混乱中的防御"也吃到减伤 —— 这条用例就是挡它的。
	auto run = [](int confusion, std::uint32_t *flags_out)
	{
		Duel d = makeDuel(/*atk=*/100000, /*def=*/1);
		d.field.at(10).confusion = confusion;
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
	// ★ DR-BT2 修正后的方向:主人防御 300 > 宠物 100 ⇒ 主人扛得更多。
	CHECK(-dmg.hp_delta > -dmg.pet_hp_delta);
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
	ScriptedRandom srng({/*dex抖动*/ 0, /*回避*/ 9999, /*暴击*/ 1, /*伤害*/ 500, 500, 500, 500});
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
	ScriptedRandom srng({0, 9999, 5000, 500}); // 暴击抽 5000 也无所谓,per=0
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
		ScriptedRandom srng({0, 9999, 1, 500, 500, 500, 500});
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
	Duel d = makeDuel(/*atk=*/100000, /*def=*/1);
	d.field.at(0).level = 5; // lv<10 ⇒ 空手恒 1 段
	d.field.at(0).mods.unarmed = true;
	d.field.at(10).max_hp = 100; // 门槛 = 140
	d.field.at(10).hp = 100;
	d.field.at(10).ultimate_accumulator = 99; // 命中后应被清零
	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({1, 10, 1}); // dex 抖动 / 段数 / per(不暴击)
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
	Duel d = makeDuel(/*atk=*/100000, /*def=*/1);
	d.field.at(0).level = 5;
	d.field.at(0).mods.unarmed = true;
	d.field.at(10).max_hp = 100;
	d.field.at(10).hp = 100;
	d.field.at(10).mods.immune_knockback = true; // ★ 标志位,非图号
	SA::Domain::BattleEvents ev{};
	ScriptedRandom rng({1, 10, 1});
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
	ScriptedRandom rng({1, 10, 1});
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
	ScriptedRandom rng({1, 10, 1});
	REQUIRE(resolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

	CHECK(countKind(ev, SA::Domain::BattleEvent::BodyKind::KNOCKBACK_STATE) == 0u);
}
