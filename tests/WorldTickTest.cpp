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

#include "world/Api.h"

#include "model/Player.h"
#include "rules/CaptureItem.h"
#include "rules/Progression.h"
#include "rules/Status.h"
#include "support/ScriptedRandom.h"

#include <cstdint>
#include <map>
#include <vector>

using namespace SA::World;

namespace
{

SA::Platform::ServerConfig makeConfig()
{
	const SA::Platform::ConfigResult r = SA::Platform::parseConfig(R"({
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
SA::Rules::BattleField makeField()
{
	SA::Rules::BattleField f{};
	SA::Rules::Combatant &me = f.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.hp = 500;
	me.max_hp = 500;
	me.attack = 300;
	me.defense = 50;
	me.quick = 200;
	me.luck = 10;

	SA::Rules::Combatant &foe = f.at(SA::Rules::kSideOffset);
	foe.occupied = true;
	foe.kind = SA::Rules::CombatantKind::kEnemy;
	foe.slot = static_cast<std::uint8_t>(SA::Rules::kSideOffset);
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
struct Fixture
{
	SA::Platform::ServerConfig config = makeConfig();
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0xABCDEF};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};
};

} // namespace

TEST_CASE("tick 会推进,且不会自己停下来")
{
	Fixture f;
	for (int i = 0; i < 10; ++i)
		f.world.tick();
	CHECK(f.world.ticks() == 10);
	CHECK_FALSE(f.world.stopped());
}

// ★★ 本文件的核心用例。
TEST_CASE("战斗推进受节拍控制,不等于 tick 频率")
{
	Fixture f;
	const BattleId id = f.world.startBattle(makeField());
	REQUIRE(f.world.stats(id) != nullptr);

	// 间隔 1000 ms。在此之前无论 tick 多少次,都不该结算出一个回合。
	for (int i = 0; i < 500; ++i)
	{
		f.clock.advance(1); // 每 tick 1 ms ⇒ 累计 500 ms
		f.world.tick();
	}
	CHECK(f.world.stats(id)->turns_resolved == 0);
	CHECK(f.world.ticks() == 500);

	// 越过间隔 ⇒ 结算一个回合。
	f.clock.advance(600);
	f.world.tick();
	CHECK(f.world.stats(id)->turns_resolved == 1);

	// ⚠️ 再连 tick 也不该多结算 —— 下一回合要再等一个间隔。
	for (int i = 0; i < 100; ++i)
		f.world.tick();
	CHECK(f.world.stats(id)->turns_resolved == 1);
}

TEST_CASE("节拍是配置项:改间隔,回合数跟着变")
{
	Fixture f;
	f.config.tempo.battle_turn_interval_ms = 100;
	World w(f.config, f.clock, f.logger, f.random, f.transport);
	const BattleId id = w.startBattle(makeField());

	for (int i = 0; i < 10; ++i)
	{
		f.clock.advance(100);
		w.tick();
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
SA::Rules::BattleField makeFieldEnemyStrong()
{
	SA::Rules::BattleField f = makeField();
	SA::Rules::Combatant &foe = f.at(SA::Rules::kSideOffset);
	foe.level = 40;
	foe.attack = 400;
	foe.quick = 300;
	return f;
}

TEST_CASE("战斗会打完:一侧全灭 ⇒ finished")
{
	Fixture f;
	const BattleId id = f.world.startBattle(makeFieldEnemyStrong());
	for (int i = 0; i < 200 && !f.world.stats(id)->finished; ++i)
	{
		f.clock.advance(1000);
		f.world.tick();
	}
	CHECK(f.world.stats(id)->finished);
	CHECK(f.world.stats(id)->turns_resolved > 0);
	// 打完之后不再结算。
	const std::uint32_t at_end = f.world.stats(id)->turns_resolved;
	f.clock.advance(10000);
	f.world.tick();
	CHECK(f.world.stats(id)->turns_resolved == at_end);
}

// ★★ 可回放(01 §10):同一主种子 ⇒ 同一场战斗逐回合完全一致。
//    这是 00 §0 中 ③ 层「规则不可自证」最实际的补偿 ——
//    无法与原版比对,但**可以与自己的历史行为比对**。
TEST_CASE("可回放:同主种子的两次运行逐位一致")
{
	auto run = [](std::uint64_t master)
	{
		SA::Platform::ServerConfig cfg = makeConfig();
		SA::Platform::ManualClock clock{0};
		SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
		SA::Platform::RandomSource random{master};
		SA::Net::LoopbackTransport transport;
		World w(cfg, clock, logger, random, transport);
		const BattleId id = w.startBattle(makeFieldEnemyStrong());
		for (int i = 0; i < 200 && !w.stats(id)->finished; ++i)
		{
			clock.advance(1000);
			w.tick();
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
namespace
{

std::vector<std::uint8_t> handshakeBytes(std::uint32_t version)
{
	SA::Transport::HandshakeRequest req{};
	req.protocol_version = version;
	req.client_build.assign("test");
	std::vector<std::uint8_t> out;
	REQUIRE(SA::Net::encodeFramed(1, req, out));
	return out;
}

} // namespace

TEST_CASE("连上 → 握手 → 已认证")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	CHECK(f.world.sessionCount() == 1);
	CHECK(f.world.sessionState(id) == SA::Net::SessionState::kAnonymous);

	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick(); // 第 2 步:网络入站

	CHECK(f.world.sessionState(id) == SA::Net::SessionState::kAuthenticated);
	CHECK_FALSE(f.transport.sent(id).empty()); // HandshakeAccepted 已发出
}

TEST_CASE("版本不符 ⇒ 连接被关,但拒绝理由已经发出去了")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version + 1);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	CHECK(f.transport.closed(id));
	CHECK_FALSE(f.transport.sent(id).empty());
}

TEST_CASE("入场后能收到事件流")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	f.transport.clearSent(id);

	const BattleId battle = f.world.startBattle(makeField());
	REQUIRE(f.world.joinBattle(battle, id, 0));
	CHECK(f.world.sessionState(id) == SA::Net::SessionState::kOnline);

	f.clock.advance(2000);
	f.world.tick();
	CHECK(f.world.stats(battle)->turns_resolved == 1);
	// ★ 1.4 demo 的验收对象就是这一串字节:**事件流端到端一致**。
	CHECK_FALSE(f.transport.sent(id).empty());
}

TEST_CASE("没握手的连接不能入场")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = f.world.startBattle(makeField());
	CHECK_FALSE(f.world.joinBattle(battle, id, 0));
}

TEST_CASE("入场参数的边界")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	const BattleId battle = f.world.startBattle(makeField());
	CHECK_FALSE(f.world.joinBattle(9999, id, 0));     // 战斗不存在
	CHECK_FALSE(f.world.joinBattle(battle, id, 200)); // 槽号越界
	CHECK(f.world.joinBattle(battle, id, 0));
	CHECK_FALSE(f.world.joinBattle(battle, id, 1)); // 重复入场
}

TEST_CASE("断线会把会话从战斗里摘掉")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	const BattleId battle = f.world.startBattle(makeField());
	REQUIRE(f.world.joinBattle(battle, id, 0));

	f.transport.close(id);
	CHECK(f.world.sessionCount() == 0);
	// 战斗本身照常推进(1.5 没有"人走了就散场"的规则,那属玩法)
	f.clock.advance(2000);
	f.world.tick();
	CHECK(f.world.stats(battle)->turns_resolved == 1);
}

// ⚠️ 01 §11.2 的完整停服流程在 1.5 做不了(没有 storage、没有跨模块请求)。
//    这条只验能做的那部分:拒绝之后确实停下来了。
TEST_CASE("停服请求 ⇒ 关闭全部连接并停止")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	f.world.requestShutdown();
	f.world.tick();
	CHECK(f.world.stopped());
	CHECK(f.transport.closed(id));
	// 停了之后 tick 不再推进
	const std::uint64_t at_stop = f.world.ticks();
	f.world.tick();
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

namespace
{

// 一个只用 wire + IDL 的"客户端"。★ 刻意不碰 SA::Net 的会话与传输 ——
//   那两样客户端有自己的实现(01 §12.1),而**成帧与信封是共享的那一份**。
struct ClientMirror
{
	SA::Net::FrameReader reader;

	// 已收到的消息计数,按 msg_id。
	std::map<std::uint32_t, int> seen;
	// 按到达顺序记下 msg_id —— 顺序本身是契约的一部分
	// (客户端得先知道自己是谁,才谈得上组指令)。
	std::vector<std::uint32_t> order;

	bool has_self = false;
	SA::Domain::BattleSelfInfo self{};
	std::uint64_t battle_id = 0;
	std::uint32_t turn = 0;
	bool has_turn = false;

	// ★ 逐槽累计伤害 —— 1.4 验收要的"逐条正确消费"落到实处:
	//   不只是"收到了 BattleEvents",而是**事件体里的字段被读出来并用上了**。
	std::map<std::uint32_t, std::int64_t> damage_taken;
	int battle_events_msgs = 0;
	int damage_events = 0;

	// 喂一段服务端出站字节,把里面所有完整帧消费掉。
	void feed(const std::vector<std::uint8_t> &bytes)
	{
		if (bytes.empty())
			return;
		REQUIRE(reader.push(bytes.data(), bytes.size()));
		for (;;)
		{
			const std::uint8_t *p = nullptr;
			std::uint32_t len = 0;
			const SA::Net::FrameStatus st = reader.next(&p, &len);
			if (st == SA::Net::FrameStatus::kNeedMore)
				break;
			REQUIRE(st == SA::Net::FrameStatus::kOk);

			SA::Net::EnvelopeView env;
			REQUIRE(SA::Net::decodeEnvelope(p, len, env));
			// ⚠️ body 必须在 Pop() 之前用掉或拷走(net_framing_test.cpp 卷首的教训)。
			dispatch(env);
			reader.pop();
		}
	}

	void dispatch(const SA::Net::EnvelopeView &env)
	{
		++seen[env.msg_id];
		order.push_back(env.msg_id);
		SA::IDL::Reader rd(env.body, env.body_len);

		switch (static_cast<SA::IDL::MsgId>(env.msg_id))
		{
		case SA::IDL::MsgId::BattleSelfInfo:
		{
			decode(rd, self);
			REQUIRE(rd.ok());
			has_self = true;
			battle_id = self.battle_id;
			break;
		}
		case SA::IDL::MsgId::BattleTurnBegin:
		{
			SA::Domain::BattleTurnBegin b;
			decode(rd, b);
			REQUIRE(rd.ok());
			battle_id = b.battle_id;
			turn = b.turn;
			has_turn = true;
			break;
		}
		case SA::IDL::MsgId::BattleEvents:
		{
			SA::Domain::BattleEvents ev;
			decode(rd, ev);
			REQUIRE(rd.ok());
			++battle_events_msgs;
			for (std::size_t i = 0; i < ev.events.size(); ++i)
			{
				const SA::Domain::BattleEvent &e = ev.events[i];
				if (e.body_kind == SA::Domain::BattleEvent::BodyKind::DAMAGE)
				{
					++damage_events;
					damage_taken[e.body.damage.target] += -e.body.damage.hp_delta;
				}
			}
			break;
		}
		default:
			break; // 握手回执等,计数已记下
		}
	}

	int count(SA::IDL::MsgId id) const
	{
		const auto it = seen.find(static_cast<std::uint32_t>(id));
		return it == seen.end() ? 0 : it->second;
	}

	// ⚠️ 用它而不是 damage_taken[slot]:map::operator[] 不是 const,
	//    且会**插入**一个 0 —— 在断言里悄悄改被观测对象是很坏的习惯。
	std::int64_t damageOf(std::uint32_t slot) const
	{
		const auto it = damage_taken.find(slot);
		return it == damage_taken.end() ? 0 : it->second;
	}
};

SA::Platform::ServerConfig demoConfig()
{
	const SA::Platform::ConfigResult r = SA::Platform::parseConfig(R"({
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
struct DemoRun
{
	ClientMirror mirror;
	std::uint32_t turns = 0;
	bool finished = false;
};

DemoRun runDemo(bool submit_commands, std::uint64_t master_seed)
{
	DemoRun out;
	SA::Platform::ServerConfig cfg = demoConfig();
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{master_seed};
	SA::Net::LoopbackTransport transport;
	World w(cfg, clock, logger, random, transport);

	const SA::Net::ConnectionId id = transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(cfg.protocol_version);
	transport.deliver(id, hs.data(), hs.size());
	w.tick();
	out.mirror.feed(transport.sent(id));
	transport.clearSent(id);

	// 客户端此刻必须已经知道:我是谁、现在第几回合。
	REQUIRE(out.mirror.has_self);
	REQUIRE(out.mirror.has_turn);

	std::uint32_t last_submitted = 0xFFFFFFFFu;
	for (int i = 0; i < 60; ++i)
	{
		if (submit_commands && out.mirror.has_turn &&
		    out.mirror.turn != last_submitted)
		{
			SA::Domain::BattleCommand cmd{};
			cmd.battle_id = out.mirror.battle_id;
			cmd.turn = out.mirror.turn;
			cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
			cmd.command.attack.target =
			    static_cast<std::uint32_t>(SA::Rules::kSideOffset);
			std::vector<std::uint8_t> wire;
			REQUIRE(SA::Net::encodeFramed(0, cmd, wire));
			transport.deliver(id, wire.data(), wire.size());
			last_submitted = out.mirror.turn;
		}

		clock.advance(1000);
		w.tick();
		out.mirror.feed(transport.sent(id));
		transport.clearSent(id);

		const BattleStats *st = w.stats(out.mirror.battle_id);
		REQUIRE(st != nullptr);
		if (st->finished)
			break;
	}

	const BattleStats *st = w.stats(out.mirror.battle_id);
	REQUIRE(st != nullptr);
	out.turns = st->turns_resolved;
	out.finished = st->finished;
	return out;
}

} // namespace

// ★ 握手完就能拿到"我是谁" —— 缺这一步客户端组不出 BattleCommand
//   (它要 battle_id 与 turn,而这两样握手回执里都没有)。
TEST_CASE("demo 装配:握手即入场,且入场信息先于事件流到达")
{
	const DemoRun run = runDemo(false, 0x2026'09'06ull);

	CHECK(run.mirror.count(SA::IDL::MsgId::HandshakeAccepted) == 1);
	CHECK(run.mirror.count(SA::IDL::MsgId::BattleSelfInfo) == 1);
	CHECK(run.mirror.has_self);
	CHECK(run.mirror.self.slot == 0);
	CHECK(run.mirror.self.battle_id != 0);
	// ★ DR-BT5:cannot_act 取自 Rules::CheckCanAct 这个唯一真源,
	//   健康的角色应当是"可行动"。
	CHECK(run.mirror.self.cannot_act ==
	      SA::Domain::CannotActReason::CANNOT_ACT_NONE);

	// ★★ 顺序:自我信息必须排在第一条 BattleEvents 之前。
	//    反过来的话客户端会先收到一堆不知道打给谁看的事件。
	std::size_t self_at = run.mirror.order.size();
	std::size_t first_events_at = run.mirror.order.size();
	for (std::size_t i = 0; i < run.mirror.order.size(); ++i)
	{
		const auto id = static_cast<SA::IDL::MsgId>(run.mirror.order[i]);
		if (id == SA::IDL::MsgId::BattleSelfInfo && self_at == run.mirror.order.size())
		{
			self_at = i;
		}
		if (id == SA::IDL::MsgId::BattleEvents &&
		    first_events_at == run.mirror.order.size())
		{
			first_events_at = i;
		}
	}
	CHECK(self_at < first_events_at);
}

// ★★ 性质 ①(见 world.cpp 的 MakeDemoField):客户端一条指令不发,
//    战斗也必须在有限回合内结束 —— 否则 demo 挂起时分不清是敌人打不动
//    还是事件流断了。
TEST_CASE("demo 战斗会自己打完 —— 客户端不出招也不会卡住")
{
	const DemoRun run = runDemo(false, 0x1111);
	CHECK(run.finished);
	CHECK(run.turns > 0);
	CHECK(run.mirror.battle_events_msgs > 0);
	CHECK(run.mirror.damage_events > 0);
	// 玩家没出招 ⇒ 敌方(slot 10)**一点伤害都不该吃到**。
	// ⚠️ 这条同时守着 L3 的既定语义:「无指令 ⇒ 本回合不行动」。
	CHECK(run.mirror.damageOf(
	          static_cast<std::uint32_t>(SA::Rules::kSideOffset)) == 0);
	// 而玩家自己在挨打。
	CHECK(run.mirror.damageOf(0) > 0);
}

// ★★★ 本文件里与 1.4 关系最直接的一条:**上行链路真的被采纳了**。
//    同一份战场、同一主种子,唯一的差别是客户端有没有把 BattleCommand 发上来。
//    ⇒ 敌方吃到伤害这件事,只可能来自那条上行指令。
TEST_CASE("端到端:客户端出招 ⇒ 敌方吃到伤害(上行链路的凭据)")
{
	const DemoRun idle = runDemo(false, 0x2222);
	const DemoRun active = runDemo(true, 0x2222);

	const auto foe = static_cast<std::uint32_t>(SA::Rules::kSideOffset);
	CHECK(idle.mirror.damageOf(foe) == 0);
	CHECK(active.mirror.damageOf(foe) > 0);

	CHECK(active.finished);
	// 出招方打得更快 —— 不是数值口味,是"指令确实进了结算"的可判定表现。
	CHECK(active.turns < idle.turns);
}

// ★★ M.3(DR-DT9):**demo 玩家的三围不再是手填值,而是四维的推导像。**
//    钉法:idle 场景玩家必然被打死 ⇒ 它累计吃到的伤害不可能少于自己的 max_hp,
//    而那个 max_hp 由 `deriveBaseStats` 给出 —— 本用例**直接引用同一个函数**,
//    ⇒ 有人把 `makeDemoField` 改回硬编码血量,这一条立刻红。
//    ⚠️ 断言取 `>=` 而不是相等:最后一击会打过头(伤害不按剩余血量截断),
//      而"打过头多少"是随机项,不可判定 ⇒ 只断言可判定的那一半。
TEST_CASE("M.3:demo 玩家的三围来自属性推导,不是硬编码(DR-DT9)")
{
	const DemoRun idle = runDemo(false, 0x1111);
	REQUIRE(idle.finished);

	// makeDemoField 里 me 的四维(World.cpp)—— 两处必须同源,不同源就是这条用例的意义。
	const SA::Rules::DerivedStats me = SA::Rules::deriveBaseStats(8000, 30000, 4000, 20000);
	REQUIRE(me.max_hp > 0); // 先证推导本身没退化成 0(捕获宠那种残缺)

	CHECK(idle.mirror.damageOf(0) >= static_cast<std::uint32_t>(me.max_hp));
}

// ⚠️ 默认关。理由见 platform/api.h 的 DemoBattleConfig:
//   打开后握手即入场会把会话语义从 kAuthenticated 变成 kOnline,
//   那是**可观察的语义变化**,不能是默认行为。
TEST_CASE("demo 装配默认关 —— 握手完只是已认证,不会自己进战斗")
{
	Fixture f; // MakeConfig() 里没有 demo_battle 段
	REQUIRE_FALSE(f.config.demo_battle.enabled);

	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	CHECK(f.world.sessionState(id) == SA::Net::SessionState::kAuthenticated);

	ClientMirror m;
	m.feed(f.transport.sent(id));
	CHECK(m.count(SA::IDL::MsgId::HandshakeAccepted) == 1);
	CHECK(m.count(SA::IDL::MsgId::BattleSelfInfo) == 0);
	CHECK(m.count(SA::IDL::MsgId::BattleTurnBegin) == 0);
}

// ★ 槽位是配置项,且**真的**按它落位 —— 不是读进来就丢。
TEST_CASE("demo 槽位按配置落位")
{
	SA::Platform::ServerConfig cfg = demoConfig();
	cfg.demo_battle.slot = 3;
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0x3333};
	SA::Net::LoopbackTransport transport;
	World w(cfg, clock, logger, random, transport);

	const SA::Net::ConnectionId id = transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(cfg.protocol_version);
	transport.deliver(id, hs.data(), hs.size());
	w.tick();

	ClientMirror m;
	m.feed(transport.sent(id));
	REQUIRE(m.has_self);
	CHECK(m.self.slot == 3);
}
// ★ 迟到的指令不该被采纳:02 §1.3 的取向是不靠"下一个到达的包就是回复",
//   这里同理 —— 上一回合的决定在新回合里执行是错的。
TEST_CASE("指令必须指向当前回合")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	const BattleId battle = f.world.startBattle(makeField());
	REQUIRE(f.world.joinBattle(battle, id, 0));

