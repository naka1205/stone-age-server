// src/platform/clock.cpp —— 单调时钟
//
// ★★ 这是全仓**唯一**允许取真实时间的地方。
//    · L3(shared/)由 tools/check_shared_purity.py 禁 <chrono>:读时钟即破坏可回放性;
//    · L1/L2 一律经 IClock 注入,否则 world 的 tick 无法被测试。
//
// ⚠️ steady_clock 而不是 system_clock:01 §3.1 明写「不用墙钟做逻辑判断」。
//    墙钟会被 NTP 往回拨,而"时间往回走"会让一切基于差值的节拍判断出现负数。

#include "platform/api.h"

#include <chrono>

namespace sa::platform {

namespace {
std::int64_t SteadyNanos() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

MonotonicClock::MonotonicClock() noexcept : origin_ns_(SteadyNanos()) {}

Millis MonotonicClock::NowMs() const noexcept {
  // ★ 从构造时刻起算而不是直接返回 epoch 毫秒:
  //   steady_clock 的 epoch 由实现定义(某些平台是开机时刻,某些是 1970),
  //   直接暴露它会让日志里的时间戳在不同平台上量级完全不同。
  return (SteadyNanos() - origin_ns_) / 1000000;
}

}  // namespace sa::platform
