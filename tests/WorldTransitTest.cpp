// tests/WorldTransitTest.cpp —— 经典长尾 NPC 交通与世界设施系统测试 (阶段 16 & 阶段 17)
//
// 覆盖:
//   1. NPC:StoryTeller 游历吟游诗人 (18 行纯 C NPC, 原版 npc_storyteller.c)
//   2. NPC:TimeMan 昼夜与世界时钟守望者 (227 行纯 C NPC, 原版 npc_timeman.c)
//   3. NPC:SavePoint 四大村庄记录点长者 (532 行纯 C NPC, 原版 npc_savepoint.c)
//      - 记录点查询、Yes/No 确认弹窗、坐标与村庄名记录
//      - 重复对话显示星球启动时长与在线人数 (原版 1:1 对白)
//      - 玩家 respawnPlayer 死亡/回城重生传送与保底 HP 恢复
//   4. NPC:Bus 长毛象公交车交通网 (729 行纯 C NPC, 原版 npc_bus.c)
//      - 组队状态拒绝乘车 (NPC_BUS_MSG_NOTPARTY)
//      - 等级与石币门禁校验 (NPC_BUS_MSG_LEVEL, NPC_BUS_MSG_GOLD)
//      - 正常购票登车与 GoldLedger kWarpFee 扣费
//      - 站点运送瞬移与到达提示 (NPC_BUS_MSG_START, NPC_BUS_MSG_END)
//   5. NPC:Charm 美容师 (286 行纯 C NPC, 原版 npc_charm.c)
//      - 经典公式计费 cost = level * 10 * (charm / 3) * (trans + 1)
//      - 魅力上升 +5、封顶 100 校验与 GoldLedger kCharmFee 扣费
//      - 携带宠物忠诚度滋养提升联动 (complianceParameter)
//   6. NPC:Oldman 称号长者 (111 行纯 C NPC, 原版 npc_oldman.c)
//      - 等级/转生门槛核定、资格称号自动发放与提示
//      - 未达标或已领取时的原版经典台词复刻
//   7. NPC:FmHealer 家族医者 (145 行纯 C NPC, 原版 npc_fmhealer.c)
//      - 领地与家族成员识别、个人及随行宠物满血满蓝与复活
//      - 组队全队队员及全员宠物集体回复广播
//   8. NPC:TranserMan 特使传送师 (192 行纯 C NPC, 原版 npc_transerman.c)
//      - 组队状态拦截 ("不能帮组队中的人传送")
//      - 等级与石币门槛校验、GoldLedger kWarpFee 扣费与快捷传送

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
	                               std::int32_t gold = 500000, std::int32_t level = 10)
	{
		const auto sid = transport.connect();
		world.onSessionReady(sid);
		REQUIRE(world.setPlayerName(sid, name));
		auto *p = world.playerForTest(sid);
		REQUIRE(p != nullptr);
		p->floor = floor;
		p->x = x;
		p->y = y;
		p->gold = gold;
		p->level = level;
		p->hp = 100;
		p->max_mp = 100;
		p->mp = 100;
		return sid;
	}
};

} // namespace

TEST_CASE("NPC:StoryTeller 游历吟游诗人传说讲述")
{
	TestFixture f;
	const auto sid = f.spawnPlayer("Alice", 1000, 20, 20);

	// 部署 StoryTeller NPC 于 (1000, 20, 21) 面对玩家
	NpcEntity st_npc{};
	st_npc.id = 7001;
	st_npc.type = NpcType::kStoryTeller;
	st_npc.floor = 1000;
	st_npc.x = 20;
	st_npc.y = 21;
	st_npc.name = "吟游诗人奥德";
	st_npc.message = "在尼斯大陆初生的纪元，精灵王赐予了这片土地四大元素的守护石……";
	f.world.loadNpcEntities({st_npc});

	// 玩家朝向 dir 4 (南方向 dy+1) 触发面前 NPC 事件交互
	SA::Domain::EventRequest req{};
	req.seqno = 101;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.dir = 4; // y+1
	req.x = 20;
	req.y = 21;
	f.world.onEvent(sid, req);

	// 验证玩家打开对话窗口
	CHECK(f.world.playerHasActiveWindow(sid));
	const std::string win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("在尼斯大陆初生的纪元") != std::string::npos);

	// 玩家点击 OK 关闭窗口
	SA::Domain::WindowReply rep{};
	rep.window_id = f.world.playerActiveWindowId(sid);
	rep.source.entity_id = 7001;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
	f.world.onWindowReply(sid, rep);

	CHECK_FALSE(f.world.playerHasActiveWindow(sid));
}

