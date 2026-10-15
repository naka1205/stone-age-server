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

// ── 阶段 20: 真实地下城多层地图装载与经典剧情任务闭环 (五兄弟的山寨) ──────────

static SA::Model::Item makeItem(std::int32_t item_id, const char *name = "测试道具")
{
	SA::Model::Item it{};
	it.item_id = item_id;
	(void)it.name.assign(name);
	return it;
}

static SA::Model::Pet makePet(std::int32_t pet_id, const char *name = "测试战宠")
{
	SA::Model::Pet pet{};
	pet.pet_id = pet_id;
	(void)pet.name.assign(name);
	pet.level = 1;
	pet.hp = 100;
	pet.mp = 50;
	pet.vital = 20;
	pet.str = 20;
	pet.tough = 20;
	pet.dex = 20;
	return pet;
}

TEST_CASE("五兄弟山寨 (Floor 31701..31706) 预设地下城装配与层级拓扑核验 (阶段 20)")
{
	TestFixture f;
	REQUIRE(f.world.loadPresetDungeons());

	const auto d = f.world.findDungeon(kDungeonFiveBrothers);
	REQUIRE(d.has_value());
	CHECK(d->name == "五兄弟的山寨");
	CHECK(d->entry_floor == 31701);
	CHECK(d->entry_x == 29);
	CHECK(d->entry_y == 49);
	CHECK(d->exit_floor == 300);
	CHECK(d->exit_x == 343);
	CHECK(d->exit_y == 191);
	CHECK(d->min_entry_level == 25);
	REQUIRE(d->floors.size() == 6);

	// 1F ~ 5F 黑暗洞窟受限视野 (2 格)
	for (std::size_t i = 0; i < 5; ++i)
	{
		CHECK(d->floors[i].is_dark);
		CHECK(d->floors[i].default_vision_radius == 2);
		CHECK(f.world.findFloorMap(d->floors[i].floor_id) != nullptr);
	}

	// 6F 圣殿明亮神殿 (10 格大视野)
	CHECK_FALSE(d->floors[5].is_dark);
	CHECK(d->floors[5].default_vision_radius == 10);
	CHECK(f.world.findFloorMap(d->floors[5].floor_id) != nullptr);
	CHECK(f.world.findFloorMap(300) != nullptr);
}

