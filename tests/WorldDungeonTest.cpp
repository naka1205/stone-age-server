// tests/WorldDungeonTest.cpp —— 地下城多层迷宫与漆黑洞窟视野探险系统测试 (阶段 18 §9.0.134)
//
// 覆盖:
//   1. 地下城全域模型注册、楼层字典索引与非法配置拒绝
//   2. 门禁等级检验、地下城进入与安全撤回地面
//   3. 迷宫多层上下楼梯传送阵 (DungeonStair) 触发与层级穿梭
//   4. 漆黑洞窟黑雾与默认受限视野半径 (is_dark / default_vision_radius = 2)
//   5. 火把照明道具 (useTorch) 视野拓展 (8 格) 与超时衰减恢复
//   6. 黑暗洞窟惊吓遇敌倍率 (1.3x) 与光照消除机制

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "world/Api.h"

#include <memory>
#include <string>
#include <vector>

using namespace SA::World;

namespace
{

SA::Platform::ServerConfig makeTestConfig()
{
	const SA::Platform::ConfigResult r = SA::Platform::parseConfig(R"({
    "protocol_version": 1, "log_level": "error",
    "tempo": { "tick_hz": 100, "battle_turn_interval_ms": 1000 }
  })");
	REQUIRE(r.ok);
	return r.config;
}

struct TestFixture
{
	SA::Platform::ServerConfig config = makeTestConfig();
	SA::Platform::ManualClock clock{1000000};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0x123456};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};

	SA::Net::SessionId spawnPlayer(const std::string &name, std::int32_t floor = 1000,
	                               std::int32_t x = 20, std::int32_t y = 20,
	                               std::int32_t level = 10)
	{
		const auto sid = transport.connect();
		world.onSessionReady(sid);
		REQUIRE(world.setPlayerName(sid, name));
		auto *p = world.playerForTest(sid);
		REQUIRE(p != nullptr);
		p->floor = floor;
		p->x = x;
		p->y = y;
		p->gold = 100000;
		p->level = level;
		p->hp = 100;
		p->max_mp = 100;
		p->mp = 100;
		return sid;
	}

	void setupDungeonMaps()
	{
		// 注册地面村庄与地下城测试地图 (kFree = 2 地面)
		world.loadFloorMap(1000, makeFixtureMap(50, 50)); // 地面村庄
		world.loadFloorMap(7001, makeFixtureMap(50, 50)); // 漆黑洞窟 1F (明亮)
		world.loadFloorMap(7002, makeFixtureMap(50, 50)); // 漆黑洞窟 2F (黑暗)
		world.loadFloorMap(7003, makeFixtureMap(50, 50)); // 漆黑洞窟 3F (黑暗深层)
	}

	DungeonDefinition makeTestDungeon()
	{
		DungeonDefinition d{};
		d.dungeon_id = 1;
		d.name = "漆黑的洞窟";
		d.min_entry_level = 20;
		d.entry_floor = 7001;
		d.entry_x = 10;
		d.entry_y = 10;
		d.exit_floor = 1000;
		d.exit_x = 25;
		d.exit_y = 25;

		// 1F: 普通明亮洞窟
		DungeonFloorConfig f1{};
		f1.floor_id = 7001;
		f1.depth = 1;
		f1.floor_name = "漆黑洞窟 1 层";
		f1.is_dark = false;
		f1.default_vision_radius = 10;
		f1.min_level = 20;

		DungeonStair s1_down{};
		s1_down.type = DungeonStairType::kDown;
		s1_down.trigger_x = 15;
		s1_down.trigger_y = 15;
		s1_down.target_floor = 7002;
		s1_down.target_x = 5;
		s1_down.target_y = 5;
		s1_down.min_level = 20;
		f1.stairs.push_back(s1_down);
		d.floors.push_back(f1);

		// 2F: 漆黑洞窟 (黑暗视野 2 格, 遇敌率放大 1.3 倍)
		DungeonFloorConfig f2{};
		f2.floor_id = 7002;
		f2.depth = 2;
		f2.floor_name = "漆黑洞窟 2 层";
		f2.is_dark = true;
		f2.default_vision_radius = 2;
		f2.min_level = 20;
		f2.dark_encounter_multiplier = 1.3;

		DungeonStair s2_up{};
		s2_up.type = DungeonStairType::kUp;
		s2_up.trigger_x = 5;
		s2_up.trigger_y = 4;
		s2_up.target_floor = 7001;
		s2_up.target_x = 15;
		s2_up.target_y = 14;
		s2_up.min_level = 20;
		f2.stairs.push_back(s2_up);

		DungeonStair s2_down{};
		s2_down.type = DungeonStairType::kDown;
		s2_down.trigger_x = 25;
		s2_down.trigger_y = 25;
		s2_down.target_floor = 7003;
		s2_down.target_x = 10;
		s2_down.target_y = 10;
		s2_down.min_level = 30; // 3F 门槛较高
		f2.stairs.push_back(s2_down);
		d.floors.push_back(f2);

		// 3F: 漆黑洞窟深层
		DungeonFloorConfig f3{};
		f3.floor_id = 7003;
		f3.depth = 3;
		f3.floor_name = "漆黑洞窟 3 层 (深渊核心)";
		f3.is_dark = true;
		f3.default_vision_radius = 2;
		f3.min_level = 30;
		f3.dark_encounter_multiplier = 1.5;

		DungeonStair s3_up{};
		s3_up.type = DungeonStairType::kUp;
		s3_up.trigger_x = 10;
		s3_up.trigger_y = 9;
		s3_up.target_floor = 7002;
		s3_up.target_x = 25;
		s3_up.target_y = 24;
		s3_up.min_level = 20;
		f3.stairs.push_back(s3_up);
		d.floors.push_back(f3);

		return d;
	}
};

} // namespace