TEST_CASE("NPC:TimeMan 昼夜与世界时钟报时")
{
	TestFixture f;
	const auto sid = f.spawnPlayer("Bob", 1000, 30, 30);

	// 部署 TimeMan NPC 于 (1000, 31, 30) (东侧 dir 2)
	NpcEntity tm_npc{};
	tm_npc.id = 7002;
	tm_npc.type = NpcType::kTimeMan;
	tm_npc.floor = 1000;
	tm_npc.x = 31;
	tm_npc.y = 30;
	tm_npc.name = "守望者克洛诺斯";
	tm_npc.message = "我是时间的记录者。";
	f.world.loadNpcEntities({tm_npc});

	// 验证独立接口返回当前世界时钟
	const std::string time_str = f.world.getWorldTimeString();
	CHECK(time_str.find("【尼斯时钟】") != std::string::npos);

	// 玩家交互 (东方向 dx+1)
	SA::Domain::EventRequest req{};
	req.seqno = 102;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.dir = 2; // x+1
	req.x = 31;
	req.y = 30;
	f.world.onEvent(sid, req);

	CHECK(f.world.playerHasActiveWindow(sid));
	const std::string win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("我是时间的记录者") != std::string::npos);
	CHECK(win_text.find("【尼斯时钟】") != std::string::npos);
}

TEST_CASE("NPC:SavePoint 记录点保存、重复访问与重生回城")
{
	TestFixture f;
	const auto sid = f.spawnPlayer("Charlie", 1000, 50, 50);

	// 初始状态下尚未记录
	const auto init_sp = f.world.getPlayerSavePoint(sid);
	CHECK(init_sp.has_value());

	// 部署 SavePoint NPC 于 (1000, 50, 51)
	NpcEntity sp_npc{};
	sp_npc.id = 7003;
	sp_npc.type = NpcType::kSavePoint;
	sp_npc.floor = 1000;
	sp_npc.x = 50;
	sp_npc.y = 51;
	sp_npc.name = "记录点长老";
	sp_npc.savepoint_name = "玛丽娜斯渔村记录点";
	f.world.loadNpcEntities({sp_npc});

	// 1. 初次对话：尚未记录，触发确认设定弹窗
	SA::Domain::EventRequest req{};
	req.seqno = 103;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.dir = 4; // y+1
	req.x = 50;
	req.y = 51;
	f.world.onEvent(sid, req);

	CHECK(f.world.playerHasActiveWindow(sid));
	std::string win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("玛丽娜斯渔村记录点") != std::string::npos);
	CHECK(win_text.find("设定为你的记录点") != std::string::npos);

	// 2. 玩家点击 YES 确认设定
	SA::Domain::WindowReply rep{};
	rep.window_id = f.world.playerActiveWindowId(sid);
	rep.source.entity_id = 7003;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(sid, rep);

	// 验证记录点已成功绑定
	const auto saved_sp = f.world.getPlayerSavePoint(sid);
	REQUIRE(saved_sp.has_value());
	CHECK(saved_sp->floor == 1000);
	CHECK(saved_sp->x == 50);
	CHECK(saved_sp->y == 51);
	CHECK(saved_sp->village_name == "玛丽娜斯渔村记录点");

	// 3. 再次对话：已是当前记录点，显示星球运行时间与在线人数
	f.world.onEvent(sid, req);
	CHECK(f.world.playerHasActiveWindow(sid));
	win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("这里已经是你的记录点了") != std::string::npos);
	CHECK(win_text.find("本星球已经启动了") != std::string::npos);
	CHECK(win_text.find("目前线上人数") != std::string::npos);

	// 4. 重生回城验证：玩家漫游到外部远古荒原 (2000, 80, 80) 并濒死战败 (hp=0)
	auto *p = f.world.playerForTest(sid);
	REQUIRE(p != nullptr);
	p->floor = 2000;
	p->x = 80;
	p->y = 80;
	p->hp = 0;

	// 调用重生
	REQUIRE(f.world.respawnPlayer(sid));
	CHECK(p->floor == 1000);
	CHECK(p->x == 50);
	CHECK(p->y == 51);
	CHECK(p->hp == 1); // 保底 1 点生命值复活
}

