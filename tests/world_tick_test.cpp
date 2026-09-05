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

#include <cstdint>
#include <map>
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

// ══ 1.4 装配:一条会话从握手到打完 ═══════════════════════════════
//
// ★★ 本节是 **1.4 最小双端 demo 的服务端侧凭据**。
//    此前 world 的用例只断言「sent(id) 非空」—— 那证明不了 1.4 的验收口径。
//    00 §9.0.4 / 客户端 01 §12.1 定的口径是**事件流端到端一致**:
//    服务端 ResolveTurn 产出的 BattleEvents 被客户端**逐条正确消费**。
//    ⇒ 下面这个 ClientMirror 就是**用客户端的方式读服务端**:
//      成帧 → 信封 → 按 msg_id 解码 → 组指令回发。
//      客户端 src/net 要做的事,它这里先做了一遍。
//
// ⚠️★ 它证明的**不是**客户端能跑,而是**服务端这一侧的线上契约是自洽的** ——
//    帧能对齐、信封能解、字段有值、上行指令能被采纳。
//    客户端仓那份用的是同一份 shared/wire(DR-TS9 乙案)⇒ 这三样它不必再验一遍,
//    它要验的是**表现层**。两边的分工由此变得清楚。

namespace {

// 一个只用 wire + IDL 的"客户端"。★ 刻意不碰 sa::net 的会话与传输 ——
//   那两样客户端有自己的实现(01 §12.1),而**成帧与信封是共享的那一份**。
struct ClientMirror {
  sa::net::FrameReader reader;

  // 已收到的消息计数,按 msg_id。
  std::map<std::uint32_t, int> seen;
  // 按到达顺序记下 msg_id —— 顺序本身是契约的一部分
  // (客户端得先知道自己是谁,才谈得上组指令)。
  std::vector<std::uint32_t> order;

  bool has_self = false;
  sa::domain::BattleSelfInfo self{};
  std::uint64_t battle_id = 0;
  std::uint32_t turn = 0;
  bool has_turn = false;

  // ★ 逐槽累计伤害 —— 1.4 验收要的"逐条正确消费"落到实处:
  //   不只是"收到了 BattleEvents",而是**事件体里的字段被读出来并用上了**。
  std::map<std::uint32_t, std::int64_t> damage_taken;
  int battle_events_msgs = 0;
  int damage_events = 0;

  // 喂一段服务端出站字节,把里面所有完整帧消费掉。
  void Feed(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return;
    REQUIRE(reader.Push(bytes.data(), bytes.size()));
    for (;;) {
      const std::uint8_t* p = nullptr;
      std::uint32_t len = 0;
      const sa::net::FrameStatus st = reader.Next(&p, &len);
      if (st == sa::net::FrameStatus::kNeedMore) break;
      REQUIRE(st == sa::net::FrameStatus::kOk);

      sa::net::EnvelopeView env;
      REQUIRE(sa::net::DecodeEnvelope(p, len, env));
      // ⚠️ body 必须在 Pop() 之前用掉或拷走(net_framing_test.cpp 卷首的教训)。
      Dispatch(env);
      reader.Pop();
    }
  }

  void Dispatch(const sa::net::EnvelopeView& env) {
    ++seen[env.msg_id];
    order.push_back(env.msg_id);
    sa::idl::Reader rd(env.body, env.body_len);

    switch (static_cast<sa::idl::MsgId>(env.msg_id)) {
      case sa::idl::MsgId::BattleSelfInfo: {
        decode(rd, self);
        REQUIRE(rd.ok());
        has_self = true;
        battle_id = self.battle_id;
        break;
      }
      case sa::idl::MsgId::BattleTurnBegin: {
        sa::domain::BattleTurnBegin b;
        decode(rd, b);
        REQUIRE(rd.ok());
        battle_id = b.battle_id;
        turn = b.turn;
        has_turn = true;
        break;
      }
      case sa::idl::MsgId::BattleEvents: {
        sa::domain::BattleEvents ev;
        decode(rd, ev);
        REQUIRE(rd.ok());
        ++battle_events_msgs;
        for (std::size_t i = 0; i < ev.events.size(); ++i) {
          const sa::domain::BattleEvent& e = ev.events[i];
          if (e.body_kind == sa::domain::BattleEvent::BodyKind::DAMAGE) {
            ++damage_events;
            damage_taken[e.body.damage.target] += -e.body.damage.hp_delta;
          }
        }
        break;
      }
      default:
        break;   // 握手回执等,计数已记下
    }
  }

