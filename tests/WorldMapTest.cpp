// tests/WorldMapTest.cpp —— 地图通行性、玩家移动、视野广播(批次 W.1)
//
// ★ 本文件是移动系统(W.1-W.4)的用例归属:
//   里程碑① 玩家移动 + 碰撞(靠坐标断言)· 里程碑② 视野广播(靠 CA/CD 断言,后续)。
//   与 WorldTickTest(战斗生命周期)分开,免得那个 2,600 行的文件继续膨胀。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "world/Api.h"

#include <algorithm>
#include <string>

using namespace SA::World;

// ══ 里程碑①a:地图通行性(移植 MAP_walkAbleFromPoint,map_deal.c:24)═══════════
//
// fixture 图元号约定:0 墙(kBlocked) · 1 需双(kNeedBoth) · 2 地面(kFree)。

TEST_CASE("地图通行性:全地面可走,越界不可走")
{
	const GridMap map = makeFixtureMap(8, 6);
	const TileAttrTable attr = makeFixtureAttr();

	// 全地面(obj 号 2 = kFree)⇒ 界内处处可走。
	CHECK(mapWalkable(map, attr, 0, 0));
	CHECK(mapWalkable(map, attr, 7, 5));
	CHECK(mapWalkable(map, attr, 3, 3));

	// ★ 越界即不可走(原版 getTileAndObjData 失败返回 FALSE,:28)。
	CHECK_FALSE(mapWalkable(map, attr, -1, 0));
	CHECK_FALSE(mapWalkable(map, attr, 0, -1));
	CHECK_FALSE(mapWalkable(map, attr, 8, 0)); // width == 8 ⇒ x=8 越界
	CHECK_FALSE(mapWalkable(map, attr, 0, 6)); // height == 6 ⇒ y=6 越界
}

TEST_CASE("地图通行性:obj 层三值分支")
{
	GridMap map = makeFixtureMap(4, 4);
	const TileAttrTable attr = makeFixtureAttr();

	// ── obj 层 kBlocked(号 0):不可走,与 tile 层无关(:41-43)。
	map.obj[map.index(1, 1)] = 0;
	map.tile[map.index(1, 1)] = 2; // tile 是地面也没用
	CHECK_FALSE(mapWalkable(map, attr, 1, 1));

	// ── obj 层 kFree(号 2):可走(:51-52)。
	map.obj[map.index(2, 2)] = 2;
	CHECK(mapWalkable(map, attr, 2, 2));

	// ── obj 层 kNeedBoth(号 1):tile 层也必须是 kNeedBoth 才可走(:44-50)。
	map.obj[map.index(3, 3)] = 1;
	map.tile[map.index(3, 3)] = 2; // tile 是 kFree ≠ kNeedBoth ⇒ 不可走
	CHECK_FALSE(mapWalkable(map, attr, 3, 3));
	map.tile[map.index(3, 3)] = 1; // tile 也是 kNeedBoth ⇒ 可走
	CHECK(mapWalkable(map, attr, 3, 3));
}

TEST_CASE("地图通行性:未知图元号按不可走兜底")
{
	GridMap map = makeFixtureMap(3, 3);
	const TileAttrTable attr = makeFixtureAttr(); // 只登记了图元号 0/1/2

	// 图元号 99 不在属性表 ⇒ kindOf 返回 kBlocked ⇒ 不可走
	//   (对应原版 getTileAndObjData 取不到数据 / switch default,:28/:54)。
	map.obj[map.index(1, 1)] = 99;
	CHECK_FALSE(mapWalkable(map, attr, 1, 1));
}

// ══ 里程碑①b:玩家移动(onWalk + kCharLoop 玩家段)══════════════════════════
//
// 方向字符(CHAR_ctodirmode):小写 a-h 移动 / 大写 A-H 转身;dir 0-7 = 北起顺时针。
//   'a'北 · 'c'东 · 'd'东南 · 'e'南 · 'g'西。出生点 = fixture 地图中心(64/2 = 32,32)。

namespace
{

SA::Platform::ServerConfig makeMoveConfig()
{
	const SA::Platform::ConfigResult r = SA::Platform::parseConfig(R"({
    "protocol_version": 1, "log_level": "error",
    "tempo": { "tick_hz": 100, "battle_turn_interval_ms": 1000 }
  })");
	REQUIRE(r.ok);
	return r.config;
}

