// src/main.cpp —— 单一入口
//
// 01 §12:产物是**单一二进制**,`--modules=...` 之类的配置决定装载什么。
// 01 §11.1 的启动序列(1 读配置 → 2 连 MySQL/Redis → 3 分配定长池 →
//   4 装载 L4 内容 → 5 装载模块 → 6 绑端口 → 7 进 tick)在 1.5 只有
//   1、5、7 三步能做 —— 2/4 要 storage 与内容管线,3 要 L2 实体族,6 要 TcpTransport。
//   ★ 但**「任一步失败即拒绝启动」这个性质从第一步就立起来**(01 §11.1)。

#include <cstdio>
#include <cstring>
#include <string>

#include "platform/api.h"
#include "world/api.h"

namespace {

void PrintUsage() {
  std::fputs(
      "用法: stone_age_server [--config <路径>] [--self-test]\n"
      "\n"
      "  --config <路径>   JSON 配置文件;不给则用内置默认值\n"
      "  --self-test       启动、跑若干 tick、退出。★ 阶段 1.5 没有 TcpTransport,\n"
      "                    这是本二进制目前唯一能证明自己活着的方式\n",
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
             {{"listen_port", static_cast<std::uint64_t>(cfg.config.listen_port)},
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

  // ⚠️★ 传输层是 Loopback,不是 TCP —— 见 net/api.h 卷首的切分说明。
  //    ⇒ **本二进制现在还不监听端口**。listen_port 已经读进来、已经校验、
  //      已经打进日志,但没有人用它。这是有意留着的:配置面先立对,
  //      TcpTransport 落地时不用回头改配置与日志。
  sa::net::LoopbackTransport transport;
  sa::world::World world(cfg.config, clock, logger, random, transport);

  logger.Log(sa::platform::LogLevel::kInfo,
             sa::platform::LogEvent::kServerReady, {});

  if (!self_test) {
    std::fputs(
        "⚠️ 阶段 1.5 尚无 TcpTransport,进程不会监听端口。\n"
        "   用 --self-test 跑一遍启动与 tick 循环;端到端行为见 ctest。\n",
        stderr);
    return 0;
  }

  // ── 7. 进 tick ────────────────────────────────────────────
  // self-test:跑够能覆盖一个战斗回合间隔的 tick 数,然后停。
  const std::uint32_t hz = cfg.config.tempo.tick_hz;
  const std::uint64_t ticks =
      static_cast<std::uint64_t>(hz) *
      (cfg.config.tempo.battle_turn_interval_ms / 1000 + 1);
  for (std::uint64_t i = 0; i < ticks && !world.stopped(); ++i) {
    world.Tick();
  }
  world.RequestShutdown();
  world.Tick();

  return 0;
}
