// tests/platform_config_test.cpp —— 配置装载与校验
//
// ★ 这些用例守的是 01 §11.1 的「快速失败」:内容/配置不合法就**拒绝启动**。
//   ⚠️ 反过来的那一半同样重要 —— 合法配置必须**真的**被读进去,
//     而不是"解析失败了但用默认值兜住,看起来一切正常"。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "platform/api.h"

using namespace sa::platform;

namespace {

bool HasError(const ConfigResult& r, const char* path_substr) {
  for (const ConfigError& e : r.errors) {
    if (e.path.find(path_substr) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("空对象 ⇒ 全默认值,且必须解析成功") {
  const ConfigResult r = ParseConfig("{}");
  REQUIRE(r.ok);
  CHECK(r.config.listen_port == 8300);
  CHECK(r.config.protocol_version == 1);
  CHECK(r.config.tempo.tick_hz == 60);
  CHECK(r.config.tempo.battle_turn_interval_ms == 1200);
  CHECK(r.config.modules.size() == 1);
  CHECK(r.config.modules[0] == "world");
}

TEST_CASE("合法配置被真的读进去(不是靠默认值兜住)") {
  const ConfigResult r = ParseConfig(R"({
    "listen_port": 9001,
    "protocol_version": 7,
    "heartbeat_interval_ms": 15000,
    "rng_seed": 20260903,
    "log_level": "debug",
    "tempo": { "tick_hz": 30, "battle_turn_interval_ms": 800,
               "char_loop_interval_ms": 500 },
    "modules": ["world"]
  })");
  REQUIRE(r.ok);
  CHECK(r.config.listen_port == 9001);
  CHECK(r.config.protocol_version == 7);
  CHECK(r.config.heartbeat_interval_ms == 15000);
  CHECK(r.config.rng_seed == 20260903u);
  CHECK(r.config.log_level == LogLevel::kDebug);
  CHECK(r.config.tempo.tick_hz == 30);
  CHECK(r.config.tempo.battle_turn_interval_ms == 800);
  CHECK(r.config.tempo.char_loop_interval_ms == 500);
}

// ★★ 这条是本文件里最要紧的一个:00 §10.4 的静默错误类型。
//    一个拼错的 "listen_prot" 若被忽略,表现是"服务端起在了另一个端口",
//    而没有任何一处会报错。
TEST_CASE("未知配置项必须报错,不能忽略") {
  const ConfigResult r = ParseConfig(R"({ "listen_prot": 9001 })");
  CHECK_FALSE(r.ok);
  CHECK(HasError(r, "listen_prot"));
}

TEST_CASE("嵌套对象里的未知项同样报错") {
  const ConfigResult r = ParseConfig(R"({ "tempo": { "tick_hzz": 60 } })");
  CHECK_FALSE(r.ok);
  CHECK(HasError(r, "tempo.tick_hzz"));
}

TEST_CASE("越界与类型错") {
  SUBCASE("端口低于 1024 —— 那要 root,而本服务端没有理由以 root 运行") {
    const ConfigResult r = ParseConfig(R"({ "listen_port": 80 })");
    CHECK_FALSE(r.ok);
    CHECK(HasError(r, "listen_port"));
  }
  SUBCASE("端口超过 65535") {
    const ConfigResult r = ParseConfig(R"({ "listen_port": 70000 })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("protocol_version 不许为 0 —— 0 常是「字段没填」的表现") {
    const ConfigResult r = ParseConfig(R"({ "protocol_version": 0 })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("整数位上给了小数") {
    const ConfigResult r = ParseConfig(R"({ "listen_port": 8300.5 })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("整数位上给了字符串") {
    const ConfigResult r = ParseConfig(R"({ "listen_port": "8300" })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("负数") {
    const ConfigResult r = ParseConfig(R"({ "listen_port": -1 })");
    CHECK_FALSE(r.ok);
  }
}

// ★★ bind_addr:本项此前是一个**静默失效的配置面**(2026-09-06 发现)。
//    字段在 ServerConfig 里、main.cpp 三处在用、Listen() 吃它,
//    唯独 ParseConfig 从不读它、也没列进已知键
//    ⇒ 写了 bind_addr 的配置文件**会被当成拼写错误而拒绝启动**,
//      而默认值恰好能用 ⇒ 没写的人永远发现不了。
//    ⇒ 与 00 §9.0.12 那个 `-DSG_WERROR` 同族:开关看着在,实际没接上。
TEST_CASE("bind_addr 被真的读进去") {
  const ConfigResult r = ParseConfig(R"({ "bind_addr": "127.0.0.1" })");
  REQUIRE(r.ok);
  CHECK(r.config.bind_addr == "127.0.0.1");
}

TEST_CASE("bind_addr 默认值") {
  const ConfigResult r = ParseConfig("{}");
  REQUIRE(r.ok);
  CHECK(r.config.bind_addr == "0.0.0.0");
}

// ⚠️★ 认得的地址集合必须与 tcp_transport.cpp 的 Listen() 一致 ——
//    它对非空地址走 inet_pton(AF_INET)。配置说"合法"而端口绑不上的话,
//    报错会晚到启动第 6 步、且以 errno 文本的面目出现,
//    与"地址写错了"看不出关系。
TEST_CASE("bind_addr 只认点分十进制 IPv4") {
  SUBCASE("主机名不放行 —— 1.5 没有 DNS 解析路径") {
    const ConfigResult r = ParseConfig(R"({ "bind_addr": "localhost" })");
    CHECK_FALSE(r.ok);
    CHECK(HasError(r, "bind_addr"));
  }
  SUBCASE("IPv6 不放行 —— Listen() 侧还不支持") {
    const ConfigResult r = ParseConfig(R"({ "bind_addr": "::1" })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("段值越界") {
    const ConfigResult r = ParseConfig(R"({ "bind_addr": "1.2.3.256" })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("段数不足") {
    const ConfigResult r = ParseConfig(R"({ "bind_addr": "10.0.1" })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("尾随的点") {
    const ConfigResult r = ParseConfig(R"({ "bind_addr": "10.0.0.1." })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("空串") {
    const ConfigResult r = ParseConfig(R"({ "bind_addr": "" })");
    CHECK_FALSE(r.ok);
  }
  // ★ 前导零:inet_pton 也拒(它与 inet_aton 的区别正在于此),
  //   而人写 "010.0.0.1" 时想的多半是十进制 10。
  SUBCASE("前导零") {
    const ConfigResult r = ParseConfig(R"({ "bind_addr": "010.0.0.1" })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("类型错") {
    const ConfigResult r = ParseConfig(R"({ "bind_addr": 3232235777 })");
    CHECK_FALSE(r.ok);
  }
}

// ── demo_battle(1.4 脚手架)──────────────────────────────────
TEST_CASE("demo_battle 默认关 —— 它改变会话语义,不能是默认行为") {
  const ConfigResult r = ParseConfig("{}");
  REQUIRE(r.ok);
  CHECK_FALSE(r.config.demo_battle.enabled);
  CHECK(r.config.demo_battle.slot == 0);
}

TEST_CASE("demo_battle 被真的读进去") {
  const ConfigResult r =
      ParseConfig(R"({ "demo_battle": { "enabled": true, "slot": 4 } })");
  REQUIRE(r.ok);
  CHECK(r.config.demo_battle.enabled);
  CHECK(r.config.demo_battle.slot == 4);
}

TEST_CASE("demo_battle 的错法") {
  SUBCASE("enabled 不是布尔") {
    const ConfigResult r = ParseConfig(R"({ "demo_battle": { "enabled": 1 } })");
    CHECK_FALSE(r.ok);
    CHECK(HasError(r, "demo_battle.enabled"));
  }
  // ★ 上限 9:己方是 0..9。让玩家落到敌方半场是配置写错,不是一种玩法。
  SUBCASE("槽号落到敌方半场") {
    const ConfigResult r = ParseConfig(R"({ "demo_battle": { "slot": 10 } })");
    CHECK_FALSE(r.ok);
    CHECK(HasError(r, "demo_battle.slot"));
  }
  SUBCASE("未知子项") {
    const ConfigResult r =
        ParseConfig(R"({ "demo_battle": { "enabld": true } })");
    CHECK_FALSE(r.ok);
    CHECK(HasError(r, "demo_battle.enabld"));
  }
  SUBCASE("不是对象") {
    const ConfigResult r = ParseConfig(R"({ "demo_battle": true })");
    CHECK_FALSE(r.ok);
  }
}

TEST_CASE("log_level 只认五个名字") {
  CHECK(ParseConfig(R"({ "log_level": "warn" })").ok);
  CHECK_FALSE(ParseConfig(R"({ "log_level": "verbose" })").ok);
  CHECK_FALSE(ParseConfig(R"({ "log_level": 3 })").ok);
}

// ⚠️ 一次说完全部错误,而不是遇到第一个就走人 —— 见 config.cpp 卷首。
TEST_CASE("多处错误一次全报") {
  const ConfigResult r = ParseConfig(R"({
    "listen_port": 80,
    "protocol_version": 0,
    "log_level": "loud"
  })");
  CHECK_FALSE(r.ok);
  CHECK(r.errors.size() >= 3);
}

// ★ 阶段 2 的模块名认得,但装载会被拒 —— 不是"忽略未实现的模块"。
//   否则部署配置写着 social 而实际没装,直到有人用到才发现。
TEST_CASE("模块清单") {
  SUBCASE("world 可用") {
    const ConfigResult r = ParseConfig(R"({ "modules": ["world"] })");
    CHECK(r.ok);
  }
  SUBCASE("阶段 2 的模块被显式拒绝,而不是静默跳过") {
    const ConfigResult r = ParseConfig(R"({ "modules": ["world", "social"] })");
    CHECK_FALSE(r.ok);
    CHECK(HasError(r, "modules[1]"));
  }
  SUBCASE("完全不认识的名字") {
    CHECK_FALSE(ParseConfig(R"({ "modules": ["worlds"] })").ok);
  }
  SUBCASE("空清单") {
    CHECK_FALSE(ParseConfig(R"({ "modules": [] })").ok);
  }
  SUBCASE("重复项") {
    CHECK_FALSE(ParseConfig(R"({ "modules": ["world", "world"] })").ok);
  }
}

TEST_CASE("JSON 本身的错误要给出位置") {
  SUBCASE("尾随逗号") {
    CHECK_FALSE(ParseConfig(R"({ "listen_port": 8300, })").ok);
  }
  SUBCASE("非对象顶层") {
    CHECK_FALSE(ParseConfig("[1,2,3]").ok);
  }
  SUBCASE("注释 —— 明确拒绝,而不是跳过") {
    const ConfigResult r = ParseConfig("{ // 端口\n \"listen_port\": 8300 }");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("重复键 —— 报错而不是后者覆盖前者") {
    const ConfigResult r =
        ParseConfig(R"({ "listen_port": 8300, "listen_port": 9000 })");
    CHECK_FALSE(r.ok);
  }
  SUBCASE("截断的输入") {
    CHECK_FALSE(ParseConfig(R"({ "listen_port": )").ok);
  }
}

TEST_CASE("读不到文件是一条错误,不是崩溃") {
  const ConfigResult r = LoadConfigFile("/tmp/sg-this-path-should-not-exist.json");
  CHECK_FALSE(r.ok);
  CHECK(r.errors.size() == 1);
}

// ── 随机源 ───────────────────────────────────────────────────
//
// ★★ 守的是 01 §10「同种子 + 同输入 ⇒ 逐位相同」这条**可回放性**,
//    它是 00 §0 中 ③ 层「规则不可自证」最实际的补偿。
TEST_CASE("RandomSource:同一主种子给出同一串战斗种子") {
  RandomSource a(12345);
  RandomSource b(12345);
  for (int i = 0; i < 64; ++i) {
    CHECK(a.NextSeed() == b.NextSeed());
  }
  CHECK(a.minted() == 64);
}

TEST_CASE("RandomSource:不同主种子给出不同序列") {
  RandomSource a(1);
  RandomSource b(2);
  bool any_diff = false;
  for (int i = 0; i < 16; ++i) {
    if (a.NextSeed() != b.NextSeed()) any_diff = true;
  }
  CHECK(any_diff);
}

// ⚠️ 0 是 shared/rules/random.h 里 SeededRandom 的哨兵(它会替换成固定常数)
//    ⇒ 若派种子时吐出 0,那一场战斗会与"任何一场恰好种子为 0 的战斗"共用序列。
TEST_CASE("RandomSource:永远不吐出 0") {
  RandomSource r(0xFFFFFFFFFFFFFFFFull);
  for (int i = 0; i < 4096; ++i) {
    CHECK(r.NextSeed() != 0u);
  }
}

TEST_CASE("RandomSource:主种子为 0 时自行派生一个非 0 的") {
  RandomSource r(0);
  CHECK(r.master_seed() != 0u);
}

// ── 时钟 ─────────────────────────────────────────────────────
TEST_CASE("ManualClock 由调用方推进 —— tick 的可测性就靠它") {
  ManualClock c(1000);
  CHECK(c.NowMs() == 1000);
  c.Advance(250);
  CHECK(c.NowMs() == 1250);
}

TEST_CASE("MonotonicClock 从构造时刻起算,不暴露平台各异的 epoch") {
  MonotonicClock c;
  const Millis a = c.NowMs();
  CHECK(a >= 0);
  CHECK(a < 60000);  // 构造到这一行不可能过了一分钟
}

// ── 日志 ─────────────────────────────────────────────────────
TEST_CASE("日志级别过滤") {
  Logger log(LogLevel::kWarn);
  CHECK_FALSE(log.Enabled(LogLevel::kInfo));
  CHECK(log.Enabled(LogLevel::kError));
  log.Log(LogLevel::kInfo, LogEvent::kServerReady, {});
  CHECK(log.emitted() == 0);
  log.Log(LogLevel::kError, LogEvent::kConfigRejected, {{"path", std::string_view("x")}});
  CHECK(log.emitted() == 1);
}