  int count(sa::idl::MsgId id) const {
    const auto it = seen.find(static_cast<std::uint32_t>(id));
    return it == seen.end() ? 0 : it->second;
  }

  // ⚠️ 用它而不是 damage_taken[slot]:map::operator[] 不是 const,
  //    且会**插入**一个 0 —— 在断言里悄悄改被观测对象是很坏的习惯。
  std::int64_t damage_of(std::uint32_t slot) const {
    const auto it = damage_taken.find(slot);
    return it == damage_taken.end() ? 0 : it->second;
  }
};

sa::platform::ServerConfig DemoConfig() {
  const sa::platform::ConfigResult r = sa::platform::ParseConfig(R"({
    "protocol_version": 1,
    "log_level": "error",
    "tempo": { "tick_hz": 100, "battle_turn_interval_ms": 1000 },
    "demo_battle": { "enabled": true, "slot": 0 }
  })");
  REQUIRE(r.ok);
  return r.config;
}

// 跑一整场 demo。submit_commands = 客户端是否每回合出招。
// 返回 mirror(收到了什么)与最终的 turns_resolved。
struct DemoRun {
  ClientMirror mirror;
  std::uint32_t turns = 0;
  bool finished = false;
};

DemoRun RunDemo(bool submit_commands, std::uint64_t master_seed) {
  DemoRun out;
  sa::platform::ServerConfig cfg = DemoConfig();
  sa::platform::ManualClock clock{0};
  sa::platform::Logger logger{sa::platform::LogLevel::kError};
  sa::platform::RandomSource random{master_seed};
  sa::net::LoopbackTransport transport;
  World w(cfg, clock, logger, random, transport);

  const sa::net::ConnectionId id = transport.Connect();
  const std::vector<std::uint8_t> hs = HandshakeBytes(cfg.protocol_version);
  transport.Deliver(id, hs.data(), hs.size());
  w.Tick();
  out.mirror.Feed(transport.sent(id));
  transport.ClearSent(id);

  // 客户端此刻必须已经知道:我是谁、现在第几回合。
  REQUIRE(out.mirror.has_self);
  REQUIRE(out.mirror.has_turn);

  std::uint32_t last_submitted = 0xFFFFFFFFu;
  for (int i = 0; i < 60; ++i) {
    if (submit_commands && out.mirror.has_turn &&
        out.mirror.turn != last_submitted) {
      sa::domain::BattleCommand cmd{};
      cmd.battle_id = out.mirror.battle_id;
      cmd.turn = out.mirror.turn;
      cmd.command_kind = sa::domain::BattleCommand::CommandKind::ATTACK;
      cmd.command.attack.target =
          static_cast<std::uint32_t>(sa::rules::kSideOffset);
      std::vector<std::uint8_t> wire;
      REQUIRE(sa::net::EncodeFramed(0, cmd, wire));
      transport.Deliver(id, wire.data(), wire.size());
      last_submitted = out.mirror.turn;
    }

    clock.Advance(1000);
    w.Tick();
    out.mirror.Feed(transport.sent(id));
    transport.ClearSent(id);

    const BattleStats* st = w.stats(out.mirror.battle_id);
    REQUIRE(st != nullptr);
    if (st->finished) break;
  }

  const BattleStats* st = w.stats(out.mirror.battle_id);
  REQUIRE(st != nullptr);
  out.turns = st->turns_resolved;
  out.finished = st->finished;
  return out;
}

}  // namespace

// ★ 握手完就能拿到"我是谁" —— 缺这一步客户端组不出 BattleCommand
//   (它要 battle_id 与 turn,而这两样握手回执里都没有)。
TEST_CASE("demo 装配:握手即入场,且入场信息先于事件流到达") {
  const DemoRun run = RunDemo(false, 0x2026'09'06ull);

  CHECK(run.mirror.count(sa::idl::MsgId::HandshakeAccepted) == 1);
  CHECK(run.mirror.count(sa::idl::MsgId::BattleSelfInfo) == 1);
  CHECK(run.mirror.has_self);
  CHECK(run.mirror.self.slot == 0);
  CHECK(run.mirror.self.battle_id != 0);
  // ★ DR-BT5:cannot_act 取自 rules::CheckCanAct 这个唯一真源,
  //   健康的角色应当是"可行动"。
  CHECK(run.mirror.self.cannot_act ==
        sa::domain::CannotActReason::CANNOT_ACT_NONE);

  // ★★ 顺序:自我信息必须排在第一条 BattleEvents 之前。
  //    反过来的话客户端会先收到一堆不知道打给谁看的事件。
  std::size_t self_at = run.mirror.order.size();
  std::size_t first_events_at = run.mirror.order.size();
  for (std::size_t i = 0; i < run.mirror.order.size(); ++i) {
    const auto id = static_cast<sa::idl::MsgId>(run.mirror.order[i]);
    if (id == sa::idl::MsgId::BattleSelfInfo && self_at == run.mirror.order.size()) {
      self_at = i;
    }
    if (id == sa::idl::MsgId::BattleEvents &&
        first_events_at == run.mirror.order.size()) {
      first_events_at = i;
    }
  }
  CHECK(self_at < first_events_at);
}

