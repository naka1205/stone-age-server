// tests/world_tick_test.cpp —— 最小 tick 与一场战斗的生命周期
//
// ★★ 本文件真正要钉死的是 01 §3.2 那条**必须新增**的性质:
//    **战斗推进速度 ≠ tick 频率。**
//    15 §5.2 实测 8.0 的 _BATTLE_TIME 与 _CHAR_LOOP_TIME 均为关
//    ⇒ 原版战斗速度就是 tick 频率,手感取决于当年硬件。
//    ⇒ 新实现必须显式建模节拍。若哪天有人"顺手"把结算挪出节拍判断,
//      表现是**战斗快得像快进**,而没有任何一处会报错。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "world/api.h"

#include <vector>

using namespace sa::world;

namespace {

sa::platform::ServerConfig MakeConfig() {
  const sa::platform::ConfigResult r = sa::platform::ParseConfig(R"({
    "protocol_version": 1,
    "log_level": "error",
    "tempo": { "tick_hz": 100, "battle_turn_interval_ms": 1000 }
  })");
  REQUIRE(r.ok);
  return r.config;
}

// 一个能打起来的最小战场:己方 1 人、敌方 1 人。
//
// ⚠️ 数值不追求"像原版" —— 00 §0 已认下 ③ 层不可自证、④ 层无法验证。
//    这里只要求"能分出胜负",验的是**生命周期**,不是平衡性。
//    平衡性归 tests/rules_battle_test.cpp 的黄金用例集。
sa::rules::BattleField MakeField() {
  sa::rules::BattleField f{};
  sa::rules::Combatant& me = f.at(0);
  me.occupied = true;
  me.kind = sa::rules::CombatantKind::kPlayer;
  me.slot = 0;
  me.level = 20;
  me.hp = 500;
  me.max_hp = 500;
  me.attack = 300;
  me.defense = 50;
  me.quick = 200;
  me.luck = 10;

  sa::rules::Combatant& foe = f.at(sa::rules::kSideOffset);
  foe.occupied = true;
  foe.kind = sa::rules::CombatantKind::kEnemy;
  foe.slot = static_cast<std::uint8_t>(sa::rules::kSideOffset);
  foe.level = 5;
  foe.hp = 40;
  foe.max_hp = 40;
  foe.attack = 20;
  foe.defense = 5;
  foe.quick = 30;
  foe.luck = 1;
  return f;
}

// 把测试用的四件套攒在一起,免得每个用例都写一遍。
struct Fixture {
  sa::platform::ServerConfig config = MakeConfig();
  sa::platform::ManualClock clock{0};
  sa::platform::Logger logger{sa::platform::LogLevel::kError};
  sa::platform::RandomSource random{0xABCDEF};
  sa::net::LoopbackTransport transport{};
  World world{config, clock, logger, random, transport};
};

}  // namespace

TEST_CASE("tick 会推进,且不会自己停下来") {
  Fixture f;
  for (int i = 0; i < 10; ++i) f.world.Tick();
  CHECK(f.world.ticks() == 10);
  CHECK_FALSE(f.world.stopped());
}

// ★★ 本文件的核心用例。
TEST_CASE("战斗推进受节拍控制,不等于 tick 频率") {
  Fixture f;
  const BattleId id = f.world.StartBattle(MakeField());
  REQUIRE(f.world.stats(id) != nullptr);

  // 间隔 1000 ms。在此之前无论 tick 多少次,都不该结算出一个回合。
  for (int i = 0; i < 500; ++i) {
    f.clock.Advance(1);   // 每 tick 1 ms ⇒ 累计 500 ms
    f.world.Tick();
  }
  CHECK(f.world.stats(id)->turns_resolved == 0);
  CHECK(f.world.ticks() == 500);

  // 越过间隔 ⇒ 结算一个回合。
  f.clock.Advance(600);
  f.world.Tick();
  CHECK(f.world.stats(id)->turns_resolved == 1);

  // ⚠️ 再连 tick 也不该多结算 —— 下一回合要再等一个间隔。
  for (int i = 0; i < 100; ++i) f.world.Tick();
  CHECK(f.world.stats(id)->turns_resolved == 1);
}

TEST_CASE("节拍是配置项:改间隔,回合数跟着变") {
  Fixture f;
  f.config.tempo.battle_turn_interval_ms = 100;
  World w(f.config, f.clock, f.logger, f.random, f.transport);
  const BattleId id = w.StartBattle(MakeField());

  for (int i = 0; i < 10; ++i) {
    f.clock.Advance(100);
    w.Tick();
  }
  // 10 次跨越 100 ms 的间隔 ⇒ 该结算多轮(具体轮数取决于战斗何时结束)。
  CHECK(w.stats(id)->turns_resolved >= 2);
}