	SA::Domain::BattleCommand stale{};
	stale.battle_id = battle;
	stale.turn = 999; // 不是当前回合
	stale.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
	stale.command.attack.target = static_cast<std::uint32_t>(SA::Rules::kSideOffset);

	std::vector<std::uint8_t> wire;
	REQUIRE(SA::Net::encodeFramed(0, stale, wire));
	f.transport.deliver(id, wire.data(), wire.size());
	f.world.tick();

	// 会话层接受了它(消息合法),world 层按回合号丢弃 —— 连接不该被关。
	CHECK_FALSE(f.transport.closed(id));
}

// ═══════════════════════════════════════════════════════════════════════════
//  批次 M.1:L2 实体族接线(01 §13 欠债 20)
// ═══════════════════════════════════════════════════════════════════════════
//
// ★★ 这一组验的是**世界写真的落下去了**,而不是"事件发出去了"。
//    欠债 20 的要害原话:「地基绿而运行时不接,ctest 一样 12/12 全过」——
//    所以每条用例都断言一个**池 / 主人槽 / 战场上可观察的后果**,不是断言事件条数。

namespace
{

// 一个捕获必定判定通过的战场:己方 1 人(高魅力)、敌方 N 个残血可捕获目标。
//
// ★ 为什么魅力给 200:`rollCapture` 的 work 会被 `min(…, 99)` 夹住(源码 :3863),
//   而 `rand(1,100) < 99` ⇒ **98% 成功**。魅力再高也不会到 100% ——
//   ⚠️ 原版没有下限钳位也没有上限 100,照抄即此。⇒ 用例靠**固定主种子**取得确定性,
//     与「可回放:同主种子的两次运行逐位一致」那条同一手法。
//   ★ 主种子改了这几条要重新确认,这是有意的代价:比"多打几回合直到成功"好 ——
//     后者是概率性用例,而偶发红是 00 §10.4 里最难归因的一类。
SA::Rules::BattleField makeCapturableField(int enemy_count)
{
	SA::Rules::BattleField f{};
	SA::Rules::Combatant &me = f.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.hp = 5000; // 打不死,免得战斗在捕获完之前结束
	me.max_hp = 5000;
	me.attack = 10;
	me.defense = 500;
	me.quick = 200;
	me.luck = 10;
	me.charm = 200; // ★ 捕获的乘性主因子(§6.2:× charm / 50)

	for (int i = 0; i < enemy_count; ++i)
	{
		SA::Rules::Combatant &foe = f.at(SA::Rules::kSideOffset + i);
		foe.occupied = true;
		foe.kind = SA::Rules::CombatantKind::kEnemy;
		foe.slot = static_cast<std::uint8_t>(SA::Rules::kSideOffset + i);
		foe.level = 5;
		foe.hp = 1; // 残血 ⇒ Df_HpPer 接近上限(二次式,源码 :3852)
		foe.max_hp = 40;
		foe.attack = 1;
		foe.defense = 1;
		foe.quick = 30;
		foe.luck = 1;
		foe.mods.capturable = true; // ★ 前置门 ②(源码 :3830)
	}
	return f;
}

// 握手 + 入场,返回战斗号。★ 会话就绪时 World 会建 Player 实体(批次 M.1)。
BattleId joinCapturable(Fixture &f, SA::Net::ConnectionId id, int enemy_count)
{
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	const BattleId battle = f.world.startBattle(makeCapturableField(enemy_count));
	REQUIRE(f.world.joinBattle(battle, id, 0));
	return battle;
}

// 发一条捕获指令并推进一个回合。
void captureTurn(Fixture &f, SA::Net::ConnectionId id, BattleId battle,
                 int target_slot)
{
	SA::Domain::BattleCommand cmd{};
	cmd.battle_id = battle;
	cmd.turn = f.world.battleField(battle)->turn;
	cmd.command_kind = SA::Domain::BattleCommand::CommandKind::CAPTURE;
	cmd.command.capture.target = static_cast<std::uint32_t>(target_slot);
	f.world.onBattleCommand(id, cmd);
	f.clock.advance(2000);
	f.world.tick();
}

} // namespace

TEST_CASE("L2:会话就绪即有 Player 实体,断线即释放")
{
	Fixture f;
	CHECK(f.world.playerCount() == 0);
	CHECK(f.world.playerCaptureCount(1) == -1); // ★ 没有实体是 −1,不是 0

	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	CHECK(f.world.playerCount() == 1);
	CHECK(f.world.playerCaptureCount(id) == 0); // 有实体了,计数确实是 0

	f.transport.close(id);
	CHECK(f.world.playerCount() == 0);
	CHECK(f.world.playerCaptureCount(id) == -1);
}

TEST_CASE("I.1:道具池 + 背包槽的观察面 —— 本批恒 0(地基已接、写入链路未接)")
{
	// ★ 背包 L2 是"地基先落、写入链路后接"的分层(捕获扣道具 / 掉落 / 使用是后三批)。
	//   本批唯一能断言的就是**观察面存在且读得到 0** —— 没有它,「池挂上了 World 吗」
	//   与欠债 20 那条静默(地基绿而运行时不接)无从区分。
	// ⚠️ 这条断言将来会被写入批次改写成"非 0":接掉落 / 捡起后,itemCount 会涨。
	Fixture f;
	CHECK(f.world.itemCount() == 0);             // 池挂在 World 上、初始空
	CHECK(f.world.playerItemSlotsUsed(1) == -1); // ★ 没有实体是 −1,不是 0(同 pet)

	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	CHECK(f.world.itemCount() == 0);             // ★ 有玩家了,但没有写入者 ⇒ 池仍恒 0
	CHECK(f.world.playerItemSlotsUsed(id) == 0); // 有实体了,背包确实是空的(0 而非 -1)

	f.transport.close(id);
	CHECK(f.world.playerItemSlotsUsed(id) == -1);
}

TEST_CASE("L2:捕获成功 ⇒ 宠物进池、挂进主人槽、计数 +1、目标离场")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinCapturable(f, id, 1);

	REQUIRE(f.world.petCount() == 0);
	REQUIRE(f.world.playerPetSlotsUsed(id) == 0);

	captureTurn(f, id, battle, SA::Rules::kSideOffset);

	// ★★ 四个后果一起断言 —— 少任何一个都说明那条链没接上:
	CHECK(f.world.petCount() == 1);             // ① 宠物进了池
	CHECK(f.world.playerPetSlotsUsed(id) == 1); // ② 挂进了主人的槽
	CHECK(f.world.playerCaptureCount(id) == 1); // ③ 捕获计数(源码 :3543)
	// ④ 目标离场(源码 :3546 BATTLE_Exit)—— occupied=false 而**不是** dead
	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK_FALSE(fld->at(SA::Rules::kSideOffset).occupied);
	CHECK_FALSE(fld->at(SA::Rules::kSideOffset).dead);
}

TEST_CASE("L2:捕获出的宠物字段 —— 拿得到的拷了,拿不到的是登记在案的零")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinCapturable(f, id, 1);
	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	REQUIRE(f.world.petCount() == 1);

	// ⚠️★ 这条用例的作用是**钉住残缺本身**,免得下一个人以为字段填上了:
	//    被捕目标在战场里只是 Rules::Combatant(战斗输入子集),
	//    没有 vital / str / tough / dex,也没有名字 ⇒ 敌人侧缺 L2 实体(Pet.h 卷首)。
	//    这不是 bug,是登记在案的缺口;补齐要等 EntityKind::kEnemy 族 + L4 内容导入。
	//
	// ★ 池内实体没有对外观察面(有意如此:L2 字段不该经 world 的公开 API 逐个漏出去),
	//   所以这里只断言"能拿到的那部分确实被拷了"这个可观察后果:
	//   宠物存在 ⇒ 说明 createPetFromCombatant 走完了整条门 + 拷贝 + 挂槽。
	CHECK(f.world.petCount() == 1);
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
}

TEST_CASE("L2:宠物槽满是捕获的门 —— 第 6 只抓不进来,且世界一个字节都没动")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	// 6 个可捕获目标:前 5 个填满宠物槽,第 6 个用来撞门。
	const BattleId battle = joinCapturable(f, id, 6);

	for (int i = 0; i < static_cast<int>(SA::Model::kMaxPetHave); ++i)
		captureTurn(f, id, battle, SA::Rules::kSideOffset + i);

	REQUIRE(f.world.petCount() == 5);
	REQUIRE(f.world.playerPetSlotsUsed(id) == 5);
	REQUIRE(f.world.playerCaptureCount(id) == 5);

	// ★★ 第 6 次:判定照样通过(L3 不知道槽满),但世界写在**门**上失败。
	captureTurn(f, id, battle, SA::Rules::kSideOffset + 5);

	CHECK(f.world.petCount() == 5);             // 没有第 6 只宠物
	CHECK(f.world.playerCaptureCount(id) == 5); // ★ 计数也没加(源码里它在门之后)
	// ★ 目标**留场** —— 源码 flg 改回 0 ⇒ BATTLE_Exit 不执行。
	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(SA::Rules::kSideOffset + 5).occupied);
}

TEST_CASE("L2:断线释放主人时,它的宠物一并回池")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinCapturable(f, id, 2);
	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	captureTurn(f, id, battle, SA::Rules::kSideOffset + 1);
	REQUIRE(f.world.petCount() == 2);

	f.transport.close(id);
	// ⚠️★ 少了这一步,Pet 池只增不减,而**没有任何一处会报错** ——
	//    表现是跑够久之后"捕获突然开始失败",那时离原因已经很远。
	CHECK(f.world.petCount() == 0);
	CHECK(f.world.playerCount() == 0);
}

TEST_CASE("L2:capture_bonus 无条件清零 —— 判定失败那条路径也清")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	// ★ 让判定**必定失败**:目标不带可捕获标记(前置门 ② 不过)⇒ flags == 0。
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	SA::Rules::BattleField field = makeCapturableField(1);
	field.at(SA::Rules::kSideOffset).mods.capturable = false;
	field.at(0).mods.capture_bonus = 40; // 假装某道具/技能设过它
	const BattleId battle = f.world.startBattle(field);
	REQUIRE(f.world.joinBattle(battle, id, 0));

	captureTurn(f, id, battle, SA::Rules::kSideOffset);

	// ★★ 源码 :3510 的 `CHAR_setWorkInt(attacker, CHAR_WORKMODCAPTURE, 0)` 在
	//    **flg 判定之后、创建宠物之前无条件执行** ⇒ 抓失败也清零。
	//    ⚠️ 此前这一条被记成"在成功分支里、且应推迟" —— 位置与条件都错了。
	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(0).mods.capture_bonus == 0);
	// 目标留场(判定失败无世界写),且没有宠物产生。
	CHECK(fld->at(SA::Rules::kSideOffset).occupied);
	CHECK(f.world.petCount() == 0);
}

TEST_CASE("骑宠 HP:pet_hp_delta 落到 ride_hp,并夹在 0 以上")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	// 己方带骑宠且防御低 ⇒ 敌方每回合都能打出分摊伤害(§3.6,DR-BT2 修正式)。
	SA::Rules::BattleField field{};
	SA::Rules::Combatant &me = field.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.hp = 100000; // 主人打不死,让回合一直进行
	me.max_hp = 100000;
	me.attack = 1;
	me.defense = 10;
	me.quick = 1; // 敌方先动
	me.luck = 0;
	me.has_ride = true;
	me.ride_defense = 10;
	me.ride_hp = 50;
	me.ride_max_hp = 50;

	SA::Rules::Combatant &foe = field.at(SA::Rules::kSideOffset);
	foe.occupied = true;
	foe.kind = SA::Rules::CombatantKind::kEnemy;
	foe.slot = static_cast<std::uint8_t>(SA::Rules::kSideOffset);
	foe.level = 30;
	foe.hp = 100000;
	foe.max_hp = 100000;
	foe.attack = 400;
	foe.defense = 10;
	foe.quick = 300;
	foe.luck = 5;

	const BattleId battle = f.world.startBattle(field);
	REQUIRE(f.world.joinBattle(battle, id, 0));

	const int before = f.world.battleField(battle)->at(0).ride_hp;
	REQUIRE(before == 50);

	// 打若干回合。★★ 断言两件事:骑宠 HP **真的减了**,且**从不为负**。
	//   ⚠️ 前者此前不成立 —— `pet_hp_delta` 一直被丢掉,而注释说的理由
	//     (「Combatant 还没有骑宠的独立 HP 槽」)在写下它时就已经是假的。
	bool dropped = false;
	for (int i = 0; i < 12; ++i)
	{
		f.clock.advance(2000);
		f.world.tick();
		const SA::Rules::BattleField *fld = f.world.battleField(battle);
		REQUIRE(fld != nullptr);
		CHECK(fld->at(0).ride_hp >= 0); // ★ 夹取:每一回合都要成立
		if (fld->at(0).ride_hp < before)
			dropped = true;
	}
	CHECK(dropped);
}

// ═══════════════════════════════════════════════════════════════════════════
//  批次 M.2:战场态宠物入场 / 离场(enterPetToField / exitPetFromField)
// ═══════════════════════════════════════════════════════════════════════════
//
// ★★ 这一组直接单元测入场**机制**(纯函数,喂 field + Pet)—— 不经 World、不跑战斗。
//    理由:"选哪只宠"要读 Player::default_pet,而设它的写者(PET_OUT 指令)还没做
//    ⇒ 本批只验"给定一只宠,投影 + 站位 + 三门"这条机制;端到端(joinBattle 自动带宠)
//    留给换宠指令批次。applyEvents 对宠位的写在 M.1「骑宠 HP」一类里已覆盖
//    (它对任意 occupied 槽无差别),这里不重复,也不拿零三围的宠去跑 L3 结算。

namespace
{

// 一只有战斗意义的 Pet:hp / 等级 / 四属性都非零,便于断言投影确实搬了值。
SA::Model::Pet makeLivePet()
{
	SA::Model::Pet p{};
	p.hp = 120;
	p.mp = 30;
	p.max_mp = 40;
	p.level = 15;
	p.luck = 7;
	// ★ 四属性给四个**不同**的值,好让"按具名下标 vs 按位置"的错误暴露出来。
	p.earth = 11;
	p.water = 22;
	p.fire = 33;
	p.wind = 44;
	return p;
}

// 一个主人在指定槽的战场。
SA::Rules::BattleField makeFieldWithOwnerAt(int owner_slot)
{
	SA::Rules::BattleField f{};
	SA::Rules::Combatant &me = f.at(owner_slot);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = static_cast<std::uint8_t>(owner_slot);
	me.level = 20;
	me.hp = 500;
	me.max_hp = 500;
	return f;
}

} // namespace

TEST_CASE("M.2:默认宠投影进 slots[主人+5],可投影字段搬对")
{
	SA::Rules::BattleField f = makeFieldWithOwnerAt(0);
	const SA::Model::Pet pet = makeLivePet();

	REQUIRE(enterPetToField(f, 0, pet));

	const SA::Rules::Combatant &p = f.at(SA::Rules::kBattlePlayerMax); // slot 5
	CHECK(p.occupied);
	CHECK(p.kind == SA::Rules::CombatantKind::kPet);
	CHECK(p.slot == static_cast<std::uint8_t>(SA::Rules::kBattlePlayerMax));
	CHECK(p.hp == 120);
	CHECK(p.mp == 30);
	CHECK(p.max_mp == 40);
	CHECK(p.level == 15);
	CHECK(p.luck == 7);
}

TEST_CASE("M.2:四属性按具名下标映射,绝不按位置(顺序陷阱)")
{
	SA::Rules::BattleField f = makeFieldWithOwnerAt(0);
	const SA::Model::Pet pet = makeLivePet(); // earth11 water22 fire33 wind44

	REQUIRE(enterPetToField(f, 0, pet));
	const SA::Rules::Combatant &p = f.at(SA::Rules::kBattlePlayerMax);

	// ★ 按 Element 具名下标断言。若实现按位置拷(pet.fire 是第 3 个字段,
	//   而 Element::kFire==2、kEarth==0),这里会红。
	CHECK(p.elements[static_cast<int>(SA::Rules::Element::kEarth)] == 11);
	CHECK(p.elements[static_cast<int>(SA::Rules::Element::kWater)] == 22);
	CHECK(p.elements[static_cast<int>(SA::Rules::Element::kFire)] == 33);
	CHECK(p.elements[static_cast<int>(SA::Rules::Element::kWind)] == 44);
}

TEST_CASE("M.3:四维无来源的宠物 ⇒ 推导出的三围仍为 0(DR-DT9,欠债 23 下一环)")
{
	SA::Rules::BattleField f = makeFieldWithOwnerAt(0);
	const SA::Model::Pet pet = makeLivePet(); // 四维默认 0(捕获宠 / 一般宠当前无来源)

	REQUIRE(enterPetToField(f, 0, pet));
	const SA::Rules::Combatant &p = f.at(SA::Rules::kBattlePlayerMax);

	// ⚠️★ complianceParameter 已移植(DR-DT9),但四维全 0 是它的不动点 ⇒ 三围仍 0。
	//    这不再是「未移植」,而是「四维无非 0 来源」(欠债 23 的下一环)—— 钉住它,免得下一个人
	//    以为「接了推导宠物就有战力」。真正参战还需非 0 四维来源(敌人模板 / 成长率消费)。
	CHECK(p.attack == 0);
	CHECK(p.defense == 0);
	CHECK(p.quick == 0);
	CHECK(p.max_hp == 0);
	// ★ hp 未被夹取(见 enterPetToField 注释):四维 0 ⇒ max_hp 0,若夹取则「叫出即死」。
	//   ⚠️ 断言值取 makeLivePet 的 hp(120),它就是「未被夹取」的唯一证据 ——
	//     写成任何别的数都会让这一条既不测夹取也不测投影。
	CHECK(p.hp == 120);
}

TEST_CASE("M.3:非 0 四维的宠物 ⇒ enterPetToField 路径上推导出非 0 三围(DR-DT9)")
{
	SA::Rules::BattleField f = makeFieldWithOwnerAt(0);
	SA::Model::Pet pet = makeLivePet();
	// 给一组非 0 四维(与 rules_progression「非对称」用例同值,手算可核)。
	pet.vital = 1000;
	pet.str = 5000;
	pet.tough = 1000;
	pet.dex = 2000;

	REQUIRE(enterPetToField(f, 0, pet));
	const SA::Rules::Combatant &p = f.at(SA::Rules::kBattlePlayerMax);

	// ★ 与 deriveBaseStats(1000,5000,1000,2000) 逐位一致(RulesProgressionTest 第 4 例):
	//   attack 53 / defense 17 / quick 20 / max_hp 120 —— 验证推导确实跑在接线路径上。
	CHECK(p.attack == 53);
	CHECK(p.defense == 17);
	CHECK(p.quick == 20);
	CHECK(p.max_hp == 120);
}

