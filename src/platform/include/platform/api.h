// src/platform/include/platform/api.h —— L0 平台设施的**唯一**对外面
//
// ★★ 本文件是 sa_platform 这个模块暴露给外界的全部内容(00 §3.1)。
//    模块目录下其余头文件都是私有的,CMake 只把 include/ 挂成 PUBLIC。
//    ⚠️ 这不是代码洁癖:00 §10.4 把「进程内捷径」列为三类静默错误之一 ——
//      单容器形态下随手读了对方内部状态,生产分布式形态就会以最难查的方式崩掉。
//      ⇒ 让「读不到」成为编译期事实,而不是评审时的口头纪律。
//
// ── 阶段 1.5 的切面(00 §9.0.4)──────────────────────────────
//   ✅ 要:配置装载 · 结构化日志 · 单调时钟 · IRandom 的服务端实现
//   ⬜ 不要:指标端点(prometheus-cpp)· 热重载
//
// ⚠️★ 一处与 01 §9 的**有意偏离**,已经用户裁定(2026-09-03):
//    §9 要求「结构化 + schema 校验,不是 key=value 文本」。本批次**只做前半**:
//    配置是结构化的(JSON),校验是**手写的必填项与范围检查**,
//    **不建声明式 schema 机制**。理由是配置项现在只有个位数,
//    为它造一套声明式校验器等于先付一笔还没有对应问题的成本。
//    ★ 但「任一项不合法 ⇒ 拒绝启动」这个性质**保留**(01 §11.1 的快速失败),
//      那才是 §9 真正要的东西 —— 声明式只是手段。

