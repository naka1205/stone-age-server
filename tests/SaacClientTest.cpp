// tests/SaacClientTest.cpp —— S22 saac 客户端与中心服务协议管线单元测试 (Phase 7)
//
// 依据: 00 §3.1 / §4.3 / §7 (D8 核心子系统 S22)
//       17-saac-boundary.md (W12 取证: 续体三元组/C33 世代隔离/C35 显式请求关联/C45 锁模型)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <vector>

#include "saac/Api.h"

namespace SA::Saac
{

TEST_CASE("S22 协议帧成帧封包与解包正确性 (SaacProtocol)")
{
	RequestEnvelope req_env{};
	req_env.instance_id = 101;
	req_env.generation = 5;
	req_env.request_id = 999999;
	req_env.deadline_ms = 3000;

	std::vector<std::uint8_t> payload = {'S', 'T', 'O', 'N', 'E', '_', 'A', 'G', 'E'};
	auto encoded = encodeRequest(PacketType::kLoginReq, req_env, payload);
	CHECK(encoded.size() == (4 + 2 + 4 + 4 + 8 + 8 + payload.size()));

	// 模拟对端回包
	ResponseEnvelope resp_env{};
	resp_env.instance_id = 101;
	resp_env.generation = 5;
	resp_env.request_id = 999999;
	resp_env.status = SaacAccountStatus::kOk;
	resp_env.error_code = 0;

	// 手工构造响应帧
	const std::uint32_t header_len = 4 + 2 + 4 + 4 + 8 + 4 + 4;
	const std::uint32_t total_len = header_len + static_cast<std::uint32_t>(payload.size());
	std::vector<std::uint8_t> resp_bytes(total_len);
	std::memcpy(resp_bytes.data(), &total_len, 4);
	const std::uint16_t t = static_cast<std::uint16_t>(PacketType::kLoginResp);
	std::memcpy(resp_bytes.data() + 4, &t, 2);
	std::memcpy(resp_bytes.data() + 6, &resp_env.instance_id, 4);
	std::memcpy(resp_bytes.data() + 10, &resp_env.generation, 4);
	std::memcpy(resp_bytes.data() + 14, &resp_env.request_id, 8);
	const std::uint32_t st = static_cast<std::uint32_t>(resp_env.status);
	std::memcpy(resp_bytes.data() + 22, &st, 4);
	std::memcpy(resp_bytes.data() + 26, &resp_env.error_code, 4);
	std::memcpy(resp_bytes.data() + 30, payload.data(), payload.size());

	PacketType out_type = PacketType::kNone;
	ResponseEnvelope out_env{};
	std::vector<std::uint8_t> out_payload;
	CHECK(decodeResponse(resp_bytes.data(), resp_bytes.size(), out_type, out_env, out_payload));

	CHECK(out_type == PacketType::kLoginResp);
	CHECK(out_env.instance_id == 101);
	CHECK(out_env.generation == 5);
	CHECK(out_env.request_id == 999999);
	CHECK(out_env.status == SaacAccountStatus::kOk);
	CHECK(out_payload == payload);
}

TEST_CASE("S22 SaacClient 在途请求装配、三元组与跨实例世代隔离 (C33 防御)")
{
	auto client = createSaacClient(/*instance_id=*/2, /*generation=*/10);
	REQUIRE(client != nullptr);
	CHECK(client->instanceId() == 2);
	CHECK(client->generation() == 10);
	CHECK(client->inFlightCount() == 0);
	CHECK(client->staleDropCount() == 0);

	SUBCASE("正常请求分发与匹配回调")
	{
		bool callback_invoked = false;
		SaacAccountStatus received_status = SaacAccountStatus::kNetworkError;
		std::string received_cdkey;

		std::uint64_t req_id = client->requestLogin("test_user_01", "pass123",
		                                            [&](SaacAccountStatus st, const std::string &cdkey, const auto & /*chars*/)
		                                            {
			                                            callback_invoked = true;
			                                            received_status = st;
			                                            received_cdkey = cdkey;
		                                            });
		CHECK(req_id > 0);
		CHECK(client->inFlightCount() == 1);

		// 模拟正常响应
		ResponseEnvelope resp{};
		resp.instance_id = 2;
		resp.generation = 10;
		resp.request_id = req_id;
		resp.status = SaacAccountStatus::kOk;

		CHECK(client->feedResponse(resp));
		CHECK(callback_invoked);
		CHECK(received_status == SaacAccountStatus::kOk);
		CHECK(received_cdkey == "test_user_01");
		CHECK(client->inFlightCount() == 0);
		CHECK(client->staleDropCount() == 0);
	}

	SUBCASE("跨实例世代号不匹配防御丢弃 (消弭原版 fdid 归零串包致命缺陷 C33)")
	{
		bool callback_invoked = false;
		std::uint64_t req_id = client->requestLogin("user_new_generation", "pass123",
		                                            [&](SaacAccountStatus /*st*/, const auto & /*cdkey*/, const auto & /*chars*/)
		                                            {
			                                            callback_invoked = true;
		                                            });
		CHECK(client->inFlightCount() == 1);

		// 假设此时收到了属于旧世代 (generation = 9) 的迟到回包
		ResponseEnvelope stale_resp{};
		stale_resp.instance_id = 2;
		stale_resp.generation = 9; // 旧世代！
		stale_resp.request_id = req_id;
		stale_resp.status = SaacAccountStatus::kOk;

		// 必须被强行拒绝拦截
		CHECK_FALSE(client->feedResponse(stale_resp));
		CHECK_FALSE(callback_invoked);
		CHECK(client->inFlightCount() == 1);
		CHECK(client->staleDropCount() == 1); // 记录拦截计次

		// 随后正确世代 (generation = 10) 回包到达，正常消费
		ResponseEnvelope correct_resp{};
		correct_resp.instance_id = 2;
		correct_resp.generation = 10;
		correct_resp.request_id = req_id;
		correct_resp.status = SaacAccountStatus::kOk;

		CHECK(client->feedResponse(correct_resp));
		CHECK(callback_invoked);
		CHECK(client->inFlightCount() == 0);
	}
}

TEST_CASE("S22 SaacClient 超时扫描与 In-Flight 清理机制")
{
	auto client = createSaacClient(/*instance_id=*/1, /*generation=*/1);

	bool timeout_triggered = false;
	SaacAccountStatus status = SaacAccountStatus::kOk;

	std::uint64_t req_id = client->requestCharLoad("cdkey_timeout", 0, [&](SaacAccountStatus st, const auto & /*rec*/)
	                                               {
		                                               timeout_triggered = true;
		                                               status = st; },
	                                               /*timeout_ms=*/50);
	CHECK(req_id > 0);
	CHECK(client->inFlightCount() == 1);
	CHECK(client->timeoutDropCount() == 0);

	// 当前时间不推进，tick 不应超时
	const std::int64_t start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	                                  std::chrono::steady_clock::now().time_since_epoch())
	                                  .count();
	client->tick(start_ms);
	CHECK_FALSE(timeout_triggered);
	CHECK(client->inFlightCount() == 1);