TEST_CASE("NPC:Bus 长毛象公车组队阻断、等级金币限制与站点运送")
{
	TestFixture f;
	const auto sid_driver = f.spawnPlayer("Driver", 1000, 40, 40, 500, 15);
	const auto sid_passenger = f.spawnPlayer("Passenger", 1000, 41, 40, 200, 8);

	// 部署 Bus NPC 于 (1000, 40, 41)
	NpcEntity bus_npc{};
	bus_npc.id = 7004;
	bus_npc.type = NpcType::kBus;
	bus_npc.floor = 1000;
	bus_npc.x = 40;
	bus_npc.y = 41;
	bus_npc.name = "长毛象公车站";

	// 配置公车路线：玛丽娜斯村 -> 萨姆吉尔村 (2000, 85, 80)，车费 100 石币，最低等级 10
	BusRoute route{};
	route.dest_floor = 2000;
	route.dest_x = 85;
	route.dest_y = 80;
	route.dest_name = "萨姆吉尔村站";
	route.ticket_cost = 100;
	route.min_level = 10;
	bus_npc.bus_routes.push_back(route);

	f.world.loadNpcEntities({bus_npc});

	// 1. 组队阻断用例：组建队伍后尝试乘车
	REQUIRE(f.world.joinParty(sid_passenger, sid_driver));
	CHECK(f.world.playerPartyMode(sid_driver) == PartyMode::kLeader);

	SA::Domain::EventRequest req{};
	req.seqno = 104;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.dir = 4; // y+1
	req.x = 40;
	req.y = 41;
	f.world.onEvent(sid_driver, req);

	CHECK(f.world.playerHasActiveWindow(sid_driver));
	std::string win_text = f.world.playerLastWindowText(sid_driver);
	CHECK(win_text.find("无法以团队加入") != std::string::npos);

	// 解散队伍
	REQUIRE(f.world.leaveParty(sid_driver));
	CHECK(f.world.playerPartyMode(sid_driver) == PartyMode::kNone);

	// 2. 乘客等级不足用例：passenger 仅 8 级 (< 10)
	auto *p_pass = f.world.playerForTest(sid_passenger);
	REQUIRE(p_pass != nullptr);
	p_pass->x = 40;
	p_pass->y = 40;
	f.world.onEvent(sid_passenger, req);

	CHECK(f.world.playerHasActiveWindow(sid_passenger));
	win_text = f.world.playerLastWindowText(sid_passenger);
	CHECK(win_text.find("等级还不够") != std::string::npos);

	// 3. 石币不足用例：driver 将石币花光 (< 100)
	auto *p_drv = f.world.playerForTest(sid_driver);
	REQUIRE(p_drv != nullptr);
	p_drv->gold = 50;
	f.world.onEvent(sid_driver, req);

	CHECK(f.world.playerHasActiveWindow(sid_driver));
	win_text = f.world.playerLastWindowText(sid_driver);
	CHECK(win_text.find("金钱不足") != std::string::npos);

	// 4. 正常乘车流程：拥有 500 石币，等级 15，单人
	p_drv->gold = 500;
	f.world.onEvent(sid_driver, req);

	CHECK(f.world.playerHasActiveWindow(sid_driver));
	win_text = f.world.playerLastWindowText(sid_driver);
	CHECK(win_text.find("萨姆吉尔村站") != std::string::npos);
	CHECK(win_text.find("是否购票登车") != std::string::npos);

	// 玩家点击 YES 购票上车
	SA::Domain::WindowReply rep{};
	rep.window_id = f.world.playerActiveWindowId(sid_driver);
	rep.source.entity_id = 7004;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(sid_driver, rep);

	// 验证车费已扣除 100 石币 (500 - 100 = 400)
	CHECK(p_drv->gold == 400);

	// 验证玩家已运送到目的地站点
	CHECK(p_drv->floor == 2000);
	CHECK(p_drv->x == 85);
	CHECK(p_drv->y == 80);

	// 验证到站弹窗
	CHECK(f.world.playerHasActiveWindow(sid_driver));
	win_text = f.world.playerLastWindowText(sid_driver);
	CHECK(win_text.find("长毛象公车已安全送达") != std::string::npos);
}

