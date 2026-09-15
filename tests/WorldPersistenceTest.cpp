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
	void finish(Code code = Code::ACCOUNT_OK, SA::Domain::CharacterRecord record = {})
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