// 敌强夹具。★ 用它而不是 MakeField(),理由是 L3 的既定语义、不是数值口味:
//   「无指令 ⇒ 本回合不行动」写死在 BuildActionOrder,而 world 只给**敌方**
//   填 AI 指令(battle.h `present` 的原话);玩家侧不补默认指令 ——
//   「玩家没提交怎么办」是收集期 / 超时的问题,属阶段 2,且玩家可感知
//   ⇒ 须显式裁定,不由实现者定。
//   ⇒ 一场没有会话参与的战斗只有敌方在动 ⇒ 要打得完,敌方就得打得动。
// ⚠️ 原夹具 attack=20 对 defense=50,每回合伤害趋近下限,200 回合打不完 ——
//   那是**用例的隐含前提**(以为双方都会动)与 L3 语义不符,不是实现错。
sa::rules::BattleField MakeFieldEnemyStrong() {
  sa::rules::BattleField f = MakeField();
  sa::rules::Combatant& foe = f.at(sa::rules::kSideOffset);
  foe.level = 40;
  foe.attack = 400;
  foe.quick = 300;
  return f;
}

TEST_CASE("战斗会打完:一侧全灭 ⇒ finished") {
  Fixture f;
  const BattleId id = f.world.StartBattle(MakeFieldEnemyStrong());
  for (int i = 0; i < 200 && !f.world.stats(id)->finished; ++i) {
    f.clock.Advance(1000);
    f.world.Tick();
  }
  CHECK(f.world.stats(id)->finished);
  CHECK(f.world.stats(id)->turns_resolved > 0);
  // 打完之后不再结算。
  const std::uint32_t at_end = f.world.stats(id)->turns_resolved;
  f.clock.Advance(10000);
  f.world.Tick();
  CHECK(f.world.stats(id)->turns_resolved == at_end);
}

// ★★ 可回放(01 §10):同一主种子 ⇒ 同一场战斗逐回合完全一致。
//    这是 00 §0 中 ③ 层「规则不可自证」最实际的补偿 ——
//    无法与原版比对,但**可以与自己的历史行为比对**。
TEST_CASE("可回放:同主种子的两次运行逐位一致") {
  auto run = [](std::uint64_t master) {
    sa::platform::ServerConfig cfg = MakeConfig();
    sa::platform::ManualClock clock{0};
    sa::platform::Logger logger{sa::platform::LogLevel::kError};
    sa::platform::RandomSource random{master};
    sa::net::LoopbackTransport transport;
    World w(cfg, clock, logger, random, transport);
    const BattleId id = w.StartBattle(MakeFieldEnemyStrong());
    for (int i = 0; i < 200 && !w.stats(id)->finished; ++i) {
      clock.Advance(1000);
      w.Tick();
    }
    return std::pair<std::uint32_t, std::uint32_t>(w.stats(id)->turns_resolved,
                                                   w.stats(id)->events_emitted);
  };
  const auto a = run(0x1234);
  const auto b = run(0x1234);
  const auto c = run(0x5678);

  CHECK(a == b);
  // 不同种子给出不同过程(理论上可能撞车,但两个统计量同时撞的概率极低)。
  CHECK(a != c);
}

// ── 连接与会话在 world 里的接线 ──────────────────────────────
namespace {

std::vector<std::uint8_t> HandshakeBytes(std::uint32_t version) {
  sa::transport::HandshakeRequest req{};
  req.protocol_version = version;
  req.client_build.assign("test");
  std::vector<std::uint8_t> out;
  REQUIRE(sa::net::EncodeFramed(1, req, out));
  return out;
}

}  // namespace

TEST_CASE("连上 → 握手 → 已认证") {
  Fixture f;
  const sa::net::ConnectionId id = f.transport.Connect();
  CHECK(f.world.session_count() == 1);
  CHECK(f.world.session_state(id) == sa::net::SessionState::kAnonymous);

  const std::vector<std::uint8_t> hs = HandshakeBytes(f.config.protocol_version);
  f.transport.Deliver(id, hs.data(), hs.size());
  f.world.Tick();   // 第 2 步:网络入站

  CHECK(f.world.session_state(id) == sa::net::SessionState::kAuthenticated);
  CHECK_FALSE(f.transport.sent(id).empty());   // HandshakeAccepted 已发出
}

TEST_CASE("版本不符 ⇒ 连接被关,但拒绝理由已经发出去了") {
  Fixture f;
  const sa::net::ConnectionId id = f.transport.Connect();
  const std::vector<std::uint8_t> hs = HandshakeBytes(f.config.protocol_version + 1);
  f.transport.Deliver(id, hs.data(), hs.size());
  f.world.Tick();

  CHECK(f.transport.closed(id));
  CHECK_FALSE(f.transport.sent(id).empty());
}