TEST_CASE("NPC 交通与世界设施 API 边界安全")
{
	TestFixture f;
	const auto sid = f.spawnPlayer("Eve", 1000, 10, 10);

	// 1. 无效 session 记录点设置
	CHECK_FALSE(f.world.setPlayerSavePoint(999999, 1000, 10, 10));

	// 2. 越界线路乘车
	CHECK_FALSE(f.world.boardBus(sid, 999999, 0));

	// 3. 手工设置记录点
	REQUIRE(f.world.setPlayerSavePoint(sid, 3000, 85, 75, "加加村"));
	const auto sp = f.world.getPlayerSavePoint(sid);
	REQUIRE(sp.has_value());
	CHECK(sp->floor == 3000);
	CHECK(sp->village_name == "加加村");
}

TEST_CASE("NPC:Charm 美容师魅力提升、经典计费公式与宠物忠诚度联动")
{
	TestFixture f;
	const auto sid = f.spawnPlayer("Cynthia", 1000, 30, 30, /*gold=*/50000, /*level=*/10);
	auto *p = f.world.playerForTest(sid);
	REQUIRE(p != nullptr);
	p->charm = 10;
	f.world.setPlayerTransmigration(sid, 0);

	// 1. 验证经典费用公式:
	// level=10, charm=10 => waru_charm = 10, cost = 10 * 10 * (10 / 3) * 1 = 100 * 3 * 1 = 300
	CHECK(f.world.calcCharmCost(sid) == 300);

	// 若 charm=1 => waru_charm = 3, cost = 10 * 10 * (3 / 3) * 1 = 100
	p->charm = 1;
	CHECK(f.world.calcCharmCost(sid) == 100);

	p->charm = 10;

	// 给玩家装备一只宠物，并设置初始忠诚度为 80
	const int pet_slot = f.world.givePetToPlayer(sid, makeTestPet(501, 10));
	REQUIRE(pet_slot >= 0);
	auto *pet = f.world.playerPetForTest(sid, pet_slot);
	REQUIRE(pet != nullptr);
	f.world.setPetLoyalty(pet->uid, 80);
	CHECK(f.world.petLoyalty(pet->uid) == 80);

	// 部署美容师 NPC 于 (1000, 30, 31)
	NpcEntity charm_npc{};
	charm_npc.id = 7005;
	charm_npc.type = NpcType::kCharm;
	charm_npc.floor = 1000;
	charm_npc.x = 30;
	charm_npc.y = 31;
	charm_npc.name = "美容师菲菲";
	f.world.loadNpcEntities({charm_npc});

	// 对话触发面前美容师
	SA::Domain::EventRequest req{};
	req.seqno = 201;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.dir = 4; // y+1
	req.x = 30;
	req.y = 31;
	f.world.onEvent(sid, req);

	CHECK(f.world.playerHasActiveWindow(sid));
	std::string win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("要将你的魅力上升 5 点") != std::string::npos);
	CHECK(win_text.find("300 石币") != std::string::npos);

	// 2. 金币不足分支
	p->gold = 50; // 少于 300
	SA::Domain::WindowReply rep{};
	rep.window_id = f.world.playerActiveWindowId(sid);
	rep.source.entity_id = 7005;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(sid, rep);

	CHECK(f.world.playerHasActiveWindow(sid));
	win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("你的钱不够") != std::string::npos);
	CHECK(p->charm == 10); // 魅力未变
	CHECK(p->gold == 50);

	// 3. 正常美容提升魅力分支
	p->gold = 5000;
	f.world.onEvent(sid, req); // 重新发起对话
	rep.window_id = f.world.playerActiveWindowId(sid);
	f.world.onWindowReply(sid, rep);

	CHECK(p->gold == 5000 - 300);              // 扣费 300 石币 (走 GoldLedger)
	CHECK(p->charm == 15);                     // 魅力提升 +5
	CHECK(f.world.petLoyalty(pet->uid) == 82); // 宠物忠诚度滋养提升 +2

	// 4. 魅力已满 100 分支
	p->charm = 100;
	CHECK(f.world.calcCharmCost(sid) == -1);
	f.world.onEvent(sid, req);
	CHECK(f.world.playerHasActiveWindow(sid));
	win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("你的魅力真是完美") != std::string::npos);
}

