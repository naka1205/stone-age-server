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

#include <cstdint>

namespace SA::Rules
{

// 注入式随机源。★ 两个方法对应原版仅有的两个入口。
class Random
{
  public:
	virtual ~Random() = default;

	// 对应原版 `RAND(lo, hi)` —— **闭区间 [lo, hi]**。
	// ⚠️ 原版语义就是闭区间(`RAND(0,1)` 会取到 0 或 1,见 §3.1 第三步第一分支
	//    「只能造成 0 或 1」的表述)。实现方不得改成半开区间。
	//
	// ★★ **退化区间(hi <= lo)的语义是「返回 lo,且照常消耗一次随机数」**
	//    (DR-BT23,2026-09-09 用户裁定「完全对齐」)。这不是防御性约定,是原版行为:
	//
	//      #define RAND(x,y) ((x-1)+1 + (int)((double)(y-(x-1))*rand()/(RAND_MAX+1.0)))
	//
	//    展开后 `y-(x-1)` 在 y==x 时为 1、在 y==x-1 时为 0,
	//    ⇒ 两种情况下取整都得 0 ⇒ 确定返回 x;⚠️ 而 `rand()` 是乘法的操作数,
	//      **无论系数是否为 0 都被求值** ⇒ 原版在退化区间上照样消耗一次。
	//    ⇒ 实现方**不得**用「hi <= lo 就早退」来省掉那一次消耗:
	//      返回值一样,而 rng 序列会自此整体平移。⇒ 见 §9.0.36。
	//
	// ⚠️★ 因此这里**不再要求**调用方保证 lo <= hi。但调用方仍应在语义上避免
	//    构造出退化区间 —— 原版靠载入期归一(如敌人表 lv_min/lv_max)保证运行期
	//    不出现 lo > hi,那些归一**仍要移植**,理由从「本接口的前提」改成
	//    「原版载入期就这么做,位置可换、行为须等价」(更硬:是源码事实而非我方约定)。
	virtual int rand(int lo, int hi) = 0;

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
		if (hi <= lo)
			return lo;
		const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1u;
		return lo + static_cast<int>(r % span);
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
