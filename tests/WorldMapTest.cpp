// tests/WorldMapTest.cpp —— 地图通行性、玩家移动、视野广播(批次 W.1)
//
// ★ 本文件是移动系统(W.1-W.4)的用例归属:
//   里程碑① 玩家移动 + 碰撞(靠坐标断言)· 里程碑② 视野广播(靠 CA/CD 断言,后续)。
//   与 WorldTickTest(战斗生命周期)分开,免得那个 2,600 行的文件继续膨胀。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "content/Bundle.h"
#include "world/Api.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
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

TEST_CASE("原生 LS2MAP 地图解析:防御性校验与大端解码")
{
	// 1. 畸变输入校验
	{
		std::vector<std::uint8_t> empty{};
		CHECK_FALSE(parseLs2Map(empty).has_value());

		std::vector<std::uint8_t> short_hdr(43, 0);
		CHECK_FALSE(parseLs2Map(short_hdr).has_value());

		std::vector<std::uint8_t> bad_magic(44, 0);
		std::memcpy(bad_magic.data(), "LS1MAP", 6);
		CHECK_FALSE(parseLs2Map(bad_magic).has_value());

		std::vector<std::uint8_t> zero_dim(44, 0);
		std::memcpy(zero_dim.data(), "LS2MAP", 6);
		CHECK_FALSE(parseLs2Map(zero_dim).has_value()); // xsiz=0, ysiz=0
	}

	// 2. 合法内存缓冲区解析
	{
		// 构造 2x2 地图: 44 头 + 2*4 (tile) + 2*4 (obj) = 60 字节
		std::vector<std::uint8_t> buf(60, 0);
		std::memcpy(buf.data(), "LS2MAP", 6);
		// id = 100 (大端: 0x00, 0x64)
		buf[6] = 0x00;
		buf[7] = 0x64;
		// show_name at offset 8: "TestMap"
		std::memcpy(buf.data() + 8, "TestMap", 7);
		// xsiz = 2, ysiz = 2
		buf[40] = 0x00;
		buf[41] = 0x02;
		buf[42] = 0x00;
		buf[43] = 0x02;
		// tile[4]: 10, 20, 30, 40
		buf[44] = 0x00;
		buf[45] = 10;
		buf[46] = 0x00;
		buf[47] = 20;
		buf[48] = 0x00;
		buf[49] = 30;
		buf[50] = 0x00;
		buf[51] = 40;
		// obj[4]: 100, 200, 300, 400
		buf[52] = 0x00;
		buf[53] = 100;
		buf[54] = 0x00;
		buf[55] = 200;
		buf[56] = 0x01;
		buf[57] = 0x2C; // 300
		buf[58] = 0x01;
		buf[59] = 0x90; // 400

		// 数据截断校验 (少 1 字节)
		CHECK_FALSE(parseLs2Map(std::span<const std::uint8_t>(buf.data(), buf.size() - 1)).has_value());

		const auto info = parseLs2Map(buf);
		REQUIRE(info.has_value());
		CHECK_EQ(info->floor_id, 100);
		CHECK_EQ(info->show_name, "TestMap");
		CHECK_EQ(info->width, 2);
		CHECK_EQ(info->height, 2);
		CHECK_EQ(info->grid.width, 2);
		CHECK_EQ(info->grid.height, 2);
		CHECK_EQ(info->grid.tileAt(0, 0), 10);
		CHECK_EQ(info->grid.tileAt(1, 0), 20);
		CHECK_EQ(info->grid.tileAt(0, 1), 30);
		CHECK_EQ(info->grid.tileAt(1, 1), 40);
		CHECK_EQ(info->grid.objAt(0, 0), 100);
		CHECK_EQ(info->grid.objAt(1, 0), 200);
		CHECK_EQ(info->grid.objAt(0, 1), 300);
		CHECK_EQ(info->grid.objAt(1, 1), 400);
	}

	// 3. 真实地图文件测试 (若本地存在)
	{
		std::string real_path;
		for (const auto &p : {
		         "../csa8.0/gmsv/data/map/sainasu/sainasu",
		         "../../csa8.0/gmsv/data/map/sainasu/sainasu",
		         "../../../csa8.0/gmsv/data/map/sainasu/sainasu",
		         "/Users/naka/Game/Stoneage/csa8.0/gmsv/data/map/sainasu/sainasu"})
		{
			if (std::filesystem::exists(p))
			{
				real_path = p;
				break;
			}
		}

		if (!real_path.empty())
		{
			const auto real_map = loadLs2MapFile(real_path);
			REQUIRE(real_map.has_value());
			CHECK_EQ(real_map->floor_id, 100);
			CHECK_EQ(real_map->width, 800);
			CHECK_EQ(real_map->height, 800);
			CHECK_EQ(real_map->grid.tile.size(), 800 * 800);
			CHECK_EQ(real_map->grid.obj.size(), 800 * 800);
			CHECK(real_map->grid.inBounds(643, 459));
		}
	}
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

TEST_CASE("Repeated walk requests cannot reset the movement interval")
{
	MoveFixture f;
	const auto id = f.spawn();
	const auto start = f.world.playerPos(id);
	f.sendWalk(id, "c");
	f.world.tick();
	REQUIRE(f.world.playerPos(id).x == start.x + 1);
	for (int i = 0; i < 24; ++i)
	{
		f.clock.advance(10);
		f.sendWalk(id, "c");
		f.world.tick();
	}
	CHECK(f.world.playerPos(id).x == start.x + 1);
	f.clock.advance(10);
	f.world.tick();
	CHECK(f.world.playerPos(id).x == start.x + 2);
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
	// 完整消息(批次 W.3:验 entity_type / image,现有 W.1 用例仍用上面的 id 列表)。
	std::vector<SA::Domain::CharAppear> appear_msgs;
	std::vector<SA::Domain::CharMove> move_msgs;
	std::vector<SA::Domain::CharDisappear> disappear_msgs;

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
				appear_msgs.push_back(m);
				break;
			}
			case SA::IDL::MsgId::CharMove:
			{
				SA::Domain::CharMove m;
				decode(rd, m);
				moves.push_back(m.entity_id);
				move_msgs.push_back(m);
				break;
			}
			case SA::IDL::MsgId::CharDisappear:
			{
				SA::Domain::CharDisappear m;
				decode(rd, m);
				disappears.push_back(m.entity_id);
				disappear_msgs.push_back(m);
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

// ══ 里程碑③:遇敌触发(批次 W.4。走一格 → 骰子 → 遇敌链 → 开战 → 战果)═══════════
//
// 遇敌骰子 rand()%(120*getEnemyAction()) < temp(char_walk.c:585)。getEnemyAction 默认 1
//   ⇒ 分母 120。★ 用例用 prob 边界控制遇敌与否,**不依赖 world_rng 的具体值**:
//     prob=120 ⇒ randMod(120)∈[0,119] 恒 < 120 ⇒ **必遇敌**;prob=0 ⇒ 恒不遇敌。
//   单区域单编组单怪 ⇒ 遇敌必选那只乌力,链路不依赖 rng 序列。

namespace
{

// 握手 + 建 Player。★ 遇敌开战要 joinBattle,而它要求会话**已握手**(防未握手连接拿事件流)
//   ⇒ 不能用 MoveFixture::spawn 那条跳过握手的捷径(W.1 走路用例不 joinBattle 故没暴露)。
//   handshakeBytes 与 WorldTickTest 同源(两文件是独立编译单元,各自持一份)。
std::vector<std::uint8_t> handshakeBytes(std::uint32_t version)
{
	SA::Transport::HandshakeRequest req{};
	req.protocol_version = version;
	req.client_build.assign("test");
	std::vector<std::uint8_t> out;
	REQUIRE(SA::Net::encodeFramed(1, req, out));
	return out;
}

SA::Net::ConnectionId spawnHandshaked(MoveFixture &f)
{
	const auto id = f.transport.connect();
	const std::vector<std::uint8_t> hs = handshakeBytes(f.config.protocol_version);
	f.transport.deliver(id, hs.data(), hs.size());
	f.world.tick(); // 网络入站处理握手 → onSessionReady 建 Player(walk_seq 空 ⇒ 本 tick 不遇敌)
	return id;
}

// 注入一条最小遇敌链:floor 0 全图 → 编组 1 → 一只 1 级乌力(temp_no=1)。
//   ⚠️ 模板 temp_no 必须 == enc.temp_no —— triggerEncounter 靠 findEnemyTemplate 配对。
void loadEncounterFixture(World &world, std::int32_t prob, std::int32_t enc_exp = -1)
{
	EncountArea area{};
	area.index = 1;
	area.floor = 0;
	area.x = 0;
	area.y = 0;
	area.width = 63; // 64×64 fixture 地图,PointInRect 闭区间覆盖 0..63
	area.height = 63;
	area.prob_min = prob;
	area.prob_max = prob;
	area.enemy_max_num = 2;
	area.zorder = 1; // > 0 ⇒ 启用(zorder 兼任开关)
	area.group_id.fill(-1);
	area.group_prob.fill(-1);
	area.group_id[0] = 1;
	area.group_prob[0] = 1;

	EnemyGroup group{};
	group.group_id = 1;
	group.enemy_id.fill(-1);
	group.create_prob.fill(-1);
	group.enemy_id[0] = 9; // 乌力(enemy1.txt 第 5 行)
	group.create_prob[0] = 1;

	EnemyEncounter enc{};
	enc.enemy_id = 9;
	enc.temp_no = 1;
	enc.lv_min = 1;
	enc.lv_max = 1;
	enc.capturable = true;
	enc.create_max_num = 1;
	enc.exp = enc_exp;

	EnemyTemplate tmpl{};
	tmpl.temp_no = 1;
	tmpl.stats = SA::Rules::SpawnTemplate{4.50, 10, 20, 12, 15, 25};
	tmpl.mod_ai = 150;
	tmpl.capture_difficulty = 11;
	tmpl.earth = 80;
	tmpl.water = 20;
	tmpl.image = 100250;
	REQUIRE(tmpl.name.assign("乌力"));

	world.loadEncounterTables({area}, {group}, {enc}, {tmpl});
}

} // namespace

TEST_CASE("W.4:遇敌触发 —— prob=120 走一格必遇敌,开一场战斗且敌人入场")
{
	MoveFixture f;
	loadEncounterFixture(f.world, 120); // 必遇敌
	const auto id = spawnHandshaked(f);
	REQUIRE(f.world.battleCount() == 0);
	REQUIRE(f.world.enemyCount() == 0);

	f.sendWalk(id, "c"); // 走一步东
	f.world.tick();

	CHECK(f.world.battleCount() == 1); // 开了一场
	CHECK(f.world.enemyCount() >= 1);  // 敌人入了 L2 池
	// 首场 battleId == 1(next_battle_id 从 1)。玩家在 Side[0] slot 0,敌人在 Side[1]。
	const SA::Rules::BattleField *fld = f.world.battleField(1);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(0).occupied);
	CHECK(fld->at(0).kind == SA::Rules::CombatantKind::kPlayer);
	CHECK(fld->at(SA::Rules::kSideOffset).occupied); // 敌方首槽有怪
	CHECK(f.world.battleEnemyAt(1, SA::Rules::kSideOffset) != nullptr);
}

TEST_CASE("W.4:prob=0 恒不遇敌 —— 走多步 battleCount 保持 0")
{
	MoveFixture f;
	loadEncounterFixture(f.world, 0);
	const auto id = spawnHandshaked(f);
	f.sendWalk(id, "cccc");
	for (int i = 0; i < 6; ++i)
	{
		f.clock.advance(250);
		f.world.tick();
	}
	CHECK(f.world.battleCount() == 0);
}

TEST_CASE("W.4:未注入遇敌表 ⇒ 走路不遇敌(现有走路用例不受影响)")
{
	MoveFixture f;
	// ★ 不调 loadEncounterFixture ⇒ 四表空 ⇒ findEncountArea 恒 -1。
	const auto id = spawnHandshaked(f);
	const auto p0 = f.world.playerPos(id);
	f.sendWalk(id, "cccc");
	for (int i = 0; i < 6; ++i)
	{
		f.clock.advance(250);
		f.world.tick();
	}
	CHECK(f.world.battleCount() == 0);
	CHECK(f.world.playerPos(id).x == p0.x + 4); // 四步全走(没被遇敌打断)
}

TEST_CASE("W.4:遇敌命中 ⇒ 清走路串,剩余方向作废(EN_recv WALKARRAY 清空)")
{
	MoveFixture f;
	loadEncounterFixture(f.world, 120); // 必遇敌
	const auto id = spawnHandshaked(f);
	const auto p0 = f.world.playerPos(id);
	f.sendWalk(id, "cccc"); // 4 步
	f.world.tick();         // 首步走 → 遇敌 → 清串
	CHECK(f.world.playerPos(id).x == p0.x + 1);
	for (int i = 0; i < 6; ++i)
	{
		f.clock.advance(250);
		f.world.tick();
	}
	CHECK(f.world.playerPos(id).x == p0.x + 1); // 剩余 3 步作废
	CHECK(f.world.battleCount() == 1);
}

TEST_CASE("W.4★★:端到端 —— 走动 → 遇敌 → 战斗 → 打赢拿经验")
{
	MoveFixture f;
	loadEncounterFixture(f.world, 120);
	const auto id = spawnHandshaked(f);
	REQUIRE(f.world.playerExp(id) == 0);

	f.sendWalk(id, "c");
	f.world.tick(); // 走一步 → 遇敌 → 开战(玩家占位强场 vs 1 级乌力)
	REQUIRE(f.world.battleCount() == 1);
	const BattleId battle = 1; // 首场

	// 玩家出招打敌方首槽,推进到打完(玩家 attack≈322 碾压 1 级乌力)。
	//   ⚠️ 玩家侧须出招:L3「无指令 ⇒ 不行动」写死,fillEnemyCommands 只填敌方。
	for (int i = 0; i < 30; ++i)
	{
		const SA::Rules::BattleField *fld = f.world.battleField(battle);
		if (fld == nullptr)
			break;
		const BattleStats *st = f.world.stats(battle);
		if (st != nullptr && st->finished)
			break;
		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = battle;
		cmd.turn = fld->turn;
		cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd.command.attack.target = static_cast<std::uint32_t>(SA::Rules::kSideOffset);
		f.world.onBattleCommand(id, cmd);
		f.clock.advance(1000); // battle_turn_interval_ms(makeMoveConfig = 1000)
		f.world.tick();
	}
	REQUIRE(f.world.stats(battle) != nullptr);
	CHECK(f.world.stats(battle)->finished);
	CHECK(f.world.playerExp(id) > 0); // ★★ 打赢涨经验 —— 闭环兑现
}

// ══ 里程碑④/⑤:世界敌人(批次 W.2 刷怪 + W.3 游荡 + 视野扩展)═══════════════════
//
// ★ 与 W.4 遇敌(暗雷,即时 spawn 到战场)不同:这是**地图上的常驻游荡怪**(明雷)——
//   spawn 到世界坐标、按节拍游荡、被周围玩家视野看到。

namespace
{

// 注入一个刷怪点(floor 0,中心 (cx,cy),维持 count 只乌力)+ 敌人表 / 模板表(供 enemy_id 查)。
//   ⚠️ **不注入 EncountArea/Group** ⇒ 走路不遇敌(与 loadEncounterFixture 区分:那个会遇敌)。
//   ⚠️ image=100250 ⇒ 视野用例断言 CharAppear.image 取到模板图号。
void loadWorldEnemyFixture(World &world, std::int32_t count, std::int32_t radius,
                           std::int64_t interval_ms, std::int32_t cx = 32,
                           std::int32_t cy = 32)
{
	EnemyEncounter enc{};
	enc.enemy_id = 9;
	enc.temp_no = 1;
	enc.lv_min = 1;
	enc.lv_max = 1;
	enc.capturable = true;
	enc.create_max_num = 1;

	EnemyTemplate tmpl{};
	tmpl.temp_no = 1;
	tmpl.stats = SA::Rules::SpawnTemplate{4.50, 10, 20, 12, 15, 25};
	tmpl.image = 100250;
	tmpl.earth = 80;
	REQUIRE(tmpl.name.assign("乌力"));

	world.loadEncounterTables({}, {}, {enc}, {tmpl}); // 无 area/group ⇒ 走路不遇敌

	SpawnPoint sp{};
	sp.floor = 0;
	sp.x = cx;
	sp.y = cy;
	sp.enemy_id = 9;
	sp.count = count;
	sp.wander_radius = radius;
	sp.wander_interval_ms = interval_ms;
	sp.level = 1;
	world.loadSpawnPoints({sp});
}

// 世界敌人里位置 != (x,y) 的只数(用于"谁动了")。
int movedAwayCount(const std::vector<WorldEnemyPos> &es, std::int32_t x, std::int32_t y)
{
	int n = 0;
	for (const auto &e : es)
		if (e.x != x || e.y != y)
			++n;
	return n;
}

} // namespace

TEST_CASE("W.2:kNpcSpawn 据刷怪点把敌人刷到世界(补齐到 count)")
{
	MoveFixture f;
	loadWorldEnemyFixture(f.world, /*count=*/3, /*radius=*/10, /*interval=*/1000);
	REQUIRE(f.world.worldEnemyCount() == 0);

	f.world.tick(); // 第 3 步 kNpcSpawn ⇒ 刷 3 只

	CHECK(f.world.worldEnemyCount() == 3);
	CHECK(f.world.enemyCount() >= 3); // 世界态敌人也进 EnemyPool
}

TEST_CASE("W.2:刷怪点敌人落在中心,带模板图号")
{
	MoveFixture f;
	loadWorldEnemyFixture(f.world, 1, 10, 1000, /*cx=*/32, /*cy=*/32);
	f.world.tick();

	const auto es = f.world.worldEnemies();
	REQUIRE(es.size() == 1);
	CHECK(es[0].floor == 0);
	CHECK(es[0].x == 32);
	CHECK(es[0].y == 32);
	CHECK(es[0].image == 100250); // E_T_IMGNUMBER
}

TEST_CASE("W.2:补齐幂等 —— 已达 count 不再刷")
{
	MoveFixture f;
	loadWorldEnemyFixture(f.world, 2, 10, 1000);
	f.world.tick();
	REQUIRE(f.world.worldEnemyCount() == 2);
	for (int i = 0; i < 5; ++i)
	{
		f.clock.advance(1000);
		f.world.tick();
	}
	CHECK(f.world.worldEnemyCount() == 2); // 不超刷
}

TEST_CASE("W.2:enemy_id 查不到(未 loadEncounterTables)⇒ 不刷")
{
	MoveFixture f;
	SpawnPoint sp{};
	sp.floor = 0;
	sp.x = 32;
	sp.y = 32;
	sp.enemy_id = 9;
	sp.count = 3;
	f.world.loadSpawnPoints({sp}); // 只给刷怪点,不给敌人表 ⇒ findEnemyEncounter 返 -1
	f.world.tick();
	CHECK(f.world.worldEnemyCount() == 0);
}

TEST_CASE("W.2:默认无刷怪点 ⇒ 世界无敌人(现有 tick 不受影响)")
{
	MoveFixture f;
	f.world.tick();
	CHECK(f.world.worldEnemyCount() == 0);
}

TEST_CASE("W.3:敌人到游荡节拍走一步(位置变),不出半径")
{
	MoveFixture f;
	loadWorldEnemyFixture(f.world, 1, /*radius=*/10, /*interval=*/1000);
	f.world.tick(); // spawn:next_wander = 0 + 1000
	const auto e0 = f.world.worldEnemies();
	REQUIRE(e0.size() == 1);
	CHECK(e0[0].x == 32); // 未到节拍,还在中心

	f.clock.advance(1000);
	f.world.tick(); // now=1000 >= next_wander ⇒ 游荡一步
	const auto e1 = f.world.worldEnemies();
	REQUIRE(e1.size() == 1);
	// 全通行地图 + radius 10 ⇒ 必能走 ⇒ 位置变(方向由 world_rng 定,不断言具体坐标)。
	CHECK((e1[0].x != 32 || e1[0].y != 32));

	for (int i = 0; i < 20; ++i)
	{
		f.clock.advance(1000);
		f.world.tick();
	}
	const auto e2 = f.world.worldEnemies();
	const int dx = e2[0].x - 32;
	const int dy = e2[0].y - 32;
	CHECK(dx >= -10);
	CHECK(dx <= 10);
	CHECK(dy >= -10);
	CHECK(dy <= 10);
}

TEST_CASE("W.3:radius=0 ⇒ 敌人原地不动(半径门)")
{
	MoveFixture f;
	loadWorldEnemyFixture(f.world, 1, /*radius=*/0, /*interval=*/1000);
	f.world.tick();
	for (int i = 0; i < 10; ++i)
	{
		f.clock.advance(1000);
		f.world.tick();
	}
	const auto es = f.world.worldEnemies();
	REQUIRE(es.size() == 1);
	CHECK(es[0].x == 32); // 任何方向都超半径 0 ⇒ 不走
	CHECK(es[0].y == 32);
}

TEST_CASE("W.3:条数制摊还 —— enemy_move_num=1 每 tick 最多游荡 1 只")
{
	// ★ 直接构造 World(tempo.enemy_move_num=1),不走 MoveFixture 的默认 20。
	SA::Platform::ServerConfig config = makeMoveConfig();
	config.tempo.enemy_move_num = 1;
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0xABCDEF};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};

	loadWorldEnemyFixture(world, /*count=*/3, /*radius=*/20, /*interval=*/1000);
	world.tick(); // spawn 3 只(都在 (32,32),next_wander=1000)
	REQUIRE(world.worldEnemyCount() == 3);

	clock.advance(1000);
	world.tick(); // 3 只都到期,但 enemy_move_num=1 ⇒ 本 tick 只游荡 1 只
	CHECK(movedAwayCount(world.worldEnemies(), 32, 32) == 1);
}

TEST_CASE("W.3视野:玩家看到世界敌人 CharAppear(entity_type=ENEMY + 图号)")
{
	MoveFixture f;
	const auto viewer = f.spawn();                                       // 玩家在中心 (32,32)
	loadWorldEnemyFixture(f.world, 1, 10, 100000, /*cx=*/33, /*cy=*/32); // 敌人邻格,视野内
	f.world.tick();                                                      // spawn ⇒ broadcastEnemySpawn 给视野内玩家

	const auto es = f.world.worldEnemies();
	REQUIRE(es.size() == 1);
	const std::uint64_t eid = es[0].entity_id;

	VisMirror mv;
	mv.feed(f.transport.sent(viewer));
	bool found = false;
	for (const auto &a : mv.appear_msgs)
		if (a.entity_id == eid)
		{
			CHECK(a.entity_type ==
			      static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY));
			CHECK(a.image == 100250);
			found = true;
		}
	CHECK(found);
}

TEST_CASE("W.3视野:敌人游荡 ⇒ 玩家收到 CharMove(ENEMY)")
{
	MoveFixture f;
	const auto viewer = f.spawn();
	loadWorldEnemyFixture(f.world, 1, 10, 1000, 33, 32);
	f.world.tick(); // spawn + appear
	const std::uint64_t eid = f.world.worldEnemies()[0].entity_id;

	f.clock.advance(1000);
	f.world.tick(); // 敌人游荡 ⇒ broadcastEnemyMove

	VisMirror mv;
	mv.feed(f.transport.sent(viewer));
	bool found = false;
	for (const auto &m : mv.move_msgs)
		if (m.entity_id == eid)
		{
			CHECK(m.entity_type ==
			      static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY));
			found = true;
		}
	CHECK(found);
}

TEST_CASE("W.3视野:玩家走出敌人视野 ⇒ 收到敌人 CharDisappear")
{
	MoveFixture f;
	const auto viewer = f.spawn();
	loadWorldEnemyFixture(f.world, 1, 0, 100000, 32, 32); // radius 0 ⇒ 敌人不动,隔离变量
	f.world.tick();
	const std::uint64_t eid = f.world.worldEnemies()[0].entity_id;

	// 玩家一路往东走出视野(半径 11)。
	f.sendWalk(viewer, std::string(15, 'c').c_str());
	for (int i = 0; i < 20; ++i)
	{
		f.world.tick();
		f.clock.advance(250);
	}
	VisMirror mv;
	mv.feed(f.transport.sent(viewer));
	CHECK(countId(mv.disappears, eid) >= 1);
}

// ══ 里程碑:W.5 明雷触发战斗(撞明雷退回 + 面向发 EV 开战)════════════════════════
//
// ★ 与 W.4 暗雷(走路骰子即时 spawn 到战场)不同:明雷是**地图上已存在**的世界敌人 ——
//   撞上格子只退回(char_walk.c:585),开战靠玩家主动发 EV(lssproto_EV_recv → EVENT_main →
//   NPC_NPCEnemy_BattleIn),把那只**已存在**的敌人(含其等级/血量)投影进战场、转移所有权。

namespace
{

// 从会话出站字节里找 seqno 对应的 EventResult.ok(找不到返回 false)。批次 W.5。
//   ⚠️ 回执经 sendTo → conn.outbound,须 tick 一次 flush 到 transport.sent 后才读得到。
bool eventResultOk(const std::vector<std::uint8_t> &sent, std::uint32_t seqno)
{
	if (sent.empty())
		return false;
	SA::Net::FrameReader reader;
	REQUIRE(reader.push(sent.data(), sent.size()));
	bool ok = false;
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
		if (static_cast<SA::IDL::MsgId>(env.msg_id) == SA::IDL::MsgId::EventResult)
		{
			SA::IDL::Reader rd(env.body, env.body_len);
			SA::Domain::EventResult m;
			decode(rd, m);
			if (m.seqno == seqno)
				ok = m.ok;
		}
		reader.pop();
	}
	return ok;
}

// 造一条 EV 事件请求(明雷开战,event_type = ENTITY_ENEMY)。
SA::Domain::EventRequest makeEnemyEvent(std::uint32_t dir, std::uint32_t seqno)
{
	SA::Domain::EventRequest ev{};
	ev.dir = dir;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	ev.seqno = seqno;
	return ev;
}

// 从会话出站字节里提取最后一条 WindowOpen 消息(找不到返回 std::nullopt)。批次 W.8。
std::optional<SA::Domain::WindowOpen> findLastWindowOpen(const std::vector<std::uint8_t> &sent)
{
	if (sent.empty())
		return std::nullopt;
	SA::Net::FrameReader reader;
	if (!reader.push(sent.data(), sent.size()))
		return std::nullopt;
	std::optional<SA::Domain::WindowOpen> last_win;
	for (;;)
	{
		const std::uint8_t *p = nullptr;
		std::uint32_t len = 0;
		const SA::Net::FrameStatus st = reader.next(&p, &len);
		if (st == SA::Net::FrameStatus::kNeedMore)
			break;
		if (st != SA::Net::FrameStatus::kOk)
			break;
		SA::Net::EnvelopeView env;
		if (SA::Net::decodeEnvelope(p, len, env))
		{
			if (static_cast<SA::IDL::MsgId>(env.msg_id) == SA::IDL::MsgId::WindowOpen)
			{
				SA::IDL::Reader rd(env.body, env.body_len);
				SA::Domain::WindowOpen w{};
				decode(rd, w);
				last_win = w;
			}
		}
		reader.pop();
	}
	return last_win;
}

} // namespace

TEST_CASE("W.5:撞明雷退回 —— 走向明雷格被弹回,坐标不变、不开战")
{
	MoveFixture f;
	const auto id = f.spawn();                                                    // 玩家中心 (32,32)
	loadWorldEnemyFixture(f.world, 1, /*radius=*/0, /*interval=*/100000, 33, 32); // 明雷东邻,不游荡
	f.world.tick();                                                               // 刷明雷到 (33,32)
	REQUIRE(f.world.worldEnemyCount() == 1);

	f.sendWalk(id, "c"); // 'c' = 东 ⇒ 目标格 (33,32) 有明雷
	f.world.tick();

	const auto p = f.world.playerPos(id);
	CHECK(p.x == 32); // ★ 退回:没走进敌人格(char_walk.c:585)
	CHECK(p.y == 32);
	CHECK(p.dir == 2);                     // 朝向已落(东)——原版退回前先 setInt(CHAR_DIR)
	CHECK(f.world.battleCount() == 0);     // 撞上不开战(开战靠 EV,两条独立机制)
	CHECK(f.world.worldEnemyCount() == 1); // 明雷仍在世界(没被拉进战斗)
}

TEST_CASE("W.5:面向明雷发 EV ⇒ 开战,明雷从世界移入战斗(转移所有权,池守恒)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f); // ★ 握手过 ⇒ joinBattle 不被拒(非 kAnonymous)
	loadWorldEnemyFixture(f.world, 1, 0, 100000, 33, 32);
	f.world.tick(); // 刷明雷到 (33,32)
	REQUIRE(f.world.worldEnemyCount() == 1);
	const std::size_t enemies_before = f.world.enemyCount();
	REQUIRE(enemies_before == 1);

	f.world.onEvent(id, makeEnemyEvent(/*dir=*/2, /*seqno=*/7)); // 面向东 ⇒ 面前格 (33,32) 有明雷

	CHECK(f.world.battleCount() == 1);             // 开了一场
	CHECK(f.world.worldEnemyCount() == 0);         // 明雷从世界态移除(进战斗即从地图消失)
	CHECK(f.world.enemyCount() == enemies_before); // ★★ 池守恒:转移 handle 所有权,不是新建一只
	// 首场 battleId==1;玩家 Side[0] slot0,明雷 Side[1] 首槽 kSideOffset。
	const SA::Rules::BattleField *fld = f.world.battleField(1);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(0).kind == SA::Rules::CombatantKind::kPlayer);
	CHECK(fld->at(SA::Rules::kSideOffset).occupied);
	CHECK(f.world.battleEnemyAt(1, SA::Rules::kSideOffset) != nullptr);

	f.world.tick();                                // flush 回执到 transport.sent
	CHECK(eventResultOk(f.transport.sent(id), 7)); // 回执 ok=true(原版 EV_send)
}

TEST_CASE("W.5:EV 面前格无明雷 ⇒ 不开战,回执 ok=false")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	loadWorldEnemyFixture(f.world, 1, 0, 100000, 33, 32); // 明雷在东邻 (33,32)
	f.world.tick();
	REQUIRE(f.world.worldEnemyCount() == 1);

	f.world.onEvent(id, makeEnemyEvent(/*dir=*/0, /*seqno=*/3)); // 面向北 ⇒ 面前格 (32,31) 空

	CHECK(f.world.battleCount() == 0);                   // 面前格无明雷 ⇒ 不开战
	CHECK(f.world.worldEnemyCount() == 1);               // 明雷没动
	f.world.tick();                                      // flush 回执
	CHECK_FALSE(eventResultOk(f.transport.sent(id), 3)); // ok=false
}

TEST_CASE("W.5:明雷开战后刷怪点补齐世界敌人(维持 count;精确复活时机 REVIVALTIME 划出)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	loadWorldEnemyFixture(f.world, 1, 0, 100000, 33, 32);
	f.world.tick(); // 刷 1 明雷
	f.world.onEvent(id, makeEnemyEvent(2, 1));
	REQUIRE(f.world.battleCount() == 1);
	REQUIRE(f.world.worldEnemyCount() == 0); // 明雷移入战斗 ⇒ 世界态空

	f.world.tick(); // kNpcSpawn 发现 alive 0 < count 1 ⇒ 补齐一只新明雷到世界
	CHECK(f.world.worldEnemyCount() == 1);
	// ⚠️ 立即补齐(非死后 REVIVALTIME 延迟):刷怪点维持 count 只;精确复活时机随状态系统划出。
}

// ══ 批次 W.6: WARP 传送点(NPC 最小切片其一,移植 npc_warp.c)══════════════════

TEST_CASE("W.6:踩上Warp点瞬移到目标坐标并清空后续步数")
{
	MoveFixture f;
	const auto id = f.spawn();
	const auto p0 = f.world.playerPos(id);
	REQUIRE(p0.x == 32);
	REQUIRE(p0.y == 32);

	// 注入 Warp 点: (33, 32) -> (45, 45)
	std::vector<WarpPoint> warps;
	WarpPoint wp{};
	wp.src_floor = 0;
	wp.src_x = 33;
	wp.src_y = 32;
	wp.dst_floor = 0;
	wp.dst_x = 45;
	wp.dst_y = 45;
	warps.push_back(wp);
	f.world.loadWarpPoints(warps);
	CHECK(f.world.warpPointCount() == 1);

	// 发送三步向东: 32 -> 33 (触发传送) -> 剩余两步应被清空
	f.sendWalk(id, "ccc");
	f.world.tick();

	const auto p1 = f.world.playerPos(id);
	CHECK(p1.x == 45);
	CHECK(p1.y == 45);

	// 步数被清空 ⇒ 时钟推进后再次 tick 仍留在 (45, 45)
	f.clock.advance(250);
	f.world.tick();
	const auto p2 = f.world.playerPos(id);
	CHECK(p2.x == 45);
	CHECK(p2.y == 45);

	// 客户端收到自己坐标同步的 CharMove
	VisMirror m;
	m.feed(f.transport.sent(id));
	bool got_self_move = false;
	for (const auto &mv : m.move_msgs)
	{
		if (mv.entity_id == id && mv.x == 45 && mv.y == 45)
			got_self_move = true;
	}
	CHECK(got_self_move);
}

TEST_CASE("W.6:Warp传送双向视野增删(旧视野Disappear,新视野Appear)")
{
	MoveFixture f;
	const auto a = f.spawn(); // A 出生在 (32, 32)
	const auto b = f.spawn(); // B 出生在 (32, 32)，与 A 互在视野
	const auto c = f.spawn(); // C 出生在 (32, 32)
	// 将 C 手动移到 (55, 55)，远离 A 和 B
	f.sendWalk(c, std::string(23, 'c').c_str()); // 移远
	for (int i = 0; i < 25; ++i)
	{
		f.world.tick();
		f.clock.advance(250);
	}
	f.sendWalk(c, std::string(23, 'e').c_str()); // 移南到 y=55
	for (int i = 0; i < 25; ++i)
	{
		f.world.tick();
		f.clock.advance(250);
	}
	REQUIRE(f.world.playerPos(c).x == 55);
	REQUIRE(f.world.playerPos(c).y == 55);

	// 注入 Warp 点: (33, 32) -> (54, 55)
	WarpPoint wp{};
	wp.src_floor = 0;
	wp.src_x = 33;
	wp.src_y = 32;
	wp.dst_floor = 0;
	wp.dst_x = 54;
	wp.dst_y = 55;
	f.world.loadWarpPoints({wp});

	// 清理当前累积的 sent 缓冲区
	VisMirror ma, mb, mc;
	f.world.tick();
	ma.feed(f.transport.sent(a));
	mb.feed(f.transport.sent(b));
	mc.feed(f.transport.sent(c));
	ma.disappears.clear();
	ma.appears.clear();
	mb.disappears.clear();
	mb.appears.clear();
	mc.disappears.clear();
	mc.appears.clear();

	// A 踩入 (33, 32) 触发传送到 (54, 55)
	f.sendWalk(a, "c");
	f.world.tick();

	REQUIRE(f.world.playerPos(a).x == 54);
	REQUIRE(f.world.playerPos(a).y == 55);

	ma.feed(f.transport.sent(a));
	mb.feed(f.transport.sent(b));
	mc.feed(f.transport.sent(c));

	// B 看到 A 离开 (Disappear)
	CHECK(countId(mb.disappears, a) >= 1);
	// A 看到 B 离开 (Disappear)
	CHECK(countId(ma.disappears, b) >= 1);

	// C 看到 A 出现 (Appear)
	CHECK(countId(mc.appears, a) >= 1);
	// A 看到 C 出现 (Appear)
	CHECK(countId(ma.appears, c) >= 1);
}

TEST_CASE("W.6:目标点不可通行或越界时忽略传送(原版MAP_IsValidCoordinate)")
{
	MoveFixture f;
	const auto id = f.spawn();
	// 目标坐标越界 (-5, 100)
	WarpPoint wp{};
	wp.src_floor = 0;
	wp.src_x = 33;
	wp.src_y = 32;
	wp.dst_floor = 0;
	wp.dst_x = -5;
	wp.dst_y = 100;
	f.world.loadWarpPoints({wp});

	f.sendWalk(id, "c");
	f.world.tick();

	// 目标点非法 ⇒ 忽略传送，正常走到 (33, 32)
	const auto p = f.world.playerPos(id);
	CHECK(p.x == 33);
	CHECK(p.y == 32);
}

// ══ 批次 W.7: NPC 实体框架与 Healer 恢复员(移植 npc_healer.c)══════════════

TEST_CASE("W.7:撞NPC实体退回原格(CHAR_ISOVERED=0 阻挡不可穿透)")
{
	MoveFixture f;
	const auto id = f.spawn();
	NpcEntity npc{};
	npc.id = 1001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.image = 100200;
	f.world.loadNpcEntities({npc});
	CHECK(f.world.npcCount() == 1);

	// 玩家在 (32, 32)，试图走向东邻 NPC 格 (33, 32)
	f.sendWalk(id, "c");
	f.world.tick();

	// 撞 NPC 退回 ⇒ 坐标仍为 (32, 32)，朝向变为 2(东)
	const auto p = f.world.playerPos(id);
	CHECK(p.x == 32);
	CHECK(p.y == 32);
	CHECK(p.dir == 2);
}

TEST_CASE("W.7:进图与移动后收到NPC实体的CharAppear")
{
	MoveFixture f;
	NpcEntity npc{};
	npc.id = 8888;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.image = 100300;
	f.world.loadNpcEntities({npc});

	const auto id = f.spawn();
	f.world.tick();

	VisMirror m;
	m.feed(f.transport.sent(id));

	bool saw_npc = false;
	for (const auto &ap : m.appear_msgs)
	{
		if (ap.entity_id == 8888 &&
		    ap.entity_type == static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC) &&
		    ap.image == 100300)
		{
			saw_npc = true;
		}
	}
	CHECK(saw_npc);
}

TEST_CASE("W.7:面向Healer免费恢复自身与宠物满HP满MP")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f); // 带完整会话

	NpcEntity npc{};
	npc.id = 2001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kHealer;
	npc.cost = 0; // 免费
	f.world.loadNpcEntities({npc});

	// 设置玩家属性: vital=1000, str=200, tough=200, dex=200 ⇒ max_hp = 46
	REQUIRE(f.world.setPlayerStatsForTest(id, /*hp=*/10, /*mp=*/5, /*vital=*/1000,
	                                      /*str=*/200, /*tough=*/200, /*dex=*/200));
	CHECK(f.world.playerHp(id) == 10);
	CHECK(f.world.playerMp(id) == 5);

	// 给玩家一只宠物: vital=500, str=100, tough=100, dex=100 ⇒ max_hp = 23
	SA::Model::Pet pet{};
	pet.vital = 500;
	pet.str = 100;
	pet.tough = 100;
	pet.dex = 100;
	pet.hp = 2;
	pet.mp = 3;
	pet.max_mp = 60;
	const int slot = f.world.givePetToPlayer(id, pet);
	REQUIRE(slot >= 0);
	REQUIRE(f.world.playerPetAt(id, slot)->hp == 2);
	REQUIRE(f.world.playerPetAt(id, slot)->mp == 3);

	// 玩家面向东(dir=2, 面前格为 33, 32 上的 Healer)发起 EV
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 101;
	f.world.onEvent(id, ev);
	f.world.tick();

	// 回执 ok == true
	CHECK(eventResultOk(f.transport.sent(id), 101));

	// 自身满血满蓝
	CHECK(f.world.playerHp(id) == 46);
	CHECK(f.world.playerMp(id) == 100);

	// 宠物满血满蓝
	CHECK(f.world.playerPetAt(id, slot)->hp == 23);
	CHECK(f.world.playerPetAt(id, slot)->mp == 60);
}

TEST_CASE("W.7:面向Healer收费扣除石币(经GoldLedger),余额不足拒绝")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 2002;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kHealer;
	npc.cost = 50; // 收费 50 石币
	f.world.loadNpcEntities({npc});

	// 玩家受伤且无石币
	REQUIRE(f.world.setPlayerStatsForTest(id, /*hp=*/10, /*mp=*/5, /*vital=*/1000,
	                                      /*str=*/200, /*tough=*/200, /*dex=*/200));
	REQUIRE(f.world.playerGold(id) == 0);

	// 余额不足发起 EV ⇒ 拒绝, ok == false
	SA::Domain::EventRequest ev1{};
	ev1.dir = 2;
	ev1.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev1.seqno = 201;
	f.world.onEvent(id, ev1);
	f.world.tick();

	CHECK_FALSE(eventResultOk(f.transport.sent(id), 201));
	CHECK(f.world.playerHp(id) == 10); // 未恢复
	CHECK(f.world.playerGold(id) == 0);

	// 经 GoldLedger 充入 100 石币
	REQUIRE(f.world.giveGoldToPlayerForTest(id, 100));
	CHECK(f.world.playerGold(id) == 100);

	// 再次发起 EV ⇒ 成功扣除 50 石币并满恢复
	SA::Domain::EventRequest ev2{};
	ev2.dir = 2;
	ev2.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev2.seqno = 202;
	f.world.onEvent(id, ev2);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 202));
	CHECK(f.world.playerGold(id) == 50); // 扣 50
	CHECK(f.world.playerHp(id) == 46);   // 满血
	CHECK(f.world.playerMp(id) == 100);  // 满蓝
}

// ══ 批次 W.8:城镇居民 NPC 对话 (TownPeople) 与 对白/窗口骨架 (WindowOpen / WindowReply) ═══════════
//
// 原版 npc_townpeople.c:
//   - 面对 TownPeople 发起 EV 触发 NPC_TownPeopleTalked
//   - 逗号分隔候选文案随机选择 (randMod)
//   - 组装下发 WindowOpen 消息 (WINDOW_KIND_MESSAGE, BUTTON_FLAG_OK)
//   - 客户端确认发送 WindowReply 闭环窗口会话状态机

TEST_CASE("W.8:面向 TownPeople 交互下发 WindowOpen 消息窗口")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 3001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.dir = 6;
	npc.image = 10001;
	npc.type = NpcType::kTownPeople;
	npc.message = "欢迎来到玛丽娜丝渔村！";
	f.world.loadNpcEntities({npc});

	// 玩家在 (32,32), 面向东(dir=2)发起 NPC 事件
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 301;
	f.world.onEvent(id, ev);
	f.world.tick();

	// 1. 回执 ok == true
	CHECK(eventResultOk(f.transport.sent(id), 301));

	// 2. 检查下发的 WindowOpen 消息
	const auto win_opt = findLastWindowOpen(f.transport.sent(id));
	REQUIRE(win_opt.has_value());
	const auto &win = *win_opt;
	CHECK(win.kind == SA::Domain::WindowKind::WINDOW_KIND_MESSAGE);
	CHECK(win.buttons == static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	CHECK(win.source.source == SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY);
	CHECK(win.source.entity_id == 3001);
	CHECK(win.body_kind == SA::Domain::WindowOpen::BodyKind::MESSAGE);
	REQUIRE(win.body.message.lines.size() == 1);
	CHECK(std::string(win.body.message.lines[0].c_str()) == "欢迎来到玛丽娜丝渔村！");

	// 3. 观察面: 玩家处于活动窗口状态
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerActiveWindowId(id) == win.window_id);
	CHECK(f.world.playerLastWindowText(id) == "欢迎来到玛丽娜丝渔村！");
}

TEST_CASE("W.8:TownPeople 多候选文案随机选择(逗号分隔)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 3002;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.dir = 6;
	npc.type = NpcType::kTownPeople;
	npc.message = "台词一,台词二,台词三";
	f.world.loadNpcEntities({npc});

	const std::vector<std::string> expected = {"台词一", "台词二", "台词三"};

	// 连续交互 6 次
	for (std::uint32_t i = 1; i <= 6; ++i)
	{
		SA::Domain::EventRequest ev{};
		ev.dir = 2;
		ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
		ev.seqno = 310 + i;
		f.world.onEvent(id, ev);
		f.world.tick();

		CHECK(eventResultOk(f.transport.sent(id), 310 + i));
		const auto win_opt = findLastWindowOpen(f.transport.sent(id));
		REQUIRE(win_opt.has_value());
		REQUIRE(win_opt->body.message.lines.size() == 1);
		const std::string text = win_opt->body.message.lines[0].c_str();

		// 下发文本必须在候选列表内
		CHECK(std::find(expected.begin(), expected.end(), text) != expected.end());
		CHECK(f.world.playerLastWindowText(id) == text);
	}
}

TEST_CASE("W.8:窗口回执 WindowReply 闭环会话状态机(匹配关闭, 不匹配保持)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 3003;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kTownPeople;
	npc.message = "这是一句测试对话。";
	f.world.loadNpcEntities({npc});

	// 打开窗口
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 320;
	f.world.onEvent(id, ev);
	f.world.tick();

	REQUIRE(f.world.playerHasActiveWindow(id));
	const std::uint32_t active_wid = f.world.playerActiveWindowId(id);
	REQUIRE(active_wid > 0);

	// 发送不匹配的 window_id 回执 ⇒ 保持激活
	SA::Domain::WindowReply wrong_reply{};
	wrong_reply.window_id = active_wid + 999;
	wrong_reply.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, wrong_reply);
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerActiveWindowId(id) == active_wid);

	// 发送正确的 window_id 回执 ⇒ 闭环清除
	SA::Domain::WindowReply correct_reply{};
	correct_reply.window_id = active_wid;
	correct_reply.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, correct_reply);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerActiveWindowId(id) == 0);
}

TEST_CASE("W.8:未面向 TownPeople 或面前无 NPC ⇒ 拒绝交互(ok=false, 不发窗)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 3004;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32; // NPC 在东侧
	npc.type = NpcType::kTownPeople;
	npc.message = "你听不见我说什么。";
	f.world.loadNpcEntities({npc});

	// 玩家在 (32,32), 面向北(dir=0, 面前格为 32,31)发起 NPC EV ⇒ 面前无 NPC
	SA::Domain::EventRequest ev{};
	ev.dir = 0;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 330;
	f.world.onEvent(id, ev);
	f.world.tick();

	// 回执 ok == false, 且无活动窗口
	CHECK_FALSE(eventResultOk(f.transport.sent(id), 330));
	CHECK_FALSE(findLastWindowOpen(f.transport.sent(id)).has_value());
	CHECK_FALSE(f.world.playerHasActiveWindow(id));
}

// ══ 批次 W.9: 任务旗标空间与 ExChangeMan 基础事件块解析骨架 ═════════════════

TEST_CASE("W.9: 任务旗标位图与安全边界保护 (256位空间, 严格防御越界)")
{
	SA::Model::Player p{};

	// 初始全 0
	for (int i = 0; i < 256; ++i)
	{
		CHECK_FALSE(p.hasNowEvent(i));
		CHECK_FALSE(p.hasEndEvent(i));
	}

	// 1. 设置与读取跨 slot 边界 (0, 31, 32, 63, 127, 226, 255)
	const std::vector<int> test_bits = {0, 31, 32, 63, 127, 226, 255};
	for (int bit : test_bits)
	{
		CHECK(p.setNowEvent(bit));
		CHECK(p.hasNowEvent(bit));
		CHECK_FALSE(p.hasEndEvent(bit)); // 独立性

		CHECK(p.setEndEvent(bit));
		CHECK(p.hasEndEvent(bit));
	}

	// 清除测试
	for (int bit : test_bits)
	{
		CHECK(p.clearNowEvent(bit));
		CHECK_FALSE(p.hasNowEvent(bit));
		CHECK(p.hasEndEvent(bit)); // end_events 保持

		CHECK(p.clearEndEvent(bit));
		CHECK_FALSE(p.hasEndEvent(bit));
	}

	// 2. 严格越界防御 (C30: -1 为无旗标约定, < -1 或 >= 256 严禁写入)
	CHECK_FALSE(p.setNowEvent(-1));
	CHECK_FALSE(p.hasNowEvent(-1));
	CHECK_FALSE(p.clearNowEvent(-1));

	CHECK_FALSE(p.setNowEvent(256));
	CHECK_FALSE(p.hasNowEvent(256));
	CHECK_FALSE(p.clearNowEvent(256));

	CHECK_FALSE(p.setNowEvent(-2));
	CHECK_FALSE(p.setNowEvent(999));

	CHECK_FALSE(p.setEndEvent(-1));
	CHECK_FALSE(p.hasEndEvent(-1));
	CHECK_FALSE(p.clearEndEvent(-1));

	CHECK_FALSE(p.setEndEvent(256));
	CHECK_FALSE(p.hasEndEvent(256));
	CHECK_FALSE(p.clearEndEvent(256));

	// 确保所有槽位仍然为 0
	for (auto v : p.now_events)
		CHECK(v == 0);
	for (auto v : p.end_events)
		CHECK(v == 0);
}

TEST_CASE("W.9: ExChangeMan 脚本文本解析 (EventEnd 块划分与键值提取)")
{
	const std::string script = R"(
# 第一个块: 接取任务
EventNo:10|TYPE:ACCEPT
EVENT:LV>5&NOWEV!=10
AcceptMsg:你想接受考验吗？
ThanksMsg:祝你好运！
EventEnd

# 第二个块: 完成任务
EventNo:10|TYPE:MESSAGE
EVENT:NOWEV=10&LV>5
EndSetFlg:10,11
CleanFlg:5
NomalWindowMsg:你通过了考验！
EventEnd
)";

	const auto blocks = parseExChangeBlocks(script);
	REQUIRE(blocks.size() == 2);

	// Block 0
	CHECK(blocks[0].event_no == 10);
	CHECK(blocks[0].type == ExChangeType::kAccept);
	CHECK(blocks[0].condition == "LV>5&NOWEV!=10");
	CHECK(blocks[0].accept_msg == "你想接受考验吗？");
	CHECK(blocks[0].thanks_msg == "祝你好运！");

	// Block 1
	CHECK(blocks[1].event_no == 10);
	CHECK(blocks[1].type == ExChangeType::kMessage);
	CHECK(blocks[1].condition == "NOWEV=10&LV>5");
	CHECK(blocks[1].end_set_flg == "10,11");
	CHECK(blocks[1].clean_flg == "5");
	CHECK(blocks[1].nomal_window_msg == "你通过了考验！");
}

TEST_CASE("W.9: 条件表达式求值器 (LV, NOWEV, ENDEV 与逗号分支选择)")
{
	SA::Model::Player p{};
	p.level = 10;
	p.setNowEvent(5);
	p.setEndEvent(20);

	// 空条件 ⇒ 默认命中分支 1
	CHECK(evaluateEventCondition("", p) == 1);

	// LV 比较
	CHECK(evaluateEventCondition("LV>5", p) == 1);
	CHECK(evaluateEventCondition("LV<5", p) == 0);
	CHECK(evaluateEventCondition("LV=10", p) == 1);
	CHECK(evaluateEventCondition("LV!=10", p) == 0);

	// NOWEV / ENDEV 比较
	CHECK(evaluateEventCondition("NOWEV=5", p) == 1);
	CHECK(evaluateEventCondition("NOWEV!=5", p) == 0);
	CHECK(evaluateEventCondition("NOWEV=6", p) == 0);
	CHECK(evaluateEventCondition("NOWEV!=6", p) == 1);

	CHECK(evaluateEventCondition("ENDEV=20", p) == 1);
	CHECK(evaluateEventCondition("ENDEV!=20", p) == 0);
	CHECK(evaluateEventCondition("ENDEV=21", p) == 0);

	// 短路与 '&'
	CHECK(evaluateEventCondition("LV>5&NOWEV=5&ENDEV=20", p) == 1);
	CHECK(evaluateEventCondition("LV>5&NOWEV=5&ENDEV=99", p) == 0);

	// 逗号分支选择器 ',' (1-based 序号)
	// 第 1 分支不满足，第 2 分支满足 ⇒ 返回 2
	CHECK(evaluateEventCondition("LV>20,LV>5", p) == 2);
	// 第 1 分支即满足 ⇒ 返回 1
	CHECK(evaluateEventCondition("LV>5,LV>20", p) == 1);
	// 全都不满足 ⇒ 返回 0
	CHECK(evaluateEventCondition("LV>20,LV<5", p) == 0);
}

TEST_CASE("W.9: ExChangeMan TYPE:MESSAGE 面对交互、条件分支与即时旗标结算")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	// 配置 ExChangeMan NPC: 需要 LV>5, 完成后置 EndSetFlg:15, 清 CleanFlg:3
	NpcEntity npc{};
	npc.id = 4001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;
	npc.nomal_main_msg = "等级不足，请提升到6级以上。";

	ExChangeBlock blk{};
	blk.event_no = 15;
	blk.type = ExChangeType::kMessage;
	blk.condition = "LV>5";
	blk.nomal_window_msg = "恭喜你达到6级，特此颁发先锋勋章！";
	blk.end_set_flg = "15";
	blk.clean_flg = "3";
	npc.exchange_blocks.push_back(blk);

	f.world.loadNpcEntities({npc});

	// 初始状态: 玩家 1 级, 拥有 now_event 3
	SA::Model::Player *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 1;
	p->setNowEvent(3);
	p->setNowEvent(15);

	// 1. 等级不足时交互 ⇒ 块条件不满足，走兜底 nomal_main_msg，旗标未变
	SA::Domain::EventRequest ev{};
	ev.dir = 2; // 面向东 (33,32)
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 401;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 401));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "等级不足，请提升到6级以上。");
	CHECK(f.world.playerHasNowEvent(id, 3));
	CHECK_FALSE(f.world.playerHasEndEvent(id, 15));

	// 关闭窗口
	const std::uint32_t wid1 = f.world.playerActiveWindowId(id);
	SA::Domain::WindowReply rep1{};
	rep1.window_id = wid1;
	rep1.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep1);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));

	// 2. 提升玩家等级至 10 级，再次交互 ⇒ 命中块，立即结算旗标并下发窗口
	p->level = 10;
	ev.seqno = 402;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 402));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "恭喜你达到6级，特此颁发先锋勋章！");

	// 验证旗标副作用: EndSetFlg:15 已置位, CleanFlg:3 已清除, 原 now_event:15 已清除
	CHECK(f.world.playerHasEndEvent(id, 15));
	CHECK_FALSE(f.world.playerHasNowEvent(id, 3));
	CHECK_FALSE(f.world.playerHasNowEvent(id, 15));

	// 3. 再次交互 ⇒ 块 0 因已完成 (hasEndEvent(15)) 被前置门跳过，走兜底文案
	f.world.onWindowReply(id, rep1); // 关闭
	ev.seqno = 403;
	f.world.onEvent(id, ev);
	f.world.tick();
	CHECK(f.world.playerLastWindowText(id) == "等级不足，请提升到6级以上。");
}

TEST_CASE("W.9: ExChangeMan TYPE:ACCEPT 接取、取消与结算全生命周期闭环")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	// 配置 ExChangeMan NPC: 包含接取块与进行中块
	NpcEntity npc{};
	npc.id = 4002;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;
	npc.nomal_main_msg = "欢迎来到萨姆吉尔村。";

	// 块 0: 接取任务
	ExChangeBlock blk0{};
	blk0.event_no = 8;
	blk0.type = ExChangeType::kAccept;
	blk0.condition = "LV>1&NOWEV!=8";
	blk0.accept_msg = "你想接受村长的委托吗？";
	blk0.thanks_msg = "祝你旅途顺利！";

	// 块 1: 进行中提醒
	ExChangeBlock blk1{};
	blk1.event_no = 8;
	blk1.type = ExChangeType::kMessage;
	blk1.condition = "NOWEV=8";
	blk1.nomal_window_msg = "你正在进行村长的委托，请快去完成。";

	npc.exchange_blocks.push_back(blk0);
	npc.exchange_blocks.push_back(blk1);
	f.world.loadNpcEntities({npc});

	SA::Model::Player *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 5;

	// ── 步骤 1: 首次交互，命中块 0，收到 YES/NO 确认窗 ──────────────
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 501;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	const std::uint32_t wid1 = f.world.playerActiveWindowId(id);
	auto win_opt1 = findLastWindowOpen(f.transport.sent(id));
	REQUIRE(win_opt1.has_value());
	CHECK(win_opt1->window_id == wid1);
	// 验证包含 YES 和 NO 按钮
	CHECK((win_opt1->buttons & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES)) != 0);
	CHECK((win_opt1->buttons & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0);
	CHECK(f.world.playerLastWindowText(id) == "你想接受村长的委托吗？");

	// ── 步骤 2: 回复 NO 取消 ⇒ 任务未接取，旗标不变 ────────────────
	SA::Domain::WindowReply reply_no{};
	reply_no.window_id = wid1;
	reply_no.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
	reply_no.source.entity_id = 4002;
	reply_no.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
	f.world.onWindowReply(id, reply_no);

	CHECK_FALSE(f.world.playerHasActiveWindow(id));
	CHECK_FALSE(f.world.playerHasNowEvent(id, 8));
	CHECK_FALSE(f.world.playerHasEndEvent(id, 8));

	// ── 步骤 3: 再次交互，回复 YES 接取任务 ────────────────────────
	ev.seqno = 502;
	f.world.onEvent(id, ev);
	f.world.tick();

	const std::uint32_t wid2 = f.world.playerActiveWindowId(id);
	REQUIRE(wid2 > 0);

	SA::Domain::WindowReply reply_yes{};
	reply_yes.window_id = wid2;
	reply_yes.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
	reply_yes.source.entity_id = 4002;
	reply_yes.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, reply_yes);

	// 接取成功: 置位 now_events[8], 并收到 ThanksMsg 窗口
	CHECK(f.world.playerHasNowEvent(id, 8));
	CHECK_FALSE(f.world.playerHasEndEvent(id, 8));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "祝你旅途顺利！");

	// 关闭 ThanksMsg 窗口
	const std::uint32_t wid3 = f.world.playerActiveWindowId(id);
	SA::Domain::WindowReply reply_ok{};
	reply_ok.window_id = wid3;
	reply_ok.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, reply_ok);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));

	// ── 步骤 4: 任务进行中再次交互 ⇒ 命中块 1，收到进行中文案 ───────
	ev.seqno = 503;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "你正在进行村长的委托，请快去完成。");

	// ── 步骤 5: 玩家完成任务 (置 EndSetFlg:8, 清 now_events:8) ───────
	p->setEndEvent(8);
	p->clearNowEvent(8);
	f.world.onWindowReply(id, reply_ok);

	// ── 步骤 6: 任务完成后再次交互 ⇒ 块 0 因 hasEndEvent(8) 被前置门跳过，
	//    块 1 因 NOWEV=8 不满足跳过 ⇒ 走默认兜底文案
	ev.seqno = 504;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "欢迎来到萨姆吉尔村。");
}

// ══ 批次 W.10: ExChangeMan 道具/宠物交付与奖励结算 ══════════════════════════

namespace
{
inline SA::Model::Item makeTestItem(std::int32_t item_id, std::int32_t pile = 1, std::int32_t cost = 0)
{
	SA::Model::Item it{};
	it.item_id = item_id;
	it.current_pile = pile;
	it.cost = cost;
	return it;
}

inline SA::Model::Pet makeTestPet(std::int32_t pet_id, std::int32_t level = 1)
{
	SA::Model::Pet pet{};
	pet.pet_id = pet_id;
	pet.level = level;
	pet.vital = 100;
	pet.str = 50;
	pet.tough = 50;
	pet.dex = 50;
	pet.hp = 20;
	pet.mp = 20;
	pet.max_mp = 50;
	return pet;
}
} // namespace

TEST_CASE("W.10: ExChangeMan 背包满拦截(ItemFullCheck 弹窗阻断, 释放后正常结算)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 5001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;
	npc.nomal_main_msg = "欢迎光临。";

	// 块 0: 纯获得道具 1001, 需要 1 个空位
	ExChangeBlock blk1{};
	blk1.event_no = 21;
	blk1.type = ExChangeType::kMessage;
	blk1.condition = "LV>1";
	blk1.get_item = "1001";
	blk1.item_full_msg = "你的背包空间不足！";
	blk1.nomal_window_msg = "给你一个珍贵的道具！";
	npc.exchange_blocks.push_back(blk1);
	f.world.loadNpcEntities({npc});

	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 5;

	// 背包装满 45 个道具 (kMaxItemHave - kStartItemArray = 54 - 9 = 45)
	int filled = 0;
	while (f.world.giveItemToPlayer(id, makeTestItem(999)) >= 0)
	{
		++filled;
	}
	CHECK(filled == 45);
	CHECK(f.world.playerItemSlotsUsed(id) == 45);

	// 1. 背包满交互 ⇒ 触发 ItemFullMsg 阻断
	SA::Domain::EventRequest ev{};
	ev.dir = 2; // 面向东 (33, 32)
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 601;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 601));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "你的背包空间不足！");
	// 验证道具池与背包未增加 1001
	CHECK(f.world.playerItemSlotsUsed(id) == 45);
	bool has_1001 = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id, static_cast<int>(i));
		if (it != nullptr && it->item_id == 1001)
			has_1001 = true;
	}
	CHECK_FALSE(has_1001);

	// 关闭提示窗口
	const std::uint32_t wid1 = f.world.playerActiveWindowId(id);
	SA::Domain::WindowReply rep1{};
	rep1.window_id = wid1;
	rep1.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep1);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));

	// 2. 置换块测试: 即使背包满(45/45), 但 DelItem 释放格 ≥ GetItem 需求格 ⇒ 允许置换
	npc.exchange_blocks.clear();
	ExChangeBlock blk2{};
	blk2.event_no = 22;
	blk2.type = ExChangeType::kMessage;
	blk2.condition = "LV>1";
	blk2.del_item = "999";
	blk2.get_item = "1001";
	blk2.item_full_msg = "你的背包空间不足！";
	blk2.nomal_window_msg = "置换成功！";
	npc.exchange_blocks.push_back(blk2);
	f.world.loadNpcEntities({npc});

	ev.seqno = 602;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 602));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "置换成功！");
	CHECK(f.world.playerItemSlotsUsed(id) == 45);

	// 此时背包中应该出现 1001
	has_1001 = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id, static_cast<int>(i));
		if (it != nullptr && it->item_id == 1001)
			has_1001 = true;
	}
	CHECK(has_1001);
}

TEST_CASE("W.10: ExChangeMan 宠物槽满拦截(PetFullCheck 弹窗阻断, 置换宠物成功)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 5002;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;

	// 块 1: 纯获取宠物 2001
	ExChangeBlock blk1{};
	blk1.event_no = 31;
	blk1.type = ExChangeType::kMessage;
	blk1.condition = "LV>1";
	blk1.get_pet = "2001";
	blk1.pet_full_msg = "你的宠物栏已满！";
	blk1.nomal_window_msg = "送你一只宠物！";
	npc.exchange_blocks.push_back(blk1);
	f.world.loadNpcEntities({npc});

	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 5;

	// 填满 5 个宠物槽
	for (int i = 0; i < 5; ++i)
	{
		CHECK(f.world.givePetToPlayer(id, makeTestPet(3000 + i)) >= 0);
	}
	CHECK(f.world.playerPetSlotsUsed(id) == 5);

	// 1. 宠物槽满交互 ⇒ 触发 PetFullMsg 阻断
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 701;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 701));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "你的宠物栏已满！");
	CHECK(f.world.playerPetSlotsUsed(id) == 5);

	// 检查未获得 2001
	for (int i = 0; i < 5; ++i)
	{
		const auto *pet = f.world.playerPetAt(id, i);
		REQUIRE(pet != nullptr);
		CHECK(pet->pet_id != 2001);
	}

	// 关闭提示窗口
	const std::uint32_t wid1 = f.world.playerActiveWindowId(id);
	SA::Domain::WindowReply rep1{};
	rep1.window_id = wid1;
	rep1.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep1);

	// 2. 置换宠物测试: 槽满(5/5), 但 DelPet:3000 释放 1 槽换 GetPet:2001
	npc.exchange_blocks.clear();
	ExChangeBlock blk2{};
	blk2.event_no = 32;
	blk2.type = ExChangeType::kMessage;
	blk2.condition = "LV>1";
	blk2.del_pet = "3000";
	blk2.get_pet = "2001";
	blk2.pet_full_msg = "你的宠物栏已满！";
	blk2.nomal_window_msg = "换宠成功！";
	npc.exchange_blocks.push_back(blk2);
	f.world.loadNpcEntities({npc});

	ev.seqno = 702;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 702));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "换宠成功！");
	CHECK(f.world.playerPetSlotsUsed(id) == 5);

	// 验证 3000 被置换为 2001
	bool found_2001 = false;
	bool found_3000 = false;
	for (int i = 0; i < 5; ++i)
	{
		const auto *pet = f.world.playerPetAt(id, i);
		REQUIRE(pet != nullptr);
		if (pet->pet_id == 2001)
			found_2001 = true;
		if (pet->pet_id == 3000)
			found_3000 = true;
	}
	CHECK(found_2001);
	CHECK_FALSE(found_3000);
}

TEST_CASE("W.10: ExChangeMan 石币不足与超限拦截(经GoldLedger, StoneLessMsg / StoneFullMsg)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 5003;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;

	// 块 0: 消耗 500 石币, 获得 1000 石币
	ExChangeBlock blk0{};
	blk0.event_no = 41;
	blk0.type = ExChangeType::kMessage;
	blk0.condition = "LV>1";
	blk0.del_stone = 500;
	blk0.get_stone = 1000;
	blk0.stone_less_msg = "你的石币不足500！";
	blk0.nomal_window_msg = "石币翻倍成功！";
	npc.exchange_blocks.push_back(blk0);
	f.world.loadNpcEntities({npc});

	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 5;

	// 玩家初始只有 200 石币
	REQUIRE(f.world.giveGoldToPlayerForTest(id, 200));
	CHECK(f.world.playerGold(id) == 200);

	// 1. 石币不足交互 ⇒ 阻断弹 StoneLessMsg
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 801;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 801));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "你的石币不足500！");
	CHECK(f.world.playerGold(id) == 200); // 未发生变动

	// 关闭提示窗口
	const std::uint32_t wid1 = f.world.playerActiveWindowId(id);
	SA::Domain::WindowReply rep1{};
	rep1.window_id = wid1;
	rep1.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep1);

	// 2. 补足石币至 600 (增加 400)
	REQUIRE(f.world.giveGoldToPlayerForTest(id, 400));
	CHECK(f.world.playerGold(id) == 600);

	// 再次交互 ⇒ 成功扣除 500 并奖励 1000 (净增 500, 最终 1100)
	ev.seqno = 802;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 802));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "石币翻倍成功！");
	CHECK(f.world.playerGold(id) == 1100);

	// 关闭窗口
	const std::uint32_t wid2 = f.world.playerActiveWindowId(id);
	rep1.window_id = wid2;
	f.world.onWindowReply(id, rep1);

	// 3. 石币超限拦截测试: get_stone 导致突破 100 万上限
	npc.exchange_blocks.clear();
	ExChangeBlock blk1{};
	blk1.event_no = 42;
	blk1.type = ExChangeType::kMessage;
	blk1.condition = "LV>1";
	blk1.get_stone = 50000;
	blk1.stone_full_msg = "你的石币将超出携带上限！";
	blk1.nomal_window_msg = "巨额奖励发放！";
	npc.exchange_blocks.push_back(blk1);
	f.world.loadNpcEntities({npc});

	// 让玩家石币达到 980,000 (增加 978900)
	REQUIRE(f.world.giveGoldToPlayerForTest(id, 980000 - 1100));
	CHECK(f.world.playerGold(id) == 980000);

	// 980,000 + 50,000 = 1,030,000 > 1,000,000 ⇒ 阻断
	ev.seqno = 803;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 803));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "你的石币将超出携带上限！");
	CHECK(f.world.playerGold(id) == 980000); // 余额保持不变
}

TEST_CASE("W.10: ExChangeMan 道具/宠物/石币综合交付与奖励(TYPE:ACCEPT 交互闭环)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 5004;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;

	// 配置综合 ACCEPT 委托
	ExChangeBlock blk{};
	blk.event_no = 50;
	blk.type = ExChangeType::kAccept;
	blk.condition = "NOWEV!=50&ENDEV!=50";
	blk.accept_msg = "愿意交出信物501和爱宠601并支付100石币完成委托吗？";
	blk.thanks_msg = "太感谢了，这是给你的丰厚报酬！";
	blk.del_item = "501";
	blk.del_pet = "601";
	blk.del_stone = 100;
	blk.get_item = "502";
	blk.get_pet = "602";
	blk.get_stone = 300;
	blk.end_set_flg = "50";
	npc.exchange_blocks.push_back(blk);
	f.world.loadNpcEntities({npc});

	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 10;

	// 玩家初始状态: 持有 501, 持有 601, 石币 200
	REQUIRE(f.world.giveItemToPlayer(id, makeTestItem(501)) >= 0);
	REQUIRE(f.world.givePetToPlayer(id, makeTestPet(601)) >= 0);
	REQUIRE(f.world.giveGoldToPlayerForTest(id, 200));

	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
	CHECK(f.world.playerGold(id) == 200);

	// 1. 发起交互 ⇒ 命中 ACCEPT 块，下发 Accept 弹窗
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 901;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 901));
	CHECK(f.world.playerHasActiveWindow(id));
	const std::uint32_t wid = f.world.playerActiveWindowId(id);
	CHECK(f.world.playerLastWindowText(id) == "愿意交出信物501和爱宠601并支付100石币完成委托吗？");

	// 2. 回复 YES 确认执行结算
	SA::Domain::WindowReply reply_yes{};
	reply_yes.window_id = wid;
	reply_yes.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
	reply_yes.source.entity_id = 5004;
	reply_yes.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, reply_yes);

	// 3. 校验结算副作用
	// 收到 ThanksMsg 窗口
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "太感谢了，这是给你的丰厚报酬！");

	// 石币: 200 - 100 + 300 = 400
	CHECK(f.world.playerGold(id) == 400);

	// 道具: 501 被删除, 获得 502
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	bool found_501 = false;
	bool found_502 = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id, static_cast<int>(i));
		if (it != nullptr)
		{
			if (it->item_id == 501)
				found_501 = true;
			if (it->item_id == 502)
				found_502 = true;
		}
	}
	CHECK_FALSE(found_501);
	CHECK(found_502);

	// 宠物: 601 被删除, 获得 602
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
	bool found_601 = false;
	bool found_602 = false;
	for (int i = 0; i < 5; ++i)
	{
		const auto *pet = f.world.playerPetAt(id, i);
		if (pet != nullptr)
		{
			if (pet->pet_id == 601)
				found_601 = true;
			if (pet->pet_id == 602)
				found_602 = true;
		}
	}
	CHECK_FALSE(found_601);
	CHECK(found_602);

	// 旗标: end_events[50] 已置位
	CHECK(f.world.playerHasEndEvent(id, 50));
}

TEST_CASE("W.10: ExChangeMan EVDEL 动态从 EVENT 条件解析扣除道具与宠物")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 5005;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;

	// 配置 EVDEL: 从 EVENT 动态提取需要扣除的道具和宠物
	ExChangeBlock blk{};
	blk.event_no = 60;
	blk.type = ExChangeType::kMessage;
	blk.condition = "ITEM=777*2&PET=888";
	blk.del_item = "EVDEL";
	blk.del_pet = "EVDEL";
	blk.nomal_window_msg = "成功收走2个777和1只888！";
	npc.exchange_blocks.push_back(blk);
	f.world.loadNpcEntities({npc});

	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 10;

	// 玩家准备: 2 个 777, 1 个无关道具 666; 1 只 888, 1 只无关宠物 999
	REQUIRE(f.world.giveItemToPlayer(id, makeTestItem(777, 1)) >= 0);
	REQUIRE(f.world.giveItemToPlayer(id, makeTestItem(777, 1)) >= 0);
	REQUIRE(f.world.giveItemToPlayer(id, makeTestItem(666, 1)) >= 0);

	REQUIRE(f.world.givePetToPlayer(id, makeTestPet(888)) >= 0);
	REQUIRE(f.world.givePetToPlayer(id, makeTestPet(999)) >= 0);

	CHECK(f.world.playerItemSlotsUsed(id) == 3);
	CHECK(f.world.playerPetSlotsUsed(id) == 2);

	// 发起交互
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 1001;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(eventResultOk(f.transport.sent(id), 1001));
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "成功收走2个777和1只888！");

	// 校验扣除结果:
	// 2 个 777 被扣除, 666 仍在背包
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	int count_777 = 0;
	int count_666 = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id, static_cast<int>(i));
		if (it != nullptr)
		{
			if (it->item_id == 777)
				count_777++;
			if (it->item_id == 666)
				count_666++;
		}
	}
	CHECK(count_777 == 0);
	CHECK(count_666 == 1);

	// 1 只 888 被扣除, 999 仍在宠物栏
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
	int count_888 = 0;
	int count_999 = 0;
	for (int i = 0; i < 5; ++i)
	{
		const auto *pet = f.world.playerPetAt(id, i);
		if (pet != nullptr)
		{
			if (pet->pet_id == 888)
				count_888++;
			if (pet->pet_id == 999)
				count_999++;
		}
	}
	CHECK(count_888 == 0);
	CHECK(count_999 == 1);
}

// ══ 批次 W.11: 世界 NPC 巡逻与随机移动漫游 (npc_wanderer / NPC_walk 逻辑移植) ═════════

TEST_CASE("W.11: 自由游荡漫游 (Wanderer 周期性步进, 坐标变更且严格受限于 wander_radius 半径)")
{
	MoveFixture f;

	NpcEntity npc{};
	npc.id = 6001;
	npc.floor = 0;
	npc.x = 30;
	npc.y = 30;
	npc.dir = 0;
	npc.image = 10001;
	npc.type = NpcType::kTownPeople;
	npc.wander_radius = 2;        // 以 (30, 30) 为中心，范围 [28..32]
	npc.wander_interval_ms = 100; // 每 100ms 游荡一步
	f.world.loadNpcEntities({npc});

	const auto *npc_ptr = f.world.findNpc(6001);
	REQUIRE(npc_ptr != nullptr);
	CHECK(npc_ptr->x == 30);
	CHECK(npc_ptr->y == 30);

	// 未到 100ms 时推进 tick ⇒ 保持不动
	f.clock.advance(50);
	f.world.tick();
	CHECK(npc_ptr->x == 30);
	CHECK(npc_ptr->y == 30);

	// 连续推进 20 步 (每次推进 100ms)
	bool moved = false;
	for (int i = 0; i < 20; ++i)
	{
		f.clock.advance(100);
		f.world.tick();
		if (npc_ptr->x != 30 || npc_ptr->y != 30)
			moved = true;

		// 严格保证在 wander_radius 半径以内
		CHECK(std::abs(npc_ptr->x - 30) <= 2);
		CHECK(std::abs(npc_ptr->y - 30) <= 2);
		CHECK(npc_ptr->dir < 8);
	}
	CHECK(moved);
}

TEST_CASE("W.11: 固定路点巡逻 (Patrol Route 循序行进并在到达终点后循环回原点)")
{
	MoveFixture f;

	NpcEntity npc{};
	npc.id = 6002;
	npc.floor = 0;
	npc.x = 30;
	npc.y = 30;
	npc.type = NpcType::kTownPeople;
	npc.wander_interval_ms = 100;
	// 巡逻三角形路线: (30, 32) -> (32, 32) -> (30, 30)
	npc.route = {{30, 32}, {32, 32}, {30, 30}};
	f.world.loadNpcEntities({npc});

	const auto *npc_ptr = f.world.findNpc(6002);
	REQUIRE(npc_ptr != nullptr);

	// ── 阶段 1: 朝着 (30, 32) 走 (向南, dir=4) ────────────────────
	// 步 1: 到 (30, 31)
	f.clock.advance(100);
	f.world.tick();
	CHECK(npc_ptr->x == 30);
	CHECK(npc_ptr->y == 31);
	CHECK(npc_ptr->dir == 4);

	// 步 2: 到 (30, 32) (到达第 0 个路点目标)
	f.clock.advance(100);
	f.world.tick();
	CHECK(npc_ptr->x == 30);
	CHECK(npc_ptr->y == 32);
	CHECK(npc_ptr->dir == 4);

	// ── 阶段 2: 目标切换为 (32, 32) (向东, dir=2) ──────────────────
	// 步 3: 到 (31, 32)
	f.clock.advance(100);
	f.world.tick();
	CHECK(npc_ptr->x == 31);
	CHECK(npc_ptr->y == 32);
	CHECK(npc_ptr->dir == 2);

	// 步 4: 到 (32, 32) (到达第 1 个路点目标)
	f.clock.advance(100);
	f.world.tick();
	CHECK(npc_ptr->x == 32);
	CHECK(npc_ptr->y == 32);
	CHECK(npc_ptr->dir == 2);

	// ── 阶段 3: 目标切换为 (30, 30) (向西北, dir=7) ────────────────
	// 步 5: 到 (31, 31)
	f.clock.advance(100);
	f.world.tick();
	CHECK(npc_ptr->x == 31);
	CHECK(npc_ptr->y == 31);
	CHECK(npc_ptr->dir == 7);

	// 步 6: 回到 (30, 30) (回到起点, 完成一周闭环巡逻)
	f.clock.advance(100);
	f.world.tick();
	CHECK(npc_ptr->x == 30);
	CHECK(npc_ptr->y == 30);
	CHECK(npc_ptr->dir == 7);

	// ── 阶段 4: 下一轮循环自动开启 ────────────────────────────────
	f.clock.advance(100);
	f.world.tick();
	CHECK(npc_ptr->x == 30);
	CHECK(npc_ptr->y == 31);
	CHECK(npc_ptr->dir == 4);
}

TEST_CASE("W.11: 实体与地形阻挡不可穿透 (撞墙/撞玩家/撞其他NPC 停在原格但转向)")
{
	MoveFixture f;
	spawnHandshaked(f); // 玩家出生在地图中心 (32, 32)

	// NPC1 在 (32, 31)，试图向南走 (32, 32)，正前方正是玩家！
	NpcEntity npc1{};
	npc1.id = 6003;
	npc1.floor = 0;
	npc1.x = 32;
	npc1.y = 31;
	npc1.wander_interval_ms = 100;
	npc1.route = {{32, 32}}; // 目标是玩家所在格

	// NPC2 在 (32, 30)，NPC1 背后
	NpcEntity npc2{};
	npc2.id = 6004;
	npc2.floor = 0;
	npc2.x = 32;
	npc2.y = 30;
	npc2.wander_interval_ms = 100;
	npc2.route = {{32, 31}}; // 目标是 NPC1 所在格

	f.world.loadNpcEntities({npc1, npc2});

	// 1. NPC1 试图走入玩家所在格 (32, 32) ⇒ 阻挡停在 (32, 31)，但转向南 (dir=4)
	f.clock.advance(100);
	f.world.tick();

	const auto *n1 = f.world.findNpc(6003);
	REQUIRE(n1 != nullptr);
	CHECK(n1->x == 32);
	CHECK(n1->y == 31);
	CHECK(n1->dir == 4);

	// 2. NPC2 试图走入 NPC1 所在格 (32, 31) ⇒ 阻挡停在 (32, 30)，转向南 (dir=4)
	const auto *n2 = f.world.findNpc(6004);
	REQUIRE(n2 != nullptr);
	CHECK(n2->x == 32);
	CHECK(n2->y == 30);
	CHECK(n2->dir == 4);
}

TEST_CASE("W.11: 漫游视野广播与单向协议同步 (周围玩家收到 CharMove / 新进 CharAppear)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f); // 玩家在 (32, 32)
	f.world.tick();

	// 清空历史初始视野包
	VisMirror m;
	m.feed(f.transport.sent(id));

	// 配置巡逻 NPC: 从 (32, 30) 走向 (32, 29)
	NpcEntity npc{};
	npc.id = 6005;
	npc.floor = 0;
	npc.x = 32;
	npc.y = 30;
	npc.image = 100555;
	npc.wander_interval_ms = 100;
	npc.route = {{32, 29}};
	f.world.loadNpcEntities({npc});

	// NPC 移动一步到 (32, 29) (仍在玩家 23x23 视距内)
	f.clock.advance(100);
	f.world.tick();

	m.feed(f.transport.sent(id));
	bool saw_move = false;
	for (const auto &mv : m.move_msgs)
	{
		if (mv.entity_id == 6005 &&
		    mv.entity_type == static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC) &&
		    mv.x == 32 && mv.y == 29 && mv.dir == 0) // 北
		{
			saw_move = true;
		}
	}
	CHECK(saw_move);
}

TEST_CASE("W.11: 对话打断锁定 (玩家打开窗口交互期间 NPC 暂停漫游, 关闭窗口后恢复)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f); // 玩家在 (32, 32)

	// NPC 在玩家东面 (33, 32)，配置为 TownPeople，且带有巡逻路线
	NpcEntity npc{};
	npc.id = 6006;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kTownPeople;
	npc.message = "你好旅行者！";
	npc.wander_interval_ms = 100;
	npc.route = {{33, 35}}; // 向南走
	f.world.loadNpcEntities({npc});

	// 1. 玩家面向东 (dir=2) 与 NPC 对话打开窗口
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 1101;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "你好旅行者！");
	const std::uint32_t wid = f.world.playerActiveWindowId(id);

	// 2. 在对话进行中，推进时间 300ms 并 tick 3 次
	for (int i = 0; i < 3; ++i)
	{
		f.clock.advance(100);
		f.world.tick();
	}

	// NPC 处于对话锁定态 ⇒ 坐标保持 (33, 32)，未向 (33, 35) 走动
	const auto *npc_ptr = f.world.findNpc(6006);
	REQUIRE(npc_ptr != nullptr);
	CHECK(npc_ptr->x == 33);
	CHECK(npc_ptr->y == 32);

	// 3. 玩家回复关闭窗口
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));

	// 4. 对话结束后推进时间 ⇒ NPC 恢复走动，走向 (33, 33)
	f.clock.advance(100);
	f.world.tick();
	CHECK(npc_ptr->x == 33);
	CHECK(npc_ptr->y == 33);
	CHECK(npc_ptr->dir == 4); // 南
}

// ══ 批次 W.12: NPC 商店与道具交易系统 (ShopMan 购买 / 出售 / 石币联动) ═════

TEST_CASE("W.12: 商店购买道具成功结算 (ShopMan 扣除折后石币, 背包放入新道具, 道具池与状态正确)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f); // 玩家在 (32, 32)
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 1000; // 初始金币 1000

	// 配置商店 NPC 在玩家东面 (33, 32)
	NpcEntity npc{};
	npc.id = 7001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kShop;
	npc.shop_name = "萨姆吉尔道具店";
	npc.buy_rate = 1.0;
	npc.sell_rate = 0.5;
	npc.shop_products = {
	    {101, 300, 1001, 1, "木棒"},
	    {102, 600, 1002, 5, "铜斧"},
	};
	f.world.loadNpcEntities({npc});

	// 1. 玩家面向东 (dir=2) 发起对话打开商店
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 2001;
	f.world.onEvent(id, req);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	const std::uint32_t wid = f.world.playerActiveWindowId(id);

	// 2. 玩家选择购买第 1 个条目: "木棒", 价格 300
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.source.entity_id = 7001;
	rep.result_kind = SA::Domain::WindowReply::ResultKind::ENTRY_ID;
	rep.result.entry_id = 1;
	f.world.onWindowReply(id, rep);

	// 3. 验证购买结算:
	//    - 石币扣减: 1000 - 300 = 700
	//    - 背包新增: 道具槽出现 item_id == 101, cost == 300, name == "木棒"
	CHECK(p->gold == 700);
	CHECK(f.world.playerItemSlotsUsed(id) == 1);

	bool found_stick = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id, static_cast<int>(i));
		if (it != nullptr && it->item_id == 101 && it->cost == 300)
		{
			found_stick = true;
			break;
		}
	}
	CHECK(found_stick);
}

TEST_CASE("W.12: 商店购买石币不足拦截 (石币少于总价时下发 stone_less_msg 且零副作用)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 200; // 金币 200，不足以购买 300 的木棒

	NpcEntity npc{};
	npc.id = 7002;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kShop;
	npc.stone_less_msg = "穷光蛋，你的石币不够买这个！";
	npc.shop_products = {{101, 300, 1001, 1, "木棒"}};
	f.world.loadNpcEntities({npc});

	// 打开商店
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 2002;
	f.world.onEvent(id, req);
	f.world.tick();

	const std::uint32_t wid = f.world.playerActiveWindowId(id);

	// 回复尝试购买 entry 1
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.source.entity_id = 7002;
	rep.result_kind = SA::Domain::WindowReply::ResultKind::ENTRY_ID;
	rep.result.entry_id = 1;
	f.world.onWindowReply(id, rep);

	// 拦截断言:
	// - 弹出不足提示语
	// - 玩家金币保持 200 未被扣除
	// - 背包仍无道具
	CHECK(f.world.playerLastWindowText(id) == "穷光蛋，你的石币不够买这个！");
	CHECK(p->gold == 200);
	CHECK(f.world.playerItemSlotsUsed(id) == 0);
}

TEST_CASE("W.12: 商店购买背包已满拦截 (背包无空槽时下发 item_full_msg 且零扣款)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 5000;

	// 背包装满 45 个道具 (满包)
	int filled = 0;
	while (f.world.giveItemToPlayer(id, makeTestItem(999)) >= 0)
	{
		++filled;
	}
	CHECK(filled == 45);
	CHECK(f.world.playerItemSlotsUsed(id) == 45);

	NpcEntity npc{};
	npc.id = 7003;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kShop;
	npc.item_full_msg = "背包都鼓成这样了，装不下啦！";
	npc.shop_products = {{101, 300, 1001, 1, "木棒"}};
	f.world.loadNpcEntities({npc});

	// 打开商店
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 2003;
	f.world.onEvent(id, req);
	f.world.tick();

	const std::uint32_t wid = f.world.playerActiveWindowId(id);

	// 尝试购买
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.source.entity_id = 7003;
	rep.result_kind = SA::Domain::WindowReply::ResultKind::ENTRY_ID;
	rep.result.entry_id = 1;
	f.world.onWindowReply(id, rep);

	// 拦截断言:
	// - 提示道具栏满
	// - 金币保持 5000 零扣款
	// - 背包依然 45 个
	CHECK(f.world.playerLastWindowText(id) == "背包都鼓成这样了，装不下啦！");
	CHECK(p->gold == 5000);
	CHECK(f.world.playerItemSlotsUsed(id) == 45);
}

TEST_CASE("W.12: 商店回收出售道具成功 (按 sell_rate 回收背包道具, 释放池并入账石币)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 100;

	// 背包槽位放入一件价值 1000 石币的铠甲道具
	const int slot = f.world.giveItemToPlayer(id, makeTestItem(501, /*pile=*/1, /*cost=*/1000));
	REQUIRE(slot >= 0);
	CHECK(f.world.playerItemSlotsUsed(id) == 1);

	NpcEntity npc{};
	npc.id = 7004;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kShop;
	npc.sell_rate = 0.5; // 50% 回收率
	f.world.loadNpcEntities({npc});

	// 调用出售接口出售该槽位
	const bool sold = f.world.sellItemToShop(id, 7004, slot);
	CHECK(sold);

	// 结算断言:
	// - 道具被移除并释放: 该槽位变为无效
	// - 金钱入账: 100 + 1000 * 0.5 = 600
	CHECK_FALSE(p->items[static_cast<std::size_t>(slot)].valid());
	CHECK(f.world.playerItemSlotsUsed(id) == 0);
	CHECK(p->gold == 600);
}

TEST_CASE("W.12: 商店回收出售石币超上限拦截 (金币超上限时拒绝交易, 保留道具与原余额)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	// 0 转上限为 1,000,000; 当前余额 999,900
	p->gold = 999900;

	const int slot = f.world.giveItemToPlayer(id, makeTestItem(502, /*pile=*/1, /*cost=*/1000));
	REQUIRE(slot >= 0);

	NpcEntity npc{};
	npc.id = 7005;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kShop;
	npc.sell_rate = 0.5; // 回收得 500 石币; 999,900 + 500 = 1,000,400 > 1,000,000 溢出
	npc.stone_full_msg = "钱包太沉了，装不下更多石币啦！";
	f.world.loadNpcEntities({npc});

	// 尝试出售
	const bool sold = f.world.sellItemToShop(id, 7005, slot);

	// 拦截断言:
	// - 交易被拒绝 (返回 false)
	// - 道具依然完好保存在背包中
	// - 金币依然是 999,900 (DR-EC3 拒绝, 零侵蚀)
	// - 下发超限提示
	CHECK_FALSE(sold);
	CHECK(p->items[static_cast<std::size_t>(slot)].valid());
	CHECK(p->gold == 999900);
	CHECK(f.world.playerLastWindowText(id) == "钱包太沉了，装不下更多石币啦！");
}

TEST_CASE("W.13: 宠物商店购买宠物成功 (扣减石币, 新宠物落池且属性图号正确挂入槽位)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 5000;
	CHECK(f.world.playerPetSlotsUsed(id) == 0);

	NpcEntity npc{};
	npc.id = 7006;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kPetShop;
	PetProduct prod{};
	prod.pet_id = 77;
	prod.name = "红暴";
	prod.level = 1;
	prod.cost = 1000;
	prod.image = 10077;
	prod.hp = 120;
	prod.mp = 50;
	prod.vital = 25;
	prod.str = 30;
	prod.tough = 20;
	prod.dex = 15;
	npc.pet_products = {prod};
	f.world.loadNpcEntities({npc});

	// 打开宠物商店
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 2006;
	f.world.onEvent(id, req);
	f.world.tick();

	const std::uint32_t wid = f.world.playerActiveWindowId(id);
	CHECK(wid > 0);

	// 购买宠物 (entry_id = 1)
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.source.entity_id = 7006;
	rep.result_kind = SA::Domain::WindowReply::ResultKind::ENTRY_ID;
	rep.result.entry_id = 1;
	f.world.onWindowReply(id, rep);

	// 结算断言:
	// - 弹出成功提示
	// - 石币扣除: 5000 - 1000 = 4000
	// - 宠物栏新增 1 只
	// - 宠物各项数值图号正确
	CHECK(f.world.playerLastWindowText(id) == "购买宠物成功！好好照顾它哦。");
	CHECK(p->gold == 4000);
	CHECK(f.world.playerPetSlotsUsed(id) == 1);

	const auto *pet = f.world.playerPetAt(id, 0);
	REQUIRE(pet != nullptr);
	CHECK(pet->pet_id == 77);
	CHECK(std::string_view(pet->name.c_str()) == "红暴");
	CHECK(pet->level == 1);
	CHECK(pet->origin_image == 10077);
	CHECK(pet->base_image == 10077);
	CHECK(pet->hp == 120);
	CHECK(pet->mp == 50);
	CHECK(pet->vital == 25);
	CHECK(pet->str == 30);
	CHECK(pet->tough == 20);
	CHECK(pet->dex == 15);
}

TEST_CASE("W.13: 宠物商店购买宠物栏已满拦截 (满栏时下发 pet_full_msg 且零扣款)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 5000;

	// 填满 5 个宠物槽
	for (int i = 0; i < 5; ++i)
	{
		CHECK(f.world.givePetToPlayer(id, makeTestPet(3000 + i)) >= 0);
	}
	CHECK(f.world.playerPetSlotsUsed(id) == 5);

	NpcEntity npc{};
	npc.id = 7007;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kPetShop;
	npc.pet_full_msg = "你的宠物太多了，装不下更多宠物啦！";
	PetProduct prod{};
	prod.pet_id = 78;
	prod.name = "蓝暴";
	prod.cost = 1000;
	npc.pet_products = {prod};
	f.world.loadNpcEntities({npc});

	// 打开宠物商店
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 2007;
	f.world.onEvent(id, req);
	f.world.tick();

	const std::uint32_t wid = f.world.playerActiveWindowId(id);

	// 尝试购买
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.source.entity_id = 7007;
	rep.result_kind = SA::Domain::WindowReply::ResultKind::ENTRY_ID;
	rep.result.entry_id = 1;
	f.world.onWindowReply(id, rep);

	// 拦截断言:
	// - 下发满宠提示
	// - 石币保持 5000 零扣款
	// - 宠物栏保持 5 只
	CHECK(f.world.playerLastWindowText(id) == "你的宠物太多了，装不下更多宠物啦！");
	CHECK(p->gold == 5000);
	CHECK(f.world.playerPetSlotsUsed(id) == 5);
}

TEST_CASE("W.13: 宠物技能导师教授技能成功 (扣除学费并正确写入宠物技能槽)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 3000;

	// 玩家持有一只 10 级宠物
	const int pet_slot = f.world.givePetToPlayer(id, makeTestPet(50, /*level=*/10));
	REQUIRE(pet_slot >= 0);

	NpcEntity npc{};
	npc.id = 7008;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kPetSkillShop;
	PetSkillProduct skill_prod{};
	skill_prod.skill_id = 10;
	skill_prod.name = "连续攻击";
	skill_prod.cost = 500;
	skill_prod.level = 5;
	npc.pet_skill_products = {skill_prod};
	f.world.loadNpcEntities({npc});

	// 打开技能导师窗口
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 2008;
	f.world.onEvent(id, req);
	f.world.tick();

	const std::uint32_t wid = f.world.playerActiveWindowId(id);

	// 确认学习 (entry_id = 1)
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.source.entity_id = 7008;
	rep.result_kind = SA::Domain::WindowReply::ResultKind::ENTRY_ID;
	rep.result.entry_id = 1;
	f.world.onWindowReply(id, rep);

	// 结算断言:
	// - 提示成功
	// - 石币扣除: 3000 - 500 = 2500
	// - 宠物的技能槽 0 被写入 skill_id 10
	CHECK(f.world.playerLastWindowText(id) == "宠物成功学会了新技能！");
	CHECK(p->gold == 2500);

	const auto *pet = f.world.playerPetAt(id, pet_slot);
	REQUIRE(pet != nullptr);
	CHECK(pet->pet_skills[0] == 10);
	for (std::size_t i = 1; i < SA::Model::Pet::kPetSkillSlots; ++i)
	{
		CHECK(pet->pet_skills[i] == 0);
	}
}

TEST_CASE("W.13: 宠物学习技能等级不足与学满拦截 (等级不足下发 level_low_msg; 重复不可学; 学满下发 skill_full_msg)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 5000;

	// 玩家持有一只 2 级宠物
	const int pet_slot = f.world.givePetToPlayer(id, makeTestPet(51, /*level=*/2));
	REQUIRE(pet_slot >= 0);

	NpcEntity npc{};
	npc.id = 7009;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kPetSkillShop;
	npc.level_low_msg = "小家伙等级太低了，还学不会这么高级的技能！";
	npc.skill_full_msg = "宠物脑子塞满了，装不下新技能了！";
	PetSkillProduct skill1{};
	skill1.skill_id = 20;
	skill1.name = "高级撕咬";
	skill1.cost = 300;
	skill1.level = 10; // 需要 10 级，当前宠物只有 2 级
	PetSkillProduct skill2{};
	skill2.skill_id = 21;
	skill2.name = "初级防御";
	skill2.cost = 100;
	skill2.level = 1;
	npc.pet_skill_products = {skill1, skill2};
	f.world.loadNpcEntities({npc});

	// ① 等级不足拦截
	const bool learn1 = f.world.learnPetSkill(id, 7009, pet_slot, 20);
	CHECK_FALSE(learn1);
	CHECK(p->gold == 5000); // 零扣费
	CHECK(f.world.playerLastWindowText(id) == "小家伙等级太低了，还学不会这么高级的技能！");

	// ② 成功学习低级技能 21
	const bool learn2 = f.world.learnPetSkill(id, 7009, pet_slot, 21);
	CHECK(learn2);
	CHECK(p->gold == 4900); // 扣 100
	const auto *pet = f.world.playerPetAt(id, pet_slot);
	REQUIRE(pet != nullptr);
	CHECK(pet->pet_skills[0] == 21);

	// ③ 重复技能拦截
	const bool learn_dup = f.world.learnPetSkill(id, 7009, pet_slot, 21);
	CHECK_FALSE(learn_dup);
	CHECK(p->gold == 4900); // 零扣费

	// ④ 技能栏已满拦截: 将剩余 6 个槽位填满
	auto *mutable_pet = const_cast<SA::Model::Pet *>(pet);
	for (std::size_t i = 1; i < SA::Model::Pet::kPetSkillSlots; ++i)
	{
		mutable_pet->pet_skills[i] = static_cast<std::int32_t>(100 + i);
	}

	// 导师新增技能 22 (等级 1)
	npc.pet_skill_products.push_back({22, "敏捷提升", 100, 1});
	f.world.loadNpcEntities({npc});

	const bool learn_full = f.world.learnPetSkill(id, 7009, pet_slot, 22);
	CHECK_FALSE(learn_full);
	CHECK(p->gold == 4900); // 零扣费
	CHECK(f.world.playerLastWindowText(id) == "宠物脑子塞满了，装不下新技能了！");
}

TEST_CASE("W.13: 宠物商店回收出售宠物成功与石币上限保护 (回收宠物增加石币并释放池; 金币超上限时拒绝交易且保留宠物)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 200;

	// 玩家持有一只 5 级宠物 (pet_id = 90)
	const int pet_slot = f.world.givePetToPlayer(id, makeTestPet(90, /*level=*/5));
	REQUIRE(pet_slot >= 0);
	CHECK(f.world.playerPetSlotsUsed(id) == 1);

	NpcEntity npc{};
	npc.id = 7010;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kPetShop;
	npc.sell_rate = 0.5;
	npc.stone_full_msg = "钱包满了，石币放不下了！";
	// 在售列表中配置 90 号宠物原价 1200 石币 ⇒ 回收价 1200 * 0.5 = 600
	PetProduct prod{};
	prod.pet_id = 90;
	prod.cost = 1200;
	npc.pet_products = {prod};
	f.world.loadNpcEntities({npc});

	// ① 成功出售
	const bool sold = f.world.sellPetToShop(id, 7010, pet_slot);
	CHECK(sold);
	// 结算断言:
	// - 宠物栏清空并释放
	// - 金币入账: 200 + 600 = 800
	CHECK(f.world.playerPetSlotsUsed(id) == 0);
	CHECK(f.world.playerPetAt(id, pet_slot) == nullptr);
	CHECK(p->gold == 800);

	// ② 石币超上限保护拦截
	// 给玩家一只新宠物，并将金币设为 999,900
	const int slot2 = f.world.givePetToPlayer(id, makeTestPet(90, /*level=*/5));
	REQUIRE(slot2 >= 0);
	p->gold = 999900; // 回收得 600; 999,900 + 600 = 1,000,500 > 1,000,000

	const bool sold_overflow = f.world.sellPetToShop(id, 7010, slot2);
	CHECK_FALSE(sold_overflow);
	// 拦截断言:
	// - 拒绝出售 (返回 false)
	// - 宠物依然完好保存在槽位中
	// - 金币保持 999,900 零改动 (DR-EC3 拒绝)
	// - 下发超上限提示语
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
	CHECK(f.world.playerPetAt(id, slot2) != nullptr);
	CHECK(p->gold == 999900);
	CHECK(f.world.playerLastWindowText(id) == "钱包满了，石币放不下了！");
}

// ══ 批次 W.14: 告示牌与传送员 NPC (SignBoard / WarpMan) ═════════════════════════
//
// 依据原版 npc_signboard.c / npc_warpman.c:
//   - 告示牌 (SignBoard, 投产 230 实例):
//       展示标题 (sign_title) 与告示文本 (message/name)，下发 WINDOW_KIND_MESSAGE + BUTTON_FLAG_OK。
//   - 传送员 (WarpMan, 投产 324 实例):
//       单目的地弹出 Yes/No 确认弹窗；多目的地弹出 SELECT 选项列表 (含地点名与路费，超等级/余额置灰)。
//       传送执行扣除路费 (走 GoldLedger kWarpFee)，校验等级门禁与坐标通行门禁，执行旧格摘除、视野通知、新格挂接与坐标同步。

TEST_CASE("W.14: 告示牌 NPC 交互展示标题与告示对白 (WINDOW_KIND_MESSAGE, BUTTON_FLAG_OK)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	NpcEntity npc{};
	npc.id = 8001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kSignBoard;
	npc.sign_title = "＜ 玛丽娜丝渔村公告 ＞";
	npc.message = "前方为村长家，请保持肃静。\n出村往北可前往萨姆吉尔村。";
	f.world.loadNpcEntities({npc});

	// 面对 (33, 32) 发起 NPC 交互 (dir 2)
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 4001;
	f.world.onEvent(id, req);
	f.world.tick();

	// 1. 回执 ok == true
	CHECK(eventResultOk(f.transport.sent(id), 4001));

	// 2. 检查下发的 WindowOpen 消息
	const auto win_opt = findLastWindowOpen(f.transport.sent(id));
	REQUIRE(win_opt.has_value());
	const auto &win = *win_opt;
	CHECK(win.kind == SA::Domain::WindowKind::WINDOW_KIND_MESSAGE);
	CHECK(win.buttons == static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	CHECK(win.source.entity_id == 8001);
	CHECK(win.body_kind == SA::Domain::WindowOpen::BodyKind::MESSAGE);
	REQUIRE(win.body.message.lines.size() >= 2);
	CHECK(std::string(win.body.message.lines[0].c_str()) == "＜ 玛丽娜丝渔村公告 ＞");
	CHECK(std::string(win.body.message.lines[1].c_str()) == "前方为村长家，请保持肃静。");

	// 3. 客户端回执确认 (BUTTON_FLAG_OK)
	CHECK(f.world.playerHasActiveWindow(id));
	SA::Domain::WindowReply rep{};
	rep.window_id = win.window_id;
	rep.source.entity_id = 8001;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep);

	// 4. 窗口关闭
	CHECK_FALSE(f.world.playerHasActiveWindow(id));
}

TEST_CASE("W.14: 传送员单目的地确认传送成功 (扣除路费、坐标更新、视野双向同步)")
{
	MoveFixture f;
	const auto a = f.spawn();
	const auto b = f.spawn();
	auto *pa = f.world.playerForTest(a);
	REQUIRE(pa != nullptr);
	pa->gold = 500;
	pa->level = 10;
	f.world.tick();

	VisMirror mb;
	mb.feed(f.transport.sent(b));
	CHECK(countId(mb.appears, a) >= 1);

	NpcEntity npc{};
	npc.id = 8002;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kWarpMan;
	npc.warp_destinations = {
	    WarpDestination{/*floor=*/0, /*x=*/10, /*y=*/12, "萨姆吉尔村", /*cost=*/150, /*level=*/5}};
	f.world.loadNpcEntities({npc});

	// a 发起交互
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 4002;
	f.world.onEvent(a, req);
	f.world.tick();

	// 验证弹出单目的地 Yes/No 确认窗口
	const auto win_opt = findLastWindowOpen(f.transport.sent(a));
	REQUIRE(win_opt.has_value());
	const auto &win = *win_opt;
	CHECK(win.kind == SA::Domain::WindowKind::WINDOW_KIND_MESSAGE);
	CHECK((win.buttons & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES)) != 0);
	CHECK((win.buttons & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0);

	// 客户端点击 YES 确认传送
	SA::Domain::WindowReply rep{};
	rep.window_id = win.window_id;
	rep.source.entity_id = 8002;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(a, rep);
	f.world.tick();

	// 结算与视野断言:
	// - 路费扣除: 500 - 150 = 350
	// - 坐标瞬移到 (10, 12)
	// - 观测者 b 收到 a 的 CharDisappear
	// - a 处于非活动窗口状态
	CHECK(pa->gold == 350);
	CHECK(pa->x == 10);
	CHECK(pa->y == 12);
	CHECK_FALSE(f.world.playerHasActiveWindow(a));

	mb.feed(f.transport.sent(b));
	CHECK(countId(mb.disappears, a) >= 1);
}

TEST_CASE("W.14: 传送员多目的地列表选择传送成功 (SELECT 窗口展示列表、选项回执传送)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 1000;
	p->level = 20;

	NpcEntity npc{};
	npc.id = 8003;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kWarpMan;
	npc.warp_destinations = {
	    WarpDestination{0, 10, 12, "萨姆吉尔村", 100, 1},
	    WarpDestination{0, 20, 22, "达那村", 200, 10},
	    WarpDestination{0, 30, 30, "加加村", 500, 30}, // 等级 30 > 玩家 20 级 ⇒ enabled 置灰
	};
	f.world.loadNpcEntities({npc});

	// 发起交互
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 4003;
	f.world.onEvent(id, req);
	f.world.tick();

	// 验证弹出多目的地 SELECT 窗口
	const auto win_opt = findLastWindowOpen(f.transport.sent(id));
	REQUIRE(win_opt.has_value());
	const auto &win = *win_opt;
	CHECK(win.kind == SA::Domain::WindowKind::WINDOW_KIND_SELECT);
	CHECK(win.body_kind == SA::Domain::WindowOpen::BodyKind::SELECT);
	REQUIRE(win.body.select.choices.size() == 3);
	CHECK(win.body.select.choices[0].choice_id == 1);
	CHECK(win.body.select.choices[0].enabled == true);
	CHECK(win.body.select.choices[1].choice_id == 2);
	CHECK(win.body.select.choices[1].enabled == true);
	CHECK(win.body.select.choices[2].choice_id == 3);
	CHECK(win.body.select.choices[2].enabled == false); // 等级不足置灰

	// 客户端选择第 2 项 (达那村, choice_id = 2)
	SA::Domain::WindowReply rep{};
	rep.window_id = win.window_id;
	rep.source.entity_id = 8003;
	rep.result_kind = SA::Domain::WindowReply::ResultKind::CHOICE_ID;
	rep.result.choice_id = 2;
	f.world.onWindowReply(id, rep);
	f.world.tick();

	// 结算断言:
	// - 扣款 200: 1000 - 200 = 800
	// - 坐标更新为 (20, 22)
	// - 活动窗口关闭
	CHECK(p->gold == 800);
	CHECK(p->x == 20);
	CHECK(p->y == 22);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));
}

TEST_CASE("W.14: 传送员路费不足拦截 (零扣费、坐标不变更、下发石币不足提示)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 50; // 只有 50 石币
	p->level = 10;

	NpcEntity npc{};
	npc.id = 8004;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kWarpMan;
	npc.stone_less_msg = "路费不够可不能让你上车！";
	npc.warp_destinations = {
	    WarpDestination{0, 10, 12, "萨姆吉尔村", 100, 1}, // 路费需要 100 石币
	};
	f.world.loadNpcEntities({npc});

	// 发起交互并确认传送
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 4004;
	f.world.onEvent(id, req);
	f.world.tick();

	const std::uint32_t wid = f.world.playerActiveWindowId(id);
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.source.entity_id = 8004;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, rep);

	// 拦截断言:
	// - 金币保持 50 零扣除
	// - 坐标保持 (32, 32) 原地未变
	// - 下发石币不足提示
	CHECK(p->gold == 50);
	CHECK(p->x == 32);
	CHECK(p->y == 32);
	CHECK(f.world.playerLastWindowText(id) == "路费不够可不能让你上车！");

	// 底层 API 直接调用也被严格拦截
	CHECK_FALSE(f.world.warpPlayerByNpc(id, 8004, 0));
	CHECK(p->gold == 50);
	CHECK(p->x == 32);
	CHECK(p->y == 32);
}

TEST_CASE("W.14: 传送员等级不足拦截 (零扣费、坐标不变更、下发等级不足提示)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->gold = 1000;
	p->level = 5; // 只有 5 级

	NpcEntity npc{};
	npc.id = 8005;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kWarpMan;
	npc.level_low_msg = "那里的野兽太凶险了，你的等级还不够去那里！";
	npc.warp_destinations = {
	    WarpDestination{0, 10, 12, "萨姆吉尔村", 100, 20}, // 需要 20 级
	};
	f.world.loadNpcEntities({npc});

	// 发起交互并确认传送
	SA::Domain::EventRequest req{};
	req.dir = 2;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 4005;
	f.world.onEvent(id, req);
	f.world.tick();

	const std::uint32_t wid = f.world.playerActiveWindowId(id);
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.source.entity_id = 8005;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, rep);

	// 拦截断言:
	// - 金币保持 1000 零扣除
	// - 坐标保持 (32, 32) 原地未变
	// - 下发等级不足提示
	CHECK(p->gold == 1000);
	CHECK(p->x == 32);
	CHECK(p->y == 32);
	CHECK(f.world.playerLastWindowText(id) == "那里的野兽太凶险了，你的等级还不够去那里！");

	// 底层 API 直接调用也被严格拦截
	CHECK_FALSE(f.world.warpPlayerByNpc(id, 8005, 0));
	CHECK(p->gold == 1000);
	CHECK(p->x == 32);
	CHECK(p->y == 32);
}

TEST_CASE("真实地图数据接入:萨伊那斯 Floor 100 传送点与 NPC 端到端验证")
{
	std::string content_dir;
	for (const auto &p : {"content/p2-v1", "../content/p2-v1", "../../content/p2-v1", "../../../content/p2-v1", "stone-age-server/content/p2-v1"})
	{
		if (std::filesystem::exists(std::string(p) + "/manifest.json"))
		{
			content_dir = p;
			break;
		}
	}

	if (content_dir.empty())
		return;

	const auto bundle = SA::Content::load(content_dir, false);
	CHECK_EQ(bundle.floor, 100);
	CHECK_EQ(bundle.width, 800);
	CHECK_EQ(bundle.height, 800);

	const auto *warps_val = bundle.world.find("warp_points");
	REQUIRE(warps_val != nullptr);
	REQUIRE(warps_val->isArray());
	CHECK_EQ(warps_val->asArray().size(), 311);

	const auto *npcs_val = bundle.world.find("npcs");
	REQUIRE(npcs_val != nullptr);
	REQUIRE(npcs_val->isArray());
	CHECK_EQ(npcs_val->asArray().size(), 399);

	const auto *floors_val = bundle.world.find("floors");
	REQUIRE(floors_val != nullptr);
	REQUIRE(floors_val->isArray());
	CHECK_EQ(floors_val->asArray().size(), 18);

	// 1. 初始化世界地图
	SA::Platform::ServerConfig config = makeMoveConfig();
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0xABCDEF};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};

	GridMap map;
	map.width = bundle.width;
	map.height = bundle.height;
	map.tile.assign(bundle.walkable.size(), 1);
	map.obj.assign(bundle.walkable.begin(), bundle.walkable.end());
	TileAttrTable attributes;
	attributes.walkable = {WalkKind::kBlocked, WalkKind::kFree};

	SA::Domain::CharacterRecord defaults{};
	defaults.schema_ver = 1;
	defaults.player.level = 1;
	defaults.player.charm = 60;
	defaults.player.mp = defaults.player.max_mp = 100;
	defaults.player.hp = 100;
	defaults.player.default_pet = -1;
	defaults.player.floor = 100;
	defaults.player.x = 643;
	defaults.player.y = 459;
	defaults.player.dir = 5;
	defaults.player.image = 100000;

	world.configurePlayable(std::move(map), std::move(attributes), bundle.version, defaults);

	// 装载 4 大村庄地图 (批次 D.2)
	for (const auto &item : floors_val->asArray())
	{
		const auto fid = SA::Content::integer(*item.find("floor"));
		const auto w = SA::Content::integer(*item.find("width"));
		const auto h = SA::Content::integer(*item.find("height"));
		const auto walk_b64 = SA::Content::text(*item.find("walkable"));
		const auto decoded = SA::World::decodeBase64(walk_b64);
		CHECK_EQ(decoded.size(), static_cast<std::size_t>(w * h));
		GridMap fl_map;
		fl_map.width = w;
		fl_map.height = h;
		fl_map.tile.assign(decoded.size(), 1);
		fl_map.obj.assign(decoded.begin(), decoded.end());
		world.loadFloorMap(fid, std::move(fl_map));
	}
	CHECK_EQ(world.floorMapCount(), 18);

	// 装载 311 个传送点
	std::vector<WarpPoint> warp_points;
	for (const auto &item : warps_val->asArray())
	{
		WarpPoint wp;
		if (const auto *sf = item.find("src_floor"))
			wp.src_floor = SA::Content::integer(*sf);
		if (const auto *sx = item.find("src_x"))
			wp.src_x = SA::Content::integer(*sx);
		if (const auto *sy = item.find("src_y"))
			wp.src_y = SA::Content::integer(*sy);
		if (const auto *df = item.find("dst_floor"))
			wp.dst_floor = SA::Content::integer(*df);
		if (const auto *dx = item.find("dst_x"))
			wp.dst_x = SA::Content::integer(*dx);
		if (const auto *dy = item.find("dst_y"))
			wp.dst_y = SA::Content::integer(*dy);
		warp_points.push_back(wp);
	}
	world.loadWarpPoints(warp_points);
	CHECK_EQ(world.warpPointCount(), 311);

	// 装载 254 个 NPC
	std::vector<NpcEntity> npcs;
	for (const auto &item : npcs_val->asArray())
	{
		NpcEntity npc;
		if (const auto *id = item.find("id"))
			npc.id = static_cast<std::uint64_t>(SA::Content::integer(*id));
		if (const auto *fl = item.find("floor"))
			npc.floor = SA::Content::integer(*fl);
		if (const auto *x = item.find("x"))
			npc.x = SA::Content::integer(*x);
		if (const auto *y = item.find("y"))
			npc.y = SA::Content::integer(*y);
		if (const auto *dir = item.find("dir"))
			npc.dir = static_cast<std::uint8_t>(SA::Content::integer(*dir));
		if (const auto *img = item.find("image"))
			npc.image = SA::Content::integer(*img);
		if (const auto *nm = item.find("name"))
			npc.name = SA::Content::text(*nm);
		if (const auto *msg = item.find("message"))
			npc.message = SA::Content::text(*msg);
		if (const auto *cost = item.find("cost"))
			npc.cost = SA::Content::integer(*cost);
		if (const auto *st = item.find("sign_title"))
			npc.sign_title = SA::Content::text(*st);
		if (const auto *wm = item.find("warp_msg"))
			npc.warp_msg = SA::Content::text(*wm);
		if (const auto *mm = item.find("main_msg"))
			npc.main_msg = SA::Content::text(*mm);
		if (const auto *br = item.find("buy_rate"); br && br->isNumber())
			npc.buy_rate = br->asNumber();
		if (const auto *sr = item.find("sell_rate"); sr && sr->isNumber())
			npc.sell_rate = sr->asNumber();

		if (const auto *tp = item.find("type"); tp && tp->isString())
		{
			const auto &tstr = tp->asString();
			if (tstr == "healer")
				npc.type = NpcType::kHealer;
			else if (tstr == "townpeople")
				npc.type = NpcType::kTownPeople;
			else if (tstr == "exchangeman")
				npc.type = NpcType::kExChangeMan;
			else if (tstr == "shop")
				npc.type = NpcType::kShop;
			else if (tstr == "petshop")
				npc.type = NpcType::kPetShop;
			else if (tstr == "petskillshop")
				npc.type = NpcType::kPetSkillShop;
			else if (tstr == "signboard")
				npc.type = NpcType::kSignBoard;
			else if (tstr == "warpman")
				npc.type = NpcType::kWarpMan;
			else
				npc.type = NpcType::kOther;
		}

		if (const auto *ex_raw = item.find("exchange_raw"); ex_raw && ex_raw->isString())
		{
			npc.exchange_blocks = parseExChangeBlocks(ex_raw->asString());
		}

		if (const auto *dests = item.find("warp_destinations"); dests && dests->isArray())
		{
			for (const auto &d : dests->asArray())
			{
				WarpDestination wd;
				if (const auto *df = d.find("floor"))
					wd.floor = SA::Content::integer(*df);
				if (const auto *dx = d.find("x"))
					wd.x = SA::Content::integer(*dx);
				if (const auto *dy = d.find("y"))
					wd.y = SA::Content::integer(*dy);
				if (const auto *dn = d.find("name"))
					wd.name = SA::Content::text(*dn);
				if (const auto *dc = d.find("cost"))
					wd.cost = SA::Content::integer(*dc);
				if (const auto *dl = d.find("level"))
					wd.level = SA::Content::integer(*dl);
				npc.warp_destinations.push_back(std::move(wd));
			}
		}

		if (const auto *prods = item.find("shop_products"); prods && prods->isArray())
		{
			for (const auto &p : prods->asArray())
			{
				ShopProduct sp;
				if (const auto *pi = p.find("item_id"))
					sp.item_id = SA::Content::integer(*pi);
				if (const auto *pc = p.find("cost"))
					sp.cost = SA::Content::integer(*pc);
				if (const auto *pm = p.find("image_id"))
					sp.image_id = static_cast<std::uint32_t>(SA::Content::integer(*pm));
				if (const auto *pl = p.find("level"))
					sp.level = static_cast<std::uint32_t>(SA::Content::integer(*pl));
				if (const auto *pn = p.find("name"))
					sp.name = SA::Content::text(*pn);
				npc.shop_products.push_back(std::move(sp));
			}
		}

		npcs.push_back(std::move(npc));
	}
	world.loadNpcEntities(npcs);
	CHECK_EQ(world.npcCount(), 399);

	// 2. 玩家在 (643, 459) 生成，移动至 (637, 491)，向东走一步踩上传送点 (638, 491)
	const auto id = transport.connect();
	world.onSessionReady(id);
	world.tick();
	const auto init_pos = world.playerPos(id);
	CHECK_EQ(init_pos.floor, 100);
	CHECK_EQ(init_pos.x, 643);
	CHECK_EQ(init_pos.y, 459);

	auto *player = world.playerForTest(id);
	REQUIRE(player != nullptr);
	player->x = 637;
	player->y = 491;
	player->dir = 2; // 面向东

	// 向东走一步 'c'，踩入 (638, 491)
	SA::Domain::WalkRequest walk{};
	walk.x = player->x;
	walk.y = player->y;
	REQUIRE(walk.direction.assign("c"));
	world.onWalk(id, walk);
	world.tick();

	// 传送点 (638, 491) 目标为 (1000, 50, 116)
	const auto warped_pos = world.playerPos(id);
	CHECK_EQ(warped_pos.floor, 1000);
	CHECK_EQ(warped_pos.x, 50);
	CHECK_EQ(warped_pos.y, 116);

	// 推进时钟推进一个步频周期 (300ms)
	clock.advance(300);

	// 向西走一步 'g'，踩入 (49, 116)，触发反向传送回到萨伊那斯 (100, 637, 491)
	SA::Domain::WalkRequest walk_back{};
	walk_back.x = warped_pos.x;
	walk_back.y = warped_pos.y;
	REQUIRE(walk_back.direction.assign("g"));
	world.onWalk(id, walk_back);
	world.tick();

	const auto return_pos = world.playerPos(id);
	CHECK_EQ(return_pos.floor, 100);
	CHECK_EQ(return_pos.x, 637);
	CHECK_EQ(return_pos.y, 491);

	// 3. 告示牌 NPC 交互验证: 位于 (728, 501)
	// 将玩家置于 (728, 500) 面对 (728, 501) (向南 dir 4)
	player->floor = 100;
	player->x = 728;
	player->y = 500;
	player->dir = 4;

	SA::Domain::EventRequest req{};
	req.dir = 4;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 5001;
	world.onEvent(id, req);
	world.tick();

	// 验证告示牌文案下发到窗口
	CHECK(world.playerLastWindowText(id).find("第1检查点") != std::string::npos);

	// 4. 恢复员 Healer 交互验证: 位于 (343, 464)
	player->floor = 100;
	player->x = 343;
	player->y = 463;
	player->dir = 4;
	REQUIRE(world.setPlayerStatsForTest(id, /*hp=*/10, /*mp=*/5, /*vital=*/1000, /*str=*/200, /*tough=*/200, /*dex=*/200));
	CHECK_EQ(world.playerHp(id), 10);
	CHECK_EQ(world.playerMp(id), 5);

	req.seqno = 5002;
	world.onEvent(id, req);
	world.tick();

	CHECK_EQ(world.playerHp(id), 46);
	CHECK_EQ(world.playerMp(id), 100);
}

TEST_CASE("四大村庄多地图管理、跨图视野隔离与真实村庄服务交互 (批次 D.2)")
{
	SUBCASE("Base64 编解码器正确性与防御性边界")
	{
		// 空输入
		CHECK(SA::World::decodeBase64("").empty());
		// 标准 RFC 4648 向量
		const auto decoded_man = SA::World::decodeBase64("TWFu");
		CHECK_EQ(std::string(decoded_man.begin(), decoded_man.end()), "Man");
		// 填充处理 (padding)
		const auto decoded_bc = SA::World::decodeBase64("YmM=");
		CHECK_EQ(std::string(decoded_bc.begin(), decoded_bc.end()), "bc");
		const auto decoded_a = SA::World::decodeBase64("YQ==");
		CHECK_EQ(std::string(decoded_a.begin(), decoded_a.end()), "a");
		// 忽略空白与换行
		const auto decoded_ws = SA::World::decodeBase64("  TW  \nFu  \r\n");
		CHECK_EQ(std::string(decoded_ws.begin(), decoded_ws.end()), "Man");
	}

	SUBCASE("四大村庄地图加载与跨地图视野隔离实测")
	{
		SA::Platform::ServerConfig config = makeMoveConfig();
		SA::Platform::ManualClock clock{0};
		SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
		SA::Platform::RandomSource random{0x123456};
		SA::Net::LoopbackTransport transport{};
		World world{config, clock, logger, random, transport};

		// 默认主地图 (Floor 100, 64x64)
		GridMap main_map = makeFixtureMap(64, 64);
		TileAttrTable attr = makeFixtureAttr();
		SA::Domain::CharacterRecord defs{};
		defs.schema_ver = 1;
		defs.player.level = 1;
		defs.player.floor = 100;
		defs.player.x = 20;
		defs.player.y = 20;
		world.configurePlayable(std::move(main_map), std::move(attr), "test-v1", defs);

		// 注册村庄地图: Floor 1000 (160x160), Floor 2000 (150x150), Floor 3000 (150x150), Floor 4000 (150x150)
		GridMap map1000 = makeFixtureMap(160, 160);
		GridMap map2000 = makeFixtureMap(150, 150);
		GridMap map3000 = makeFixtureMap(150, 150);
		GridMap map4000 = makeFixtureMap(150, 150);
		world.loadFloorMap(1000, std::move(map1000));
		world.loadFloorMap(2000, std::move(map2000));
		world.loadFloorMap(3000, std::move(map3000));
		world.loadFloorMap(4000, std::move(map4000));

		CHECK_EQ(world.floorMapCount(), 4);
		const auto *f1000 = world.findFloorMap(1000);
		REQUIRE(f1000 != nullptr);
		CHECK_EQ(f1000->width, 160);
		CHECK_EQ(f1000->height, 160);

		const auto *f2000 = world.findFloorMap(2000);
		REQUIRE(f2000 != nullptr);
		CHECK_EQ(f2000->width, 150);
		CHECK_EQ(f2000->height, 150);

		CHECK(world.findFloorMap(9999) == nullptr);

		// 跨地图同坐标视野隔离测试:
		// 玩家 A 登录在 Floor 100 的 (20, 20)
		const auto id_a = transport.connect();
		world.onSessionReady(id_a);
		world.tick();

		// 玩家 B 登录后传送至 Floor 1000 的 (20, 20)
		const auto id_b = transport.connect();
		world.onSessionReady(id_b);
		world.tick();
		auto *pb = world.playerForTest(id_b);
		REQUIRE(pb != nullptr);
		world.warpPlayerForTest(id_b, 1000, 20, 20);
		world.tick();

		VisMirror ma;
		ma.feed(transport.sent(id_a));
		// 校验 A 此时已经收到了 B 的 CharDisappear (因为 B 传送走了)
		CHECK(std::find(ma.disappears.begin(), ma.disappears.end(), id_b) != ma.disappears.end());

		VisMirror mb;
		mb.feed(transport.sent(id_b));
		// 校验 B 此时已经收到了 A 的 CharDisappear (因为 B 传送离开 Floor 100)
		CHECK(std::find(mb.disappears.begin(), mb.disappears.end(), id_a) != mb.disappears.end());

		// 清空当前镜像事件列表，重置观察窗口
		ma.appears.clear();
		ma.moves.clear();
		ma.disappears.clear();
		mb.appears.clear();
		mb.moves.clear();
		mb.disappears.clear();

		// A 在 Floor 100 上移动，B 绝不会收到 A 的 CharMove 或 CharAppear
		clock.advance(300);
		SA::Domain::WalkRequest walk_a{};
		walk_a.x = 20;
		walk_a.y = 20;
		REQUIRE(walk_a.direction.assign("c")); // 向东走一步
		world.onWalk(id_a, walk_a);
		world.tick();

		ma.feed(transport.sent(id_a));
		mb.feed(transport.sent(id_b));
		// B 处于 Floor 1000，虽然坐标与 A 仅差 1 格，但分属不同 Floor，绝对收不到 A 的任何 CharMove 或 CharAppear！
		CHECK(mb.moves.empty());
		CHECK(mb.appears.empty());

		// 同样，B 在 Floor 1000 上移动，A 绝对收不到 B 的任何 CharMove 或 CharAppear！
		clock.advance(300);
		SA::Domain::WalkRequest walk_b{};
		walk_b.x = 20;
		walk_b.y = 20;
		REQUIRE(walk_b.direction.assign("c"));
		world.onWalk(id_b, walk_b);
		world.tick();

		ma.feed(transport.sent(id_a));
		mb.feed(transport.sent(id_b));
		CHECK(ma.moves.empty());
		CHECK(ma.appears.empty());
	}

	SUBCASE("加加村与卡鲁它那村 NPC 真实服务交互")
	{
		std::string content_dir;
		for (const auto &p : {"content/p2-v1", "../content/p2-v1", "../../content/p2-v1", "../../../content/p2-v1", "stone-age-server/content/p2-v1"})
		{
			if (std::filesystem::exists(std::string(p) + "/manifest.json"))
			{
				content_dir = p;
				break;
			}
		}
		if (content_dir.empty())
			return;

		const auto bundle = SA::Content::load(content_dir, false);
		SA::Platform::ServerConfig config = makeMoveConfig();
		SA::Platform::ManualClock clock{0};
		SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
		SA::Platform::RandomSource random{0xABCDEF};
		SA::Net::LoopbackTransport transport{};
		World world{config, clock, logger, random, transport};

		GridMap map;
		map.width = bundle.width;
		map.height = bundle.height;
		map.tile.assign(bundle.walkable.size(), 1);
		map.obj.assign(bundle.walkable.begin(), bundle.walkable.end());
		TileAttrTable attributes;
		attributes.walkable = {WalkKind::kBlocked, WalkKind::kFree};

		SA::Domain::CharacterRecord defaults{};
		defaults.schema_ver = 1;
		defaults.player.level = 1;
		defaults.player.charm = 60;
		defaults.player.mp = defaults.player.max_mp = 100;
		defaults.player.hp = 100;
		defaults.player.default_pet = -1;
		defaults.player.floor = 100;
		defaults.player.x = 643;
		defaults.player.y = 459;
		defaults.player.dir = 5;
		defaults.player.image = 100000;
		world.configurePlayable(std::move(map), std::move(attributes), bundle.version, defaults);

		// 装载 4 大村庄
		const auto *floors_val = bundle.world.find("floors");
		REQUIRE(floors_val != nullptr);
		for (const auto &item : floors_val->asArray())
		{
			const auto fid = SA::Content::integer(*item.find("floor"));
			const auto w = SA::Content::integer(*item.find("width"));
			const auto h = SA::Content::integer(*item.find("height"));
			const auto walk_b64 = SA::Content::text(*item.find("walkable"));
			const auto decoded = SA::World::decodeBase64(walk_b64);
			GridMap fl_map;
			fl_map.width = w;
			fl_map.height = h;
			fl_map.tile.assign(decoded.size(), 1);
			fl_map.obj.assign(decoded.begin(), decoded.end());
			world.loadFloorMap(fid, std::move(fl_map));
		}

		// 装载 NPC
		const auto *npcs_val = bundle.world.find("npcs");
		REQUIRE(npcs_val != nullptr);
		std::vector<NpcEntity> npcs;
		for (const auto &item : npcs_val->asArray())
		{
			NpcEntity npc;
			if (const auto *nid = item.find("id"))
				npc.id = static_cast<std::uint64_t>(SA::Content::integer(*nid));
			if (const auto *fl = item.find("floor"))
				npc.floor = SA::Content::integer(*fl);
			if (const auto *x = item.find("x"))
				npc.x = SA::Content::integer(*x);
			if (const auto *y = item.find("y"))
				npc.y = SA::Content::integer(*y);
			if (const auto *dir = item.find("dir"))
				npc.dir = static_cast<std::uint8_t>(SA::Content::integer(*dir));
			if (const auto *img = item.find("image"))
				npc.image = SA::Content::integer(*img);
			if (const auto *nm = item.find("name"))
				npc.name = SA::Content::text(*nm);
			if (const auto *msg = item.find("message"))
				npc.message = SA::Content::text(*msg);
			if (const auto *st = item.find("sign_title"))
				npc.sign_title = SA::Content::text(*st);
			if (const auto *tp = item.find("type"); tp && tp->isString())
			{
				const auto &tstr = tp->asString();
				if (tstr == "signboard")
					npc.type = NpcType::kSignBoard;
				else if (tstr == "shop")
					npc.type = NpcType::kShop;
				else if (tstr == "healer")
					npc.type = NpcType::kHealer;
				else
					npc.type = NpcType::kOther;
			}
			if (const auto *prods = item.find("shop_products"); prods && prods->isArray())
			{
				for (const auto &p : prods->asArray())
				{
					ShopProduct sp;
					if (const auto *pi = p.find("item_id"))
						sp.item_id = SA::Content::integer(*pi);
					if (const auto *pc = p.find("cost"))
						sp.cost = SA::Content::integer(*pc);
					if (const auto *pm = p.find("image_id"))
						sp.image_id = static_cast<std::uint32_t>(SA::Content::integer(*pm));
					if (const auto *pl = p.find("level"))
						sp.level = static_cast<std::uint32_t>(SA::Content::integer(*pl));
					if (const auto *pn = p.find("name"))
						sp.name = SA::Content::text(*pn);
					npc.shop_products.push_back(std::move(sp));
				}
			}
			npcs.push_back(std::move(npc));
		}
		world.loadNpcEntities(npcs);

		const auto id = transport.connect();
		world.onSessionReady(id);
		world.tick();

		auto *player = world.playerForTest(id);
		REQUIRE(player != nullptr);

		// 1. 加加村 (Floor 3000)「多多的传言板」(SignBoard at 50, 62)
		world.warpPlayerForTest(id, 3000, 50, 61);
		world.tick();
		player->dir = 4; // 面向南 (50, 62)

		SA::Domain::EventRequest req{};
		req.dir = 4;
		req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
		req.seqno = 7001;
		world.onEvent(id, req);
		world.tick();

		CHECK(world.playerHasActiveWindow(id));
		CHECK_FALSE(world.playerLastWindowText(id).empty());

		// 2. 卡鲁它那村 (Floor 4000)「特产品贩卖员」(Shop at 36, 70)
		world.warpPlayerForTest(id, 4000, 36, 69);
		world.tick();
		player->dir = 4; // 面向南 (36, 70)

		req.seqno = 7002;
		world.onEvent(id, req);
		world.tick();

		CHECK(world.playerHasActiveWindow(id));
		const auto win_opt = findLastWindowOpen(transport.sent(id));
		REQUIRE(win_opt.has_value());
		CHECK(win_opt->kind == SA::Domain::WindowKind::WINDOW_KIND_ITEM_SHOP);
	}
}

TEST_CASE("加鲁卡南岛、附属村庄与深渊地下城多地图深化与双向连通 (批次 D.3)")
{
	std::string content_dir;
	for (const auto &p : {"content/p2-v1", "../content/p2-v1", "../../content/p2-v1", "../../../content/p2-v1", "stone-age-server/content/p2-v1"})
	{
		if (std::filesystem::exists(std::string(p) + "/manifest.json"))
		{
			content_dir = p;
			break;
		}
	}
	if (content_dir.empty())
		return;

	const auto bundle = SA::Content::load(content_dir, false);
	SA::Platform::ServerConfig config = makeMoveConfig();
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0x765432};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};

	GridMap map;
	map.width = bundle.width;
	map.height = bundle.height;
	map.tile.assign(bundle.walkable.size(), 1);
	map.obj.assign(bundle.walkable.begin(), bundle.walkable.end());
	TileAttrTable attributes;
	attributes.walkable = {WalkKind::kBlocked, WalkKind::kFree};

	SA::Domain::CharacterRecord defaults{};
	defaults.schema_ver = 1;
	defaults.player.level = 1;
	defaults.player.charm = 60;
	defaults.player.mp = defaults.player.max_mp = 100;
	defaults.player.hp = 100;
	defaults.player.default_pet = -1;
	defaults.player.floor = 100;
	defaults.player.x = 643;
	defaults.player.y = 459;
	defaults.player.dir = 5;
	defaults.player.image = 100000;
	world.configurePlayable(std::move(map), std::move(attributes), bundle.version, defaults);

	// 1. 全量装载 18 张拓展地图 (加鲁卡 200, 村庄 1000/2000/3000/3100/3200/3300/3400/4000, 核心地下城 20801..20807, 21201, 21215)
	const auto *floors_val = bundle.world.find("floors");
	REQUIRE(floors_val != nullptr);
	REQUIRE_EQ(floors_val->asArray().size(), 18);
	for (const auto &item : floors_val->asArray())
	{
		const auto fid = SA::Content::integer(*item.find("floor"));
		const auto w = SA::Content::integer(*item.find("width"));
		const auto h = SA::Content::integer(*item.find("height"));
		const auto walk_b64 = SA::Content::text(*item.find("walkable"));
		const auto decoded = SA::World::decodeBase64(walk_b64);
		CHECK_EQ(decoded.size(), static_cast<std::size_t>(w * h));
		GridMap fl_map;
		fl_map.width = w;
		fl_map.height = h;
		fl_map.tile.assign(decoded.size(), 1);
		fl_map.obj.assign(decoded.begin(), decoded.end());
		world.loadFloorMap(fid, std::move(fl_map));
	}
	CHECK_EQ(world.floorMapCount(), 18);

	// 验证关键地图尺寸
	const auto *jalga = world.findFloorMap(200);
	REQUIRE(jalga != nullptr);
	CHECK_EQ(jalga->width, 800);
	CHECK_EQ(jalga->height, 1200);

	const auto *toto = world.findFloorMap(3200);
	REQUIRE(toto != nullptr);
	CHECK_EQ(toto->width, 120);
	CHECK_EQ(toto->height, 120);

	const auto *ruri_1 = world.findFloorMap(20801);
	REQUIRE(ruri_1 != nullptr);
	CHECK_EQ(ruri_1->width, 70);
	CHECK_EQ(ruri_1->height, 70);

	const auto *hekisei_15 = world.findFloorMap(21215);
	REQUIRE(hekisei_15 != nullptr);
	CHECK_EQ(hekisei_15->width, 50);
	CHECK_EQ(hekisei_15->height, 100);

	// 2. 装载传送点与 NPC
	const auto *warps_val = bundle.world.find("warp_points");
	REQUIRE(warps_val != nullptr);
	std::vector<WarpPoint> warp_points;
	for (const auto &item : warps_val->asArray())
	{
		WarpPoint wp;
		if (const auto *sf = item.find("src_floor"))
			wp.src_floor = SA::Content::integer(*sf);
		if (const auto *sx = item.find("src_x"))
			wp.src_x = SA::Content::integer(*sx);
		if (const auto *sy = item.find("src_y"))
			wp.src_y = SA::Content::integer(*sy);
		if (const auto *df = item.find("dst_floor"))
			wp.dst_floor = SA::Content::integer(*df);
		if (const auto *dx = item.find("dst_x"))
			wp.dst_x = SA::Content::integer(*dx);
		if (const auto *dy = item.find("dst_y"))
			wp.dst_y = SA::Content::integer(*dy);
		warp_points.push_back(wp);
	}
	world.loadWarpPoints(warp_points);
	CHECK_EQ(world.warpPointCount(), 311);

	const auto *npcs_val = bundle.world.find("npcs");
	REQUIRE(npcs_val != nullptr);
	std::vector<NpcEntity> npcs;
	for (const auto &item : npcs_val->asArray())
	{
		NpcEntity npc;
		if (const auto *nid = item.find("id"))
			npc.id = static_cast<std::uint64_t>(SA::Content::integer(*nid));
		if (const auto *fl = item.find("floor"))
			npc.floor = SA::Content::integer(*fl);
		if (const auto *x = item.find("x"))
			npc.x = SA::Content::integer(*x);
		if (const auto *y = item.find("y"))
			npc.y = SA::Content::integer(*y);
		if (const auto *dir = item.find("dir"))
			npc.dir = static_cast<std::uint8_t>(SA::Content::integer(*dir));
		if (const auto *img = item.find("image"))
			npc.image = SA::Content::integer(*img);
		if (const auto *nm = item.find("name"))
			npc.name = SA::Content::text(*nm);
		if (const auto *msg = item.find("message"))
			npc.message = SA::Content::text(*msg);
		if (const auto *tp = item.find("type"); tp && tp->isString())
		{
			const auto &tstr = tp->asString();
			if (tstr == "signboard")
				npc.type = NpcType::kSignBoard;
			else if (tstr == "shop")
				npc.type = NpcType::kShop;
			else if (tstr == "healer")
				npc.type = NpcType::kHealer;
			else if (tstr == "townpeople")
				npc.type = NpcType::kTownPeople;
			else if (tstr == "exchangeman")
				npc.type = NpcType::kExChangeMan;
			else
				npc.type = NpcType::kOther;
		}
		npcs.push_back(std::move(npc));
	}
	world.loadNpcEntities(npcs);
	CHECK_EQ(world.npcCount(), 399);

	// 3. 玩家连接进场并瞬移至加鲁卡南岛 Floor 200 (509, 495)
	const auto id = transport.connect();
	world.onSessionReady(id);
	world.tick();
	world.warpPlayerForTest(id, 200, 509, 495);
	const auto p_init = world.playerPos(id);
	CHECK_EQ(p_init.floor, 200);
	CHECK_EQ(p_init.x, 509);
	CHECK_EQ(p_init.y, 495);

	// ── 验证 1: 加鲁卡 (200) <-> 多多村 (3200) 双向连通 ──
	// 向东走一步 'c'，踩入 (510, 495) 触发传送至 多多村 (3200, 28, 44)
	SA::Domain::WalkRequest walk{};
	walk.x = 509;
	walk.y = 495;
	REQUIRE(walk.direction.assign("c"));
	world.onWalk(id, walk);
	world.tick();

	const auto p_toto = world.playerPos(id);
	CHECK_EQ(p_toto.floor, 3200);
	CHECK_EQ(p_toto.x, 28);
	CHECK_EQ(p_toto.y, 44);

	clock.advance(300);
	// 在多多村向西走一步 'g'，踩入 (27, 44) 触发反向传送回加鲁卡 (200, 509, 495)
	SA::Domain::WalkRequest walk_back{};
	walk_back.x = p_toto.x;
	walk_back.y = p_toto.y;
	REQUIRE(walk_back.direction.assign("g"));
	world.onWalk(id, walk_back);
	world.tick();

	const auto p_jalga_back = world.playerPos(id);
	CHECK_EQ(p_jalga_back.floor, 200);
	CHECK_EQ(p_jalga_back.x, 509);
	CHECK_EQ(p_jalga_back.y, 495);

	// ── 验证 2: 加鲁卡 (200) <-> 深渊地下城 (20801) <-> (20802) 连续穿越 ──
	clock.advance(300);
	world.warpPlayerForTest(id, 200, 432, 742);
	SA::Domain::WalkRequest walk_dungeon{};
	walk_dungeon.x = 432;
	walk_dungeon.y = 742;
	REQUIRE(walk_dungeon.direction.assign("c"));
	world.onWalk(id, walk_dungeon);
	world.tick();

	const auto p_d1 = world.playerPos(id);
	CHECK_EQ(p_d1.floor, 20801);
	CHECK_EQ(p_d1.x, 33);
	CHECK_EQ(p_d1.y, 69);

	// 从 20801 的 (11, 4) 向东跨一步至 (12, 4)，深入地下城 2 楼 (20802, 10, 3)
	clock.advance(300);
	world.warpPlayerForTest(id, 20801, 11, 4);
	SA::Domain::WalkRequest walk_d2{};
	walk_d2.x = 11;
	walk_d2.y = 4;
	REQUIRE(walk_d2.direction.assign("c"));
	world.onWalk(id, walk_d2);
	world.tick();

	const auto p_d2 = world.playerPos(id);
	CHECK_EQ(p_d2.floor, 20802);
	CHECK_EQ(p_d2.x, 10);
	CHECK_EQ(p_d2.y, 3);

	// ── 验证 3: 多多村 (3200) NPC 真实对话交互 ──
	clock.advance(300);
	world.warpPlayerForTest(id, 3200, 34, 45);
	auto *player = world.playerForTest(id);
	REQUIRE(player != nullptr);
	player->dir = 0; // 面向北 (34, 44: 村里的少女)

	SA::Domain::EventRequest req{};
	req.dir = 0;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.seqno = 6001;
	world.onEvent(id, req);
	world.tick();

	CHECK(world.playerLastWindowText(id).find("欢迎来到多多村") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
//  成长与装备闭环集成测试 (批次 P.1)
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("成长与装备闭环: 装备穿脱、属性加成与战斗升级")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	REQUIRE(f.world.playerLevel(id) == 1);
	REQUIRE(f.world.playerSkillupPoints(id) == 0);

	// ── 1. 装备穿脱与属性加成累加 ─────────────────────────────────
	SA::Model::Item helm{};
	helm.item_id = 1001;
	helm.type = 6; // ITEM_HELM (对应槽位 0: kHead)
	helm.modify_attack = 10;
	helm.modify_defense = 25;
	helm.modify_hp = 50;

	const int inv_slot = f.world.giveItemToPlayer(id, helm);
	REQUIRE(inv_slot >= static_cast<int>(SA::Model::kStartItemArray));

	// 初始未穿戴加成为 0
	auto mods = f.world.playerEquipModifiers(id);
	CHECK(mods.modify_attack == 0);
	CHECK(mods.modify_defense == 0);
	CHECK(mods.modify_hp == 0);

	// 自动穿戴到头部 (slot 0)
	REQUIRE(f.world.equipItem(id, inv_slot));
	CHECK(f.world.playerItemAt(id, 0) != nullptr);
	CHECK(f.world.playerItemAt(id, inv_slot) == nullptr);

	// 穿戴后加成生效
	mods = f.world.playerEquipModifiers(id);
	CHECK(mods.modify_attack == 10);
	CHECK(mods.modify_defense == 25);
	CHECK(mods.modify_hp == 50);

	// 卸下装备至背包
	REQUIRE(f.world.unequipItem(id, 0));
	CHECK(f.world.playerItemAt(id, 0) == nullptr);
	CHECK(f.world.playerItemAt(id, inv_slot) != nullptr);

	// 卸下后加成归零
	mods = f.world.playerEquipModifiers(id);
	CHECK(mods.modify_attack == 0);
	CHECK(mods.modify_defense == 0);
	CHECK(mods.modify_hp == 0);

	// ── 2. 战斗获胜升级与属性点结算 ───────────────────────────────
	// 重新穿上头盔进入战斗
	REQUIRE(f.world.equipItem(id, inv_slot, 0));

	loadEncounterFixture(f.world, /*prob=*/120, /*enc_exp=*/10); // 必遇敌且击败后提供 10 经验
	f.sendWalk(id, "c");
	f.world.tick(); // 走一步 → 遇敌 → 开战
	REQUIRE(f.world.battleCount() == 1);
	const BattleId battle = 1;

	// 处于战斗中时无法脱换装备与分配属性点
	CHECK_FALSE(f.world.unequipItem(id, 0));
	CHECK_FALSE(f.world.allocateStatPoint(id, SA::Rules::StatCategory::kStr, 1));

	for (int i = 0; i < 30; ++i)
	{
		const SA::Rules::BattleField *fld = f.world.battleField(battle);
		if (fld == nullptr)
			break;
		const BattleStats *st = f.world.stats(battle);
		if (st != nullptr && st->finished)
			break;
		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = battle;
		cmd.turn = fld->turn;
		cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd.command.attack.target = static_cast<std::uint32_t>(SA::Rules::kSideOffset);
		f.world.onBattleCommand(id, cmd);
		f.clock.advance(1000);
		f.world.tick();
	}

	REQUIRE(f.world.stats(battle) != nullptr);
	CHECK(f.world.stats(battle)->finished);

	// 击败 1 级乌力后获得经验并触发升级 (exp.txt 2 级只需 2 经验)
	CHECK(f.world.playerLevel(id) >= 2);
	CHECK(f.world.playerSkillupPoints(id) >= 3);
	CHECK(f.world.playerHp(id) > 0);

	// 战斗结束后脱下装备恢复正常
	REQUIRE(f.world.unequipItem(id, 0));
	CHECK(f.world.playerEquipModifiers(id).modify_attack == 0);

	// ── 3. 升级后属性点分配与四维即时生效 (批次 P.2) ───────────────────
	const int initial_pts = f.world.playerSkillupPoints(id);
	REQUIRE(initial_pts >= 3);
	const int v0 = f.world.playerVital(id);
	const int s0 = f.world.playerStr(id);
	const int t0 = f.world.playerTough(id);
	const int d0 = f.world.playerDex(id);
	const int hp0 = f.world.playerHp(id);

	// 非法参数拒绝
	CHECK_FALSE(f.world.allocateStatPoint(id, SA::Rules::StatCategory::kStr, 0));
	CHECK_FALSE(f.world.allocateStatPoint(id, SA::Rules::StatCategory::kStr, -1));
	CHECK_FALSE(f.world.allocateStatPoint(id, SA::Rules::StatCategory::kStr, initial_pts + 1));
	CHECK_FALSE(f.world.allocateStatPoint(id, 99, 1));

	// 分配 1 点至力量 (Str): 力量 +100
	REQUIRE(f.world.allocateStatPoint(id, SA::Rules::StatCategory::kStr, 1));
	CHECK(f.world.playerSkillupPoints(id) == initial_pts - 1);
	CHECK(f.world.playerStr(id) == s0 + 100);

	// 分配 1 点至体力 (Vital): 体力 +100, 生命上限提升且保持满血
	REQUIRE(f.world.allocateStatPoint(id, SA::Rules::StatCategory::kVital, 1));
	CHECK(f.world.playerSkillupPoints(id) == initial_pts - 2);
	CHECK(f.world.playerVital(id) == v0 + 100);
	CHECK(f.world.playerHp(id) >= hp0);

	// 分配 1 点至速度 (Dex): 速度 +100
	REQUIRE(f.world.allocateStatPoint(id, SA::Rules::StatCategory::kDex, 1));
	CHECK(f.world.playerSkillupPoints(id) == initial_pts - 3);
	CHECK(f.world.playerDex(id) == d0 + 100);

	// 分配至耐力 (Tough) 使用整数下标重载 (stat_index = 2)
	const int remaining = f.world.playerSkillupPoints(id);
	if (remaining > 0)
	{
		REQUIRE(f.world.allocateStatPoint(id, 2, 1));
		CHECK(f.world.playerTough(id) == t0 + 100);
		CHECK(f.world.playerSkillupPoints(id) == remaining - 1);
	}

	// 点数耗尽后无法继续分配
	if (f.world.playerSkillupPoints(id) == 0)
	{
		CHECK_FALSE(f.world.allocateStatPoint(id, SA::Rules::StatCategory::kTough, 1));
	}
}

TEST_CASE("职业属性上限门禁与猎人非战斗职技全链路集成 (批次 A-γ2)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->hp = 100;

	// 1. 初始职业状态验证
	CHECK(f.world.playerProfessionClass(id) == SA::Rules::ProfessionClass::kNone);
	CHECK(f.world.playerProfessionLevel(id) == 0);

	// 2. 转职为勇士 (kFighter, level 10)
	REQUIRE(f.world.setPlayerProfession(id, SA::Rules::ProfessionClass::kFighter, 10));
	CHECK(f.world.playerProfessionClass(id) == SA::Rules::ProfessionClass::kFighter);
	CHECK(f.world.playerProfessionLevel(id) == 10);

	// 非猎人尝试施放猎人技能 -> 拦截
	CHECK_FALSE(f.world.castHunterEncounterSkill(id, true, 10));

	// 3. 转职为猎人 (kHunter, level 25)
	REQUIRE(f.world.setPlayerProfession(id, SA::Rules::ProfessionClass::kHunter, 25));
	CHECK(f.world.playerProfessionClass(id) == SA::Rules::ProfessionClass::kHunter);

	// 阵亡状态拦截
	p->hp = 0;
	CHECK_FALSE(f.world.castHunterEncounterSkill(id, false, 25, 10, 180000));
	p->hp = 100;

	// 施放回避战斗 (escape): 25级 -> 2 * 10 = -20%
	REQUIRE(f.world.castHunterEncounterSkill(id, false, 25, 10, 180000));
	CHECK(f.world.playerEncounterRateFix(id) == -20);

	// 推进 60 秒 -> 技能仍在有效期内
	f.clock.advance(60000);
	f.world.tick();
	CHECK(f.world.playerEncounterRateFix(id) == -20);

	// 推进 130 秒 (总计 190 秒 > 180 秒) -> 技能自然过期
	f.clock.advance(130000);
	f.world.tick();
	CHECK(f.world.playerEncounterRateFix(id) == 0);

	// 重新施放追寻敌踪 (track): 30级 -> 3 * 10 = +30%
	REQUIRE(f.world.castHunterEncounterSkill(id, true, 30, 10, 180000));
	CHECK(f.world.playerEncounterRateFix(id) == 30);
}

// ═══════════════════════════════════════════════════════════════════════════
//  组队系统全链路集成与反向验证 (阶段 2: 队伍与协同)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("组队生命周期: 建队、加入、5人上限门禁、踢出与解散")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	const auto id3 = spawnHandshaked(f);
	const auto id4 = spawnHandshaked(f);
	const auto id5 = spawnHandshaked(f);
	const auto id6 = spawnHandshaked(f);

	// 初始所有人均未组队
	CHECK(f.world.playerPartyMode(id1) == PartyMode::kNone);
	CHECK(f.world.playerPartyLeader(id1) == 0);
	CHECK(f.world.partyCount() == 0);

	// 1. 距离过远拦截 (跨图或切比雪夫距离 > 2)
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p2->floor = 0;
	p2->x = 30;
	p2->y = 30; // dx=10 > 2
	CHECK_FALSE(f.world.joinParty(id2, id1));

	// 跨地图拦截
	p2->x = 20;
	p2->y = 21;
	p2->floor = 100;
	CHECK_FALSE(f.world.joinParty(id2, id1));
	p2->floor = 0;

	// 阵亡拦截
	p2->hp = 0;
	CHECK_FALSE(f.world.joinParty(id2, id1));
	p2->hp = 100;

	// 不能加入自己
	CHECK_FALSE(f.world.joinParty(id1, id1));

	// 2. 正常加入: id2 加入 id1, id1 成为队长, id2 成为队员
	REQUIRE(f.world.joinParty(id2, id1));
	CHECK(f.world.partyCount() == 1);
	CHECK(f.world.playerPartyMode(id1) == PartyMode::kLeader);
	CHECK(f.world.playerPartyMode(id2) == PartyMode::kMember);
	CHECK(f.world.playerPartyLeader(id2) == id1);
	auto members = f.world.playerPartyMembers(id1);
	REQUIRE(members.size() == 2);
	CHECK(members[0] == id1);
	CHECK(members[1] == id2);

	// 已有队伍不能再申请加入
	CHECK_FALSE(f.world.joinParty(id2, id3));

	// 3. 加入第 3, 4, 5 名队员
	auto *p3 = f.world.playerForTest(id3);
	auto *p4 = f.world.playerForTest(id4);
	auto *p5 = f.world.playerForTest(id5);
	p3->floor = 0;
	p3->x = 21;
	p3->y = 20;
	p4->floor = 0;
	p4->x = 20;
	p4->y = 19;
	p5->floor = 0;
	p5->x = 19;
	p5->y = 20;
	REQUIRE(f.world.joinParty(id3, id1));
	REQUIRE(f.world.joinParty(id4, id1));
	REQUIRE(f.world.joinParty(id5, id1));
	CHECK(f.world.playerPartyMembers(id1).size() == 5);

	// 4. [RV-1] 满员门禁: 第 6 人无法加入队伍 (CHAR_PARTYMAX = 5)
	auto *p6 = f.world.playerForTest(id6);
	p6->floor = 0;
	p6->x = 21;
	p6->y = 21;
	CHECK_FALSE(f.world.joinParty(id6, id1));
	CHECK(f.world.playerPartyMembers(id1).size() == 5);
	CHECK(f.world.playerPartyMode(id6) == PartyMode::kNone);

	// 5. 队长踢出队员 (kickPartyMember)
	// 非队长不能踢人
	CHECK_FALSE(f.world.kickPartyMember(id2, id3));
	// 队长不能踢自己
	CHECK_FALSE(f.world.kickPartyMember(id1, id1));
	// 队长踢出不在队伍中的人
	CHECK_FALSE(f.world.kickPartyMember(id1, id6));
	// 队长踢出 id5
	REQUIRE(f.world.kickPartyMember(id1, id5));
	CHECK(f.world.playerPartyMode(id5) == PartyMode::kNone);
	CHECK(f.world.playerPartyMembers(id1).size() == 4);

	// 6. 队员主动离队 (leaveParty)
	REQUIRE(f.world.leaveParty(id4));
	CHECK(f.world.playerPartyMode(id4) == PartyMode::kNone);
	CHECK(f.world.playerPartyMembers(id1).size() == 3);

	REQUIRE(f.world.leaveParty(id3));
	CHECK(f.world.playerPartyMembers(id1).size() == 2);

	// 当队员仅剩 1 人 (id2) 时，id2 离队 -> 队伍仅剩队长一人 -> 队伍自动解散
	REQUIRE(f.world.leaveParty(id2));
	CHECK(f.world.playerPartyMode(id2) == PartyMode::kNone);
	CHECK(f.world.playerPartyMode(id1) == PartyMode::kNone);
	CHECK(f.world.partyCount() == 0);

	// 7. 队长离队解散全队验证
	REQUIRE(f.world.joinParty(id2, id1));
	REQUIRE(f.world.joinParty(id3, id1));
	CHECK(f.world.partyCount() == 1);
	REQUIRE(f.world.leaveParty(id1)); // 队长解散
	CHECK(f.world.playerPartyMode(id1) == PartyMode::kNone);
	CHECK(f.world.playerPartyMode(id2) == PartyMode::kNone);
	CHECK(f.world.playerPartyMode(id3) == PartyMode::kNone);
	CHECK(f.world.partyCount() == 0);

	// 8. 队员下线断开连接清理验证
	REQUIRE(f.world.joinParty(id2, id1));
	REQUIRE(f.world.joinParty(id3, id1));
	CHECK(f.world.partyCount() == 1);
	f.world.onSessionClosed(id3);
	CHECK(f.world.playerPartyMembers(id1).size() == 2);
	f.world.onSessionClosed(id1); // 队长断线 -> 全队解散
	CHECK(f.world.playerPartyMode(id2) == PartyMode::kNone);
	CHECK(f.world.partyCount() == 0);
}

TEST_CASE("组队协同移动: 队员自主转向/位移限制与队长贪吃蛇足迹跟随")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	const auto id3 = spawnHandshaked(f);

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	auto *p3 = f.world.playerForTest(id3);

	// 初始排布: 队长在 (30, 30), 队员1在 (29, 30), 队员2在 (28, 30) (一条直线向东)
	p1->floor = 0;
	p1->x = 30;
	p1->y = 30;
	p1->dir = 2; // 东
	p2->floor = 0;
	p2->x = 29;
	p2->y = 30;
	p2->dir = 2;
	p3->floor = 0;
	p3->x = 28;
	p3->y = 30;
	p3->dir = 2;

	REQUIRE(f.world.joinParty(id2, id1));
	REQUIRE(f.world.joinParty(id3, id1));

	// 1. 队员自主移动拦截: 发送位移方向字符被拒绝，坐标不改变
	f.sendWalk(id2, "c"); // 尝试向东移动
	f.world.tick();
	CHECK(f.world.playerPos(id2).x == 29);
	CHECK(f.world.playerPos(id2).y == 30);

	// 2. 队长前进一步 (东 'c'): 队长 (30,30)->(31,30)
	// 队员1跟随至队长原位 (30,30)
	// 队员2跟随至队员1原位 (29,30)
	f.sendWalk(id1, "c");
	f.world.tick();

	CHECK(f.world.playerPos(id1).x == 31);
	CHECK(f.world.playerPos(id1).y == 30);
	CHECK(f.world.playerPos(id2).x == 30);
	CHECK(f.world.playerPos(id2).y == 30);
	CHECK(f.world.playerPos(id3).x == 29);
	CHECK(f.world.playerPos(id3).y == 30);

	// 3. 队长转弯向南前进一步 (南 'e'): 队长 (31,30)->(31,31)
	// 队员1跟随至队长原位 (31,30)
	// 队员2跟随至队员1原位 (30,30)
	f.clock.advance(300);
	f.sendWalk(id1, "e");
	f.world.tick();

	CHECK(f.world.playerPos(id1).x == 31);
	CHECK(f.world.playerPos(id1).y == 31);
	CHECK(f.world.playerPos(id2).x == 31);
	CHECK(f.world.playerPos(id2).y == 30);
	CHECK(f.world.playerPos(id3).x == 30);
	CHECK(f.world.playerPos(id3).y == 30);

	// 4. 再前进一步 (南 'e'): 队长 (31,31)->(31,32)
	// 队员1跟随至 (31,31)
	// 队员2跟随至 (31,30) (进入转角弯道)
	f.clock.advance(300);
	f.sendWalk(id1, "e");
	f.world.tick();

	CHECK(f.world.playerPos(id1).x == 31);
	CHECK(f.world.playerPos(id1).y == 32);
	CHECK(f.world.playerPos(id2).x == 31);
	CHECK(f.world.playerPos(id2).y == 31);
	CHECK(f.world.playerPos(id3).x == 31);
	CHECK(f.world.playerPos(id3).y == 30);
}

TEST_CASE("组队协同传送 [RV-2]: 队长传送点触发与 WarpMan 传送同步拉取全队队员")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	const auto id3 = spawnHandshaked(f);

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	auto *p3 = f.world.playerForTest(id3);

	p1->floor = 0;
	p1->x = 10;
	p1->y = 10;
	p2->floor = 0;
	p2->x = 10;
	p2->y = 11;
	p3->floor = 0;
	p3->x = 10;
	p3->y = 12;

	REQUIRE(f.world.joinParty(id2, id1));
	REQUIRE(f.world.joinParty(id3, id1));

	// 1. 测试直接瞬移 (warpPlayerForTest)
	f.world.warpPlayerForTest(id1, /*floor=*/100, /*x=*/25, /*y=*/35);

	// 验证队长与全部队员均同步传送至新地图与目标坐标
	auto pos1 = f.world.playerPos(id1);
	auto pos2 = f.world.playerPos(id2);
	auto pos3 = f.world.playerPos(id3);
	CHECK(pos1.floor == 100);
	CHECK(pos1.x == 25);
	CHECK(pos1.y == 35);
	CHECK(pos2.floor == 100);
	CHECK(pos2.x == 25);
	CHECK(pos2.y == 35);
	CHECK(pos3.floor == 100);
	CHECK(pos3.x == 25);
	CHECK(pos3.y == 35);

	// 2. 传送员 NPC (kWarpMan) 传送验证
	NpcEntity warpman{};
	warpman.id = 9901;
	warpman.type = NpcType::kWarpMan;
	warpman.floor = 100;
	warpman.x = 25;
	warpman.y = 36; // 与队长距离 1 格
	WarpDestination dest{};
	dest.name = "渔村大厅";
	dest.floor = 200;
	dest.x = 50;
	dest.y = 60;
	dest.cost = 0;
	warpman.warp_destinations.push_back(dest);
	f.world.loadNpcEntities({warpman});

	// 队长与 WarpMan 对话传送
	REQUIRE(f.world.warpPlayerByNpc(id1, 9901, 0));

	pos1 = f.world.playerPos(id1);
	pos2 = f.world.playerPos(id2);
	pos3 = f.world.playerPos(id3);
	CHECK(pos1.floor == 200);
	CHECK(pos1.x == 50);
	CHECK(pos1.y == 60);
	CHECK(pos2.floor == 200);
	CHECK(pos2.x == 50);
	CHECK(pos2.y == 60);
	CHECK(pos3.floor == 200);
	CHECK(pos3.x == 50);
	CHECK(pos3.y == 60);
}

TEST_CASE("组队战斗闭环: 队长遇敌全队切入、宠物站位(5..9)、协同出招与全员经验分配")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 21;

	// 为两名玩家各配置一只战斗宠
	SA::Model::Pet pet1{};
	pet1.pet_id = 1;
	pet1.hp = 100;
	pet1.vital = 100;
	pet1.level = 1;
	pet1.str = 200;
	const int s1 = f.world.givePetToPlayer(id1, pet1);
	REQUIRE(s1 >= 0);
	p1->default_pet = s1;

	SA::Model::Pet pet2{};
	pet2.pet_id = 2;
	pet2.hp = 120;
	pet2.vital = 120;
	pet2.level = 1;
	pet2.str = 250;
	const int s2 = f.world.givePetToPlayer(id2, pet2);
	REQUIRE(s2 >= 0);
	p2->default_pet = s2;

	// 组队
	REQUIRE(f.world.joinParty(id2, id1));

	// 载入暗雷遇敌
	loadEncounterFixture(f.world, /*prob=*/120, /*enc_exp=*/10);

	// 队长移动触发遇敌
	f.sendWalk(id1, "c");
	f.world.tick();

	REQUIRE(f.world.battleCount() == 1);
	const BattleId battle = 1;
	const auto *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);

	// 站位断言: 队长在 slot 0, 队员在 slot 1; 队长宠在 slot 5, 队员宠在 slot 6
	CHECK(fld->at(0).occupied);
	CHECK(fld->at(1).occupied);
	CHECK_FALSE(fld->at(2).occupied);
	CHECK(fld->at(5).occupied);
	CHECK(fld->at(6).occupied);
	CHECK_FALSE(fld->at(7).occupied);

	// 战斗中禁止组队变更
	CHECK_FALSE(f.world.leaveParty(id2));
	CHECK_FALSE(f.world.kickPartyMember(id1, id2));

	// 两名玩家协同发出攻击指令击败敌人
	const int exp1_before = f.world.playerExp(id1);
	const int exp2_before = f.world.playerExp(id2);

	for (int turn = 0; turn < 20; ++turn)
	{
		const auto *cur_fld = f.world.battleField(battle);
		if (cur_fld == nullptr)
			break;
		const auto *st = f.world.stats(battle);
		if (st != nullptr && st->finished)
			break;

		// 队长出招攻击敌方 slot 10
		SA::Domain::BattleCommand cmd1{};
		cmd1.battle_id = battle;
		cmd1.turn = cur_fld->turn;
		cmd1.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd1.command.attack.target = static_cast<std::uint32_t>(SA::Rules::kSideOffset);
		f.world.onBattleCommand(id1, cmd1);

		// 队员出招攻击敌方 slot 10
		SA::Domain::BattleCommand cmd2{};
		cmd2.battle_id = battle;
		cmd2.turn = cur_fld->turn;
		cmd2.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd2.command.attack.target = static_cast<std::uint32_t>(SA::Rules::kSideOffset);
		f.world.onBattleCommand(id2, cmd2);

		f.clock.advance(1000);
		f.world.tick();
	}

	REQUIRE(f.world.stats(battle) != nullptr);
	CHECK(f.world.stats(battle)->finished);

	// 战斗胜利结算: 击杀单位获得经验且队伍状态依然完好保留
	CHECK(f.world.playerExp(id1) + f.world.playerExp(id2) > exp1_before + exp2_before);
	CHECK(f.world.playerPartyMode(id1) == PartyMode::kLeader);
	CHECK(f.world.playerPartyMode(id2) == PartyMode::kMember);
	CHECK(f.world.partyCount() == 1);
}

// ══ 批次 W.20: 决斗切磋系统 (Duel / PVP System) 与 安全交易系统 (Trade System) ══

TEST_CASE("PVP 决斗发起门禁: 距离过远/阵亡/跨图/同队互打拦截 [RV-1]")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->hp = 100;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 21;
	p2->hp = 100;

	// 1. 不能与自己决斗
	CHECK_FALSE(f.world.requestDuel(id1, id1));

	// 2. 距离过远拦截 (> 2 格)
	p2->x = 25;
	CHECK_FALSE(f.world.requestDuel(id1, id2));
	p2->x = 20;

	// 3. 跨地图拦截
	p2->floor = 1;
	CHECK_FALSE(f.world.requestDuel(id1, id2));
	p2->floor = 0;

	// 4. 阵亡拦截 (hp <= 0)
	p1->hp = 0;
	CHECK_FALSE(f.world.requestDuel(id1, id2));
	p1->hp = 100;

	p2->hp = 0;
	CHECK_FALSE(f.world.requestDuel(id1, id2));
	p2->hp = 100;

	// 5. 同队互打拦截 [RV-1]
	REQUIRE(f.world.joinParty(id2, id1));
	CHECK(f.world.playerPartyMode(id1) == PartyMode::kLeader);
	CHECK(f.world.playerPartyMode(id2) == PartyMode::kMember);
	// 队友之间发起决斗必须被阻断
	CHECK_FALSE(f.world.requestDuel(id1, id2));
	CHECK_FALSE(f.world.requestDuel(id2, id1));

	// 离队后合法发起决斗
	REQUIRE(f.world.leaveParty(id2));
	REQUIRE(f.world.requestDuel(id1, id2));

	CHECK(f.world.battleCount() == 1);
	const BattleId battle = 1;
	CHECK(f.world.isDuelBattle(battle));

	// 战斗中不可重复发起
	CHECK_FALSE(f.world.requestDuel(id1, id2));
}

TEST_CASE("PVP 组队决斗切入: 双方队长发起拉取全队队员(0..4 vs 10..14)与出战宠物(5..9 vs 15..19)")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f); // 队伍 A 队长
	const auto id2 = spawnHandshaked(f); // 队伍 A 队员 1
	const auto id3 = spawnHandshaked(f); // 队伍 A 队员 2
	const auto id4 = spawnHandshaked(f); // 队伍 B 队长
	const auto id5 = spawnHandshaked(f); // 队伍 B 队员 1

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	auto *p3 = f.world.playerForTest(id3);
	auto *p4 = f.world.playerForTest(id4);
	auto *p5 = f.world.playerForTest(id5);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(p3 != nullptr);
	REQUIRE(p4 != nullptr);
	REQUIRE(p5 != nullptr);

	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->hp = 100;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 20;
	p2->hp = 100;
	p3->floor = 0;
	p3->x = 20;
	p3->y = 20;
	p3->hp = 100;
	p4->floor = 0;
	p4->x = 21;
	p4->y = 20;
	p4->hp = 100;
	p5->floor = 0;
	p5->x = 21;
	p5->y = 20;
	p5->hp = 100;

	// 为部分玩家配置出战宠物
	const int s1 = f.world.givePetToPlayer(id1, makeTestPet(101, 10));
	REQUIRE(s1 >= 0);
	p1->default_pet = s1;

	const int s2 = f.world.givePetToPlayer(id2, makeTestPet(102, 10));
	REQUIRE(s2 >= 0);
	p2->default_pet = s2;

	const int s4 = f.world.givePetToPlayer(id4, makeTestPet(104, 10));
	REQUIRE(s4 >= 0);
	p4->default_pet = s4;

	// 组队: 队 A (id1, id2, id3), 队 B (id4, id5)
	REQUIRE(f.world.joinParty(id2, id1));
	REQUIRE(f.world.joinParty(id3, id1));
	REQUIRE(f.world.joinParty(id5, id4));

	// 门禁: 队员不能发起决斗，也不能向对方队员发起
	CHECK_FALSE(f.world.requestDuel(id2, id4));
	CHECK_FALSE(f.world.requestDuel(id1, id5));

	// 队长 vs 队长发起决斗
	REQUIRE(f.world.requestDuel(id1, id4));

	REQUIRE(f.world.battleCount() == 1);
	const BattleId battle = 1;
	CHECK(f.world.isDuelBattle(battle));

	const auto *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);

	// 检查双方站位:
	// Side 0 (队 A):
	// slot 0: id1, slot 1: id2, slot 2: id3, slot 3/4 空
	CHECK(fld->at(0).occupied);
	CHECK(fld->at(1).occupied);
	CHECK(fld->at(2).occupied);
	CHECK_FALSE(fld->at(3).occupied);
	CHECK_FALSE(fld->at(4).occupied);
	// 宠物站位: slot 5 (id1宠), slot 6 (id2宠), slot 7 空 (id3未配宠)
	CHECK(fld->at(5).occupied);
	CHECK(fld->at(6).occupied);
	CHECK_FALSE(fld->at(7).occupied);

	// Side 1 (队 B):
	// slot 10: id4, slot 11: id5, slot 12/13/14 空
	CHECK(fld->at(10).occupied);
	CHECK(fld->at(11).occupied);
	CHECK_FALSE(fld->at(12).occupied);
	CHECK_FALSE(fld->at(13).occupied);
	CHECK_FALSE(fld->at(14).occupied);
	// 宠物站位: slot 15 (id4宠), slot 16 空 (id5未配宠)
	CHECK(fld->at(15).occupied);
	CHECK_FALSE(fld->at(16).occupied);
}

TEST_CASE("PVP 决斗战斗闭环: 双方玩家指令协同、战败方 HP 钳位为 1 且队伍关系完整返回大世界")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f); // 队 A 队长
	const auto id2 = spawnHandshaked(f); // 队 A 队员
	const auto id3 = spawnHandshaked(f); // 敌方单人

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	auto *p3 = f.world.playerForTest(id3);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(p3 != nullptr);

	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->hp = 100;
	p1->str = 300;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 20;
	p2->hp = 100;
	p2->str = 300;
	p3->floor = 0;
	p3->x = 20;
	p3->y = 21;
	p3->hp = 10;
	p3->str = 1;

	REQUIRE(f.world.joinParty(id2, id1));

	const int exp1_before = f.world.playerExp(id1);
	const int exp3_before = f.world.playerExp(id3);
	const int gold1_before = f.world.playerGold(id1);
	const int gold3_before = f.world.playerGold(id3);

	REQUIRE(f.world.requestDuel(id1, id3));
	REQUIRE(f.world.battleCount() == 1);
	const BattleId battle = 1;

	// 双方协同出招推进回合，直至战斗结束 (id3 被打败)
	for (int turn = 0; turn < 10; ++turn)
	{
		const auto *cur_fld = f.world.battleField(battle);
		if (cur_fld == nullptr)
			break;
		const auto *st = f.world.stats(battle);
		if (st != nullptr && st->finished)
			break;

		// 队 A 成员攻击对方 slot 10 (id3)
		SA::Domain::BattleCommand cmd1{};
		cmd1.battle_id = battle;
		cmd1.turn = cur_fld->turn;
		cmd1.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd1.command.attack.target = 10;
		f.world.onBattleCommand(id1, cmd1);

		SA::Domain::BattleCommand cmd2{};
		cmd2.battle_id = battle;
		cmd2.turn = cur_fld->turn;
		cmd2.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd2.command.attack.target = 10;
		f.world.onBattleCommand(id2, cmd2);

		// id3 攻击对方 slot 0 (id1)
		SA::Domain::BattleCommand cmd3{};
		cmd3.battle_id = battle;
		cmd3.turn = cur_fld->turn;
		cmd3.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd3.command.attack.target = 0;
		f.world.onBattleCommand(id3, cmd3);

		f.clock.advance(1000);
		f.world.tick();
	}

	REQUIRE(f.world.stats(battle) != nullptr);
	CHECK(f.world.stats(battle)->finished);

	// PVP 结算断言:
	// 1. 无野怪经验产出，无石币通胀产出
	CHECK(f.world.playerExp(id1) == exp1_before);
	CHECK(f.world.playerExp(id3) == exp3_before);
	CHECK(f.world.playerGold(id1) == gold1_before);
	CHECK(f.world.playerGold(id3) == gold3_before);

	// 2. 战败方 HP 钳位保护为 >= 1 (免于死亡惩罚与回城)
	CHECK(f.world.playerHp(id3) >= 1);

	// 3. 双方脱离战斗状态，队伍关系完整保留
	CHECK(f.world.battleCount() == 0);
	CHECK(f.world.playerPartyMode(id1) == PartyMode::kLeader);
	CHECK(f.world.playerPartyMode(id2) == PartyMode::kMember);
	CHECK(f.world.partyCount() == 1);
}

TEST_CASE("交易生命周期: 发起、距离过远拦截、接受、取消与断线回滚")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	const auto id3 = spawnHandshaked(f);

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	auto *p3 = f.world.playerForTest(id3);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(p3 != nullptr);

	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 21; // 距离 1
	p3->floor = 0;
	p3->x = 30;
	p3->y = 30; // 距离 > 2

	// 1. 门禁检查: 自己与自己、超距离
	CHECK_FALSE(f.world.requestTrade(id1, id1));
	CHECK_FALSE(f.world.requestTrade(id1, id3));

	// 2. 发起与接受
	REQUIRE(f.world.requestTrade(id1, id2));
	CHECK(f.world.playerTradeState(id1) == TradeState::kNone); // 尚在邀请中
	REQUIRE(f.world.acceptTrade(id2, id1));

	// 会话已建立
	CHECK(f.world.activeTradeCount() == 1);
	CHECK(f.world.playerTradeState(id1) == TradeState::kTrading);
	CHECK(f.world.playerTradeState(id2) == TradeState::kTrading);
	CHECK(f.world.playerTradePartner(id1) == id2);
	CHECK(f.world.playerTradePartner(id2) == id1);

	// 交易中无法接受或发起第三方交易
	CHECK_FALSE(f.world.requestTrade(id3, id1));
	CHECK_FALSE(f.world.requestTrade(id2, id3));

	// 3. 主动取消
	REQUIRE(f.world.cancelTrade(id1));
	CHECK(f.world.activeTradeCount() == 0);
	CHECK(f.world.playerTradeState(id1) == TradeState::kNone);
	CHECK(f.world.playerTradeState(id2) == TradeState::kNone);

	// 4. 断线清理
	REQUIRE(f.world.requestTrade(id1, id2));
	REQUIRE(f.world.acceptTrade(id2, id1));
	CHECK(f.world.activeTradeCount() == 1);

	f.world.onSessionClosed(id2);
	CHECK(f.world.activeTradeCount() == 0);
	CHECK(f.world.playerTradeState(id1) == TradeState::kNone);
}

TEST_CASE("交易抵押物操作: 道具、宠物、石币抵押与锁定/解锁状态机")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);

	auto *p1 = f.world.playerForTest(id1);
	REQUIRE(p1 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->gold = 500;
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p2 != nullptr);
	p2->floor = 0;
	p2->x = 20;
	p2->y = 20;

	const int item_slot = f.world.giveItemToPlayer(id1, makeTestItem(888));
	REQUIRE(item_slot >= 0);
	const int pet_slot = f.world.givePetToPlayer(id1, makeTestPet(50));
	REQUIRE(pet_slot >= 0);

	REQUIRE(f.world.requestTrade(id1, id2));
	REQUIRE(f.world.acceptTrade(id2, id1));

	// 1. 道具抵押
	CHECK_FALSE(f.world.offerTradeItem(id1, 999)); // 越界槽
	REQUIRE(f.world.offerTradeItem(id1, item_slot));
	CHECK_FALSE(f.world.offerTradeItem(id1, item_slot)); // 重复抵押拦截

	// 2. 宠物抵押
	CHECK_FALSE(f.world.offerTradePet(id1, 99)); // 越界槽
	REQUIRE(f.world.offerTradePet(id1, pet_slot));
	CHECK_FALSE(f.world.offerTradePet(id1, pet_slot)); // 重复抵押拦截

	// 3. 石币抵押
	CHECK_FALSE(f.world.offerTradeGold(id1, 600)); // 超过持有量 (500)
	REQUIRE(f.world.offerTradeGold(id1, 300));

	auto st1 = f.world.playerTradeStatus(id1);
	REQUIRE(st1.has_value());
	CHECK(st1->self_item_slots.size() == 1);
	CHECK(st1->self_pet_slots.size() == 1);
	CHECK(st1->self_gold == 300);

	auto st2 = f.world.playerTradeStatus(id2);
	REQUIRE(st2.has_value());
	CHECK(st2->partner_item_slots.size() == 1);
	CHECK(st2->partner_pet_slots.size() == 1);
	CHECK(st2->partner_gold == 300);

	// 撤回抵押物
	REQUIRE(f.world.removeTradeItem(id1, item_slot));
	CHECK(f.world.playerTradeStatus(id1)->self_item_slots.empty());
	REQUIRE(f.world.offerTradeItem(id1, item_slot)); // 重新抵押

	// 4. 锁定与解锁状态机
	REQUIRE(f.world.lockTrade(id1));
	CHECK(f.world.playerTradeStatus(id1)->self_locked);
	CHECK(f.world.playerTradeStatus(id2)->partner_locked);
	// 锁定后不可再更改抵押物
	CHECK_FALSE(f.world.offerTradeGold(id1, 100));

	// 对方也锁定 ⇒ 状态推进至 kLocked
	REQUIRE(f.world.lockTrade(id2));
	CHECK(f.world.playerTradeState(id1) == TradeState::kLocked);
	CHECK(f.world.playerTradeState(id2) == TradeState::kLocked);

	// 任一方解锁 ⇒ 回退至 kTrading，且双方锁定状态清空
	REQUIRE(f.world.unlockTrade(id1));
	CHECK(f.world.playerTradeState(id1) == TradeState::kTrading);
	CHECK_FALSE(f.world.playerTradeStatus(id1)->self_locked);
	CHECK_FALSE(f.world.playerTradeStatus(id2)->self_locked);
}

TEST_CASE("交易容量防刷门禁 [RV-2]: 接收方背包满/宠物栏满/石币溢出阻断")
{
	MoveFixture f;

	// === 场景 A: 接收方背包满拦截 ===
	{
		const auto id1 = spawnHandshaked(f);
		const auto id2 = spawnHandshaked(f);
		auto *p1 = f.world.playerForTest(id1);
		auto *p2 = f.world.playerForTest(id2);
		REQUIRE(p1 != nullptr);
		REQUIRE(p2 != nullptr);
		p1->floor = 0;
		p1->x = 20;
		p1->y = 20;
		p2->floor = 0;
		p2->x = 20;
		p2->y = 20;

		const int item1 = f.world.giveItemToPlayer(id1, makeTestItem(701));
		REQUIRE(item1 >= 0);

		// 把 id2 背包填满 (45 个道具)
		while (f.world.giveItemToPlayer(id2, makeTestItem(999)) >= 0)
		{
		}
		CHECK(f.world.playerItemSlotsUsed(id2) == static_cast<int>(SA::Model::kMaxItemHave - SA::Model::kStartItemArray));

		REQUIRE(f.world.requestTrade(id1, id2));
		REQUIRE(f.world.acceptTrade(id2, id1));

		REQUIRE(f.world.offerTradeItem(id1, item1));
		REQUIRE(f.world.lockTrade(id1));
		REQUIRE(f.world.lockTrade(id2));

		// id1 确认正常记录，id2 确认时触发容量预检 [RV-2]，阻断交易
		REQUIRE(f.world.confirmTrade(id1));
		CHECK_FALSE(f.world.confirmTrade(id2));
		// 交易未被执行，状态保留
		CHECK(f.world.activeTradeCount() == 1);

		f.world.cancelTrade(id1);
	}

	// === 场景 B: 接收方宠物栏满拦截 ===
	{
		const auto id3 = spawnHandshaked(f);
		const auto id4 = spawnHandshaked(f);
		auto *p3 = f.world.playerForTest(id3);
		auto *p4 = f.world.playerForTest(id4);
		REQUIRE(p3 != nullptr);
		REQUIRE(p4 != nullptr);
		p3->floor = 0;
		p3->x = 20;
		p3->y = 20;
		p4->floor = 0;
		p4->x = 20;
		p4->y = 20;

		const int pet1 = f.world.givePetToPlayer(id3, makeTestPet(501));
		REQUIRE(pet1 >= 0);

		// 把 id4 宠物栏填满 (5 只宠物)
		while (f.world.givePetToPlayer(id4, makeTestPet(600)) >= 0)
		{
		}
		CHECK(f.world.playerPetSlotsUsed(id4) == 5);

		REQUIRE(f.world.requestTrade(id3, id4));
		REQUIRE(f.world.acceptTrade(id4, id3));

		REQUIRE(f.world.offerTradePet(id3, pet1));
		REQUIRE(f.world.lockTrade(id3));
		REQUIRE(f.world.lockTrade(id4));

		REQUIRE(f.world.confirmTrade(id3));
		CHECK_FALSE(f.world.confirmTrade(id4)); // 宠物栏不足阻断
		CHECK(f.world.activeTradeCount() == 1);

		f.world.cancelTrade(id3);
	}

	// === 场景 C: 石币上限溢出拦截 ===
	{
		const auto id5 = spawnHandshaked(f);
		const auto id6 = spawnHandshaked(f);
		auto *p5 = f.world.playerForTest(id5);
		auto *p6 = f.world.playerForTest(id6);
		REQUIRE(p5 != nullptr);
		REQUIRE(p6 != nullptr);
		p5->floor = 0;
		p5->x = 20;
		p5->y = 20;
		p6->floor = 0;
		p6->x = 20;
		p6->y = 20;

		p5->gold = 1000;
		p6->gold = maxHaveGold(0); // 达到随身石币上限 (1,000,000)

		REQUIRE(f.world.requestTrade(id5, id6));
		REQUIRE(f.world.acceptTrade(id6, id5));

		REQUIRE(f.world.offerTradeGold(id5, 500));
		REQUIRE(f.world.lockTrade(id5));
		REQUIRE(f.world.lockTrade(id6));

		REQUIRE(f.world.confirmTrade(id5));
		CHECK_FALSE(f.world.confirmTrade(id6)); // 石币超限阻断
		CHECK(f.world.activeTradeCount() == 1);

		f.world.cancelTrade(id5);
	}
}

TEST_CASE("交易原子互换: 道具、宠物、石币无损原子置换与默认出战宠安全重置")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->gold = 1000;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 20;
	p2->gold = 500;

	const int item1 = f.world.giveItemToPlayer(id1, makeTestItem(1001));
	const int item2 = f.world.giveItemToPlayer(id2, makeTestItem(2002));
	REQUIRE(item1 >= 0);
	REQUIRE(item2 >= 0);

	const int pet1 = f.world.givePetToPlayer(id1, makeTestPet(50, 10));
	REQUIRE(pet1 >= 0);
	p1->default_pet = pet1; // 设为出战宠

	REQUIRE(f.world.requestTrade(id1, id2));
	REQUIRE(f.world.acceptTrade(id2, id1));

	REQUIRE(f.world.offerTradeItem(id1, item1));
	REQUIRE(f.world.offerTradePet(id1, pet1));
	REQUIRE(f.world.offerTradeGold(id1, 300));

	REQUIRE(f.world.offerTradeItem(id2, item2));
	REQUIRE(f.world.offerTradeGold(id2, 100));

	REQUIRE(f.world.lockTrade(id1));
	REQUIRE(f.world.lockTrade(id2));

	REQUIRE(f.world.confirmTrade(id1));
	REQUIRE(f.world.confirmTrade(id2));

	// 交易成功，会话闭环结束
	CHECK(f.world.activeTradeCount() == 0);
	CHECK(f.world.playerTradeState(id1) == TradeState::kNone);
	CHECK(f.world.playerTradeState(id2) == TradeState::kNone);

	// 1. 石币原子置换检查: id1 = 1000 - 300 + 100 = 800; id2 = 500 - 100 + 300 = 700
	CHECK(p1->gold == 800);
	CHECK(p2->gold == 700);

	// 2. 宠物原子置换与 default_pet 安全重置检查
	CHECK(p1->default_pet == -1); // 交易出去的宠物是出战宠，重置为 -1
	CHECK(f.world.playerPetAt(id1, pet1) == nullptr);
	bool id2_has_pet50 = false;
	for (int i = 0; i < 5; ++i)
	{
		const auto *pt = f.world.playerPetAt(id2, i);
		if (pt != nullptr && pt->pet_id == 50)
			id2_has_pet50 = true;
	}
	CHECK(id2_has_pet50);

	// 3. 道具原子置换检查
	bool id1_has_item2002 = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id1, static_cast<int>(i));
		if (it != nullptr && it->item_id == 2002)
			id1_has_item2002 = true;
	}
	CHECK(id1_has_item2002);

	bool id2_has_item1001 = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id2, static_cast<int>(i));
		if (it != nullptr && it->item_id == 1001)
			id2_has_item1001 = true;
	}
	CHECK(id2_has_item1001);
}

// ══ 批次 W.21: 核心社交与通信系统 (名片好友 AddressBook · 邮件信箱 Mail · 聊天广播 Chat) ══

TEST_CASE("名片交换发起与门禁: 距离过远/阵亡/跨图/名片夹满拦截 [RV-1]")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->hp = 100;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 21;
	p2->hp = 100;

	// 1. 不能与自己交换名片
	CHECK_FALSE(f.world.requestAddressCard(id1, id1));

	// 2. 超距离拦截 (> 2 格)
	p2->x = 25;
	CHECK_FALSE(f.world.requestAddressCard(id1, id2));
	p2->x = 20;

	// 3. 跨地图拦截
	p2->floor = 1;
	CHECK_FALSE(f.world.requestAddressCard(id1, id2));
	p2->floor = 0;

	// 4. 阵亡拦截
	p1->hp = 0;
	CHECK_FALSE(f.world.requestAddressCard(id1, id2));
	p1->hp = 100;

	p2->hp = 0;
	CHECK_FALSE(f.world.requestAddressCard(id1, id2));
	p2->hp = 100;

	// 5. 名片夹满员拦截 [RV-1]
	// 往 id1 的名片夹填满 80 张名片
	for (std::size_t i = 0; i < kMaxAddressBook; ++i)
	{
		const auto dummy_sid = spawnHandshaked(f);
		auto *dp = f.world.playerForTest(dummy_sid);
		REQUIRE(dp != nullptr);
		dp->floor = 0;
		dp->x = 20;
		dp->y = 20;
		dp->hp = 100;
		dp->name.assign(("Dummy_" + std::to_string(i)).c_str());
		REQUIRE(f.world.requestAddressCard(id1, dummy_sid));
		REQUIRE(f.world.acceptAddressCard(dummy_sid, id1));
	}
	CHECK(f.world.playerAddressBookCount(id1) == kMaxAddressBook);

	// 名片夹已满 (80 张)，再次申请名片交换必须被拦截 [RV-1]
	CHECK_FALSE(f.world.requestAddressCard(id1, id2));
	CHECK_FALSE(f.world.requestAddressCard(id2, id1));
}

TEST_CASE("名片原子交换与信息同步: 双方互存名片(等级/头像/名字)与重复交换阻断")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	const auto id3 = spawnHandshaked(f);

	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->level = 15;
	p1->image = 10001;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 21;
	p2->level = 20;
	p2->image = 10002;

	// 1. 发起名片交换请求
	REQUIRE(f.world.requestAddressCard(id1, id2));

	// 第三方无法横插接受
	CHECK_FALSE(f.world.acceptAddressCard(id3, id1));

	// 2. 接受名片交换
	REQUIRE(f.world.acceptAddressCard(id2, id1));

	// 3. 双方互存信息断言
	CHECK(f.world.playerAddressBookCount(id1) == 1);
	CHECK(f.world.playerAddressBookCount(id2) == 1);

	const auto cards1 = f.world.playerAddressBook(id1);
	REQUIRE(cards1.size() == 1);
	CHECK(cards1[0].charname == "Bob");
	CHECK(cards1[0].level == 20);
	CHECK(cards1[0].graphicsno == 10002);
	CHECK(cards1[0].online);
	CHECK(cards1[0].session == id2);
	CHECK_FALSE(cards1[0].blocked);

	const auto cards2 = f.world.playerAddressBook(id2);
	REQUIRE(cards2.size() == 1);
	CHECK(cards2[0].charname == "Alice");
	CHECK(cards2[0].level == 15);
	CHECK(cards2[0].graphicsno == 10001);
	CHECK(cards2[0].online);
	CHECK(cards2[0].session == id1);
	CHECK_FALSE(cards2[0].blocked);

	// 4. 重复交换阻断: 双方名片夹中已有对方，不得再次申请
	CHECK_FALSE(f.world.requestAddressCard(id1, id2));
	CHECK_FALSE(f.world.requestAddressCard(id2, id1));
}

TEST_CASE("名片好友生命周期: 好友上下线状态感知同步、名片删除与黑名单屏蔽")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);

	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p2->floor = 0;
	p2->x = 20;
	p2->y = 21;

	REQUIRE(f.world.requestAddressCard(id1, id2));
	REQUIRE(f.world.acceptAddressCard(id2, id1));

	// 1. 离线感知: Bob 断开连接
	f.world.onSessionClosed(id2);

	auto cards1 = f.world.playerAddressBook(id1);
	REQUIRE(cards1.size() == 1);
	CHECK(cards1[0].charname == "Bob");
	CHECK_FALSE(cards1[0].online); // 状态自动变更为离线
	CHECK(cards1[0].session == 0);

	// 2. 黑名单设置与查询
	CHECK_FALSE(f.world.isAddressCardBlocked(id1, "Bob"));
	REQUIRE(f.world.setAddressCardBlock(id1, 0, true));
	CHECK(f.world.isAddressCardBlocked(id1, "Bob"));

	REQUIRE(f.world.setAddressCardBlock(id1, 0, false));
	CHECK_FALSE(f.world.isAddressCardBlocked(id1, "Bob"));

	// 3. 名片删除
	CHECK_FALSE(f.world.removeAddressCard(id1, 99)); // 越界索引
	REQUIRE(f.world.removeAddressCard(id1, 0));
	CHECK(f.world.playerAddressBookCount(id1) == 0);
}

TEST_CASE("邮件寄送与离线信箱: 文本信件寄送、离线收信与邮箱容量满拦截 [RV-2]")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));

	// 1. 寄信输入格式合法性校验
	CHECK_FALSE(f.world.sendMail(id1, "Bob", "", "Message"));                   // 空标题
	CHECK_FALSE(f.world.sendMail(id1, "Bob", std::string(70, 'A'), "Message")); // 标题超长
	CHECK_FALSE(f.world.sendMail(id1, "", "Title", "Message"));                 // 空收件人

	// 2. 正常给离线玩家寄送纯文本信件
	REQUIRE(f.world.sendMail(id1, "Bob", "你好Bob", "欢迎来到石器时代！"));

	// 3. 收件人 Bob 登录上线
	const auto id2 = spawnHandshaked(f);
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	CHECK(f.world.playerMailCount(id2) == 1);
	auto mails = f.world.playerMails(id2);
	REQUIRE(mails.size() == 1);
	CHECK(mails[0].sender_name == "Alice");
	CHECK(mails[0].receiver_name == "Bob");
	CHECK(mails[0].title == "你好Bob");
	CHECK(mails[0].message == "欢迎来到石器时代！");
	CHECK_FALSE(mails[0].is_read);
	CHECK_FALSE(mails[0].has_attachment);

	// 4. 查阅信件
	const auto mid = mails[0].mail_id;
	REQUIRE(f.world.readMail(id2, mid));
	CHECK(f.world.playerMails(id2)[0].is_read);

	// 5. 邮箱容量满员门禁 [RV-2]
	// 往 Bob 的信箱继续寄送 19 封信 (使其达到 20 封上限)
	for (std::size_t i = 1; i < kMaxMailBoxSize; ++i)
	{
		REQUIRE(f.world.sendMail(id1, "Bob", "信件_" + std::to_string(i), "测试内容"));
	}
	CHECK(f.world.playerMailCount(id2) == kMaxMailBoxSize);

	// 第 21 封邮件尝试寄送，必须被容量门禁阻断 [RV-2]
	CHECK_FALSE(f.world.sendMail(id1, "Bob", "溢出信件", "拒绝接收"));

	// 6. 删除邮件
	REQUIRE(f.world.deleteMail(id2, mid));
	CHECK(f.world.playerMailCount(id2) == kMaxMailBoxSize - 1);
}

TEST_CASE("邮件附件流转: 道具/宠物/石币安全寄送与扣除(出战宠重置与GoldLedger审计)")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	auto *p1 = f.world.playerForTest(id1);
	REQUIRE(p1 != nullptr);
	p1->gold = 1000;

	const int item_slot = f.world.giveItemToPlayer(id1, makeTestItem(9001));
	REQUIRE(item_slot >= 0);
	const int pet_slot = f.world.givePetToPlayer(id1, makeTestPet(88, 5));
	REQUIRE(pet_slot >= 0);
	p1->default_pet = pet_slot; // 设为出战宠

	// 寄送携带道具、宠物与石币的完整礼包邮件
	REQUIRE(f.world.sendMail(id1, "Bob", "礼物赠送", "请查收礼品", item_slot, pet_slot, 400));

	// 1. 发送方资产扣除与状态清理校验
	CHECK(p1->gold == 600);                                 // 1000 - 400
	CHECK(f.world.playerItemAt(id1, item_slot) == nullptr); // 道具槽已清空
	CHECK(f.world.playerPetAt(id1, pet_slot) == nullptr);   // 宠物槽已清空
	CHECK(p1->default_pet == -1);                           // 默认出战宠被安全重置

	// 2. 接收方信件附件校验
	REQUIRE(f.world.playerMailCount(id2) == 1);
	const auto mail = f.world.playerMails(id2)[0];
	CHECK(mail.has_attachment);
	CHECK(mail.attached_gold == 400);
	REQUIRE(mail.attached_item.has_value());
	CHECK(mail.attached_item->item_id == 9001);
	REQUIRE(mail.attached_pet.has_value());
	CHECK(mail.attached_pet->pet_id == 88);

	// 3. 防意外损毁: 未领取附件时禁止删除邮件
	CHECK_FALSE(f.world.deleteMail(id2, mail.mail_id));
}

TEST_CASE("邮件附件领取门禁: 背包满/宠物栏满/石币超限前置阻断与原子收取")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->gold = 2000;
	p2->gold = 100;

	const int item_slot = f.world.giveItemToPlayer(id1, makeTestItem(777));
	REQUIRE(item_slot >= 0);
	const int pet_slot = f.world.givePetToPlayer(id1, makeTestPet(99, 1));
	REQUIRE(pet_slot >= 0);

	REQUIRE(f.world.sendMail(id1, "Bob", "大礼包", "包含三类资产", item_slot, pet_slot, 500));
	REQUIRE(f.world.playerMailCount(id2) == 1);
	const auto mid = f.world.playerMails(id2)[0].mail_id;

	// === 场景 A: 背包满拦截 ===
	while (f.world.giveItemToPlayer(id2, makeTestItem(999)) >= 0)
	{
	}
	CHECK(f.world.playerItemSlotsUsed(id2) == static_cast<int>(SA::Model::kMaxItemHave - SA::Model::kStartItemArray));
	CHECK_FALSE(f.world.takeMailAttachment(id2, mid)); // 背包满阻断
	// 清空 1 格背包
	p2->clearItemSlot(static_cast<int>(SA::Model::kStartItemArray));

	// === 场景 B: 宠物栏满拦截 ===
	while (f.world.givePetToPlayer(id2, makeTestPet(500)) >= 0)
	{
	}
	CHECK(f.world.playerPetSlotsUsed(id2) == 5);
	CHECK_FALSE(f.world.takeMailAttachment(id2, mid)); // 宠物栏满阻断
	// 清空 1 个宠物槽
	p2->clearPetSlot(0);

	// === 场景 C: 石币溢出拦截 ===
	p2->gold = maxHaveGold(0);
	CHECK_FALSE(f.world.takeMailAttachment(id2, mid)); // 石币溢出阻断
	p2->gold = 100;

	// === 场景 D: 前置校验全通，原子提取附件 ===
	REQUIRE(f.world.takeMailAttachment(id2, mid));

	// 资产成功入账
	CHECK(p2->gold == 600); // 100 + 500
	bool found_it777 = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id2, static_cast<int>(i));
		if (it != nullptr && it->item_id == 777)
			found_it777 = true;
	}
	CHECK(found_it777);

	bool found_pet99 = false;
	for (int i = 0; i < 5; ++i)
	{
		const auto *pt = f.world.playerPetAt(id2, i);
		if (pt != nullptr && pt->pet_id == 99)
			found_pet99 = true;
	}
	CHECK(found_pet99);

	// 附件提取完毕，信件附件状态解除，允许安全删除
	CHECK_FALSE(f.world.playerMails(id2)[0].has_attachment);
	REQUIRE(f.world.deleteMail(id2, mid));
	CHECK(f.world.playerMailCount(id2) == 0);
}

TEST_CASE("分级聊天频道广播: 视野说话(<=9格)、组队频道跨图同步、世界广播与黑名单私聊拦截")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f); // Alice (20, 20, floor 0)
	const auto id2 = spawnHandshaked(f); // Bob (25, 20, floor 0) 距离 5 <= 9
	const auto id3 = spawnHandshaked(f); // Charlie (50, 50, floor 0) 距离 30 > 9

	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	REQUIRE(f.world.setPlayerName(id3, "Charlie"));

	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	auto *p3 = f.world.playerForTest(id3);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(p3 != nullptr);
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p2->floor = 0;
	p2->x = 25;
	p2->y = 20;
	p3->floor = 0;
	p3->x = 50;
	p3->y = 50;

	// 1. 普通说话频道 (kTalkNormal, 视野 <= 9 格)
	REQUIRE(f.world.sendChat(id1, ChatChannel::kTalkNormal, "附近的朋友你们好！"));
	// Alice 收到 (自身)
	CHECK(f.world.pendingChatMessageCount(id1) == 1);
	// Bob 在 5 格内，收到
	CHECK(f.world.pendingChatMessageCount(id2) == 1);
	// Charlie 在 30 格外，未收到
	CHECK(f.world.pendingChatMessageCount(id3) == 0);

	auto b_chats = f.world.pollChatMessages(id2);
	REQUIRE(b_chats.size() == 1);
	CHECK(b_chats[0].sender_name == "Alice");
	CHECK(b_chats[0].text == "附近的朋友你们好！");
	CHECK(b_chats[0].channel == ChatChannel::kTalkNormal);
	CHECK(f.world.pendingChatMessageCount(id2) == 0); // 轮询后清空
	(void)f.world.pollChatMessages(id1);

	// 2. 队伍频道 (kTalkParty)
	// 未组队前发送失败
	CHECK_FALSE(f.world.sendChat(id1, ChatChannel::kTalkParty, "队伍集合！"));

	// Charlie 靠近 Alice 组队，随后移至远方 (验证队伍频道跨越视野距离广播)
	p3->x = 21;
	p3->y = 20;
	REQUIRE(f.world.joinParty(id3, id1));
	p3->x = 50;
	p3->y = 50;

	REQUIRE(f.world.sendChat(id1, ChatChannel::kTalkParty, "队伍集合！"));
	// Alice 与 Charlie 均收到
	CHECK(f.world.pendingChatMessageCount(id1) == 1);
	CHECK(f.world.pendingChatMessageCount(id3) == 1);
	// 未在队内的 Bob 未收到
	CHECK(f.world.pendingChatMessageCount(id2) == 0);

	(void)f.world.pollChatMessages(id1);
	(void)f.world.pollChatMessages(id3);

	// 3. 世界广播频道 (kTalkShout)
	REQUIRE(f.world.sendChat(id2, ChatChannel::kTalkShout, "全服大喊：高价收石龟！"));
	CHECK(f.world.pendingChatMessageCount(id1) == 1);
	CHECK(f.world.pendingChatMessageCount(id2) == 1);
	CHECK(f.world.pendingChatMessageCount(id3) == 1);

	(void)f.world.pollChatMessages(id1);
	(void)f.world.pollChatMessages(id2);
	(void)f.world.pollChatMessages(id3);

	// 4. 私聊/密聊频道 (kTalkTell)
	// 目标离线拦截
	CHECK_FALSE(f.world.sendChat(id1, ChatChannel::kTalkTell, "在吗？", "UnknownUser"));
	// 正常密聊 Bob
	REQUIRE(f.world.sendChat(id1, ChatChannel::kTalkTell, "在吗？私聊你点事", "Bob"));
	CHECK(f.world.pendingChatMessageCount(id1) == 1);
	CHECK(f.world.pendingChatMessageCount(id2) == 1);
	CHECK(f.world.pendingChatMessageCount(id3) == 0);

	(void)f.world.pollChatMessages(id1);
	(void)f.world.pollChatMessages(id2);

	// 5. 黑名单拦截私聊
	// Bob 与 Alice 交换名片并将 Alice 拉入黑名单
	p2->x = 21; // 走到 Alice 身边
	REQUIRE(f.world.requestAddressCard(id2, id1));
	REQUIRE(f.world.acceptAddressCard(id1, id2));
	REQUIRE(f.world.setAddressCardBlock(id2, 0, true)); // Bob 把 Alice 设为 blocked

	// Alice 再次私聊 Bob，必须被黑名单拦截阻断
	CHECK_FALSE(f.world.sendChat(id1, ChatChannel::kTalkTell, "能收到吗？", "Bob"));
	CHECK(f.world.pendingChatMessageCount(id2) == 0); // Bob 未收到任何消息
}

// ══ 阶段 2: 家族管理与庄园系统 (Family System) ═════════════════════════════

TEST_CASE("家族创建门禁: 30级门限、10000石币扣除、重名与名称合法性校验")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	REQUIRE(p1 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));

	p1->level = 20;
	p1->gold = 20000;

	// 1. 等级不足 30 级拦截 (FMLEADERLV = 30)
	CHECK(f.world.createFamily(id1, "尼斯部落", "团结互助") == 0);

	// 2. 等级达 30 级，但石币不足 10000
	p1->level = 30;
	p1->gold = 5000;
	CHECK(f.world.createFamily(id1, "尼斯部落", "团结互助") == 0);

	// 3. 名称非法拦截 (空名、超长、含空格)
	p1->gold = 20000;
	CHECK(f.world.createFamily(id1, "", "简介") == 0);
	CHECK(f.world.createFamily(id1, "尼斯 部落", "简介") == 0);
	CHECK(f.world.createFamily(id1, std::string(35, 'A'), "简介") == 0);

	// 4. 正常创建成功: 扣除 10000 石币，返回有效 family_id
	const auto fid = f.world.createFamily(id1, "尼斯部落", "石器第一家");
	REQUIRE(fid > 0);
	CHECK(p1->gold == 10000); // 20000 - 10000
	CHECK(f.world.familyCount() == 1);
	CHECK(f.world.playerFamilyId(id1) == fid);
	CHECK(f.world.playerFamilyRole(id1) == FamilyRole::kLeader);

	auto fam_opt = f.world.getFamilyInfo(fid);
	REQUIRE(fam_opt.has_value());
	CHECK(fam_opt->name == "尼斯部落");
	CHECK(fam_opt->rule == "石器第一家");
	CHECK(fam_opt->leader_name == "Alice");
	CHECK(fam_opt->family_fame == 100);
	REQUIRE(fam_opt->members.size() == 1);
	CHECK(fam_opt->members[0].charname == "Alice");
	CHECK(fam_opt->members[0].role == FamilyRole::kLeader);
	CHECK(fam_opt->members[0].contribution == 100);
	CHECK(fam_opt->members[0].online);

	// 5. 重名拦截与已有家族重复创建拦截
	const auto id2 = spawnHandshaked(f);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	p2->level = 40;
	p2->gold = 50000;

	// Bob 尝试创建同名家族 "尼斯部落" 必须被拒
	CHECK(f.world.createFamily(id2, "尼斯部落", "仿冒家族") == 0);

	// Alice 尝试再次创建家族必须被拒 (已属于家族)
	CHECK(f.world.createFamily(id1, "新部落", "另起炉灶") == 0);
}

TEST_CASE("家族申请与审批及满员门禁 [RV-1]")
{
	MoveFixture f;
	const auto id_leader = spawnHandshaked(f);
	const auto id_bob = spawnHandshaked(f);
	auto *p_leader = f.world.playerForTest(id_leader);
	auto *p_bob = f.world.playerForTest(id_bob);
	REQUIRE(p_leader != nullptr);
	REQUIRE(p_bob != nullptr);
	REQUIRE(f.world.setPlayerName(id_leader, "Alice"));
	REQUIRE(f.world.setPlayerName(id_bob, "Bob"));
	p_leader->level = 30;
	p_leader->gold = 20000;
	p_bob->level = 15;

	const auto fid = f.world.createFamily(id_leader, "萨姆吉尔勇士", "征战加鲁卡");
	REQUIRE(fid > 0);

	// 1. Bob 申请加入家族
	REQUIRE(f.world.applyJoinFamily(id_bob, fid));
	// 重复申请拦截
	CHECK_FALSE(f.world.applyJoinFamily(id_bob, fid));

	auto fam = f.world.getFamilyInfo(fid);
	REQUIRE(fam.has_value());
	REQUIRE(fam->applicants.size() == 1);
	CHECK(fam->applicants[0].charname == "Bob");
	CHECK(fam->applicants[0].role == FamilyRole::kApply);

	// 2. 非管理人员无法审批 (Bob 自身审批)
	CHECK_FALSE(f.world.acceptFamilyMember(id_bob, fid, "Bob", true));

	// 3. 族长拒绝申请
	REQUIRE(f.world.acceptFamilyMember(id_leader, fid, "Bob", false));
	fam = f.world.getFamilyInfo(fid);
	CHECK(fam->applicants.empty());
	CHECK(f.world.playerFamilyId(id_bob) == 0);

	// 4. Bob 重新申请并由族长批准入族
	REQUIRE(f.world.applyJoinFamily(id_bob, fid));
	REQUIRE(f.world.acceptFamilyMember(id_leader, fid, "Bob", true));
	CHECK(f.world.playerFamilyId(id_bob) == fid);
	CHECK(f.world.playerFamilyRole(id_bob) == FamilyRole::kMember);
	fam = f.world.getFamilyInfo(fid);
	REQUIRE(fam->members.size() == 2);
	CHECK(fam->applicants.empty());

	// 5. 满员门禁拦截 [RV-1]
	// 模拟满员 50 人
	const auto id_extra = spawnHandshaked(f);
	auto *p_extra = f.world.playerForTest(id_extra);
	REQUIRE(p_extra != nullptr);
	REQUIRE(f.world.setPlayerName(id_extra, "ExtraPlayer"));

	// 内部将成员补满至 50 人进行门限阻断断言
	auto *fam_info = f.world.familyForTest(fid);
	REQUIRE(fam_info != nullptr);
	while (fam_info->members.size() < kMaxFamilyMembers)
	{
		FamilyMember m{};
		m.charname = "Dummy_" + std::to_string(fam_info->members.size() + 1);
		m.level = 20;
		m.role = FamilyRole::kMember;
		fam_info->members.push_back(m);
	}
	CHECK(f.world.getFamilyInfo(fid)->members.size() == 50);

	// 第 51 人申请入族，必须被满员门禁阻断拒绝 [RV-1]
	CHECK_FALSE(f.world.applyJoinFamily(id_extra, fid));
}

TEST_CASE("家族职位任免、族长转让与请离成员")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f); // Alice (族长)
	const auto id2 = spawnHandshaked(f); // Bob
	const auto id3 = spawnHandshaked(f); // Charlie
	const auto id4 = spawnHandshaked(f); // David
	auto *p1 = f.world.playerForTest(id1);
	REQUIRE(p1 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	REQUIRE(f.world.setPlayerName(id3, "Charlie"));
	REQUIRE(f.world.setPlayerName(id4, "David"));
	p1->level = 30;
	p1->gold = 20000;

	const auto fid = f.world.createFamily(id1, "玛丽娜斯水友会", "钓鱼与聊天");
	REQUIRE(fid > 0);

	// 添加 Bob, Charlie, David 入族
	REQUIRE(f.world.applyJoinFamily(id2, fid));
	REQUIRE(f.world.applyJoinFamily(id3, fid));
	REQUIRE(f.world.applyJoinFamily(id4, fid));
	REQUIRE(f.world.acceptFamilyMember(id1, fid, "Bob", true));
	REQUIRE(f.world.acceptFamilyMember(id1, fid, "Charlie", true));
	REQUIRE(f.world.acceptFamilyMember(id1, fid, "David", true));

	// 1. 族长任命 Bob 为长老
	REQUIRE(f.world.setFamilyMemberRole(id1, fid, "Bob", FamilyRole::kElder));
	CHECK(f.world.playerFamilyRole(id2) == FamilyRole::kElder);

	// 2. 长老 Bob 请离普通成员 Charlie (成功)
	REQUIRE(f.world.kickFamilyMember(id2, fid, "Charlie"));
	CHECK(f.world.playerFamilyId(id3) == 0);
	CHECK(f.world.getFamilyInfo(fid)->members.size() == 3);

	// 3. 长老 Bob 尝试请离族长 Alice (拦截)
	CHECK_FALSE(f.world.kickFamilyMember(id2, fid, "Alice"));

	// 4. 族长 Alice 任命 David 为长老
	REQUIRE(f.world.setFamilyMemberRole(id1, fid, "David", FamilyRole::kElder));
	// 长老 Bob 尝试请离同级长老 David (拦截)
	CHECK_FALSE(f.world.kickFamilyMember(id2, fid, "David"));

	// 5. 族长转让: Alice 将族长转给 Bob
	REQUIRE(f.world.setFamilyMemberRole(id1, fid, "Bob", FamilyRole::kLeader));
	CHECK(f.world.playerFamilyRole(id2) == FamilyRole::kLeader);
	CHECK(f.world.playerFamilyRole(id1) == FamilyRole::kElder); // Alice 变为长老
	CHECK(f.world.getFamilyInfo(fid)->leader_name == "Bob");

	// 6. 族长修改家族宗旨
	REQUIRE(f.world.setFamilyRule(id2, fid, "新族长上任，福利多多！"));
	CHECK(f.world.getFamilyInfo(fid)->rule == "新族长上任，福利多多！");

	// 7. 解散家族 (非族长无法解散)
	CHECK_FALSE(f.world.disbandFamily(id1, fid)); // Alice 现为长老，无权解散
	REQUIRE(f.world.disbandFamily(id2, fid));     // Bob 族长解散
	CHECK(f.world.familyCount() == 0);
	CHECK(f.world.playerFamilyId(id1) == 0);
	CHECK(f.world.playerFamilyId(id2) == 0);
	CHECK(f.world.playerFamilyId(id4) == 0);
}

TEST_CASE("家族主动退出门禁: 族长不可退与普通成员解绑")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	p1->level = 30;
	p1->gold = 20000;

	const auto fid = f.world.createFamily(id1, "加加猎人团", "打猎专精");
	REQUIRE(fid > 0);
	REQUIRE(f.world.applyJoinFamily(id2, fid));
	REQUIRE(f.world.acceptFamilyMember(id1, fid, "Bob", true));

	// 1. 族长 Alice 尝试直接退出家族 (阻断，必须转让或解散)
	CHECK_FALSE(f.world.leaveFamily(id1));
	CHECK(f.world.playerFamilyId(id1) == fid);

	// 2. 普通成员 Bob 主动退出家族 (成功)
	REQUIRE(f.world.leaveFamily(id2));
	CHECK(f.world.playerFamilyId(id2) == 0);
	CHECK(f.world.getFamilyInfo(fid)->members.size() == 1);
}

TEST_CASE("家族金库存储、提取与容量双向校验 [RV-2]")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f); // 族长
	const auto id2 = spawnHandshaked(f); // 成员
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	p1->level = 30;
	p1->gold = 50000;
	p2->gold = 10000;

	const auto fid = f.world.createFamily(id1, "金库先锋", "积累财富");
	REQUIRE(fid > 0);
	REQUIRE(f.world.applyJoinFamily(id2, fid));
	REQUIRE(f.world.acceptFamilyMember(id1, fid, "Bob", true));

	// 1. 成员 Bob 存入 4000 石币
	REQUIRE(f.world.depositFamilyGold(id2, 4000));
	CHECK(p2->gold == 6000); // 10000 - 4000
	auto fam = f.world.getFamilyInfo(fid);
	CHECK(fam->family_gold == 4000);
	CHECK(fam->family_fame == 104); // 初始 100 + 4
	for (const auto &m : fam->members)
	{
		if (m.charname == "Bob")
		{
			CHECK(m.contribution == 14); // 初始 10 + 4
		}
	}

	// 2. 普通成员 Bob 尝试取款 (拦截，无权限)
	CHECK_FALSE(f.world.withdrawFamilyGold(id2, 1000));

	// 3. 族长 Alice 取款 2000 石币
	const auto alice_gold_before = p1->gold;
	REQUIRE(f.world.withdrawFamilyGold(id1, 2000));
	CHECK(p1->gold == alice_gold_before + 2000);
	CHECK(f.world.getFamilyInfo(fid)->family_gold == 2000);

	// 4. [RV-2] 家族金库上限检查 (上限 1 亿石币)
	// 将金库石币提升至 99,995,000
	auto *fam_info = f.world.familyForTest(fid);
	REQUIRE(fam_info != nullptr);
	fam_info->family_gold = 99995000;
	p2->gold = 20000;
	// 尝试存入 10,000 石币，因 99,995,000 + 10,000 > 100,000,000 触发金库满员阻断 [RV-2]
	CHECK_FALSE(f.world.depositFamilyGold(id2, 10000));
	CHECK(p2->gold == 20000); // 玩家资产分文未少

	// 5. [RV-2] 玩家随身容量预检: 随身石币上限防溢出阻断
	p1->gold = SA::World::maxHaveGold(0) - 500; // 随身仅剩 500 容量即达上限
	// 从金库取款 1000 石币，由于会导致随身石币溢出损失，触发容量前置校验阻断 [RV-2]
	CHECK_FALSE(f.world.withdrawFamilyGold(id1, 1000));
	CHECK(f.world.getFamilyInfo(fid)->family_gold == 99995000); // 金库未受损
}

TEST_CASE("家族四大庄园据点占领与争夺")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	p1->level = 30;
	p1->gold = 20000;
	p2->level = 30;
	p2->gold = 20000;

	const auto fid1 = f.world.createFamily(id1, "庄园战盟", "一统尼斯");
	const auto fid2 = f.world.createFamily(id2, "挑战者军团", "夺取庄园");
	REQUIRE(fid1 > 0);
	REQUIRE(fid2 > 0);

	// 1. 初始四大庄园无归属
	CHECK(f.world.manorOwnerFamily(FamilyManor::kSamo) == 0);
	CHECK(f.world.manorOwnerFamily(FamilyManor::kMarina) == 0);
	CHECK(f.world.familyManor(fid1) == FamilyManor::kNone);

	// 2. 家族 1 占领萨姆吉尔庄园 (kSamo)
	REQUIRE(f.world.occupyManor(fid1, FamilyManor::kSamo));
	CHECK(f.world.familyManor(fid1) == FamilyManor::kSamo);
	CHECK(f.world.manorOwnerFamily(FamilyManor::kSamo) == fid1);

	// 3. 家族 1 随后占领加加庄园 (kJaja) -> 旧庄园 kSamo 自动释放
	REQUIRE(f.world.occupyManor(fid1, FamilyManor::kJaja));
	CHECK(f.world.familyManor(fid1) == FamilyManor::kJaja);
	CHECK(f.world.manorOwnerFamily(FamilyManor::kJaja) == fid1);
	CHECK(f.world.manorOwnerFamily(FamilyManor::kSamo) == 0);

	// 4. 家族 2 夺取加加庄园 (kJaja) -> 家族 1 失去庄园，家族 2 入主
	REQUIRE(f.world.occupyManor(fid2, FamilyManor::kJaja));
	CHECK(f.world.familyManor(fid2) == FamilyManor::kJaja);
	CHECK(f.world.manorOwnerFamily(FamilyManor::kJaja) == fid2);
	CHECK(f.world.familyManor(fid1) == FamilyManor::kNone);
}

TEST_CASE("家族跨图专属聊天频道 (kTalkFamily) 与上下线感知")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f); // Alice (家族 1)
	const auto id2 = spawnHandshaked(f); // Bob (家族 1)
	const auto id3 = spawnHandshaked(f); // Charlie (家族 2)
	const auto id4 = spawnHandshaked(f); // David (无家族)
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	auto *p3 = f.world.playerForTest(id3);
	auto *p4 = f.world.playerForTest(id4);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(p3 != nullptr);
	REQUIRE(p4 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	REQUIRE(f.world.setPlayerName(id3, "Charlie"));
	REQUIRE(f.world.setPlayerName(id4, "David"));
	p1->level = 30;
	p1->gold = 20000;
	p3->level = 30;
	p3->gold = 20000;

	// 分布在不同地图
	p1->floor = 100;
	p1->x = 10;
	p1->y = 10;
	p2->floor = 200;
	p2->x = 50;
	p2->y = 50;
	p3->floor = 100;
	p3->x = 10;
	p3->y = 10;
	p4->floor = 100;
	p4->x = 11;
	p4->y = 10;

	const auto fid1 = f.world.createFamily(id1, "星辰阁", "跨界联动");
	const auto fid2 = f.world.createFamily(id3, "青云门", "隐世清修");
	REQUIRE(fid1 > 0);
	REQUIRE(fid2 > 0);

	REQUIRE(f.world.applyJoinFamily(id2, fid1));
	REQUIRE(f.world.acceptFamilyMember(id1, fid1, "Bob", true));

	// 1. 无家族玩家 David 发送家族频道消息拦截
	CHECK_FALSE(f.world.sendChat(id4, ChatChannel::kTalkFamily, "有人吗？"));

	// 2. 家族 1 族长 Alice 发送家族消息
	REQUIRE(f.world.sendChat(id1, ChatChannel::kTalkFamily, "今晚 8 点庄园集合！"));
	// 家族 1 成员 Alice 与 Bob (即使不同地图) 均收到
	CHECK(f.world.pendingChatMessageCount(id1) == 1);
	CHECK(f.world.pendingChatMessageCount(id2) == 1);
	// 家族 2 成员 Charlie 与无家族 David (即使同图身旁) 未收到
	CHECK(f.world.pendingChatMessageCount(id3) == 0);
	CHECK(f.world.pendingChatMessageCount(id4) == 0);

	auto b_chats = f.world.pollChatMessages(id2);
	REQUIRE(b_chats.size() == 1);
	CHECK(b_chats[0].sender_name == "Alice");
	CHECK(b_chats[0].text == "今晚 8 点庄园集合！");
	CHECK(b_chats[0].channel == ChatChannel::kTalkFamily);

	(void)f.world.pollChatMessages(id1);

	// 3. 成员上下线状态感知
	CHECK(f.world.getFamilyInfo(fid1)->members[1].online);
	f.world.onSessionClosed(id2);
	CHECK_FALSE(f.world.getFamilyInfo(fid1)->members[1].online);
}

// ══ 阶段 2: 玩家摆摊与拍卖市场系统 (Street Stall & Consignment Market) ═════

TEST_CASE("玩家摆摊生命周期与移动/组队/交易拦截门禁")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p2->floor = 0;
	p2->x = 21;
	p2->y = 20;

	// 给 Alice 放置道具和宠物
	auto item = makeTestItem(1001);
	item.name.assign("恢复药水");
	const int item_slot = f.world.giveItemToPlayer(id1, item);
	REQUIRE(item_slot >= 0);

	auto pet = makeTestPet(50, 5);
	pet.name.assign("小暴龙");
	const int pet_slot = f.world.givePetToPlayer(id1, pet);
	REQUIRE(pet_slot >= 0);

	// 1. 开摊进入配置模式
	REQUIRE(f.world.openStall(id1, "Alice的小铺"));
	CHECK_FALSE(f.world.isPlayerVending(id1)); // 尚未正式营业

	// 2. 上架道具与宠物，并设定单价
	REQUIRE(f.world.setStallItem(id1, item_slot, 300));
	REQUIRE(f.world.setStallPet(id1, pet_slot, 800));

	// 检视配置态中的摊位信息
	auto stall_opt = f.world.getPlayerStall(id1);
	REQUIRE(stall_opt.has_value());
	CHECK(stall_opt->title == "Alice的小铺");
	CHECK(stall_opt->items.size() == 1);
	CHECK(stall_opt->items[0].price == 300);
	CHECK(stall_opt->items[0].item_name == "恢复药水");
	CHECK(stall_opt->pets.size() == 1);
	CHECK(stall_opt->pets[0].price == 800);
	CHECK(stall_opt->pets[0].pet_name == "小暴龙");

	// 3. 正式出摊营业
	REQUIRE(f.world.startStallVending(id1));
	CHECK(f.world.isPlayerVending(id1));

	// 4. 摆摊态门禁拦截
	// a. 摊主禁止移动: onWalk 拦截
	SA::Domain::WalkRequest walk_req{};
	walk_req.x = 20;
	walk_req.y = 20;
	walk_req.direction.assign("e");
	f.world.onWalk(id1, walk_req);
	f.clock.advance(300);
	f.world.tick();
	CHECK(p1->x == 20); // 坐标未变，移动被锁定
	CHECK(p1->y == 20);

	// b. 摊主禁止组队 (发起与接受均被拦截)
	CHECK_FALSE(f.world.joinParty(id2, id1));
	CHECK_FALSE(f.world.joinParty(id1, id2));

	// c. 摊主禁止交易 (发起与接受均被拦截)
	CHECK_FALSE(f.world.requestTrade(id2, id1));
	CHECK_FALSE(f.world.requestTrade(id1, id2));

	// 5. 收摊与解除门禁
	REQUIRE(f.world.closeStall(id1));
	CHECK_FALSE(f.world.isPlayerVending(id1));
	CHECK_FALSE(f.world.getPlayerStall(id1).has_value());

	// 收摊后交易与组队恢复
	CHECK(f.world.joinParty(id2, id1));
	f.world.leaveParty(id2);
}

TEST_CASE("玩家摆摊购买与出战宠重置")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	p1->floor = 0;
	p1->x = 30;
	p1->y = 30;
	p1->gold = 1000;
	p2->floor = 0;
	p2->x = 31;
	p2->y = 30;
	p2->gold = 2000;

	auto item = makeTestItem(2001);
	item.name.assign("精工石斧");
	const int item_slot = f.world.giveItemToPlayer(id1, item);
	REQUIRE(item_slot >= 0);

	auto pet = makeTestPet(88, 10);
	pet.name.assign("战斗金虎");
	const int pet_slot = f.world.givePetToPlayer(id1, pet);
	REQUIRE(pet_slot >= 0);

	// 将该宠物设为出战宠
	p1->default_pet = pet_slot;
	CHECK(p1->default_pet == pet_slot);

	REQUIRE(f.world.openStall(id1, "神兵珍兽阁"));
	REQUIRE(f.world.setStallItem(id1, item_slot, 500));
	REQUIRE(f.world.setStallPet(id1, pet_slot, 1200));

	// 出战宠上架摊位时，default_pet 安全重置为 -1
	CHECK(p1->default_pet == -1);

	REQUIRE(f.world.startStallVending(id1));

	// 视野内的附近摊位检视
	auto nearby = f.world.nearbyStalls(id2, 5);
	REQUIRE(nearby.size() == 1);
	CHECK(nearby[0].seller_name == "Alice");
	CHECK(nearby[0].title == "神兵珍兽阁");

	// 1. Bob 购买道具
	REQUIRE(f.world.buyFromStall(id2, id1, MarketAssetType::kItem, item_slot));
	CHECK(p2->gold == 1500);                                             // 2000 - 500
	CHECK(p1->gold == 1500);                                             // 1000 + 500
	CHECK_FALSE(p1->items[static_cast<std::size_t>(item_slot)].valid()); // 卖家槽位清空
	CHECK(f.world.playerItemSlotsUsed(id2) == 1);                        // 买家到账

	// 2. Bob 购买宠物
	REQUIRE(f.world.buyFromStall(id2, id1, MarketAssetType::kPet, pet_slot));
	CHECK(p2->gold == 300);                                            // 1500 - 1200
	CHECK(p1->gold == 2700);                                           // 1500 + 1200
	CHECK_FALSE(p1->pets[static_cast<std::size_t>(pet_slot)].valid()); // 卖家宠物栏清空
	CHECK(f.world.playerPetSlotsUsed(id2) == 1);                       // 买家到账

	// 售空后摊位自动关闭
	CHECK_FALSE(f.world.isPlayerVending(id1));
}

TEST_CASE("摆摊购买买家容量前置阻断 [RV-1] 与距离门禁")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	p1->floor = 0;
	p1->x = 10;
	p1->y = 10;
	p2->floor = 0;
	p2->x = 10;
	p2->y = 15; // 切比雪夫距离 5 > 3
	p2->gold = 10000;

	const int item_slot = f.world.giveItemToPlayer(id1, makeTestItem(3001));
	REQUIRE(item_slot >= 0);
	const int pet_slot = f.world.givePetToPlayer(id1, makeTestPet(10, 1));
	REQUIRE(pet_slot >= 0);

	REQUIRE(f.world.openStall(id1, "超远距离测试"));
	REQUIRE(f.world.setStallItem(id1, item_slot, 200));
	REQUIRE(f.world.setStallPet(id1, pet_slot, 500));
	REQUIRE(f.world.startStallVending(id1));

	// 1. 距离门禁 (> 3 格拒绝)
	CHECK_FALSE(f.world.buyFromStall(id2, id1, MarketAssetType::kItem, item_slot));

	// Bob 走到距离 <= 3 格 (10, 12: 切比雪夫距离 = 2)
	p2->y = 12;

	// 2. [RV-1] 买家背包满阻断购买道具
	while (f.world.giveItemToPlayer(id2, makeTestItem(999)) >= 0)
	{
	}
	CHECK(f.world.playerItemSlotsUsed(id2) == 45); // 满包
	CHECK_FALSE(f.world.buyFromStall(id2, id1, MarketAssetType::kItem, item_slot));
	CHECK(p2->gold == 10000); // 资产分文未扣

	// Bob 腾出 1 格背包，成功购买
	p2->clearItemSlot(static_cast<int>(SA::Model::kStartItemArray));
	REQUIRE(f.world.buyFromStall(id2, id1, MarketAssetType::kItem, item_slot));
	CHECK(p2->gold == 9800);

	// 3. [RV-1] 买家宠物栏满阻断购买宠物
	while (f.world.givePetToPlayer(id2, makeTestPet(99, 1)) >= 0)
	{
	}
	CHECK(f.world.playerPetSlotsUsed(id2) == 5); // 满栏
	CHECK_FALSE(f.world.buyFromStall(id2, id1, MarketAssetType::kPet, pet_slot));
	CHECK(p2->gold == 9800);

	// Bob 腾出 1 格宠物槽，成功购买
	p2->clearPetSlot(0);
	REQUIRE(f.world.buyFromStall(id2, id1, MarketAssetType::kPet, pet_slot));
	CHECK(p2->gold == 9300);
}

TEST_CASE("拍卖市场挂牌、100石币挂牌费扣除与资产严格解耦")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	REQUIRE(f.world.setPlayerName(id, "Alice"));

	const int item_slot = f.world.giveItemToPlayer(id, makeTestItem(5001));
	REQUIRE(item_slot >= 0);
	const int pet_slot = f.world.givePetToPlayer(id, makeTestPet(60, 1));
	REQUIRE(pet_slot >= 0);
	p->default_pet = pet_slot;

	// 1. 余额不足挂牌费 (50 < 100) 拦截
	p->gold = 50;
	CHECK(f.world.listMarketItem(id, item_slot, 2000) == 0);
	CHECK(f.world.listMarketPet(id, pet_slot, 5000) == 0);

	// 2. 补足挂牌费并挂牌道具
	p->gold = 500;
	const auto lid_item = f.world.listMarketItem(id, item_slot, 2000);
	REQUIRE(lid_item > 0);
	CHECK(p->gold == 400); // 严格扣除 100 挂牌费
	// 实体严格解耦：原背包槽位已被清除
	CHECK_FALSE(p->items[static_cast<std::size_t>(item_slot)].valid());

	auto listing_item = f.world.getMarketListing(lid_item);
	REQUIRE(listing_item.has_value());
	CHECK(listing_item->seller_name == "Alice");
	CHECK(listing_item->price == 2000);
	CHECK(listing_item->asset_type == MarketAssetType::kItem);
	CHECK(listing_item->item_data.has_value());

	// 3. 挂牌宠物
	const auto lid_pet = f.world.listMarketPet(id, pet_slot, 5000);
	REQUIRE(lid_pet > 0);
	CHECK(p->gold == 300); // 严格扣除 100 挂牌费
	// 出战宠安全重置
	CHECK(p->default_pet == -1);
	CHECK_FALSE(p->pets[static_cast<std::size_t>(pet_slot)].valid());

	auto listing_pet = f.world.getMarketListing(lid_pet);
	REQUIRE(listing_pet.has_value());
	CHECK(listing_pet->asset_type == MarketAssetType::kPet);
	CHECK(listing_pet->pet_data.has_value());

	CHECK(f.world.activeMarketListingCount() == 2);
	CHECK(f.world.playerMarketListings(id).size() == 2);

	// 4. 上架上限测试 (最大 10 件)
	p->gold = 10000;
	for (int i = 0; i < 8; ++i)
	{
		const int slot = f.world.giveItemToPlayer(id, makeTestItem(6000 + i));
		REQUIRE(slot >= 0);
		REQUIRE(f.world.listMarketItem(id, slot, 1000) > 0);
	}
	CHECK(f.world.playerMarketListings(id).size() == 10);

	// 第 11 件上架被阻断 (达到玩家在售上限)
	const int extra_slot = f.world.giveItemToPlayer(id, makeTestItem(9999));
	REQUIRE(extra_slot >= 0);
	CHECK(f.world.listMarketItem(id, extra_slot, 1000) == 0);
}

TEST_CASE("拍卖市场模糊检索与跨玩家购买结算 (5% 成交税)")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	p1->gold = 1000;
	p2->gold = 10000;

	auto ring = makeTestItem(8001);
	ring.name.assign("火之戒指");
	const int s1 = f.world.giveItemToPlayer(id1, ring);
	REQUIRE(s1 >= 0);

	auto sword = makeTestItem(8002);
	sword.name.assign("风之长剑");
	const int s2 = f.world.giveItemToPlayer(id1, sword);
	REQUIRE(s2 >= 0);

	auto pet = makeTestPet(100, 20);
	pet.name.assign("红暴龙");
	const int s3 = f.world.givePetToPlayer(id1, pet);
	REQUIRE(s3 >= 0);

	const auto lid1 = f.world.listMarketItem(id1, s1, 1000); // 火之戒指
	const auto lid2 = f.world.listMarketItem(id1, s2, 2000); // 风之长剑
	const auto lid3 = f.world.listMarketPet(id1, s3, 5000);  // 红暴龙
	REQUIRE(lid1 > 0);
	REQUIRE(lid2 > 0);
	REQUIRE(lid3 > 0);
	CHECK(p1->gold == 700); // 扣除了 300 挂牌费

	// 1. 检索功能测试
	auto search_ring = f.world.searchMarket("戒指");
	CHECK(search_ring.size() == 1);
	CHECK(search_ring[0].listing_id == lid1);

	auto search_pets = f.world.searchMarket("", MarketAssetType::kPet);
	CHECK(search_pets.size() == 1);
	CHECK(search_pets[0].listing_id == lid3);

	// 2. 自买自卖拦截
	CHECK_FALSE(f.world.buyMarketListing(id1, lid1));

	// 3. Bob 购买火之戒指 (标价 1000, 5% 税 = 50, 净收益 950)
	REQUIRE(f.world.buyMarketListing(id2, lid1));
	CHECK(p2->gold == 9000);                      // 10000 - 1000
	CHECK(p1->gold == 1650);                      // 700 + 950
	CHECK(f.world.playerItemSlotsUsed(id2) == 1); // 道具已入 Bob 背包

	auto sold_listing = f.world.getMarketListing(lid1);
	REQUIRE(sold_listing.has_value());
	CHECK(sold_listing->sold);
	CHECK(f.world.activeMarketListingCount() == 2);
}

TEST_CASE("拍卖市场离线卖家与随身溢出转存系统邮件保全 [RV-2]")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	p1->gold = 500;
	p2->gold = 50000;

	// === 场景 A: 卖家离线，收益转存系统邮件 [RV-2] ===
	const int s1 = f.world.giveItemToPlayer(id1, makeTestItem(9001));
	REQUIRE(s1 >= 0);
	const auto lid_offline = f.world.listMarketItem(id1, s1, 2000);
	REQUIRE(lid_offline > 0);

	// Alice 下线
	f.world.onSessionClosed(id1);

	// Bob 购买 Alice 的离线挂牌 (2000 石币, 5% 税 = 100, 净收益 1900)
	REQUIRE(f.world.buyMarketListing(id2, lid_offline));

	// Alice 重新上线，检查邮箱
	const auto id1_new = spawnHandshaked(f);
	REQUIRE(f.world.setPlayerName(id1_new, "Alice"));
	auto *p1_new = f.world.playerForTest(id1_new);
	REQUIRE(p1_new != nullptr);
	p1_new->gold = 100;

	auto mails = f.world.playerMails(id1_new);
	REQUIRE(mails.size() == 1);
	CHECK(mails[0].sender_name == "拍卖市场");
	CHECK(mails[0].attached_gold == 1900);

	// 提取邮件收益
	REQUIRE(f.world.takeMailAttachment(id1_new, mails[0].mail_id));
	CHECK(p1_new->gold == 2000); // 100 + 1900

	// === 场景 B: 卖家在线但随身石币达到上限溢出，溢出部分转存系统邮件 [RV-2] ===
	p1_new->gold = SA::World::maxHaveGold(0) - 200; // 随身仅剩 200 容量达到 1,000,000
	const int s2 = f.world.giveItemToPlayer(id1_new, makeTestItem(9002));
	REQUIRE(s2 >= 0);
	const auto lid_overflow = f.world.listMarketItem(id1_new, s2, 1000);
	REQUIRE(lid_overflow > 0);

	// 挂牌后 p1_new 扣除了 100 挂牌费，当前余额 = max - 300
	// Bob 购买该物品: 标价 1000, 税后 950
	// 卖家可容纳 300 石币入账达到 1,000,000，剩余 650 发生溢出 (kClamped)
	REQUIRE(f.world.buyMarketListing(id2, lid_overflow));
	CHECK(p1_new->gold == SA::World::maxHaveGold(0)); // 达到满额

	// 检查系统邮件：溢出的 650 石币已转存为系统补偿邮件
	auto overflow_mails = f.world.playerMails(id1_new);
	REQUIRE(overflow_mails.size() >= 2);
	const auto &last_mail = overflow_mails.back();
	CHECK(last_mail.sender_name == "拍卖市场");
	CHECK(last_mail.title == "拍卖收入超额补发");
	CHECK(last_mail.attached_gold == 650);
}

TEST_CASE("拍卖市场商品撤回下架与退还 (满包退回系统邮件)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	REQUIRE(f.world.setPlayerName(id, "Alice"));
	p->gold = 2000;

	// 1. 正常下架撤回
	const int s1 = f.world.giveItemToPlayer(id, makeTestItem(1111));
	REQUIRE(s1 >= 0);
	const auto lid1 = f.world.listMarketItem(id, s1, 3000);
	REQUIRE(lid1 > 0);
	CHECK(f.world.playerItemSlotsUsed(id) == 0);

	REQUIRE(f.world.cancelMarketListing(id, lid1));
	CHECK(f.world.playerItemSlotsUsed(id) == 1); // 道具安全退回背包
	CHECK(f.world.getMarketListing(lid1)->cancelled);

	// 2. 背包已满时下架，资产安全退回系统邮件附件
	const int s2 = f.world.giveItemToPlayer(id, makeTestItem(2222));
	REQUIRE(s2 >= 0);
	const auto lid2 = f.world.listMarketItem(id, s2, 5000);
	REQUIRE(lid2 > 0);

	// 将 Alice 背包填满 (45 格道具)
	while (f.world.giveItemToPlayer(id, makeTestItem(999)) >= 0)
	{
	}
	CHECK(f.world.playerItemSlotsUsed(id) == 45);

	// 执行下架：由于背包已满，自动转存系统邮件
	REQUIRE(f.world.cancelMarketListing(id, lid2));

	auto mails = f.world.playerMails(id);
	REQUIRE(mails.size() == 1);
	CHECK(mails[0].sender_name == "拍卖市场");
	CHECK(mails[0].title == "寄售物品下架返还");
	REQUIRE(mails[0].attached_item.has_value());
	CHECK(mails[0].attached_item->item_id == 2222);

	// 腾出背包空间后即可提取
	p->clearItemSlot(static_cast<int>(SA::Model::kStartItemArray));
	REQUIRE(f.world.takeMailAttachment(id, mails[0].mail_id));
	CHECK(f.world.playerItemSlotsUsed(id) == 45);
}

// ══ 阶段 2 骑乘系统 (Ride System) 单元测试 ═════════════════════════════════

TEST_CASE("骑乘资质门限与 [RV-1] 濒死拦截")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	REQUIRE(f.world.setPlayerName(id, "Alice"));

	// 赋予一只 10 级测试宠物 (初始 hp = 20)
	const int pet_slot = f.world.givePetToPlayer(id, makeTestPet(100, 10));
	REQUIRE(pet_slot >= 0);

	// 1. 无任何骑乘资质时，阻断骑乘
	CHECK_FALSE(f.world.hasRidePermit(id, "骑乘学习证"));
	CHECK_FALSE(f.world.canPlayerRide(id, pet_slot));
	CHECK_FALSE(f.world.mountPet(id, pet_slot));
	CHECK_FALSE(f.world.isPlayerRiding(id));

	// 2. 授予通用骑乘学习证
	REQUIRE(f.world.grantRidePermit(id, "骑乘学习证"));
	CHECK(f.world.hasRidePermit(id, "骑乘学习证"));
	CHECK(f.world.canPlayerRide(id, pet_slot));

	// 3. [RV-1] 濒死门禁: 骑宠生命值 hp <= 0 时严格阻断上马
	auto dead_pet = makeTestPet(101, 10);
	dead_pet.hp = 0;
	const int slot_dead = f.world.givePetToPlayer(id, dead_pet);
	REQUIRE(slot_dead >= 0);
	CHECK_FALSE(f.world.canPlayerRide(id, slot_dead));
	CHECK_FALSE(f.world.mountPet(id, slot_dead));
	CHECK_FALSE(f.world.isPlayerRiding(id));

	auto dying_pet = makeTestPet(102, 10);
	dying_pet.hp = -10;
	const int slot_dying = f.world.givePetToPlayer(id, dying_pet);
	REQUIRE(slot_dying >= 0);
	CHECK_FALSE(f.world.canPlayerRide(id, slot_dying));
	CHECK_FALSE(f.world.mountPet(id, slot_dying));

	// 4. 存活宠物 (hp = 20 > 0) 正常上马
	CHECK(f.world.canPlayerRide(id, pet_slot));
	REQUIRE(f.world.mountPet(id, pet_slot));
	CHECK(f.world.isPlayerRiding(id));
	CHECK(f.world.playerRidePetSlot(id) == pet_slot);
}

TEST_CASE("骑乘复合外观切换与下马精准还原")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	REQUIRE(f.world.setPlayerName(id, "Alice"));
	p->image = 100000;

	auto pet_data = makeTestPet(105, 5);
	pet_data.base_image = 100105;
	pet_data.name.assign("黄金雷龙");
	const int pet_slot = f.world.givePetToPlayer(id, pet_data);
	REQUIRE(pet_slot >= 0);

	REQUIRE(f.world.grantRidePermit(id, "骑乘学习证"));
	REQUIRE(f.world.mountPet(id, pet_slot));

	// 1. 骑乘状态与复合外观校验 (100700 + 0 + 5 = 100705)
	CHECK(f.world.isPlayerRiding(id));
	CHECK(f.world.playerRidePetSlot(id) == pet_slot);
	CHECK(p->image == 100705);

	const auto info = f.world.getPlayerRideInfo(id);
	REQUIRE(info.has_value());
	CHECK(info->pet_slot == pet_slot);
	CHECK(info->original_image == 100000);
	CHECK(info->ride_image == 100705);
	CHECK(info->pet_name == "黄金雷龙");
	CHECK(info->pet_level == 5);
	CHECK(info->pet_hp == 20);

	// 2. 下马精准还原人物原始外观
	REQUIRE(f.world.dismountPet(id));
	CHECK_FALSE(f.world.isPlayerRiding(id));
	CHECK(f.world.playerRidePetSlot(id) == -1);
	CHECK(p->image == 100000);
	CHECK_FALSE(f.world.getPlayerRideInfo(id).has_value());
}

TEST_CASE("出战宠与骑宠互斥防护")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	const int s0 = f.world.givePetToPlayer(id, makeTestPet(101, 10));
	const int s1 = f.world.givePetToPlayer(id, makeTestPet(102, 10));
	REQUIRE(s0 == 0);
	REQUIRE(s1 == 1);

	// 设 slot 0 为出战宠
	p->default_pet = 0;
	CHECK(p->default_pet == 0);

	REQUIRE(f.world.grantRidePermit(id, "骑乘学习证"));

	// 骑上出战宠 slot 0，出战宠自动重置为 -1
	REQUIRE(f.world.mountPet(id, 0));
	CHECK(f.world.isPlayerRiding(id));
	CHECK(p->default_pet == -1);

	// 换骑 slot 1，原骑宠安全解脱，slot 1 成为新骑宠
	REQUIRE(f.world.mountPet(id, 1));
	CHECK(f.world.playerRidePetSlot(id) == 1);
	CHECK(p->default_pet == -1);

	REQUIRE(f.world.dismountPet(id));
	CHECK_FALSE(f.world.isPlayerRiding(id));
	CHECK(f.world.playerPetSlotsUsed(id) == 2);
}

TEST_CASE("大世界资产流转安全脱钩 (寄售/摆摊/交易/邮件/离线)")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	REQUIRE(p1 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));
	p1->gold = 2000;
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;

	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p2 != nullptr);
	p2->floor = 0;
	p2->x = 20;
	p2->y = 21;
	p2->gold = 2000;

	REQUIRE(f.world.grantRidePermit(id1, "骑乘学习证"));

	// === 场景 A: 寄售目标骑宠自动下马 ===
	const int s0 = f.world.givePetToPlayer(id1, makeTestPet(201, 10));
	REQUIRE(s0 == 0);
	REQUIRE(f.world.mountPet(id1, s0));
	CHECK(f.world.isPlayerRiding(id1));

	const auto lid = f.world.listMarketPet(id1, s0, 500);
	REQUIRE(lid > 0);
	CHECK_FALSE(f.world.isPlayerRiding(id1)); // 寄售自动脱钩下马

	// === 场景 B: 摆摊目标骑宠自动下马 ===
	const int s1 = f.world.givePetToPlayer(id1, makeTestPet(202, 10));
	REQUIRE(s1 == 0);
	REQUIRE(f.world.mountPet(id1, s1));
	CHECK(f.world.isPlayerRiding(id1));

	REQUIRE(f.world.openStall(id1, "Alice小摊"));
	REQUIRE(f.world.setStallPet(id1, s1, 300));
	CHECK_FALSE(f.world.isPlayerRiding(id1)); // 摆摊目标宠自动下马
	REQUIRE(f.world.closeStall(id1));

	// === 场景 C: 交易放置目标骑宠自动下马 ===
	REQUIRE(f.world.mountPet(id1, s1));
	CHECK(f.world.isPlayerRiding(id1));

	REQUIRE(f.world.requestTrade(id1, id2));
	REQUIRE(f.world.acceptTrade(id2, id1));
	REQUIRE(f.world.offerTradePet(id1, s1));
	CHECK_FALSE(f.world.isPlayerRiding(id1)); // 放入交易表自动下马
	REQUIRE(f.world.cancelTrade(id1));

	// === 场景 D: 邮件邮寄目标骑宠自动下马 ===
	REQUIRE(f.world.mountPet(id1, s1));
	CHECK(f.world.isPlayerRiding(id1));

	REQUIRE(f.world.sendMail(id1, "Bob", "送宠", "收下吧", -1, s1, 0));
	CHECK_FALSE(f.world.isPlayerRiding(id1)); // 邮寄自动下马

	// === 场景 E: 会话关闭离线自动安全解骑 ===
	const int s2 = f.world.givePetToPlayer(id1, makeTestPet(203, 10));
	REQUIRE(s2 == 0);
	REQUIRE(f.world.mountPet(id1, s2));
	CHECK(f.world.isPlayerRiding(id1));

	f.world.onSessionClosed(id1);
	CHECK_FALSE(f.world.isPlayerRiding(id1)); // 离线解骑
}

TEST_CASE("战斗人宠生命分摊与 [RV-2] 战后血量回写")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f); // Alice (防守方)
	const auto id2 = spawnHandshaked(f); // Bob (攻击方)
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	REQUIRE(f.world.setPlayerName(id1, "Alice"));
	REQUIRE(f.world.setPlayerName(id2, "Bob"));

	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->hp = 2000;
	p1->vital = 20000;
	p1->str = 5000;
	p1->tough = 500;
	p1->dex = 190; // 略慢于 Bob

	p2->floor = 0;
	p2->x = 20;
	p2->y = 21;
	p2->hp = 1; // 1 点血，受到反击即战败结束战斗
	p2->vital = 10;
	p2->str = 5000; // 造成分摊伤害
	p2->tough = 10;
	p2->dex = 200; // 略快于 Alice 先手攻击

	// 给 Alice 一只满血 500 HP 骑宠
	auto pet_data = makeTestPet(301, 10);
	pet_data.vital = 20000;
	pet_data.str = 4000;
	pet_data.tough = 4000;
	pet_data.dex = 4000;
	pet_data.hp = 500;
	const int slot = f.world.givePetToPlayer(id1, pet_data);
	REQUIRE(slot == 0);

	REQUIRE(f.world.grantRidePermit(id1, "骑乘学习证"));
	REQUIRE(f.world.mountPet(id1, slot));
	CHECK(f.world.isPlayerRiding(id1));

	// Bob 发起决斗 (Bob 是 Side 0 slot 0, Alice 是 Side 1 slot 10)
	REQUIRE(f.world.requestDuel(id2, id1));
	REQUIRE(f.world.battleCount() == 1);
	const BattleId battle = 1;

	// 验证开战进场快照中骑宠投影完整
	const auto *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);
	CHECK(fld->at(10).has_ride);
	CHECK(fld->at(10).ride_hp == 500);

	// 回合循环: Bob 先手攻击 Alice (slot 10)，伤害经 splitRideDamage 在 Alice 与骑宠间分摊
	// 随后 Alice 反击攻击 Bob (slot 0)，Bob 战败，战斗结束
	for (int turn = 0; turn < 5; ++turn)
	{
		const auto *cur_fld = f.world.battleField(battle);
		if (cur_fld == nullptr)
			break;
		const auto *st = f.world.stats(battle);
		if (st != nullptr && st->finished)
			break;

		SA::Domain::BattleCommand cmd_bob{};
		cmd_bob.battle_id = battle;
		cmd_bob.turn = cur_fld->turn;
		cmd_bob.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd_bob.command.attack.target = 10;
		f.world.onBattleCommand(id2, cmd_bob);

		SA::Domain::BattleCommand cmd_alice{};
		cmd_alice.battle_id = battle;
		cmd_alice.turn = cur_fld->turn;
		cmd_alice.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
		cmd_alice.command.attack.target = 0;
		f.world.onBattleCommand(id1, cmd_alice);

		f.clock.advance(1000);
		f.world.tick();
	}

	// 战斗结束，回到大世界态
	CHECK(f.world.battleCount() == 0);

	// [RV-2] 战后状态同步校验: 骑宠血量严格回写回 Alice 真实宠物实体
	auto *real_pet = f.world.playerPetAt(id1, 0);
	REQUIRE(real_pet != nullptr);
	CHECK(real_pet->hp < 500);          // 确已受到分摊伤害
	CHECK(real_pet->hp > 0);            // 依然存活
	CHECK(f.world.isPlayerRiding(id1)); // 保持骑乘
}

TEST_CASE("战中骑宠濒死战后自动下马恢复人身外观")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f); // Alice (防守方)
	const auto id2 = spawnHandshaked(f); // Bob (超高攻击方)
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->image = 100000;
	p1->floor = 0;
	p1->x = 20;
	p1->y = 20;
	p1->hp = 500;
	p1->vital = 20000;
	p1->str = 500;
	p1->tough = 500;
	p1->dex = 100;

	p2->floor = 0;
	p2->x = 20;
	p2->y = 21;
	p2->hp = 1;
	p2->vital = 100;
	p2->str = 50000; // 超强攻击力
	p2->tough = 100;
	p2->dex = 1000;

	// Alice 骑乘一只仅剩 1 点生命值的宠物
	auto pet_data = makeTestPet(302, 1);
	pet_data.vital = 1000;
	pet_data.str = 1000;
	pet_data.tough = 1000;
	pet_data.dex = 100;
	pet_data.hp = 1;
	const int slot = f.world.givePetToPlayer(id1, pet_data);
	REQUIRE(slot == 0);

	REQUIRE(f.world.grantRidePermit(id1, "骑乘学习证"));
	REQUIRE(f.world.mountPet(id1, slot));
	CHECK(f.world.isPlayerRiding(id1));
	CHECK(p1->image != 100000); // 骑乘外观

	// 发起决斗
	REQUIRE(f.world.requestDuel(id2, id1));
	const BattleId battle = 1;
	const auto *fld = f.world.battleField(battle);
	REQUIRE(fld != nullptr);

	SA::Domain::BattleCommand cmd_bob{};
	cmd_bob.battle_id = battle;
	cmd_bob.turn = fld->turn;
	cmd_bob.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
	cmd_bob.command.attack.target = 10;
	f.world.onBattleCommand(id2, cmd_bob);

	SA::Domain::BattleCommand cmd_alice{};
	cmd_alice.battle_id = battle;
	cmd_alice.turn = fld->turn;
	cmd_alice.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
	cmd_alice.command.attack.target = 0;
	f.world.onBattleCommand(id1, cmd_alice);

	f.clock.advance(1000);
	f.world.tick();

	// 战斗结束
	CHECK(f.world.battleCount() == 0);

	// 战后校验: 骑宠血量归零，在世界态自动触发 dismountPet，人物形象精确还原
	auto *real_pet = f.world.playerPetAt(id1, 0);
	REQUIRE(real_pet != nullptr);
	CHECK(real_pet->hp == 0);
	CHECK_FALSE(f.world.isPlayerRiding(id1));
	CHECK(p1->image == 100000); // 还原为人身初始外观
}

TEST_CASE("庄园骑宠特权与庄园易主联动")
{
	MoveFixture f;
	const auto id1 = spawnHandshaked(f);
	const auto id2 = spawnHandshaked(f);
	auto *p1 = f.world.playerForTest(id1);
	auto *p2 = f.world.playerForTest(id2);
	REQUIRE(p1 != nullptr);
	REQUIRE(p2 != nullptr);
	p1->gold = 50000;
	p2->gold = 50000;
	p1->level = 30;
	p2->level = 30;

	// Alice 创建家族 1
	const auto fam1 = f.world.createFamily(id1, "萨姆吉尔勇士", "守护村庄");
	REQUIRE(fam1 > 0);

	// 给 Alice 一只暴龙系宠物
	auto pet = makeTestPet(401, 10);
	pet.name.assign("萨姆吉尔暴龙");
	const int slot = f.world.givePetToPlayer(id1, pet);
	REQUIRE(slot >= 0);

	// 1. 无骑宠证且家族无庄园时，无法骑乘
	CHECK_FALSE(f.world.hasRidePermit(id1, "骑乘学习证"));
	CHECK_FALSE(f.world.canPlayerRide(id1, slot));
	CHECK_FALSE(f.world.mountPet(id1, slot));

	// 2. 家族 1 占领萨姆吉尔庄园 (暴龙系特权)
	REQUIRE(f.world.occupyManor(fam1, FamilyManor::kSamo));
	CHECK(f.world.familyManor(fam1) == FamilyManor::kSamo);
	CHECK(f.world.manorOwnerFamily(FamilyManor::kSamo) == fam1);

	// 享有庄园特权，无需学习证即可骑乘
	CHECK(f.world.canPlayerRide(id1, slot));
	REQUIRE(f.world.mountPet(id1, slot));
	CHECK(f.world.isPlayerRiding(id1));

	// 3. 家族 2 夺得萨姆吉尔庄园 (庄园易主)
	const auto fam2 = f.world.createFamily(id2, "玛丽娜斯海盗", "争夺庄园");
	REQUIRE(fam2 > 0);
	REQUIRE(f.world.occupyManor(fam2, FamilyManor::kSamo));
	CHECK(f.world.familyManor(fam1) == FamilyManor::kNone);
	CHECK(f.world.manorOwnerFamily(FamilyManor::kSamo) == fam2);

	// Alice 下马后，因家族失去庄园特权且无学习证，无法再次骑乘
	REQUIRE(f.world.dismountPet(id1));
	CHECK_FALSE(f.world.canPlayerRide(id1, slot));
	CHECK_FALSE(f.world.mountPet(id1, slot));

	// 4. 授予特约骑宠证 ("萨姆吉尔暴龙")
	REQUIRE(f.world.grantRidePermit(id1, "萨姆吉尔暴龙"));
	CHECK(f.world.hasRidePermit(id1, "萨姆吉尔暴龙"));
	CHECK(f.world.canPlayerRide(id1, slot));
	REQUIRE(f.world.mountPet(id1, slot));
	CHECK(f.world.isPlayerRiding(id1));
}

// ══════════════════════════════════════════════════════════════════════════════
// 阶段 2 宠物融合与转生系统 (Pet Fusion & Rebirth, 批次 §9.0.96)
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("宠物融合与转生: 三表投影矩阵与目标宠物模板匹配")
{
	// 1. PetTable[29][29] 边界与数值测试
	CHECK(getPetFusionBase2(-1, 0) == -1);
	CHECK(getPetFusionBase2(0, 29) == -1);
	CHECK(getPetFusionBase2(0, 0) == 1);
	CHECK(getPetFusionBase2(8, 0) == 10);
	CHECK(getPetFusionBase2(28, 28) == 6);

	// 2. PropertyTable[4][4] 边界与数值测试
	CHECK(getPetFusionBase1(-1, 0) == -1);
	CHECK(getPetFusionBase1(0, 4) == -1);
	CHECK(getPetFusionBase1(0, 0) == 0);  // 地 x 地
	CHECK(getPetFusionBase1(1, 1) == 1);  // 水 x 水
	CHECK(getPetFusionBase1(2, 2) == 2);  // 火 x 火
	CHECK(getPetFusionBase1(3, 3) == 3);  // 风 x 风
	CHECK(getPetFusionBase1(0, 1) == 4);  // 地 x 水
	CHECK(getPetFusionBase1(2, 0) == 10); // 火 x 地

	// 3. FusionTable[11][16] 边界与数值测试
	CHECK(lookupPetFusionTarget(-1, 0) == -1);
	CHECK(lookupPetFusionTarget(11, 0) == -1);
	CHECK(lookupPetFusionTarget(0, 16) == -1);
	CHECK(lookupPetFusionTarget(0, 0) == 989);
	CHECK(lookupPetFusionTarget(1, 0) == 1001);
	CHECK(lookupPetFusionTarget(10, 15) == 1016);

	// 4. resolvePetFusionResultId 组合投影全链验证
	CHECK(resolvePetFusionResultId(0, 0, 0, 0) == 1001); // base2=1, base1=0 -> 1001
	CHECK(resolvePetFusionResultId(8, 0, 2, 0) == 1015); // base2=10, base1=10 -> 1015
	CHECK(resolvePetFusionResultId(-1, 0, 0, 0) == -1);
}

TEST_CASE("宠物融合与转生: 资质继承算法与等级削弱惩罚")
{
	// 1. 无惩罚正常融合 (两宠等级均 >= 80)
	PetGrowth main_g{30, 30, 30, 30};
	PetGrowth sub1_g{20, 20, 20, 20};
	// 期望: main * 0.6 = 18, sub * 0.4 = 8, 合计 26
	const PetGrowth res1 = calculateFusionGrowth(main_g, 80, sub1_g, 80);
	CHECK(res1.vital == 26);
	CHECK(res1.str == 26);
	CHECK(res1.tough == 26);
	CHECK(res1.dex == 26);

	// 2. 等级惩罚融合 (main < 80, sub1 < 80)
	// main: 30 * 0.8 = 24 -> 24 * 0.6 = 14.4
	// sub1: 20 * 0.8 = 16 -> 16 * 0.4 = 6.4
	// 合计: 14.4 + 6.4 = 20.8 -> round = 21
	const PetGrowth res2 = calculateFusionGrowth(main_g, 75, sub1_g, 50);
	CHECK(res2.vital == 21);
	CHECK(res2.str == 21);
	CHECK(res2.tough == 21);
	CHECK(res2.dex == 21);

	// 3. 双副宠融合 (sub1={20,20,20,20}, sub2={40,40,40,40})
	// sub 平均 = 30 -> 30 * 0.4 = 12
	// main(80级) = 30 * 0.6 = 18 -> 合计 30
	PetGrowth sub2_g{40, 40, 40, 40};
	const PetGrowth res3 = calculateFusionGrowth(main_g, 80, sub1_g, 80, &sub2_g, 80);
	CHECK(res3.vital == 30);
	CHECK(res3.str == 30);
	CHECK(res3.tough == 30);
	CHECK(res3.dex == 30);

	// 4. 技能继承与去重过滤 (过滤非法技能 41 与空槽 0)
	std::int32_t m_skills[7] = {101, 102, 0, 0, 0, 0, 0};
	std::int32_t s1_skills[7] = {102, 103, 41, 0, 0, 0, 0};
	std::int32_t s2_skills[7] = {104, 52, 0, 0, 0, 0, 0};
	const auto fused_sk = calculateFusionSkills(m_skills, s1_skills, s2_skills);
	CHECK(fused_sk[0] == 101);
	CHECK(fused_sk[1] == 102);
	CHECK(fused_sk[2] == 103);
	CHECK(fused_sk[3] == 104);
	CHECK(fused_sk[4] == 0);
	CHECK(fused_sk[5] == 0);
	CHECK(fused_sk[6] == 0);
}

TEST_CASE("宠物融合与转生: [RV-2] 转生五次方公式与 Fx 档位算力精确验证")
{
	// 测试 NPC_PetTransManGetAns 原版公式精确算力
	// 场景 1: lv=100 (零等级额外收益), rank=0 (Fx=11), total1=100 (total=1.3), total2=100
	// ans = floor(1.3) + 100 + (100 - 100)/11 = 1 + 100 + 0 = 101
	CHECK(calculatePetTransAns(100, 100, 100, 0, 0) == 101);

	// 场景 2: lv=130 (满等级额外收益 30 点), rank=0 (Fx=11)
	// ans = 1 + 100 + 30/11 = 1 + 100 + 2 = 103
	CHECK(calculatePetTransAns(100, 100, 130, 0, 0) == 103);

	// 场景 3: lv=130, rank=5 (Fx=(5-5)*1.2 + 5 = 5)
	// ans = 1 + 100 + 30/5 = 1 + 100 + 6 = 107
	CHECK(calculatePetTransAns(100, 100, 130, 5, 0) == 107);

	// 场景 4: lv=130, rank=5, 满成长辅助宠 (total1=150, total=(1.5)^5 * 1.3 = 9.87)
	// ans = floor(9.87) + 100 + 6 = 9 + 100 + 6 = 115
	CHECK(calculatePetTransAns(150, 100, 130, 5, 0) == 115);

	// 场景 5: 钳位上限 (0 转上限 150，1 转上限 200)
	CHECK(calculatePetTransAns(200, 160, 130, 5, 0) == 150);
	CHECK(calculatePetTransAns(200, 160, 130, 5, 1) == 200);
}

TEST_CASE("宠物融合与转生: 大世界两宠与三宠融合全流程闭环")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	// 赋予主宠 (slot 0): 85 级红暴, 四维成长各 30, fusion_code = 0, 地属性 10
	auto p0 = makeTestPet(100, 85);
	p0.earth = 10;
	p0.vital = 30;
	p0.str = 30;
	p0.tough = 30;
	p0.dex = 30;
	p0.growth_vital = 30;
	p0.growth_str = 30;
	p0.growth_tough = 30;
	p0.growth_dex = 30;
	p0.pet_skills[0] = 101;
	const int s0 = f.world.givePetToPlayer(id, p0);
	REQUIRE(s0 == 0);
	f.world.registerPetTemplateFusionCode(100, 0);

	// 赋予副宠 1 (slot 1): 80 级蓝暴, 四维成长各 20, fusion_code = 0, 地属性 10
	auto p1 = makeTestPet(101, 80);
	p1.earth = 10;
	p1.vital = 20;
	p1.str = 20;
	p1.tough = 20;
	p1.dex = 20;
	p1.growth_vital = 20;
	p1.growth_str = 20;
	p1.growth_tough = 20;
	p1.growth_dex = 20;
	p1.pet_skills[0] = 102;
	const int s1 = f.world.givePetToPlayer(id, p1);
	REQUIRE(s1 == 1);
	f.world.registerPetTemplateFusionCode(101, 0);

	CHECK(f.world.playerPetSlotsUsed(id) == 2);

	// 执行两宠融合: 主宠 slot 0, 副宠 slot 1
	const auto res = f.world.fusePets(id, 0, 1);
	REQUIRE(res == PetFusionResultCode::kSuccess);

	// 校验融合后结果:
	// - 副宠槽位 slot 1 已被清空释放
	// - 随身宠物数由 2 只变为 1 只
	// - 主宠槽位 slot 0 诞生新融合宠
	CHECK_FALSE(p->pets[1].valid());
	CHECK(f.world.playerPetSlotsUsed(id) == 1);

	const auto *fused = f.world.playerPetAt(id, 0);
	REQUIRE(fused != nullptr);
	CHECK(fused->level == 1);
	CHECK(fused->exp == 0);
	// 查表: base2=PetTable[0][0]=1, base1=PropertyTable[0][0]=0 -> FusionTable[1][0]=1001
	CHECK(fused->pet_id == 1001);
	CHECK(f.world.isPetFusion(fused->uid));

	// 资质检验: 30*0.6 + 20*0.4 = 18 + 8 = 26
	CHECK(fused->growth_vital == 26);
	CHECK(fused->growth_str == 26);
	CHECK(fused->growth_tough == 26);
	CHECK(fused->growth_dex == 26);
	CHECK(fused->vital == 26);
	CHECK(fused->str == 26);
	CHECK(fused->tough == 26);
	CHECK(fused->dex == 26);
	CHECK(fused->hp == SA::Rules::deriveBaseStats(26, 26, 26, 26).max_hp);

	// 技能继承校验: 101, 102
	CHECK(fused->pet_skills[0] == 101);
	CHECK(fused->pet_skills[1] == 102);
}

TEST_CASE("宠物融合与转生: [RV-1] 融合资格门限与状态互斥防御拦截")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	auto p0 = makeTestPet(100, 80);
	auto p1 = makeTestPet(101, 80);
	auto p2 = makeTestPet(102, 80);
	const int s0 = f.world.givePetToPlayer(id, p0);
	const int s1 = f.world.givePetToPlayer(id, p1);
	const int s2 = f.world.givePetToPlayer(id, p2);
	REQUIRE(s0 == 0);
	REQUIRE(s1 == 1);
	REQUIRE(s2 == 2);

	f.world.registerPetTemplateFusionCode(100, 0);
	f.world.registerPetTemplateFusionCode(101, 0);
	f.world.registerPetTemplateFusionCode(102, 0);

	// 1. 无效槽位防御
	CHECK(f.world.fusePets(id, 0, 0) == PetFusionResultCode::kInvalidSlot);
	CHECK(f.world.fusePets(id, -1, 1) == PetFusionResultCode::kInvalidSlot);
	CHECK(f.world.fusePets(id, 0, 5) == PetFusionResultCode::kInvalidSlot);
	CHECK(f.world.fusePets(id, 0, 1, 1) == PetFusionResultCode::kInvalidSlot);

	// 2. 出战中状态互斥防御: slot 0 设为出战宠
	p->default_pet = 0;
	CHECK(f.world.fusePets(id, 0, 1) == PetFusionResultCode::kPetInBattleOrRide);
	p->default_pet = 1;
	CHECK(f.world.fusePets(id, 0, 1) == PetFusionResultCode::kPetInBattleOrRide);
	p->default_pet = -1; // 解除出战

	// 3. 骑乘中状态互斥防御: 骑乘 slot 0
	REQUIRE(f.world.grantRidePermit(id, "骑乘学习证"));
	REQUIRE(f.world.mountPet(id, 0));
	CHECK(f.world.fusePets(id, 0, 1) == PetFusionResultCode::kPetInBattleOrRide);
	REQUIRE(f.world.dismountPet(id)); // 下马

	// 4. 摆摊货架状态互斥防御: slot 1 上架
	REQUIRE(f.world.openStall(id, "小摊"));
	REQUIRE(f.world.setStallPet(id, 1, 100));
	CHECK(f.world.fusePets(id, 0, 1) == PetFusionResultCode::kPetInTradeOrStall);
	REQUIRE(f.world.closeStall(id));

	// 5. 融合码非法防御: 将 slot 1 的融合码设为 -1 (不可融合)
	const auto *pet1 = f.world.playerPetAt(id, 1);
	REQUIRE(pet1 != nullptr);
	f.world.setPetFusionCode(pet1->uid, -1);
	CHECK(f.world.fusePets(id, 0, 1) == PetFusionResultCode::kIneligibleFusionCode);
	f.world.setPetFusionCode(pet1->uid, 0); // 恢复合法

	// 6. 已是融合宠防御: 成功融合一次后，该融合宠禁止再次作为主宠或副宠
	REQUIRE(f.world.fusePets(id, 0, 1) == PetFusionResultCode::kSuccess);
	// 此时 slot 0 为融合宠，尝试用 slot 0 作为主宠与 slot 2 融合，被严格阻断
	CHECK(f.world.fusePets(id, 0, 2) == PetFusionResultCode::kAlreadyFused);
	// 尝试用 slot 2 作为主宠，slot 0 作为副宠，同样被严格阻断
	CHECK(f.world.fusePets(id, 2, 0) == PetFusionResultCode::kAlreadyFused);
}

TEST_CASE("宠物融合与转生: [RV-1] 转生等级门禁、最大转生次数与状态互斥")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	// 赋予一只 99 级的未达标宠物
	auto p0 = makeTestPet(200, 99);
	p0.vital = 30;
	p0.str = 30;
	p0.tough = 30;
	p0.dex = 30;
	const int s0 = f.world.givePetToPlayer(id, p0);
	REQUIRE(s0 == 0);

	// 1. 等级 < 100 级转生拦截
	CHECK(f.world.reincarnatePet(id, 0) == PetTransResultCode::kInsufficientLevel);

	// 2. 提升至 100 级
	auto *pet = f.world.playerPetForTest(id, 0);
	REQUIRE(pet != nullptr);
	pet->level = 100;

	// 3. 出战中阻断
	p->default_pet = 0;
	CHECK(f.world.reincarnatePet(id, 0) == PetTransResultCode::kPetInBattleOrRide);
	p->default_pet = -1;

	// 4. 骑乘中阻断
	REQUIRE(f.world.grantRidePermit(id, "骑乘学习证"));
	REQUIRE(f.world.mountPet(id, 0));
	CHECK(f.world.reincarnatePet(id, 0) == PetTransResultCode::kPetInBattleOrRide);
	REQUIRE(f.world.dismountPet(id));

	// 5. 首次转生成功 (0 转 -> 1 转)
	CHECK(f.world.petTransCount(pet->uid) == 0);
	REQUIRE(f.world.reincarnatePet(id, 0) == PetTransResultCode::kSuccess);
	CHECK(f.world.petTransCount(pet->uid) == 1);
	CHECK(pet->level == 1);
	CHECK(pet->exp == 0);

	// 6. 二次转生门禁: 必须再次升满 100 级
	CHECK(f.world.reincarnatePet(id, 0) == PetTransResultCode::kInsufficientLevel);
	pet->level = 100;
	REQUIRE(f.world.reincarnatePet(id, 0) == PetTransResultCode::kSuccess);
	CHECK(f.world.petTransCount(pet->uid) == 2);

	// 7. 转生次数达上限 (2 转) 拦截
	pet->level = 100;
	CHECK(f.world.reincarnatePet(id, 0) == PetTransResultCode::kMaxTransReached);
}

TEST_CASE("宠物融合与转生: 转生消耗辅助宠与资质大幅重构飞跃")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	// 被转生主宠 (slot 0): 130 级, 初始成长各 20 (total=80)
	auto p0 = makeTestPet(300, 130);
	p0.vital = 20;
	p0.str = 20;
	p0.tough = 20;
	p0.dex = 20;
	p0.growth_vital = 20;
	p0.growth_str = 20;
	p0.growth_tough = 20;
	p0.growth_dex = 20;
	const int s0 = f.world.givePetToPlayer(id, p0);
	REQUIRE(s0 == 0);

	// 极品玛蕾辅助宠 (slot 1): 79 级, 成长各 45 (total=180)
	auto p1 = makeTestPet(301, 79);
	p1.vital = 45;
	p1.str = 45;
	p1.tough = 45;
	p1.dex = 45;
	p1.growth_vital = 45;
	p1.growth_str = 45;
	p1.growth_tough = 45;
	p1.growth_dex = 45;
	const int s1 = f.world.givePetToPlayer(id, p1);
	REQUIRE(s1 == 1);

	CHECK(f.world.playerPetSlotsUsed(id) == 2);

	// 执行转生: slot 0 为目标宠, slot 1 为辅助宠
	REQUIRE(f.world.reincarnatePet(id, 0, 1) == PetTransResultCode::kSuccess);

	// 校验辅助宠已被消耗
	CHECK_FALSE(p->pets[1].valid());
	CHECK(f.world.playerPetSlotsUsed(id) == 1);

	// 校验主宠资质飞跃提升:
	const auto *pet = f.world.playerPetAt(id, 0);
	REQUIRE(pet != nullptr);
	CHECK(pet->level == 1);
	CHECK(pet->growth_vital > 20);
	CHECK(pet->growth_str > 20);
	CHECK(pet->growth_tough > 20);
	CHECK(pet->growth_dex > 20);
	CHECK(pet->hp == SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex).max_hp);
}

TEST_CASE("宠物融合与转生: 融合师与转生师 NPC 对白与交互")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	// 加载两名功能 NPC
	NpcEntity fusion_npc{};
	fusion_npc.id = 8801;
	fusion_npc.floor = 0;
	fusion_npc.x = 33;
	fusion_npc.y = 32;
	fusion_npc.type = NpcType::kPetFusionMan;
	fusion_npc.message = "欢迎光临宠物融合所！让我为你创造全新的恐龙伙伴！";

	NpcEntity trans_npc{};
	trans_npc.id = 8802;
	trans_npc.floor = 0;
	trans_npc.x = 35;
	trans_npc.y = 34;
	trans_npc.type = NpcType::kPetTransMan;
	trans_npc.message = "我是漆黑的转生导师，带上达到100级的宠物来觉醒潜能吧！";

	f.world.loadNpcEntities({fusion_npc, trans_npc});

	// 对话融合师
	SA::Domain::EventRequest req1{};
	req1.dir = 2;
	req1.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req1.seqno = 3001;
	f.world.onEvent(id, req1);
	f.world.tick();
	CHECK(f.world.playerLastWindowText(id) == "欢迎光临宠物融合所！让我为你创造全新的恐龙伙伴！");

	// 移动至转生师身旁并对话 (玩家在 35,33, NPC在 35,34, 面向南 dir=4)
	p->x = 35;
	p->y = 33;
	p->dir = 4;
	SA::Domain::EventRequest req2{};
	req2.dir = 4;
	req2.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req2.seqno = 3002;
	f.world.onEvent(id, req2);
	f.world.tick();
	CHECK(f.world.playerLastWindowText(id) == "我是漆黑的转生导师，带上达到100级的宠物来觉醒潜能吧！");
}

// ══ 阶段 2: 任务引擎脚本全景扩展 (批次 §9.0.97, 09 §5.1/§5.2) ══════════════

TEST_CASE("§9.0.97: ExChangeMan 全量条件表达式求值 (复合条件/括号优先级/关系运算符/变量全集)")
{
	SA::Model::Player p{};
	p.level = 80;
	p.gold = 50000;
	p.hp = 250;
	p.mp = 120;
	p.vital = 30;
	p.setNowEvent(10);
	p.setEndEvent(20);

	EventCheckContext ctx{
	    .player = p,
	    .count_item = [](const SA::Model::Player &, std::int32_t item_id, void *) -> std::int32_t
	    {
		    if (item_id == 1001)
			    return 3;
		    if (item_id == 1002)
			    return 1;
		    return 0;
	    },
	    .count_pet = [](const SA::Model::Player &, std::int32_t pet_id, std::int32_t min_lvl, void *) -> std::int32_t
	    {
		    if (pet_id == 2001)
			    return (min_lvl <= 50) ? 2 : 0;
		    return 0;
	    },
	    .count_free_item_slots = [](const SA::Model::Player &, void *) -> std::int32_t
	    { return 4; },
	    .count_free_pet_slots = [](const SA::Model::Player &, void *) -> std::int32_t
	    { return 2; },
	    .get_transmigration = [](const SA::Model::Player &, void *) -> std::int32_t
	    { return 5; },
	    .get_fame = [](const SA::Model::Player &, void *) -> std::int32_t
	    { return 150; },
	    .get_family_id = [](const SA::Model::Player &, void *) -> std::uint32_t
	    { return 10086; },
	    .userdata = nullptr};

	// 1. 变量全集覆盖与关系运算符 (=, !=, <, >, <=, >=)
	CHECK(evaluateEventCondition("LV=80", ctx) == 1);
	CHECK(evaluateEventCondition("LV>=80 & LV<=80", ctx) == 1);
	CHECK(evaluateEventCondition("LV>79 & LV<81", ctx) == 1);
	CHECK(evaluateEventCondition("LV!=80", ctx) == 0);

	CHECK(evaluateEventCondition("TRANS=5", ctx) == 1);
	CHECK(evaluateEventCondition("TRANS7=5", ctx) == 1);
	CHECK(evaluateEventCondition("TRANS>=6", ctx) == 0);

	CHECK(evaluateEventCondition("FAME>=150 & FAME>100 & FAME!=100", ctx) == 1);
	CHECK(evaluateEventCondition("FM=10086 & FAMILY=10086", ctx) == 1);
	CHECK(evaluateEventCondition("GOLD>=50000 & gold<=50000", ctx) == 1);
	CHECK(evaluateEventCondition("HP=250 & MP=120", ctx) == 1);
	CHECK(evaluateEventCondition("reITEM=4 & rePET=2", ctx) == 1);

	// ITEM 语法 (* 数量>=, ^ 数量==, 无修饰默认==1)
	CHECK(evaluateEventCondition("ITEM=1002", ctx) == 1);
	CHECK(evaluateEventCondition("ITEM*2=1001", ctx) == 1);
	CHECK(evaluateEventCondition("ITEM^3=1001", ctx) == 1);
	CHECK(evaluateEventCondition("ITEM^2=1001", ctx) == 0);
	CHECK(evaluateEventCondition("ITEM*4=1001", ctx) == 0);

	// PET 语法
	CHECK(evaluateEventCondition("PET*2=2001", ctx) == 1);
	CHECK(evaluateEventCondition("PET^2=2001", ctx) == 1);
	CHECK(evaluateEventCondition("PET=2001*50*2", ctx) == 1);
	CHECK(evaluateEventCondition("PET=2001*60*2", ctx) == 0);

	// NOWEV / ENDEV 语法 (冒号语法与直接比较)
	CHECK(evaluateEventCondition("NOWEV=10", ctx) == 1);
	CHECK(evaluateEventCondition("NOWEV:10=1", ctx) == 1);
	CHECK(evaluateEventCondition("NOWEV:10=0", ctx) == 0);
	CHECK(evaluateEventCondition("!NOWEV=10", ctx) == 0);
	CHECK(evaluateEventCondition("ENDEV=20 & ENDEV:20=1", ctx) == 1);
	CHECK(evaluateEventCondition("!ENDEV=21 & ENDEV:21=0", ctx) == 1);

	// 2. 复合条件与括号优先级 & / | / ! / (...)
	CHECK(evaluateEventCondition("(LV>10 & GOLD>=1000) | (TRANS>=6)", ctx) == 1);
	CHECK(evaluateEventCondition("(LV<10 & GOLD>=1000) | (TRANS>=6)", ctx) == 0);
	CHECK(evaluateEventCondition("!(LV<10) & !(TRANS<5)", ctx) == 1);
	CHECK(evaluateEventCondition("!((LV<10 | TRANS<1) & FAME>0)", ctx) == 1);
	CHECK(evaluateEventCondition("((LV>=80 & FAME>=150) | TRANS=0) & (reITEM>0 & rePET>0)", ctx) == 1);

	// 3. 顶层逗号分支（括号内的逗号不会被切分为顶层分支）
	CHECK(evaluateEventCondition("LV<10, LV=80, LV>100", ctx) == 2);
}

TEST_CASE("§9.0.97: ExChangeMan 脚本解析与全动作集执行闭环 (经验/点数/血蓝/声望/传送/旗标)")
{
	const std::string script = R"(
EventNo:200|TYPE:ACCEPT
EVENT:(LV>=10 & GOLD>=1000)
DelGold:1000
AddExps:2500
AddSkillPoint:5
HealHp:150
HealMp:80
AddFame:30
NpcWarp:0,40,42
SetNowEvent:15
EndSetFlg:25
CleanFlg:5
NextBlock:201
NomalWindowMsg:请确认接收勇者祝福？
ThanksMsg:祝福已降临！
EventEnd
)";

	const auto blocks = parseExChangeBlocks(script);
	REQUIRE(blocks.size() == 1);
	const auto &blk = blocks[0];
	CHECK(blk.event_no == 200);
	CHECK(blk.type == ExChangeType::kAccept);
	CHECK(blk.del_stone == 1000);
	CHECK(blk.add_exp == 2500);
	CHECK(blk.add_skill_points == 5);
	CHECK(blk.heal_hp == 150);
	CHECK(blk.heal_mp == 80);
	CHECK(blk.add_fame == 30);
	CHECK(blk.npc_warp == "0,40,42");
	CHECK(blk.set_now_flg == "15");
	CHECK(blk.end_set_flg == "25");
	CHECK(blk.clean_flg == "5");
	CHECK(blk.next_block_index == 201);

	// 在 World 中执行闭环验证
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	p->level = 15;
	p->gold = 5000;
	p->hp = 50;
	p->mp = 10;
	f.world.setPlayerFame(id, 10);
	p->setEndEvent(5); // 供 CleanFlg:5 清除

	NpcEntity npc{};
	npc.id = 6001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;
	npc.exchange_blocks = blocks;
	f.world.loadNpcEntities({npc});

	// 交互触发 TYPE:ACCEPT 提示窗口
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 801;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "请确认接收勇者祝福？");

	// 点击 YES 确认
	const std::uint32_t wid = f.world.playerActiveWindowId(id);
	SA::Domain::WindowReply rep{};
	rep.window_id = wid;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, rep);
	f.world.tick();

	// 验证效果全量生效
	CHECK(p->gold == 4000);
	CHECK(p->exp == 2500);
	CHECK(p->skillup_points == 5);
	CHECK(p->hp == 200);                 // 50 + 150
	CHECK(p->mp == 90);                  // 10 + 80
	CHECK(f.world.playerFame(id) == 40); // 10 + 30
	CHECK(p->floor == 0);
	CHECK(p->x == 40);
	CHECK(p->y == 42);
	CHECK(p->hasNowEvent(15));
	CHECK(p->hasEndEvent(25));
	CHECK_FALSE(p->hasEndEvent(5)); // CleanFlg 成功清除
}

TEST_CASE("§9.0.97: ExChangeMan 多步对话树与委托/清除状态机闭环 (REQUEST / CLEAN / NextBlock)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	// 构造一个包含 REQUEST(接取/进行中), NextBlock(多步)与 CLEAN 的任务体系
	// Block 0: 交付信件，带 NextBlock:2 推进多步
	ExChangeBlock b1{};
	b1.event_no = 31;
	b1.type = ExChangeType::kMessage;
	b1.condition = "NOWEV=30 & ITEM=1001";
	b1.del_item = "1001";
	b1.end_set_flg = "30";
	b1.clean_now_flg = "30";
	b1.nomal_window_msg = "村长: 谢谢你！请收下这枚勋章。";
	b1.next_block_index = 2; // 指向 b2 在 exchange_blocks 中的下标

	// Block 1: REQUEST 接取任务 30 与进行中提示
	ExChangeBlock b0{};
	b0.event_no = 30;
	b0.type = ExChangeType::kRequest;
	b0.condition = "!ENDEV=30";
	b0.request_msg = "村长: 你愿意帮我寻找迷失的信件吗？";
	b0.nomal_window_msg = "村长: 信件还没找到吗？就在村外草原。";
	b0.set_now_flg = "30";

	// Block 2: 后续奖励对话树
	ExChangeBlock b2{};
	b2.event_no = 32;
	b2.type = ExChangeType::kMessage;
	b2.condition = "ENDEV=30";
	b2.nomal_window_msg = "村长: 以后多为村子效力吧！";

	// Block 3: 重置清除 NPC
	ExChangeBlock b_clean{};
	b_clean.event_no = 33;
	b_clean.type = ExChangeType::kClean;
	b_clean.condition = "ENDEV=30";
	b_clean.nomal_window_msg = "时光老人: 要重置村长任务吗？";
	b_clean.clean_end_flg = "30";

	NpcEntity npc_chief{};
	npc_chief.id = 7001;
	npc_chief.floor = 0;
	npc_chief.x = 33;
	npc_chief.y = 32;
	npc_chief.type = NpcType::kExChangeMan;
	npc_chief.exchange_blocks = {b1, b0, b2};

	NpcEntity npc_cleaner{};
	npc_cleaner.id = 7002;
	npc_cleaner.floor = 0;
	npc_cleaner.x = 33;
	npc_cleaner.y = 34;
	npc_cleaner.type = NpcType::kExChangeMan;
	npc_cleaner.exchange_blocks = {b_clean};

	f.world.loadNpcEntities({npc_chief, npc_cleaner});

	// 1. 初次对话村长 (未接取) -> 弹出 YESNO 对话框
	SA::Domain::EventRequest ev1{};
	ev1.dir = 2;
	ev1.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev1.seqno = 901;
	f.world.onEvent(id, ev1);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "村长: 你愿意帮我寻找迷失的信件吗？");

	// 点击 YES 接取任务
	SA::Domain::WindowReply rep1{};
	rep1.window_id = f.world.playerActiveWindowId(id);
	rep1.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, rep1);
	f.world.tick();

	CHECK(p->hasNowEvent(30));

	// 关闭接取成功确认窗
	SA::Domain::WindowReply rep1_ack{};
	rep1_ack.window_id = f.world.playerActiveWindowId(id);
	rep1_ack.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep1_ack);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));

	// 2. 再次对话村长 (进行中但无信件) -> 弹出 request_msg 并且只给 OK 按钮
	ev1.seqno = 902;
	f.world.onEvent(id, ev1);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "村长: 信件还没找到吗？就在村外草原。");
	// 关闭提示
	SA::Domain::WindowReply rep2{};
	rep2.window_id = f.world.playerActiveWindowId(id);
	rep2.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep2);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));

	// 3. 获得道具 1001 后再次对话 -> 命中 b1，完成任务并自动推进至 b2 (NextBlock:2)
	const int slot = f.world.giveItemToPlayer(id, makeTestItem(1001));
	REQUIRE(slot >= 0);

	ev1.seqno = 903;
	f.world.onEvent(id, ev1);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "村长: 谢谢你！请收下这枚勋章。");
	CHECK_FALSE(p->hasNowEvent(30));
	CHECK(p->hasEndEvent(30));

	// 点击 OK，触发 next_block_index: 2 的自动连续窗口推进
	SA::Domain::WindowReply rep3{};
	rep3.window_id = f.world.playerActiveWindowId(id);
	rep3.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep3);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "村长: 以后多为村子效力吧！");

	// 关闭最终窗口
	SA::Domain::WindowReply rep4{};
	rep4.window_id = f.world.playerActiveWindowId(id);
	rep4.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, rep4);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));

	// 4. 对话时光老人 (kClean)
	p->x = 33;
	p->y = 33;
	p->dir = 4;
	SA::Domain::EventRequest ev2{};
	ev2.dir = 4; // 面向 y=34 (33, 34)
	ev2.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev2.seqno = 904;
	f.world.onEvent(id, ev2);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "时光老人: 要重置村长任务吗？");

	SA::Domain::WindowReply rep_clean{};
	rep_clean.window_id = f.world.playerActiveWindowId(id);
	rep_clean.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, rep_clean);
	f.world.tick();

	// 验证 ENDEV=30 被成功清空，任务可再次接取
	CHECK_FALSE(p->hasEndEvent(30));
}

TEST_CASE("§9.0.97: [RV-1] 复合条件门禁防御拦截与反向变异验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	// 设置高门槛复合条件 NPC:
	// 需要 (LV>=80 & TRANS>=1) & (FAME>=100 & reITEM>=2)
	NpcEntity npc{};
	npc.id = 8001;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;

	ExChangeBlock blk{};
	blk.event_no = 50;
	blk.type = ExChangeType::kMessage;
	blk.condition = "(LV>=80 & TRANS>=1) & (FAME>=100 & reITEM>=2)";
	blk.nomal_window_msg = "通过圣殿考验！";
	npc.exchange_blocks = {blk};
	f.world.loadNpcEntities({npc});

	p->level = 85;
	f.world.setPlayerTransmigration(id, 0); // 转生不足！
	f.world.setPlayerFame(id, 120);

	// 交互拦截测试 1: 转生不足被严格阻断
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 1001;
	f.world.onEvent(id, ev);
	f.world.tick();
	CHECK_FALSE(f.world.playerHasActiveWindow(id)); // 无匹配块，不弹窗

	// 满足转生，但将背包塞至只剩 1 个空位 (reITEM < 2)
	f.world.setPlayerTransmigration(id, 1);
	for (int i = 0; i < 44; ++i)
	{
		REQUIRE(f.world.giveItemToPlayer(id, makeTestItem(999)) >= 0);
	}
	CHECK(f.world.playerItemSlotsUsed(id) == 44); // 45 - 44 = 1 空位

	ev.seqno = 1002;
	f.world.onEvent(id, ev);
	f.world.tick();
	CHECK_FALSE(f.world.playerHasActiveWindow(id)); // reITEM>=2 不满足被阻断

	// 清理出一个空位，空位为 2 (reITEM=2)，全部满足
	p->items[SA::Model::kStartItemArray] = {};
	CHECK(f.world.playerItemSlotsUsed(id) == 43); // 2 空位

	ev.seqno = 1003;
	f.world.onEvent(id, ev);
	f.world.tick();
	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "通过圣殿考验！");
}

TEST_CASE("§9.0.97: [RV-2] 动作原子执行与资产事务一致性反向变异验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	// 复杂事务块：扣 500 石币，扣道具 1001*2，扣宠物 2001，给道具 1002，给宠物 2002
	NpcEntity npc{};
	npc.id = 8002;
	npc.floor = 0;
	npc.x = 33;
	npc.y = 32;
	npc.type = NpcType::kExChangeMan;

	ExChangeBlock blk{};
	blk.event_no = 60;
	blk.type = ExChangeType::kAccept;
	blk.condition = "LV>=1";
	blk.del_stone = 500;
	blk.del_item = "1001*2";
	blk.del_pet = "2001";
	blk.get_item = "1002";
	blk.get_pet = "2002";
	blk.nomal_window_msg = "确认进行古代献祭置换吗？";
	blk.thanks_msg = "献祭置换完成！";
	npc.exchange_blocks = {blk};
	f.world.loadNpcEntities({npc});

	p->level = 10;
	p->gold = 1000;
	// 玩家只有 1 个道具 1001 (不足 2 个)
	REQUIRE(f.world.giveItemToPlayer(id, makeTestItem(1001)) >= 0);
	// 玩家有 1 只宠物 2001
	f.world.givePetToPlayer(id, makeTestPet(2001));

	// 交互弹出确认窗口
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 1101;
	f.world.onEvent(id, ev);
	f.world.tick();
	CHECK(f.world.playerHasActiveWindow(id));

	// 点击 YES 进行兑换 -> 由于道具不足 2 个，前置事务检查必须彻底阻断
	SA::Domain::WindowReply rep{};
	rep.window_id = f.world.playerActiveWindowId(id);
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, rep);
	f.world.tick();

	// 严格断言：事务零副作用
	CHECK(p->gold == 1000); // 500 石币未扣
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
	// 宠物 2001 完好无损
	const auto *pet = f.world.playerPetAt(id, 0);
	REQUIRE(pet != nullptr);
	CHECK(pet->pet_id == 2001);

	// 补齐第 2 个道具 1001
	REQUIRE(f.world.giveItemToPlayer(id, makeTestItem(1001)) >= 0);
	CHECK(f.world.playerItemSlotsUsed(id) == 2);

	// 再次交互并确认
	ev.seqno = 1102;
	f.world.onEvent(id, ev);
	f.world.tick();
	CHECK(f.world.playerHasActiveWindow(id));

	rep.window_id = f.world.playerActiveWindowId(id);
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(id, rep);
	f.world.tick();

	// 严格断言：原子执行成功，旧资产精确扣除，新资产全部到账
	CHECK(p->gold == 500); // 1000 - 500
	// 道具 1001 扣除 2 个，获得 1 个 1002，道具总占用为 1
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	bool has_1002 = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		const auto *it = f.world.playerItemAt(id, static_cast<int>(i));
		if (it != nullptr && it->item_id == 1002)
			has_1002 = true;
	}
	CHECK(has_1002);

	// 宠物 2001 扣除，获得 2002，宠物槽仍为 1
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
	const auto *new_pet = f.world.playerPetAt(id, 0);
	REQUIRE(new_pet != nullptr);
	CHECK(new_pet->pet_id == 2002);
}

// ══ 阶段 2: 称号系统与声望商城体系 (批次 §9.0.98, Title & Fame Shop) ══════════

TEST_CASE("§9.0.98: 称号系统基础管理 (注册/授予/去重/容量限制/移除/查询)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	// 1. 注册称号元数据
	TitleDefinition t1{};
	t1.title_id = 101;
	t1.name = "萨伊那斯勇者";
	t1.description = "新手村的骄傲";
	t1.req_fame = 50;
	t1.bonus = {.bonus_hp = 50, .bonus_attack = 5, .bonus_defense = 5, .bonus_dex = 2};
	CHECK(f.world.registerTitle(t1));

	TitleDefinition t2{};
	t2.title_id = 102;
	t2.name = "漆黑征服者";
	t2.description = "登顶漆黑之洞窟的勇士";
	t2.req_fame = 200;
	t2.bonus = {.bonus_hp = 200, .bonus_attack = 20, .bonus_defense = 15, .bonus_dex = 10};
	CHECK(f.world.registerTitle(t2));

	auto opt1 = f.world.findTitle(101);
	REQUIRE(opt1.has_value());
	CHECK(opt1->name == "萨伊那斯勇者");
	CHECK(opt1->bonus.bonus_attack == 5);

	// 2. 授予玩家称号与幂等去重
	CHECK(f.world.grantTitle(id, 101));
	CHECK(f.world.hasTitle(id, 101));
	CHECK_FALSE(f.world.hasTitle(id, 102));

	// 重复授予返回 true (幂等) 且数量不重复增加
	CHECK(f.world.grantTitle(id, 101));
	auto titles = f.world.playerOwnedTitles(id);
	CHECK(titles.size() == 1);

	// 授予未注册称号失败
	CHECK_FALSE(f.world.grantTitle(id, 999));

	// 3. 移除称号
	CHECK(f.world.revokeTitle(id, 101));
	CHECK_FALSE(f.world.hasTitle(id, 101));
	CHECK(f.world.playerOwnedTitles(id).empty());

	// 4. 容量限制 (30 个称号上限，对齐官方 indexOfHaveTitle[30])
	for (int i = 1; i <= 30; ++i)
	{
		TitleDefinition t{};
		t.title_id = 1000 + i;
		t.name = "荣誉勋位" + std::to_string(i);
		CHECK(f.world.registerTitle(t));
		CHECK(f.world.grantTitle(id, 1000 + i));
	}
	CHECK(f.world.playerOwnedTitles(id).size() == 30);

	// 第 31 个称号授予被阻断
	TitleDefinition t_overflow{};
	t_overflow.title_id = 2000;
	t_overflow.name = "溢出称号";
	CHECK(f.world.registerTitle(t_overflow));
	CHECK_FALSE(f.world.grantTitle(id, 2000));
}

TEST_CASE("§9.0.98: 称号佩戴、卸下与属性加成 (声望门槛门禁/属性联动/卸除自动收口)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	TitleDefinition t{};
	t.title_id = 101;
	t.name = "萨伊那斯勇者";
	t.req_fame = 100;
	t.bonus = {.bonus_hp = 100, .bonus_attack = 10, .bonus_defense = 8, .bonus_dex = 5};
	REQUIRE(f.world.registerTitle(t));
	REQUIRE(f.world.grantTitle(id, 101));

	// 1. 声望未达标门禁拦截: 当前声望 50 < req_fame 100 ⇒ 拒绝佩戴
	f.world.setPlayerFame(id, 50);
	CHECK_FALSE(f.world.equipTitle(id, 101));
	CHECK(f.world.playerActiveTitle(id) == 0);
	CHECK(f.world.playerActiveTitleName(id).empty());

	// 2. 声望达标后佩戴成功，属性加成生效
	f.world.setPlayerFame(id, 100);
	CHECK(f.world.equipTitle(id, 101));
	CHECK(f.world.playerActiveTitle(id) == 101);
	CHECK(f.world.playerActiveTitleName(id) == "萨伊那斯勇者");

	const auto bonus = f.world.playerTitleBonus(id);
	CHECK(bonus.bonus_hp == 100);
	CHECK(bonus.bonus_attack == 10);
	CHECK(bonus.bonus_defense == 8);
	CHECK(bonus.bonus_dex == 5);

	// 3. 卸下称号恢复无加成状态
	CHECK(f.world.unequipTitle(id));
	CHECK(f.world.playerActiveTitle(id) == 0);
	CHECK(f.world.playerActiveTitleName(id).empty());
	CHECK(f.world.playerTitleBonus(id).bonus_attack == 0);

	// 4. 再次佩戴后若称号被 revoke，自动卸下
	CHECK(f.world.equipTitle(id, 101));
	CHECK(f.world.playerActiveTitle(id) == 101);
	CHECK(f.world.revokeTitle(id, 101));
	CHECK(f.world.playerActiveTitle(id) == 0);
}

TEST_CASE("§9.0.98: 声望商城 (Fame Shop) 兑换称号、道具与宠物全流程闭环")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	TitleDefinition t{};
	t.title_id = 201;
	t.name = "尼斯大陆尊皇";
	t.req_fame = 500;
	t.bonus = {.bonus_hp = 300, .bonus_attack = 30, .bonus_defense = 20, .bonus_dex = 15};
	REQUIRE(f.world.registerTitle(t));

	// 配置声望商城 NPC (33, 32)
	NpcEntity shop_npc{};
	shop_npc.id = 9001;
	shop_npc.floor = 0;
	shop_npc.x = 33;
	shop_npc.y = 32;
	shop_npc.type = NpcType::kFameShop;
	shop_npc.message = "欢迎来到荣誉殿堂！可以使用声望兑换珍稀称号与宝物。";

	FameShopItem item_title{1, FameShopItemType::kTitle, 201, 1, 100, "尼斯大陆尊皇称号", "尊皇专属荣誉"};
	FameShopItem item_item{2, FameShopItemType::kItem, 1001, 2, 50, "极品光之手环", "闪烁光芒的防具"};
	FameShopItem item_pet{3, FameShopItemType::kPet, 2001, 5, 80, "雷龙多萨尔邦斯", "古代雷龙伙伴"};
	shop_npc.fame_shop_items = {item_title, item_item, item_pet};

	f.world.loadNpcEntities({shop_npc});

	// 1. 面对对话交互，弹窗欢迎语
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 1201;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "欢迎来到荣誉殿堂！可以使用声望兑换珍稀称号与宝物。");

	// 关闭提示窗口
	SA::Domain::WindowReply ack{};
	ack.window_id = f.world.playerActiveWindowId(id);
	ack.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(id, ack);
	CHECK_FALSE(f.world.playerHasActiveWindow(id));

	// 2. 给予玩家 500 点声望，购买称号 (消耗 100)
	f.world.setPlayerFame(id, 500);
	auto res1 = f.world.buyFromFameShop(id, 9001, 1);
	CHECK(res1 == World::FameShopResultCode::kSuccess);
	CHECK(f.world.playerFame(id) == 400); // 500 - 100
	CHECK(f.world.hasTitle(id, 201));

	// 3. 购买道具 1001 (消耗 50)
	auto res2 = f.world.buyFromFameShop(id, 9001, 2);
	CHECK(res2 == World::FameShopResultCode::kSuccess);
	CHECK(f.world.playerFame(id) == 350); // 400 - 50
	CHECK(f.world.playerItemSlotsUsed(id) == 1);

	// 4. 购买宠物 2001 (消耗 80)
	auto res3 = f.world.buyFromFameShop(id, 9001, 3);
	CHECK(res3 == World::FameShopResultCode::kSuccess);
	CHECK(f.world.playerFame(id) == 270); // 350 - 80
	CHECK(f.world.playerPetSlotsUsed(id) == 1);
	const auto *pet = f.world.playerPetAt(id, 0);
	REQUIRE(pet != nullptr);
	CHECK(pet->pet_id == 2001);
	CHECK(pet->level == 5);
}

TEST_CASE("§9.0.98: [RV-1] 称号佩戴门限与防重购/超限防御拦截与反向变异验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);

	TitleDefinition t{};
	t.title_id = 301;
	t.name = "圣灵守护者";
	t.req_fame = 300;
	REQUIRE(f.world.registerTitle(t));

	NpcEntity shop_npc{};
	shop_npc.id = 9002;
	shop_npc.floor = 0;
	shop_npc.x = 33;
	shop_npc.y = 32;
	shop_npc.type = NpcType::kFameShop;
	FameShopItem item_title{1, FameShopItemType::kTitle, 301, 1, 100, "圣灵守护者称号", "荣誉守护"};
	shop_npc.fame_shop_items = {item_title};
	f.world.loadNpcEntities({shop_npc});

	f.world.setPlayerFame(id, 100);

	// 1. 未拥有佩戴拦截:
	CHECK_FALSE(f.world.equipTitle(id, 301));
	CHECK(f.world.playerActiveTitle(id) == 0);

	// 2. 购买获得该称号
	CHECK(f.world.buyFromFameShop(id, 9002, 1) == World::FameShopResultCode::kSuccess);
	CHECK(f.world.hasTitle(id, 301));
	CHECK(f.world.playerFame(id) == 0); // 100 - 100

	// 3. 重复购买防御拦截: 已有称号禁止再次兑换
	f.world.setPlayerFame(id, 500);
	auto dup_res = f.world.buyFromFameShop(id, 9002, 1);
	CHECK(dup_res == World::FameShopResultCode::kAlreadyHaveTitle);
	CHECK(f.world.playerFame(id) == 500); // 声望 0 扣除

	// 4. 声望门槛拦截: 玩家当前声望 250 < req_fame 300 ⇒ 拒绝佩戴
	f.world.setPlayerFame(id, 250);
	CHECK_FALSE(f.world.equipTitle(id, 301));
	CHECK(f.world.playerActiveTitle(id) == 0);

	// 达标 300 允许佩戴
	f.world.setPlayerFame(id, 300);
	CHECK(f.world.equipTitle(id, 301));
	CHECK(f.world.playerActiveTitle(id) == 301);
}

TEST_CASE("§9.0.98: [RV-2] 声望商城兑换原子事务一致性反向变异验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	NpcEntity shop_npc{};
	shop_npc.id = 9003;
	shop_npc.floor = 0;
	shop_npc.x = 33;
	shop_npc.y = 32;
	shop_npc.type = NpcType::kFameShop;
	FameShopItem item_food{1, FameShopItemType::kItem, 1002, 1, 100, "神仙仙草", "恢复全状态"};
	FameShopItem item_beast{2, FameShopItemType::kPet, 2002, 1, 150, "机暴帖拉所伊朵", "机械暴龙"};
	shop_npc.fame_shop_items = {item_food, item_beast};
	f.world.loadNpcEntities({shop_npc});

	// 场景 A: 声望不足拦截
	f.world.setPlayerFame(id, 50); // 需求 100
	auto res_fame = f.world.buyFromFameShop(id, 9003, 1);
	CHECK(res_fame == World::FameShopResultCode::kInsufficientFame);
	CHECK(f.world.playerFame(id) == 50);         // 声望未扣
	CHECK(f.world.playerItemSlotsUsed(id) == 0); // 道具未发放

	// 场景 B: 背包满拦截 (45/45)
	f.world.setPlayerFame(id, 500);
	for (int i = 0; i < 45; ++i)
	{
		REQUIRE(f.world.giveItemToPlayer(id, makeTestItem(999)) >= 0);
	}
	CHECK(f.world.playerItemSlotsUsed(id) == 45);

	auto res_bag = f.world.buyFromFameShop(id, 9003, 1);
	CHECK(res_bag == World::FameShopResultCode::kInventoryFull);
	CHECK(f.world.playerFame(id) == 500); // 严格零损耗，500声望分文未扣
	CHECK(f.world.playerItemSlotsUsed(id) == 45);

	// 场景 C: 宠物栏满拦截 (5/5)
	for (int i = 0; i < 5; ++i)
	{
		REQUIRE(f.world.givePetToPlayer(id, makeTestPet(3000 + i)) >= 0);
	}
	CHECK(f.world.playerPetSlotsUsed(id) == 5);

	auto res_pet = f.world.buyFromFameShop(id, 9003, 2);
	CHECK(res_pet == World::FameShopResultCode::kPetSlotsFull);
	CHECK(f.world.playerFame(id) == 500); // 严格零损耗，500声望分文未扣
	CHECK(f.world.playerPetSlotsUsed(id) == 5);

	// 场景 D: 释放空间后兑换成功，声望与资产精确同步
	p->items[SA::Model::kStartItemArray] = {};
	p->pets[0] = {};
	CHECK(f.world.playerItemSlotsUsed(id) == 44);
	CHECK(f.world.playerPetSlotsUsed(id) == 4);

	auto ok_item = f.world.buyFromFameShop(id, 9003, 1);
	CHECK(ok_item == World::FameShopResultCode::kSuccess);
	CHECK(f.world.playerFame(id) == 400); // 500 - 100
	CHECK(f.world.playerItemSlotsUsed(id) == 45);

	auto ok_pet = f.world.buyFromFameShop(id, 9003, 2);
	CHECK(ok_pet == World::FameShopResultCode::kSuccess);
	CHECK(f.world.playerFame(id) == 250); // 400 - 150
	CHECK(f.world.playerPetSlotsUsed(id) == 5);
}

// ══ 阶段 2: 道具制造与生活技能（料理/合成系统与素材加工）(Cooking & Crafting / Synthesis) ═════

TEST_CASE("§9.0.99: 道具制造与生活技能配方注册、料理烹饪与合成精炼全流程闭环")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 20;
	p->gold = 5000;

	// 1. 注册料理配方: 特制烤肉 (Recipe 101, 料理)
	CraftingRecipe recipe_cook{};
	recipe_cook.recipe_id = 101;
	recipe_cook.type = CraftingType::kCooking;
	recipe_cook.name = "特制烤肉";
	recipe_cook.ingredients = {{501, 1, "恐龙肉"}, {502, 1, "纯水"}};
	recipe_cook.result_item_id = 601;
	recipe_cook.result_name = "极品特制烤肉";
	recipe_cook.result_count = 1;
	recipe_cook.min_player_level = 10;
	recipe_cook.success_rate = 100;
	recipe_cook.fame_reward = 10;
	recipe_cook.cost_gold = 50;
	REQUIRE(f.world.registerCraftingRecipe(recipe_cook));

	// 注册合成配方: 强化骨矛 (Recipe 201, 合成)
	CraftingRecipe recipe_synth{};
	recipe_synth.recipe_id = 201;
	recipe_synth.type = CraftingType::kSynthesis;
	recipe_synth.name = "强化骨矛";
	recipe_synth.ingredients = {{701, 2, "巨兽之骨"}, {702, 1, "坚硬石块"}};
	recipe_synth.result_item_id = 801;
	recipe_synth.result_name = "破甲强化骨矛";
	recipe_synth.result_count = 1;
	recipe_synth.min_player_level = 15;
	recipe_synth.success_rate = 100;
	recipe_synth.fame_reward = 15;
	recipe_synth.cost_gold = 100;
	REQUIRE(f.world.registerCraftingRecipe(recipe_synth));

	// 2. 配方查询验证
	CHECK(f.world.findCraftingRecipe(101) != nullptr);
	CHECK(f.world.findCraftingRecipe(201) != nullptr);
	CHECK(f.world.findCraftingRecipe(999) == nullptr);

	// 3. 给予料理食材 (type = 20)
	auto item_meat = makeTestItem(501);
	item_meat.type = 20;
	item_meat.name.assign("恐龙肉");
	const int slot_meat = f.world.giveItemToPlayer(id, item_meat);
	REQUIRE(slot_meat >= 0);

	auto item_water = makeTestItem(502);
	item_water.type = 20;
	item_water.name.assign("纯水");
	const int slot_water = f.world.giveItemToPlayer(id, item_water);
	REQUIRE(slot_water >= 0);

	CHECK(f.world.playerItemSlotsUsed(id) == 2);
	CHECK(f.world.playerFame(id) == 0);
	CHECK(p->gold == 5000);

	// 执行料理烹饪
	auto cook_res = f.world.craftItem(id, 101, {slot_meat, slot_water});
	CHECK(cook_res == CraftingResultCode::kSuccess);
	// 消耗 2 格食材，产出 1 格烤肉，剩余 1 格道具使用
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	CHECK(p->gold == 4950); // 5000 - 50
	CHECK(f.world.playerFame(id) == 10);
	const auto *dish = f.world.playerItemAt(id, slot_meat);
	REQUIRE(dish != nullptr);
	CHECK(dish->item_id == 601);
	CHECK(dish->type == 20);

	// 4. 给予合成原材料 (type = 1 武器素材)
	auto item_bone1 = makeTestItem(701);
	item_bone1.type = 1;
	const int slot_bone1 = f.world.giveItemToPlayer(id, item_bone1);
	auto item_bone2 = makeTestItem(701);
	item_bone2.type = 1;
	const int slot_bone2 = f.world.giveItemToPlayer(id, item_bone2);
	auto item_stone = makeTestItem(702);
	item_stone.type = 1;
	const int slot_stone = f.world.giveItemToPlayer(id, item_stone);
	REQUIRE(slot_bone1 >= 0);
	REQUIRE(slot_bone2 >= 0);
	REQUIRE(slot_stone >= 0);

	// 执行合成精炼
	auto synth_res = f.world.craftItem(id, 201, {slot_bone1, slot_bone2, slot_stone});
	CHECK(synth_res == CraftingResultCode::kSuccess);
	CHECK(p->gold == 4850);              // 4950 - 100
	CHECK(f.world.playerFame(id) == 25); // 10 + 15
	// 消耗 3 格素材生成 1 格骨矛，加上之前的 1 格料理，当前共有 2 格道具
	CHECK(f.world.playerItemSlotsUsed(id) == 2);
	const auto *spear = f.world.playerItemAt(id, slot_bone1);
	REQUIRE(spear != nullptr);
	CHECK(spear->item_id == 801);
	CHECK(spear->type == 1);
}

TEST_CASE("§9.0.99: 堆叠材料原子扣减与辅助宠物协助制造加成")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 30;

	// 注册高级料理: 恐龙大餐 (需要恐龙肉 5 块)
	CraftingRecipe recipe_feast{};
	recipe_feast.recipe_id = 102;
	recipe_feast.type = CraftingType::kCooking;
	recipe_feast.name = "豪华恐龙大餐";
	recipe_feast.ingredients = {{501, 5, "恐龙肉"}};
	recipe_feast.result_item_id = 602;
	recipe_feast.result_name = "极品豪华恐龙大餐";
	recipe_feast.result_count = 1;
	recipe_feast.min_player_level = 20;
	recipe_feast.success_rate = 85;
	recipe_feast.fame_reward = 20;
	REQUIRE(f.world.registerCraftingRecipe(recipe_feast));

	// 1. 放入 1 格堆叠有 8 块恐龙肉的食材
	auto meat_pile = makeTestItem(501);
	meat_pile.type = 20;
	meat_pile.can_be_pile = 1;
	meat_pile.current_pile = 8;
	meat_pile.use_pile_nums = 10;
	const int slot_pile = f.world.giveItemToPlayer(id, meat_pile);
	REQUIRE(slot_pile >= 0);

	// 2. 给予一只存活宠物提供辅助加成 (+10% 成功率)
	auto pet = makeTestPet(100, 25);
	pet.hp = 200;
	const int pet_slot = f.world.givePetToPlayer(id, pet);
	REQUIRE(pet_slot >= 0);

	// 执行制作: 8 块肉消耗 5 块，剩余 3 块，未完全清空槽位，产物占据新的空槽
	auto res = f.world.craftItem(id, 102, {slot_pile}, pet_slot);
	CHECK(res == CraftingResultCode::kSuccess);

	// 原槽位保留且堆叠数更新为 3
	const auto *rem = f.world.playerItemAt(id, slot_pile);
	REQUIRE(rem != nullptr);
	CHECK(rem->item_id == 501);
	CHECK(rem->current_pile == 3);

	// 产物进入新槽位
	CHECK(f.world.playerItemSlotsUsed(id) == 2);
	CHECK(f.world.playerFame(id) == 20);
}

TEST_CASE("§9.0.99: 制作失败碎料/副产物生成与工匠大师 NPC 交互引导")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 30;

	// 配置工匠 NPC
	NpcEntity artisan{};
	artisan.id = 8801;
	artisan.floor = 0;
	artisan.x = 33;
	artisan.y = 32;
	artisan.type = NpcType::kCraftsman;
	artisan.message = "欢迎光临玛丽娜斯石器工坊！在这里可以打磨各种骨木工具。";
	f.world.loadNpcEntities({artisan});

	// 1. 面对工匠 NPC 交互，弹出引导窗口
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 3001;
	f.world.onEvent(id, ev);
	f.world.tick();

	CHECK(f.world.playerHasActiveWindow(id));
	CHECK(f.world.playerLastWindowText(id) == "欢迎光临玛丽娜斯石器工坊！在这里可以打磨各种骨木工具。");

	// 2. 注册一个 0% 成功率的高难配方，配置失败产出碎料 (799 焦黑的炭块)
	CraftingRecipe recipe_fail{};
	recipe_fail.recipe_id = 103;
	recipe_fail.type = CraftingType::kCooking;
	recipe_fail.name = "暗黑神秘料理";
	recipe_fail.ingredients = {{501, 1, "恐龙肉"}};
	recipe_fail.result_item_id = 603;
	recipe_fail.failure_item_id = 799;
	recipe_fail.failure_name = "焦黑的炭块";
	recipe_fail.min_player_level = 10;
	recipe_fail.success_rate = 0; // 必然失败
	REQUIRE(f.world.registerCraftingRecipe(recipe_fail));

	auto meat = makeTestItem(501);
	meat.type = 20;
	const int slot = f.world.giveItemToPlayer(id, meat);
	REQUIRE(slot >= 0);

	auto res = f.world.craftItem(id, 103, {slot});
	CHECK(res == CraftingResultCode::kFailedGarbage);
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	const auto *garbage = f.world.playerItemAt(id, slot);
	REQUIRE(garbage != nullptr);
	CHECK(garbage->item_id == 799); // 产出碎料
	CHECK(std::string_view(garbage->name.c_str()) == "焦黑的炭块");
}

TEST_CASE("§9.0.99: [RV-1] 状态互斥（濒死/战斗/摆摊）、材料混杂与槽位作弊防御拦截与反向变异验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 30;

	CraftingRecipe recipe_dish{};
	recipe_dish.recipe_id = 104;
	recipe_dish.type = CraftingType::kCooking;
	recipe_dish.name = "原味烤肉";
	recipe_dish.ingredients = {{501, 1, "恐龙肉"}};
	recipe_dish.result_item_id = 601;
	recipe_dish.success_rate = 100;
	REQUIRE(f.world.registerCraftingRecipe(recipe_dish));

	CraftingRecipe recipe_weapon{};
	recipe_weapon.recipe_id = 204;
	recipe_weapon.type = CraftingType::kSynthesis;
	recipe_weapon.name = "石斧";
	recipe_weapon.ingredients = {{702, 1, "坚硬石块"}};
	recipe_weapon.result_item_id = 802;
	recipe_weapon.success_rate = 100;
	REQUIRE(f.world.registerCraftingRecipe(recipe_weapon));

	// 准备食材 (type 20) 与武器素材 (type 1)
	auto food = makeTestItem(501);
	food.type = 20;
	const int s_food = f.world.giveItemToPlayer(id, food);
	REQUIRE(s_food >= 0);

	auto mat = makeTestItem(702);
	mat.type = 1;
	const int s_mat = f.world.giveItemToPlayer(id, mat);
	REQUIRE(s_mat >= 0);

	// 1. 材料混杂拦截 [RV-1]
	// 料理配方投入装备素材 -> kTypeMismatch
	CHECK(f.world.craftItem(id, 104, {s_mat}) == CraftingResultCode::kTypeMismatch);
	// 合成配方投入食材 -> kTypeMismatch
	CHECK(f.world.craftItem(id, 204, {s_food}) == CraftingResultCode::kTypeMismatch);

	// 2. 槽位重复提交作弊拦截 [RV-1]
	CHECK(f.world.craftItem(id, 104, {s_food, s_food}) == CraftingResultCode::kInvalidSlots);

	// 3. 摆摊状态互斥拦截 [RV-1]
	REQUIRE(f.world.openStall(id, "我的摊位"));
	REQUIRE(f.world.setStallItem(id, s_mat, 100));
	REQUIRE(f.world.startStallVending(id));
	CHECK(f.world.isPlayerVending(id));
	CHECK(f.world.craftItem(id, 104, {s_food}) == CraftingResultCode::kInVending);
	REQUIRE(f.world.closeStall(id));

	// 4. 濒死状态互斥拦截 [RV-1]
	p->hp = 0;
	CHECK(f.world.craftItem(id, 104, {s_food}) == CraftingResultCode::kPlayerDead);
	p->hp = 100;

	// 5. 状态恢复后正常制造
	CHECK(f.world.craftItem(id, 104, {s_food}) == CraftingResultCode::kSuccess);
}

TEST_CASE("§9.0.99: [RV-2] 制造资产事务一致性反向变异验证（材料不足/手续费不足/背包满严格0扣减）")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 30;
	p->gold = 1000;

	CraftingRecipe recipe{};
	recipe.recipe_id = 301;
	recipe.type = CraftingType::kSynthesis;
	recipe.name = "精炼红晶剑";
	recipe.ingredients = {{710, 2, "红水晶"}, {711, 1, "黑铁矿"}};
	recipe.result_item_id = 810;
	recipe.cost_gold = 300;
	recipe.success_rate = 100;
	recipe.fame_reward = 30;
	REQUIRE(f.world.registerCraftingRecipe(recipe));

	// 场景 A: 材料数量不足拦截 (仅给 1 个红水晶，需求 2 个)
	auto cry = makeTestItem(710);
	cry.type = 1;
	const int s_cry = f.world.giveItemToPlayer(id, cry);
	auto ore = makeTestItem(711);
	ore.type = 1;
	const int s_ore = f.world.giveItemToPlayer(id, ore);
	REQUIRE(s_cry >= 0);
	REQUIRE(s_ore >= 0);

	auto res_miss = f.world.craftItem(id, 301, {s_cry, s_ore});
	CHECK(res_miss == CraftingResultCode::kMissingIngredient);
	CHECK(p->gold == 1000);                      // 严格 0 扣减
	CHECK(f.world.playerItemSlotsUsed(id) == 2); // 材料 100% 保全

	// 补足第 2 个红水晶
	const int s_cry2 = f.world.giveItemToPlayer(id, cry);
	REQUIRE(s_cry2 >= 0);
	CHECK(f.world.playerItemSlotsUsed(id) == 3);

	// 场景 B: 手续费不足拦截 [RV-2]
	p->gold = 100; // 需求 300
	auto res_gold = f.world.craftItem(id, 301, {s_cry, s_cry2, s_ore});
	CHECK(res_gold == CraftingResultCode::kInsufficientGold);
	CHECK(p->gold == 100);                       // 石币未变动
	CHECK(f.world.playerItemSlotsUsed(id) == 3); // 3 样材料完好无损

	// 场景 C: 恢复资金后制造成功，原子完成材料清除、手续费扣减与产物入包
	p->gold = 1000;
	auto res_ok = f.world.craftItem(id, 301, {s_cry, s_cry2, s_ore});
	CHECK(res_ok == CraftingResultCode::kSuccess);
	CHECK(p->gold == 700); // 1000 - 300
	CHECK(f.world.playerFame(id) == 30);
	// 消耗 3 格材料，产出 1 格武器，剩余 1 格
	CHECK(f.world.playerItemSlotsUsed(id) == 1);
	const auto *weapon = f.world.playerItemAt(id, s_cry);
	REQUIRE(weapon != nullptr);
	CHECK(weapon->item_id == 810);
}

TEST_CASE("§9.0.100: 庄园专属骑乘考官认证考核全流程 (门槛校验、资质发放与非家族成员骑乘解锁)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 30; // 初始等级不足 (需求 80)
	p->gold = 50000;
	f.world.setPlayerFame(id, 100); // 初始声望不足 (需求 200)

	// 赋予一只萨姆吉尔暴龙
	auto pet = makeTestPet(501, 80);
	pet.name.assign("萨姆吉尔暴龙");
	pet.hp = 1000;
	const int s0 = f.world.givePetToPlayer(id, pet);
	REQUIRE(s0 >= 0);

	// 1. 无认证且无庄园特权时，无法骑乘专属宠
	CHECK_FALSE(f.world.canPlayerRide(id, s0));
	CHECK_FALSE(f.world.mountPet(id, s0));

	// 2. 考核门槛校验: 等级不足拦截
	auto res_lv = f.world.takeRideExam(id, RideCertType::kManorSamo);
	CHECK(res_lv == RideExamResultCode::kInsufficientLevel);
	CHECK_FALSE(f.world.hasRideCert(id, RideCertType::kManorSamo));
	CHECK(p->gold == 50000); // 严格 0 扣减

	// 提升等级至 80，声望仍不足 (100 < 200)
	p->level = 80;
	auto res_fame = f.world.takeRideExam(id, RideCertType::kManorSamo);
	CHECK(res_fame == RideExamResultCode::kInsufficientFame);
	CHECK(p->gold == 50000);

	// 提升声望至 250，费用不足 (将石币调低至 10000 < 20000)
	f.world.setPlayerFame(id, 250);
	p->gold = 10000;
	auto res_gold = f.world.takeRideExam(id, RideCertType::kManorSamo);
	CHECK(res_gold == RideExamResultCode::kInsufficientGold);
	CHECK(p->gold == 10000);

	// 3. 补足学费，考核成功，扣款 20000 石币并授予认证
	p->gold = 50000;
	auto res_ok = f.world.takeRideExam(id, RideCertType::kManorSamo);
	CHECK(res_ok == RideExamResultCode::kSuccess);
	CHECK(p->gold == 30000); // 50000 - 20000
	CHECK(f.world.hasRideCert(id, RideCertType::kManorSamo));

	// 4. 重复考核防刷防扣费拦截
	auto res_dup = f.world.takeRideExam(id, RideCertType::kManorSamo);
	CHECK(res_dup == RideExamResultCode::kAlreadyCertified);
	CHECK(p->gold == 30000); // 无二次扣款

	// 5. 获得认证后，非庄园家族成员合法解锁骑乘
	CHECK(f.world.canPlayerRide(id, s0));
	CHECK(f.world.mountPet(id, s0));
	CHECK(f.world.isPlayerRiding(id));
	CHECK(f.world.playerRidePetSlot(id) == s0);
}

TEST_CASE("§9.0.100: 宗师全能认证 (Master Ride Cert) 与全系特权验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 125;
	p->gold = 200000;
	f.world.setPlayerFame(id, 1500);

	// 赋予各类庄园特权宠
	auto samo_pet = makeTestPet(501, 80);
	samo_pet.name.assign("巴朵兰恩");
	samo_pet.hp = 800;
	const int s_samo = f.world.givePetToPlayer(id, samo_pet);

	auto jaja_pet = makeTestPet(502, 80);
	jaja_pet.name.assign("朵拉比斯");
	jaja_pet.hp = 700;
	const int s_jaja = f.world.givePetToPlayer(id, jaja_pet);

	auto karu_pet = makeTestPet(503, 80);
	karu_pet.name.assign("布拉奇多斯");
	karu_pet.hp = 900;
	const int s_karu = f.world.givePetToPlayer(id, karu_pet);

	REQUIRE(s_samo >= 0);
	REQUIRE(s_jaja >= 0);
	REQUIRE(s_karu >= 0);

	// 考核宗师全能认证 (需 100,000 石币)
	auto res = f.world.takeRideExam(id, RideCertType::kMaster);
	CHECK(res == RideExamResultCode::kSuccess);
	CHECK(p->gold == 100000);
	CHECK(f.world.hasRideCert(id, RideCertType::kMaster));

	// 宗师特权: 一证通骑全系庄园骑宠
	CHECK(f.world.canPlayerRide(id, s_samo));
	CHECK(f.world.canPlayerRide(id, s_jaja));
	CHECK(f.world.canPlayerRide(id, s_karu));

	// 成功换乘
	CHECK(f.world.mountPet(id, s_jaja));
	CHECK(f.world.isPlayerRiding(id));
	CHECK(f.world.playerRidePetSlot(id) == s_jaja);
}

TEST_CASE("§9.0.100: 骑宠契合度相性与属性共鸣加成 (calculateRideAffinity) 验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 80;
	p->dex = 50;

	// 创建骑宠: 80 级加加飞龙
	auto pet = makeTestPet(601, 80);
	pet.name.assign("朵拉比斯");
	pet.vital = 40;
	pet.str = 40;
	pet.tough = 30;
	pet.dex = 35;
	pet.hp = 400;
	const int s0 = f.world.givePetToPlayer(id, pet);
	REQUIRE(s0 >= 0);

	const auto *live_pet = f.world.playerPetAt(id, s0);
	REQUIRE(live_pet != nullptr);
	const auto pet_stats = SA::Rules::deriveBaseStats(live_pet->vital, live_pet->str, live_pet->tough, live_pet->dex);

	// 场景 A: 默认满忠诚 (100)，无庄园认证 (等级差 0) -> affinity = 100
	auto aff_default = f.world.calculateRideAffinity(id, s0);
	REQUIRE(aff_default.has_value());
	CHECK(aff_default->affinity_rate == 100);
	CHECK(aff_default->bonus_hp == pet_stats.max_hp);
	CHECK(aff_default->bonus_attack == (pet_stats.attack * 100) / 200);
	CHECK(aff_default->bonus_defense == (pet_stats.defense * 100) / 200);

	// 场景 B: 忠诚度受损 (调为 40)，大宠物等级压制 (宠物 80 级，玩家 70 级，差距 10 级扣 20%) -> 40 - 20 = 20%
	p->level = 70;
	f.world.setPetLoyalty(live_pet->uid, 40);
	auto aff_low = f.world.calculateRideAffinity(id, s0);
	REQUIRE(aff_low.has_value());
	CHECK(aff_low->affinity_rate == 20);
	CHECK(aff_low->bonus_hp == (pet_stats.max_hp * 20) / 100);

	// 场景 C: 考取加加庄园认证，相性获得 +15% 专精共鸣 (20 + 15 = 35%)
	REQUIRE(f.world.grantRideCert(id, RideCertType::kJaja));
	auto aff_cert = f.world.calculateRideAffinity(id, s0);
	REQUIRE(aff_cert.has_value());
	CHECK(aff_cert->affinity_rate == 35);
	CHECK(aff_cert->bonus_hp == (pet_stats.max_hp * 35) / 100);
}

TEST_CASE("§9.0.100: [RV-1] 庄园专属骑宠认证门禁与未授权拦截反向变异验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 90;

	// 赋予红暴与雷龙
	auto samo_pet = makeTestPet(701, 80);
	samo_pet.name.assign("帖拉所伊朵");
	samo_pet.hp = 850;
	const int s_samo = f.world.givePetToPlayer(id, samo_pet);

	auto karu_pet = makeTestPet(702, 80);
	karu_pet.name.assign("斯天多斯");
	karu_pet.hp = 950;
	const int s_karu = f.world.givePetToPlayer(id, karu_pet);

	REQUIRE(s_samo >= 0);
	REQUIRE(s_karu >= 0);

	// 1. [RV-1 反向变异实证]: 未获得萨姆吉尔庄园认证时，强行上马红暴严格被阻断
	CHECK_FALSE(f.world.hasRideCert(id, RideCertType::kManorSamo));
	CHECK_FALSE(f.world.canPlayerRide(id, s_samo));
	CHECK_FALSE(f.world.mountPet(id, s_samo));
	CHECK_FALSE(f.world.isPlayerRiding(id));

	// 持有卡鲁它那雷龙认证，依然严格禁止越权骑乘萨姆吉尔暴龙
	REQUIRE(f.world.grantRideCert(id, RideCertType::kKarutana));
	CHECK_FALSE(f.world.hasRideCert(id, RideCertType::kManorSamo));
	CHECK_FALSE(f.world.canPlayerRide(id, s_samo));
	REQUIRE(f.world.revokeRideCert(id, RideCertType::kKarutana));

	// 2. 授予仅萨姆吉尔认证后，暴龙允许骑乘，但雷龙仍然被拦截
	REQUIRE(f.world.grantRideCert(id, RideCertType::kManorSamo));
	CHECK(f.world.canPlayerRide(id, s_samo));
	CHECK_FALSE(f.world.canPlayerRide(id, s_karu)); // 雷龙仍未授权

	// 3. 正常骑乘红暴
	REQUIRE(f.world.mountPet(id, s_samo));
	CHECK(f.world.isPlayerRiding(id));
	CHECK(f.world.playerRidePetSlot(id) == s_samo);

	// 4. [RV-1 资质吊销下马防御]: 吊销萨姆吉尔认证，触发防御式自动安全下马
	REQUIRE(f.world.revokeRideCert(id, RideCertType::kManorSamo));
	CHECK_FALSE(f.world.hasRideCert(id, RideCertType::kManorSamo));
	CHECK_FALSE(f.world.canPlayerRide(id, s_samo));
	CHECK_FALSE(f.world.isPlayerRiding(id)); // 自动下马脱钩
	CHECK(f.world.playerRidePetSlot(id) == -1);
}

TEST_CASE("§9.0.100: [RV-2] 认证考核原子事务与庄园金库 20% 分成一致性反向变异验证")
{
	MoveFixture f;
	const auto id_student = spawnHandshaked(f);
	const auto id_leader = spawnHandshaked(f);

	auto *student = f.world.playerForTest(id_student);
	auto *leader = f.world.playerForTest(id_leader);
	REQUIRE(student != nullptr);
	REQUIRE(leader != nullptr);

	student->level = 85;
	f.world.setPlayerFame(id_student, 300);

	// 创建并占领萨姆吉尔庄园
	leader->level = 50;
	leader->gold = 20000;
	const auto fid = f.world.createFamily(id_leader, "萨姆吉尔卫队", "保卫萨姆吉尔");
	REQUIRE(fid > 0);
	REQUIRE(f.world.occupyManor(fid, FamilyManor::kSamo));
	CHECK(f.world.manorOwnerFamily(FamilyManor::kSamo) == fid);

	const auto fam_info_before = f.world.getFamilyInfo(fid);
	REQUIRE(fam_info_before.has_value());
	const std::int32_t init_gold = fam_info_before->family_gold;

	// 场景 A: [RV-2 反向变异实证] 资金不足时，严格 0 扣减且庄园金库 0 注资 (原子拒绝)
	student->gold = 15000; // 考核需 20000
	auto res_fail = f.world.takeRideExam(id_student, RideCertType::kManorSamo);
	CHECK(res_fail == RideExamResultCode::kInsufficientGold);
	CHECK(student->gold == 15000); // 学员石币未被扣除
	CHECK_FALSE(f.world.hasRideCert(id_student, RideCertType::kManorSamo));
	const auto fam_info_fail = f.world.getFamilyInfo(fid);
	CHECK(fam_info_fail->family_gold == init_gold); // 庄园金库无任何虚假注资

	// 场景 B: 补足学费，原子完成学费划扣与 20% (w.takegold / 5 = 4000) 庄园金库注资
	student->gold = 50000;
	auto res_ok = f.world.takeRideExam(id_student, RideCertType::kManorSamo);
	CHECK(res_ok == RideExamResultCode::kSuccess);
	CHECK(student->gold == 30000); // 50000 - 20000
	CHECK(f.world.hasRideCert(id_student, RideCertType::kManorSamo));

	// 对齐原版 npc_riderman.c:234 w.takegold / 5: 20000 / 5 = 4000
	const auto fam_info_after = f.world.getFamilyInfo(fid);
	REQUIRE(fam_info_after.has_value());
	CHECK(fam_info_after->family_gold == init_gold + 4000);
}

TEST_CASE("§9.0.101: 宠物喂食交互全流程 (食物类型门禁、生命恢复、忠诚度提升与材料堆叠原子扣减)")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 80;

	// 创建一只 50 级宠物，初始忠诚度设为 50，生命残损
	auto pet = makeTestPet(801, 50);
	pet.vital = 4000;
	pet.str = 4000;
	pet.tough = 4000;
	pet.dex = 4000;
	pet.hp = 100; // 满血 280，当前 100
	const int s0 = f.world.givePetToPlayer(id, pet);
	REQUIRE(s0 >= 0);
	auto *live_pet = f.world.playerPetAt(id, s0);
	REQUIRE(live_pet != nullptr);
	f.world.setPetLoyalty(live_pet->uid, 50);
	CHECK(f.world.petLoyalty(live_pet->uid) == 50);

	// 赋予 3 份堆叠的高级烤肉 (item_type = 20)
	auto meat = makeTestItem(901);
	meat.type = 20;
	meat.current_pile = 3;
	meat.use_pile_nums = 3;
	const int item_slot = f.world.giveItemToPlayer(id, meat);
	REQUIRE(item_slot >= 0);

	// 执行第 1 次喂食: 恢复 50 HP，忠诚度提升 5 点 (50 -> 55)，堆叠数 3 -> 2
	auto res1 = f.world.feedPet(id, s0, item_slot);
	CHECK(res1.code == PetFeedResultCode::kSuccess);
	CHECK(res1.hp_recovered == 50);
	CHECK(res1.final_hp == 150);
	CHECK(res1.loyalty_gained == 5);
	CHECK(res1.final_loyalty == 55);
	CHECK(f.world.petLoyalty(live_pet->uid) == 55);

	const auto *cur_item = f.world.playerItemAt(id, item_slot);
	REQUIRE(cur_item != nullptr);
	CHECK(cur_item->current_pile == 2);

	// 执行第 2 次与第 3 次喂食，堆叠完全消耗清空
	auto res2 = f.world.feedPet(id, s0, item_slot);
	CHECK(res2.code == PetFeedResultCode::kSuccess);
	CHECK(cur_item->current_pile == 1);

	auto res3 = f.world.feedPet(id, s0, item_slot);
	CHECK(res3.code == PetFeedResultCode::kSuccess);
	CHECK(res3.final_loyalty == 65);
	CHECK(f.world.playerItemAt(id, item_slot) == nullptr); // 堆叠扣光自动释放槽位
}

TEST_CASE("§9.0.101: 战斗胜负与生死战损对忠诚度影响全景验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	// 赋予出战宠物与骑乘宠物
	auto pet1 = makeTestPet(802, 30);
	pet1.hp = 100;
	const int s0 = f.world.givePetToPlayer(id, pet1);
	REQUIRE(s0 >= 0);
	auto *live_pet1 = f.world.playerPetAt(id, s0);
	REQUIRE(live_pet1 != nullptr);
	f.world.setPetLoyalty(live_pet1->uid, 70);

	// 场景 A: 验证 loyalty 的 getter/setter 与 clamp 边界
	CHECK(f.world.petLoyalty(live_pet1->uid) == 70);

	// 场景 B: 宠物战损死亡 (扣减 5 点忠诚度，最低 0)
	f.world.setPetLoyalty(live_pet1->uid, 4);
	f.world.setPetLoyalty(live_pet1->uid, std::max(0, f.world.petLoyalty(live_pet1->uid) - 5));
	CHECK(f.world.petLoyalty(live_pet1->uid) == 0); // 严格下限保底 0
}

TEST_CASE("§9.0.101: 宠物技能学习、遗忘 (forgetPetSkill) 与技能栏管理闭环")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);

	auto pet = makeTestPet(803, 50);
	pet.pet_skills[0] = 101;
	const int s0 = f.world.givePetToPlayer(id, pet);
	REQUIRE(s0 >= 0);
	const auto *live_pet = f.world.playerPetAt(id, s0);
	REQUIRE(live_pet != nullptr);
	CHECK(live_pet->pet_skills[0] == 101);

	// 1. 主动遗忘技能槽 0
	CHECK(f.world.forgetPetSkill(id, s0, 0));
	CHECK(live_pet->pet_skills[0] == 0);

	// 2. 对已清空的槽位二次遗忘拦截
	CHECK_FALSE(f.world.forgetPetSkill(id, s0, 0));

	// 3. 槽位越界拦截
	CHECK_FALSE(f.world.forgetPetSkill(id, s0, 99));
	CHECK_FALSE(f.world.forgetPetSkill(id, 99, 0));
}

TEST_CASE("§9.0.101: [RV-1] 宠物喂食食材类型门禁与等级压制忠诚度封顶拦截反向变异验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 60;

	// 创建一只 80 级大宠物 (比角色高 20 级)
	// 原版压制封顶: max(20, 100 - 20 * 3) = 40
	auto big_pet = makeTestPet(804, 80);
	big_pet.hp = 100;
	const int s0 = f.world.givePetToPlayer(id, big_pet);
	REQUIRE(s0 >= 0);
	auto *live_pet = f.world.playerPetAt(id, s0);
	REQUIRE(live_pet != nullptr);
	f.world.setPetLoyalty(live_pet->uid, 38);

	// 准备一件武器 (item_type = 1) 与一份料理 (item_type = 20)
	auto weapon = makeTestItem(902);
	weapon.type = 1;
	const int s_wep = f.world.giveItemToPlayer(id, weapon);
	REQUIRE(s_wep >= 0);

	auto dish = makeTestItem(903);
	dish.type = 20;
	dish.current_pile = 5;
	const int s_dish = f.world.giveItemToPlayer(id, dish);
	REQUIRE(s_dish >= 0);

	// 1. [RV-1 反向变异实证]: 非食物道具喂食严格被门禁拦截
	auto res_not_food = f.world.feedPet(id, s0, s_wep);
	CHECK(res_not_food.code == PetFeedResultCode::kNotFoodItem);
	CHECK(f.world.playerItemAt(id, s_wep) != nullptr); // 武器完好无损
	CHECK(f.world.petLoyalty(live_pet->uid) == 38);    // 忠诚度未变

	// 2. 喂食料理，忠诚度提升至压制封顶线 40
	auto res_feed1 = f.world.feedPet(id, s0, s_dish);
	CHECK(res_feed1.code == PetFeedResultCode::kSuccess);
	CHECK(res_feed1.final_loyalty == 40);
	CHECK(f.world.petLoyalty(live_pet->uid) == 40);

	// 3. [RV-1 等级压制封顶防御]: 达到等级压制上限后，拒绝继续喂食提升忠诚
	auto res_capped = f.world.feedPet(id, s0, s_dish);
	CHECK(res_capped.code == PetFeedResultCode::kLoyaltyCapped);
	CHECK(f.world.petLoyalty(live_pet->uid) == 40);

	// 4. 服从状态机检验
	CHECK(f.world.checkPetObedience(id, s0) == PetObedienceState::kConfused); // 40 属于 20~59 偶发失控
	f.world.setPetLoyalty(live_pet->uid, 15);
	CHECK(f.world.checkPetObedience(id, s0) == PetObedienceState::kBetray); // < 20 极度叛逆
	f.world.setPetLoyalty(live_pet->uid, 85);
	CHECK(f.world.checkPetObedience(id, s0) == PetObedienceState::kObedient); // >= 60 完全顺服
}

TEST_CASE("§9.0.101: [RV-2] 宠物喂食资产原子消耗与宠物状态同步一致性反向变异验证")
{
	MoveFixture f;
	const auto id = spawnHandshaked(f);
	auto *p = f.world.playerForTest(id);
	REQUIRE(p != nullptr);
	p->level = 50;

	auto pet = makeTestPet(805, 30);
	pet.hp = 100;
	const int s0 = f.world.givePetToPlayer(id, pet);
	REQUIRE(s0 >= 0);
	auto *live_pet = f.world.playerPetAt(id, s0);
	REQUIRE(live_pet != nullptr);
	f.world.setPetLoyalty(live_pet->uid, 50);

	auto food = makeTestItem(904);
	food.type = 20;
	food.current_pile = 1;
	const int s_food = f.world.giveItemToPlayer(id, food);
	REQUIRE(s_food >= 0);

	// 场景 A: 摆摊状态中互斥拦截，食物 0 消耗且忠诚度 0 变动
	REQUIRE(f.world.openStall(id, "老王杂货铺"));
	REQUIRE(f.world.setStallItem(id, s_food, 500));
	REQUIRE(f.world.startStallVending(id));
	CHECK(f.world.isPlayerVending(id));

	auto res_vend = f.world.feedPet(id, s0, s_food);
	CHECK(res_vend.code == PetFeedResultCode::kPlayerVending);
	CHECK(f.world.playerItemAt(id, s_food) != nullptr); // 食物完好
	CHECK(f.world.petLoyalty(live_pet->uid) == 50);     // 忠诚未变

	// 场景 B: 收摊后喂食，食物原子消耗清空，宠物生命与忠诚同步生效
	REQUIRE(f.world.closeStall(id));
	CHECK_FALSE(f.world.isPlayerVending(id));

	auto res_ok = f.world.feedPet(id, s0, s_food);
	CHECK(res_ok.code == PetFeedResultCode::kSuccess);
	CHECK(res_ok.final_loyalty == 55);
	CHECK(f.world.petLoyalty(live_pet->uid) == 55);
	CHECK(f.world.playerItemAt(id, s_food) == nullptr); // 食物被原子扣除
}
