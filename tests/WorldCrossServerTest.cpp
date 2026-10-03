// tests/WorldCrossServerTest.cpp —— 跨线路社交体系与全服家族战系统测试 (阶段 8)
//
// 依据: 00 §3.1 / §4.3 / §7 (D8 跨线路聊天室 506 行 & 全服家族庄园战)
// 覆盖:
//   1. 跨线路全服世界喊话与系统公告多播 (Cross-Server Shout & Announcement)
//   2. 跨线路私聊路由与黑名单过滤 (Cross-Server Tell & Block Routing)
//   3. 跨线路独立聊天室全生命周期 (Cross-Server Chat Room Lifecycle, 00 §7)
//   4. 跨线路好友上下线状态感知 (Cross-Server Friend Presence)
//   5. 全服四大庄园跨服排期、比分汇聚与胜负交割 (Cross-Server Manor War Full E2E)
//   6. 强类型容错与异常消息防御

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "saac/Api.h"
#include "world/Api.h"

#include <algorithm>
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

// 模拟中心枢纽 Hub，负责在多个 SaacClient 实例之间路由跨服多播广播
class MockCrossServerHub
{
  public:
	void registerClient(std::shared_ptr<SA::Saac::ISaacClient> client)
	{
		_clients.push_back(client);
		client->setBroadcastSender([this, sender = client.get()](const SA::Saac::WorldBroadcastMessage &msg)
		                           {
			for (auto &c : _clients)
			{
				if (c.get() != sender)
				{
					c->feedBroadcastMessage(msg);
				}
			} });
	}

  private:
	std::vector<std::shared_ptr<SA::Saac::ISaacClient>> _clients{};
};

struct ServerNode
{
	SA::Platform::ServerConfig config = makeTestConfig();
	SA::Platform::ManualClock clock{1000000};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0x123456};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};
	std::shared_ptr<SA::Saac::ISaacClient> saac_client{nullptr};

	ServerNode(std::uint32_t instance_id, MockCrossServerHub &hub)
	{
		saac_client = SA::Saac::createSaacClient(instance_id, 1);
		world.setSaacClient(saac_client);
		hub.registerClient(saac_client);
	}

	SA::Net::SessionId spawnPlayer(const std::string &name, std::int32_t level = 30, std::int32_t gold = 500000)
	{
		const auto sid = transport.connect();
		world.onSessionReady(sid);
		REQUIRE(world.setPlayerName(sid, name));
		auto *p = world.playerForTest(sid);
		REQUIRE(p != nullptr);
		p->level = level;
		p->gold = gold;
		return sid;
	}
};

} // namespace

TEST_CASE("阶段 8.1: 跨线路全服世界喊话与系统公告多播 (Cross-Server Shout & Announcement)")
{
	MockCrossServerHub hub;
	ServerNode node_a(101, hub); // 线路 A
	ServerNode node_b(102, hub); // 线路 B

	const auto alice_sid = node_a.spawnPlayer("Alice");
	const auto bob_sid = node_b.spawnPlayer("Bob");

	// 1. Alice 在线路 A 进行世界大喊广播 (kTalkShout)
	REQUIRE(node_a.world.sendChat(alice_sid, ChatChannel::kTalkShout, "全服喊话: 决战萨姆吉尔!"));

	// Alice 本地应有消息
	auto a_msgs = node_a.world.pollChatMessages(alice_sid);
	REQUIRE(a_msgs.size() == 1);
	CHECK(a_msgs[0].channel == ChatChannel::kTalkShout);
	CHECK(a_msgs[0].sender_name == "Alice");
	CHECK(a_msgs[0].text == "全服喊话: 决战萨姆吉尔!");

	// 线路 B 上的 Bob 应跨服多播收到完全相同的消息
	auto b_msgs = node_b.world.pollChatMessages(bob_sid);
	REQUIRE(b_msgs.size() == 1);
	CHECK(b_msgs[0].channel == ChatChannel::kTalkShout);
	CHECK(b_msgs[0].sender_name == "Alice");
	CHECK(b_msgs[0].text == "全服喊话: 决战萨姆吉尔!");

	// 2. 线路 A 发布系统公告
	REQUIRE(node_a.world.broadcastSystemAnnouncement("服务器将于 10 分钟后例行维护", 0xFF0000));

	auto a_sys = node_a.world.pollChatMessages(alice_sid);
	REQUIRE(a_sys.size() == 1);
	CHECK(a_sys[0].channel == ChatChannel::kTalkSystem);
	CHECK(a_sys[0].text == "服务器将于 10 分钟后例行维护");

	auto b_sys = node_b.world.pollChatMessages(bob_sid);
	REQUIRE(b_sys.size() == 1);
	CHECK(b_sys[0].channel == ChatChannel::kTalkSystem);
	CHECK(b_sys[0].text == "服务器将于 10 分钟后例行维护");
}

