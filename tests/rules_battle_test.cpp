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

#include "rules/battle.h"

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

using namespace sg::rules;
using sg::domain::BattleStatus;
using sg::domain::CannotActReason;

namespace {

// 造一个干净的战斗单位。★ 默认全 0 属性 ⇒ NoneElement() == 100(全无属性)。
Combatant MakeCombatant(CombatantKind kind, int atk, int def, int quick = 0) {
  Combatant c{};
  c.occupied = true;
  c.kind     = kind;
  c.hp = c.max_hp = 1000;
  c.attack   = atk;
  c.defense  = def;
  c.quick    = quick;
  return c;
}

BattleField MakeField() {
  BattleField f{};
  f.battle_id = 1;
  f.turn      = 1;
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

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  1. 相克矩阵 —— 对源码逐项核对
// ═══════════════════════════════════════════════════════════════════════════
//
// 来源:`battle_event.c:908-953 BATTLE_AttrCalc`。
// 原式按「攻方属性」分五行,每行对五个守方属性加权。本用例把那五行逐项抄成断言,
// 抄的是**源码**不是文档表格 —— constants.h 的矩阵是按 Element(地水火风)
// **重排过**的,而文档表头是「无火水地风」。★ 重排出错会得到"看起来对、算出来错"
// 的矩阵,且不会有任何外部表现。
TEST_CASE("相克矩阵:与 battle_event.c:922-949 逐项一致") {
  auto M = [](Element a, Element d) {
    return kElementMatrix[static_cast<int>(a)][static_cast<int>(d)];
  };

  // My_Fire(:922-926):Vs_None 1.5 · Vs_Fire 1.0 · Vs_Water 0.6 · Vs_Earth 1.0 · Vs_Wind 1.5
  CHECK(M(Element::kFire,  Element::kNone)  == doctest::Approx(kAjUp));
  CHECK(M(Element::kFire,  Element::kFire)  == doctest::Approx(kAjSame));
  CHECK(M(Element::kFire,  Element::kWater) == doctest::Approx(kAjDown));
  CHECK(M(Element::kFire,  Element::kEarth) == doctest::Approx(kAjSame));
  CHECK(M(Element::kFire,  Element::kWind)  == doctest::Approx(kAjUp));

  // My_Water(:927-931):Vs_None 1.5 · Vs_Fire 1.5 · Vs_Water 1.0 · Vs_Earth 0.6 · Vs_Wind 1.0
  CHECK(M(Element::kWater, Element::kNone)  == doctest::Approx(kAjUp));
  CHECK(M(Element::kWater, Element::kFire)  == doctest::Approx(kAjUp));
  CHECK(M(Element::kWater, Element::kWater) == doctest::Approx(kAjSame));
  CHECK(M(Element::kWater, Element::kEarth) == doctest::Approx(kAjDown));
  CHECK(M(Element::kWater, Element::kWind)  == doctest::Approx(kAjSame));

  // My_Earth(:933-937):Vs_None 1.5 · Vs_Fire 1.0 · Vs_Water 1.5 · Vs_Earth 1.0 · Vs_Wind 0.6
  CHECK(M(Element::kEarth, Element::kNone)  == doctest::Approx(kAjUp));
  CHECK(M(Element::kEarth, Element::kFire)  == doctest::Approx(kAjSame));
  CHECK(M(Element::kEarth, Element::kWater) == doctest::Approx(kAjUp));
  CHECK(M(Element::kEarth, Element::kEarth) == doctest::Approx(kAjSame));
  CHECK(M(Element::kEarth, Element::kWind)  == doctest::Approx(kAjDown));

  // My_Wind(:939-943):Vs_None 1.5 · Vs_Fire 0.6 · Vs_Water 1.0 · Vs_Earth 1.5 · Vs_Wind 1.0
  CHECK(M(Element::kWind,  Element::kNone)  == doctest::Approx(kAjUp));
  CHECK(M(Element::kWind,  Element::kFire)  == doctest::Approx(kAjDown));
  CHECK(M(Element::kWind,  Element::kWater) == doctest::Approx(kAjSame));
  CHECK(M(Element::kWind,  Element::kEarth) == doctest::Approx(kAjUp));
  CHECK(M(Element::kWind,  Element::kWind)  == doctest::Approx(kAjSame));

  // My_None(:945-949):Vs_None 1.0,其余四项全 0.6
  CHECK(M(Element::kNone,  Element::kNone)  == doctest::Approx(kAjSame));
  CHECK(M(Element::kNone,  Element::kFire)  == doctest::Approx(kAjDown));
  CHECK(M(Element::kNone,  Element::kWater) == doctest::Approx(kAjDown));
  CHECK(M(Element::kNone,  Element::kEarth) == doctest::Approx(kAjDown));
  CHECK(M(Element::kNone,  Element::kWind)  == doctest::Approx(kAjDown));
}

// ★ 量纲自洽(`05` §3.4):因 Σatk = Σdef = 100,全无属性时系数恰为 1.0。
//   这条是**可手算**的:100 × 100 × 1.0 / 10000 == 1。
TEST_CASE("相克:全无属性时量纲自洽,系数恰为 1.0") {
  const auto atk = MakeCombatant(CombatantKind::kPlayer, 100, 50);
  const auto def = MakeCombatant(CombatantKind::kEnemy, 100, 50);

  REQUIRE(atk.NoneElement() == kAttrMax);   // 未设四属 ⇒ 无属余量 100
  REQUIRE(def.NoneElement() == kAttrMax);
  CHECK(ElementCoefficient(atk, def) == doctest::Approx(1.0));

  // 结算路径同样应恒等(全无属性 ⇒ 无放大也无衰减)。
  const auto field = MakeField();
  CHECK(ApplyElementMatrix(field, atk, def, 100) == 100);
  CHECK(ApplyElementMatrix(field, atk, def, 1) == 1);
}

// ★ 相克的手算基准:地 100 攻 vs 水 100 守,系数 1.5(kElementMatrix[地][水])。
//   手算:at_scaled[地] = 100 × damage;Σ = (100·damage) × 100 × 1.5;
//        /10000 ⇒ damage × 1.5。
TEST_CASE("相克:地攻水守 = 1.5 倍(手算基准)") {
  auto atk = MakeCombatant(CombatantKind::kPlayer, 100, 50);
  auto def = MakeCombatant(CombatantKind::kEnemy, 100, 50);
  atk.elements[static_cast<int>(Element::kEarth)] = kAttrMax;  // 纯地
  def.elements[static_cast<int>(Element::kWater)] = kAttrMax;  // 纯水
  REQUIRE(atk.NoneElement() == 0);
  REQUIRE(def.NoneElement() == 0);

  CHECK(ElementCoefficient(atk, def) == doctest::Approx(1.5));

  const auto field = MakeField();
  CHECK(ApplyElementMatrix(field, atk, def, 100) == 150);
  CHECK(ApplyElementMatrix(field, atk, def, 200) == 300);

  // 反向:水攻地守 = 0.6(被克)。
  CHECK(ElementCoefficient(def, atk) == doctest::Approx(kAjDown));
  CHECK(ApplyElementMatrix(field, def, atk, 100) == 60);
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
TEST_CASE("三分段:无窄缝,阈值边界走第二分支而非返回 0") {
  const auto field = MakeField();
  RulesConfig cfg{};
  cfg.damage_calc_percent = 100;   // 摘掉第 7 步,单独看分段

  // 复刻源码 `:1225-1235` 的分支判定(用同样的 float 类型与表达式形状)。
  auto branch_of = [](int a, int d) -> int {
    const float attack = static_cast<float>(a);
    const float defense = static_cast<float>(d);
    if (defense <= attack && attack < (defense * 8.0 / 7.0)) return 2;
    if (defense > attack) return 1;
    if (attack >= (defense * 8 / 7)) return 3;
    return 0;   // ← 若"窄缝"存在,会命中这里
  };

  int gap = 0;
  for (int d = 1; d <= 4000; ++d) {
    const int lo = d;                    // 从 attack == defense 起
    const int hi = d * 8 / 7 + 2;        // 扫过阈值
    for (int a = lo; a <= hi; ++a) {
      if (branch_of(a, d) == 0) ++gap;
    }
  }
  CHECK(gap == 0);   // ★ 这一条就是"文档说的窄缝不存在"

  // 阈值上的那一格必须产出**第二分支**的分布 RAND(0, attack/16),而不是恒 0。
  // 取 attack = defense 恰好相等的情形:落第二分支,damage ∈ [0, attack/16]。
  auto atk = MakeCombatant(CombatantKind::kPlayer, 1000, 0);
  auto def = MakeCombatant(CombatantKind::kEnemy, 0, 0);
  // defense = DEF × 0.70;要让 defense == attack,取 DEF = attack / 0.7
  def.defense = static_cast<std::int32_t>(1000 / kDefenseCoefNewPower);

  bool saw_nonzero = false;
  for (std::uint64_t seed = 1; seed <= 64 && !saw_nonzero; ++seed) {
    SeededRandom rng(seed);
    if (ComputeDamage(field, atk, def, cfg, rng) > 0) saw_nonzero = true;
  }
  CHECK(saw_nonzero);   // 若实现里塞了"窄缝返回 0",这里会恒 0 而失败
}

// ═══════════════════════════════════════════════════════════════════════════
//  3. 伤害主公式的三条分段 —— 手算区间
// ═══════════════════════════════════════════════════════════════════════════

// 第一分支(源码顺序的第二个):`defense > attack` ⇒ damage = RAND(0,1)
// ⇒ 相克全无属性(×1)、第 7 步 ×0.70 ⇒ 最终 ∈ {0}(1×70/100 == 0,整数除法)。
// ★ 这条同时钉住第 7 步是**整数除法**:1 × 70 / 100 == 0,不是 0.7。
TEST_CASE("伤害:防高于攻 ⇒ RAND(0,1),且第 7 步整数除法把 1 压成 0") {
  const auto field = MakeField();
  const RulesConfig cfg{};   // damage_calc_percent 默认 70
  REQUIRE(cfg.damage_calc_percent == 70);   // ★ 默认 70 不是 100(`08` / §3.1 第 7 步)

  const auto atk = MakeCombatant(CombatantKind::kPlayer, 10, 0);
  const auto def = MakeCombatant(CombatantKind::kEnemy, 0, 1000);  // defense = 700 > 10

  for (std::uint64_t seed = 1; seed <= 32; ++seed) {
    SeededRandom rng(seed);
    CHECK(ComputeDamage(field, atk, def, cfg, rng) == 0);
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
TEST_CASE("伤害:攻远高于防 ⇒ 落第三分支,结果在手算区间内") {
  const auto field = MakeField();
  const RulesConfig cfg{};

  const auto atk = MakeCombatant(CombatantKind::kPlayer, 1000, 0);
  const auto def = MakeCombatant(CombatantKind::kPlayer, 0, 100);   // ★ 非敌人

  for (std::uint64_t seed = 1; seed <= 256; ++seed) {
    SeededRandom rng(seed);
    const auto dmg = ComputeDamage(field, atk, def, cfg, rng);
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
TEST_CASE("伤害:守方是敌人 ⇒ 防御上浮(_NPCENEMY_ADDPOWER 生效)") {
  const auto field = MakeField();
  const RulesConfig cfg{};
  const auto atk = MakeCombatant(CombatantKind::kPlayer, 1000, 0);

  const auto def_player = MakeCombatant(CombatantKind::kPlayer, 0, 100);
  const auto def_enemy  = MakeCombatant(CombatantKind::kEnemy,  0, 100);

  // defense 上浮 ⇒ (attack − defense) 变小 ⇒ 伤害下界被拉低。
  // 手算下界:defense 最高 = 70 + (70×9 + 2)/100 = 76.32
  //          damage = (1000 − 76.32)×2 − 62.5 = 1784.86 → 截断 1784
  //                 → 1784 × 70 / 100 = 1248
  int below_player_floor = 0;
  for (std::uint64_t seed = 1; seed <= 512; ++seed) {
    SeededRandom rng(seed);
    const auto dmg = ComputeDamage(field, atk, def_enemy, cfg, rng);
    CHECK(dmg >= 1248);          // ★ defense 最高 76.32 时的下界(含两次截断)
    CHECK(dmg <= 1345);
    if (dmg < 1257) ++below_player_floor;   // 跌破玩家守方的下界
  }
  // 至少有一部分样本跌破玩家基线 ⇒ 证明修正确实在动,而不是恒等。
  CHECK(below_player_floor > 0);

  // 同种子下,敌人守方的伤害不应高于玩家守方(防御只增不减)。
  // ⚠️ 不能逐样本比 —— 两者消耗的随机数个数不同(敌人多一次 RandMod),
  //    序列会错开。⇒ 比**均值**。
  auto mean = [&](const Combatant& d) {
    long long sum = 0;
    for (std::uint64_t seed = 1; seed <= 512; ++seed) {
      SeededRandom rng(seed);
      sum += ComputeDamage(field, atk, d, cfg, rng);
    }
    return static_cast<double>(sum) / 512;
  };
  CHECK(mean(def_enemy) < mean(def_player));
}

// ★ 第 7 步的系数必须是配置项且默认 70 —— 写成 100 会让全局伤害偏高 43%。
TEST_CASE("伤害:damage_calc_percent 生效且默认 70(偏高 43% 的经典错误)") {
  const auto field = MakeField();
  const auto atk = MakeCombatant(CombatantKind::kPlayer, 1000, 0);
  const auto def = MakeCombatant(CombatantKind::kPlayer, 0, 100);

  RulesConfig c70{};
  RulesConfig c100{};
  c100.damage_calc_percent = 100;

  SeededRandom r1(12345), r2(12345);
  const auto d70  = ComputeDamage(field, atk, def, c70, r1);
  const auto d100 = ComputeDamage(field, atk, def, c100, r2);

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
TEST_CASE("可回放:同种子 + 同输入 ⇒ 逐位相同") {
  const auto field = MakeField();
  const RulesConfig cfg{};
  const auto atk = MakeCombatant(CombatantKind::kPlayer, 800, 0, 120);
  const auto def = MakeCombatant(CombatantKind::kEnemy, 0, 150, 90);

  for (std::uint64_t seed : {1ull, 7ull, 4242ull, 0xDEADBEEFull}) {
    SeededRandom a(seed), b(seed);
    for (int i = 0; i < 50; ++i) {
      CHECK(ComputeDamage(field, atk, def, cfg, a) ==
            ComputeDamage(field, atk, def, cfg, b));
    }
  }

  // 不同种子应当给出不同序列(否则"注入随机源"名存实亡)。
  SeededRandom s1(1), s2(2);
  bool differs = false;
  for (int i = 0; i < 50 && !differs; ++i) {
    if (ComputeDamage(field, atk, def, cfg, s1) !=
        ComputeDamage(field, atk, def, cfg, s2)) differs = true;
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
TEST_CASE("CheckCanAct:8 项否决逐条覆盖(DR-BT5)") {
  auto c = MakeCombatant(CombatantKind::kPlayer, 100, 100);

  CHECK(CheckCanAct(c) == CannotActReason::CANNOT_ACT_NONE);

  struct Case { BattleStatus st; CannotActReason want; const char* name; };
  const Case cases[] = {
      {BattleStatus::BATTLE_ST_PARALYSIS, CannotActReason::CANNOT_ACT_PARALYSIS, "麻痹"},
      {BattleStatus::BATTLE_ST_STONE,     CannotActReason::CANNOT_ACT_STONE,     "石化"},
      {BattleStatus::BATTLE_ST_SLEEP,     CannotActReason::CANNOT_ACT_SLEEP,     "睡眠"},
      {BattleStatus::BATTLE_ST_DIZZY,     CannotActReason::CANNOT_ACT_DIZZY,     "晕眩"},
      {BattleStatus::BATTLE_ST_DRAGNET,   CannotActReason::CANNOT_ACT_DRAGNET,   "天罗地网"},
      // ★ 以下三项是 DR-BT5 修正的核心:原版**上行不拒绝、结算否决**
      {BattleStatus::BATTLE_ST_BARRIER,   CannotActReason::CANNOT_ACT_BARRIER,   "魔障"},
      {BattleStatus::BATTLE_ST_T_ENCLOSE, CannotActReason::CANNOT_ACT_T_ENCLOSE, "雷附体"},
  };
  for (const auto& k : cases) {
    CAPTURE(k.name);
    c.status = static_cast<std::uint8_t>(k.st);
    CHECK(CheckCanAct(c) == k.want);
  }

  // 第 8 项:世界末日集气 —— ★ 独立字段,不是 status 槽值(见 combatant.h)。
  c.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_NONE);
  c.charging_turns = 3;
  CHECK(CheckCanAct(c) == CannotActReason::CANNOT_ACT_CHARGING);

  // ⚠️ 与状态并存时,按源码判定顺序**状态优先**(集气在最后一项)。
  c.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_PARALYSIS);
  CHECK(CheckCanAct(c) == CannotActReason::CANNOT_ACT_PARALYSIS);
}

// ⚠️ 不在 8 项里的状态**不得**否决行动 —— 这条挡的是"顺手多加一项"。
// 例:毒 / 混乱 / 沉默 / 遗忘都不影响能否行动。
TEST_CASE("CheckCanAct:8 项之外的状态不否决行动") {
  auto c = MakeCombatant(CombatantKind::kPlayer, 100, 100);
  for (auto st : {BattleStatus::BATTLE_ST_POISON, BattleStatus::BATTLE_ST_CONFUSION,
                  BattleStatus::BATTLE_ST_NOCAST, BattleStatus::BATTLE_ST_OBLIVION,
                  BattleStatus::BATTLE_ST_DRUNK,  BattleStatus::BATTLE_ST_WEAKEN}) {
    c.status = static_cast<std::uint8_t>(st);
    CHECK(CheckCanAct(c) == CannotActReason::CANNOT_ACT_NONE);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  6. 回避 —— 七道前置(★ 比 05 §3.2 的清单多一道)
// ═══════════════════════════════════════════════════════════════════════════
//
// 来源:`battle_event.c:754 BATTLE_DuckCheck`。
// ⚠️ `05` §3.2 写"六道前置否决",实测源码是 **6 道否决 + 1 道必闪**,
//    且清单**漏了 `CHAR_BATTLEFLG_ABIO`**(`:797`)。
TEST_CASE("回避:七道前置逐条覆盖(★ 含 05 §3.2 漏记的 ABIO)") {
  const RulesConfig cfg{};
  // 造一个回避率会很高的守方(dex 差极大),这样"没被否决"时几乎必闪 ——
  // 否则无法区分"被否决"与"没闪中"。
  auto atk = MakeCombatant(CombatantKind::kPlayer, 100, 100, 1);
  auto def = MakeCombatant(CombatantKind::kEnemy, 100, 100, 100000);

  // 基线:七道都不命中 ⇒ 应当闪掉(高 dex 差 ⇒ per 打到 75% 上限)。
  {
    int dodged = 0;
    for (std::uint64_t s = 1; s <= 200; ++s) {
      SeededRandom rng(s);
      if (RollDodge(atk, def, /*guarding=*/false, /*casting=*/false, cfg, rng)) ++dodged;
    }
    CHECK(dodged > 100);   // 75% 上限 ⇒ 200 次里应远多于 100
  }

  auto never_dodges = [&](const Combatant& a, const Combatant& d,
                          bool guarding, bool casting) {
    for (std::uint64_t s = 1; s <= 100; ++s) {
      SeededRandom rng(s);
      if (RollDodge(a, d, guarding, casting, cfg, rng)) return false;
    }
    return true;
  };

  SUBCASE("① 攻方集气完成 ⇒ 不可回避") {
    auto a = atk; a.charge_ready = true;
    CHECK(never_dodges(a, def, false, false));
  }
  SUBCASE("② 守方防御 ⇒ 不可回避") {
    CHECK(never_dodges(atk, def, /*guarding=*/true, false));
  }
  SUBCASE("③ 守方有反应类状态 ⇒ 不可回避") {
    auto d = def; d.damage_react = 1;
    CHECK(never_dodges(atk, d, false, false));
  }
  SUBCASE("④ 守方不能行动 ⇒ 不可回避") {
    auto d = def; d.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_SLEEP);
    CHECK(never_dodges(atk, d, false, false));
  }
  SUBCASE("⑤ NODUCK ⇒ 不可回避") {
    auto d = def; d.mods.no_duck = true;
    CHECK(never_dodges(atk, d, false, false));
  }
  SUBCASE("⑥ ★ ABIO ⇒ 不可回避(05 §3.2 漏记的一道)") {
    auto d = def; d.mods.abio = true;
    CHECK(never_dodges(atk, d, false, false));
  }
  SUBCASE("⑦ 必闪技 ⇒ 恒回避,且优先于概率") {
    // 用一个 dex 差为 0 的守方 —— 正常情况下 per 会被压到下限,几乎闪不掉。
    auto d = MakeCombatant(CombatantKind::kEnemy, 100, 100, 1);
    d.mods.always_dodge = true;
    for (std::uint64_t s = 1; s <= 100; ++s) {
      SeededRandom rng(s);
      CHECK(RollDodge(atk, d, false, false, cfg, rng));
    }
  }
}

// ★ `_PROFESSION_ADDSKILL`(8.0 开)的例外:**集气中仍可闪避**,
//   除非同时处于天罗地网或晕眩(`battle_event.c:779-788`)。
//   ⚠️ 这条与第 ④ 道否决直接冲突,是原版有意留的口子 —— 照抄。
TEST_CASE("回避:集气中仍可闪(_PROFESSION_ADDSKILL 的例外),但天罗/晕眩时不行") {
  const RulesConfig cfg{};
  const auto atk = MakeCombatant(CombatantKind::kPlayer, 100, 100, 1);

  auto charging = MakeCombatant(CombatantKind::kEnemy, 100, 100, 100000);
  charging.charging_turns = 3;   // ⇒ CheckCanAct 判 CANNOT_ACT_CHARGING
  REQUIRE(CheckCanAct(charging) == CannotActReason::CANNOT_ACT_CHARGING);

  int dodged = 0;
  for (std::uint64_t s = 1; s <= 200; ++s) {
    SeededRandom rng(s);
    if (RollDodge(atk, charging, false, false, cfg, rng)) ++dodged;
  }
  CHECK(dodged > 100);   // ★ 集气中照样能闪

  // 但天罗地网 / 晕眩会把这个口子关上。
  for (auto st : {BattleStatus::BATTLE_ST_DRAGNET, BattleStatus::BATTLE_ST_DIZZY}) {
    auto pinned = charging;
    pinned.status = static_cast<std::uint8_t>(st);
    for (std::uint64_t s = 1; s <= 100; ++s) {
      SeededRandom rng(s);
      CHECK_FALSE(RollDodge(atk, pinned, false, false, cfg, rng));
    }
  }
}

// ★ 咒术时更易被闪:gKawashiPara 0.027 vs 0.02 ⇒ 分母大 ⇒ Work 小 ⇒ per 小。
//   ⚠️ 注意方向:参数变大**降低**回避率。这与"更易被闪"是同一件事
//     (守方在念咒 ⇒ 守方**自己**闪避率下降)。
TEST_CASE("回避:守方咒术时回避率更低(0.027 vs 0.02)") {
  const RulesConfig cfg{};
  const auto atk = MakeCombatant(CombatantKind::kPlayer, 100, 100, 1);
  // dex 差要小到不会撞上 75% 上限,否则两档都被钳平、看不出差别。
  const auto def = MakeCombatant(CombatantKind::kEnemy, 100, 100, 30);

  int normal = 0, casting = 0;
  for (std::uint64_t s = 1; s <= 2000; ++s) {
    SeededRandom r1(s), r2(s);
    if (RollDodge(atk, def, false, /*casting=*/false, cfg, r1)) ++normal;
    if (RollDodge(atk, def, false, /*casting=*/true,  cfg, r2)) ++casting;
  }
  CHECK(casting < normal);
}

// 回避率的硬上限 75%(`KAWASHI_MAX_RATE`,`battle_event.c:871`)。
TEST_CASE("回避:硬上限 75%,极端 dex 差也不会必闪") {
  const RulesConfig cfg{};
  const auto atk = MakeCombatant(CombatantKind::kPlayer, 100, 100, 0);
  const auto def = MakeCombatant(CombatantKind::kEnemy, 100, 100, 1000000000);

  int dodged = 0;
  const int trials = 4000;
  for (std::uint64_t s = 1; s <= static_cast<std::uint64_t>(trials); ++s) {
    SeededRandom rng(s);
    if (RollDodge(atk, def, false, false, cfg, rng)) ++dodged;
  }
  const double rate = static_cast<double>(dodged) / trials;
  CHECK(rate > 0.70);
  CHECK(rate < 0.80);   // ★ 上限 75% ⇒ 不可能接近 1.0
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

namespace {

// 可编排的随机源 —— 用来把"抽到第几档"从概率变成断言。
//
// ★ 为什么不用 SeededRandom 撞运气:分档边界(§3.5 的 25/50/70/85/95/100、
//   §3.9 的 10/30/70)必须**逐个边界值**验,而不是"跑一万次看分布像不像"。
//   分布用例挡不住"档位表抄错一格"这种最常见的移植错误。
class ScriptedRandom final : public IRandom {
 public:
  explicit ScriptedRandom(std::vector<int> script) : script_(std::move(script)) {}

  int Rand(int lo, int hi) override {
    const int v = Next();
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
  }
  int RandMod(int n) override {
    if (n <= 0) return 0;
    const int v = Next();
    return v % n;
  }

 private:
  int Next() {
    if (script_.empty()) return 0;
    // ★ 用尽后**重复最后一个值**,不回卷:回卷会让"多消费了一次随机数"这种
    //   偏差在长序列里自愈,从而掩盖 rng 消费序列的变化。
    if (cursor_ >= script_.size()) return script_.back();
    return script_[cursor_++];
  }
  std::vector<int> script_;
  std::size_t cursor_ = 0;
};

// 恒取上界的随机源。★ 回避判定是 `RAND(1,10000) <= per` 而 per 硬上限 7500
//   ⇒ 取 10000 时**必不闪避**,把回避这个自由度从调度用例里摘出去。
class MaxRandom final : public IRandom {
 public:
  int Rand(int lo, int hi) override { return hi > lo ? hi : lo; }
  int RandMod(int n) override { return n > 0 ? n - 1 : 0; }
};

TurnCommands NoCommands() { return TurnCommands{}; }

void SetAttack(TurnCommands& tc, int slot, int target) {
  tc.present[slot] = true;
  tc.commands[slot] = sg::domain::BattleCommand{};
  tc.commands[slot].command_kind = sg::domain::BattleCommand::CommandKind::ATTACK;
  tc.commands[slot].command.attack.target = static_cast<std::uint32_t>(target);
}

void SetKind(TurnCommands& tc, int slot,
             sg::domain::BattleCommand::CommandKind kind) {
  tc.present[slot] = true;
  tc.commands[slot] = sg::domain::BattleCommand{};
  tc.commands[slot].command_kind = kind;
}

std::size_t CountKind(const sg::domain::BattleEvents& ev,
                      sg::domain::BattleEvent::BodyKind kind) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < ev.events.size(); ++i)
    if (ev.events[i].body_kind == kind) ++n;
  return n;
}

}  // namespace

// ── 行动顺序 ───────────────────────────────────────────────────────────────

TEST_CASE("行动顺序:排序键 = quick + 20 + sequence,且不夹下限") {
  // `BATTLE_DexCalc` 基数 = WORKQUICK + 20(05 §2.5)。
  // quick == 0 ⇒ 抖动项 RAND(0, 0) == 0 ⇒ dex 恒等于基数,可以精确断言。
  auto c = MakeCombatant(CombatantKind::kPlayer, 100, 100, /*quick=*/0);
  sg::domain::BattleCommand cmd{};
  cmd.command_kind = sg::domain::BattleCommand::CommandKind::ATTACK;

  SeededRandom rng(1);
  CHECK(ComputeActionDex(c, cmd, rng) == kDexBase);

  // 装备「先攻」直接加在排序键上。
  c.mods.sequence = 7;
  CHECK(ComputeActionDex(c, cmd, rng) == kDexBase + 7);

  // ⚠️★ 原版 `if (dex <= 1) dex = 1;` **是被注释掉的** ⇒ 结果可以 ≤ 1 甚至为负。
  //    这里用负 sequence 逼出该情形:若有人"顺手加个下限",这条会失败。
  c.mods.sequence = -100;
  CHECK(ComputeActionDex(c, cmd, rng) < 0);
}

TEST_CASE("行动顺序:同速按入场位次(DR-BT8)") {
  // ★★ 这是 DR-BT8 的回归用例。原版 `EsCmp` 是布尔比较器(小于与等于都返回 0),
  //    不满足严格弱序 ⇒ 同速顺序是标准库未定义行为(00 §10.2 六项之一)。
  //    DR-BT8 裁定「按入场位次」⇒ 全员同速时,顺序必须恰好是槽号升序。
  //    ⚠️ 若把实现里的插入排序换成 std::sort,这条就会在某些标准库上失败。
  BattleField f = MakeField();
  TurnCommands tc = NoCommands();
  for (int i = 0; i < kSlotCount; ++i) {
    f.at(i) = MakeCombatant(CombatantKind::kPlayer, 100, 100, /*quick=*/0);
    f.at(i).slot = static_cast<std::uint8_t>(i);
    SetKind(tc, i, sg::domain::BattleCommand::CommandKind::WAIT);
  }

  std::uint8_t order[kSlotCount] = {};
  SeededRandom rng(12345);
  const int n = BuildActionOrder(f, tc, rng, order);
  REQUIRE(n == kSlotCount);
  for (int i = 0; i < kSlotCount; ++i) CHECK(order[i] == i);
}

TEST_CASE("行动顺序:快的先动;无指令 / 已死 / 空槽不入列") {
  BattleField f = MakeField();
  TurnCommands tc = NoCommands();

  for (int i = 0; i < 4; ++i) {
    f.at(i) = MakeCombatant(CombatantKind::kPlayer, 100, 100, /*quick=*/0);
    f.at(i).slot = static_cast<std::uint8_t>(i);
  }
  f.at(1).mods.sequence = 50;   // 用 sequence 制造确定的速度差(quick=0 ⇒ 无抖动)
  f.at(2).dead = true;          // 已死不入列
  // slot 3 有单位但**不给指令** ⇒ 不入列(05 §2.2 第 1 步:敌方由 AI 填齐)
  SetKind(tc, 0, sg::domain::BattleCommand::CommandKind::WAIT);
  SetKind(tc, 1, sg::domain::BattleCommand::CommandKind::WAIT);
  SetKind(tc, 2, sg::domain::BattleCommand::CommandKind::WAIT);

  std::uint8_t order[kSlotCount] = {};
  SeededRandom rng(7);
  const int n = BuildActionOrder(f, tc, rng, order);
  REQUIRE(n == 2);
  CHECK(order[0] == 1);   // sequence +50 ⇒ 先动
  CHECK(order[1] == 0);
}

// ── 攻击次数(§3.9 / DR-BT1)──────────────────────────────────────────────

TEST_CASE("攻击次数:有武器走 RAND(min,max),≤0 则 1") {
  const RulesConfig cfg{};
  auto c = MakeCombatant(CombatantKind::kPlayer, 100, 100);
  c.level = 50;
  c.mods.unarmed = false;
  c.mods.attack_num_min = 2;
  c.mods.attack_num_max = 2;

  SeededRandom rng(1);
  CHECK(RollAttackCount(c, cfg, rng) == 2);

  // 武器数据坏成 0 / 负数时兜底 1 段(原版 `if (n <= 0) n = 1;`)。
  c.mods.attack_num_min = 0;
  c.mods.attack_num_max = 0;
  CHECK(RollAttackCount(c, cfg, rng) == 1);
}

TEST_CASE("攻击次数:空手的两道前置 —— 等级 ≥ 10 且是玩家") {
  const RulesConfig cfg{};

  // ★ 若漏掉这两道,**所有敌人都可能触发 10 连击**。
  auto low_level = MakeCombatant(CombatantKind::kPlayer, 100, 100);
  low_level.level = kUnarmedMultihitMinLevel - 1;
  low_level.luck  = 25;
  auto enemy = MakeCombatant(CombatantKind::kEnemy, 100, 100);
  enemy.level = 99;
  enemy.luck  = 25;

  // 脚本首值 1 ⇒ 落在最优档;若前置失效就会返回多段。
  ScriptedRandom r1({1, 10});
  ScriptedRandom r2({1, 10});
  CHECK(RollAttackCount(low_level, cfg, r1) == 1);
  CHECK(RollAttackCount(enemy, cfg, r2) == 1);
}

TEST_CASE("攻击次数:空手四档逐个边界(DR-BT1 照抄)") {
  const RulesConfig cfg{};
  auto c = MakeCombatant(CombatantKind::kPlayer, 100, 100);
  c.level = kUnarmedMultihitMinLevel;
  c.luck  = 0;          // ⇒ luckwork = 0,阈值恰为 10 / 30 / 70

  auto count_for = [&](int roll, int burst) {
    ScriptedRandom rng({roll, burst});
    return RollAttackCount(c, cfg, rng);
  };

  CHECK(count_for(10, 7) == 7);    // ≤ 10 ⇒ RAND(5,10);脚本给 7
  CHECK(count_for(11, 0) == 3);    // ≤ 30 ⇒ 3
  CHECK(count_for(30, 0) == 3);
  CHECK(count_for(31, 0) == 2);    // ≤ 70 ⇒ 2
  CHECK(count_for(70, 0) == 2);
  CHECK(count_for(71, 0) == 1);    // 否则 1

  // ★ RAND(5,10) 的两端被夹住 ⇒ 段数上限确实是 10(「空手连击可达 10 段」)。
  CHECK(count_for(1, 99) == kUnarmedBurstMax);
  CHECK(count_for(1, -99) == kUnarmedBurstMin);

  // luckwork = LUCK × 5 上限 25 ⇒ luck 25 时首档阈值 = 10 + 25 = 35。
  c.luck = 25;
  CHECK(count_for(35, 6) == 6);
  CHECK(count_for(36, 0) == 3);   // 越过首档,落到「≤ 30 + 25 = 55」这一档
}

// ── 防御减伤六档(§3.5)──────────────────────────────────────────────────

TEST_CASE("防御减伤:六档逐个边界,期望系数 0.175(★ 文档的 0.155 算错了)") {
  // 表在 constants.h(kGuardTiers)。★ 逐边界断言,挡"抄错一格"。
  auto factor_at = [](int roll) {
    ScriptedRandom rng({roll});
    return RollGuardFactor(rng);
  };
  CHECK(factor_at(1)   == doctest::Approx(0.00));
  CHECK(factor_at(25)  == doctest::Approx(0.00));   // ★ 25% 概率完全免伤
  CHECK(factor_at(26)  == doctest::Approx(0.10));
  CHECK(factor_at(50)  == doctest::Approx(0.10));
  CHECK(factor_at(51)  == doctest::Approx(0.20));
  CHECK(factor_at(70)  == doctest::Approx(0.20));
  CHECK(factor_at(71)  == doctest::Approx(0.30));
  CHECK(factor_at(85)  == doctest::Approx(0.30));
  CHECK(factor_at(86)  == doctest::Approx(0.40));
  CHECK(factor_at(95)  == doctest::Approx(0.40));
  CHECK(factor_at(96)  == doctest::Approx(0.50));
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
                           15 * 0.30 + 10 * 0.40 + 5 * 0.50) / 100.0;
  CHECK(expected == doctest::Approx(0.175));
}

// ── 骑宠分摊(§3.6 / DR-BT2)─────────────────────────────────────────────

TEST_CASE("骑宠分摊:DR-BT2 修正 —— 无损,且防御高者多扛") {
  // ⚠️ 原式 `player = damage·petDef/(myDef+petDef) + 1` 有两处 `+1`(总伤多 2),
  //    且 petDef 在分子 ⇒ **宠物防御越高、主人吃得越多**,反向惩罚养骑宠。
  //    DR-BT2 裁定 = 修正:分子改 myDef、去掉 +1。
  const RideSplit s = SplitRideDamage(100, /*myDef=*/300, /*petDef=*/100);
  CHECK(s.player + s.pet == 100);   // ① 无损(IDL Damage 的注释按此写)
  CHECK(s.player == 75);            // ② 主人防御 3 倍于宠物 ⇒ 主人扛 75%
  CHECK(s.pet == 25);

  // 反向确认:宠物防御高时宠物多扛 —— 这正是原式做不到的。
  const RideSplit s2 = SplitRideDamage(100, /*myDef=*/100, /*petDef=*/300);
  CHECK(s2.player == 25);
  CHECK(s2.pet == 75);

  // 双方防御都是 0 ⇒ 原式除零。新实现全部记在主人身上,且仍然无损。
  const RideSplit s3 = SplitRideDamage(37, 0, 0);
  CHECK(s3.player == 37);
  CHECK(s3.pet == 0);
}

// ── ResolveTurn ───────────────────────────────────────────────────────────

namespace {

// 一场 1v1:slot 0 是玩家(空手、等级 1 ⇒ 恒 1 段),slot 10 是敌人。
struct Duel {
  BattleField  field = MakeField();
  TurnCommands cmds  = NoCommands();
};

Duel MakeDuel(int atk = 1000, int def = 10) {
  Duel d;
  d.field.at(0) = MakeCombatant(CombatantKind::kPlayer, atk, 100);
  d.field.at(0).slot = 0;
  d.field.at(10) = MakeCombatant(CombatantKind::kEnemy, 100, def);
  d.field.at(10).slot = 10;
  SetAttack(d.cmds, 0, 10);
  return d;
}

}  // namespace

TEST_CASE("ResolveTurn:一次普攻 ⇒ Hit + Damage,且 target_count 与 Damage 数一致") {
  Duel d = MakeDuel();
  sg::domain::BattleEvents ev{};
  MaxRandom rng;

  REQUIRE(ResolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
  CHECK(ev.battle_id == d.field.battle_id);
  CHECK(ev.turn == d.field.turn);

  REQUIRE(ev.events.size() == 2);
  REQUIRE(ev.events[0].body_kind == sg::domain::BattleEvent::BodyKind::HIT);
  const sg::domain::Hit& hit = ev.events[0].body.hit;
  CHECK(hit.attacker == 0u);
  CHECK(hit.kind == sg::domain::AttackKind::ATTACK_KIND_MELEE);
  CHECK(hit.skill_id == 0u);
  // ★ 变长目标列表的新形状(IDL 注释):Hit 声明 target_count,其后紧跟同样多个 Damage。
  //   ⚠️ 这条关系一旦对不上,客户端就会把下一个 Hit 当成本次的目标读进来。
  CHECK(hit.target_count == 1u);
  CHECK(CountKind(ev, sg::domain::BattleEvent::BodyKind::DAMAGE) == hit.target_count);

  REQUIRE(ev.events[1].body_kind == sg::domain::BattleEvent::BodyKind::DAMAGE);
  const sg::domain::Damage& dmg = ev.events[1].body.damage;
  CHECK(dmg.target == 10u);
  CHECK(dmg.hp_delta < 0);
  CHECK((dmg.flags &
         static_cast<std::uint32_t>(sg::domain::DamageFlag::DAMAGE_FLAG_NORMAL)) != 0u);
}

TEST_CASE("ResolveTurn:L3 不写世界状态 —— field 逐字节不变") {
  // ★★ 这是四步改造第②步的**机械验证**:原版 `BATTLE_DamageSub` 直接
  //    `CHAR_setInt(HP)`,新实现只产事件、由调用方应用。
  //    (`field` 已是 const 引用,类型系统本就挡住了写;本用例挡的是
  //     "将来有人把 const 去掉"这种回归。)
  Duel d = MakeDuel();
  const BattleField before = d.field;
  sg::domain::BattleEvents ev{};
  SeededRandom rng(99);
  ResolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev);
  CHECK(std::memcmp(&before, &d.field, sizeof(BattleField)) == 0);
}

TEST_CASE("ResolveTurn:回避产事件而不是被跳过(DODGE 标志)") {
  // ⚠️ 闪避**必须**产事件:事件流是演出脚本,少一条客户端就少一个动作 ——
  //    而 1.4 的验收口径正是「事件流端到端一致」(客户端 01 §12.1)。
  Duel d = MakeDuel();
  d.field.at(10).mods.always_dodge = true;   // ⑦ 必闪(_PETSKILL_SETDUCK,8.0 开)

  sg::domain::BattleEvents ev{};
  SeededRandom rng(5);
  REQUIRE(ResolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

  REQUIRE(ev.events.size() == 2);
  const sg::domain::Damage& dmg = ev.events[1].body.damage;
  CHECK((dmg.flags &
         static_cast<std::uint32_t>(sg::domain::DamageFlag::DAMAGE_FLAG_DODGE)) != 0u);
  CHECK(dmg.hp_delta == 0);
  CHECK(ev.events[0].body.hit.target_count == 1u);
}

TEST_CASE("ResolveTurn:致死置 DEATH,且同回合剩余段数作废") {
  Duel d = MakeDuel(/*atk=*/100000, /*def=*/1);
  d.field.at(0).level = kUnarmedMultihitMinLevel;
  d.field.at(10).hp = 1;

  // 脚本:段数档(1 ⇒ 首档)· 段数(10)· 其后一律取上界。
  ScriptedRandom rng({1, 10, 10000});
  sg::domain::BattleEvents ev{};
  REQUIRE(ResolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));

  // ★ 10 段只打出 1 条 Damage ⇒ 剩余 9 段作废(原版同样逐段查存活)。
  CHECK(CountKind(ev, sg::domain::BattleEvent::BodyKind::DAMAGE) == 1);
  CHECK(ev.events[0].body.hit.target_count == 1u);
  CHECK((ev.events[1].body.damage.flags &
         static_cast<std::uint32_t>(sg::domain::DamageFlag::DAMAGE_FLAG_DEATH)) != 0u);
}

TEST_CASE("ResolveTurn:不可行动者不产事件(DR-BT5)") {
  // ★ 原因在**指令阶段**由 BattleSelfInfo.cannot_act 下发(DR-CP7 置灰),
  //   不是结算完再告诉玩家"你刚才动不了"(DR-CP6 反对的假交互)。
  for (const auto st : {BattleStatus::BATTLE_ST_PARALYSIS,
                        BattleStatus::BATTLE_ST_STONE,
                        BattleStatus::BATTLE_ST_SLEEP}) {
    Duel d = MakeDuel();
    d.field.at(0).status = static_cast<std::uint8_t>(st);
    sg::domain::BattleEvents ev{};
    MaxRandom rng;
    REQUIRE(ResolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
    CHECK(ev.events.size() == 0);
  }
}

TEST_CASE("ResolveTurn:批次 0.5 未接入的指令一律跳过,不产事件") {
  // ⚠️ 本用例把**覆盖边界**钉住:逃跑 / 捕获 / 道具 / 换宠 / 宠技 / 职技 / 咒术
  //    在批次 0.5 里必须是"什么都不发生",而不是"发生了一半"。
  //    ⇒ 接入任一指令时本用例会失败,那正是提醒去更新 battle.h 的覆盖边界表。
  using K = sg::domain::BattleCommand::CommandKind;
  for (const auto k : {K::GUARD, K::WAIT, K::ESCAPE, K::CAPTURE, K::PET_IN,
                       K::PET_OUT, K::USE_ITEM, K::PET_SKILL, K::PROF_SKILL, K::SPELL}) {
    Duel d = MakeDuel();
    SetKind(d.cmds, 0, k);
    sg::domain::BattleEvents ev{};
    MaxRandom rng;
    REQUIRE(ResolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
    CHECK(ev.events.size() == 0);
  }
}

TEST_CASE("ResolveTurn:守方防御 ⇒ 减伤且置 GUARD;混乱值 > 0 时不减伤") {
  // ★ §3.5 的触发条件是**两条**:守方指令 = 防御 **且 混乱值 ≤ 0**。
  //   ⚠️ 只判指令会让"混乱中的防御"也吃到减伤 —— 这条用例就是挡它的。
  auto run = [](int confusion, std::uint32_t* flags_out) {
    Duel d = MakeDuel(/*atk=*/100000, /*def=*/1);
    d.field.at(10).confusion = confusion;
    d.field.at(10).hp = d.field.at(10).max_hp = 100000000;
    SetKind(d.cmds, 10, sg::domain::BattleCommand::CommandKind::GUARD);
    sg::domain::BattleEvents ev{};
    MaxRandom rng;   // 防御减伤抽到 RAND(1,100) == 100 ⇒ 系数 0.50(最弱一档)
    ResolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev);
    REQUIRE(ev.events.size() >= 2);
    *flags_out = ev.events[1].body.damage.flags;
    return ev.events[1].body.damage.hp_delta;
  };

  std::uint32_t guard_flags = 0, confused_flags = 0;
  const std::int32_t guarded  = run(/*confusion=*/0, &guard_flags);
  const std::int32_t confused = run(/*confusion=*/1, &confused_flags);

  CHECK((guard_flags &
         static_cast<std::uint32_t>(sg::domain::DamageFlag::DAMAGE_FLAG_GUARD)) != 0u);
  CHECK((confused_flags &
         static_cast<std::uint32_t>(sg::domain::DamageFlag::DAMAGE_FLAG_GUARD)) == 0u);
  CHECK(guarded > confused);   // hp_delta 是负数 ⇒ 掉血更少 = 值更大
}

TEST_CASE("ResolveTurn:骑宠分摊写进 hp_delta / pet_hp_delta") {
  Duel d = MakeDuel(/*atk=*/100000, /*def=*/1);
  d.field.at(10).has_ride     = true;
  d.field.at(10).ride_hp      = 500;
  d.field.at(10).ride_max_hp  = 500;
  d.field.at(10).defense      = 300;
  d.field.at(10).ride_defense = 100;
  d.field.at(10).hp = d.field.at(10).max_hp = 100000000;

  sg::domain::BattleEvents ev{};
  MaxRandom rng;
  REQUIRE(ResolveTurn(d.field, d.cmds, RulesConfig{}, rng, ev));
  REQUIRE(ev.events.size() == 2);
  const sg::domain::Damage& dmg = ev.events[1].body.damage;

  CHECK(dmg.hp_delta < 0);
  CHECK(dmg.pet_hp_delta < 0);
  // ★ DR-BT2 修正后的方向:主人防御 300 > 宠物 100 ⇒ 主人扛得更多。
  CHECK(-dmg.hp_delta > -dmg.pet_hp_delta);
}

TEST_CASE("ResolveTurn:事件溢出返回 false,不静默截断") {
  // ⚠️★ 05 §10.4:原版 `szAllBattleString` 的 strncat 第三参写错、等价无上界 strcat,
  //    余量仅 56 字节且**无第二道防线**。⇒ 新实现宁可分包,不可静默截断。
  //
  // 构造:20 个单位环形互攻,每人 30 段 ⇒ 20 × (1 + 30) = 620 > 256。
  // ★ 攻击力 0 ⇒ damage 走「defense > attack」分支、再被第 7 步的 ×70/100 压成 0
  //   ⇒ 没人会死,段数不会因死亡提前中断。
  BattleField f = MakeField();
  TurnCommands tc = NoCommands();
  for (int i = 0; i < kSlotCount; ++i) {
    f.at(i) = MakeCombatant(CombatantKind::kPlayer, /*atk=*/0, /*def=*/1000);
    f.at(i).slot = static_cast<std::uint8_t>(i);
    f.at(i).hp = f.at(i).max_hp = 1000000;
    f.at(i).mods.unarmed = false;
    f.at(i).mods.attack_num_min = 30;
    f.at(i).mods.attack_num_max = 30;
    SetAttack(tc, i, (i + 1) % kSlotCount);
  }

  sg::domain::BattleEvents ev{};
  MaxRandom rng;
  CHECK(ResolveTurn(f, tc, RulesConfig{}, rng, ev) == false);
  CHECK(ev.events.size() == ev.events.capacity());
}

TEST_CASE("ResolveTurn:可回放 —— 同种子 + 同输入 ⇒ 事件流逐位相同") {
  // ★★ 这是 00 §0 第 ③ 层「不可自证」的补偿手段本身(05 §1.5):
  //    无法与原版比对,但**可以与自己的历史行为比对**。
  //    ⚠️ 调度层比公式层更容易破这条 —— 遍历顺序、提前 break、少抽一次随机数都会破。
  Duel a = MakeDuel();
  Duel b = MakeDuel();
  SetAttack(a.cmds, 10, 0);
  SetAttack(b.cmds, 10, 0);
  a.field.at(0).level = b.field.at(0).level = 30;   // ⇒ 空手可多段,序列更长

  sg::domain::BattleEvents ev1{}, ev2{};
  SeededRandom r1(0xC0FFEE), r2(0xC0FFEE);
  const bool ok1 = ResolveTurn(a.field, a.cmds, RulesConfig{}, r1, ev1);
  const bool ok2 = ResolveTurn(b.field, b.cmds, RulesConfig{}, r2, ev2);

  CHECK(ok1 == ok2);
  REQUIRE(ev1.events.size() == ev2.events.size());
  CHECK(ev1.events.size() > 0);
  // 逐位比较:生成物是 POD(sg_idl_runtime.h 的 ② 条)⇒ 可直接 memcmp。
  CHECK(std::memcmp(&ev1, &ev2, sizeof(sg::domain::BattleEvents)) == 0);
  CHECK(r1.state() == r2.state());   // ★ 随机源的消费序列也必须一致
}