TEST_CASE("地下城全域模型注册与配置查询")
{
	TestFixture f;
	f.setupDungeonMaps();

	// 1. 非法配置拒绝
	DungeonDefinition invalid_d{};
	invalid_d.dungeon_id = 0;
	CHECK_FALSE(f.world.registerDungeon(invalid_d));

	invalid_d.dungeon_id = 1; // floors 为空
	CHECK_FALSE(f.world.registerDungeon(invalid_d));

	// 2. 正常注册
	const auto d = f.makeTestDungeon();
	REQUIRE(f.world.registerDungeon(d));

	// 3. 查询地下城信息
	auto opt = f.world.findDungeon(1);
	REQUIRE(opt.has_value());
	CHECK(opt->name == "漆黑的洞窟");
	CHECK(opt->min_entry_level == 20);
	CHECK(opt->entry_floor == 7001);
	CHECK(opt->entry_x == 10);
	CHECK(opt->entry_y == 10);
	CHECK(opt->exit_floor == 1000);
	CHECK(opt->floors.size() == 3);
	CHECK(opt->floors[1].floor_name == "漆黑洞窟 2 层");
	CHECK(opt->floors[1].is_dark == true);

	CHECK_FALSE(f.world.findDungeon(999).has_value());
}

TEST_CASE("地下城进入门禁校验与安全撤回地面")
{
	TestFixture f;
	f.setupDungeonMaps();
	REQUIRE(f.world.registerDungeon(f.makeTestDungeon()));

	// 玩家等级 10 (门槛为 20)
	const auto sid = f.spawnPlayer("Bob", 1000, 20, 20, /*level=*/10);
	CHECK_FALSE(f.world.isPlayerInDungeon(sid));

	// 1. 等级不足拒绝进入
	CHECK_FALSE(f.world.enterDungeon(sid, 1));
	CHECK(f.world.playerPos(sid).floor == 1000);

	// 2. 提升等级达标
	auto *p = f.world.playerForTest(sid);
	p->level = 25;
	REQUIRE(f.world.enterDungeon(sid, 1));

	// 进入成功：坐标已瞬移至入口层 7001 (10, 10)
	CHECK(f.world.isPlayerInDungeon(sid));
	CHECK(f.world.playerCurrentDungeonId(sid) == 1);
	CHECK(f.world.playerPos(sid).floor == 7001);
	CHECK(f.world.playerPos(sid).x == 10);
	CHECK(f.world.playerPos(sid).y == 10);

	// 3. 安全撤回地面
	REQUIRE(f.world.leaveDungeon(sid));
	CHECK_FALSE(f.world.isPlayerInDungeon(sid));
	CHECK_FALSE(f.world.playerCurrentDungeonId(sid).has_value());
	CHECK(f.world.playerPos(sid).floor == 1000);
	CHECK(f.world.playerPos(sid).x == 25);
	CHECK(f.world.playerPos(sid).y == 25);
}