TEST_CASE("M.2:死宠(hp<=0)不入场")
{
	SA::Rules::BattleField f = makeFieldWithOwnerAt(0);
	SA::Model::Pet pet = makeLivePet();
	pet.hp = 0;

	CHECK_FALSE(enterPetToField(f, 0, pet));
	CHECK_FALSE(f.at(SA::Rules::kBattlePlayerMax).occupied);
}

TEST_CASE("M.2:宠位已被占 ⇒ 入场失败,原单位不被覆盖")
{
	SA::Rules::BattleField f = makeFieldWithOwnerAt(0);
	SA::Rules::Combatant &squatter = f.at(SA::Rules::kBattlePlayerMax);
	squatter.occupied = true;
	squatter.kind = SA::Rules::CombatantKind::kEnemy;
	squatter.hp = 99;

	const SA::Model::Pet pet = makeLivePet();
	CHECK_FALSE(enterPetToField(f, 0, pet));
	// ★ 原单位原样保留 —— 入场失败不该踩掉已在场的单位。
	CHECK(f.at(SA::Rules::kBattlePlayerMax).kind == SA::Rules::CombatantKind::kEnemy);
	CHECK(f.at(SA::Rules::kBattlePlayerMax).hp == 99);
}

TEST_CASE("M.2:owner 必须在玩家段 —— 宠位 / 越界都不带宠")
{
	SA::Rules::BattleField f{};
	const SA::Model::Pet pet = makeLivePet();
	// owner=5 是己方宠位(kBattlePlayerMax..kSideOffset-1)⇒ 拒绝。
	CHECK_FALSE(enterPetToField(f, SA::Rules::kBattlePlayerMax, pet));
	CHECK_FALSE(enterPetToField(f, SA::Rules::kSlotCount, pet)); // 越界
	CHECK_FALSE(enterPetToField(f, -1, pet));
}

TEST_CASE("M.2:敌方主人的宠站到敌方宠段(主人+5,不串半场)")
{
	// 敌方主人 slot 10。
	SA::Rules::BattleField f = makeFieldWithOwnerAt(SA::Rules::kSideOffset);
	const SA::Model::Pet pet = makeLivePet();

	REQUIRE(enterPetToField(f, SA::Rules::kSideOffset, pet));
	// 敌方宠位 = 10 + 5 = 15,仍在敌方半场。
	const int pet_slot = SA::Rules::kSideOffset + SA::Rules::kBattlePlayerMax;
	CHECK(f.at(pet_slot).occupied);
	CHECK(f.at(pet_slot).kind == SA::Rules::CombatantKind::kPet);
}

TEST_CASE("M.2:exitPetFromField 撤下宠物 —— 清占位但不置 dead")
{
	SA::Rules::BattleField f = makeFieldWithOwnerAt(0);
	const SA::Model::Pet pet = makeLivePet();
	REQUIRE(enterPetToField(f, 0, pet));
	REQUIRE(f.at(SA::Rules::kBattlePlayerMax).occupied);

	exitPetFromField(f, 0);
	CHECK_FALSE(f.at(SA::Rules::kBattlePlayerMax).occupied);
	CHECK_FALSE(f.at(SA::Rules::kBattlePlayerMax).dead); // ★ 撤下不是战死
}

// ═══════════════════════════════════════════════════════════════════════════
//  批次 DR-BT21:换宠指令 PET_OUT / PET_IN(端到端世界写)
// ═══════════════════════════════════════════════════════════════════════════
//
// ★★ 这一组验**世界写真的落下去了**(同 M.1 捕获组):经 World 公开接口发换宠指令、
//    tick 跑 resolveTurn + applyEvents,断言战场宠位 / default_pet 这些**可观察后果** ——
//    applyEvents 是内部函数,不直接测它,测它的效果。
// ★ 换宠要有宠可换 ⇒ 先用捕获抓一只进 pets[](沿用 M.1 的 joinCapturable/captureTurn
//   固定主种子手法),再叫出 / 收回。enemy_count≥2:抓 1 只后敌方仍有存活,战斗不结束。

namespace
{

// 发一条 PET_OUT(叫出第 pet_slot 槽宠)并推进一个回合。
void petOutTurn(Fixture &f, SA::Net::ConnectionId id, BattleId battle, int pet_slot)
{
	SA::Domain::BattleCommand cmd{};
	cmd.battle_id = battle;
	cmd.turn = f.world.battleField(battle)->turn;
	cmd.command_kind = SA::Domain::BattleCommand::CommandKind::PET_OUT;
	cmd.command.pet_out.pet_slot = static_cast<std::uint32_t>(pet_slot);
	f.world.onBattleCommand(id, cmd);
	f.clock.advance(2000);
	f.world.tick();
}

// 发一条 PET_IN(收回当前出战宠,无参)并推进一个回合。
void petInTurn(Fixture &f, SA::Net::ConnectionId id, BattleId battle)
{
	SA::Domain::BattleCommand cmd{};
	cmd.battle_id = battle;
	cmd.turn = f.world.battleField(battle)->turn;
	cmd.command_kind = SA::Domain::BattleCommand::CommandKind::PET_IN;
	f.world.onBattleCommand(id, cmd);
	f.clock.advance(2000);
	f.world.tick();
}

} // namespace

TEST_CASE("DR-BT21:PET_OUT 叫出宠物入场 slots[主人+5] 并写 default_pet")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinCapturable(f, id, 2);
	captureTurn(f, id, battle, SA::Rules::kSideOffset); // 抓进 pets[0]
	REQUIRE(f.world.petCount() == 1);
	REQUIRE(f.world.playerDefaultPet(id) == -1); // 还没叫出

	petOutTurn(f, id, battle, 0);

	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(SA::Rules::kBattlePlayerMax).occupied); // 主人 0 + 5
	CHECK(fld->at(SA::Rules::kBattlePlayerMax).kind == SA::Rules::CombatantKind::kPet);
	CHECK(f.world.playerDefaultPet(id) == 0);
}

TEST_CASE("DR-BT21:PET_IN 收回出战宠 ⇒ 宠位清空 + default_pet=-1")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinCapturable(f, id, 2);
	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	petOutTurn(f, id, battle, 0);
	REQUIRE(f.world.battleField(battle)->at(SA::Rules::kBattlePlayerMax).occupied);
	REQUIRE(f.world.playerDefaultPet(id) == 0);

	petInTurn(f, id, battle);

	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK_FALSE(fld->at(SA::Rules::kBattlePlayerMax).occupied);
	CHECK_FALSE(fld->at(SA::Rules::kBattlePlayerMax).dead); // 撤下不是战死
	CHECK(f.world.playerDefaultPet(id) == -1);
}

TEST_CASE("DR-BT21:叫出到已占宠位失败 ⇒ default_pet 不变(不复刻源码反推 bug)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinCapturable(f, id, 3);
	captureTurn(f, id, battle, SA::Rules::kSideOffset);     // pets[0]
	captureTurn(f, id, battle, SA::Rules::kSideOffset + 1); // pets[1]
	REQUIRE(f.world.petCount() == 2);

	petOutTurn(f, id, battle, 0); // slots[5] = pets[0]
	REQUIRE(f.world.playerDefaultPet(id) == 0);

	// 宠位已被 pets[0] 占 ⇒ enterPetToField 门③ 失败。★ 关键:default_pet **仍是 0**,
	//   不是 1 —— 源码靠「入场后 DEFAULTPET<0」反推会误判成功并置 1(DR-BT20 陷阱①),
	//   我们用 enterPetToField 真实返回值,失败即不改 default_pet。
	petOutTurn(f, id, battle, 1);
	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(SA::Rules::kBattlePlayerMax).occupied);
	CHECK(f.world.playerDefaultPet(id) == 0); // 没被改成 1
}

TEST_CASE("DR-BT21:叫出空槽 ⇒ 叫不出,宠位空、default_pet 不变")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinCapturable(f, id, 2);
	captureTurn(f, id, battle, SA::Rules::kSideOffset); // 只有 pets[0]

	petOutTurn(f, id, battle, 4); // pets[4] 空 ⇒ no_pet,不入场

	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK_FALSE(fld->at(SA::Rules::kBattlePlayerMax).occupied);
	CHECK(f.world.playerDefaultPet(id) == -1);
}

TEST_CASE("DR-BT21:joinBattle 自动带出出战宠(default_pet 跨战斗)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle1 = joinCapturable(f, id, 2);
	captureTurn(f, id, battle1, SA::Rules::kSideOffset);
	petOutTurn(f, id, battle1, 0);
	REQUIRE(f.world.playerDefaultPet(id) == 0); // 出战宠已定

	// ★ 同一会话进入新战斗:joinBattle 读 default_pet 自动带宠(本批激活的链路)。
	//   ⚠️ demo 玩家 default_pet 恒 -1 ⇒ demo 不触发;这里靠先 PET_OUT 设好它来验证。
	const BattleId battle2 = f.world.startBattle(makeCapturableField(1));
	REQUIRE(f.world.joinBattle(battle2, id, 0));

	const SA::Rules::BattleField *fld2 = f.world.battleField(battle2);
	REQUIRE(fld2 != nullptr);
	CHECK(fld2->at(SA::Rules::kBattlePlayerMax).occupied);
	CHECK(fld2->at(SA::Rules::kBattlePlayerMax).kind == SA::Rules::CombatantKind::kPet);
}

// ═══════════════════════════════════════════════════════════════════════════
//  批次 M.4b:敌人 L2 实体族接线(01 §13 欠债 23 + 25)
// ═══════════════════════════════════════════════════════════════════════════
//
// ★★ 这一组的判据不是"函数返回对了",而是**欠债 23 与 25 的关闭条件**:
//    · 欠债 25:`rollSpawnStats` 有真实调用方,且**有观察面能断言它被调过**
//      (`enemyCount()` / `battleEnemyAt()`)—— 立案原话是「地基绿而运行时不接,
//      ctest 一样全过」,所以每条用例都断言一个池 / 战场上可观察的后果;
//    · 欠债 23:**捕获出的宠物四维非 0**,并且那些数**等于敌人实体里的那一份**。
//      ⇒ "四维有源"这件事被逐字段比对,不是靠"看起来不是 0"。

namespace
{

// 用例集专用 rng 存根 ⇒ `tests/support/ScriptedRandom.h`(**唯一一份**)。
// ★★ 此前三个用例集各带一份拷贝,而实测三份已经漂了(`randMod` 的退化分支自相矛盾、
//    `calls()` 只有两份有)⇒ 见该头文件卷首与 DR-BT23。

// 「乌力」= `enemybase1.txt` **第 1 行的全部实测列**(2026-09-08 核,名字列按 GBK 解)。
//
// ★ 与 `rules_progression` 的 `kWuli` 是同一行的两个视图:那边只要 L3 用的 6 列,
//   这边要 world 侧完整的 `EnemyTemplate`。⇒ 两处的 6 列必须一致,
//   ⚠️ 不一致的表现是"同一只怪在两个测试里四维不同",而没有一处会报错。
//
// 逐列出处(1-based 列号 = 7 + E_T_* 枚举序,`06` §3.5):
//   c9 lvup=4.50 · c8 init=10 · c10-13 基数=[20,12,15,25] · c14 MODAI=150 ·
//   c15 GET=11 · c16 EARTH=80 · c17 WATER=20 · c18 FIRE=0 · c19 WIND=0 · c37 IMG=100250
// ⚠️★ `capturable` **不在这张表里** —— 它来自敌人表 `enemy1.txt` 的 `ENEMY_PETFLG`
//    (源码 `enemy.c:1165` 读 `*(p + ENEMY_PETFLG)`)。模板表 c38 也叫 E_T_PETFLG(=1),
//    但 `ENEMY_createEnemy` **读的不是它** ⇒ 由 `EnemyEncounter` 给,见下方 fixture。
EnemyTemplate makeWuliTemplate()
{
	EnemyTemplate t{};
	t.stats = SA::Rules::SpawnTemplate{4.50, 10, 20, 12, 15, 25};
	t.mod_ai = 150;
	t.capture_difficulty = 11; // ★ 实测最常见的取值(276/1053 行)
	t.earth = 80;
	t.water = 20;
	t.fire = 0;
	t.wind = 0;
	t.image = 100250;
	REQUIRE(t.name.assign("乌力"));
	return t;
}

// ── 敌人表 fixture(批次 M.5)= `enemy1.txt` 的三行**实测真值** ────────────────
//
// ★★ 三行**共用模板 tempno=1**(就是上面那只乌力)—— 这不是为了省事,而是这张表
//    最要紧的性质:实测 `enemy1.txt` 里 tempno=1 出现在 **28 行**不同配置上,
//    等级区间与可捕性各不相同 ⇒ "一个模板 × 多个敌人表行"是常态,不是特例。
//    ★ 佐证:第 75 行的 `ENEMY_NAME` 是 `sai_w_001_2/3乌力` —— 配表人把等级区间
//      写进了名字,而生成路径根本不读那一列(见 `EnemyTemplate::name` 那条)。
//
// ⚠️★★ **选行本身要避开三类"有隐藏行为"的 `ENEMY_ID`**,否则将来接了那些机制,
//    这几条用例的期望值会**无声地**变掉(2026-09-09 回源码核出,两处形参名都在骗人):
//      ① `ENEMY_RandomChange(enemyindex, tempno)` —— ★ 形参名叫 `tempno`,而 `:1152`
//         传的实参是 **`ENEMY_ID`** ⇒ 判据六段区间 564-580 · 739-750 · 895-906 ·
//         655-720 · 859-894 · 907-940 都是 **ID** 区间(改图号 / 四属 / 宠技)。
//      ② `ENEMY_RandomEnemyArray(e_array, ...)` —— ★ 形参名叫 `e_array`(行下标),
//         而 `:1384` 传的也是 **`ENEMY_ID`** ⇒ 945-956 · 964-969 会被整行换掉。
//    ⇒ 下面三个 id(9 / 142 / 1309)**都不在那八段区间内**,已逐段核过。
//    ★ 教训与 M.4b ④ 同族但方向相反:那次是"手填数据掩盖了公式值域",
//      这次是**真数据也要选对** —— 挑到一行带隐藏机制的,真实性反而成了陷阱。

// `enemy1.txt` **第 5 行**:id=9 · tempno=1 · lv 1-1 · petflg=1。
// ★ 固定等级(lv_min == lv_max,实测这种占 1142/2154 行 = 53%)+ 可捕。
EnemyEncounter makeWuliEncounterFixedLv1()
{
	EnemyEncounter e{};
	e.enemy_id = 9;
	e.temp_no = 1;
	e.lv_min = 1;
	e.lv_max = 1;
	e.capturable = true;
	return e;
}

// `enemy1.txt` **第 76 行**:id=142 · tempno=1 · lv 3-5 · petflg=1。
// ★ 真区间(宽度 2,实测最常见的非零宽度 = 291 行)+ 可捕。
EnemyEncounter makeWuliEncounterRange3to5()
{
	EnemyEncounter e{};
	e.enemy_id = 142;
	e.temp_no = 1;
	e.lv_min = 3;
	e.lv_max = 5;
	e.capturable = true;
	return e;
}

// `enemy1.txt` **第 941 行**:id=1309 · tempno=1 · lv 60-80 · petflg=**0**。
//
// ★★ 它是"同一模板、不同敌人表配置 ⇒ 可捕性不同"的**真数据**对照 ——
//    与上面两行同模板(tempno=1)而 `capturable` 相反。⇒ 断言 `capturable`
//    取自 `enc` 而不是 `tmpl` 时不必手造数据。
// ★ 顺带给出一个宽区间(60-80),摇号断言用它比 1-1 更有区分力。
EnemyEncounter makeWuliEncounterWideNoCapture()
{
	EnemyEncounter e{};
	e.enemy_id = 1309;
	e.temp_no = 1;
	e.lv_min = 60;
	e.lv_max = 80;
	e.capturable = false;
	return e;
}

// 只有玩家的战场:高魅力(捕获乘性主因子)、高防低攻(打不死也不被打死)。
// ★ 敌方槽**留空**,由 `spawnEnemyToField` 填 —— 这正是本组要验的路径。
SA::Rules::BattleField makePlayerOnlyField()
{
	SA::Rules::BattleField f{};
	SA::Rules::Combatant &me = f.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.hp = 5000;
	me.max_hp = 5000;
	me.attack = 1; // ★ 不要打死敌人 —— 本组要抓活的
	me.defense = 500;
	me.quick = 200;
	me.luck = 10;
	me.charm = 200;
	return f;
}

// 握手 + 入场 + 据模板刷 n 只敌人,返回战斗号。
BattleId joinWithSpawnedEnemies(Fixture &f, SA::Net::ConnectionId id, int n,
                                std::int32_t level)
{
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	const BattleId battle = f.world.startBattle(makePlayerOnlyField());
	REQUIRE(f.world.joinBattle(battle, id, 0));
	for (int i = 0; i < n; ++i)
	{
		REQUIRE(f.world.spawnEnemyToField(
		    battle, static_cast<std::uint8_t>(SA::Rules::kSideOffset + i),
		    makeWuliTemplate(), makeWuliEncounterFixedLv1(), level));
	}
	return battle;
}

} // namespace

// ── spawnEnemy:纯生成函数,逐值可算 ─────────────────────────────────────

