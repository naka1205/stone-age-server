// tests/WorldGmTest.cpp —— GM 管理员指令通道与个人银行存取系统测试 (阶段 15)
//
// 依据: 00 §3.1 / §4.3 / §7 (D8 核心子系统 S06 GM 命令 7,357 行 & NPC:Bankman 599 行)
// 覆盖:
//   1. S06 GM 文本指令识别判定 (isGmCommand: /cmd 与 [cmd ...] 格式，普通聊天不误判)
//   2. S06 GM 核心指令执行调度 (/warp, /heal, /gold, /level, /setfame, /additem, /delitem, /title, /shutdown, /help)
//   3. S06 sendChat 拦截通道与防泄露 (指令不向同屏广播，系统反馈独立回传)
//   4. NPC:Bankman 个人钱庄/银行个人金库存取系统 (bankDeposit / bankWithdraw / 1亿上限与随身溢出防御)
//   5. NPC:Bankman 钱庄对话交互

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
	                               std::int32_t gold = 500000)
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
		p->level = 10;
		return sid;
	}
};

} // namespace

TEST_CASE("阶段 15.1: S06 GM 指令判定 (isGmCommand)")
{
	TestFixture tf;

	// 1. 标准斜杠指令
	CHECK(tf.world.isGmCommand("/warp 1000 20 30"));
	CHECK(tf.world.isGmCommand("/heal"));
	CHECK(tf.world.isGmCommand("/gold 10000"));
	CHECK(tf.world.isGmCommand("/level 50"));
	CHECK(tf.world.isGmCommand("/setfame 500"));
	CHECK(tf.world.isGmCommand("/additem 1001"));
	CHECK(tf.world.isGmCommand("/delitem 9"));
	CHECK(tf.world.isGmCommand("/title 1"));
	CHECK(tf.world.isGmCommand("/announce 维护通知"));
	CHECK(tf.world.isGmCommand("/shutdown"));
	CHECK(tf.world.isGmCommand("/help"));

	// 2. 经典石器时代中括号 chatmagic 格式
	CHECK(tf.world.isGmCommand("[warp 1000 20 30]"));
	CHECK(tf.world.isGmCommand("[heal]"));
	CHECK(tf.world.isGmCommand("[gold 50000]"));
	CHECK(tf.world.isGmCommand("[level 30]"));

	// 3. 普通文本不误判为 GM 指令
	CHECK_FALSE(tf.world.isGmCommand(""));
	CHECK_FALSE(tf.world.isGmCommand("你好石器时代!"));
	CHECK_FALSE(tf.world.isGmCommand("加鲁卡怎么走?"));
	CHECK_FALSE(tf.world.isGmCommand("[招募] 渔村抓红暴组队 4=1"));
	CHECK_FALSE(tf.world.isGmCommand("[哈哈] 今天真开心"));
}

TEST_CASE("阶段 15.2: S06 GM 核心指令执行调度 (executeGmCommand)")
{
	TestFixture tf;
	const auto alice_sid = tf.spawnPlayer("Alice", 1000, 20, 20, 500000);
	std::string reply;

	// 1. /help 指令
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/help", reply));
	CHECK(reply.find("石器时代 GM 指令集") != std::string::npos);

	// 2. /warp 瞬移指令
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/warp 2000 35 45", reply));
	CHECK(reply.find("已成功传送至地图 2000") != std::string::npos);
	auto *p = tf.world.playerForTest(alice_sid);
	CHECK(p->floor == 2000);
	CHECK(p->x == 35);
	CHECK(p->y == 45);

	// 3. /gold 增减随身石币
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/gold 50000", reply));
	CHECK(reply.find("石币调整成功") != std::string::npos);
	CHECK(tf.world.playerGold(alice_sid) == 550000);

	REQUIRE(tf.world.executeGmCommand(alice_sid, "/gold -100000", reply));
	CHECK(tf.world.playerGold(alice_sid) == 450000);

	// 4. /level 调整等级与属性
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/level 50", reply));
	CHECK(reply.find("等级已调整为 50") != std::string::npos);
	CHECK(tf.world.playerLevel(alice_sid) == 50);
	CHECK(p->skillup_points == (50 - 1) * 3);

	// 5. /heal 满血满蓝
	p->hp = 1;
	p->mp = 0;
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/heal", reply));
	CHECK(p->hp > 100);
	CHECK(p->mp == p->max_mp);

	// 6. /setfame 调整声望
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/setfame 888", reply));
	CHECK(tf.world.playerFame(alice_sid) == 888);

	// 7. /additem 道具添加
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/additem 2001", reply));
	CHECK(reply.find("已添加至背包槽位") != std::string::npos);
	const int added_slot = p->items[SA::Model::kStartItemArray].valid() ? static_cast<int>(SA::Model::kStartItemArray) : -1;
	REQUIRE(added_slot >= 0);

	// 8. /delitem 道具移除
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/delitem " + std::to_string(added_slot), reply));
	CHECK(reply.find("已删除背包槽位") != std::string::npos);
	CHECK_FALSE(p->items[static_cast<std::size_t>(added_slot)].valid());

	// 9. /title 称号授予
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/title 1", reply));
	CHECK(reply.find("已成功授予称号") != std::string::npos);

	// 10. /shutdown 停机维护
	CHECK_FALSE(tf.world.isShutdownRequested());
	REQUIRE(tf.world.executeGmCommand(alice_sid, "/shutdown", reply));
	CHECK(tf.world.isShutdownRequested());

	// 11. 经典中括号格式 [heal]
	p->hp = 5;
	REQUIRE(tf.world.executeGmCommand(alice_sid, "[heal]", reply));
	CHECK(p->hp > 100);

	// 12. 非法指令防御
	CHECK_FALSE(tf.world.executeGmCommand(alice_sid, "/invalid_command", reply));
	CHECK(reply.find("未知 GM 指令") != std::string::npos);
}

