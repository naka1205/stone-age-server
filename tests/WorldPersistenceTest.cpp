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