// 脚本:前 4 次 `rand(0,4)` 全取 2 ⇒ 抖动 0;后 10 次 `rand(0,3)` 全取 0 ⇒ 10 点全给 vital。
// ⇒ coef = (18−1)×4.5 + 10 = **86.5**,基数 [30,12,15,25]
//   ⇒ 四维 = [2595, 1038, 1297(1297.5 截断), 2162(2162.5 截断)]
//   ⇒ 三围 = attack 15 · defense 17 · quick 21 · max_hp 148,**hp = max_hp(满血入场)**
TEST_CASE("M.4b:spawnEnemy 逐值 —— 四维 / 满血 / 评级 / 两张表的列都落对")
{
	ScriptedRandom rng({2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SA::Model::Enemy e =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 18, rng,
	               SA::Rules::RulesConfig{});

	// ★★ 四维非 0 且逐值 —— 这就是欠债 23 找的那个"来源"(源码 :1067-1070)。
	CHECK(e.vital == 2595);
	CHECK(e.str == 1038);
	CHECK(e.tough == 1297);
	CHECK(e.dex == 2162);

	// 成长率取「+10 之前」的基数(源码 :1052-1056 在撒点循环之前)。
	CHECK(e.growth_vital == 20);
	CHECK(e.growth_str == 12);
	CHECK(e.growth_tough == 15);
	CHECK(e.growth_dex == 25);

	// ★ 满血入场 = 推导出的 max_hp(源码 :1153 推导 → :1159 `HP = WORKMAXHP`)。
	const SA::Rules::DerivedStats d =
	    SA::Rules::deriveBaseStats(e.vital, e.str, e.tough, e.dex);
	CHECK(d.max_hp == 148);
	CHECK(e.hp == d.max_hp);

	// ⚠️ MP 恒 0 —— 源码从不写它,默认模板 `player` 里也是 0(Enemy.h 记明)。
	CHECK(e.mp == 0);
	CHECK(e.max_mp == 0);

	// 评级:模板基数和 72 ⇒ 末档 5。★ 与摇号无关(用的是未扰动的模板基数)。
	CHECK(e.pet_rank == 5);

	// 模板表那几列。
	CHECK(e.mod_ai == 150);
	CHECK(e.base_image == 100250);
	CHECK(e.origin_image == 100250); // 源码 :1023-1024 两槽同值
	CHECK(e.capture_difficulty == 11);
	CHECK(std::string(e.name.c_str()) == "乌力");
	// ★ 遇敌表那一列。
	CHECK(e.capturable);

	// ⚠️★ `variable_ai` 恒 0(源码 :1076)—— 它与"幸运"同槽,捕获会以幸运的名义
	//    把这个 0 拷进宠物。★ Enemy **没有** luck 字段,理由见 Enemy.h 卷首。
	CHECK(e.variable_ai == 0);

	// ★ pet_id = 模板号(源码 :1200 `CHAR_PETID = *(tp + E_T_TEMPNO)`,捕获扣道具批次)。
	//   makeWuliTemplate 未设 temp_no ⇒ 默认 0 ⇒ pet_id 0(不在 NeedEnemy 表内)。
	CHECK(e.pet_id == 0);

	// ★ rng 消耗恰好 14 次 —— `spawnEnemy` 自己不摇任何数(见 Api.h 声明处)。
	CHECK(rng.calls() == 14);
}

TEST_CASE("捕获扣道具:spawnEnemy 把模板号落进 pet_id(源码 enemy.c:1200)")
{
	// ★ 匹配键 = CHAR_PETID = E_T_TEMPNO。把模板号设成 NeedEnemy 表里有的 524,
	//   断言它原样落进 Enemy.pet_id ⇒ 捕获扣道具据它查表(见 CaptureItem.h)。
	EnemyTemplate t = makeWuliTemplate();
	t.temp_no = 524; // NeedEnemy 表:524 → {2456}
	ScriptedRandom rng({2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SA::Model::Enemy e =
	    spawnEnemy(t, makeWuliEncounterFixedLv1(), 18, rng, SA::Rules::RulesConfig{});
	CHECK(e.pet_id == 524);
	CHECK(SA::Rules::isNeedCaptureItem(e.pet_id) == 0); // 表第 0 行
}

TEST_CASE("M.4b:等级是入参 —— 同模板不同等级 ⇒ 四维按 coef 成比例(enemy.c:1040)")
{
	const std::vector<int> script{2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	ScriptedRandom r1(script), r18(script);
	const SA::Model::Enemy lo =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 1, r1,
	               SA::Rules::RulesConfig{});
	const SA::Model::Enemy hi =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 18, r18,
	               SA::Rules::RulesConfig{});

	// level 1 ⇒ coef = init_num = 10;level 18 ⇒ 86.5 ⇒ 基数 30 各乘之。
	CHECK(lo.vital == 300);
	CHECK(hi.vital == 2595);
	CHECK(lo.level == 1);
	CHECK(hi.level == 18);
	// ★ 成长率与等级无关(它在 PARAM_CAL 之前就定了)。
	CHECK(lo.growth_vital == hi.growth_vital);
	// ★ 评级也与等级无关(模板基数的函数)。
	CHECK(lo.pet_rank == hi.pet_rank);
}

// ── 等级摇号:敌人表的 LV_MIN/LV_MAX(批次 M.5)────────────────────────────
//
// ★★ 本组的整体判据:M.4b 把 `level` 与 `capturable` 记成"入参,待敌人表移植"。
//    ⇒ 关闭它的凭据不是"加了个函数",而是**这两个值现在真的从敌人表来**,
//      且"从入参来"那一支**没有被推翻**(源码里两支都在)。

TEST_CASE("M.5:baselevel <= 0 ⇒ 据敌人表区间摇号(enemy.c:1034)")
{
	// 区间 3-5 ⇒ 闭区间三个取值。★ 脚本第一个数被等级摇号吃掉,
	//   后面 14 个才是 `rollSpawnStats` 的 ⇒ 顺序即语义(见 spawnEnemy 声明处)。
	SUBCASE("摇出下界")
	{
		ScriptedRandom rng({3, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
		const SA::Model::Enemy e =
		    spawnEnemy(makeWuliTemplate(), makeWuliEncounterRange3to5(), 0, rng,
		               SA::Rules::RulesConfig{});
		CHECK(e.level == 3);
		// ★★ rng 共 **15** 次:1 次摇等级 + 14 次摇四维(M.4b 那条断言的是 14)。
		CHECK(rng.calls() == 15);
	}
	SUBCASE("摇出上界 —— 闭区间,取得到 lv_max")
	{
		ScriptedRandom rng({5, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
		const SA::Model::Enemy e =
		    spawnEnemy(makeWuliTemplate(), makeWuliEncounterRange3to5(), 0, rng,
		               SA::Rules::RulesConfig{});
		CHECK(e.level == 5);
	}
}

TEST_CASE("M.5★:baselevel > 0 ⇒ 覆盖摇号,且一次 rng 都不为它花(两支都是原版)")
{
	// ⚠️★ 用**宽区间 60-80** 的那行做对照:若实现误走摇号,等级会落在 60-80,
	//    与期望的 7 差得一眼可见 ⇒ 这条断言有区分力,不是同义反复。
	ScriptedRandom rng({2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SA::Model::Enemy e =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterWideNoCapture(), 7, rng,
	               SA::Rules::RulesConfig{});
	CHECK(e.level == 7);
	// ★★ 14 次 —— 摇号那一次**没有发生** ⇒ 同种子下 baselevel 分支的四维
	//    与摇号分支不同,而这是顺序造成的、不是缺陷(见 spawnEnemy 声明处)。
	CHECK(rng.calls() == 14);
}

TEST_CASE("M.5:固定等级的行(lv_min == lv_max)照样摇一次 —— 序列长度不能因数据而变")
{
	// ★★ 实测 1142/2154 行是 `lv_min == lv_max`。这条钉的是**不能"优化"掉那次摇号**:
	//    结果恒等于 lv_min,但 rng 必须被消耗 —— 否则固定等级的怪与区间等级的怪
	//    走出不同长度的随机序列 ⇒ 回放对不上,而两者的四维看起来都"正常"。
	ScriptedRandom rng({1, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SA::Model::Enemy e =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 0, rng,
	               SA::Rules::RulesConfig{});
	CHECK(e.level == 1);
	CHECK(rng.calls() == 15); // ★ 15 而不是 14
}

TEST_CASE("M.5★★:摇出的等级真的喂进四维生成 —— 逐值,不是只看非 0")
{
	// ★★ **这条用例是反向验证逼出来的**(2026-09-09):原先此处只断言 `vital > 0`,
	//    而注入「把 `baselevel` 而不是已决定的 `out.level` 喂给 `rollSpawnStats`」
	//    **一条都没红**。⚠️ 算一下就知道为什么:
	//      coef = (level − 1) × lvup + init ⇒ level 0 时 = −4.5 + 10 = **5.5**,
	//    仍是正数 ⇒ 四维只是**偏小**(165 而不是 840),不是负数、更不是 0。
	//    ⇒ 「> 0」这种量级断言对这类错误完全没有区分力,必须逐值。
	//    ★ 教训与 M.4b ④ 同族但更细:那次是"手填数据掩盖值域",
	//      这次是**断言的形状掩盖了错误**——「非 0」和「对」之间差着一个数量级。
	//
	// 脚本:等级摇 5(区间 3-5 的上界)· 4 次抖动取 2(⇒ 0)· 10 点全给 vital。
	// ⇒ coef = (5−1) × 4.5 + 10 = **28**,基数 [30, 12, 15, 25]
	ScriptedRandom rng({5, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SA::Model::Enemy e =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterRange3to5(), 0, rng,
	               SA::Rules::RulesConfig{});

	CHECK(e.level == 5);
	CHECK(e.vital == 840); // 30 × 28 ⇒ 若误用 baselevel(0) 会是 165
	CHECK(e.str == 336);   // 12 × 28
	CHECK(e.tough == 420); // 15 × 28
	CHECK(e.dex == 700);   // 25 × 28
}

TEST_CASE("M.5★★:capturable 来自敌人表,不是模板表 —— 同模板两行,一可捕一不可捕")
{
	// ★★ 全真数据对照(`enemy1.txt` 第 5 行 vs 第 941 行):两行 `temp_no` 都是 1
	//    ⇒ 喂给 `spawnEnemy` 的 `tmpl` **完全相同**,唯一的差别在 `enc`。
	//    ⇒ 若实现回头去读 `tmpl`(模板表 c38 那个同名的 `E_T_PETFLG`),这两条必有一条红。
	const std::vector<int> script{2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	ScriptedRandom ra(script), rb(script);
	const SA::Model::Enemy yes =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 18, ra,
	               SA::Rules::RulesConfig{});
	const SA::Model::Enemy no =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterWideNoCapture(), 18, rb,
	               SA::Rules::RulesConfig{});

	CHECK(yes.capturable);
	CHECK_FALSE(no.capturable);
	// ★ 其余一切相同 —— 坐实差别只来自那一列(同模板 + 同 baselevel + 同脚本)。
	CHECK(yes.vital == no.vital);
	CHECK(yes.capture_difficulty == no.capture_difficulty); // ★ 难度跟模板走
	CHECK(yes.level == no.level);
}

TEST_CASE("M.5:载入期两条归一 —— 实测数据一次都不触发,只能手造数据钉住")
{
	// ⚠️★★ 这一条用的是**手造数据**,而这是有判据的:实测 `enemy1.txt` 2154 行里
	//    `lv_min == 0` 与 `lv_min > lv_max` **各 0 行** ⇒ 真数据永远走不到这两条分支。
	//    ★ 而它们**必须移植**,因为 `Rules::Random::rand` 的契约把 `lo <= hi`
	//      的责任交给调用方 ⇒ 缺了归一就是把实现定义行为放进运行期。
	//    ⇒ 这正是 M.4b ④ 那条教训的反面:真数据能暴露公式值域,
	//      但**防御性分支只有手造数据能覆盖**,两者都要有。
	SUBCASE("归一①:lv_min == 0 ⇒ 固定为 lv_max,不是 RAND(0, lv_max)")
	{
		EnemyEncounter enc = makeWuliEncounterFixedLv1();
		enc.lv_min = 0;
		enc.lv_max = 18;
		// ★ 脚本给 0:若实现漏了归一而摇 `RAND(0,18)`,会取到 0 ⇒ 这条转红。
		ScriptedRandom rng({0, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
		CHECK(rollEncounterLevel(enc, rng) == 18);
	}
	SUBCASE("归一②:lv_min > lv_max ⇒ 交换后再摇")
	{
		EnemyEncounter enc = makeWuliEncounterFixedLv1();
		enc.lv_min = 20;
		enc.lv_max = 10;
		ScriptedRandom rng({10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
		// ★ 归一后区间是 [10, 20];脚本取 10 ⇒ 结果 10。
		//   ⚠️ 若不归一,`rand(20, 10)` 的行为由实现定义 —— 这条断言的价值不在
		//     "结果是 10",而在**它有一个确定的期望值**。
		CHECK(rollEncounterLevel(enc, rng) == 10);
	}
	SUBCASE("归一① + ②:两条同时适用(lv_min == 0 且 lv_max 也是 0)")
	{
		EnemyEncounter enc = makeWuliEncounterFixedLv1();
		enc.lv_min = 0;
		enc.lv_max = 0;
		ScriptedRandom rng({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
		// ★ 归一后 [0,0] ⇒ 恒 0。⚠️ 这是**原版行为**:0 级敌人在原版里是可配的,
		//   `PARAM_CAL` 的 `level-1` 会取到 −1 ⇒ 四维为负。不替它修(不猜)。
		CHECK(rollEncounterLevel(enc, rng) == 0);
	}
}

TEST_CASE("M.5:World 侧摇号端到端 —— 摇出的等级落在区间内且可观察")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	const BattleId battle = f.world.startBattle(makePlayerOnlyField());
	REQUIRE(f.world.joinBattle(battle, id, 0));

	// ★ 走真 rng(战斗自己的),不是脚本 ⇒ 断言的是**区间**而不是具体值。
	//   ⚠️ 这一条要的正是"不知道会摇出几"—— 那才是摇号路径真的接上了。
	REQUIRE(f.world.spawnEnemyToField(
	    battle, static_cast<std::uint8_t>(SA::Rules::kSideOffset),
	    makeWuliTemplate(), makeWuliEncounterRange3to5(), 0));

	const SA::Model::Enemy *e = f.world.battleEnemyAt(
	    battle, static_cast<std::uint8_t>(SA::Rules::kSideOffset));
	REQUIRE(e != nullptr);
	CHECK(e->level >= 3);
	CHECK(e->level <= 5);
	// ★★ 四维非 0 —— 摇号出来的等级真的喂进了 `rollSpawnStats`。
	//    ⚠️★ 但**这条断言的区分力很弱**:误传 `baselevel`(0)算出的四维照样是正数
	//      (coef = 5.5),只是小一个数量级 ⇒ 逐值那条在上面单开一个用例,
	//      本处保留量级断言是因为它走的是**真 rng**(摇出几不确定,逐值无从写)。
	CHECK(e->vital > 0);
	CHECK(e->hp > 0);
	CHECK(e->capturable); // 该行 petflg=1
}

// ── enterEnemyToField:投影 + 两道门 ───────────────────────────────────

TEST_CASE("M.4b:enterEnemyToField 投影 —— 三围由四维推出,捕获两列第一次有真数据")
{
	ScriptedRandom rng({2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SA::Model::Enemy e =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 18, rng,
	               SA::Rules::RulesConfig{});

	SA::Rules::BattleField f{};
	REQUIRE(enterEnemyToField(f, SA::Rules::kSideOffset, e));
	const SA::Rules::Combatant &c = f.at(SA::Rules::kSideOffset);

	CHECK(c.occupied);
	CHECK(c.kind == SA::Rules::CombatantKind::kEnemy);
	CHECK(c.slot == SA::Rules::kSideOffset);
	CHECK(c.level == 18);
	CHECK(c.attack == 15);
	CHECK(c.defense == 17);
	CHECK(c.quick == 21);
	CHECK(c.max_hp == 148);
	CHECK(c.hp == 148); // 满血

	// ⚠️★★ 四属**按具名下标**核 —— 模板是 地 80 / 水 20 / 火 0 / 风 0。
	//    按位置拷会把 80 写进 `elements[0]` 恰好也对(kEarth==0),但水火会互换 ⇒
	//    这里逐个具名断言才接得住。
	CHECK(c.elements[static_cast<int>(SA::Rules::Element::kEarth)] == 80);
	CHECK(c.elements[static_cast<int>(SA::Rules::Element::kWater)] == 20);
	CHECK(c.elements[static_cast<int>(SA::Rules::Element::kFire)] == 0);
	CHECK(c.elements[static_cast<int>(SA::Rules::Element::kWind)] == 0);

	// ★★ `Combatant.h` 里「1.5 无敌人数值表 ⇒ 调用方按 30 兜底」那条,到此兑现。
	CHECK(c.mods.capturable);
	CHECK(c.mods.capture_difficulty == 11);
	CHECK(c.mods.capture_difficulty != SA::Rules::kCaptureDifficultyDefault);

	// ⚠️★ `luck` 留 0 —— 敌人没有幸运这个属性(同槽异义),不是"忘了拷"。
	CHECK(c.luck == 0);
	// ⚠️ DR-BT11 的两个免疫标志仍 false:它们在原版**没有模板列**,是新造的数据面。
	CHECK_FALSE(c.mods.immune_critical);
	CHECK_FALSE(c.mods.immune_knockback);
}

TEST_CASE("M.4b:enterEnemyToField 两道门 —— 宠位 / 越界 / 已占槽都不入场")
{
	ScriptedRandom rng({2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const SA::Model::Enemy e =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 18, rng,
	               SA::Rules::RulesConfig{});
	SA::Rules::BattleField f{};

	// 门 ①:宠位(每 side 的后 5 槽)与越界。
	CHECK_FALSE(enterEnemyToField(f, SA::Rules::kBattlePlayerMax, e));
	CHECK_FALSE(enterEnemyToField(f, SA::Rules::kSideOffset + SA::Rules::kBattlePlayerMax, e));
	CHECK_FALSE(enterEnemyToField(f, SA::Rules::kSlotCount, e));
	CHECK_FALSE(enterEnemyToField(f, -1, e));

	// ★ 玩家半场的玩家段**允许** —— 站位由调用方决定,不由本函数猜(见 Api.h)。
	CHECK(enterEnemyToField(f, 1, e));

	// 门 ②:已占槽不覆盖。
	CHECK_FALSE(enterEnemyToField(f, 1, e));
	CHECK(f.at(1).occupied);
}

// ── spawnEnemyToField:World 侧接线(欠债 25 的关闭判据)──────────────────

TEST_CASE("M.4b:spawnEnemyToField 建 L2 实体并可观察(欠债 25)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	CHECK(f.world.enemyCount() == 0);

	const BattleId battle = joinWithSpawnedEnemies(f, id, 2, 18);

	// ★★ 三个后果一起断言 —— 少任何一个都说明"生成公式有了调用方"这句话不成立:
	CHECK(f.world.enemyCount() == 2); // ① 实体真的进了池
	const SA::Model::Enemy *e0 =
	    f.world.battleEnemyAt(battle, SA::Rules::kSideOffset);
	REQUIRE(e0 != nullptr); // ② 槽 → 实体的映射建起来了
	CHECK(e0->vital > 0);   // ③ 四维**非 0** —— rollSpawnStats 真的被调了

	// ④ 战场投影与实体一致:三围是从**那一份**四维推出来的。
	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	const SA::Rules::DerivedStats d =
	    SA::Rules::deriveBaseStats(e0->vital, e0->str, e0->tough, e0->dex);
	CHECK(fld->at(SA::Rules::kSideOffset).attack == d.attack);
	CHECK(fld->at(SA::Rules::kSideOffset).max_hp == d.max_hp);
	CHECK(fld->at(SA::Rules::kSideOffset).hp == d.max_hp);

	// ⑤ 四维落在**手算的边界**内:基数 b ∈ [20−2, 20+2+10] ⇒ vital = 86.5 × b。
	//    ★ rng 由战斗种子驱动、逐值不可手算,但**区间可以** ⇒ 用区间守住量级,
	//      逐值那部分由上面 spawnEnemy 的脚本用例负责。
	CHECK(e0->vital >= 18 * 865 / 10);
	CHECK(e0->vital <= 32 * 865 / 10);
}

TEST_CASE("M.4b:同一槽重复 spawn ⇒ 拒绝,且不泄漏池槽")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinWithSpawnedEnemies(f, id, 1, 18);
	REQUIRE(f.world.enemyCount() == 1);

	// ⚠️★ 覆盖旧句柄等于泄漏一个池槽 ⇒ 直接拒绝(门 ① 的第二半)。
	CHECK_FALSE(f.world.spawnEnemyToField(
	    battle, static_cast<std::uint8_t>(SA::Rules::kSideOffset),
	    makeWuliTemplate(), makeWuliEncounterFixedLv1(), 18));
	CHECK(f.world.enemyCount() == 1); // ★ 没有多出一个孤儿
}

TEST_CASE("M.4b:入场失败要把实体还回池 —— 预留可回滚,不留孤儿")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	const BattleId battle = f.world.startBattle(makePlayerOnlyField());
	REQUIRE(f.world.joinBattle(battle, id, 0));
	REQUIRE(f.world.enemyCount() == 0);

	// 槽 0 已被玩家占 ⇒ enterEnemyToField 门 ② 失败。
	CHECK_FALSE(f.world.spawnEnemyToField(battle, 0, makeWuliTemplate(),
	                                      makeWuliEncounterFixedLv1(), 18));
	// ★★ 这一条是本用例的全部意义:allocate 成功、入场失败 ⇒ 必须 release 回去。
	//    ⚠️ 漏了它,每次"槽被占"都泄漏一个槽,而入场失败是完全正常的事件。
	CHECK(f.world.enemyCount() == 0);

	// 宠位同理(门 ①)。
	CHECK_FALSE(f.world.spawnEnemyToField(
	    battle, static_cast<std::uint8_t>(SA::Rules::kSideOffset + SA::Rules::kBattlePlayerMax),
	    makeWuliTemplate(), makeWuliEncounterFixedLv1(), 18));
	CHECK(f.world.enemyCount() == 0);

	// 不存在的战斗 / 越界槽号(门 ①)⇒ 连 allocate 都不该发生。
	CHECK_FALSE(f.world.spawnEnemyToField(9999, 0, makeWuliTemplate(),
	                                      makeWuliEncounterFixedLv1(), 18));
	CHECK_FALSE(f.world.spawnEnemyToField(battle, SA::Rules::kSlotCount,
	                                      makeWuliTemplate(),
	                                      makeWuliEncounterFixedLv1(), 18));
	CHECK(f.world.enemyCount() == 0);
}

// ── ★★ 捕获链路:欠债 23 的关闭判据 ────────────────────────────────────

TEST_CASE("M.4b★★:捕获从敌人 L2 实体拷四维 ⇒ 宠物四维非 0 且逐字段相等(欠债 23)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	// 2 只:抓 1 只后敌方仍有存活 ⇒ 战斗不结束,实体不被战斗结束那段回收。
	const BattleId battle = joinWithSpawnedEnemies(f, id, 2, 1);

	// 先把敌人实体的四维记下来 —— 捕获后它会回池,拿不到了。
	const SA::Model::Enemy *src =
	    f.world.battleEnemyAt(battle, SA::Rules::kSideOffset);
	REQUIRE(src != nullptr);
	const SA::Model::Enemy expect = *src; // 值拷贝一份做基线

	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	REQUIRE(f.world.petCount() == 1);

	const SA::Model::Pet *pet = f.world.playerPetAt(id, 0);
	REQUIRE(pet != nullptr);

	// ★★★ **这四行就是欠债 23 的关闭**:M.1–M.4a 期间它们必然全是 0。
	CHECK(pet->vital == expect.vital);
	CHECK(pet->str == expect.str);
	CHECK(pet->tough == expect.tough);
	CHECK(pet->dex == expect.dex);
	CHECK(pet->vital > 0); // ★ 显式钉住"非 0"这件事本身

	// 成长率整组拷(源码 :374)。
	CHECK(pet->growth_vital == expect.growth_vital);
	CHECK(pet->growth_str == expect.growth_str);
	CHECK(pet->growth_tough == expect.growth_tough);
	CHECK(pet->growth_dex == expect.growth_dex);

	// 名字(源码 :375-377)★ M.1 时留空,现在有源了。
	CHECK(std::string(pet->name.c_str()) == "乌力");
	// 评级 / AI 模式 / 图号。
	CHECK(pet->pet_rank == expect.pet_rank);
	CHECK(pet->mod_ai == 150);
	CHECK(pet->base_image == 100250);

	// ⚠️★★ `luck` **恒 0,而这是原版行为**:源码 `pet.c:347` 以"幸运"的名义读的是
	//    `CHAR_LUCK`,而它与 `CHAR_VARIABLEAI` 同槽、被 `enemy.c:1076` 写成了 0。
	//    ★ 这条断言的措辞很重要:它钉的不是"我们没实现幸运",是"原版就是 0"。
	CHECK(pet->luck == 0);
	CHECK(pet->luck == expect.variable_ai);
	CHECK(pet->variable_ai == 0); // 源码 battle_event.c:3547 再清一次

	// HP / MP 取**战场当前值**,不是实体里那份满血(两个数据源的分工,见实现处)。
	CHECK(pet->level == expect.level);
	CHECK(pet->capture_level == pet->level); // 源码 :3519 读的是新宠的 LV
}

TEST_CASE("M.4b:Y 五项 = 用新宠自己的四维推出的三围(源码 pet.c:384-389)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinWithSpawnedEnemies(f, id, 2, 1);
	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	REQUIRE(f.world.petCount() == 1);

	const SA::Model::Pet *pet = f.world.playerPetAt(id, 0);
	REQUIRE(pet != nullptr);

	// ★ 顺序照源码::384 先推导,:385-389 再取 WORK 值 ⇒ Y 是**推导后**的快照。
	const SA::Rules::DerivedStats d =
	    SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex);
	CHECK(pet->y_hp == d.max_hp);
	CHECK(pet->y_atk == d.attack);
	CHECK(pet->y_def == d.defense);
	CHECK(pet->y_quick == d.quick);
	CHECK(pet->y_lv == pet->level);

	// ★★ 非 0 —— M.1/M.2/M.3 三批"有意不建"的理由正是"此刻算出来只能是 0"。
	//    ⇒ 这一条同时是那三批那句话的**解除凭据**。
	CHECK(pet->y_hp > 0);
	CHECK(pet->y_quick > 0);
}

TEST_CASE("M.4b★:捕获宠叫出后战场三围非 0 —— 欠债 23 端到端")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinWithSpawnedEnemies(f, id, 2, 1);
	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	REQUIRE(f.world.petCount() == 1);

	petOutTurn(f, id, battle, 0);

	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	const SA::Rules::Combatant &p = fld->at(SA::Rules::kBattlePlayerMax);
	REQUIRE(p.occupied);
	REQUIRE(p.kind == SA::Rules::CombatantKind::kPet);

	// ★★★ **这就是欠债 23 从头到尾要的那句话**:「叫得出、而且打得动」。
	//    M.2 落地时这里是 attack 0 / max_hp 0;M.3 补了公式仍是 0(0 是不动点);
	//    M.4b 补上来源之后才不是 0。
	CHECK(p.attack > 0);
	CHECK(p.defense > 0);
	CHECK(p.quick > 0);
	CHECK(p.max_hp > 0);
	// ⚠️ 三围必须与宠物自己的四维一致,不是"随便一个非 0"。
	const SA::Model::Pet *pet = f.world.playerPetAt(id, 0);
	REQUIRE(pet != nullptr);
	const SA::Rules::DerivedStats d =
	    SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex);
	CHECK(p.attack == d.attack);
	CHECK(p.max_hp == d.max_hp);
}

TEST_CASE("M.4b:捕获成功 ⇒ 敌人 L2 实体回池(源码 battle.c:1114-1115)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinWithSpawnedEnemies(f, id, 2, 1);
	REQUIRE(f.world.enemyCount() == 2);

	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	REQUIRE(f.world.petCount() == 1);

	// ★★ 源码依据不是"顺手清理":`_BATTLE_Exit` 对 `CHAR_TYPEENEMY` 直接
	//    `CHAR_endCharOneArray` ⇒ 敌人离场即销毁。
	// ⚠️ 漏掉的表现与 M.1 那条一模一样:池只增不减、没有一处报错。
	CHECK(f.world.enemyCount() == 1);
	CHECK(f.world.battleEnemyAt(battle, SA::Rules::kSideOffset) == nullptr);
	// 另一只还在。
	CHECK(f.world.battleEnemyAt(battle, SA::Rules::kSideOffset + 1) != nullptr);
}

TEST_CASE("M.4b:战斗结束 ⇒ 该场剩余敌人实体全部回池")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	// 1 只、level 1(满血高级怪抓不到,见本组末尾那条用例):抓掉它 ⇒ 敌方全灭 ⇒ 战斗结束。
	const BattleId battle = joinWithSpawnedEnemies(f, id, 1, 1);
	REQUIRE(f.world.enemyCount() == 1);

	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	REQUIRE(f.world.stats(battle)->finished);

	// ★ 被捕那只在 applyEvents 里已释放;这里验的是"结束那段不会重复释放、
	//   也不会漏掉剩下的"—— release 对空句柄返回 false 且不做事。
	CHECK(f.world.enemyCount() == 0);
}

TEST_CASE("M.4b:战斗结束时未被捕的敌人也回池(打死的那条路径)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	// 玩家攻击力拉高 ⇒ 几个回合内打死敌人,战斗自然结束(不经捕获路径)。
	SA::Rules::BattleField field = makePlayerOnlyField();
	field.at(0).attack = 5000;
	const BattleId battle = f.world.startBattle(field);
	REQUIRE(f.world.joinBattle(battle, id, 0));
	// ★ 用不可捕的那行(id=1309)+ `baselevel = 1` —— 两点都是有意的:
	//   ① 不可捕 ⇒ 这条路径不会拐进捕获(本用例要验的是"打死"那条);
	//   ② `baselevel > 0` ⇒ 走源码 :1031 那支,**敌人表的 60-80 区间不生效**,
	//      等级仍是 1 ⇒ 顺带钉住"入参优先于摇号"(M.5)。
	REQUIRE(f.world.spawnEnemyToField(
	    battle, static_cast<std::uint8_t>(SA::Rules::kSideOffset),
	    makeWuliTemplate(), makeWuliEncounterWideNoCapture(), 1));
	REQUIRE(f.world.enemyCount() == 1);

	// 敌方 AI 会打玩家、玩家无指令 ⇒ 玩家不动;所以让敌人自己耗死不行 ⇒ 发普攻。
	for (int i = 0; i < 20 && !f.world.stats(battle)->finished; ++i)
	{
		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = battle;
		cmd.turn = f.world.battleField(battle)->turn;
		cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd.command.attack.target = static_cast<std::uint32_t>(SA::Rules::kSideOffset);
		f.world.onBattleCommand(id, cmd);
		f.clock.advance(2000);
		f.world.tick();
	}
	REQUIRE(f.world.stats(battle)->finished);
	CHECK(f.world.enemyCount() == 0);
}

TEST_CASE("M.4b:没有 L2 实体的目标 ⇒ 宠物四维仍 0(记账路径,不是回归)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	// ★ 沿用 M.1 的手填战场:那里的 foe 是直接写进 `field` 的 `Combatant`,
	//   **没有** Enemy 实体 ⇒ 捕获走 `src_enemy == nullptr` 那一支。
	const BattleId battle = joinCapturable(f, id, 2);
	CHECK(f.world.enemyCount() == 0); // 手填的 foe 不占敌人池

	captureTurn(f, id, battle, SA::Rules::kSideOffset);
	REQUIRE(f.world.petCount() == 1);

	const SA::Model::Pet *pet = f.world.playerPetAt(id, 0);
	REQUIRE(pet != nullptr);

	// ⚠️★★ 这条用例**有意保留"四维为 0"**,它钉的是那条脚手架路径的症状,
	//    不是欠债 23 的复活:目标没有 L2 实体 ⇒ 四维无源 ⇒ 只能是 0,
	//    实现处会落一条 `no_l2_enemy` 的 warn。
	//    ★ 与上面那条 M.4b 用例并排放着才有意义:同一个捕获链路,
	//      **目标有 L2 实体则四维非 0、没有则为 0** —— 差别只在目标那一侧。
	CHECK(pet->vital == 0);
	CHECK(pet->str == 0);
	CHECK(pet->name.empty()); // 名字同样无源
	CHECK(pet->y_hp == 0);    // Y 五项是推导后的快照 ⇒ 0 四维 ⇒ 0
	// ★ 但"拿得到的那一半"仍然拷了 —— 门 + 挂槽整条链路是通的。
	CHECK(pet->level == 5); // makeCapturableField 的 foe 等级
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
}

// ★★ 一条**回源码算出来的性质**,顺手钉住 —— 它解释了上面几条捕获用例
//    为什么用 **level 1** 的敌人而不是 level 18。
//
// `Df_HpPer = 10 − HP²/MaxHp`(源码 `battle_event.c:3852`)是**二次式** ⇒
// 目标满血时该项恒为 `10 − MaxHp`:
//     level 18 的乌力  MaxHp 148 ⇒ Df_HpPer = **−138** ⇒ work = −416 ⇒ 必失败
//     level 1  的乌力  MaxHp 15  ⇒ Df_HpPer = **−5**   ⇒ work = 155 ⇒ 夹到 99
// ⇒ ★ **原版的捕获设计要求先把怪打残**:满血怪几乎抓不到,而且 MaxHp 越高越抓不到。
//   ⚠️ 这不是我们的实现限制,是公式本身 —— A.2 落地时用的是手填的 `hp = 1` 残血目标
//     (`makeCapturableField`),所以这条性质此前**从未被暴露**;
//     接上真实模板生成的满血敌人之后它立刻显形。
TEST_CASE("M.4b★★:满血敌人几乎抓不到 —— Df_HpPer 二次式的直接后果(:3852)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	// level 18 ⇒ MaxHp 约 148 ⇒ Df_HpPer ≈ −138 ⇒ work 深负 ⇒ `rand(1,100) < work` 恒假。
	const BattleId battle = joinWithSpawnedEnemies(f, id, 2, 18);
	REQUIRE(f.world.enemyCount() == 2);

	captureTurn(f, id, battle, SA::Rules::kSideOffset);

	// ★ 抓不到:没有宠物、目标留场、敌人实体也不回池。
	CHECK(f.world.petCount() == 0);
	CHECK(f.world.enemyCount() == 2);
	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(SA::Rules::kSideOffset).occupied);
	// ⚠️ 但 capture_bonus 仍被无条件清零(源码 :3510,A.2 那条用例的性质在此复现)。
	CHECK(fld->at(0).mods.capture_bonus == 0);

	// ★ 反面由上面几条 level 1 的用例给出:同一模板、同一玩家,只有 MaxHp 不同就抓得到。
	//   ⇒ 这一对用例合起来说明"抓不到"来自 MaxHp,不是来自接线出了错。
}

// ── ★★ 批次 M.6:遇敌坐标表 → 编组(findEncountArea / pickEnemyGroup)────────
//
// ★ 两套数据各钉不同的东西(M.5 ⑤ 立的那条):
//   · **真数据**(`encount.txt` 第 1 行 + 它引用的 5 个编组)钉「配置怎么被解释」;
//   · **手造数据**钉那些真数据里**一次都不出现**的防御分支(`zorder <= 0` / 权重 0)——
//     ★ 它们只能靠手造覆盖,而"真数据跑得过"证明不了它们。

// `encount.txt` 第 1 行的**全部实测列**(csa8.0 数据包,2026-09-09):
//   21,100,582,399,783,560,1,5,2,40,88,91,89,92,1230,,,,,,50,50,10,10,100,,,,,,,,
//
// ⚠️★ 注意 `group_id` 的槽序是 **88,91,89,92,1230** —— 与 `group1.txt` 里的表序
//    (88,89,91,92,1230)**不同**。★ 抽签遍历的是**这个槽序**,不是表序;
//    ⇒ 实现若按表序遍历,权重就会对错组,而两种顺序下"总权重"完全相同 ⇒ 只有逐值断言抓得到。
SA::World::EncountArea makeArea21()
{
	SA::World::EncountArea a{};
	a.index = 21;
	a.floor = 100;
	// c3-c6 = 582,399,783,560 ⇒ 载入期归一成左上 + 宽高(⚠️ 宽高不 +1)
	a.x = 582;
	a.y = 399;
	a.width = 783 - 582;  // 201
	a.height = 560 - 399; // 161
	a.prob_min = 1;
	a.prob_max = 5;
	a.enemy_max_num = 2;
	a.zorder = 40;
	a.group_id = {88, 91, 89, 92, 1230, -1, -1, -1, -1, -1};
	a.group_prob = {50, 50, 10, 10, 100, -1, -1, -1, -1, -1};
	return a;
}

// 区域 21 引用的 5 个编组的**实测行**。
// ★★ 其中 `GROUP_ID=1230` 带 `APPEARBYITEMID=1961`,而它的权重是 **100 —— 5 个里最大**
//    (总 220,占 **45%**)⇒ 这一行让「道具系统未移植」这个偏差的规模**在用例里看得见**:
//    空背包下它整个消失,该区域的总权重从 220 掉到 120。
std::vector<SA::World::EnemyGroup> makeGroups21()
{
	auto g = [](std::int32_t id, std::int32_t appear, std::vector<std::int32_t> eids,
	            std::vector<std::int32_t> probs)
	{
		SA::World::EnemyGroup r{};
		r.group_id = id;
		r.appear_by_item_id = appear;
		r.not_appear_by_item_id = -1;
		r.enemy_id.fill(-1);
		r.create_prob.fill(-1);
		for (std::size_t i = 0; i < eids.size(); ++i)
			r.enemy_id[i] = eids[i];
		for (std::size_t i = 0; i < probs.size(); ++i)
			r.create_prob[i] = probs[i];
		return r;
	};
	// 表序(= group1.txt 的行序),★ 与区域 21 的槽序不同
	return {g(88, -1, {119}, {1}), g(89, -1, {120}, {1}), g(91, -1, {122}, {1}),
	        g(92, -1, {123}, {1}),
	        g(1230, 1961, {2250, 2251, 2249, 2252, 2253}, {1, 1, 1, 1, 1})};
}

// 取选中编组的 `GROUP_ID`(函数返回的是**行下标**,断言写 id 才读得懂)。
std::int32_t pickedGroupId(const SA::World::EncountArea &a,
                           const std::vector<SA::World::EnemyGroup> &gs,
                           const std::vector<std::int32_t> &bag, SA::Rules::Random &rng)
{
	const std::int32_t row = SA::World::pickEnemyGroup(a, gs, bag, rng);
	return row < 0 ? -1 : gs[static_cast<std::size_t>(row)].group_id;
}

TEST_CASE("M.6:findEncountArea —— 闭区间矩形,四条边与四个角都算命中")
{
	const std::vector<SA::World::EncountArea> areas{makeArea21()};

	SUBCASE("矩形内部")
	{
		CHECK(SA::World::findEncountArea(areas, 100, 600, 450) == 0);
	}
	SUBCASE("★ 四个角 —— 闭区间的判据")
	{
		CHECK(SA::World::findEncountArea(areas, 100, 582, 399) == 0); // 左上
		CHECK(SA::World::findEncountArea(areas, 100, 783, 399) == 0); // 右上
		CHECK(SA::World::findEncountArea(areas, 100, 582, 560) == 0); // 左下
		CHECK(SA::World::findEncountArea(areas, 100, 783, 560) == 0); // ★ 右下
	}
	SUBCASE("★ 越界一格即不命中 —— 与上一条成对,单独看任一条都证明不了闭区间")
	{
		CHECK(SA::World::findEncountArea(areas, 100, 581, 450) == -1);
		CHECK(SA::World::findEncountArea(areas, 100, 784, 450) == -1);
		CHECK(SA::World::findEncountArea(areas, 100, 600, 398) == -1);
		CHECK(SA::World::findEncountArea(areas, 100, 600, 561) == -1);
	}
	SUBCASE("floor 不同 ⇒ 坐标再对也不命中")
	{
		CHECK(SA::World::findEncountArea(areas, 101, 600, 450) == -1);
	}
	SUBCASE("★★ 单点区域(x1==x2)照样命中 —— 宽高不 +1 与闭区间必须配对")
	{
		// ⚠️ 若把判据写成 `px < x + width`,width==0 的区域**永不命中**,
		//    而这种区域在真数据里存在(把宽高写成 0 就是"一格")。
		SA::World::EncountArea dot = makeArea21();
		dot.x = 700;
		dot.y = 500;
		dot.width = 0;
		dot.height = 0;
		const std::vector<SA::World::EncountArea> one{dot};
		CHECK(SA::World::findEncountArea(one, 100, 700, 500) == 0);
		CHECK(SA::World::findEncountArea(one, 100, 701, 500) == -1);
	}
}

TEST_CASE("M.6★★:zorder 的双重身份 —— 既是优先级,也是启用开关")
{
	// ★★ 实测 `encount.txt` 1050 行**全部 zorder > 0** ⇒ 「<= 0 就跳过」这一支
	//    在真数据上一次都不触发 ⇒ **只能靠手造数据钉住**,而它是源码行为(`:378`)。
	SUBCASE("zorder <= 0 ⇒ 整行跳过,哪怕坐标完全匹配")
	{
		SA::World::EncountArea off = makeArea21();
		off.zorder = 0;
		const std::vector<SA::World::EncountArea> areas{off};
		CHECK(SA::World::findEncountArea(areas, 100, 600, 450) == -1);

		off.zorder = -1;
		const std::vector<SA::World::EncountArea> neg{off};
		CHECK(SA::World::findEncountArea(neg, 100, 600, 450) == -1);
	}

	SUBCASE("重叠区域取 zorder 最大")
	{
		SA::World::EncountArea lo = makeArea21();
		lo.index = 1;
		lo.zorder = 10;
		SA::World::EncountArea hi = makeArea21();
		hi.index = 2;
		hi.zorder = 99;
		CHECK(SA::World::findEncountArea({lo, hi}, 100, 600, 450) == 1); // 后者胜
		CHECK(SA::World::findEncountArea({hi, lo}, 100, 600, 450) == 0); // 换序仍是它
	}

	SUBCASE("★ zorder 相等 ⇒ 保留先遇到的(源码判据是严格 >,顺序敏感)")
	{
		// ⚠️ 后果:D 线入库时**不得重排行** —— 重排会静默改变重叠区域的胜者。
		SA::World::EncountArea a = makeArea21();
		a.index = 1;
		SA::World::EncountArea b = makeArea21();
		b.index = 2; // zorder 同为 40
		CHECK(SA::World::findEncountArea({a, b}, 100, 600, 450) == 0);
		CHECK(SA::World::findEncountArea({b, a}, 100, 600, 450) == 0);
	}
}

TEST_CASE("M.6★★:pickEnemyGroup 按权重抽签 —— 逐值,且槽序不是表序")
{
	const SA::World::EncountArea a = makeArea21();
	const std::vector<SA::World::EnemyGroup> gs = makeGroups21();
	const std::vector<std::int32_t> empty_bag{};

	// 空背包 ⇒ 编组 1230(要道具 1961)被排除 ⇒ 候选 4 个,总权重 50+50+10+10 = 120
	// 累加边界:50 → 槽0(88) · 100 → 槽1(91) · 110 → 槽2(89) · 落空 → 槽3(92)
	SUBCASE("★ 四段边界逐值 —— 每段取首尾两个值")
	{
		const std::vector<std::pair<int, std::int32_t>> cases{
		    {0, 88},
		    {49, 88}, // 第一段 [0,50)
		    {50, 91},
		    {99, 91}, // 第二段 [50,100)
		    {100, 89},
		    {109, 89}, // 第三段 [100,110)
		    {110, 92},
		    {119, 92}, // ★ 兜底段 —— 最后一个候选不参与判定
		};
		for (const auto &c : cases)
		{
			ScriptedRandom rng({c.first});
			CHECK(pickedGroupId(a, gs, empty_bag, rng) == c.second);
			CHECK(rng.calls() == 1); // ★ 恰好摇一次
		}
	}

	SUBCASE("★★ 带上道具 1961 ⇒ 第 5 组回归,总权重 120 → 220")
	{
		// ⚠️★ 这一条把「道具系统未移植」的偏差规模钉成断言:那一组权重 100,
		//    占带道具时总权重的 **45%** ⇒ 空背包下它整个消失。
		const std::vector<std::int32_t> bag{1961};
		{
			ScriptedRandom rng({119}); // 空背包时这个值落兜底段 → 92
			CHECK(pickedGroupId(a, gs, bag, rng) == 92);
		}
		{
			ScriptedRandom rng({120}); // ★ 带道具后 120 起才进第 5 组
			CHECK(pickedGroupId(a, gs, bag, rng) == 1230);
		}
		{
			ScriptedRandom rng({219}); // 上界
			CHECK(pickedGroupId(a, gs, bag, rng) == 1230);
		}
		// ⇒ 同一个 r=120 在两种背包下结果不同,而**唯一差别是背包**
		{
			ScriptedRandom rng({120});
			CHECK(pickedGroupId(a, gs, empty_bag, rng) == 92); // 空背包:仍是兜底
		}
	}

	SUBCASE("★★ 槽序 ≠ 表序 —— 实现若按表序遍历,总权重不变而结果全错")
	{
		// 槽序 88,91,89,92 ⇒ r=50 落槽1 = **91**
		// 若按表序 88,89,91,92 遍历 ⇒ r=50 会落到 **89**
		ScriptedRandom rng({50});
		CHECK(pickedGroupId(a, gs, empty_bag, rng) == 91);
	}
}

TEST_CASE("M.6:pickEnemyGroup 的三条门与两处不消耗 rng")
{
	const std::vector<SA::World::EnemyGroup> gs = makeGroups21();

	SUBCASE("★ 无候选 ⇒ 返回 -1 且**不消耗 rng**(源码在 RAND 之前就 return)")
	{
		SA::World::EncountArea a = makeArea21();
		a.group_id.fill(-1); // 一个编组都没配
		ScriptedRandom rng({0});
		CHECK(SA::World::pickEnemyGroup(a, gs, {}, rng) == -1);
		CHECK(rng.calls() == 0); // ★ 这一条钉住"提前返回"的位置
	}

	SUBCASE("★★ 全部编组都被道具门排除 ⇒ -1(实测 25/969 个区域是这个状态)")
	{
		SA::World::EncountArea a = makeArea21();
		a.group_id = {1230, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // 只留要道具那组
		a.group_prob = {100, -1, -1, -1, -1, -1, -1, -1, -1, -1};
		ScriptedRandom rng({0});
		CHECK(SA::World::pickEnemyGroup(a, gs, {}, rng) == -1);
		CHECK(rng.calls() == 0);
		// ★ 带上道具就回来了 —— 坐实这 -1 来自道具门而不是别的
		ScriptedRandom rng2({0});
		CHECK(pickedGroupId(a, gs, {1961}, rng2) == 1230);
	}

	SUBCASE("NOTAPPEARBYITEMID:持有则排除,不持有则入选")
	{
		std::vector<SA::World::EnemyGroup> g2 = gs;
		g2[0].not_appear_by_item_id = 777; // 编组 88 加一道"禁入"门
		SA::World::EncountArea a = makeArea21();
		a.group_id = {88, -1, -1, -1, -1, -1, -1, -1, -1, -1};
		a.group_prob = {50, -1, -1, -1, -1, -1, -1, -1, -1, -1};

		ScriptedRandom r1({0});
		CHECK(pickedGroupId(a, g2, {}, r1) == 88); // 没那道具 ⇒ 照常
		ScriptedRandom r2({0});
		CHECK(SA::World::pickEnemyGroup(a, g2, {777}, r2) == -1); // 有 ⇒ 排除
	}

	SUBCASE("★ 编组号查不到对应 group 行 ⇒ 跳过(有意不照抄原版的坏组入选)")
	{
		SA::World::EncountArea a = makeArea21();
		a.group_id = {999999, 88, -1, -1, -1, -1, -1, -1, -1, -1};
		a.group_prob = {50, 50, -1, -1, -1, -1, -1, -1, -1, -1};
		ScriptedRandom rng({0});
		// 候选只剩 88 ⇒ 总权重 50,必中它
		CHECK(pickedGroupId(a, gs, {}, rng) == 88);
	}

	SUBCASE("权重为 0 的槽不被选中 —— ⚠️★★ 但这条断言**区分不了**实现有没有那个条件")
	{
		// ⚠️★★ **本 SUBCASE 的注释初稿是错的**,原文写着「源码判据是 `wr[i] != 0 && r < aaa`,
		//    少了前半个条件时权重 0 的槽会被选中」。反向验证注入「删掉 `weight != 0`」
		//    **一条都没红**,穷举随后证明了为什么:
		//      1..4 槽 × 权重 {−1,0,1,2,3} × r 遍历 [0,Σ−1] 共 2,580 组,
		//      删掉该条件与保留它 **结果差异 0 组** ⇒ 它是**冗余**的。
		//    ★ 道理:权重 0 的槽不让 `acc` 增长,而 `r < acc_prev` 若成立,前一轮就已 break;
		//      i == 0 那种情形要求 `r < 0`,而 `r >= 0` 恒成立。
		// ⇒ ★ 这条断言**仍然有价值**(它钉住"权重 0 不被选中"这个**性质**),
		//   但它**不是**那个条件的守卫 —— 那个条件没有守卫,因为它没有可观察后果。
		//   ⚠️ 别在这里再加断言去"覆盖"它:冗余的分支没有能区分它的输入。
		SA::World::EncountArea a = makeArea21();
		a.group_id = {88, 91, 89, -1, -1, -1, -1, -1, -1, -1};
		a.group_prob = {0, 50, 50, -1, -1, -1, -1, -1, -1, -1};
		ScriptedRandom rng({0});
		CHECK(pickedGroupId(a, gs, {}, rng) == 91);
	}

	SUBCASE("★ 抽签上界 found-1 的兜底段 —— 同样是等价写法,不是判据")
	{
		// ★ 注入「上界改 found」也是 0 条转红,穷举同样 0 差异 ⇒ 两种写法等价。
		//   ⇒ 本条断言的是**兜底段真的会被取到**(这是性质),而不是"上界必须写 found-1"。
		SA::World::EncountArea a = makeArea21();
		a.group_id = {88, 91, -1, -1, -1, -1, -1, -1, -1, -1};
		a.group_prob = {50, 50, -1, -1, -1, -1, -1, -1, -1, -1};
		ScriptedRandom lo({49}); // 落第一段
		CHECK(pickedGroupId(a, gs, {}, lo) == 88);
		ScriptedRandom hi({50}); // ★ 落兜底段(最后一个候选)
		CHECK(pickedGroupId(a, gs, {}, hi) == 91);
		ScriptedRandom top({99}); // 上界
		CHECK(pickedGroupId(a, gs, {}, top) == 91);
	}
}

// ══ 批次 M.7:遇敌第三跳(编组 → 敌人列表)═══════════════════════════════════
//
// 详注见 world/Api.h 的 `rollEnemyList` 声明处;裁定见 `11` §2.16(DR-DT14)。
// ⚠️★ 与 M.6 相反:大怪布阵(BIG 占模板表 50.6%)是**真实主路径**,逐分支覆盖。

namespace
{

// 模板表:NORMAL(tempno=1,乌力)+ BIG(tempno=1001)。★ 复用 M.5 的乌力模板,只改 tempno/size。
std::vector<SA::World::EnemyTemplate> makeTemplatesM7()
{
	SA::World::EnemyTemplate normal = makeWuliTemplate();
	normal.temp_no = 1;
	normal.size = SA::World::kEnemySizeNormal;
	SA::World::EnemyTemplate big = makeWuliTemplate();
	big.temp_no = 1001;
	big.size = SA::World::kEnemySizeBig;
	return {normal, big};
}

// 敌人表一行:id / 指向的模板 tempno / CREATEMAXNUM。★ 纯逻辑 fixture,id 任取。
SA::World::EnemyEncounter encRow(std::int32_t id, std::int32_t tempno, std::int32_t cmax)
{
	SA::World::EnemyEncounter e{};
	e.enemy_id = id;
	e.temp_no = tempno;
	e.lv_min = 1;
	e.lv_max = 1;
	e.create_max_num = cmax;
	return e;
}

// 编组:引用若干 ENEMY_ID + 各自权重(CREATEPROB)。
SA::World::EnemyGroup groupOf(std::vector<std::int32_t> eids, std::vector<std::int32_t> probs)
{
	SA::World::EnemyGroup g{};
	g.group_id = 1;
	g.enemy_id.fill(-1);
	g.create_prob.fill(-1);
	for (std::size_t i = 0; i < eids.size(); ++i)
		g.enemy_id[i] = eids[i];
	for (std::size_t i = 0; i < probs.size(); ++i)
		g.create_prob[i] = probs[i];
	return g;
}

} // namespace

TEST_CASE("M.7:findEnemyEncounter / findEnemyTemplate —— 命中与找不到")
{
	const std::vector<SA::World::EnemyEncounter> encs{encRow(9, 1, 2), encRow(142, 1, 3),
	                                                  encRow(1309, 1001, 1)};
	const std::vector<SA::World::EnemyTemplate> tmpls = makeTemplatesM7();

	CHECK(SA::World::findEnemyEncounter(encs, 142) == 1);
	CHECK(SA::World::findEnemyEncounter(encs, 9) == 0);
	CHECK(SA::World::findEnemyEncounter(encs, 999) == -1); // 不存在 ⇒ -1
	CHECK(SA::World::findEnemyTemplate(tmpls, 1001) == 1);
	CHECK(SA::World::findEnemyTemplate(tmpls, 1) == 0);
	CHECK(SA::World::findEnemyTemplate(tmpls, 42) == -1); // 不存在 ⇒ -1
}

TEST_CASE("M.7:无候选 ⇒ 返回空且不消耗 rng(源码 :1399 在 RAND 之前 return)")
{
	const std::vector<SA::World::EnemyEncounter> encs{encRow(9, 1, 2)};
	const std::vector<SA::World::EnemyTemplate> tmpls = makeTemplatesM7();

	SUBCASE("编组全空槽")
	{
		const SA::World::EnemyGroup g = groupOf({}, {});
		ScriptedRandom rng({5});
		CHECK(SA::World::rollEnemyList(g, encs, tmpls, 10, rng).empty());
		CHECK(rng.calls() == 0); // ★ 一次都没摇
	}
	SUBCASE("编组引用的 ENEMY_ID 全部解析失败")
	{
		const SA::World::EnemyGroup g = groupOf({777, 888}, {10, 10});
		ScriptedRandom rng({5});
		CHECK(SA::World::rollEnemyList(g, encs, tmpls, 10, rng).empty());
		CHECK(rng.calls() == 0);
	}
}

TEST_CASE("M.7★:出场数上界 = min(enemy_max_num, Σ CREATEMAXNUM)")
{
	// 单敌人 idA(NORMAL,cmax=3),Σcmax=3。★ ScriptedRandom 给超大值被 rand 钳到上界,
	//    上界直接体现在产出数量上(同族门阈 = 3*1 = 3,与上界持平 ⇒ 不额外挡)。
	const std::vector<SA::World::EnemyEncounter> encs{encRow(9, 1, 3)};
	const std::vector<SA::World::EnemyTemplate> tmpls = makeTemplatesM7();
	const SA::World::EnemyGroup g = groupOf({9}, {10});

	SUBCASE("enemy_max_num 更小 ⇒ 上界 = enemy_max_num")
	{
		ScriptedRandom rng({99}); // entrymax = rand(1,2) 钳到 2
		const auto out = SA::World::rollEnemyList(g, encs, tmpls, 2, rng);
		CHECK(out.size() == 2);
	}
	SUBCASE("Σ CREATEMAXNUM 更小 ⇒ 上界 = Σ CREATEMAXNUM")
	{
		ScriptedRandom rng({99}); // entrymax = rand(1,3) 钳到 3
		const auto out = SA::World::rollEnemyList(g, encs, tmpls, 10, rng);
		REQUIRE(out.size() == 3);
		CHECK(out[0] == 0); // 都是 idA 的行下标
	}
}

TEST_CASE("M.7★★:同族上限门 —— 放够 CREATEMAXNUM*samecount 就不再放它")
{
	const std::vector<SA::World::EnemyTemplate> tmpls = makeTemplatesM7();

	SUBCASE("★ A(cmax=1)被门挡在 1 只,名额让给 B")
	{
		const std::vector<SA::World::EnemyEncounter> encs{encRow(9, 1, 1), encRow(142, 1, 5)};
		const SA::World::EnemyGroup g = groupOf({9, 142}, {100, 1});
		// rng:[0]=entrymax=3 · [1]=0(A,放) · [2]=0(A,cnt=1>=1 挡) · [3]=100(B) · 重复 100(B)
		ScriptedRandom rng({3, 0, 0, 100});
		const auto out = SA::World::rollEnemyList(g, encs, tmpls, 10, rng);
		REQUIRE(out.size() == 3);
		CHECK(out[0] == 0);
		CHECK(out[1] == 1);
		CHECK(out[2] == 1); // ★ A 只 1 只(门挡第 2 只),B 补 2 只
	}
	SUBCASE("★ 抽中的行一直被门挡 ⇒ loopcounter 兜底退出,不死循环")
	{
		const std::vector<SA::World::EnemyEncounter> encs{encRow(9, 1, 1), encRow(142, 1, 1)};
		const SA::World::EnemyGroup g = groupOf({9, 142}, {100, 1});
		ScriptedRandom rng({2}); // entrymax=2;抽签恒 0 ⇒ 恒选 A,A 放 1 只后永远被挡
		const auto out = SA::World::rollEnemyList(g, encs, tmpls, 10, rng);
		CHECK(out.size() == 1); // ★ 靠 loopcounter<100 退出,否则死循环
	}
}

TEST_CASE("M.7★:大怪布阵 —— 前 5 只 BIG 直接放前排(i<=4)")
{
	const std::vector<SA::World::EnemyEncounter> encs{encRow(500, 1001, 5)}; // BIG
	const std::vector<SA::World::EnemyTemplate> tmpls = makeTemplatesM7();
	const SA::World::EnemyGroup g = groupOf({500}, {10});
	ScriptedRandom rng({3}); // entrymax=3;单候选恒选它
	const auto out = SA::World::rollEnemyList(g, encs, tmpls, 10, rng);
	REQUIRE(out.size() == 3);
	for (auto row : out)
		CHECK(row == 0); // 都是那只 BIG 的行下标
}

TEST_CASE("M.7★★:大怪布阵 —— 第 6 只起 BIG 放不下,减少出场数(bigcnt>=5)")
{
	const std::vector<SA::World::EnemyEncounter> encs{encRow(500, 1001, 10)}; // BIG,cmax 够大
	const std::vector<SA::World::EnemyTemplate> tmpls = makeTemplatesM7();
	const SA::World::EnemyGroup g = groupOf({500}, {10});
	ScriptedRandom rng({8}); // 想出 8 只,但全是 BIG ⇒ 满 5 只后每多抽一只就 entrymax--
	const auto out = SA::World::rollEnemyList(g, encs, tmpls, 10, rng);
	CHECK(out.size() == 5); // 大怪上限 5,超出的名额被 entrymax-- 吃掉
	// ⚠️★★ **单靠 size 无区分力**:删掉 `bigcnt>=5` 门后,第 6 只 BIG 走 i>4 换位、
	//    因前 5 位全是 BIG(无 NORMAL 可换)而 continue ⇒ out **同样是 5 只**。
	//    ⇒ `bigcnt>=5` 门的**独立可观察后果是 rng 取数次数**:它 `entrymax--` 让循环在
	//    第 8 轮结束(摇号 1 + 抽签 8 = 9 次);删掉后靠 `loopcounter<100` 兜底 ⇒ 空转到 101 次。
	//    ★ 这正是 §9.0.36 ⑥ 那族(反向验证逼出"断言的形状要能区分被注入的那处")——
	//      2026-09-09 反向验证注入 `bigcnt>=99` 时 size 断言 0 转红,补此条后立刻转红。
	CHECK(rng.calls() == 9);
}

TEST_CASE("M.7★★★:大怪布阵 —— 第 6 只 BIG 换到前排,被顶出的 NORMAL 落到后排(i>4)")
{
	// idN(NORMAL,行 0)+ idC(BIG,行 1)。权重 N=100 / C=1 ⇒ r<100 选 N、r>=100 选 C。
	const std::vector<SA::World::EnemyEncounter> encs{encRow(300, 1, 10), encRow(500, 1001, 10)};
	const std::vector<SA::World::EnemyTemplate> tmpls = makeTemplatesM7();
	const SA::World::EnemyGroup g = groupOf({300, 500}, {100, 1});
	// rng:[0]=entrymax=6 · 前 5 抽选 N(r=0)· 第 6 抽选 C(r=100)
	ScriptedRandom rng({6, 0, 0, 0, 0, 0, 100});
	const auto out = SA::World::rollEnemyList(g, encs, tmpls, 10, rng);
	REQUIRE(out.size() == 6);
	CHECK(out[0] == 1); // ★ BIG(idC 行下标 1)被换到最前排
	for (std::size_t k = 1; k < 6; ++k)
		CHECK(out[k] == 0); // 其余是 NORMAL;被顶出的那只 NORMAL 落到位置 5
}

TEST_CASE("M.7:敌人行的模板查不到 ⇒ 整只不放(i 不推进,靠 loopcounter 退出)")
{
	// idX 指向不存在的模板 tempno=9999 ⇒ findEnemyTemplate 返 -1。
	const std::vector<SA::World::EnemyEncounter> encs{encRow(700, 9999, 5)};
	const std::vector<SA::World::EnemyTemplate> tmpls = makeTemplatesM7();
	const SA::World::EnemyGroup g = groupOf({700}, {10});
	ScriptedRandom rng({3});
	const auto out = SA::World::rollEnemyList(g, encs, tmpls, 10, rng);
	CHECK(out.empty()); // ★ 候选有效(收进了)但模板缺失 ⇒ 一只都放不出
}

// ══ 战果结算(EXP / DUELPOINT + 战斗结束经验分配)══════════════════════════
//
// ★ 敌人身上的 exp / duelpoint 走 spawnEnemy 的判定树(直接可算,不依赖战斗);
//   玩家实拿的经验走 finished 段的等级差衰减(端到端,靠 playerExp 观察面)。
// ⚠️ 乌力模板(makeWuliTemplate):四维基数和 20+12+15+25 = 72 < 80 ⇒ rank 5(bonus 0.0);
//    capture_difficulty = 11 是 alpha 里的 E_T_GET 项 ⇒ alpha = 11/100.0 = 0.11;
//    其余抗性列 / rare 均 0。⇒ enemyExp = base[level−1] + (0.0 + 0.11) * level,截断。

TEST_CASE("战果:敌人 EXP/DUELPOINT 判定树三分支(enemy.c:1101-1107)")
{
	const std::vector<int> script{2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	const SA::Rules::RulesConfig cfg{};

	SUBCASE("duelpoint <= 0 且 exp == -1(哨兵)⇒ 走 enemyExp 公式")
	{
		ScriptedRandom rng(script);
		// fixedLv1 默认 exp = -1 / duelpoint = 0;传 baselevel = 10 固定等级。
		const SA::Model::Enemy e =
		    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 10, rng, cfg);
		// base[9] = 18 ⇒ raw = 18 + (0.0 + 0.11) * 10 = 19.1 ⇒ 19。
		CHECK(e.exp == 19);
		CHECK(e.duelpoint == 0);
	}

	SUBCASE("duelpoint <= 0 且 exp != -1 ⇒ 用敌人表值,不走公式")
	{
		ScriptedRandom rng(script);
		EnemyEncounter enc = makeWuliEncounterFixedLv1();
		enc.exp = 500;
		const SA::Model::Enemy e = spawnEnemy(makeWuliTemplate(), enc, 10, rng, cfg);
		CHECK(e.exp == 500); // ★ 表值直接用 —— 不是公式算出的 19
		CHECK(e.duelpoint == 0);
	}

	SUBCASE("duelpoint > 0 ⇒ 决斗点怪,exp 保持 0(即使敌人表配了 exp 也不给)")
	{
		ScriptedRandom rng(script);
		EnemyEncounter enc = makeWuliEncounterFixedLv1();
		enc.duelpoint = 100;
		enc.exp = 500; // ★ 配了也不进 exp 分支(duelpoint > 0 短路)
		const SA::Model::Enemy e = spawnEnemy(makeWuliTemplate(), enc, 10, rng, cfg);
		CHECK(e.exp == 0); // ★ 决斗点怪不给经验(判定树 if(duelpoint<=0) 不成立)
		CHECK(e.duelpoint == 100);
	}
}

TEST_CASE("战果:enemyExp 逐值 + enemybaseexptbl 的 74 级递减异常照抄")
{
	const std::vector<int> script{2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	const SA::Rules::RulesConfig cfg{};
	const EnemyEncounter enc = makeWuliEncounterFixedLv1(); // exp = -1 ⇒ 走公式

	auto expAt = [&](std::int32_t level)
	{
		ScriptedRandom rng(script);
		return spawnEnemy(makeWuliTemplate(), enc, level, rng, cfg).exp;
	};

	// ★ 逐值(避量级断言无区分力,§9.0.35 ⑤):
	CHECK(expAt(1) == 1);   // base[0]=1 ⇒ 1 + 0.11*1 = 1.11 ⇒ 1
	CHECK(expAt(10) == 19); // base[9]=18 ⇒ 18 + 0.11*10 = 19.1 ⇒ 19

	// ★★ 74 级递减异常:base[72]=959(lv73)· base[73]=956(lv74,比前一个小)。
	//   照抄原版数据毛刺的**可观察后果** = 74 级经验反而比 73 级少(单调递增序列里唯一
	//   的下坠)。⇒ 若哪天有人"顺手把 956 改成 985 插值",这条会转红。
	const std::int32_t e73 = expAt(73); // 959 + 0.11*73 = 967.03 ⇒ 967
	const std::int32_t e74 = expAt(74); // 956 + 0.11*74 = 964.14 ⇒ 964
	CHECK(e73 == 967);
	CHECK(e74 == 964);
	CHECK(e74 < e73); // ★ 异常值没被"修正"的钉子
}

TEST_CASE("战果:端到端 —— 打赢野怪,玩家按等级差衰减涨经验(playerExp)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick(); // onSessionReady ⇒ 建 Player 实体
	REQUIRE(f.world.playerCount() == 1);
	REQUIRE(f.world.playerExp(id) == 0); // 入场时 0

	// ★ 玩家强场(秒杀 + 高血不死),敌方槽空 —— 由 spawnEnemyToField 填真敌人。
	SA::Rules::BattleField pf{};
	SA::Rules::Combatant &me = pf.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20; // ★ 玩家 20 级
	me.hp = 100000;
	me.max_hp = 100000;
	me.attack = 100000; // 秒杀敌人
	me.defense = 10000;
	me.quick = 500; // 先手
	me.luck = 10;
	const BattleId battle = f.world.startBattle(pf);
	REQUIRE(f.world.joinBattle(battle, id, 0));

	// 刷一只 10 级乌力(exp = -1 ⇒ 公式 ⇒ 19)。
	REQUIRE(f.world.spawnEnemyToField(battle, SA::Rules::kSideOffset,
	                                  makeWuliTemplate(), makeWuliEncounterFixedLv1(),
	                                  /*baselevel=*/10));

	// 推进:每回合玩家出招打敌人 slot 10,直到打完(finished 后 tick 会跳过)。
	for (int i = 0; i < 30; ++i)
	{
		const SA::Rules::BattleField *fld = f.world.battleField(battle);
		if (fld == nullptr)
			break;
		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = battle;
		cmd.turn = fld->turn;
		cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd.command.attack.target = static_cast<std::uint32_t>(SA::Rules::kSideOffset);
		f.world.onBattleCommand(id, cmd);
		f.clock.advance(2000);
		f.world.tick();
	}

	// ★★ 玩家 20 级打 10 级敌人:b_level = 20 − 10 = 10 > 5 ⇒ 衰减
	//    b_level = 5 + 15 − 10 = 10;nowexp = 19 * 10 / 15 = 12(整除)。
	CHECK(f.world.playerExp(id) == 12);
}

// ═══════════════════════════════════════════════════════════════════════════
//  捕获扣道具(道具域第二批,DR-BT10「全删」+ 前置门 CaptureItemCheck)
// ═══════════════════════════════════════════════════════════════════════════
//
// ★ 端到端:玩家背包有条件道具 → 抓表内怪 → 道具被全删;背包缺条件道具 → 门 ④ 拦、抓不到。
//   ⚠️ 用**真的 L2 敌人**(spawnEnemyToField),因为匹配键 pet_id 来自敌人实体。

namespace
{
// 造一只可捕、低级、模板号 = 指定值的 L2 敌人模板(其余同乌力)。
EnemyTemplate makeNeedItemTemplate(std::int32_t temp_no)
{
	EnemyTemplate t = makeWuliTemplate();
	t.temp_no = temp_no; // 落进 Enemy.pet_id ⇒ NeedEnemy 表匹配键
	return t;
}

// 握手 + 入场(强场,charm 200 ⇒ 捕获必过概率门)+ 刷一只模板号 = temp_no 的可捕敌人。
BattleId joinWithNeedItemEnemy(Fixture &f, SA::Net::ConnectionId id,
                               std::int32_t temp_no)
{
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	const BattleId battle = f.world.startBattle(makePlayerOnlyField());
	REQUIRE(f.world.joinBattle(battle, id, 0));
	REQUIRE(f.world.spawnEnemyToField(battle, SA::Rules::kSideOffset,
	                                  makeNeedItemTemplate(temp_no),
	                                  makeWuliEncounterFixedLv1(), /*baselevel=*/1));
	return battle;
}

// 造一个指定 id 的道具。★ `current_pile = 1` = 「这一格里有一件」——
//   ⚠️ 批次 I.4 起**必须填**:`current_pile <= 0` 被使用链路视同空格(用不了、不扣)。
//   掉落路径在 World 内部自己填(World.cpp),经 `giveItemToPlayer` seam 灌入的由调用方填。
SA::Model::Item makeItem(std::int32_t item_id, std::int32_t pile = 1)
{
	SA::Model::Item it{};
	it.item_id = item_id;
	it.current_pile = pile;
	return it;
}

// 发一条「使用道具」战斗指令并推进一个回合(批次 I.4)。
void useItemTurn(Fixture &f, SA::Net::ConnectionId id, BattleId battle, int item_slot,
                 int target_slot)
{
	SA::Domain::BattleCommand cmd{};
	cmd.battle_id = battle;
	cmd.turn = f.world.battleField(battle)->turn;
	cmd.command_kind = SA::Domain::BattleCommand::CommandKind::USE_ITEM;
	cmd.command.use_item.item_slot = static_cast<std::uint32_t>(item_slot);
	cmd.command.use_item.target = static_cast<std::uint32_t>(target_slot);
	f.world.onBattleCommand(id, cmd);
	f.clock.advance(2000);
	f.world.tick();
}

// 开一场「玩家残血、敌方槽空」的战斗:用来看恢复药到底加没加血。
//   ★ hp 起点由参数给 ⇒ 与 max_hp 拉开距离,恢复才有可观察后果(满血会被 clamp 抹平)。
BattleId startHurtPlayerBattle(Fixture &f, SA::Net::ConnectionId id, std::int32_t hp,
                               std::int32_t max_hp)
{
	SA::Rules::BattleField pf{};
	SA::Rules::Combatant &me = pf.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.hp = hp;
	me.max_hp = max_hp;
	me.attack = 100;
	me.defense = 100;
	me.quick = 500;
	me.luck = 10;
	const BattleId battle = f.world.startBattle(pf);
	REQUIRE(f.world.joinBattle(battle, id, 0));
	return battle;
}
} // namespace

TEST_CASE("捕获扣道具:抓表内怪 ⇒ 条件道具被全删,非条件道具留下(DR-BT10)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	// 524 → 需要道具 2456(NeedEnemy 表第 0 行)。
	const BattleId battle = joinWithNeedItemEnemy(f, id, 524);

	// 背包:1 个条件道具 2456 + 1 个无关道具 9999。
	const int slot_need = f.world.giveItemToPlayer(id, makeItem(2456));
	const int slot_decoy = f.world.giveItemToPlayer(id, makeItem(9999));
	REQUIRE(slot_need >= 0);
	REQUIRE(slot_decoy >= 0);
	REQUIRE(f.world.itemCount() == 2);
	REQUIRE(f.world.playerItemSlotsUsed(id) == 2);

	captureTurn(f, id, battle, SA::Rules::kSideOffset);

	// ★★ 抓到了(门 ④ 满足:背包有 2456)⇒ 宠物入槽。
	CHECK(f.world.petCount() == 1);
	CHECK(f.world.playerCaptureCount(id) == 1);

	// ★★ 条件道具 2456 被删,无关道具 9999 留下(全删只删表里列的)。
	CHECK(f.world.itemCount() == 1);                       // 池里只剩 1 个(2456 已 release)
	CHECK(f.world.playerItemSlotsUsed(id) == 1);           // 背包只剩 1 槽
	CHECK(f.world.playerItemAt(id, slot_need) == nullptr); // ★ 条件道具槽已空
	const SA::Model::Item *decoy = f.world.playerItemAt(id, slot_decoy);
	REQUIRE(decoy != nullptr);
	CHECK(decoy->item_id == 9999); // ★ 无关道具原样还在
}

TEST_CASE("捕获扣道具★★:背包缺条件道具 ⇒ 门 ④ 拦,抓不到(不是白给)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinWithNeedItemEnemy(f, id, 524); // 需要 2456

	// ★ 只给无关道具,**不给** 2456 ⇒ CaptureItemCheck 门 ④ 不过。
	REQUIRE(f.world.giveItemToPlayer(id, makeItem(9999)) >= 0);
	REQUIRE(f.world.itemCount() == 1);

	captureTurn(f, id, battle, SA::Rules::kSideOffset);

	// ★★ 抓不到:门 ④ 在 rollCapture 之前拦下 ⇒ flags=0 ⇒ 无宠物、无计数、目标留场。
	CHECK(f.world.petCount() == 0);
	CHECK(f.world.playerCaptureCount(id) == 0);
	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(SA::Rules::kSideOffset).occupied); // 目标还在场
	// ★ 无关道具没被碰(抓失败不删任何东西)。
	CHECK(f.world.itemCount() == 1);
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
}

TEST_CASE("捕获扣道具:抓不在表内的怪 ⇒ 背包一个道具都不动")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	// temp_no=1 不在 NeedEnemy 表 ⇒ isNeedCaptureItem 返 -1 ⇒ 门自动满足、无删除。
	const BattleId battle = joinWithNeedItemEnemy(f, id, 1);

	REQUIRE(f.world.giveItemToPlayer(id, makeItem(2456)) >= 0); // 就算带着 2456
	REQUIRE(f.world.itemCount() == 1);

	captureTurn(f, id, battle, SA::Rules::kSideOffset);

	CHECK(f.world.petCount() == 1);  // 照常抓到(无条件道具要求)
	CHECK(f.world.itemCount() == 1); // ★ 道具没被删(这只怪不吃道具)
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
}

TEST_CASE("捕获扣道具★★:全删 —— 同一条件 id 的多个道具一次全删(源码 :4074 不 break)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const BattleId battle = joinWithNeedItemEnemy(f, id, 524); // 需要 2456

	// ★ 背包放**两个** 2456(源码那句被注释掉的 break ⇒ 全删,不是只删一个)。
	REQUIRE(f.world.giveItemToPlayer(id, makeItem(2456)) >= 0);
	REQUIRE(f.world.giveItemToPlayer(id, makeItem(2456)) >= 0);
	REQUIRE(f.world.itemCount() == 2);

	captureTurn(f, id, battle, SA::Rules::kSideOffset);

	CHECK(f.world.petCount() == 1);
	// ★★ 两个都删掉,不是剩一个 —— 这条钉住"全删"语义。
	CHECK(f.world.itemCount() == 0);
	CHECK(f.world.playerItemSlotsUsed(id) == 0);
}

TEST_CASE("捕获扣道具:多条件道具怪(1105→3 种)—— 缺一即拦、齐备才抓并全删")
{
	// NeedEnemy 表:1105 → {1690, 1691, 1692}(源码 :3901,唯一的多道具行)。
	SUBCASE("只带 2 种(缺 1692)⇒ 门 ④ 拦,抓不到")
	{
		Fixture f;
		const SA::Net::ConnectionId id = f.transport.connect();
		const BattleId battle = joinWithNeedItemEnemy(f, id, 1105);
		REQUIRE(f.world.giveItemToPlayer(id, makeItem(1690)) >= 0);
		REQUIRE(f.world.giveItemToPlayer(id, makeItem(1691)) >= 0);

		captureTurn(f, id, battle, SA::Rules::kSideOffset);

		CHECK(f.world.petCount() == 0);  // 缺一种 ⇒ 抓不到
		CHECK(f.world.itemCount() == 2); // 没删(抓失败)
	}

	SUBCASE("三种齐备 ⇒ 抓到并三种全删")
	{
		Fixture f;
		const SA::Net::ConnectionId id = f.transport.connect();
		const BattleId battle = joinWithNeedItemEnemy(f, id, 1105);
		REQUIRE(f.world.giveItemToPlayer(id, makeItem(1690)) >= 0);
		REQUIRE(f.world.giveItemToPlayer(id, makeItem(1691)) >= 0);
		REQUIRE(f.world.giveItemToPlayer(id, makeItem(1692)) >= 0);
		REQUIRE(f.world.giveItemToPlayer(id, makeItem(9999)) >= 0); // 无关道具
		REQUIRE(f.world.itemCount() == 4);

		captureTurn(f, id, battle, SA::Rules::kSideOffset);

		CHECK(f.world.petCount() == 1);
		CHECK(f.world.itemCount() == 1); // 三种条件道具删光,只剩无关的 9999
		CHECK(f.world.playerItemSlotsUsed(id) == 1);
	}
}

TEST_CASE("捕获扣道具:giveItemToPlayer 注入 seam —— 三门与背包段边界")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();

	// 门 ①:无 L2 玩家实体(还没握手)⇒ -1,世界不动。
	CHECK(f.world.giveItemToPlayer(id, makeItem(1)) == -1);
	CHECK(f.world.itemCount() == 0);

	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	// 放入 ⇒ 落在背包段(下标 >= kStartItemArray)。
	const int slot = f.world.giveItemToPlayer(id, makeItem(42));
	REQUIRE(slot >= static_cast<int>(SA::Model::kStartItemArray));
	CHECK(f.world.itemCount() == 1);
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	const SA::Model::Item *it = f.world.playerItemAt(id, slot);
	REQUIRE(it != nullptr);
	CHECK(it->item_id == 42);
}

// ═══════════════════════════════════════════════════════════════════════════
//  掉落(道具域第三批 I.3,野怪表驱动:spawn 千分率摇 → 结算逐件随机拾取 → 灌背包)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("掉落:spawn 期摇号 —— 千分率、prob=0 不耗 rng、紧凑存保持摇号序")
{
	using SA::Rules::RulesConfig;
	// script 值 500 ⇒ rand(0,999)=500;prob=1000 场景 500<1000 必掉。够长(≫ rollSpawnStats)。
	const std::vector<int> script(40, 500);

	// A:无掉落配置 ⇒ drop_count 0,记基准取数次数(= rollSpawnStats,baselevel>0 不摇等级)。
	ScriptedRandom ra(script);
	const SA::Model::Enemy a =
	    spawnEnemy(makeWuliTemplate(), makeWuliEncounterFixedLv1(), 18, ra, RulesConfig{});
	CHECK(a.drop_count == 0);
	const int base_calls = ra.calls();

	// B:槽0/槽2 prob=1000 必掉,槽1 prob=0 跳过。
	ScriptedRandom rb(script);
	EnemyEncounter enc = makeWuliEncounterFixedLv1();
	enc.item[0] = 11;
	enc.item_prob[0] = 1000;
	enc.item[1] = 22;
	enc.item_prob[1] = 0; // ★ prob=0 ⇒ 不摇
	enc.item[2] = 33;
	enc.item_prob[2] = 1000;
	const SA::Model::Enemy b = spawnEnemy(makeWuliTemplate(), enc, 18, rb, RulesConfig{});

	// ★ 只摇 2 次(prob!=0 的两槽),prob=0 的槽1 未耗 rng ⇒ 序列不平移。
	CHECK(rb.calls() == base_calls + 2);
	// ★ 紧凑存 + 保持摇号顺序(跳过槽1 ⇒ [11,33] 而非 [11,_,33])。
	REQUIRE(b.drop_count == 2);
	CHECK(b.dropped_items[0] == 11);
	CHECK(b.dropped_items[1] == 33);
	// ★ 掉落摇号在四维之后 ⇒ 不影响四维(A/B 同 script 四维逐值相同)。
	CHECK(a.vital == b.vital);
	CHECK(a.str == b.str);
	CHECK(a.tough == b.tough);
	CHECK(a.dex == b.dex);

	// C:prob=500、rand 返回 600 ⇒ 600<500 false ⇒ 没摇中(千分率真判定,非恒掉)。
	ScriptedRandom rc(std::vector<int>(40, 600));
	EnemyEncounter enc2 = makeWuliEncounterFixedLv1();
	enc2.item[0] = 44;
	enc2.item_prob[0] = 500;
	const SA::Model::Enemy c = spawnEnemy(makeWuliTemplate(), enc2, 18, rc, RulesConfig{});
	CHECK(c.drop_count == 0);
}

namespace
{
// 强场秒杀 + 刷一只带指定 enc 的乌力 + 推进到战斗结束(端到端打赢野怪)。
void winBattleAgainst(Fixture &f, SA::Net::ConnectionId id, const EnemyEncounter &enc)
{
	SA::Rules::BattleField pf{};
	SA::Rules::Combatant &me = pf.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.hp = 100000;
	me.max_hp = 100000;
	me.attack = 100000; // 秒杀
	me.defense = 10000;
	me.quick = 500; // 先手
	me.luck = 10;
	const BattleId battle = f.world.startBattle(pf);
	REQUIRE(f.world.joinBattle(battle, id, 0));
	REQUIRE(f.world.spawnEnemyToField(battle, SA::Rules::kSideOffset, makeWuliTemplate(),
	                                  enc, /*baselevel=*/10));
	for (int i = 0; i < 30; ++i)
	{
		const SA::Rules::BattleField *fld = f.world.battleField(battle);
		if (fld == nullptr)
			break;
		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = battle;
		cmd.turn = fld->turn;
		cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd.command.attack.target = static_cast<std::uint32_t>(SA::Rules::kSideOffset);
		f.world.onBattleCommand(id, cmd);
		f.clock.advance(2000);
		f.world.tick();
	}
}
} // namespace

TEST_CASE("掉落:端到端 —— 打赢配掉落的野怪,战利品逐件进玩家背包(阶段①②③)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	REQUIRE(f.world.playerCount() == 1);
	REQUIRE(f.world.itemCount() == 0);

	// 刷必掉两件(1234 + 5678,均 1000‰)的乌力。
	EnemyEncounter enc = makeWuliEncounterFixedLv1();
	enc.item[0] = 1234;
	enc.item_prob[0] = 1000;
	enc.item[1] = 5678;
	enc.item_prob[1] = 1000;
	winBattleAgainst(f, id, enc);

	// ★ 两件都进了背包(单玩家 ⇒ 逐件都归他)。
	CHECK(f.world.itemCount() == 2);
	bool got1234 = false, got5678 = false;
	for (std::size_t s = SA::Model::kStartItemArray; s < SA::Model::kMaxItemHave; ++s)
	{
		const SA::Model::Item *pit = f.world.playerItemAt(id, static_cast<int>(s));
		if (pit == nullptr)
			continue;
		if (pit->item_id == 1234)
			got1234 = true;
		if (pit->item_id == 5678)
			got5678 = true;
	}
	CHECK(got1234);
	CHECK(got5678);
}

TEST_CASE("掉落:未配掉落打赢背包不增 + 背包满则丢弃(源码满销毁)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	REQUIRE(f.world.playerCount() == 1);

	SUBCASE("未配掉落表 ⇒ 打赢背包仍空(向后兼容,同 I.1 默认恒 0)")
	{
		winBattleAgainst(f, id, makeWuliEncounterFixedLv1()); // item_prob 全 0
		CHECK(f.world.itemCount() == 0);
	}

	SUBCASE("背包满 ⇒ 战利品丢弃、不崩、itemCount 不超")
	{
		int put = 0;
		while (f.world.giveItemToPlayer(id, makeItem(7)) >= 0)
			++put;
		const std::size_t full = f.world.itemCount();
		CHECK(f.world.playerItemSlotsUsed(id) == put); // 背包段全满
		REQUIRE(put > 0);

		EnemyEncounter enc = makeWuliEncounterFixedLv1();
		enc.item[0] = 1234;
		enc.item_prob[0] = 1000;
		enc.item[1] = 5678;
		enc.item_prob[1] = 1000;
		winBattleAgainst(f, id, enc);

		CHECK(f.world.itemCount() == full); // ★ 无空位 ⇒ 掉落丢弃,池没涨
	}
}

// ═══════════════════════════════════════════════════════════════════════════
//  使用道具(批次 I.4,道具域第四批)—— 世界侧:投影 + 扣道具
// ═══════════════════════════════════════════════════════════════════════════
//
// 世界侧要验的是 L3 之外的那两半(L3 的恢复量/区间/clamp 在 RulesBattleTest 里):
//   ① `projectItemUsePower` —— 读道具效果表 + 攻方背包,把 `power` 投影进 mods;
//   ② `consumeUsedItems`    —— applyEvents 之后扣一个 pile,归零则清槽 + release。
// ⚠️★ ② 的「清槽 + release 成对」漏一半不会有任何一处报错(同 `captureItemDelAll`)
//    ⇒ 必须同时断言 `playerItemSlotsUsed`(槽)与 `itemCount`(池)。

TEST_CASE("使用道具:端到端 —— 残血玩家喝药回血,堆叠减一(阶段①②)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	// 道具效果表:1001 号药 power = 100 ⇒ 恢复量 ∈ [90, 110](L3 侧已逐值验过区间)。
	f.world.loadItemEffects({{/*item_id=*/1001, /*heal_power=*/100}});

	const BattleId battle = startHurtPlayerBattle(f, id, /*hp=*/500, /*max_hp=*/100000);
	const int slot = f.world.giveItemToPlayer(id, makeItem(1001, /*pile=*/3));
	REQUIRE(slot >= 0);
	REQUIRE(f.world.itemCount() == 1);
	REQUIRE(f.world.playerItemPile(id, slot) == 3);

	useItemTurn(f, id, battle, slot, /*target_slot=*/0); // 给自己用

	const SA::Rules::BattleField *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	const std::int32_t hp_now = fld->at(0).hp;
	// ★★ 血确实加了,且落在原版 RAND(0.9p,1.1p) 区间内 —— 这一条同时证明
	//    「投影到了」+「L3 产了 SetHp」+「applyEvents 写回了世界」三段都通。
	CHECK(hp_now >= 500 + 90);
	CHECK(hp_now <= 500 + 110);

	// ★ 扣了一个 pile,但槽与池都还在(3 → 2)。
	CHECK(f.world.playerItemPile(id, slot) == 2);
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	CHECK(f.world.itemCount() == 1);
}

TEST_CASE("使用道具★★:最后一个 pile 用掉 ⇒ 清槽 + 释放实体(成对,阶段②)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	f.world.loadItemEffects({{1001, 100}});

	const BattleId battle = startHurtPlayerBattle(f, id, 500, 100000);
	const int slot = f.world.giveItemToPlayer(id, makeItem(1001, /*pile=*/1));
	REQUIRE(slot >= 0);
	REQUIRE(f.world.itemCount() == 1);
	REQUIRE(f.world.playerItemSlotsUsed(id) == 1);

	useItemTurn(f, id, battle, slot, 0);

	// ★★ 槽与池**同时**归零 —— 只清槽不 release 会让池只增不减(泄漏),
	//    只 release 不清槽会留下悬空句柄,两种都没有一处会报错。
	CHECK(f.world.playerItemPile(id, slot) == -1); // 空槽
	CHECK(f.world.playerItemAt(id, slot) == nullptr);
	CHECK(f.world.playerItemSlotsUsed(id) == 0);
	CHECK(f.world.itemCount() == 0);
	// 血照样回了(删道具与恢复是两件事)。
	CHECK(f.world.battleField(battle)->at(0).hp >= 500 + 90);
}

TEST_CASE("使用道具:满血喝药 ⇒ 血被 clamp 不变,但道具照样扣(battle_item.c:323 无条件删)")
{
	// ★ 原版 `ITEM_useRecovery_Battle` 末尾 `CHAR_DelItemMess` 在 `BATTLE_MultiRecovery`
	//   之后**无条件**执行 —— 满血也照扣。⚠️ 若把扣道具挂在「血有变化」上,这条会红。
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	f.world.loadItemEffects({{1001, 100}});

	const BattleId battle = startHurtPlayerBattle(f, id, /*hp=*/1000, /*max_hp=*/1000);
	const int slot = f.world.giveItemToPlayer(id, makeItem(1001, 2));
	REQUIRE(slot >= 0);

	useItemTurn(f, id, battle, slot, 0);

	CHECK(f.world.battleField(battle)->at(0).hp == 1000); // clamp ⇒ 没变
	CHECK(f.world.playerItemPile(id, slot) == 1);         // ★ 照样扣了一个
}

TEST_CASE("使用道具:道具不在效果表 ⇒ 不回血、不扣(power 投影恒 0)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	f.world.loadItemEffects({{1001, 100}}); // 表里只有 1001

	const BattleId battle = startHurtPlayerBattle(f, id, 500, 100000);
	const int slot = f.world.giveItemToPlayer(id, makeItem(/*item_id=*/9999, 2)); // 非药
	REQUIRE(slot >= 0);

	useItemTurn(f, id, battle, slot, 0);

	CHECK(f.world.battleField(battle)->at(0).hp == 500); // 没回血
	CHECK(f.world.playerItemPile(id, slot) == 2);        // ★ 也没扣 —— 用不了就不该消耗
	CHECK(f.world.itemCount() == 1);
}

TEST_CASE("使用道具:效果表默认空 ⇒ 任何道具都没效果(不注入即不受影响)")
{
	// ★ 与 `loadEncounterTables` 同取向:不注入 = 该玩法不生效,现有用例不必改。
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	// ⚠️ 故意不调 loadItemEffects

	const BattleId battle = startHurtPlayerBattle(f, id, 500, 100000);
	const int slot = f.world.giveItemToPlayer(id, makeItem(1001, 2));
	REQUIRE(slot >= 0);

	useItemTurn(f, id, battle, slot, 0);

	CHECK(f.world.battleField(battle)->at(0).hp == 500);
	CHECK(f.world.playerItemPile(id, slot) == 2);
}

TEST_CASE("使用道具:空槽 / 越界槽 / 堆叠已空 ⇒ 不回血、不扣、不崩")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	f.world.loadItemEffects({{1001, 100}});

	const BattleId battle = startHurtPlayerBattle(f, id, 500, 100000);
	// 背包里放一个**堆叠为 0** 的 1001(模拟未回填 current_pile 的写入者)。
	const int empty_pile_slot = f.world.giveItemToPlayer(id, makeItem(1001, /*pile=*/0));
	REQUIRE(empty_pile_slot >= 0);

	// ① 完全空的槽 · ② 越界槽 · ③ 堆叠为 0 的槽 —— 三者都应「什么都不发生」。
	const int free_slot = empty_pile_slot + 1;
	for (const int slot : {free_slot, -1, static_cast<int>(SA::Model::kMaxItemHave),
	                       empty_pile_slot})
	{
		useItemTurn(f, id, battle, slot, 0);
		CHECK(f.world.battleField(battle)->at(0).hp == 500);
	}
	CHECK(f.world.itemCount() == 1); // 那个 pile=0 的道具还在池里,没被误 release
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
}

TEST_CASE("使用道具★:掉落进背包的道具可以直接喝(掉落回填了 current_pile)")
{
	// ★★ 这条把 I.3(掉落)与 I.4(使用)接在一起 —— 掉落路径若忘了填 `current_pile`,
	//    道具进了背包却**用不了**,而 I.3 自己的用例(只数 itemCount)全绿。
	//    ⇒ 是「地基绿而运行时不接」(欠债 20)在两个批次接缝上的形态。
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();
	f.world.loadItemEffects({{/*item_id=*/7777, /*heal_power=*/100}});

	// 打赢一只必掉 7777 的野怪 ⇒ 战利品进背包。
	EnemyEncounter enc = makeWuliEncounterFixedLv1();
	enc.item[0] = 7777;
	enc.item_prob[0] = 1000; // 千分率 ⇒ 必掉
	winBattleAgainst(f, id, enc);
	REQUIRE(f.world.itemCount() == 1);
	REQUIRE(f.world.playerItemPile(id, SA::Model::kStartItemArray) == 1); // ★ 掉落填了 1

	// 再开一场残血战斗,把刚掉的药喝掉。
	const BattleId battle = startHurtPlayerBattle(f, id, 500, 100000);
	useItemTurn(f, id, battle, SA::Model::kStartItemArray, 0);

	CHECK(f.world.battleField(battle)->at(0).hp >= 500 + 90);
	CHECK(f.world.itemCount() == 0); // 喝光 ⇒ 清槽 + 释放
}

// ── L4.1 状态异常:世界侧写回 ───────────────────────────────────────────────
//
// ★★ 这一组存在的理由就是欠债 20 那族:L3 的状态机绿了,不代表世界态**真的**被
//    写进去。`Combatant::status` 此前**只有用例在写**(全仓实测)⇒ 没有观察面就
//    没东西能证明「中毒之后世界里真有这个状态」。

namespace
{
// 开一场「玩家带毒装备 vs 一只可被打中的敌人」的战斗。
BattleId startPoisonSuitBattle(Fixture &f, SA::Net::ConnectionId id, int suit_poison)
{
	SA::Rules::BattleField pf{};
	SA::Rules::Combatant &me = pf.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.hp = me.max_hp = 10000;
	me.attack = 5000;
	me.defense = 100;
	me.quick = 500;
	me.luck = 10;
	me.mods.suit_poison = suit_poison; // ★ 带毒装备:> 0 即开关,值即 PerOffset

	SA::Rules::Combatant &foe = pf.at(10);
	foe.occupied = true;
	foe.kind = SA::Rules::CombatantKind::kEnemy;
	foe.slot = 10;
	foe.level = 1;
	foe.hp = foe.max_hp = 100000; // 打不死 ⇒ 能看到多回合的毒
	foe.attack = 1;
	foe.defense = 1;
	foe.quick = 1;
	// 四维:体力占比 25/100 ⇒ 命中率里减 10;毒伤害 ((10000/100)-20)/4 = 20
	foe.vital = 2500;
	foe.str = 2500;
	foe.tough = 2500;
	foe.dex = 2500;

	const BattleId battle = f.world.startBattle(pf);
	REQUIRE(f.world.joinBattle(battle, id, 0));
	return battle;
}

void attackTurn(Fixture &f, SA::Net::ConnectionId id, BattleId battle, int target)
{
	SA::Domain::BattleCommand cmd{};
	cmd.battle_id = battle;
	cmd.turn = f.world.battleField(battle)->turn;
	cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
	cmd.command.attack.target = static_cast<std::uint32_t>(target);
	f.world.onBattleCommand(id, cmd);
	f.clock.advance(2000);
	f.world.tick();
}
} // namespace

TEST_CASE("状态★★:带毒装备命中 ⇒ 世界态真的写上了毒 + 4 回合(欠债 20 那族的观察面)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	// per_offset 给 200 ⇒ 夹到 80% ⇒ 绝大多数种子都能中(下面断言不依赖具体种子:
	// 只要中了,状态与回合数就必须对;没中则整组跳过并由 rng 用例覆盖判定本身)。
	const BattleId battle = startPoisonSuitBattle(f, id, /*suit_poison=*/200);
	attackTurn(f, id, battle, 10);

	const SA::Rules::Combatant &foe = f.world.battleField(battle)->at(10);
	if (foe.status != 0)
	{
		CHECK(foe.status ==
		      static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON));
		// ★★ 落地回合数 = 声明 3 + 1 = 4(`battle_event.c:2918`),**但断言的是 3**:
		//    玩家 quick 高 ⇒ 先手上毒 ⇒ 敌人在**同一回合**轮到自己时就跑一次
		//    `BATTLE_StatusSeq`,当场递减为 3 并结算一次毒伤害。
		//    ★ 这是原版结构(`battle.c:7074` 的 StatusSeq 在 actor 循环内),不是偏差。
		//    ⚠️ 我最初把它写成 4 并据此断言,是**没跟到"同回合还会推进一次"这一层**。
		CHECK(foe.status_turns == SA::Rules::statusTurnsOnApply(SA::Rules::kSuitPoisonTurns) - 1);
		CHECK(foe.status_turns == 3);
	}
}

TEST_CASE("状态★★:中毒后逐回合掉血,4 回合后世界态被清空(端到端闭环)")
{
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	const BattleId battle = startPoisonSuitBattle(f, id, /*suit_poison=*/200);

	// 直接把状态写进战场(绕开施加判定的随机性 —— 判定本身由 rules_battle 逐值验)。
	// ⚠️ 这里改的是 World 内部的 field ⇒ 用公开的 battleField 拿 const 引用不行,
	//    改用「先打一拳看中没中,没中就再打」的稳妥法:最多试 8 回合。
	int tries = 0;
	while (f.world.battleField(battle)->at(10).status == 0 && tries < 8)
	{
		attackTurn(f, id, battle, 10);
		++tries;
	}
	REQUIRE(f.world.battleField(battle)->at(10).status != 0); // 前提不成立就别信结论

	// ★ 上毒的那一回合敌人已自推进一次 ⇒ 起点是 3(见上一条用例的注记)。
	const std::int32_t turns0 = f.world.battleField(battle)->at(10).status_turns;
	REQUIRE(turns0 >= 1);
	REQUIRE(turns0 <= 3);

	// 之后每推一回合,剩余回合数递减 1;第 4 回合归零 ⇒ 状态被清空。
	std::int32_t prev_turns = turns0;
	for (int i = 0; i < 4; ++i)
	{
		attackTurn(f, id, battle, 10);
		const SA::Rules::Combatant &foe = f.world.battleField(battle)->at(10);
		if (foe.status == 0)
		{
			// ★★ 解除了 ⇒ 世界态两个字段**成对**清零(只清一个会留下幽灵回合数)。
			CHECK(foe.status_turns == 0);
			break;
		}
		CHECK(foe.status_turns < prev_turns); // 单调递减
		prev_turns = foe.status_turns;
	}
	// 4 回合内必然解除。
	CHECK(f.world.battleField(battle)->at(10).status == 0);
	CHECK(f.world.battleField(battle)->at(10).status_turns == 0);
}

TEST_CASE("状态★★:投影拷的是**原始四维**而不是推导三围(拿三围代入不会报错但数全错)")
{
	// ⚠️★ 状态系统的两条公式直接读四维:命中率的体力占比(battle_event.c:5088)与
	//    毒的每回合掉血(battle.c:5251)。⇒ 若 World 投影时漏拷四维,两条公式的输入
	//    全是 0 —— 而 attack/defense 照常有值 ⇒ **战斗照打,只有状态相关的数悄悄错**。
	Fixture f;
	const SA::Net::ConnectionId id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick();

	// 用真实敌人模板刷一只进战场 ⇒ 走 enterEnemyToField 的投影路径。
	const BattleId battle = startPoisonSuitBattle(f, id, 0);
	REQUIRE(f.world.spawnEnemyToField(battle, 11, makeWuliTemplate(),
	                                  makeWuliEncounterFixedLv1(), /*baselevel=*/18));

	const SA::Rules::Combatant &e = f.world.battleField(battle)->at(11);
	// ★ 四维必须非 0(M.4a 的 rollSpawnStats 给的真值),且与三围**不是同一组数**。
	CHECK(e.vital > 0);
	CHECK(e.str > 0);
	CHECK(e.tough > 0);
	CHECK(e.dex > 0);
	// ★★ 判据:四维之和与三围之和不相等 —— 若投影误把三围拷进四维,这条红。
	CHECK((e.vital + e.str + e.tough + e.dex) != (e.attack + e.defense + e.quick));
}