TEST_CASE("NPC:Oldman 称号长者资格认定与称号发放")
{
	TestFixture f;
	const auto sid = f.spawnPlayer("Douglas", 1000, 50, 50, /*gold=*/1000, /*level=*/35);
	auto *p = f.world.playerForTest(sid);
	REQUIRE(p != nullptr);

	// 部署称号长老 NPC 于 (1000, 50, 51)
	NpcEntity elder_npc{};
	elder_npc.id = 7006;
	elder_npc.type = NpcType::kOldman;
	elder_npc.floor = 1000;
	elder_npc.x = 50;
	elder_npc.y = 51;
	elder_npc.name = "称号长者萨姆";
	f.world.loadNpcEntities({elder_npc});

	// 1. 初次对话：玩家 35 级，满足称号 1(初出茅庐 lv 10) 与称号 2(原始猎人 lv 30)
	SA::Domain::EventRequest req{};
	req.seqno = 202;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.dir = 4;
	req.x = 50;
	req.y = 51;
	f.world.onEvent(sid, req);

	CHECK(f.world.playerHasActiveWindow(sid));
	std::string win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("特授予你 2 个全新称号") != std::string::npos);
	CHECK(f.world.hasTitle(sid, 1));
	CHECK(f.world.hasTitle(sid, 2));
	CHECK_FALSE(f.world.hasTitle(sid, 3));

	// 2. 重复对话且无新晋条件：触发原版经典台词
	f.world.onEvent(sid, req);
	CHECK(f.world.playerHasActiveWindow(sid));
	win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("在给你称号时不是还没来吗") != std::string::npos);

	// 3. 提升到 120 级：再次触发，获得称号 3(尼斯勇者) 与称号 4(部落英雄)
	p->level = 120;
	f.world.onEvent(sid, req);
	CHECK(f.world.playerHasActiveWindow(sid));
	win_text = f.world.playerLastWindowText(sid);
	CHECK(win_text.find("特授予你 2 个全新称号") != std::string::npos);
	CHECK(f.world.hasTitle(sid, 3));
	CHECK(f.world.hasTitle(sid, 4));
}

TEST_CASE("NPC:FmHealer 家族医者全员全宠治疗与组队广播")
{
	TestFixture f;
	const auto sid_leader = f.spawnPlayer("FmLeader", 1000, 60, 60, /*gold=*/1000, /*level=*/20);
	const auto sid_member = f.spawnPlayer("FmMember", 1000, 60, 59, /*gold=*/1000, /*level=*/20);

	auto *lead = f.world.playerForTest(sid_leader);
	auto *mem = f.world.playerForTest(sid_member);
	REQUIRE(lead != nullptr);
	REQUIRE(mem != nullptr);

	// 队长和队员各携带一只战宠，均打残或致死
	const int s_lead_pet = f.world.givePetToPlayer(sid_leader, makeTestPet(601, 15));
	const int s_mem_pet = f.world.givePetToPlayer(sid_member, makeTestPet(602, 15));
	auto *lead_pet = f.world.playerPetForTest(sid_leader, s_lead_pet);
	auto *mem_pet = f.world.playerPetForTest(sid_member, s_mem_pet);
	REQUIRE(lead_pet != nullptr);
	REQUIRE(mem_pet != nullptr);

	lead->hp = 5;
	lead->mp = 0;
	lead_pet->hp = 0;

	mem->hp = 8;
	mem->mp = 5;
	mem_pet->hp = 0;

	// 部署家族医者 NPC 于 (1000, 60, 61)
	NpcEntity fm_npc{};
	fm_npc.id = 7007;
	fm_npc.type = NpcType::kFmHealer;
	fm_npc.floor = 1000;
	fm_npc.x = 60;
	fm_npc.y = 61;
	fm_npc.name = "家族医者吉娜";
	f.world.loadNpcEntities({fm_npc});

	// 1. 单人治疗测试 (尚未组队)
	SA::Domain::EventRequest req{};
	req.seqno = 203;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.dir = 4;
	req.x = 60;
	req.y = 61;
	f.world.onEvent(sid_leader, req);

	CHECK(f.world.playerHasActiveWindow(sid_leader));
	std::string win_text = f.world.playerLastWindowText(sid_leader);
	CHECK(win_text.find("由于你是本家族人员，请让我帮你回复吧") != std::string::npos);

	// 队长满血满蓝，队长战宠复活满血
	CHECK(lead->hp > 5);
	CHECK(lead->mp == lead->max_mp);
	CHECK(lead_pet->hp > 0);

	// 队员此时仍为残血状态
	CHECK(mem->hp == 8);
	CHECK(mem_pet->hp == 0);

	// 2. 组队全队回复测试
	REQUIRE(f.world.joinParty(sid_member, sid_leader));
	CHECK(f.world.playerPartyMode(sid_leader) == PartyMode::kLeader);
	CHECK(f.world.playerPartyMode(sid_member) == PartyMode::kMember);

	// 再次使队长和队员受伤
	lead->hp = 12;
	lead_pet->hp = 1;
	mem->hp = 15;
	mem_pet->hp = 0;

	f.world.onEvent(sid_leader, req);

	// 验证全队全员与所有战宠全部复活满血满蓝
	CHECK(lead->hp > 12);
	CHECK(lead->mp == lead->max_mp);
	CHECK(lead_pet->hp > 1);
	CHECK(mem->hp > 15);
	CHECK(mem->mp == mem->max_mp);
	CHECK(mem_pet->hp > 0);
}

