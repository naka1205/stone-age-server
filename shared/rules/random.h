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

#ifndef SG_SHARED_RULES_RANDOM_H
#define SG_SHARED_RULES_RANDOM_H

#include <cstdint>

namespace sg::rules {

// 注入式随机源。★ 两个方法对应原版仅有的两个入口。
class IRandom {
 public:
  virtual ~IRandom() = default;

  // 对应原版 `RAND(lo, hi)` —— **闭区间 [lo, hi]**。
  // ⚠️ 原版语义就是闭区间(`RAND(0,1)` 会取到 0 或 1,见 §3.1 第三步第一分支
  //    「只能造成 0 或 1」的表述)。实现方不得改成半开区间。
  // ⚠️ lo > hi 时的行为由实现定义;调用方不得依赖 —— L3 内部须自行保证 lo <= hi。
  virtual int Rand(int lo, int hi) = 0;

  // 对应原版 `rand() % n` —— 返回 [0, n)。
  // ⚠️ 单独保留而不用 Rand(0, n-1) 表达:原版这两个入口的取数序列不同,
  //    合并会改变可回放序列。移植期须逐调用点对应到原来那个入口。
  virtual int RandMod(int n) = 0;

 protected:
  IRandom() = default;
  IRandom(const IRandom&) = default;
  IRandom& operator=(const IRandom&) = default;
};

// 确定性实现:同种子 + 同调用序列 ⇒ 同结果。用于黄金用例集与回放。
//
// ★ 算法固定为 xorshift64*,**不用 std::mt19937** —— 后者的实现虽由标准规定,
//   但 std::uniform_int_distribution 的取数方式**不由标准规定**,
//   跨标准库实现会给出不同序列 ⇒ 黄金用例集在另一个平台上会整批失败。
//   这里自己算,序列跨平台逐位一致。
class SeededRandom final : public IRandom {
 public:
  explicit SeededRandom(std::uint64_t seed) noexcept
      : state_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

  int Rand(int lo, int hi) noexcept override {
    if (hi <= lo) return lo;
    const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1u;
    return lo + static_cast<int>(Next() % span);
  }

  int RandMod(int n) noexcept override {
    if (n <= 0) return 0;
    return static_cast<int>(Next() % static_cast<std::uint64_t>(n));
  }

  std::uint64_t state() const noexcept { return state_; }

 private:
  std::uint64_t Next() noexcept {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 0x2545F4914F6CDD1Dull;
  }

  std::uint64_t state_;
};

}  // namespace sg::rules

#endif  // SG_SHARED_RULES_RANDOM_H
