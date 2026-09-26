#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "world/Api.h"
#include <doctest/doctest.h>
#include <vector>

namespace
{
using Op = SA::SessionStorage::Operation;
using Code = SA::Transport::AccountCode;
class PendingStorage final : public SA::SessionStorage::Service
{
  public:
	std::vector<SA::SessionStorage::Request> requests;
	std::vector<SA::SessionStorage::Completion> completions;
	std::size_t pending = 0;
	bool submit(SA::SessionStorage::Request request) override
	{
		requests.push_back(std::move(request));
		++pending;
		return true;
	}
	std::vector<SA::SessionStorage::Completion> poll() override
	{
		auto out = completions;
		completions.clear();
		return out;
	}
	bool idle() const override { return pending == 0; }
	void finish(Code code = Code::ACCOUNT_OK, SA::Domain::CharacterRecord record = {},
	            SA::IDL::FixedVec<SA::Transport::CharacterSummary, 2> chars = {})
	{
		REQUIRE(pending > 0);
		const auto &request = requests.back();
		SA::SessionStorage::Completion out{};
		out.operation = request.operation;
		out.session = request.session;
		out.correlation = request.correlation;
		out.logout = request.logout;
		out.code = code;
		out.character = record;
		out.characters = chars;
		completions.push_back(out);
		--pending;
	}
};

struct Fixture
{
	SA::Platform::ServerConfig config = SA::Platform::parseConfig(R"({"log_level":"error"})").config;
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{42};
	SA::Net::LoopbackTransport transport;
	PendingStorage storage;
	SA::World::World world{config, clock, logger, random, transport, &storage};
	SA::Net::SessionId id = 0;
	SA::Domain::CharacterRecord saved{};
	Fixture()
	{
		saved.schema_ver = 1;
		saved.char_id = 99;
		saved.revision = 6;
		saved.player.floor = 7;
		saved.player.x = 10;
		saved.player.y = 10;
		saved.player.image = 100000;
		saved.player.default_pet = -1;
		saved.player.level = 1;
		saved.player.hp = 50;
		saved.player.mp = saved.player.max_mp = 100;
		saved.player.vital = 800;
		saved.player.str = 800;
		saved.player.tough = saved.player.dex = 200;
		(void)saved.player.name.assign("存档角色");
		SA::Domain::PetSlot pet{};
		pet.slot = 2;
		pet.uid = 123;
		pet.value.level = 1;
		pet.value.hp = 17;
		(void)pet.value.name.assign("乌力");
		(void)saved.pets.push_back(pet);
		world.configurePlayable(SA::World::makeFixtureMap(20, 20), SA::World::makeFixtureAttr(), "test-content", saved);
		id = transport.connect();
		SA::Transport::HandshakeRequest hello{};
		hello.protocol_version = config.protocol_version;
		feed(hello, 1);
	}
	template <typename Message>
	void feed(const Message &message, std::uint64_t correlation)
	{
		std::vector<std::uint8_t> bytes;
		REQUIRE(SA::Net::encodeFramed(correlation, message, bytes));
		transport.deliver(id, bytes.data(), bytes.size());
		world.tick();
	}
	void login()
	{
		SA::Transport::LoginRequest request{};
		(void)request.login.assign("test");
		(void)request.password.assign("long-test-password");
		feed(request, 2);
		REQUIRE(storage.requests.back().operation == Op::kLogin);
		storage.finish();
		world.tick();
	}
	void select()
	{
		login();
		SA::Transport::SelectCharacterRequest request{};
		request.char_id = saved.char_id;
		feed(request, 3);
		storage.finish(Code::ACCOUNT_OK, saved);
		world.tick();
		REQUIRE(world.playerCount() == 1);
		transport.clearSent(id);
	}
	void step()
	{
		SA::Domain::WalkRequest request{};
		request.x = 11;
		request.y = 10;
		(void)request.direction.assign("c");
		feed(request, 0);
	}
	std::size_t messages(SA::IDL::MsgId type) const
	{
		SA::Wire::FrameReader reader;
		const auto &bytes = transport.sent(id);
		REQUIRE(reader.push(bytes.data(), bytes.size()));
		std::size_t count = 0;
		const std::uint8_t *frame = nullptr;
		std::uint32_t size = 0;
		while (reader.next(&frame, &size) == SA::Wire::FrameStatus::kOk)
		{
			SA::Wire::EnvelopeView envelope;
			REQUIRE(SA::Wire::decodeEnvelope(frame, size, envelope));
			if (envelope.msg_id == static_cast<std::uint32_t>(type))
				++count;
			reader.pop();
		}
		return count;
	}
	template <typename Message>
	std::optional<Message> decodeLast(SA::IDL::MsgId type) const
	{
		SA::Wire::FrameReader reader;
		const auto &bytes = transport.sent(id);
		if (!reader.push(bytes.data(), bytes.size()))
			return std::nullopt;
		const std::uint8_t *frame = nullptr;
		std::uint32_t size = 0;
		std::optional<Message> res;
		while (reader.next(&frame, &size) == SA::Wire::FrameStatus::kOk)
		{
			SA::Wire::EnvelopeView envelope;
			if (SA::Wire::decodeEnvelope(frame, size, envelope))
			{
				if (envelope.msg_id == static_cast<std::uint32_t>(type))
				{
					Message msg{};
					SA::IDL::Reader r(envelope.body, envelope.body_len);
					decode(r, msg);
					if (r.ok())
						res = msg;
				}
			}
			reader.pop();
		}
		return res;
	}
};
} // namespace