TEST_CASE("五兄弟山寨逐层攀爬与任务门禁旗标拦截 (阶段 20)")
{
	TestFixture f;
	REQUIRE(f.world.loadPresetDungeons());

	const auto sid = f.spawnPlayer("Hero", 300, 343, 191, /*level=*/35);

	// 1. 进入五兄弟地下城 -> 传送至 1F (31701, 29, 49)
	REQUIRE(f.world.enterDungeon(sid, kDungeonFiveBrothers));
	CHECK(f.world.playerPos(sid).floor == 31701);
	CHECK(f.world.playerPos(sid).x == 29);
	CHECK(f.world.playerPos(sid).y == 49);
	CHECK(f.world.isPlayerInDarkCave(sid));
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 2);

	auto *p = f.world.playerForTest(sid);
	REQUIRE(p != nullptr);

	// 2. 1F 走向下楼楼梯 (11, 4) -> 到达 2F (31702, 20, 2)
	p->x = 11;
	p->y = 4;
	REQUIRE(f.world.triggerDungeonStair(sid));
	CHECK(f.world.playerPos(sid).floor == 31702);
	CHECK(f.world.playerPos(sid).x == 20);
	CHECK(f.world.playerPos(sid).y == 2);

	// 3. 2F 走向上楼楼梯 (20, 2) -> 返回 1F (31701, 11, 4)
	p->x = 20;
	p->y = 2;
	REQUIRE(f.world.triggerDungeonStair(sid));
	CHECK(f.world.playerPos(sid).floor == 31701);
	CHECK(f.world.playerPos(sid).x == 11);
	CHECK(f.world.playerPos(sid).y == 4);

	// 4. 重回 2F 并一路攀爬到 5F (31705)
	p->x = 11;
	p->y = 4;
	REQUIRE(f.world.triggerDungeonStair(sid)); // 1F -> 2F
	p->x = 37;
	p->y = 41;
	REQUIRE(f.world.triggerDungeonStair(sid)); // 2F -> 3F (31703, 45, 41)
	CHECK(f.world.playerPos(sid).floor == 31703);
	CHECK(f.world.playerPos(sid).x == 45);
	CHECK(f.world.playerPos(sid).y == 41);

	p->x = 10;
	p->y = 4;
	REQUIRE(f.world.triggerDungeonStair(sid)); // 3F -> 4F (31704, 5, 5)
	CHECK(f.world.playerPos(sid).floor == 31704);
	CHECK(f.world.playerPos(sid).x == 5);
	CHECK(f.world.playerPos(sid).y == 5);

	p->x = 25;
	p->y = 25;
	REQUIRE(f.world.triggerDungeonStair(sid)); // 4F -> 5F (31705, 10, 10)
	CHECK(f.world.playerPos(sid).floor == 31705);
	CHECK(f.world.playerPos(sid).x == 10);
	CHECK(f.world.playerPos(sid).y == 10);
	CHECK(f.world.isPlayerInDarkCave(sid));

	// 5. 5F 到 6F 的楼梯位于 (45, 65)，门禁要求前置任务旗标 28！
	p->x = 45;
	p->y = 65;
	// 尚未完成前置任务，踩在楼梯上被拒放行！
	CHECK_FALSE(f.world.triggerDungeonStair(sid));
	CHECK(f.world.playerPos(sid).floor == 31705);
	CHECK(f.world.playerPos(sid).x == 45);
	CHECK(f.world.playerPos(sid).y == 65);

	// 6. 达成前置任务旗标 28 (NowEvent 或 EndEvent)
	p->setNowEvent(28);
	REQUIRE(f.world.triggerDungeonStair(sid));

	// 成功进入 6F 明亮圣殿 (31706, 25, 75)
	CHECK(f.world.playerPos(sid).floor == 31706);
	CHECK(f.world.playerPos(sid).x == 25);
	CHECK(f.world.playerPos(sid).y == 75);
	CHECK_FALSE(f.world.isPlayerInDarkCave(sid));
	CHECK(f.world.playerEffectiveVisionRadius(sid) == 10);
}