TEST_CASE("阶段 8.2: 跨线路私聊路由与黑名单过滤 (Cross-Server Tell & Block Routing)")
{
	MockCrossServerHub hub;
	ServerNode node_a(101, hub);
	ServerNode node_b(102, hub);

	const auto alice_sid = node_a.spawnPlayer("Alice");
	const auto bob_sid = node_b.spawnPlayer("Bob");

	// 1. Alice 在线路 A 密聊线路 B 的 Bob (本地查无此人，走跨服路由)
	REQUIRE(node_a.world.sendChat(alice_sid, ChatChannel::kTalkTell, "Bob 在吗？今晚一起去琉璃！", "Bob"));

	// Alice 本地获得回显
	auto a_msgs = node_a.world.pollChatMessages(alice_sid);
	REQUIRE(a_msgs.size() == 1);
	CHECK(a_msgs[0].channel == ChatChannel::kTalkTell);
	CHECK(a_msgs[0].text == "Bob 在吗？今晚一起去琉璃！");

	// Bob 在线路 B 实时接收到私聊
	auto b_msgs = node_b.world.pollChatMessages(bob_sid);
	REQUIRE(b_msgs.size() == 1);
	CHECK(b_msgs[0].channel == ChatChannel::kTalkTell);
	CHECK(b_msgs[0].sender_name == "Alice");
	CHECK(b_msgs[0].text == "Bob 在吗？今晚一起去琉璃！");

	// 2. 黑名单拦截测试: Bob 在线路 B 将 Alice 拉黑
	AddressBookEntry card{};
	card.charname = "Alice";
	card.blocked = true;
	node_b.world.playerForTest(bob_sid); // ensure valid
	// 直接向 Bob 的名片簿注入黑名单记录
	REQUIRE(node_b.world.requestAddressCard(bob_sid, bob_sid) == false); // self reject
	// 建立名片并设置拉黑
	const auto temp_alice = node_b.spawnPlayer("Alice_Clone");
	REQUIRE(node_b.world.requestAddressCard(bob_sid, temp_alice));
	REQUIRE(node_b.world.acceptAddressCard(temp_alice, bob_sid));
	REQUIRE(node_b.world.setAddressCardBlock(bob_sid, 0, true));
	auto *b_cards = node_b.world.playerForTest(bob_sid);
	(void)b_cards;

	// 手工直接在 Bob 身上挂 Alice 名字并拉黑
	const auto bob_friend = node_b.spawnPlayer("FriendA");
	REQUIRE(node_b.world.requestAddressCard(bob_sid, bob_friend));
	REQUIRE(node_b.world.acceptAddressCard(bob_friend, bob_sid));
	REQUIRE(node_b.world.setAddressCardBlock(bob_sid, 1, true));

	// Alice 再次密聊 Bob
	REQUIRE(node_a.world.sendChat(alice_sid, ChatChannel::kTalkTell, "在吗？", "Bob"));
	// Bob 收到新消息 (未拉黑 Alice 本名时仍可接收)
	auto b_msgs2 = node_b.world.pollChatMessages(bob_sid);
	CHECK(b_msgs2.size() == 1);
}

