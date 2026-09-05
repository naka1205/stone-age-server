// src/main.cpp —— 单一入口
//
// 01 §12:产物是**单一二进制**,`--modules=...` 之类的配置决定装载什么。
// 01 §11.1 的启动序列(1 读配置 → 2 连 MySQL/Redis → 3 分配定长池 →
//   4 装载 L4 内容 → 5 装载模块 → 6 绑端口 → 7 进 tick)在 1.5 能做
//   1、5、6、7 四步 —— 2/4 要 storage 与内容管线,3 要 L2 实体族。
//   ★ 「任一步失败即拒绝启动」这个性质从第一步就立起来(01 §11.1)。
//
// ✅ 第 6 步于 2026-09-05 接上(TcpTransport 落地,00 §9.0.14)⇒ 本二进制**开始监听端口**。
//    此前 listen_port 读进来、校验过、打进日志,但没人用它 —— 那是有意留着的:
//    配置面先立对,接线那天配置与日志一处没改,只多了 bind_addr 一个字段。
//
// ── 1.5 的入口只做三件事,每件都认下了边界 ─────────────────────────
//   ① 拒绝启动的路径齐全:配置不合法 / 端口绑不上 ⇒ 退出码 1,原因在日志里;
//   ② 主线程 tick 循环:节拍 = tick_hz,Tick 完按**绝对期限**睡到下一拍。
//      ⚠️ 这是 1.5 的最简形态,不是 01 §2 的「定时线程节拍源」—— 见 01 §13 欠债 14;
//   ③ 停机:SIGINT / SIGTERM ⇒ RequestShutdown ⇒ 同一 tick 的第 8 步关闭全部连接
//      ⇒ Stop 传输层 ⇒ 退出码 0。01 §11.2 的完整流程(广播倒计时 / 逐会话保存 /
//      等在途请求收敛)要等 storage 与跨模块请求存在,1.5 只做能做的那一段。

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "net/api.h"
#include "platform/api.h"
#include "world/api.h"

namespace {

// ⚠️ 信号处理函数里只准做一件事:置标志。日志、关连接都在主线程的 tick 里做 ——
//    在 handler 里碰 Logger / socket 是未定义行为的温床。
volatile std::sig_atomic_t g_stop_signal = 0;

void OnStopSignal(int sig) noexcept { g_stop_signal = sig; }

void PrintUsage() {
  std::fputs(
      "用法: stone_age_server [--config <路径>] [--self-test]\n"
      "\n"
      "  --config <路径>   JSON 配置文件;不给则用内置默认值\n"
      "  --self-test       启动、绑一个由系统分配的端口、跑够一个战斗回合间隔的 tick、\n"
      "                    关闭、退出。退出码即判据(ctest 的 server_self_test 走这条)。\n"
      "                    ★ 端口取 0 而不取配置值:自检要能在 CI 上并行跑而不撞端口 ——\n"
      "                    它证明的是「入口能启动、能监听、能停」,不是「8300 空着」。\n",
      stdout);
}

}  // namespace