struct MoveFixture
{
	SA::Platform::ServerConfig config = makeMoveConfig();
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0xABCDEF};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};

	SA::Net::ConnectionId spawn()
	{
		const auto id = transport.connect(); // onConnected ⇒ 建 Conn
		world.onSessionReady(id);            // 建 Player + 出生点(地图中心)
		return id;
	}

	// req.x/y 设当前坐标 ⇒ 避开 (0,0) 门、过防瞬移(|Δ|=0)与碰撞预检(当前格可走)。
	void sendWalk(SA::Net::ConnectionId id, const char *dir)
	{
		SA::Domain::WalkRequest req{};
		const auto pos = world.playerPos(id);
		req.x = pos.x;
		req.y = pos.y;
		REQUIRE(req.direction.assign(dir));
		world.onWalk(id, req);
	}
};

} // namespace

TEST_CASE("玩家移动:走一步,坐标按方向增量变")
{
	MoveFixture f;
	const auto id = f.spawn();
	const auto p0 = f.world.playerPos(id);
	REQUIRE(p0.valid);

	f.sendWalk(id, "c"); // 'c' = dir 2 = 东(dx+1)
	f.world.tick();      // 首步立即走(last_walk_at_ms == 0)

	const auto p1 = f.world.playerPos(id);
	CHECK(p1.x == p0.x + 1);
	CHECK(p1.y == p0.y);
	CHECK(p1.dir == 2);
}

TEST_CASE("玩家移动:走路串按 walksendinterval 逐字符消费")
{
	MoveFixture f;
	const auto id = f.spawn();
	const auto p0 = f.world.playerPos(id);

	f.sendWalk(id, "cc"); // 两步东
	f.world.tick();       // 首步立即
	CHECK(f.world.playerPos(id).x == p0.x + 1);

	// 间隔未到(clock 未推进)⇒ 第二步不走(CHAR_walk_check:4590 的间隔门)。
	f.world.tick();
	CHECK(f.world.playerPos(id).x == p0.x + 1);

	// 跨过 250ms(walkinterval=2500 × 100us)⇒ 第二步走。
	f.clock.advance(250);
	f.world.tick();
	CHECK(f.world.playerPos(id).x == p0.x + 2);

	// 串已空 ⇒ 再 tick 不动。
	f.clock.advance(250);
	f.world.tick();
	CHECK(f.world.playerPos(id).x == p0.x + 2);
}

TEST_CASE("玩家移动:大写方向只转身,不移动")
{
	MoveFixture f;
	const auto id = f.spawn();
	const auto p0 = f.world.playerPos(id);

	f.sendWalk(id, "A"); // 'A' 大写 ⇒ 转身到 dir 0(北),不移动
	f.world.tick();

	const auto p1 = f.world.playerPos(id);
	CHECK(p1.x == p0.x);
	CHECK(p1.y == p0.y);
	CHECK(p1.dir == 0);
}

TEST_CASE("玩家移动:走到地图边界即停,不越界")
{
	MoveFixture f;
	const auto id = f.spawn();
	const auto p0 = f.world.playerPos(id);
	REQUIRE(p0.x == 32); // 出生点 = width/2 = 64/2

	// 一路往西(dir 6):32 步正好到 x==0(FixedStr<32> 上限 = 32)。
	f.sendWalk(id, std::string(32, 'g').c_str());
	for (int i = 0; i < 40; ++i)
	{
		f.world.tick();
		f.clock.advance(250);
	}
	CHECK(f.world.playerPos(id).x == 0);

	// 再往西 ⇒ 目标格越界(x=-1),mapWalkable 返回 false ⇒ 撞边界不动。
	f.sendWalk(id, "gg");
	for (int i = 0; i < 4; ++i)
	{
		f.world.tick();
		f.clock.advance(250);
	}
	CHECK(f.world.playerPos(id).x == 0); // 停在边界,未越界成负
}

TEST_CASE("玩家移动:斜向可走(全通行地图)")
{
	MoveFixture f;
	const auto id = f.spawn();
	const auto p0 = f.world.playerPos(id);

	f.sendWalk(id, "d"); // 'd' = dir 3 = 东南(dx+1,dy+1)
	f.world.tick();

	const auto p1 = f.world.playerPos(id);
	CHECK(p1.x == p0.x + 1);
	CHECK(p1.y == p0.y + 1);
}