TEST_CASE("6 楼五兄弟誓约与守护神兽奖励全流程事务闭环 (阶段 20)")
{
	TestFixture f;
	REQUIRE(f.world.loadPresetDungeons());

	const auto sid = f.spawnPlayer("Brave", 31706, 20, 20, /*level=*/50);
	auto *p = f.world.playerForTest(sid);
	REQUIRE(p != nullptr);

	// ── 异常分支 1: 背包无“智慧水晶” (Item ID: 2517) ──
	auto r1 = f.world.concludeFiveBrothersQuest(sid, 1, 0);
	CHECK_FALSE(r1.success);
	CHECK(r1.message.find("智慧的挡晶") != std::string::npos);

	// ── 异常分支 2: 携带智慧水晶，但随身宠物栏满 (5 槽全满) ──
	const int crystal_slot = f.world.giveItemToPlayer(sid, makeItem(2517, "智慧水晶"));
	REQUIRE(crystal_slot >= 0);

	for (int i = 0; i < 5; ++i)
	{
		REQUIRE(f.world.givePetToPlayer(sid, makePet(100 + i, "已有战宠")) >= 0);
	}
	CHECK(p->findFreePetSlot() == -1);

	auto r2 = f.world.concludeFiveBrothersQuest(sid, 1, 0);
	CHECK_FALSE(r2.success);
	CHECK(r2.message.find("宠物栏空下来") != std::string::npos);
	// 验证智慧水晶未被误扣！
	CHECK(f.world.playerItemForTest(sid, crystal_slot) != nullptr);
	CHECK(f.world.playerItemForTest(sid, crystal_slot)->item_id == 2517);

	// ── 异常分支 3: 宠物栏有空位，但背包除水晶外无空槽 ──
	REQUIRE(p->clearPetSlot(4)); // 释放 1 个宠槽
	CHECK(p->findFreePetSlot() >= 0);

	// 填满背包其余 14 个道具槽
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (static_cast<int>(i) != crystal_slot && !p->items[i].valid())
		{
			REQUIRE(f.world.giveItemToPlayer(sid, makeItem(9000, "杂物")) >= 0);
		}
	}

	auto r3 = f.world.concludeFiveBrothersQuest(sid, 1, 0);
	CHECK_FALSE(r3.success);
	CHECK(r3.message.find("预留一个道具栏空位") != std::string::npos);
	// 水晶仍然完好
	CHECK(f.world.playerItemForTest(sid, crystal_slot) != nullptr);

	// ── 正常分支: 腾出背包空位，正式与大地守护者【利诺恩】誓约 (index = 0) ──
	const int dummy_slot = (crystal_slot == static_cast<int>(SA::Model::kStartItemArray))
	                           ? static_cast<int>(SA::Model::kStartItemArray) + 1
	                           : static_cast<int>(SA::Model::kStartItemArray);
	REQUIRE(p->clearItemSlot(dummy_slot)); // 腾出 1 个杂物背包位 (保留智慧水晶)

	auto r_earth = f.world.concludeFiveBrothersQuest(sid, 1, 0);
	CHECK(r_earth.success);
	CHECK(r_earth.item_id == 2518); // 地之首饰
	CHECK(r_earth.pet_id == 54);    // 战宠利诺恩
	CHECK(r_earth.message.find("地之首饰") != std::string::npos);

	// 水晶已被原子扣减消耗，新发放的首饰放入了空出的槽位
	bool found_crystal = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (const auto *it = f.world.playerItemForTest(sid, static_cast<int>(i)))
		{
			if (it->item_id == 2517)
				found_crystal = true;
		}
	}
	CHECK_FALSE(found_crystal);

	// 确认获得首饰与宠物
	bool found_necklace = false;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (const auto *it = f.world.playerItemForTest(sid, static_cast<int>(i)))
		{
			if (it->item_id == 2518)
				found_necklace = true;
		}
	}
	CHECK(found_necklace);

	// 宠物栏中存在利诺恩 (pet_id = 54)
	bool found_pet = false;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (const auto *pet = f.world.playerPetForTest(sid, static_cast<int>(i)))
		{
			if (pet->pet_id == 54)
				found_pet = true;
		}
	}
	CHECK(found_pet);

	// 登记终生任务完成旗标 (EndSetFlg 31)
	CHECK(p->hasEndEvent(31));
}

TEST_CASE("五兄弟其余四位守护者奖励映射准确性 (奇洛斯/浦洛斯/吉诺/浦鲁德)")
{
	TestFixture f;
	REQUIRE(f.world.loadPresetDungeons());

	// index 1: 奇洛斯 (水火) -> 2520 水之戒指, Pet 55
	{
		const auto sid = f.spawnPlayer("P1", 31706, 20, 20);
		f.world.giveItemToPlayer(sid, makeItem(2517, "智慧水晶"));
		auto r = f.world.concludeFiveBrothersQuest(sid, 2, 1);
		CHECK(r.success);
		CHECK(r.item_id == 2520);
		CHECK(r.pet_id == 55);
		CHECK(r.message.find("水之戒指") != std::string::npos);
	}

	// index 2: 浦洛斯 (火) -> 2526 火之笛, Pet 56
	{
		const auto sid = f.spawnPlayer("P2", 31706, 20, 20);
		f.world.giveItemToPlayer(sid, makeItem(2517, "智慧水晶"));
		auto r = f.world.concludeFiveBrothersQuest(sid, 3, 2);
		CHECK(r.success);
		CHECK(r.item_id == 2526);
		CHECK(r.pet_id == 56);
		CHECK(r.message.find("火之笛") != std::string::npos);
	}

	// index 3: 吉诺 (火风) -> 2519 风之手环, Pet 57
	{
		const auto sid = f.spawnPlayer("P3", 31706, 20, 20);
		f.world.giveItemToPlayer(sid, makeItem(2517, "智慧水晶"));
		auto r = f.world.concludeFiveBrothersQuest(sid, 4, 3);
		CHECK(r.success);
		CHECK(r.item_id == 2519);
		CHECK(r.pet_id == 57);
		CHECK(r.message.find("风之手环") != std::string::npos);
	}

	// index 4: 浦鲁德 (风火) -> 2525 风火竖琴, Pet 481
	{
		const auto sid = f.spawnPlayer("P4", 31706, 20, 20);
		f.world.giveItemToPlayer(sid, makeItem(2517, "智慧水晶"));
		auto r = f.world.concludeFiveBrothersQuest(sid, 5, 4);
		CHECK(r.success);
		CHECK(r.item_id == 2525);
		CHECK(r.pet_id == 481);
		CHECK(r.message.find("风火竖琴") != std::string::npos);
	}
}

