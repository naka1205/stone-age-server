// src/platform/random.cpp —— 随机源的服务端侧
//
// ★ L3 的 IRandom 接口与确定性实现在 shared/rules/random.h(必须双端共享)。
//   本文件只负责一件事:**种子从哪来,以及它有没有被记下来**。
//
// ⚠️★ 为什么这件事值得单独一个类:
//   01 §10 把「同一随机种子 + 同一输入 ⇒ 结果逐位相同」列为
//   00 §0 中 ③ 层「规则能写出来但无法自证正确」最实际的补偿。
//   ⇒ 补偿成立的前提是**事后能重放**,而重放的前提是**当时的种子还在**。
//     种子若在栈上随手 rand() 出来又丢掉,那句话就只是一句话。

#include "platform/api.h"

#include <chrono>

namespace sa::platform {

namespace {

// splitmix64。★ 与 shared/rules/random.h 的 xorshift64* 是**两个不同用途**:
//   那个产玩法随机数(必须双端逐位一致);这个只派生种子(不进 L3)。
//   ⚠️ 不复用同一个算法是有意的 —— 若种子序列与玩法序列同源,
//     "第 N 场战斗的种子"会与"某场战斗内第 N 次取数"产生可预测的关联。
std::uint64_t SplitMix64(std::uint64_t& state) noexcept {
  state += 0x9E3779B97F4A7C15ull;
  std::uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

std::uint64_t DeriveBootSeed() noexcept {
  // ⚠️ 这是**唯一**允许用墙钟的地方,而且它不参与任何逻辑判断 ——
  //    只是要一个"每次启动都不同"的数。取到之后立刻被打进日志,
  //    从那一刻起整条随机序列就是确定的、可复现的。
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const std::uint64_t ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
  std::uint64_t s = ns;
  return SplitMix64(s);
}

}  // namespace

RandomSource::RandomSource(std::uint64_t master_seed) noexcept
    : master_seed_(master_seed != 0 ? master_seed : DeriveBootSeed()),
      state_(master_seed_) {}

std::uint64_t RandomSource::NextSeed() noexcept {
  ++minted_;
  const std::uint64_t s = SplitMix64(state_);
  // ★ 0 是 SeededRandom 的哨兵(它会替换成一个固定常数)⇒ 让 0 永远不出现,
  //   否则"第 N 场战斗"与"某场种子恰为 0 的战斗"会共用同一条序列。
  return s != 0 ? s : 0x9E3779B97F4A7C15ull;
}

}  // namespace sa::platform