TEST_CASE("Playable handshake waits for account authentication and character load")
{
	Fixture f;
	CHECK(f.world.playerCount() == 0);
	f.login();
	CHECK(f.world.playerCount() == 0);
	CHECK(f.world.sessionState(f.id) == SA::Net::SessionState::kSelectingChar);
	SA::Transport::SelectCharacterRequest request{};
	request.char_id = 999;
	f.feed(request, 3);
	f.storage.finish(Code::ACCOUNT_NOT_FOUND);
	f.world.tick();
	CHECK(f.world.playerCount() == 0);
	CHECK(f.world.sessionState(f.id) == SA::Net::SessionState::kSelectingChar);
}

TEST_CASE("Movement submits an immutable snapshot and does not block ticks or acknowledge an unfinished save")
{
	Fixture f;
	f.select();
	f.step();
	REQUIRE(f.storage.requests.back().operation == Op::kSave);
	const auto record = f.storage.requests.back().character;
	CHECK(record.player.x == 11);
	CHECK(record.char_id == 99);
	CHECK(record.revision == 6);
	REQUIRE(record.pets.size() == 1);
	CHECK(record.pets[0].uid == 123);
	CHECK(record.pets[0].slot == 2);
	f.step();
	const auto before = f.world.ticks();
	for (int i = 0; i < 100; ++i)
	{
		f.clock.advance(10);
		f.world.tick();
	}
	CHECK(f.world.ticks() == before + 100);
	CHECK(f.world.playerPos(f.id).x == 11);
	CHECK(f.messages(SA::IDL::MsgId::CharacterState) == 0);
	CHECK(f.messages(SA::IDL::MsgId::SaveResult) == 0);
}

TEST_CASE("Logout failure retains entities and lease; only a durable acknowledgement releases them")
{
	Fixture f;
	f.select();
	SA::Transport::SaveRequest request{};
	request.logout = true;
	f.feed(request, 4);
	CHECK(f.world.playerCount() == 1);
	CHECK_FALSE(f.transport.closed(f.id));
	CHECK(f.messages(SA::IDL::MsgId::SaveResult) == 0);
	f.storage.finish(Code::ACCOUNT_UNAVAILABLE);
	f.world.tick();
	CHECK(f.world.playerCount() == 1);
	CHECK_FALSE(f.transport.closed(f.id));
	CHECK(f.world.sessionState(f.id) == SA::Net::SessionState::kLoggingOut);
	f.feed(request, 5);
	auto durable = f.storage.requests.back().character;
	++durable.revision;
	f.storage.finish(Code::ACCOUNT_OK, durable);
	f.world.tick();
	f.world.tick();
	CHECK(f.transport.closed(f.id));
	CHECK(f.world.playerCount() == 0);
	CHECK(f.world.petCount() == 0);
}