#ifndef SA_PLATFORM_API_H
#define SA_PLATFORM_API_H

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace sa::platform {

// ── 单调时钟 ──────────────────────────────────────────────────
//
// ★★ 为什么要一个接口而不是直接调 std::chrono:
//    01 §3.1 tick 第 1 步写「统一时钟源,单调时钟,**不用墙钟做逻辑判断**」。
//    ⚠️ 更要紧的是 shared/ 侧的对应约束 —— tools/check_shared_purity.py
//      **禁止 L3 include <chrono>**,因为「L3 读时钟即破坏可回放性」。
//    ⇒ 时钟必须是 L0 的东西,且必须可替换,否则 world 的 tick 无法被测试
//      (测试要的是「把时间推到第 3000 毫秒」,不是「睡 3 秒」)。
using Millis = std::int64_t;

class IClock {
 public:
  virtual ~IClock() = default;
  // 单调递增的毫秒数。起点无意义,只有差值有意义。
  virtual Millis NowMs() const noexcept = 0;

 protected:
  IClock() = default;
  IClock(const IClock&) = default;
  IClock& operator=(const IClock&) = default;
};

// 生产实现:std::chrono::steady_clock。★ 全仓唯一允许取真实时间的地方。
class MonotonicClock final : public IClock {
 public:
  MonotonicClock() noexcept;
  Millis NowMs() const noexcept override;

 private:
  std::int64_t origin_ns_;
};

// 测试实现:时间由调用方推。
class ManualClock final : public IClock {
 public:
  explicit ManualClock(Millis start = 0) noexcept : now_(start) {}
  Millis NowMs() const noexcept override { return now_; }
  void Advance(Millis delta) noexcept { now_ += delta; }
  void SetNow(Millis t) noexcept { now_ = t; }

 private:
  Millis now_;
};

// ── 结构化日志 ────────────────────────────────────────────────
//
// 01 §10:「不做 Web 后台的直接后果:运营与排查全靠日志与指标。这两项不能省。」
// 且要求**结构化**、事件类型是枚举(原版 8.0 是 28 条日志类 / 生效 24,
// 13 已给逐条门控 ⇒ 直接转为事件类型枚举)。
//
// ⚠️★ 与 01 §12 的偏离:那里写的日志库是 **spdlog**。本批次**不引入** ——
//    理由同配置那条:spdlog 会是服务端 src/ 的第一个运行时第三方依赖,
//    而 1.5 的日志需求是「把结构化事件打到 stderr」。
//    ★ 但**事件枚举与字段模型现在就定死**,那才是难改的部分;
//      将来换 spdlog 只需替换 Sink,调用点一处不动。
enum class LogLevel : std::uint8_t {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
};

// 事件类型。★ 显式赋值 —— 与协议号同一条理由(02 §1.1):
// 日志会被外部工具消费,编号一经发布不得复用。
enum class LogEvent : std::uint16_t {
  kUnspecified = 0,

  // 1–99:进程生命周期(01 §11)
  kServerStarting = 1,
  kServerReady = 2,
  kServerStopping = 3,
  kConfigLoaded = 4,
  kConfigRejected = 5,   // ★ 拒绝启动,见本文件卷首
  kModuleLoaded = 6,
  // ★ 01 §11.1 第 6 步「绑定端口」的失败面(2026-09-05,TcpTransport 接入入口时补)。
  //   与 kConfigRejected 同一性质:任一步失败即拒绝启动,原因要在日志里说得出。
  kListenFailed = 7,
  // 收到 SIGINT / SIGTERM ⇒ 进入 01 §11.2 的停服路径。记它是为了让「进程为什么退了」
  //   在日志里有答案 —— 被信号停掉与自己崩掉,事后看退出码分不出来。
  kShutdownSignal = 8,

  // 100–199:网络与会话(01 §5)
  kConnectionAccepted = 100,
  kConnectionClosed = 101,
  kHandshakeAccepted = 102,
  kHandshakeRejected = 103,
  kFrameRejected = 104,   // ★ 超长 / 解码失败 ⇒ 整条消息作废
  kSessionStateChanged = 105,

  // 200–299:世界与战斗(01 §3)
  kTickBudgetExceeded = 200,
  kBattleStarted = 201,
  kBattleTurnResolved = 202,
  kBattleFinished = 203,
  // ★★ 可回放的落点:每场战斗的种子必须落日志。
  //    01 §10「战斗事件流 + 注入式随机源 = 可回放」是 00 §0 中 ③ 层
  //    「规则不可自证」最实际的补偿 —— 而它成立的前提是**种子留得下来**。
  kBattleSeed = 204,
  kBattleEventsTruncated = 205,  // ResolveTurn 返回 false,见 battle.h
};

// 日志字段。定长语义、不做格式化字符串 —— printf 风格的日志无法被机器消费。
class LogField {
 public:
  enum class Kind : std::uint8_t { kInt, kUInt, kStr, kBool };

  LogField(const char* k, std::int64_t v) noexcept
      : key_(k), kind_(Kind::kInt), i_(v) {}
  LogField(const char* k, std::uint64_t v) noexcept
      : key_(k), kind_(Kind::kUInt), u_(v) {}
  LogField(const char* k, std::string_view v) noexcept
      : key_(k), kind_(Kind::kStr), s_(v) {}
  LogField(const char* k, bool v) noexcept
      : key_(k), kind_(Kind::kBool), b_(v) {}

  const char* key() const noexcept { return key_; }
  Kind kind() const noexcept { return kind_; }
  std::int64_t as_int() const noexcept { return i_; }
  std::uint64_t as_uint() const noexcept { return u_; }
  std::string_view as_str() const noexcept { return s_; }
  bool as_bool() const noexcept { return b_; }

 private:
  const char* key_;
  Kind kind_;
  std::int64_t i_ = 0;
  std::uint64_t u_ = 0;
  std::string_view s_{};
  bool b_ = false;
};

class Logger {
 public:
  explicit Logger(LogLevel min_level = LogLevel::kInfo) noexcept
      : min_level_(min_level) {}

  void set_min_level(LogLevel l) noexcept { min_level_ = l; }
  LogLevel min_level() const noexcept { return min_level_; }
  bool Enabled(LogLevel l) const noexcept { return l >= min_level_; }

  void Log(LogLevel level, LogEvent event,
           std::initializer_list<LogField> fields = {}) const;

  // 已产出的行数 —— 测试用,免得为了断言"记了这条日志"去解析 stderr。
  std::uint64_t emitted() const noexcept { return emitted_; }

 private:
  LogLevel min_level_;
  mutable std::uint64_t emitted_ = 0;
};

// ── 配置 ──────────────────────────────────────────────────────
//
// 01 §3.2:节拍是**玩法参数不是性能参数** ——
//   tick_hz 固定不影响玩法;battle_turn_interval_ms / char_loop_interval_ms 可配。
// ⚠️ 15 §5.2 实测 8.0 的 _BATTLE_TIME 与 _CHAR_LOOP_TIME **均为关**
//    ⇒ 原版战斗推进速度 = tick 频率,手感取决于当年硬件。
//    ⇒ 新实现必须显式建模,且 00 §0 已认下 ④ 层永远无法验证
//      ⇒ 这些值只能靠人试,所以它们是配置不是常量。
struct TempoConfig {
  std::uint32_t tick_hz = 60;
  std::uint32_t battle_turn_interval_ms = 1200;
  std::uint32_t char_loop_interval_ms = 1000;
};

struct ServerConfig {
  std::uint16_t listen_port = 8300;
  // ★ 绑定地址(2026-09-04,TcpTransport 落地时补)。
  //   默认 0.0.0.0 = 全部网卡 —— 单容器形态下这是唯一可用的取值。
  //   ⚠️ 生产分布式(00 §4.1)要把 world / gateway 绑在内网网卡上,
  //     那时它才真正起作用。⇒ 现在就把字段立出来,免得届时回头改配置面。
  std::string bind_addr = "0.0.0.0";
  // ★ 单一整数,不做「主版本兼容、次版本忽略」的分支。不等即拒(02 §2.1)。
  std::uint32_t protocol_version = 1;
  std::uint32_t heartbeat_interval_ms = 30000;
  // 0 = 由启动时刻派生并**打进日志**;非 0 = 固定种子,用于回放。
  std::uint64_t rng_seed = 0;
  LogLevel log_level = LogLevel::kInfo;
  TempoConfig tempo{};
  // 01 §12:产物是**单一二进制**,--modules=... 决定装载哪些模块。
  std::vector<std::string> modules{"world"};
};

struct ConfigError {
  std::string path;     // 例:"tempo.tick_hz"
  std::string message;
};

struct ConfigResult {
  bool ok = false;
  ServerConfig config{};
  std::vector<ConfigError> errors{};
};

// 解析并校验。⚠️ 任一项不合法 ⇒ ok == false,调用方必须拒绝启动(01 §11.1)。
//   「内容数据不全就起来,只会在玩家碰到时才炸」—— 这个快速失败的性质是保留项。
ConfigResult ParseConfig(std::string_view json_text);

// 读文件后交给 ParseConfig。文件读不到也是一条 ConfigError,不抛异常。
ConfigResult LoadConfigFile(const std::string& path);

// ── 随机源的服务端侧 ──────────────────────────────────────────
//
// ★ L3 的 IRandom 与确定性实现在 shared/rules/random.h(它必须双端共享)。
//   **服务端这一侧要负责的是种子从哪来、以及它有没有被记下来** ——
//   01 §10:「同一随机种子 + 同一输入,结果必须逐位相同」。
//   ⇒ 种子丢了,可回放性就只是一句话。
class RandomSource {
 public:
  // master_seed == 0 ⇒ 从启动时刻派生一个,并由调用方打进日志。
  explicit RandomSource(std::uint64_t master_seed) noexcept;

  std::uint64_t master_seed() const noexcept { return master_seed_; }

  // 每场战斗取一个。★ 序列由 master_seed 完全决定
  //   ⇒ 记下 master_seed + 第几场,就能重放任意一场。
  std::uint64_t NextSeed() noexcept;

  std::uint64_t minted() const noexcept { return minted_; }

 private:
  std::uint64_t master_seed_;
  std::uint64_t state_;
  std::uint64_t minted_ = 0;
};

}  // namespace sa::platform

#endif  // SA_PLATFORM_API_H