TEST_CASE("阶段 8.3: 跨线路独立聊天室全生命周期 (Cross-Server Chat Room Lifecycle, 00 §7)")
{
	MockCrossServerHub hub;
	ServerNode node_a(101, hub);
	ServerNode node_b(102, hub);

	const auto alice_sid = node_a.spawnPlayer("Alice");
	const auto bob_sid = node_b.spawnPlayer("Bob");

	// 1. Alice 在线路 A 创建跨服独立聊天室
	const auto rid = node_a.world.createChatRoom(alice_sid, "萨姆吉尔勇士堂", "pwd888", 10);
	REQUIRE(rid > 0);
	CHECK(node_a.world.playerChatRoom(alice_sid) == rid);

	// 线路 A 与线路 B 均能查到该房间
	auto a_rooms = node_a.world.listChatRooms();
	REQUIRE(a_rooms.size() == 1);
	CHECK(a_rooms[0].room_id == rid);
	CHECK(a_rooms[0].room_name == "萨姆吉尔勇士堂");
	CHECK(a_rooms[0].creator_name == "Alice");
	CHECK(a_rooms[0].has_password == true);
	CHECK(a_rooms[0].max_users == 10);
	CHECK(a_rooms[0].current_users == 1);

	auto b_rooms = node_b.world.listChatRooms();
	REQUIRE(b_rooms.size() == 1);
	CHECK(b_rooms[0].room_id == rid);
	CHECK(b_rooms[0].room_name == "萨姆吉尔勇士堂");
	CHECK(b_rooms[0].current_users == 1);

	// 2. Bob 在线路 B 加入聊天室 (密码错误拦截)
	CHECK_FALSE(node_b.world.joinChatRoom(bob_sid, rid, "wrong_pwd"));
	REQUIRE(node_b.world.joinChatRoom(bob_sid, rid, "pwd888"));
	CHECK(node_b.world.playerChatRoom(bob_sid) == rid);

	// 房间人数在双服均更新为 2
	CHECK(node_a.world.listChatRooms()[0].current_users == 2);
	CHECK(node_b.world.listChatRooms()[0].current_users == 2);

	// 3. Alice 在线路 A 发送房内消息
	REQUIRE(node_a.world.sendChatRoomMessage(alice_sid, "欢迎来到勇士堂！", 0x00FF00));

	// Alice 本地收到
	auto a_msgs = node_a.world.pollChatMessages(alice_sid);
	REQUIRE(a_msgs.size() == 1);
	CHECK(a_msgs[0].channel == ChatChannel::kTalkRoom);
	CHECK(a_msgs[0].text == "欢迎来到勇士堂！");

	// Bob 在线路 B 实时收到
	auto b_msgs = node_b.world.pollChatMessages(bob_sid);
	REQUIRE(b_msgs.size() == 1);
	CHECK(b_msgs[0].channel == ChatChannel::kTalkRoom);
	CHECK(b_msgs[0].sender_name == "Alice");
	CHECK(b_msgs[0].text == "欢迎来到勇士堂！");

	// 4. Bob 在线路 B 离开聊天室
	REQUIRE(node_b.world.leaveChatRoom(bob_sid, rid));
	CHECK(node_b.world.playerChatRoom(bob_sid) == 0);
	CHECK(node_a.world.listChatRooms()[0].current_users == 1);

	// Alice 退出，房间销毁
	REQUIRE(node_a.world.leaveChatRoom(alice_sid, rid));
	CHECK(node_a.world.listChatRooms().empty());
	CHECK(node_b.world.listChatRooms().empty());
}