// ══ 里程碑②:视野广播(529 格 CA/CD/Move,靠 mirror 断言"A 动 B 收到")════════════

namespace
{

// 只解视野下行的最小客户端镜像(仿 WorldTickTest 的 ClientMirror,只关心 CA/CD/Move)。
//   ⚠️ transport.sent() 是累积字节 ⇒ 记 consumed,每次只解新增段。
struct VisMirror
{
	SA::Net::FrameReader reader;
	std::size_t consumed = 0;
	std::vector<std::uint64_t> appears;
	std::vector<std::uint64_t> moves;
	std::vector<std::uint64_t> disappears;

	void feed(const std::vector<std::uint8_t> &sent)
	{
		if (sent.size() <= consumed)
			return;
		REQUIRE(reader.push(sent.data() + consumed, sent.size() - consumed));
		consumed = sent.size();
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
			SA::IDL::Reader rd(env.body, env.body_len);
			switch (static_cast<SA::IDL::MsgId>(env.msg_id))
			{
			case SA::IDL::MsgId::CharAppear:
			{
				SA::Domain::CharAppear m;
				decode(rd, m);
				appears.push_back(m.entity_id);
				break;
			}
			case SA::IDL::MsgId::CharMove:
			{
				SA::Domain::CharMove m;
				decode(rd, m);
				moves.push_back(m.entity_id);
				break;
			}
			case SA::IDL::MsgId::CharDisappear:
			{
				SA::Domain::CharDisappear m;
				decode(rd, m);
				disappears.push_back(m.entity_id);
				break;
			}
			default:
				break;
			}
			reader.pop();
		}
	}
};

int countId(const std::vector<std::uint64_t> &v, std::uint64_t id)
{
	return static_cast<int>(std::count(v.begin(), v.end(), id));
}

} // namespace

TEST_CASE("视野:B 看到 A 的出现 / 移动 / 消失(双向 529 格)")
{
	MoveFixture f;
	const auto a = f.spawn(); // A 在地图中心 (32,32)
	const auto b = f.spawn(); // B 同格 ⇒ B 进图时与 A 双向 CharAppear
	f.world.tick();           // flush outbound → transport.sent

	VisMirror mb;
	mb.feed(f.transport.sent(b));
	CHECK(countId(mb.appears, a) >= 1); // B 看到 A 出现
	CHECK(countId(mb.disappears, a) == 0);

	// A 在视野内走一步 ⇒ B 收到 A 的 CharMove(不是 Appear:A 一直可见)。
	f.sendWalk(a, "c");
	f.world.tick();
	mb.feed(f.transport.sent(b));
	CHECK(countId(mb.moves, a) >= 1);
	CHECK(countId(mb.disappears, a) == 0);

	// A 一路往东走出视野(半径 11):走够步数后 B 收到 A 的 CharDisappear。
	f.sendWalk(a, std::string(15, 'c').c_str());
	for (int i = 0; i < 20; ++i)
	{
		f.world.tick();
		f.clock.advance(250);
	}
	mb.feed(f.transport.sent(b));
	CHECK(countId(mb.disappears, a) >= 1);
}

TEST_CASE("视野:视野外的移动不广播给对方")
{
	MoveFixture f;
	const auto a = f.spawn();
	const auto b = f.spawn();

	// 先让 A 走出 B 视野(收 CD 后清点基线)。
	f.sendWalk(a, std::string(15, 'c').c_str());
	for (int i = 0; i < 20; ++i)
	{
		f.world.tick();
		f.clock.advance(250);
	}
	VisMirror mb;
	mb.feed(f.transport.sent(b));
	REQUIRE(countId(mb.disappears, a) >= 1);
	const int moves_before = countId(mb.moves, a);

	// A 在视野外(远处)继续走 ⇒ B 不该再收到 A 的 CharMove。
	f.sendWalk(a, "cc");
	for (int i = 0; i < 4; ++i)
	{
		f.world.tick();
		f.clock.advance(250);
	}
	mb.feed(f.transport.sent(b));
	CHECK(countId(mb.moves, a) == moves_before);
}
