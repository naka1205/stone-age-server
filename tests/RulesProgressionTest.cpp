// tests/RulesProgressionTest.cpp —— 成长养成属性推导的黄金用例集(DR-DT9)
//
// ★★ 与 RulesBattleTest 同一硬要求(00 §0 第 ③ 层「能写出来、无法自证正确」):
//    本文件每条断言必须指到 `char/char.c` 的**行号**、一条 **DR**、或一条**实测**。
//    输入一律选**手算可验证**的整数,把公式逐位钉死;不凭"看起来对"写断言。
//
// 移植来源:`CHAR_initcharWorkInt`(展开视图 `char/char.c:3419-3487`),经
//   `CHAR_complianceParameter`(`:3542-3547`)拷进三围。公式与截断语义见 Progression.h。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <utility>
#include <vector>

#include "rules/Progression.h"

using namespace SA::Rules;

// ── 用例集专用 rng 存根 ───────────────────────────────────────────────
//
// ★ 与 RulesBattleTest.cpp 同一取向:**不用 SeededRandom 撞运气**。
//   本文件要钉的是「第 ② 步在第 ③ 步之前」这种**顺序**,以及浮点/截断的分档 ——
//   都必须逐个取值验,分布用例挡不住"两步对调"这类移植错误。
class ScriptedRandom final : public Random
{
  public:
	explicit ScriptedRandom(std::vector<int> script) : _script(std::move(script)) {}

	int rand(int lo, int hi) override
	{
		++_calls;
		const int v = next();
		if (v < lo)
			return lo;
		if (v > hi)
			return hi;
		return v;
	}
	int randMod(int n) override
	{
		++_calls;
		if (n <= 0)
			return 0;
		return next() % n;
	}

	// ★ 消费次数是**可回放性的一部分**,不只是调试信息:多摇或少摇一次,
	//   同种子下后续所有取值全错位,而没有任何一处报错。
	int calls() const { return _calls; }

  private:
	int next()
	{
		if (_script.empty())
			return 0;
		// 用尽后重复最后一个值,不回卷(同 RulesBattleTest 的理由:回卷会让
		// "多消费一次"的偏差在长序列里自愈,从而掩盖 rng 消费序列的变化)。
		if (_cursor >= _script.size())
			return _script.back();
		return _script[_cursor++];
	}
	std::vector<int> _script;
	std::size_t _cursor = 0;
	int _calls = 0;
};

// `enemybase1.txt` 的两行**真实模板**(csa8.0 数据包,2026-09-08 实测按 E_T_* 枚举解出)。
//
// ★ 用真数据而不是编一组好算的数:模板列的语义(哪一列是 LVUPPOINT)本身就是
//   被核过的结论(plan `05` §3.5 / 本仓 `04` §7.4),用例顺带把那个映射钉住。
const SpawnTemplate kWuli{4.50, 10, 20, 12, 15, 25};     // 行 1「乌力」
const SpawnTemplate kHolyStone{4.50, 10, 150, 0, 50, 0}; // 行 45「光之圣石」★ 两维基数为 0

