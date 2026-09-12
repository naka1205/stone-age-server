// shared/rules/random.h —— 注入式随机源
//
// ★★ 这个接口只有两个方法,不是设计上的极简主义,是**实测结论的直接对应**:
//    07 §11.3 判据 ④ / 00 §1.1:战斗闭包内 **166 个随机点只经 2 个入口**
//    (`RAND(lo,hi)` 与 `rand()`),0.0 全闭包重跑后仍是 **2 个入口**(§1.3 第④步)。
//    ⇒ 四步改造第④步「RAND() → 注入序列」只需替换这两个入口。
//
// ★ 为什么必须注入而不是直接 rand():
//    05-battle.md §1.5 —— **战斗事件流 + 注入式随机源 = 可回放**。
//    这是 00 §0 中 ③ 层「规则能写出来但无法自证正确」**唯一**的补偿手段:
//    虽然无法与原版比对,但**可以与自己的历史行为比对** ——
//    同种子 + 同输入,结果必须逐位相同。⇒ 黄金用例集据此成立。
//
// ⚠️ 因此实现方**不得**在 L3 内部调用任何其他随机源(包括 std::rand、
//    std::mt19937 的全局实例、时间戳)。一处漏网,整个用例集失去意义。

#ifndef __SA_RandomSource_H__
#define __SA_RandomSource_H__

#include <cmath>
#include <cstdint>

namespace SA::Rules
{

// SSRC80 include/util.h:77–78。浮点端点参与计算，只有跨度乘积在这里截断。
// unit 由注入源提供，范围 [0,1)。外层赋给整数的截断由调用点按原类型执行。
inline double scaleOriginalRand(double lo, double hi, double unit) noexcept
{
	return (lo - 1.0) + 1.0 + std::trunc((hi - (lo - 1.0)) * unit);
}

// 注入式随机源。★ 两个方法对应原版仅有的两个入口。
class Random
{
  public:
	virtual ~Random() = default;

	// 整数 RAND 的取值与原闭区间一致，每次调用均消耗随机数。
	// hi==lo 或 hi==lo-1 时恒返 lo；更深的倒置仍按原有符号跨度取数。
	virtual int rand(int lo, int hi) = 0;
	virtual double randReal(double lo, double hi) = 0;

	// 对应原版 `rand() % n` —— 返回 [0, n)。
	// ⚠️ 单独保留而不用 Rand(0, n-1) 表达:原版这两个入口的取数序列不同,
	//    合并会改变可回放序列。移植期须逐调用点对应到原来那个入口。
	//
	// ★ `n <= 0` 同上条:**照常消耗一次**再返回 0。原版 `rand() % 0` 是除零,
	//   ⇒ 那个状态原版不该出现,这里的 0 是防御;⚠️ 但「防御」不该顺带改变
	//   消耗次数 —— 原版的 `rand()` 在取模之前就已求值。
	virtual int randMod(int n) = 0;

  protected:
	Random() = default;
	Random(const Random &) = default;
	Random &operator=(const Random &) = default;
};

// 确定性实现:同种子 + 同调用序列 ⇒ 同结果。用于黄金用例集与回放。
//
// ★ 算法固定为 xorshift64*,**不用 std::mt19937** —— 后者的实现虽由标准规定,
//   但 std::uniform_int_distribution 的取数方式**不由标准规定**,
//   跨标准库实现会给出不同序列 ⇒ 黄金用例集在另一个平台上会整批失败。
//   这里自己算,序列跨平台逐位一致。
class SeededRandom final : public Random
{
  public:
	explicit SeededRandom(std::uint64_t seed) noexcept
	    : _state(seed ? seed : 0x9E3779B97F4A7C15ull) {}

	// ★★ 两个方法都**先取数、后判退化**,顺序即语义(DR-BT23)。
	//    写成 `if (hi <= lo) return lo;` 再取数会省掉一次消耗 ⇒ 返回值一样、
	//    序列平移 ⇒ 这类偏差不会被任何「返回值对不对」的断言抓到。
	int rand(int lo, int hi) noexcept override
	{
		const std::uint64_t r = next(); // ★ 无条件消耗,对应原版 `rand()` 恒被求值
		if (hi < lo)
			return static_cast<int>(scaleOriginalRand(lo, hi, unit(r)));
		const std::uint64_t span = static_cast<std::uint64_t>(
		                               static_cast<std::int64_t>(hi) - lo) +
		                           1u;
		return static_cast<int>(static_cast<std::int64_t>(lo) +
		                        static_cast<std::int64_t>(r % span));
	}

	double randReal(double lo, double hi) noexcept override
	{
		return scaleOriginalRand(lo, hi, unit(next()));
	}

	int randMod(int n) noexcept override
	{
		const std::uint64_t r = next(); // ★ 同上:原版取模前 `rand()` 已求值
		if (n <= 0)
			return 0;
		return static_cast<int>(r % static_cast<std::uint64_t>(n));
	}

	std::uint64_t state() const noexcept { return _state; }

  private:
	static double unit(std::uint64_t raw) noexcept
	{
		// 原 GNU libc rand 的 31 位取值域；保留表达式可达范围，PRNG 本身仍是 xorshift。
		return static_cast<double>(raw & 0x7FFFFFFFull) / 2147483648.0;
	}

	std::uint64_t next() noexcept
	{
		_state ^= _state >> 12;
		_state ^= _state << 25;
		_state ^= _state >> 27;
		return _state * 0x2545F4914F6CDD1Dull;
	}

	std::uint64_t _state;
};

} // namespace SA::Rules

#endif // __SA_RandomSource_H__