// ★★ 性质 ①(见 world.cpp 的 MakeDemoField):客户端一条指令不发,
//    战斗也必须在有限回合内结束 —— 否则 demo 挂起时分不清是敌人打不动
//    还是事件流断了。
TEST_CASE("demo 战斗会自己打完 —— 客户端不出招也不会卡住") {
  const DemoRun run = RunDemo(false, 0x1111);
  CHECK(run.finished);
  CHECK(run.turns > 0);
  CHECK(run.mirror.battle_events_msgs > 0);
  CHECK(run.mirror.damage_events > 0);
  // 玩家没出招 ⇒ 敌方(slot 10)**一点伤害都不该吃到**。
  // ⚠️ 这条同时守着 L3 的既定语义:「无指令 ⇒ 本回合不行动」。
  CHECK(run.mirror.damage_of(
            static_cast<std::uint32_t>(sa::rules::kSideOffset)) == 0);
  // 而玩家自己在挨打。
  CHECK(run.mirror.damage_of(0) > 0);
}

// ★★★ 本文件里与 1.4 关系最直接的一条:**上行链路真的被采纳了**。
//    同一份战场、同一主种子,唯一的差别是客户端有没有把 BattleCommand 发上来。
//    ⇒ 敌方吃到伤害这件事,只可能来自那条上行指令。
TEST_CASE("端到端:客户端出招 ⇒ 敌方吃到伤害(上行链路的凭据)") {
  const DemoRun idle = RunDemo(false, 0x2222);
  const DemoRun active = RunDemo(true, 0x2222);

  const auto foe = static_cast<std::uint32_t>(sa::rules::kSideOffset);
  CHECK(idle.mirror.damage_of(foe) == 0);
  CHECK(active.mirror.damage_of(foe) > 0);

  CHECK(active.finished);
  // 出招方打得更快 —— 不是数值口味,是"指令确实进了结算"的可判定表现。
  CHECK(active.turns < idle.turns);
}

// ⚠️ 默认关。理由见 platform/api.h 的 DemoBattleConfig:
//   打开后握手即入场会把会话语义从 kAuthenticated 变成 kOnline,
//   那是**可观察的语义变化**,不能是默认行为。
TEST_CASE("demo 装配默认关 —— 握手完只是已认证,不会自己进战斗") {
  Fixture f;   // MakeConfig() 里没有 demo_battle 段
  REQUIRE_FALSE(f.config.demo_battle.enabled);

  const sa::net::ConnectionId id = f.transport.Connect();
  const std::vector<std::uint8_t> hs = HandshakeBytes(f.config.protocol_version);
  f.transport.Deliver(id, hs.data(), hs.size());
  f.world.Tick();

  CHECK(f.world.session_state(id) == sa::net::SessionState::kAuthenticated);

  ClientMirror m;
  m.Feed(f.transport.sent(id));
  CHECK(m.count(sa::idl::MsgId::HandshakeAccepted) == 1);
  CHECK(m.count(sa::idl::MsgId::BattleSelfInfo) == 0);
  CHECK(m.count(sa::idl::MsgId::BattleTurnBegin) == 0);
}

// ★ 槽位是配置项,且**真的**按它落位 —— 不是读进来就丢。
TEST_CASE("demo 槽位按配置落位") {
  sa::platform::ServerConfig cfg = DemoConfig();
  cfg.demo_battle.slot = 3;
  sa::platform::ManualClock clock{0};
  sa::platform::Logger logger{sa::platform::LogLevel::kError};
  sa::platform::RandomSource random{0x3333};
  sa::net::LoopbackTransport transport;
  World w(cfg, clock, logger, random, transport);

  const sa::net::ConnectionId id = transport.Connect();
  const std::vector<std::uint8_t> hs = HandshakeBytes(cfg.protocol_version);
  transport.Deliver(id, hs.data(), hs.size());
  w.Tick();

  ClientMirror m;
  m.Feed(transport.sent(id));
  REQUIRE(m.has_self);
  CHECK(m.self.slot == 3);
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
