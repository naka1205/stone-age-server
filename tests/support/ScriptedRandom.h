// tests/support/ScriptedRandom.h —— 脚本化随机源(测试专用)
//
// ★★ **本文件是唯一一份。** 2026-09-09 之前 `RulesBattleTest` / `RulesProgressionTest` /
//    `WorldTickTest` 各自带一份 `ScriptedRandom` 拷贝,而实测**三份已经漂了**:
//      · `randMod` 的退化分支:三份都「先返回 0、不取数」,而各自的 `rand` 却无条件取数
//        ⇒ 同一个类里两个方法自相矛盾;
//      · `calls()` 只有两份有;
//      · `rand` 的钳制写法三份各不相同(等价,但已经开始分化)。
//    ⇒ 与欠债 24(`.clang-format` 同源多份)同族:**同源多份必然漂移,而漂移不报错**。
//
// ★ 为什么不用 `SeededRandom` 撞运气:分档边界(`05` §3.5 的 25/50/70/85/95/100、
//   §3.9 的 10/30/70)必须**逐个边界值**验,而不是"跑一万次看分布像不像"。
//   分布用例挡不住"档位表抄错一格"这种最常见的移植错误。

#ifndef __SA_TESTS_ScriptedRandom_H__
#define __SA_TESTS_ScriptedRandom_H__

#include "rules/RandomSource.h"

#include <cstddef>
#include <utility>
#include <vector>

// 脚本化随机源:逐值给定取数序列,用尽后重复最后一个值。
class ScriptedRandom final : public SA::Rules::Random
{
  public:
	explicit ScriptedRandom(std::vector<int> script) : _script(std::move(script)) {}

	// ★★ 退化区间(`hi <= lo`)**照常取数**,与 `SeededRandom` 及原版 `RAND` 宏一致
	//    (DR-BT23)。⚠️ 这一条不是防御:原版 `RAND(x,x)` 的 `rand()` 是乘法操作数,
	//    无论系数是否为 0 都被求值 ⇒ 省掉它会让 rng 序列自此整体平移,
	//    而**返回值一模一样** ⇒ 没有任何「返回值对不对」的断言会红。
	int rand(int lo, int hi) override
	{
		++_calls;
		const int v = next();
		const int lower = hi < lo - static_cast<std::int64_t>(1) ? hi + 2 : lo;
		const int upper = hi < lo ? lo : hi;
		return v < lower ? lower : (v > upper ? upper : v);
	}

	// 浮点 RAND 的结果是 lo + 整数偏移；脚本仍指定截断后的期望结果。
	double randReal(double lo, double hi) override
	{
		++_calls;
		const double span = hi - (lo - 1.0);
		const double end = std::trunc(span * 2147483647.0 / 2147483648.0);
		const double lower = end < 0 ? end : 0;
		const double upper = end > 0 ? end : 0;
		const double offset = static_cast<double>(next()) - std::trunc(lo);
		return (lo - 1.0) + 1.0 +
		       (offset < lower ? lower : (offset > upper ? upper : offset));
	}

	// ★ `n <= 0` 同样**先取数再返回 0** —— 原版 `rand() % n` 的 `rand()` 在取模前已求值。
	//   ⚠️ 三份拷贝在这一支上原本都是「先返回、不取数」,与同一个类的 `rand` 自相矛盾。
	int randMod(int n) override
	{
		++_calls;
		const int v = next();
		return n <= 0 ? 0 : v % n;
	}

	// 调用次数。★ 自 DR-BT23 起它同时**等于取数次数** —— 两个入口都无条件 `next()`。
	//   ⚠️ 在那之前两者在 `randMod(n <= 0)` 上分叉,而名字看不出这件事。
	int calls() const { return _calls; }

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
	int _calls = 0;
};

#endif // __SA_TESTS_ScriptedRandom_H__