TEST_CASE("迷宫上下楼梯传送阵 (DungeonStair) 触发与层级穿梭")
{
	TestFixture f;
	f.setupDungeonMaps();
	REQUIRE(f.world.registerDungeon(f.makeTestDungeon()));

	const auto sid = f.spawnPlayer("Charlie", 1000, 20, 20, /*level=*/25);
	REQUIRE(f.world.enterDungeon(sid, 1)); // 位于 7001 (10, 10)

	auto *p = f.world.playerForTest(sid);

	// 1. 未踩楼梯触发无效
	CHECK_FALSE(f.world.triggerDungeonStair(sid));

	// 2. 走到 1F 下楼楼梯 (15, 15)
	p->x = 15;
	p->y = 15;
	REQUIRE(f.world.triggerDungeonStair(sid));

	// 验证进入 2F (5, 5)
	CHECK(f.world.playerPos(sid).floor == 7002);
	CHECK(f.world.playerPos(sid).x == 5);
	CHECK(f.world.playerPos(sid).y == 5);

	// 3. 走到 2F 上楼楼梯 (5, 4)
	p->x = 5;
	p->y = 4;
	REQUIRE(f.world.triggerDungeonStair(sid));

	// 验证返回 1F (15, 14)
	CHECK(f.world.playerPos(sid).floor == 7001);
	CHECK(f.world.playerPos(sid).x == 15);
	CHECK(f.world.playerPos(sid).y == 14);

	// 再次进入 2F
	p->x = 15;
	p->y = 15;
	REQUIRE(f.world.triggerDungeonStair(sid));
	CHECK(f.world.playerPos(sid).floor == 7002);

	// 4. 2F 下往 3F 楼梯门禁检查 (需等级 30，当前 25)
	p->x = 25;
	p->y = 25;
	CHECK_FALSE(f.world.triggerDungeonStair(sid)); // 等级不足阻断
	CHECK(f.world.playerPos(sid).floor == 7002);

	// 提升等级至 32
	p->level = 32;
	REQUIRE(f.world.triggerDungeonStair(sid));
	CHECK(f.world.playerPos(sid).floor == 7003);
	CHECK(f.world.playerPos(sid).x == 10);
	CHECK(f.world.playerPos(sid).y == 10);
}

TEST_CASE("漆黑洞窟黑雾与默认受限视野半径")
{
	TestFixture f;
	f.setupDungeonMaps();
	REQUIRE(f.world.registerDungeon(f.makeTestDungeon()));

	const auto sid = f.spawnPlayer("Diana", 1000, 20, 20, /*level=*/25);

	// 1. 地面村庄：非黑暗洞窟，标准全开视野 (10 格)
	CHECK_FALSE(f.world.isPlayerInDarkCave(sid));
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 10);

	// 2. 洞窟 1F：虽是地下城，但配置为非黑暗洞窟 (明亮)
	REQUIRE(f.world.enterDungeon(sid, 1));
	CHECK_FALSE(f.world.isPlayerInDarkCave(sid));
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 10);

	// 3. 穿梭至洞窟 2F：配置为 is_dark = true
	auto *p = f.world.playerForTest(sid);
	p->x = 15;
	p->y = 15;
	REQUIRE(f.world.triggerDungeonStair(sid));
	CHECK(f.world.playerPos(sid).floor == 7002);

	// 漆黑洞窟：视野受黑雾阻挡，默认受限为 2 格！
	CHECK(f.world.isPlayerInDarkCave(sid));
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 2);
}

TEST_CASE("火把照明道具 (useTorch) 视野拓展与超时衰减恢复")
{
	TestFixture f;
	f.setupDungeonMaps();
	REQUIRE(f.world.registerDungeon(f.makeTestDungeon()));

	const auto sid = f.spawnPlayer("Edward", 7002, 10, 10, /*level=*/25);
	REQUIRE(f.world.isPlayerInDarkCave(sid));
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 2);

	// 1. 点燃火把 (持续 60,000 毫秒，光照半径 8 格)
	REQUIRE(f.world.useTorch(sid, 60000, 8));

	// 光照拓展生效
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 8);

	// 2. 时间推移 30,000 毫秒：火把仍在燃烧
	f.clock.advance(30000);
	f.world.tick();
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 8);

	// 3. 时间推移 35,000 毫秒 (累计 65,000 毫秒)：火把燃尽熄灭！
	f.clock.advance(35000);
	f.world.tick();

	// 视野自然衰减恢复为黑雾默认值 2 格
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 2);
}

TEST_CASE("黑暗洞窟惊吓遇敌倍率 (1.3x) 与火把光照消除")
{
	TestFixture f;
	f.setupDungeonMaps();
	REQUIRE(f.world.registerDungeon(f.makeTestDungeon()));

	const auto sid = f.spawnPlayer("Frank", 7002, 10, 10, /*level=*/25);

	// 1. 漆黑洞窟中无火把：视线受阻与惊吓，遇敌率放大 1.3 倍
	CHECK(f.world.playerEncounterRateMultiplier(sid) == doctest::Approx(1.3));

	// 2. 点燃火把：光明消除惊吓，遇敌率回归基准 1.0
	REQUIRE(f.world.useTorch(sid, 60000, 8));
	CHECK(f.world.playerEncounterRateMultiplier(sid) == doctest::Approx(1.0));

	// 3. 离开地下城到地面：遇敌率恒为 1.0
	REQUIRE(f.world.leaveDungeon(sid));
	CHECK(f.world.playerEncounterRateMultiplier(sid) == doctest::Approx(1.0));
}