TEST_CASE("Disconnect during an in-flight save waits for its revision before the final save")
{
	Fixture f;
	f.select();
	f.step();
	f.transport.close(f.id);
	CHECK(f.world.playerCount() == 1);
	auto first = f.storage.requests.back().character;
	++first.revision;
	f.storage.finish(Code::ACCOUNT_OK, first);
	f.world.tick();
	REQUIRE(f.storage.requests.back().logout);
	CHECK(f.storage.requests.back().character.revision == first.revision);
	CHECK(f.storage.requests.back().character.player.x == 11);
	CHECK(f.world.playerCount() == 1);
	auto final = f.storage.requests.back().character;
	++final.revision;
	f.storage.finish(Code::ACCOUNT_OK, final);
	f.world.tick();
	CHECK(f.world.playerCount() == 0);
	CHECK(f.world.sessionCount() == 0);
}

TEST_CASE("Shutdown drains character saves before reporting stopped")
{
	Fixture f;
	f.select();
	f.world.requestShutdown();
	f.world.tick();
	CHECK_FALSE(f.world.stopped());
	REQUIRE(f.storage.requests.back().logout);
	auto record = f.storage.requests.back().character;
	++record.revision;
	f.storage.finish(Code::ACCOUNT_OK, record);
	f.world.tick();
	CHECK(f.world.stopped());
	CHECK(f.world.playerCount() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  经济地基:石币的存档接线与上限边界(计划项 A4)
// ═══════════════════════════════════════════════════════════════════════════
//
// ★ 为什么这几条在**持久化面**而不是 world_tick:上限 / 溢出的边界需要"进场时就带着
//   一大笔钱"的玩家,而 Model::Player 的余额**只能**从存档记录恢复(账本是唯一写入口,
//   World 的公开面刻意没有任何"摆钱"方法 —— world/Api.h 的 GoldLedger 节卷首)。⇒ 存档记录是
//   摆位的唯一合法面,而这也正是本批要验的那条链本身。

namespace
{
// ⚠️ 完成判据 = `stats(battle)->finished`(战斗收场后 field 观察面仍保留快照,
//    `battleField != nullptr` 不是"已收场"的判据)。
bool battleFinished(const SA::World::World &world, SA::World::BattleId battle)
{
	const SA::World::BattleStats *st = world.stats(battle);
	return st != nullptr && st->finished;
}

// 一场"只站玩家"的战斗:敌方一侧全空 ⇒ 首次结算即判敌方全灭 ⇒ finished
// ⇒ 战果结算段给 +10(DR-EC6)。战斗随即 retire。
void finishSoloBattle(Fixture &f)
{
	SA::Rules::BattleField pf{};
	SA::Rules::Combatant &me = pf.at(0);
	me.occupied = true;
	me.kind = SA::Rules::CombatantKind::kPlayer;
	me.slot = 0;
	me.level = 20;
	me.hp = 100000;
	me.max_hp = 100000;
	me.attack = 10;
	me.defense = 10000;
	me.quick = 200;
	me.luck = 10;
	const SA::World::BattleId battle = f.world.startBattle(pf);
	REQUIRE(f.world.joinBattle(battle, f.id, 0));
	for (int i = 0; i < 10; ++i)
	{
		if (battleFinished(f.world, battle))
			break;
		const SA::Rules::BattleField *fld = f.world.battleField(battle);
		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = battle;
		cmd.turn = fld->turn;
		cmd.command_kind = SA::Domain::BattleCommand::CommandKind::WAIT;
		f.world.onBattleCommand(f.id, cmd);
		f.clock.advance(2000);
		f.world.tick();
	}
	REQUIRE(battleFinished(f.world, battle));
}
} // namespace

TEST_CASE("Economy: 战斗金币进存档快照,且在上限处被钳位(钳位 + 溢出处置可观察)")
{
	Fixture f;
	// 摆位:距上限只差 1(上限 = 1,000,000;0 转,char_base.c:3212)。
	// ⚠️ 这是**存档记录**的摆位,不是账务写点 —— 守卫脚本不扫 tests/,理由见其卷首 ②。
	f.saved.player.gold = 999999;
	f.select();
	REQUIRE(f.world.playerGold(f.id) == 999999); // install 从记录恢复

	finishSoloBattle(f);

	// 战斗收场触发存档(saveCharacter:false, corr 0)⇒ 快照必须带结算后的余额。
	REQUIRE(f.storage.requests.back().operation == Op::kSave);
	const auto record = f.storage.requests.back().character;
	// 999,999 + 10 = 1,000,009 > 上限 ⇒ 钳位:**溢出 9 被具名销毁**(不是静默,
	// 审计事件带 overflow=9 与 disposition=clamped,见 world_tick 的日志通道用例)。
	CHECK(record.player.gold == 1000000);
	CHECK(f.world.playerGold(f.id) == 1000000);
}

TEST_CASE("Economy: 超上限的存量第一次变更时被拉回上限(源码 :3232 预钳)")
{
	Fixture f;
	// 原版玩家石币可被推过上限(`NPC_AcceptDel` 不看上限,`12` §3.2 C7)⇒ 存量
	// 超上限是**真实存在过的状态**,不是我们发明的:存档里就带着它。
	f.saved.player.gold = 1500000;
	f.select();
	REQUIRE(f.world.playerGold(f.id) == 1500000);

	finishSoloBattle(f);

	const auto record = f.storage.requests.back().character;
	// ① 先钳回上限(1,000,000)② +10 又全溢出 ⇒ 终值 = 上限。
	CHECK(record.player.gold == 1000000);
	CHECK(f.world.playerGold(f.id) == 1000000);
}

TEST_CASE("Economy: 重登一致 —— 战斗赚的钱落盘后原样回到实体")
{
	Fixture f;
	f.saved.player.gold = 5;
	f.select();
	REQUIRE(f.world.playerGold(f.id) == 5);

	finishSoloBattle(f);
	CHECK(f.world.playerGold(f.id) == 15);

	// 收场那笔存档先完成(否则登出会排在它后面)。
	{
		const auto record = f.storage.requests.back().character;
		f.storage.finish(Code::ACCOUNT_OK, record);
		f.world.tick();
	}

	// 正常登出 ⇒ 拿到**落盘后的**记录。
	SA::Transport::SaveRequest request{};
	request.logout = true;
	f.feed(request, 4);
	REQUIRE(f.storage.requests.back().logout);
	auto durable = f.storage.requests.back().character;
	CHECK(durable.player.gold == 15); // ★ 存档里就是 15
	++durable.revision;
	f.storage.finish(Code::ACCOUNT_OK, durable);
	f.world.tick();
	f.world.tick();
	CHECK(f.transport.closed(f.id));
	CHECK(f.world.playerCount() == 0);

	// ── 重登:同一条记录 ⇒ 同一个余额 ──
	f.id = f.transport.connect();
	SA::Transport::HandshakeRequest hello{};
	hello.protocol_version = f.config.protocol_version;
	f.feed(hello, 1);
	f.login();
	SA::Transport::SelectCharacterRequest select_request{};
	select_request.char_id = durable.char_id;
	f.feed(select_request, 3);
	f.storage.finish(Code::ACCOUNT_OK, durable);
	f.world.tick();
	REQUIRE(f.world.playerCount() == 1);
	CHECK(f.world.playerGold(f.id) == 15); // ★★ 重登一致
}

// ═══════════════════════════════════════════════════════════════════════════
//  选角流程与多角色槽位系统 (阶段 2: 选角流程与多角色槽位)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("选角流程: 账号多角色槽位查询与按 ID 选角进入")
{
	Fixture f;
	f.transport.clearSent(f.id);

	SA::Transport::LoginRequest req{};
	(void)req.login.assign("test_account");
	(void)req.password.assign("pwd123456");
	f.feed(req, 20);
	REQUIRE(f.storage.requests.back().operation == Op::kLogin);

	// 构造 2 角色摘要 (2 slots)
	SA::IDL::FixedVec<SA::Transport::CharacterSummary, 2> chars{};
	SA::Transport::CharacterSummary s1{};
	s1.char_id = 101;
	(void)s1.name.assign("石器小英雄");
	s1.level = 1;
	s1.image = 100000;
	(void)chars.push_back(s1);

	SA::Transport::CharacterSummary s2{};
	s2.char_id = 102;
	(void)s2.name.assign("尼斯老猎人");
	s2.level = 20;
	s2.image = 100020;
	(void)chars.push_back(s2);

	f.storage.finish(Code::ACCOUNT_OK, {}, chars);
	f.world.tick();

	auto login_res = f.decodeLast<SA::Transport::LoginResult>(SA::IDL::MsgId::LoginResult);
	REQUIRE(login_res.has_value());
	CHECK(login_res->code == Code::ACCOUNT_OK);
	REQUIRE(login_res->characters.size() == 2);
	CHECK(login_res->characters[0].char_id == 101);
	CHECK(std::string(login_res->characters[0].name.c_str()) == "石器小英雄");
	CHECK(login_res->characters[0].level == 1);
	CHECK(login_res->characters[0].image == 100000);
	CHECK(login_res->characters[1].char_id == 102);
	CHECK(std::string(login_res->characters[1].name.c_str()) == "尼斯老猎人");
	CHECK(login_res->characters[1].level == 20);
	CHECK(login_res->characters[1].image == 100020);

	// 选角 101 进入
	SA::Transport::SelectCharacterRequest sel1{};
	sel1.char_id = 101;
	f.feed(sel1, 21);
	REQUIRE(f.storage.requests.back().operation == Op::kSelect);
	CHECK(f.storage.requests.back().character.char_id == 101);

	SA::Domain::CharacterRecord rec1 = f.saved;
	rec1.char_id = 101;
	rec1.player.name = s1.name;
	rec1.player.image = s1.image;
	rec1.player.level = s1.level;

	f.storage.finish(Code::ACCOUNT_OK, rec1);
	f.world.tick();

	CHECK(f.world.playerCount() == 1);
	auto ppos1 = f.world.playerPos(f.id);
	CHECK(ppos1.valid);

	auto char_res1 = f.decodeLast<SA::Transport::CharacterResult>(SA::IDL::MsgId::CharacterResult);
	REQUIRE(char_res1.has_value());
	CHECK(char_res1->code == Code::ACCOUNT_OK);
	CHECK(char_res1->character.char_id == 101);
	CHECK(std::string(char_res1->character.player.name.c_str()) == "石器小英雄");

	// 登出
	SA::Transport::SaveRequest save_req{};
	save_req.logout = true;
	f.feed(save_req, 22);
	auto durable1 = f.storage.requests.back().character;
	++durable1.revision;
	f.storage.finish(Code::ACCOUNT_OK, durable1);
	f.world.tick();
	f.world.tick();
	CHECK(f.transport.closed(f.id));
	CHECK(f.world.playerCount() == 0);

	// 重登并选角 102 进入
	f.id = f.transport.connect();
	SA::Transport::HandshakeRequest hello{};
	hello.protocol_version = f.config.protocol_version;
	f.feed(hello, 1);
	f.transport.clearSent(f.id);

	f.feed(req, 23);
	f.storage.finish(Code::ACCOUNT_OK, {}, chars);
	f.world.tick();

	SA::Transport::SelectCharacterRequest sel2{};
	sel2.char_id = 102;
	f.feed(sel2, 24);
	REQUIRE(f.storage.requests.back().operation == Op::kSelect);
	CHECK(f.storage.requests.back().character.char_id == 102);

	SA::Domain::CharacterRecord rec2 = f.saved;
	rec2.char_id = 102;
	rec2.player.name = s2.name;
	rec2.player.image = s2.image;
	rec2.player.level = s2.level;

	f.storage.finish(Code::ACCOUNT_OK, rec2);
	f.world.tick();

	CHECK(f.world.playerCount() == 1);
	auto char_res2 = f.decodeLast<SA::Transport::CharacterResult>(SA::IDL::MsgId::CharacterResult);
	REQUIRE(char_res2.has_value());
	CHECK(char_res2->code == Code::ACCOUNT_OK);
	CHECK(char_res2->character.char_id == 102);
	CHECK(std::string(char_res2->character.player.name.c_str()) == "尼斯老猎人");
	CHECK(char_res2->character.player.image == 100020);
}

TEST_CASE("选角流程: [RV-1] 槽位已满新建角色被阻断 (ACCOUNT_CONFLICT)")
{
	Fixture f;
	f.login();

	// 模拟已满 2 槽位，客户端再发起新建角色请求
	SA::Transport::CreateCharacterRequest create_req{};
	(void)create_req.name.assign("多余角色");
	create_req.image = f.saved.player.image;
	create_req.vital = 8;
	create_req.str = 8;
	create_req.tough = 2;
	create_req.dex = 2;
	create_req.earth = 10;
	f.feed(create_req, 30);
	REQUIRE(f.storage.requests.back().operation == Op::kCreate);

	// Storage 返回 ACCOUNT_CONFLICT (对齐 MySQL 槽位超限回滚)
	f.storage.finish(Code::ACCOUNT_CONFLICT);
	f.world.tick();

	auto res = f.decodeLast<SA::Transport::CharacterResult>(SA::IDL::MsgId::CharacterResult);
	REQUIRE(res.has_value());
	// ★ [RV-1] 变异验证断言: 槽位超限必须严格返回 ACCOUNT_CONFLICT 并拒绝入场
	CHECK(res->code == Code::ACCOUNT_CONFLICT);
	CHECK(f.world.playerCount() == 0);
	CHECK(f.world.sessionState(f.id) == SA::Net::SessionState::kSelectingChar);
}

TEST_CASE("角色创建: 12 种原型与 48 种配色外观合法性与头像映射")
{
	Fixture f;

	// 1. 静态合法性与头像映射验证 (48 种配色)
	for (std::int32_t k = 0; k < 48; ++k)
	{
		const std::int32_t img = 100000 + k * 5;
		CHECK(SA::World::World::isValidPlayerImage(img));
		const std::int32_t expected_face = 30000 + (k / 4) * 100 + (k % 4) * 25;
		CHECK(SA::World::World::computeFaceImage(img) == expected_face);
	}

	// 2. 非法图号拦截
	CHECK_FALSE(SA::World::World::isValidPlayerImage(99999));
	CHECK_FALSE(SA::World::World::isValidPlayerImage(100001)); // 武器帧不是空手裸模
	CHECK_FALSE(SA::World::World::isValidPlayerImage(100004));
	CHECK_FALSE(SA::World::World::isValidPlayerImage(100236));
	CHECK_FALSE(SA::World::World::isValidPlayerImage(100240));
	CHECK_FALSE(SA::World::World::isValidPlayerImage(-1));

	f.login();

	// 3. 提交非法图号 100001 创建角色被本地门禁拦截 (ACCOUNT_INVALID)
	const auto req_count_before = f.storage.requests.size();
	SA::Transport::CreateCharacterRequest bad_req{};
	(void)bad_req.name.assign("非法外观");
	bad_req.image = 100001;
	bad_req.vital = 8;
	bad_req.str = 8;
	bad_req.tough = 2;
	bad_req.dex = 2;
	bad_req.earth = 10;
	f.feed(bad_req, 31);

	// 未向 storage 提交
	CHECK(f.storage.requests.size() == req_count_before);
	auto bad_res = f.decodeLast<SA::Transport::CharacterResult>(SA::IDL::MsgId::CharacterResult);
	REQUIRE(bad_res.has_value());
	CHECK(bad_res->code == Code::ACCOUNT_INVALID);

	// 4. 提交合法原型 100140 (少女1, 绿色) 创建角色
	SA::Transport::CreateCharacterRequest good_req{};
	(void)good_req.name.assign("合法少女");
	good_req.image = 100140; // k = 28, archetype = 7, color = 0
	good_req.vital = 8;
	good_req.str = 8;
	good_req.tough = 2;
	good_req.dex = 2;
	good_req.wind = 10;
	f.feed(good_req, 32);

	REQUIRE(f.storage.requests.size() == req_count_before + 1);
	const auto submitted = f.storage.requests.back().character;
	CHECK(submitted.player.image == 100140);
	CHECK(submitted.player.face_image == 30700); // 30000 + 7*100 + 0*25
	CHECK(submitted.player.wind == 100);
}

TEST_CASE("角色创建: [RV-2] 初始四维与地水火风属性点分配合法性校验")
{
	Fixture f;
	f.login();
	const auto req_count_base = f.storage.requests.size();

	auto tryCreate = [&](int v, int s, int t, int d, int ea, int wa, int fi, int wi) -> Code
	{
		SA::Transport::CreateCharacterRequest r{};
		(void)r.name.assign("属性测试");
		r.image = f.saved.player.image;
		r.vital = v;
		r.str = s;
		r.tough = t;
		r.dex = d;
		r.earth = ea;
		r.water = wa;
		r.fire = fi;
		r.wind = wi;
		f.feed(r, 40);
		auto res = f.decodeLast<SA::Transport::CharacterResult>(SA::IDL::MsgId::CharacterResult);
		if (res.has_value())
			return res->code;
		return Code::ACCOUNT_OK;
	};

	// 1. 点数超标拦截: 10 + 8 + 2 + 2 = 22 > 20
	CHECK(tryCreate(10, 8, 2, 2, 10, 0, 0, 0) == Code::ACCOUNT_INVALID);

	// 2. 点数不足拦截: 5 + 5 + 2 + 2 = 14 < 20
	CHECK(tryCreate(5, 5, 2, 2, 10, 0, 0, 0) == Code::ACCOUNT_INVALID);

	// 3. 单项负数拦截
	CHECK(tryCreate(-1, 15, 3, 3, 10, 0, 0, 0) == Code::ACCOUNT_INVALID);

	// 4. 单项超 20 拦截
	CHECK(tryCreate(21, 0, 0, -1, 10, 0, 0, 0) == Code::ACCOUNT_INVALID);

	// 5. 属性点总和 != 10 拦截: 8 + 3 = 11
	CHECK(tryCreate(8, 8, 2, 2, 8, 0, 0, 3) == Code::ACCOUNT_INVALID);

	// 6. 相克属性共存拦截: 地 + 火
	CHECK(tryCreate(8, 8, 2, 2, 5, 0, 5, 0) == Code::ACCOUNT_INVALID);

	// 7. 相克属性共存拦截: 水 + 风
	CHECK(tryCreate(8, 8, 2, 2, 0, 5, 0, 5) == Code::ACCOUNT_INVALID);

	// 8. 超过 2 项属性拦截: 地 + 水 + 风
	CHECK(tryCreate(8, 8, 2, 2, 4, 3, 0, 3) == Code::ACCOUNT_INVALID);

	// ★ 均被本地前置防御拦截，无任何请求漏到 storage
	CHECK(f.storage.requests.size() == req_count_base);

	// 9. 合法分配通过: 体 5, 力 7, 耐 2, 敏 6 (总和 20); 地 5, 水 5 (总和 10)
	SA::Transport::CreateCharacterRequest ok_req{};
	(void)ok_req.name.assign("合格分配者");
	ok_req.image = f.saved.player.image;
	ok_req.vital = 5;
	ok_req.str = 7;
	ok_req.tough = 2;
	ok_req.dex = 6;
	ok_req.earth = 5;
	ok_req.water = 5;
	f.feed(ok_req, 41);

	REQUIRE(f.storage.requests.size() == req_count_base + 1);
	const auto sub = f.storage.requests.back().character.player;
	CHECK(sub.vital == 500);
	CHECK(sub.str == 700);
	CHECK(sub.tough == 200);
	CHECK(sub.dex == 600);
	CHECK(sub.earth == 50);
	CHECK(sub.water == 50);
	CHECK(sub.fire == 0);
	CHECK(sub.wind == 0);
	// ★ [RV-2] 初始血量由四维精准换算
	const auto expected_hp = SA::Rules::deriveBaseStats(500, 700, 200, 600).max_hp;
	CHECK(sub.hp == expected_hp);
}

TEST_CASE("选角流程: 四大新手村出生地分配与兜底")
{
	Fixture f;
	f.login();

	// 1. 验证默认新手村配置
	auto sp0 = f.world.hometownSpawn(0);
	CHECK(sp0.floor == 1000);
	CHECK(sp0.x == 98);
	CHECK(sp0.y == 93);

	// 2. 自定义配置玛丽娜斯 (Hometown 0) 到可通行的 (7, 12, 12)
	f.world.setHometownSpawn(0, 7, 12, 12);
	f.world.setSessionHometown(f.id, 0);
	CHECK(f.world.sessionHometown(f.id) == 0);

	SA::Transport::CreateCharacterRequest req0{};
	(void)req0.name.assign("玛丽娜斯新手");
	req0.image = f.saved.player.image;
	req0.vital = 8;
	req0.str = 8;
	req0.tough = 2;
	req0.dex = 2;
	req0.earth = 10;
	f.feed(req0, 50);

	REQUIRE(f.storage.requests.back().operation == Op::kCreate);
	const auto rec0 = f.storage.requests.back().character.player;
	CHECK(rec0.floor == 7);
	CHECK(rec0.x == 12);
	CHECK(rec0.y == 12);
	f.storage.finish(Code::ACCOUNT_UNAVAILABLE);
	f.world.tick();
	f.transport.clearSent(f.id);

	// 3. 未设置新手村 (或设置 -1 / 越界 99)，平滑兜底为 character_defaults 初始坐标
	f.world.setSessionHometown(f.id, -1);
	CHECK(f.world.sessionHometown(f.id) == -1);

	SA::Transport::CreateCharacterRequest req_def{};
	(void)req_def.name.assign("默认村新手");
	req_def.image = f.saved.player.image;
	req_def.vital = 8;
	req_def.str = 8;
	req_def.tough = 2;
	req_def.dex = 2;
	req_def.wind = 10;
	f.feed(req_def, 51);

	REQUIRE(f.storage.requests.back().operation == Op::kCreate);
	const auto rec_def = f.storage.requests.back().character.player;
	CHECK(rec_def.floor == f.saved.player.floor);
	CHECK(rec_def.x == f.saved.player.x);
	CHECK(rec_def.y == f.saved.player.y);
}

TEST_CASE("选角流程: 在线中收到选角/建角请求被冲突阻断 (ACCOUNT_CONFLICT)")
{
	Fixture f;
	f.select(); // 进入大世界，当前角色在线且 conn.char_id != 0

	const auto req_count_before = f.storage.requests.size();

	// 1. 业务接口防御: 已在世界中(conn.char_id != 0)，直接调用 onSelectCharacter 被 ACCOUNT_CONFLICT 拦截
	SA::Transport::SelectCharacterRequest sel{};
	sel.char_id = 999;
	f.world.onSelectCharacter(f.id, sel, 60);

	// 未向 storage 提交新请求
	CHECK(f.storage.requests.size() == req_count_before);

	// 2. 业务接口防御: 已在世界中(conn.char_id != 0)，直接调用 onCreateCharacter 被 ACCOUNT_CONFLICT 拦截
	SA::Transport::CreateCharacterRequest create{};
	(void)create.name.assign("在线偷建");
	create.image = f.saved.player.image;
	create.vital = 8;
	create.str = 8;
	create.tough = 2;
	create.dex = 2;
	create.earth = 10;
	f.world.onCreateCharacter(f.id, create, 61);

	// 未向 storage 提交新请求
	CHECK(f.storage.requests.size() == req_count_before);

	// 3. 协议信令防护: 若在线状态经由网关字节流强送选角请求，状态机会直接判定协议越权并安全断开连接
	f.feed(sel, 62);
	CHECK(f.transport.closed(f.id));
}

TEST_CASE("选角流程: 多楼层大世界跨图角色恢复登入")
{
	Fixture f;

	// 为世界注册 Floor 1000 地图 (160x160)
	f.world.loadFloorMap(1000, SA::World::makeFixtureMap(160, 160));
	REQUIRE(f.world.findFloorMap(1000) != nullptr);

	f.login();

	// 角色此前存档在 Floor 1000 的 (25, 25)
	SA::Domain::CharacterRecord fl1000_char = f.saved;
	fl1000_char.char_id = 888;
	fl1000_char.player.floor = 1000;
	fl1000_char.player.x = 25;
	fl1000_char.player.y = 25;
	(void)fl1000_char.player.name.assign("跨图旅行者");

	SA::Transport::SelectCharacterRequest sel{};
	sel.char_id = 888;
	f.feed(sel, 70);

	f.storage.finish(Code::ACCOUNT_OK, fl1000_char);
	f.world.tick();

	// 成功载入，不再被 default floor 误杀
	CHECK(f.world.playerCount() == 1);
	auto ppos = f.world.playerPos(f.id);
	CHECK(ppos.valid);
	CHECK(ppos.floor == 1000);
	CHECK(ppos.x == 25);
	CHECK(ppos.y == 25);
}