TEST_CASE("NPC:TranserMan 特使传送师组队阻断、等级石币门禁与瞬移")
{
	TestFixture f;
	const auto sid_leader = f.spawnPlayer("WarpLeader", 1000, 70, 70, /*gold=*/5000, /*level=*/25);
	const auto sid_member = f.spawnPlayer("WarpMember", 1000, 70, 69, /*gold=*/5000, /*level=*/25);
	auto *lead = f.world.playerForTest(sid_leader);
	REQUIRE(lead != nullptr);

	// 部署特使传送师 NPC 于 (1000, 70, 71)，配置前往卡鲁它那村(3000, 100, 100)
	NpcEntity transer_npc{};
	transer_npc.id = 7008;
	transer_npc.type = NpcType::kTranserMan;
	transer_npc.floor = 1000;
	transer_npc.x = 70;
	transer_npc.y = 71;
	transer_npc.name = "特使传送师拉鲁";
	TranserRoute route{};
	route.dest_floor = 3000;
	route.dest_x = 100;
	route.dest_y = 100;
	route.dest_name = "卡鲁它那村庄";
	route.ticket_cost = 200;
	route.min_level = 20;
	transer_npc.transer_routes = {route};
	f.world.loadNpcEntities({transer_npc});

	// 1. 组队状态拦截测试 (原版 npc_transerman.c:78)
	REQUIRE(f.world.joinParty(sid_member, sid_leader));

	SA::Domain::EventRequest req{};
	req.seqno = 204;
	req.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	req.dir = 4;
	req.x = 70;
	req.y = 71;
	f.world.onEvent(sid_leader, req);

	CHECK(f.world.playerHasActiveWindow(sid_leader));
	std::string win_text = f.world.playerLastWindowText(sid_leader);
	CHECK(win_text.find("不能帮组队中的人传送") != std::string::npos);

	// 解散队伍
	REQUIRE(f.world.leaveParty(sid_leader));

	// 2. 等级不足拦截测试 (要求 20 级，设为 10 级)
	lead->level = 10;
	f.world.onEvent(sid_leader, req);
	SA::Domain::WindowReply rep{};
	rep.window_id = f.world.playerActiveWindowId(sid_leader);
	rep.source.entity_id = 7008;
	rep.button = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES);
	f.world.onWindowReply(sid_leader, rep);

	CHECK(f.world.playerHasActiveWindow(sid_leader));
	win_text = f.world.playerLastWindowText(sid_leader);
	CHECK(win_text.find("你的等级还不够") != std::string::npos);
	CHECK(lead->floor == 1000); // 未传送

	// 3. 石币不足拦截测试
	lead->level = 25;
	lead->gold = 50; // 少于 200
	f.world.onEvent(sid_leader, req);
	rep.window_id = f.world.playerActiveWindowId(sid_leader);
	f.world.onWindowReply(sid_leader, rep);

	CHECK(f.world.playerHasActiveWindow(sid_leader));
	win_text = f.world.playerLastWindowText(sid_leader);
	CHECK(win_text.find("金钱不足") != std::string::npos);
	CHECK(lead->floor == 1000);

	// 4. 正常快捷传送
	lead->gold = 1000;
	f.world.onEvent(sid_leader, req);
	rep.window_id = f.world.playerActiveWindowId(sid_leader);
	f.world.onWindowReply(sid_leader, rep);

	CHECK(lead->gold == 1000 - 200); // 扣款 200 (走 GoldLedger kWarpFee)
	CHECK(lead->floor == 3000);      // 瞬移至卡鲁它那村
	CHECK(lead->x == 100);
	CHECK(lead->y == 100);

	CHECK(f.world.playerHasActiveWindow(sid_leader));
	win_text = f.world.playerLastWindowText(sid_leader);
	CHECK(win_text.find("已平安抵达【卡鲁它那村庄】") != std::string::npos);
}
