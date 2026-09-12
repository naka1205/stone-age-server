#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "session_storage/Api.h"
#include <chrono>
#include <cstdlib>
#include <doctest/doctest.h>
#include <hiredis.h>
#include <memory>
#include <mysql/jdbc.h>
#include <random>
#include <string>
#include <thread>

using namespace SA::SessionStorage;
using Code = SA::Transport::AccountCode;

static Completion transact(Service &service, Request request)
{
	const auto session = request.session;
	const auto operation = request.operation;
	REQUIRE(service.submit(std::move(request)));
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	while (std::chrono::steady_clock::now() < deadline)
	{
		for (const auto &out : service.poll())
			if (out.session == session && out.operation == operation)
				return out;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	FAIL("storage completion timed out");
	return {};
}

static std::string temporaryPassword()
{
	std::random_device random;
	return "ephemeral-" + std::to_string(random()) + '-' + std::to_string(random()) + '-' + std::to_string(random());
}

static void removeTestLease(const Settings &settings, std::uint64_t character)
{
	// Remove only the lease belonging to the character created by this test.
	// This emulates lease expiry while its old worker is still alive.
	struct ConnectorThread
	{
		sql::mysql::MySQL_Driver *driver = sql::mysql::get_mysql_driver_instance();
		ConnectorThread() { driver->threadInit(); }
		~ConnectorThread() { driver->threadEnd(); }
	} thread;
	sql::ConnectOptionsMap options;
	options["hostName"] = "tcp://" + settings.mysql_host + ":" + std::to_string(settings.mysql_port);
	options["userName"] = settings.mysql_user;
	options["password"] = settings.mysql_password;
	options["schema"] = std::string("sa_session");
	options[OPT_SSL_MODE] = static_cast<int>(sql::SSL_MODE_VERIFY_IDENTITY);
	options["sslCA"] = settings.mysql_ca;
	std::unique_ptr<sql::Connection> database(sql::mysql::get_mysql_driver_instance()->connect(options));
	std::unique_ptr<sql::PreparedStatement> query(database->prepareStatement("SELECT account_id FROM `character` WHERE char_id=?"));
	query->setUInt64(1, character);
	std::unique_ptr<sql::ResultSet> rows(query->executeQuery());
	REQUIRE(rows->next());
	const auto key = "sa:session:" + std::to_string(rows->getUInt64(1));
	const timeval timeout{3, 0};
	std::unique_ptr<redisContext, decltype(&redisFree)> redis(
	    redisConnectWithTimeout(settings.redis_host.c_str(), settings.redis_port, timeout), redisFree);
	REQUIRE(redis);
	REQUIRE(redis->err == 0);
	REQUIRE(redisSetTimeout(redis.get(), timeout) == REDIS_OK);
	using Reply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;
	Reply auth(static_cast<redisReply *>(redisCommand(redis.get(), "AUTH %b", settings.redis_password.data(), settings.redis_password.size())), freeReplyObject);
	REQUIRE(auth);
	REQUIRE(auth->type == REDIS_REPLY_STATUS);
	Reply removed(static_cast<redisReply *>(redisCommand(redis.get(), "DEL %s", key.c_str())), freeReplyObject);
	REQUIRE(removed);
	REQUIRE(removed->type == REDIS_REPLY_INTEGER);
	REQUIRE(removed->integer == 1);
}

TEST_CASE("MySQL and Redis: durable typed records, lease ownership, revisions and re-login")
{
	const char *file = std::getenv("SA_STORAGE_CONFIG");
	REQUIRE(file != nullptr);
	Settings settings;
	std::string error;
	REQUIRE(loadSettingsFile(file, settings, error));
	auto service = open(settings, error);
	INFO(error);
	REQUIRE(service);
	const auto name = "verify_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
	Request login{};
	login.session = 1;
	login.operation = Operation::kLogin;
	(void)login.login.login.assign(name.c_str());
	(void)login.login.password.assign(temporaryPassword().c_str());
	login.login.register_account = true;
	CHECK(transact(*service, login).code == Code::ACCOUNT_OK);
	login.login.register_account = false;
	login.session = 2;
	CHECK(transact(*service, login).code == Code::ACCOUNT_BUSY);
	Request create{};
	create.operation = Operation::kCreate;
	create.session = 1;
	auto &record = create.character;
	record.schema_ver = 1;
	record.player.level = 1;
	record.player.hp = 62;
	record.player.mp = 100;
	record.player.max_mp = 100;
	record.player.default_pet = -1;
	record.player.vital = 800;
	record.player.str = 800;
	record.player.tough = 200;
	record.player.dex = 200;
	record.player.floor = 100;
	record.player.x = 643;
	record.player.y = 459;
	(void)record.player.name.assign(name.substr(0, 31).c_str());
	auto created = transact(*service, create);
	REQUIRE(created.code == Code::ACCOUNT_OK);
	REQUIRE(created.character.char_id != 0);
	Request save{};
	save.operation = Operation::kSave;
	save.session = 1;
	save.character = created.character;
	save.character.player.exp = 37;
	save.character.player.capture_count = 1;
	save.character.player.x += 1;
	SA::Domain::PetSlot pet{};
	pet.slot = 2;
	pet.value.level = 1;
	pet.value.hp = 17;
	pet.value.growth_str = 255;
	(void)pet.value.name.assign("乌力");
	(void)save.character.pets.push_back(pet);
	SA::Domain::ItemSlot item{};
	item.slot = 9;
	item.value.item_id = 1234;
	item.value.current_pile = 3;
	(void)item.value.name.assign("石头");
	(void)save.character.items.push_back(item);
	auto saved = transact(*service, save);
	REQUIRE(saved.code == Code::ACCOUNT_OK);
	CHECK(saved.character.revision == 1);
	CHECK(saved.character.pets[0].uid != 0);
	CHECK(saved.character.items[0].uid != 0);
	CHECK(transact(*service, save).code == Code::ACCOUNT_CONFLICT);
	save.character = saved.character;
	save.logout = true;
	CHECK(transact(*service, save).code == Code::ACCOUNT_OK);
	service.reset();
	service = open(settings, error);
	REQUIRE(service);
	const auto logged = transact(*service, login);
	REQUIRE(logged.code == Code::ACCOUNT_OK);
	REQUIRE(logged.characters.size() == 1);
	Request select{};
	select.operation = Operation::kSelect;
	select.session = 2;
	select.character.char_id = created.character.char_id + 999999;
	CHECK(transact(*service, select).code == Code::ACCOUNT_NOT_FOUND);
	select.character.char_id = created.character.char_id;
	const auto loaded = transact(*service, select);
	REQUIRE(loaded.code == Code::ACCOUNT_OK);
	CHECK(loaded.character.player.exp == 37);
	CHECK(loaded.character.player.x == 644);
	REQUIRE(loaded.character.pets.size() == 1);
	CHECK(loaded.character.pets[0].value.growth_str == 255);
	CHECK(std::string(loaded.character.pets[0].value.name.c_str()) == "乌力");
	CHECK(loaded.character.pets[0].uid == saved.character.pets[0].uid);
	REQUIRE(loaded.character.items.size() == 1);
	CHECK(loaded.character.items[0].value.current_pile == 3);
	CHECK(loaded.character.items[0].uid == saved.character.items[0].uid);
	CHECK(transact(*service, save).code == Code::ACCOUNT_LEASE_LOST);
	save.session = 2;
	save.character = loaded.character;
	save.logout = true;
	CHECK(transact(*service, save).code == Code::ACCOUNT_OK);
}

TEST_CASE("Real lease takeover fences a still-running old worker and preserves the committed record")
{
	const char *file = std::getenv("SA_STORAGE_CONFIG");
	REQUIRE(file != nullptr);
	Settings settings;
	std::string error;
	REQUIRE(loadSettingsFile(file, settings, error));
	// Also cover cleanup when the second dependency rejects startup.
	Settings invalid = settings;
	invalid.redis_password = temporaryPassword();
	CHECK_FALSE(open(invalid, error));
	auto old_worker = open(settings, error);
	auto new_worker = open(settings, error);
	REQUIRE(old_worker);
	REQUIRE(new_worker);
	const auto name = "lease_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
	Request login{};
	login.session = 11;
	login.operation = Operation::kLogin;
	(void)login.login.login.assign(name.c_str());
	(void)login.login.password.assign(temporaryPassword().c_str());
	login.login.register_account = true;
	REQUIRE(transact(*old_worker, login).code == Code::ACCOUNT_OK);
	login.session = 22;
	login.login.register_account = false;
	CHECK(transact(*new_worker, login).code == Code::ACCOUNT_BUSY);
	Request create{};
	create.operation = Operation::kCreate;
	create.session = 11;
	create.character.schema_ver = 1;
	create.character.player.default_pet = -1;
	create.character.player.level = 1;
	create.character.player.hp = 44;
	create.character.player.exp = 9;
	(void)create.character.player.name.assign(name.substr(0, 31).c_str());
	const auto created = transact(*old_worker, create);
	REQUIRE(created.code == Code::ACCOUNT_OK);
	removeTestLease(settings, created.character.char_id);
	REQUIRE(transact(*new_worker, login).code == Code::ACCOUNT_OK);
	Request stale{};
	stale.session = 11;
	stale.operation = Operation::kSave;
	stale.character = created.character;
	stale.character.player.exp = 999;
	CHECK(transact(*old_worker, stale).code == Code::ACCOUNT_LEASE_LOST);
	Request select{};
	select.session = 22;
	select.operation = Operation::kSelect;
	select.character.char_id = created.character.char_id;
	const auto loaded = transact(*new_worker, select);
	REQUIRE(loaded.code == Code::ACCOUNT_OK);
	CHECK(loaded.character.player.exp == 9);
	CHECK(loaded.character.revision == 0);
	Request save{};
	save.session = 22;
	save.operation = Operation::kSave;
	save.character = loaded.character;
	save.logout = true;
	CHECK(transact(*new_worker, save).code == Code::ACCOUNT_OK);
}