	// 推进超过 50ms (比如 +100ms)
	client->tick(start_ms + 100);
	CHECK(timeout_triggered);
	CHECK(status == SaacAccountStatus::kTimeout);
	CHECK(client->inFlightCount() == 0);
	CHECK(client->timeoutDropCount() == 1);
}

TEST_CASE("S22 账号排他锁与租约申请/释放 (消弭原版移民锁误解缺陷 C45)")
{
	auto client = createSaacClient(/*instance_id=*/1, /*generation=*/1);

	SUBCASE("登入独占锁 (kLoginLock)")
	{
		bool lock_cb = false;
		std::string token;
		std::uint64_t req_id = client->requestLock("player_cdkey_1", AccountLockType::kLoginLock,
		                                           [&](SaacAccountStatus st, const std::string &lease)
		                                           {
			                                           CHECK(st == SaacAccountStatus::kOk);
			                                           lock_cb = true;
			                                           token = lease;
		                                           });
		CHECK(client->completeInFlight(req_id, SaacAccountStatus::kOk));
		CHECK(lock_cb);
		CHECK_FALSE(token.empty());

		// 释放锁
		bool unlock_cb = false;
		std::uint64_t unl_id = client->requestUnlock("player_cdkey_1", token,
		                                             [&](SaacAccountStatus st, const auto & /*unused*/)
		                                             {
			                                             CHECK(st == SaacAccountStatus::kOk);
			                                             unlock_cb = true;
		                                             });
		CHECK(client->completeInFlight(unl_id, SaacAccountStatus::kOk));
		CHECK(unlock_cb);
	}

	SUBCASE("星系移民锁独立区分 (kMigrationLock 与 C45 隔离验证)")
	{
		bool lock_cb = false;
		std::string token;
		std::uint64_t req_id = client->requestLock("player_migrating", AccountLockType::kMigrationLock,
		                                           [&](SaacAccountStatus st, const std::string &lease)
		                                           {
			                                           CHECK(st == SaacAccountStatus::kOk);
			                                           lock_cb = true;
			                                           token = lease;
		                                           });
		CHECK(client->completeInFlight(req_id, SaacAccountStatus::kOk));
		CHECK(lock_cb);
		CHECK_FALSE(token.empty());
	}
}

TEST_CASE("S22 跨线路全服广播/聊天室分发 (8.0 独有进第一批，对齐 00 §7)")
{
	auto client = createSaacClient(/*instance_id=*/1, /*generation=*/1);

	std::vector<WorldBroadcastMessage> received_msgs;
	client->registerBroadcastListener([&](const WorldBroadcastMessage &msg)
	                                  { received_msgs.push_back(msg); });

	bool ack_called = false;
	client->broadcastWorldMessage(/*channel=*/1, "GM_Server", "全服维护通知: 5分钟后更新", [&](bool success)
	                              {
		ack_called = true;
		CHECK(success); });

	CHECK(ack_called);
	REQUIRE(received_msgs.size() == 1);
	CHECK(received_msgs[0].channel == 1);
	CHECK(received_msgs[0].sender == "GM_Server");
	CHECK(received_msgs[0].text == "全服维护通知: 5分钟后更新");
	CHECK(received_msgs[0].timestamp_ms > 0);
}

} // namespace SA::Saac