TEST_CASE("入场后能收到事件流") {
  Fixture f;
  const sa::net::ConnectionId id = f.transport.Connect();
  const std::vector<std::uint8_t> hs = HandshakeBytes(f.config.protocol_version);
  f.transport.Deliver(id, hs.data(), hs.size());
  f.world.Tick();
  f.transport.ClearSent(id);

  const BattleId battle = f.world.StartBattle(MakeField());
  REQUIRE(f.world.JoinBattle(battle, id, 0));
  CHECK(f.world.session_state(id) == sa::net::SessionState::kOnline);

  f.clock.Advance(2000);
  f.world.Tick();
  CHECK(f.world.stats(battle)->turns_resolved == 1);
  // ★ 1.4 demo 的验收对象就是这一串字节:**事件流端到端一致**。
  CHECK_FALSE(f.transport.sent(id).empty());
}

TEST_CASE("没握手的连接不能入场") {
  Fixture f;
  const sa::net::ConnectionId id = f.transport.Connect();
  const BattleId battle = f.world.StartBattle(MakeField());
  CHECK_FALSE(f.world.JoinBattle(battle, id, 0));
}

TEST_CASE("入场参数的边界") {
  Fixture f;
  const sa::net::ConnectionId id = f.transport.Connect();
  const std::vector<std::uint8_t> hs = HandshakeBytes(f.config.protocol_version);
  f.transport.Deliver(id, hs.data(), hs.size());
  f.world.Tick();

  const BattleId battle = f.world.StartBattle(MakeField());
  CHECK_FALSE(f.world.JoinBattle(9999, id, 0));                 // 战斗不存在
  CHECK_FALSE(f.world.JoinBattle(battle, id, 200));             // 槽号越界
  CHECK(f.world.JoinBattle(battle, id, 0));
  CHECK_FALSE(f.world.JoinBattle(battle, id, 1));               // 重复入场
}

TEST_CASE("断线会把会话从战斗里摘掉") {
  Fixture f;
  const sa::net::ConnectionId id = f.transport.Connect();
  const std::vector<std::uint8_t> hs = HandshakeBytes(f.config.protocol_version);
  f.transport.Deliver(id, hs.data(), hs.size());
  f.world.Tick();
  const BattleId battle = f.world.StartBattle(MakeField());
  REQUIRE(f.world.JoinBattle(battle, id, 0));

  f.transport.Close(id);
  CHECK(f.world.session_count() == 0);
  // 战斗本身照常推进(1.5 没有"人走了就散场"的规则,那属玩法)
  f.clock.Advance(2000);
  f.world.Tick();
  CHECK(f.world.stats(battle)->turns_resolved == 1);
}

// ⚠️ 01 §11.2 的完整停服流程在 1.5 做不了(没有 storage、没有跨模块请求)。
//    这条只验能做的那部分:拒绝之后确实停下来了。
TEST_CASE("停服请求 ⇒ 关闭全部连接并停止") {
  Fixture f;
  const sa::net::ConnectionId id = f.transport.Connect();
  f.world.RequestShutdown();
  f.world.Tick();
  CHECK(f.world.stopped());
  CHECK(f.transport.closed(id));
  // 停了之后 tick 不再推进
  const std::uint64_t at_stop = f.world.ticks();
  f.world.Tick();
  CHECK(f.world.ticks() == at_stop);
}

// ★ 迟到的指令不该被采纳:02 §1.3 的取向是不靠"下一个到达的包就是回复",
//   这里同理 —— 上一回合的决定在新回合里执行是错的。
TEST_CASE("指令必须指向当前回合") {
  Fixture f;
  const sa::net::ConnectionId id = f.transport.Connect();
  const std::vector<std::uint8_t> hs = HandshakeBytes(f.config.protocol_version);
  f.transport.Deliver(id, hs.data(), hs.size());
  f.world.Tick();
  const BattleId battle = f.world.StartBattle(MakeField());
  REQUIRE(f.world.JoinBattle(battle, id, 0));

  sa::domain::BattleCommand stale{};
  stale.battle_id = battle;
  stale.turn = 999;   // 不是当前回合
  stale.command_kind = sa::domain::BattleCommand::CommandKind::ATTACK;
  stale.command.attack.target = static_cast<std::uint32_t>(sa::rules::kSideOffset);

  std::vector<std::uint8_t> wire;
  REQUIRE(sa::net::EncodeFramed(0, stale, wire));
  f.transport.Deliver(id, wire.data(), wire.size());
  f.world.Tick();

  // 会话层接受了它(消息合法),world 层按回合号丢弃 —— 连接不该被关。
  CHECK_FALSE(f.transport.closed(id));
}