TEST_CASE("阶段 15.3: S06 sendChat 拦截通道与防泄露 (Chat Interception)")
{
	TestFixture tf;
	const auto alice_sid = tf.spawnPlayer("Alice", 1000, 20, 20);
	const auto bob_sid = tf.spawnPlayer("Bob", 1000, 21, 20); // Bob 在切比雪夫距离 <= 9 内

	// 1. Alice 触发 GM 瞬移指令
	REQUIRE(tf.world.sendChat(alice_sid, ChatChannel::kTalkNormal, "/warp 3000 50 60"));

	// Alice 收到 GM 系统的专属回复
	auto a_msgs = tf.world.pollChatMessages(alice_sid);
	REQUIRE(a_msgs.size() == 1);
	CHECK(a_msgs[0].channel == ChatChannel::kTalkSystem);
	CHECK(a_msgs[0].sender_name == "GM系统");
	CHECK(a_msgs[0].text.find("已成功传送至地图 3000") != std::string::npos);

	// 同屏的 Bob 绝不能收到 Alice 的 GM 指令，零广播外溢
	auto b_msgs = tf.world.pollChatMessages(bob_sid);
	CHECK(b_msgs.empty());

	// 2. 普通玩家聊天正常广播
	REQUIRE(tf.world.sendChat(alice_sid, ChatChannel::kTalkNormal, "大家好看!"));
	auto b_msgs2 = tf.world.pollChatMessages(bob_sid);
	// Alice 已经传送到地图 3000，Bob 在地图 1000，因此普通聊天跨图隔离
	CHECK(b_msgs2.empty());
}

TEST_CASE("阶段 15.4: NPC:Bankman 个人钱庄/银行存取款与金库防护 (Bank Depository)")
{
	TestFixture tf;
	const auto alice_sid = tf.spawnPlayer("Alice", 1000, 20, 20, 500000);

	// 初始状态
	CHECK(tf.world.playerGold(alice_sid) == 500000);
	CHECK(tf.world.playerBankGold(alice_sid) == 0);

	// 1. 存入 200,000 石币至银行
	REQUIRE(tf.world.bankDeposit(alice_sid, 200000));
	CHECK(tf.world.playerGold(alice_sid) == 300000);
	CHECK(tf.world.playerBankGold(alice_sid) == 200000);

	// 2. 从银行取出 100,000 石币至随身钱包
	REQUIRE(tf.world.bankWithdraw(alice_sid, 100000));
	CHECK(tf.world.playerGold(alice_sid) == 400000);
	CHECK(tf.world.playerBankGold(alice_sid) == 100000);

	// 3. 存款超额防御 (随身只有 400,000，尝试存 500,000 应被拒绝)
	CHECK_FALSE(tf.world.bankDeposit(alice_sid, 500000));
	CHECK(tf.world.playerGold(alice_sid) == 400000);
	CHECK(tf.world.playerBankGold(alice_sid) == 100000);

	// 4. 取款超额防御 (银行只有 100,000，尝试取 150,000 应被拒绝)
	CHECK_FALSE(tf.world.bankWithdraw(alice_sid, 150000));
	CHECK(tf.world.playerGold(alice_sid) == 400000);
	CHECK(tf.world.playerBankGold(alice_sid) == 100000);

	// 5. 随身钱包上限 1,000,000 防御
	auto *p = tf.world.playerForTest(alice_sid);
	p->gold = 950000;
	// 尝试取 100,000 会导致随身石币达到 1,050,000 (> 1,000,000)，应被拒绝以防爆仓
	CHECK_FALSE(tf.world.bankWithdraw(alice_sid, 100000));
	CHECK(p->gold == 950000);
	CHECK(tf.world.playerBankGold(alice_sid) == 100000);

	// 6. 非法金额输入防御 (<= 0)
	CHECK_FALSE(tf.world.bankDeposit(alice_sid, 0));
	CHECK_FALSE(tf.world.bankDeposit(alice_sid, -500));
	CHECK_FALSE(tf.world.bankWithdraw(alice_sid, 0));
	CHECK_FALSE(tf.world.bankWithdraw(alice_sid, -500));
}

TEST_CASE("阶段 15.5: NPC:Bankman 钱庄 NPC 对白交互 (NPC Dialog)")
{
	TestFixture tf;
	const auto alice_sid = tf.spawnPlayer("Alice", 1000, 20, 20, 500000);

	NpcEntity bankman{};
	bankman.id = 9001;
	bankman.floor = 1000;
	bankman.x = 21;
	bankman.y = 20;
	bankman.dir = 6;
	bankman.type = NpcType::kBankman;
	bankman.name = "钱庄掌柜";
	tf.world.loadNpcEntities({bankman});
	CHECK(tf.world.npcCount() == 1);

	// Alice 在 (20, 20), 面向东 (dir=2) 与 NPC (21, 20) 对话
	SA::Domain::EventRequest ev{};
	ev.dir = 2;
	ev.event_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ev.seqno = 901;
	tf.world.onEvent(alice_sid, ev);
	tf.world.tick();
}