// ═══════════════════════════════════════════════════════════════════════════
//  1. 边界:四维全 0 ⇒ 三围全 0
// ═══════════════════════════════════════════════════════════════════════════
//
// ★★ 这正是 DR-BT19/20 记的「捕获宠 / 换宠入场宠三围恒 0」的根:四维为 0 时,
//    补了推导公式**仍是 0**(0 是不动点)。钉住它,免得下一个人以为"接了推导就有战力"
//    —— 真正让宠物有战力还需要非 0 的四维来源(欠债 23 的下一环)。
TEST_CASE("属性推导:四维全 0 ⇒ 三围与 max_hp 全 0(char.c:3438-3487)")
{
	const DerivedStats s = deriveBaseStats(0, 0, 0, 0);
	CHECK(s.attack == 0);
	CHECK(s.defense == 0);
	CHECK(s.quick == 0);
	CHECK(s.max_hp == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  2. ★ 复刻整数截断(DR-DT9):小四维被截断为 0 —— 反直觉,但照源码
// ═══════════════════════════════════════════════════════════════════════════
//
// vital=str=tough=dex=50:
//   quick   = (int)(50*0.01)              = (int)0.5   = 0
//   attack  = (int)(0.5+0.05+0.05+0.025)  = (int)0.625 = 0
//   defense = 同 attack(str==tough,对称) = 0
//   max_hp  = (int)((200+50+50+50)*0.01)  = (int)3.5   = 3
// ★ 若未来有人误把中间值改成"四舍五入"或"保留浮点",本例立即转红。
TEST_CASE("属性推导:小四维(各 50)整数截断 ⇒ 三围 0、max_hp 3(DR-DT9)")
{
	const DerivedStats s = deriveBaseStats(50, 50, 50, 50);
	CHECK(s.attack == 0);
	CHECK(s.defense == 0);
	CHECK(s.quick == 0);
	CHECK(s.max_hp == 3);
}

// ═══════════════════════════════════════════════════════════════════════════
//  3. 手算基线:四维各 100
// ═══════════════════════════════════════════════════════════════════════════
//
// attack=defense = (int)(1 + 0.1 + 0.1 + 0.05) = (int)1.25 = 1
// quick          = (int)(100*0.01)             = (int)1.0  = 1
// max_hp         = (int)((400+100+100+100)*0.01)= (int)7.0  = 7
TEST_CASE("属性推导:四维各 100 的手算基线(char.c:3438-3487)")
{
	const DerivedStats s = deriveBaseStats(100, 100, 100, 100);
	CHECK(s.attack == 1);
	CHECK(s.defense == 1);
	CHECK(s.quick == 1);
	CHECK(s.max_hp == 7);
}

// ═══════════════════════════════════════════════════════════════════════════
//  4. ★ 非对称:力量入攻、耐力入防 ⇒ attack ≠ defense
// ═══════════════════════════════════════════════════════════════════════════
//
// vital=1000, str=5000, tough=1000, dex=2000:
//   attack  = (int)(5000*0.01 + 1000*0.001 + 1000*0.001 + 2000*0.0005)
//           = (int)(50 + 1 + 1 + 1)   = 53
//   defense = (int)(1000*0.01 + 5000*0.001 + 1000*0.001 + 2000*0.0005)
//           = (int)(10 + 5 + 1 + 1)   = 17
//   quick   = (int)(2000*0.01)        = 20
//   max_hp  = (int)((4000+5000+1000+2000)*0.01) = (int)120.0 = 120
// ★ 钉住"主项互换"这个结构:str 走 attack 的 ×1.0、走 defense 的 ×0.1(反之亦然)。
//   照抄时把两条公式的交叉系数写反,只有非对称输入能接住。
TEST_CASE("属性推导:非对称四维 ⇒ 力量入攻/耐力入防(char.c:3441-3461)")
{
	const DerivedStats s = deriveBaseStats(1000, 5000, 1000, 2000);
	CHECK(s.attack == 53);
	CHECK(s.defense == 17);
	CHECK(s.quick == 20);
	CHECK(s.max_hp == 120);
}

// ═══════════════════════════════════════════════════════════════════════════
//  5. 大四维(各 10000):量级验证
// ═══════════════════════════════════════════════════════════════════════════
//
// attack=defense = (int)(100 + 10 + 10 + 5) = 125
// quick          = (int)(10000*0.01)        = 100
// max_hp         = (int)((40000+10000+10000+10000)*0.01) = (int)700.0 = 700
// ★ 印证「原版四维是几千~几万量级」——*0.01 后三围才落到几十~几百的观感。
TEST_CASE("属性推导:大四维(各 10000)量级(char.c:3438-3487)")
{
	const DerivedStats s = deriveBaseStats(10000, 10000, 10000, 10000);
	CHECK(s.attack == 125);
	CHECK(s.defense == 125);
	CHECK(s.quick == 100);
	CHECK(s.max_hp == 700);
}

// ═══════════════════════════════════════════════════════════════════════════
//  6. ★★ 四维生成的**四步顺序** —— 成长率取「+10 前」、四维取「+10 后」
// ═══════════════════════════════════════════════════════════════════════════
//
// 移植来源 `ENEMY_createEnemy`(`char/enemy.c`):
//   :1045-1048  基数 ±2   →  :1052-1056 **成长率打包**  →
//   :1058-1064  撒 10 点   →  :1067-1070 `PARAM_CAL`
//
// 脚本:前 4 次 `rand(0,4)` 全取 2 ⇒ `2-2=0` 无扰动;后 10 次 `rand(0,3)` 全取 0
//       ⇒ 10 点**全撒给 vital**。
// ⇒ 成长率应是**扰动后的原基数** [20,12,15,25];
//   四维用的是 vital 已 +10 的 [30,12,15,25],coef = (1−1)×4.5 + 10 = 10。
//
// ★★ 这一条是本批最容易做错的地方的正面防线:若把成长率挪到 +10 之后,
//    `growth_vital` 会是 **30** 而不是 20 —— 成长率系统性偏大(期望 +2.5/维)。
TEST_CASE("四维生成:成长率取 +10 前、四维取 +10 后(enemy.c:1045-1070)")
{
	ScriptedRandom rng({2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SpawnStats s = rollSpawnStats(kWuli, 1, rng, RulesConfig{});

	// 成长率 = 扰动后、撒点前
	CHECK(s.growth_vital == 20);
	CHECK(s.growth_str == 12);
	CHECK(s.growth_tough == 15);
	CHECK(s.growth_dex == 25);

	// 四维 = coef(10) × 撒点后的基数 [30,12,15,25]
	CHECK(s.vital == 300);
	CHECK(s.str == 120);
	CHECK(s.tough == 150);
	CHECK(s.dex == 250);

	// ★ rng 消费次数 = 4(±2)+ 10(撒点)= 14,一次不多一次不少。
	CHECK(rng.calls() == 14);
}

// ═══════════════════════════════════════════════════════════════════════════
//  7. ★★ DR-DT1:`E_T_LVUPPOINT` 默认按浮点,开关打开才复刻 atoi 截断
// ═══════════════════════════════════════════════════════════════════════════
//
// 乌力 lvup=4.50 · init=10 · level=21 ⇒ coef =(21−1)×lvup + 10:
//   浮点 ⇒ 20×4.5 + 10 = **100**
//   atoi ⇒ 20×4   + 10 = **90**    (−10.0%)
// ★ 印证 DR-DT1 的判据「系数被乘以 (level−1) ⇒ 截断按等级累积,不是一次性的 0.5」——
//   等级越高差得越多;level=1 时两者相同(coef 恒为 init_num)。
TEST_CASE("四维生成:DR-DT1 浮点系数 vs 复刻 atoi 截断(enemy.c:1040)")
{
	const std::vector<int> script{2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	RulesConfig as_designed{}; // replicate_atoi_truncation = false(默认)
	REQUIRE(as_designed.replicate_atoi_truncation == false);
	ScriptedRandom rng_f(script);
	const SpawnStats f = rollSpawnStats(kWuli, 21, rng_f, as_designed);
	CHECK(f.vital == 3000); // 100 × 30
	CHECK(f.str == 1200);   // 100 × 12
	CHECK(f.tough == 1500);
	CHECK(f.dex == 2500);

	RulesConfig replicate{};
	replicate.replicate_atoi_truncation = true;
	ScriptedRandom rng_t(script);
	const SpawnStats t = rollSpawnStats(kWuli, 21, rng_t, replicate);
	CHECK(t.vital == 2700); // 90 × 30
	CHECK(t.str == 1080);
	CHECK(t.tough == 1350);
	CHECK(t.dex == 2250);

	// ★ 开关只该影响系数,不该影响成长率(它在 PARAM_CAL 之前就定了)。
	CHECK(f.growth_vital == t.growth_vital);
	CHECK(f.growth_dex == t.growth_dex);

	// ★ level = 1 时 coef 恒为 init_num ⇒ 两种口径必须一致(截断按等级累积的反面)。
	ScriptedRandom rng_f1(script), rng_t1(script);
	CHECK(rollSpawnStats(kWuli, 1, rng_f1, as_designed).vital ==
	      rollSpawnStats(kWuli, 1, rng_t1, replicate).vital);
}

// ═══════════════════════════════════════════════════════════════════════════
//  8. ★★ 负基数:成长率**各字段独立截断**(用户 2026-09-08 裁定,判据同 DR-DT7)
// ═══════════════════════════════════════════════════════════════════════════
//
// 光之圣石基数 [150, 0, 50, 0];脚本让 str 与 dex 各摇到 `0-2 = −2`
// (`rand(0,4)` 取 0 ⇒ 扰动 −2),vital / tough 取 2 ⇒ 扰动 0。
// ⇒ 扰动后 [150, −2, 50, −2]。
//
// ★★ 原版打包用 `+` 而不是 `|` ⇒ 负的低位向高位**借位**。实测对照(2026-09-08):
//        原版   → [149, 254, 49, 254]   ← vital 与 tough **各被借走 1**
//        本实现 → [150, 254, 50, 254]   ← 各字段独立,vital/tough 不受污染
//   ⇒ 裁定「修正」:跨字段借位是 `+` 写成 `|` 的纯算术 bug(判据同 DR-DT7)。
//   ★ 但**字段内回绕保留**:−2 ⇒ 254 两者一致 —— 修的只有跨字段那一半。
//
// ⚠️ 负四维**照抄不修**:`PARAM_CAL(−2) = 10 × (−2) = −20`。
//    原版就是这样,且它有下游(`deriveBaseStats` 对负输入照算)⇒ 不在这里替它兜。
TEST_CASE("四维生成:负基数 ⇒ 成长率独立截断、四维照抄为负(用户裁定 / enemy.c:1052)")
{
	ScriptedRandom rng({2, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SpawnStats s = rollSpawnStats(kHolyStone, 1, rng, RulesConfig{});

	// ★ 修正生效点:这两维**不被相邻的负字段借位**(原版会是 149 / 49)。
	CHECK(s.growth_vital == 150);
	CHECK(s.growth_tough == 50);
	// 字段内回绕保留,与原版一致。
	CHECK(s.growth_str == 254);
	CHECK(s.growth_dex == 254);

	// 四维:10 点全给 vital ⇒ 基数 [160, −2, 50, −2],coef = 10。
	CHECK(s.vital == 1600);
	CHECK(s.str == -20); // ★ 负四维照抄
	CHECK(s.tough == 500);
	CHECK(s.dex == -20);
}

// ═══════════════════════════════════════════════════════════════════════════
//  9. 可回放性:同种子 ⇒ 逐字段相同;不同种子 ⇒ 至少一维不同
// ═══════════════════════════════════════════════════════════════════════════
//
// ★ `00` §0 第 ③ 层的唯一补偿手段是「与自己的历史行为比对」⇒ 同种子逐位一致
//   是本函数的**契约**,不是巧合。
TEST_CASE("四维生成:同种子逐位可回放(00 §0 / 05 §1.5)")
{
	SeededRandom a(20260908u), b(20260908u);
	const SpawnStats x = rollSpawnStats(kWuli, 30, a, RulesConfig{});
	const SpawnStats y = rollSpawnStats(kWuli, 30, b, RulesConfig{});
	CHECK(x.vital == y.vital);
	CHECK(x.str == y.str);
	CHECK(x.tough == y.tough);
	CHECK(x.dex == y.dex);
	CHECK(x.growth_vital == y.growth_vital);
	CHECK(x.growth_str == y.growth_str);
	CHECK(x.growth_tough == y.growth_tough);
	CHECK(x.growth_dex == y.growth_dex);

	// 换种子:14 次取数全同的概率可忽略 ⇒ 四维不应全等(否则 rng 根本没被消费)。
	SeededRandom c(99991u);
	const SpawnStats z = rollSpawnStats(kWuli, 30, c, RulesConfig{});
	// ⚠️ 先归约成一个 bool 再断言 —— doctest 的表达式分解器接不住 `&&` 链
	//    (`Expression Too Complex Please Rewrite As Binary Comparison`)。
	const bool all_equal = x.vital == z.vital && x.str == z.str &&
	                       x.tough == z.tough && x.dex == z.dex;
	CHECK_FALSE(all_equal);
}

// ═══════════════════════════════════════════════════════════════════════════
//  10. ★★ 量级:真实模板生成的低级敌人,比 demo 手填的单位弱一个数量级
// ═══════════════════════════════════════════════════════════════════════════
//
// 把两个函数串起来:模板 + 等级 → 四维 → 三围。乌力 18 级(脚本让撒点均匀分布):
//   coef = 17×4.5 + 10 = 86.5 · 基数 [23,15,17,27] · 四维 [1989,1297,1470,2335]
//   ⇒ 三围/血 = attack 17 · defense 19 · quick 23 · max_hp 130
//
// ⚠️★★ **对照 `makeDemoField` 手填的 foe:attack 273 / max_hp 590 —— 相差约 16 倍。**
//    ⇒ 这条钉住一个对下一批(M.4b 敌人 L2 实体 + 刷怪)要紧的事实:
//      **真实模板在低等级下产出的战力远低于 demo 那两个单位**。
//      demo 的四维是 `World.cpp` 里显式说明「判据不是原版 20 级该有多少」的手填值,
//      ⇒ 直接把 demo 的敌人换成真实模板会让它几乎打不动玩家(860 血 ÷ 17 攻),
//        `00` §9.0.16 ② 要守的 demo 性质①「无指令也有限回合结束」当场破。
//      ★ 所以本批**有意不接 demo** —— 那不是遗漏,是这条用例算出来的结论。
TEST_CASE("四维生成:真实模板 18 级的战力量级(与 demo 手填值的对照)")
{
	// 撒点脚本 0,1,2,3 循环 ⇒ vital+3 str+3 tough+2 dex+2(前 8 轮各 2 次,后 2 轮给 0/1)
	ScriptedRandom rng({2, 2, 2, 2, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1});
	const SpawnStats s = rollSpawnStats(kWuli, 18, rng, RulesConfig{});
	CHECK(s.vital == 1989);
	CHECK(s.str == 1297);
	CHECK(s.tough == 1470);
	CHECK(s.dex == 2335);

	const DerivedStats d = deriveBaseStats(s.vital, s.str, s.tough, s.dex);
	CHECK(d.attack == 17);
	CHECK(d.defense == 19);
	CHECK(d.quick == 23);
	CHECK(d.max_hp == 130);

	// ★ 与 demo 手填的 foe 对照 —— 差一个数量级这件事本身被断言,不只写在注释里。
	const DerivedStats demo_foe = deriveBaseStats(4000, 26000, 2000, 15000);
	CHECK(demo_foe.attack == 273);
	CHECK(demo_foe.attack > d.attack * 10);
}
