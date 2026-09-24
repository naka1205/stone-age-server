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
void loadEncounterFixture(World &world, std::int32_t prob)
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