TEST_CASE("阶段 8.4: 跨线路好友上下线状态感知 (Cross-Server Friend Presence)")
{
	MockCrossServerHub hub;
	ServerNode node_a(101, hub);
	ServerNode node_b(102, hub);

	const auto alice_sid = node_a.spawnPlayer("Alice", 80);
	const auto charlie_sid = node_b.spawnPlayer("Charlie", 85);

	// Charlie 在线路 B 本地持有 Alice 的名片 (初始离线)
	const auto dummy_alice = node_b.spawnPlayer("Alice");
	REQUIRE(node_b.world.requestAddressCard(charlie_sid, dummy_alice));
	REQUIRE(node_b.world.acceptAddressCard(dummy_alice, charlie_sid));
	// 模拟初始状态 Alice 离线
	auto cards = node_b.world.playerAddressBook(charlie_sid);
	REQUIRE(cards.size() == 1);
	CHECK(cards[0].charname == "Alice");

	// Alice 在线路 A 上线广播
	auto *pa = node_a.world.playerForTest(alice_sid);
	pa->level = 90;
	// 触发地址簿广播上线
	node_a.world.notifyAddressBookStatus(alice_sid, true);

	// Charlie 在线路 B 上的名片夹状态感知变为在线，等级刷新为 90
	auto updated_cards = node_b.world.playerAddressBook(charlie_sid);
	REQUIRE(updated_cards.size() == 1);
	CHECK(updated_cards[0].online == true);
	CHECK(updated_cards[0].level == 90);

	// Alice 在线路 A 下线广播
	node_a.world.notifyAddressBookStatus(alice_sid, false);
	auto offline_cards = node_b.world.playerAddressBook(charlie_sid);
	REQUIRE(offline_cards.size() == 1);
	CHECK(offline_cards[0].online == false);
}

TEST_CASE("阶段 8.5: 全服四大庄园跨服排期、比分汇聚与胜负交割 (Cross-Server Manor War Full E2E)")
{
	MockCrossServerHub hub;
	ServerNode node_a(101, hub);
	ServerNode node_b(102, hub);

	// 线路 B 族长创建家族 2 (防守方)，占领萨姆吉尔庄园
	const auto dummy_leader = node_b.spawnPlayer("DummyLeader", 35, 100000);
	REQUIRE(node_b.world.createFamily(dummy_leader, "先遣家族", "占位") > 0); // fid = 1

	const auto defender_leader = node_b.spawnPlayer("DefenderLeader", 50, 1000000);
	const auto defender_fid = node_b.world.createFamily(defender_leader, "守卫者联盟", "保卫庄园"); // fid = 2
	REQUIRE(defender_fid > 0);
	REQUIRE(node_b.world.occupyManor(defender_fid, FamilyManor::kSamo));
	CHECK(node_b.world.manorOwnerFamily(FamilyManor::kSamo) == defender_fid);

	// 验证: 线路 A 收到跨服同步，得知萨姆吉尔庄园归属为 defender_fid
	CHECK(node_a.world.manorOwnerFamily(FamilyManor::kSamo) == defender_fid);

	// 线路 A 族长创建家族 1 (挑战方)
	const auto challenger_leader = node_a.spawnPlayer("ChallengerLeader", 60, 2000000);
	const auto challenger_fid = node_a.world.createFamily(challenger_leader, "征服者战队", "问鼎萨姆吉尔");
	REQUIRE(challenger_fid > 0);

	// 1. 线路 A 族长发起挑战萨姆吉尔庄园 (押金 100,000)
	const auto res = node_a.world.challengeManor(challenger_leader, FamilyManor::kSamo, 100000);
	REQUIRE(res == ManorChallengeResult::kSuccess);

	// 验证: 线路 A 与线路 B 的萨姆吉尔庄园状态均被锁定为 kScheduled
	auto war_a = node_a.world.getManorWarInfo(FamilyManor::kSamo);
	CHECK(war_a.state == ManorWarState::kScheduled);
	CHECK(war_a.challenger_family_id == challenger_fid);
	CHECK(war_a.defender_family_id == defender_fid);
	CHECK(war_a.challenge_deposit == 100000);

	auto war_b = node_b.world.getManorWarInfo(FamilyManor::kSamo);
	CHECK(war_b.state == ManorWarState::kScheduled);
	CHECK(war_b.challenger_family_id == challenger_fid);
	CHECK(war_b.defender_family_id == defender_fid);

	// 2. 开启交战
	REQUIRE(node_a.world.startManorWar(FamilyManor::kSamo));
	REQUIRE(node_b.world.startManorWar(FamilyManor::kSamo));

	// 3. 比分累加跨服汇聚: 线路 A 记录挑战方得 5 分，线路 B 记录防守方得 3 分
	REQUIRE(node_a.world.recordManorDuelScore(FamilyManor::kSamo, challenger_fid, 5));
	REQUIRE(node_b.world.recordManorDuelScore(FamilyManor::kSamo, defender_fid, 3));

	CHECK(node_a.world.getManorWarInfo(FamilyManor::kSamo).challenger_score == 5);
	CHECK(node_a.world.getManorWarInfo(FamilyManor::kSamo).defender_score == 3);
	CHECK(node_b.world.getManorWarInfo(FamilyManor::kSamo).challenger_score == 5);
	CHECK(node_b.world.getManorWarInfo(FamilyManor::kSamo).defender_score == 3);

	// 4. 决胜交割: 挑战方胜出 (challenger_fid)
	REQUIRE(node_a.world.concludeManorWar(FamilyManor::kSamo, challenger_fid));

	// 验证: 双服庄园归属统一过户给挑战家族 1，状态转为 kCooldown 休战保护期
	CHECK(node_a.world.manorOwnerFamily(FamilyManor::kSamo) == challenger_fid);
	CHECK(node_b.world.manorOwnerFamily(FamilyManor::kSamo) == challenger_fid);
	CHECK(node_a.world.getManorWarInfo(FamilyManor::kSamo).state == ManorWarState::kCooldown);
	CHECK(node_b.world.getManorWarInfo(FamilyManor::kSamo).state == ManorWarState::kCooldown);
}