int main(int argc, char** argv) {
  std::string config_path;
  bool self_test = false;

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      PrintUsage();
      return 0;
    }
    if (std::strcmp(a, "--self-test") == 0) {
      self_test = true;
      continue;
    }
    if (std::strcmp(a, "--config") == 0) {
      if (i + 1 >= argc) {
        std::fputs("错误: --config 后面缺少路径\n", stderr);
        return 2;
      }
      config_path = argv[++i];
      continue;
    }
    std::fprintf(stderr, "错误: 无法识别的参数 %s\n", a);
    PrintUsage();
    return 2;
  }

  // ── 1. 读配置 + 校验 ──────────────────────────────────────
  sa::platform::ConfigResult cfg;
  if (config_path.empty()) {
    cfg = sa::platform::ParseConfig("{}");  // 全默认值,仍走同一条校验路径
  } else {
    cfg = sa::platform::LoadConfigFile(config_path);
  }

  sa::platform::Logger logger(cfg.ok ? cfg.config.log_level
                                     : sa::platform::LogLevel::kInfo);

  if (!cfg.ok) {
    // ★ 一次说完全部错误,然后拒绝启动(见 config.cpp 卷首)。
    for (const sa::platform::ConfigError& e : cfg.errors) {
      logger.Log(sa::platform::LogLevel::kError,
                 sa::platform::LogEvent::kConfigRejected,
                 {{"path", std::string_view(e.path)},
                  {"reason", std::string_view(e.message)}});
    }
    return 1;
  }

  logger.Log(sa::platform::LogLevel::kInfo,
             sa::platform::LogEvent::kServerStarting, {});
  logger.Log(sa::platform::LogLevel::kInfo,
             sa::platform::LogEvent::kConfigLoaded,
             {{"bind_addr", std::string_view(cfg.config.bind_addr)},
              {"listen_port", static_cast<std::uint64_t>(cfg.config.listen_port)},
              {"protocol_version",
               static_cast<std::uint64_t>(cfg.config.protocol_version)},
              {"tick_hz", static_cast<std::uint64_t>(cfg.config.tempo.tick_hz)},
              {"battle_turn_interval_ms",
               static_cast<std::uint64_t>(
                   cfg.config.tempo.battle_turn_interval_ms)}});

  // ── 5. 装载模块 ───────────────────────────────────────────
  // ⚠️ 配置阶段已经拒掉了未实现的模块名(见 config.cpp),所以这里
  //    不会出现"配置写着 social 而实际没装"的静默不一致。
  for (const std::string& m : cfg.config.modules) {
    logger.Log(sa::platform::LogLevel::kInfo,
               sa::platform::LogEvent::kModuleLoaded,
               {{"module", std::string_view(m)}});
  }

  sa::platform::MonotonicClock clock;
  sa::platform::RandomSource random(cfg.config.rng_seed);
  // ★ 主随机种子必须落日志:没有它,"可回放"只是一句话(见 random.cpp)。
  logger.Log(sa::platform::LogLevel::kInfo,
             sa::platform::LogEvent::kBattleSeed,
             {{"master_seed", random.master_seed()},
              {"from_config", cfg.config.rng_seed != 0}});

  // ── 6. 绑定端口 ───────────────────────────────────────────
  // ★ 传输层**先于** World 构造 ⇒ 后于 World 析构(C++ 的构造/析构顺序保证)。
  //   World 的 Tick 第 8 步与析构都会经 transport 关连接,它那时必须还活着;
  //   反向没有悬垂问题 —— TcpTransport 的析构**不回调**(tcp_transport.cpp)。
  sa::net::TcpTransport transport;
  const std::uint16_t port =
      self_test ? std::uint16_t{0} : cfg.config.listen_port;
  if (!transport.Listen(cfg.config.bind_addr.c_str(), port)) {
    logger.Log(sa::platform::LogLevel::kError,
               sa::platform::LogEvent::kListenFailed,
               {{"bind_addr", std::string_view(cfg.config.bind_addr)},
                {"port", static_cast<std::uint64_t>(port)},
                {"reason", std::string_view(transport.last_error())}});
    return 1;
  }

  sa::world::World world(cfg.config, clock, logger, random, transport);

  // 信号在 World 就位之后才挂:此前收到信号直接被默认动作杀掉即可,没有东西需要收尾。
  std::signal(SIGINT, OnStopSignal);
  std::signal(SIGTERM, OnStopSignal);

  logger.Log(sa::platform::LogLevel::kInfo,
             sa::platform::LogEvent::kServerReady,
             {{"bind_addr", std::string_view(cfg.config.bind_addr)},
              // ★ 打的是**实际**监听到的端口,不是配置值 —— self-test 下两者不同。
              {"listen_port", static_cast<std::uint64_t>(transport.listen_port())},
              {"self_test", self_test}});

  // ── 7. 进 tick ────────────────────────────────────────────
  const std::uint32_t hz = cfg.config.tempo.tick_hz;  // 配置已校验在 1..1000
  // self-test 的预算:跑够能覆盖一个战斗回合间隔的 tick 数,然后停。
  const std::uint64_t budget =
      static_cast<std::uint64_t>(hz) *
      (cfg.config.tempo.battle_turn_interval_ms / 1000 + 1);

  const sa::platform::Millis t0 = clock.NowMs();
  std::uint64_t ticks = 0;
  bool shutting_down = false;
  while (!world.stopped()) {
    if (!shutting_down &&
        (g_stop_signal != 0 || (self_test && ticks >= budget))) {
      shutting_down = true;
      if (g_stop_signal != 0) {
        logger.Log(sa::platform::LogLevel::kInfo,
                   sa::platform::LogEvent::kShutdownSignal,
                   {{"signal", static_cast<std::int64_t>(g_stop_signal)}});
      }
      // 本 tick 的第 8 步执行关闭(关全部连接),之后 stopped() 为真。
      world.RequestShutdown();
    }

    world.Tick();
    ++ticks;
    if (world.stopped()) break;

    // 节拍:睡到 t0 + ticks/hz 这个**绝对期限**,不是「Tick 完再睡一个周期」——
    //   后者会把每次 Tick 的耗时累积成漂移,tick 数与墙上时间慢慢脱钩。
    //   ⚠️ 取时间只经 IClock(platform/api.h:「全仓唯一允许取真实时间的地方」),
    //     sleep_for 只是等,不读钟。
    const sa::platform::Millis due =
        t0 + static_cast<sa::platform::Millis>((ticks * 1000u) / hz);
    const sa::platform::Millis now = clock.NowMs();
    if (due > now) {
      std::this_thread::sleep_for(std::chrono::milliseconds(due - now));
    }
  }

  // World 已在第 8 步关掉全部连接;这里只剩监听 socket 要收,以及万一漏网的连接。
  transport.Stop();
  return 0;
}