TEST_CASE("6 楼神圣石板 (kExit) 终点脱离直返吉鲁大世界地面")
{
	TestFixture f;
	REQUIRE(f.world.loadPresetDungeons());

	const auto sid = f.spawnPlayer("Pilgrim", 300, 343, 191, /*level=*/35);
	REQUIRE(f.world.enterDungeon(sid, kDungeonFiveBrothers));

	auto *p = f.world.playerForTest(sid);
	p->floor = 31706; // 直接置于 6 楼圣殿
	p->x = 39;
	p->y = 6; // 踏上神圣石板坐标

	// 触发神圣石板脱离
	REQUIRE(f.world.triggerDungeonStair(sid));

	// 检查瞬间重返吉鲁大世界地面 Floor 300 (343, 191)
	CHECK(f.world.playerPos(sid).floor == 300);
	CHECK(f.world.playerPos(sid).x == 343);
	CHECK(f.world.playerPos(sid).y == 191);
	CHECK_FALSE(f.world.isPlayerInDarkCave(sid));
}

TEST_CASE("五兄弟地下城反向变异与异常边界防御")
{
	TestFixture f;
	REQUIRE(f.world.loadPresetDungeons());

	const auto sid = f.spawnPlayer("BoundaryTester", 31706, 20, 20);
	f.world.giveItemToPlayer(sid, makeItem(2517, "智慧水晶"));

	// 1. 非法 brother_index (< 0 或 > 4)
	auto r_neg = f.world.concludeFiveBrothersQuest(sid, 1, -1);
	CHECK_FALSE(r_neg.success);
	CHECK(r_neg.message == "未知的守护者。");

	auto r_overflow = f.world.concludeFiveBrothersQuest(sid, 1, 5);
	CHECK_FALSE(r_overflow.success);
	CHECK(r_overflow.message == "未知的守护者。");

	// 2. 玩家处于阵亡或重伤倒地状态 (hp = 0)
	auto *p = f.world.playerForTest(sid);
	p->hp = 0;
	auto r_dead = f.world.concludeFiveBrothersQuest(sid, 1, 0);
	CHECK_FALSE(r_dead.success);
	CHECK(r_dead.message == "当前状态无法进行誓约。");
}

TEST_CASE("阶段 23 尼斯大陆经典洞窟拓扑扩充与地下城生态贯通 (阿布洞窟与奇努伊海底通路)")
{
	TestFixture f;
	REQUIRE(f.world.loadPresetDungeons());

	// 1. 验证阿布洞窟注册与楼层拓扑 (20901..20904)
	auto abu_opt = f.world.findDungeon(kDungeonAbu);
	REQUIRE(abu_opt.has_value());
	CHECK(abu_opt->name == "阿布的洞窟");
	CHECK(abu_opt->floors.size() == 4);
	CHECK(abu_opt->min_entry_level == 15);

	const auto sid_abu = f.spawnPlayer("AbuExplorer", 200, 24, 40, /*level=*/16);
	REQUIRE(f.world.enterDungeon(sid_abu, kDungeonAbu));
	CHECK(f.world.playerPos(sid_abu).floor == 20901);

	// 2. 验证奇努伊海底洞窟注册与楼层拓扑 (20701..20704)
	auto chinue_opt = f.world.findDungeon(kDungeonChinue);
	REQUIRE(chinue_opt.has_value());
	CHECK(chinue_opt->name == "奇努伊海底洞窟");
	CHECK(chinue_opt->floors.size() == 4);
	CHECK(chinue_opt->min_entry_level == 20);

	const auto sid_chinue = f.spawnPlayer("ChinueDiver", 300, 45, 32, /*level=*/22);
	REQUIRE(f.world.enterDungeon(sid_chinue, kDungeonChinue));
	CHECK(f.world.playerPos(sid_chinue).floor == 20701);
	CHECK(f.world.isPlayerInDarkCave(sid_chinue));
}