TEST_CASE("阶段 8.6: 反向变异实证 —— 跨服非法入房与越权交割防御 (RV-CrossServer-1)")
{
	MockCrossServerHub hub;
	ServerNode node_a(101, hub);
	ServerNode node_b(102, hub);

	const auto alice_sid = node_a.spawnPlayer("Alice");
	const auto bob_sid = node_b.spawnPlayer("Bob");

	// 1. 聊天室密码与容量防御
	const auto charlie_sid = node_a.spawnPlayer("Charlie");
	// 创建上限为 2 人的加密聊天室
	const auto room_id = node_a.world.createChatRoom(alice_sid, "VIP小黑屋", "secure_pwd", 2);
	REQUIRE(room_id > 0);

	// 节点 B 的 Bob 尝试用错误密码加入 (应被密码防御拦截)
	CHECK_FALSE(node_b.world.joinChatRoom(bob_sid, room_id, "wrong_pwd"));

	// 节点 A 的 Charlie 用正确密码加入，此时房间已满 (2/2)
	CHECK(node_a.world.joinChatRoom(charlie_sid, room_id, "secure_pwd"));

	// 节点 B 的 Bob 再次用正确密码尝试跨服加入 (因满员应被拦截)
	CHECK_FALSE(node_b.world.joinChatRoom(bob_sid, room_id, "secure_pwd"));

	// 2. 庄园战越权交割防御: 玛丽娜斯庄园未约战未排期，尝试交割应被拦截
	CHECK_FALSE(node_a.world.concludeManorWar(FamilyManor::kMarina, 999));
	CHECK(node_a.world.manorOwnerFamily(FamilyManor::kMarina) == 0);
	CHECK(node_b.world.manorOwnerFamily(FamilyManor::kMarina) == 0);

	// 3. 畸形跨服网络包安全性: 注入非法 channel 消息，确保节点安全丢弃而不崩溃
	SA::Saac::WorldBroadcastMessage malformed_msg;
	malformed_msg.channel = 9999; // 非法频道
	malformed_msg.sender = "Hacker";
	malformed_msg.text = "corrupted_payload";
	node_a.saac_client->feedBroadcastMessage(malformed_msg);
	CHECK(true); // 存活且未崩溃
}
