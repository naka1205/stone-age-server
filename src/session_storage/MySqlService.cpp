#include "data/RecordJson.h"
#include "session_storage/Api.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

#include <hiredis.h>
#include <mysql/jdbc.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace SA::SessionStorage
{
namespace
{
using Code = SA::Transport::AccountCode;
using Statement = std::unique_ptr<sql::PreparedStatement>;
using Rows = std::unique_ptr<sql::ResultSet>;
using Reply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;
using Mono = std::chrono::steady_clock;
constexpr int kIterations = 600000;
constexpr int kLeaseSeconds = 30;

std::string hex(const unsigned char *bytes, std::size_t size)
{
	constexpr char digits[] = "0123456789abcdef";
	std::string out;
	for (std::size_t i = 0; i < size; ++i)
	{
		out += digits[bytes[i] >> 4u];
		out += digits[bytes[i] & 15u];
	}
	return out;
}
std::string randomToken()
{
	unsigned char bytes[16];
	if (RAND_bytes(bytes, sizeof(bytes)) != 1)
		throw std::runtime_error("random generator unavailable");
	return hex(bytes, sizeof(bytes));
}
std::string passwordHash(const std::string &password, const std::string &salt, int iterations)
{
	unsigned char hash[32];
	if (iterations < kIterations || iterations > 2000000 ||
	    PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
	                      reinterpret_cast<const unsigned char *>(salt.data()), static_cast<int>(salt.size()),
	                      iterations, EVP_sha256(), sizeof(hash), hash) != 1)
		throw std::runtime_error("password derivation failed");
	const auto out = hex(hash, sizeof(hash));
	OPENSSL_cleanse(hash, sizeof(hash));
	return out;
}
std::string normalizedLogin(const SA::IDL::FixedStr<63> &input)
{
	std::string out(input.data, input.size());
	if (out.size() < 3 || out.size() > 63)
		return {};
	for (auto &c : out)
	{
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return {};
	}
	return out;
}
template <typename T>
std::string payload(const T &value)
{
	return SA::Data::Json::stringify(SA::Data::JsonCodec<T>::encode(value));
}
template <typename T>
T recordFrom(const sql::SQLString &value)
{
	const std::string text = value.asStdString();
	if (text.size() > 256u * 1024u)
		throw std::invalid_argument("oversized record");
	const auto parsed = SA::Data::Json::parse(text);
	if (!parsed.ok)
		throw std::invalid_argument("invalid record JSON");
	return SA::Data::JsonCodec<T>::decode(parsed.value);
}

class MySqlService final : public Service
{
  public:
	explicit MySqlService(Settings settings) : _settings(std::move(settings)) {}
	~MySqlService() override
	{
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_stopping = true;
		}
		_cv.notify_all();
		if (_worker.joinable())
			_worker.join();
	}
	bool start(std::string &error)
	{
		std::promise<std::string> ready;
		auto result = ready.get_future();
		_worker = std::thread([this, signal = std::move(ready)]() mutable
		                      {
			auto *driver = sql::mysql::get_mysql_driver_instance();
			driver->threadInit();
			try { connect(); signal.set_value({}); }
			catch (const std::exception &)
			{
				// Connector objects must be destroyed on their owning worker before
				// threadEnd, including a failure after MySQL connected but Redis did not.
				_db.reset();
				if (_redis) { redisFree(_redis); _redis = nullptr; }
				driver->threadEnd();
				signal.set_value("MySQL/Redis/TLS/schema startup validation failed");
				return;
			}
			run();
			_db.reset();
			if (_redis) { redisFree(_redis); _redis = nullptr; }
			driver->threadEnd(); });
		error = result.get();
		return error.empty();
	}
	bool submit(Request request) override
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_stopping || _requests.size() >= 256)
			return false;
		_requests.push_back(std::move(request));
		++_pending;
		_cv.notify_one();
		return true;
	}
	std::vector<Completion> poll() override
	{
		std::lock_guard<std::mutex> lock(_mutex);
		std::vector<Completion> out;
		out.swap(_completions);
		return out;
	}
	bool idle() const override { return _pending == 0; }

  private:
	struct Lease
	{
		std::uint64_t account = 0;
		std::uint64_t character = 0;
		std::string token;
	};
	Settings _settings;
	std::unique_ptr<sql::Connection> _db;
	redisContext *_redis = nullptr;
	std::map<std::uint64_t, Lease> _leases;
	std::mutex _mutex;
	std::condition_variable _cv;
	std::deque<Request> _requests;
	std::vector<Completion> _completions;
	std::thread _worker;
	std::atomic<std::size_t> _pending{0};
	bool _stopping = false;
	Mono::time_point _heartbeat = Mono::now();
	Statement prepare(const std::string &sql) { return Statement(_db->prepareStatement(sql)); }
	void execute(const std::string &sql)
	{
		std::unique_ptr<sql::Statement> statement(_db->createStatement());
		(void)statement->execute(sql);
	}
	std::uint64_t insertedId()
	{
		auto statement = prepare("SELECT LAST_INSERT_ID()");
		Rows rows(statement->executeQuery());
		if (!rows->next())
			throw std::runtime_error("missing inserted id");
		return rows->getUInt64(1);
	}
	void connect()
	{
		sql::ConnectOptionsMap options;
		options["hostName"] = "tcp://" + _settings.mysql_host + ":" + std::to_string(_settings.mysql_port);
		options["userName"] = _settings.mysql_user;
		options["password"] = _settings.mysql_password;
		options["schema"] = std::string("sa_session");
		options[OPT_SSL_MODE] = static_cast<int>(sql::SSL_MODE_VERIFY_IDENTITY);
		options["sslCA"] = _settings.mysql_ca;
		options[OPT_CONNECT_TIMEOUT] = 3;
		options[OPT_READ_TIMEOUT] = 3;
		options[OPT_WRITE_TIMEOUT] = 3;
		_db.reset(sql::mysql::get_mysql_driver_instance()->connect(options));
		_db->setTransactionIsolation(sql::TRANSACTION_REPEATABLE_READ);
		execute("SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci");
		auto version = prepare("SELECT VERSION(), (SELECT MAX(version) FROM schema_migration)");
		Rows rows(version->executeQuery());
		if (!rows->next() || rows->getString(1).asStdString().rfind("8.4.8", 0) != 0 || rows->getInt(2) != 1)
			throw std::runtime_error("wrong database/schema version");
		auto ssl = prepare("SHOW SESSION STATUS LIKE 'Ssl_cipher'");
		Rows tls(ssl->executeQuery());
		if (!tls->next() || tls->getString(2).length() == 0)
			throw std::runtime_error("database TLS missing");
		if (_redis)
			redisFree(_redis);
		const timeval timeout{3, 0};
		_redis = redisConnectWithTimeout(_settings.redis_host.c_str(), _settings.redis_port, timeout);
		if (!_redis || _redis->err)
			throw std::runtime_error("Redis connection failed");
		if (redisSetTimeout(_redis, timeout) != REDIS_OK)
			throw std::runtime_error("Redis timeout failed");
		Reply auth(static_cast<redisReply *>(redisCommand(_redis, "AUTH %b", _settings.redis_password.data(), _settings.redis_password.size())), freeReplyObject);
		if (!auth || auth->type != REDIS_REPLY_STATUS)
			throw std::runtime_error("Redis authentication failed");
		Reply info(static_cast<redisReply *>(redisCommand(_redis, "INFO server")), freeReplyObject);
		if (!info || info->type != REDIS_REPLY_STRING || std::string(info->str, info->len).find("redis_version:8.6.2\r\n") == std::string::npos)
			throw std::runtime_error("wrong Redis version");
	}
	std::string key(const Lease &lease) const { return "sa:session:" + std::to_string(lease.account); }
	bool redisOwned(const Lease &lease)
	{
		const auto name = key(lease);
		Reply reply(static_cast<redisReply *>(redisCommand(_redis, "GET %s", name.c_str())), freeReplyObject);
		if (!reply || reply->type == REDIS_REPLY_ERROR)
			throw std::runtime_error("Redis read failed");
		return reply->type == REDIS_REPLY_STRING && std::string(reply->str, reply->len) == lease.token;
	}
	bool redisLease(const Lease &lease, bool release)
	{
		const char *script = release ? "if redis.call('get',KEYS[1])==ARGV[1] then return redis.call('del',KEYS[1]) else return 0 end"
		                             : "if redis.call('get',KEYS[1])==ARGV[1] then return redis.call('expire',KEYS[1],ARGV[2]) else return 0 end";
		Reply reply(static_cast<redisReply *>(redisCommand(_redis, "EVAL %s 1 %s %s %d", script, key(lease).c_str(), lease.token.c_str(), kLeaseSeconds)), freeReplyObject);
		if (!reply || reply->type != REDIS_REPLY_INTEGER)
			throw std::runtime_error("Redis lease failed");
		return reply->integer == 1;
	}
	bool fence(const Lease &lease)
	{
		if (!redisOwned(lease))
			return false;
		auto statement = prepare("SELECT token, lease_until > CURRENT_TIMESTAMP(6) FROM account_session WHERE account_id=? FOR UPDATE");
		statement->setUInt64(1, lease.account);
		Rows rows(statement->executeQuery());
		return rows->next() && rows->getString(1).asStdString() == lease.token && rows->getBoolean(2);
	}
	void clearFence(const Lease &lease)
	{
		auto statement = prepare("UPDATE account_session SET token='', char_id=NULL, lease_until=NULL WHERE account_id=? AND token=?");
		statement->setUInt64(1, lease.account);
		statement->setString(2, lease.token);
		(void)statement->executeUpdate();
	}
	Completion login(const Request &request, Completion out)
	{
		const auto login_name = normalizedLogin(request.login.login);
		const std::string password(request.login.password.data, request.login.password.size());
		if (login_name.empty() || password.size() < 12 || password.size() > 127)
		{
			out.code = Code::ACCOUNT_INVALID;
			return out;
		}
		if (_leases.count(request.session))
		{
			out.code = Code::ACCOUNT_BUSY;
			return out;
		}
		if (request.login.register_account)
		{
			const auto salt = randomToken();
			auto statement = prepare("INSERT INTO account(login,password_salt,password_hash,password_iterations) VALUES(?,?,?,?)");
			statement->setString(1, login_name);
			statement->setString(2, salt);
			statement->setString(3, passwordHash(password, salt, kIterations));
			statement->setInt(4, kIterations);
			(void)statement->executeUpdate();
		}
		auto statement = prepare("SELECT account_id,password_salt,password_hash,password_iterations,status FROM account WHERE login=?");
		statement->setString(1, login_name);
		Rows rows(statement->executeQuery());
		if (!rows->next())
		{
			out.code = Code::ACCOUNT_CREDENTIALS;
			return out;
		}
		const auto expected = rows->getString(3).asStdString();
		const auto actual = passwordHash(password, rows->getString(2).asStdString(), rows->getInt(4));
		if (expected.size() != actual.size() || CRYPTO_memcmp(expected.data(), actual.data(), actual.size()) != 0 || rows->getInt(5) != 1)
		{
			out.code = Code::ACCOUNT_CREDENTIALS;
			return out;
		}
		Lease lease{rows->getUInt64(1), 0, randomToken()};
		Reply acquired(static_cast<redisReply *>(redisCommand(_redis, "SET %s %s NX EX %d", key(lease).c_str(), lease.token.c_str(), kLeaseSeconds)), freeReplyObject);
		if (!acquired || acquired->type == REDIS_REPLY_ERROR)
			throw std::runtime_error("Redis acquisition failed");
		if (acquired->type == REDIS_REPLY_NIL)
		{
			out.code = Code::ACCOUNT_BUSY;
			return out;
		}
		try
		{
			_db->setAutoCommit(false);
			auto claim = prepare("INSERT INTO account_session(account_id,generation,token,lease_until) VALUES(?,1,?,CURRENT_TIMESTAMP(6)+INTERVAL 30 SECOND) ON DUPLICATE KEY UPDATE generation=generation+1,token=VALUES(token),char_id=NULL,lease_until=VALUES(lease_until)");
			claim->setUInt64(1, lease.account);
			claim->setString(2, lease.token);
			(void)claim->executeUpdate();
			auto list = prepare("SELECT c.char_id,c.name,d.level,JSON_UNQUOTE(JSON_EXTRACT(d.payload,'$.image')) FROM `character` c JOIN character_data d ON c.char_id=d.char_id WHERE c.account_id=? ORDER BY c.slot");
			list->setUInt64(1, lease.account);
			Rows characters(list->executeQuery());
			while (characters->next())
			{
				SA::Transport::CharacterSummary value{};
				value.char_id = characters->getUInt64(1);
				const auto name = characters->getString(2).asStdString();
				if (!value.name.assign(name.data(), name.size()))
					throw std::runtime_error("invalid character name");
				value.level = characters->getInt(3);
				value.image = characters->getInt(4);
				if (!out.characters.push_back(value))
					throw std::runtime_error("too many character slots");
			}
			_db->commit();
			_db->setAutoCommit(true);
			_leases.emplace(request.session, lease);
			out.code = Code::ACCOUNT_OK;
		}
		catch (...)
		{
			_db->rollback();
			_db->setAutoCommit(true);
			(void)redisLease(lease, true);
			throw;
		}
		return out;
	}

	template <typename Collection>
	void writeAssets(const char *table, std::uint64_t character, Collection &values)
	{
		// Validate ownership before replacing rows. An old UID cannot be reassigned across characters.
		std::set<std::uint64_t> owned;
		auto previous = prepare(std::string("SELECT uid FROM ") + table + " WHERE char_id=? FOR UPDATE");
		previous->setUInt64(1, character);
		Rows rows(previous->executeQuery());
		while (rows->next())
			owned.insert(rows->getUInt64(1));
		for (const auto &value : values)
			if (value.uid && !owned.count(value.uid))
				throw std::invalid_argument("foreign asset id");
		auto erase = prepare(std::string("DELETE FROM ") + table + " WHERE char_id=?");
		erase->setUInt64(1, character);
		(void)erase->executeUpdate();
		for (auto &value : values)
		{
			auto insert = prepare(std::string("INSERT INTO ") + table + "(uid,char_id,slot,schema_ver,payload) VALUES(?,?,?,1,?)");
			if (value.uid)
				insert->setUInt64(1, value.uid);
			else
				insert->setNull(1, sql::DataType::BIGINT);
			insert->setUInt64(2, character);
			insert->setUInt(3, value.slot);
			insert->setString(4, payload(value.value));
			(void)insert->executeUpdate();
			if (!value.uid)
				value.uid = insertedId();
		}
	}
	template <typename Data, typename Collection>
	void readAssets(const char *table, std::uint64_t character, Collection &values)
	{
		auto statement = prepare(std::string("SELECT uid,slot,schema_ver,payload FROM ") + table + " WHERE char_id=? ORDER BY slot");
		statement->setUInt64(1, character);
		Rows rows(statement->executeQuery());
		while (rows->next())
		{
			auto *value = values.push_back();
			if (!value || rows->getInt(3) != 1)
				throw std::invalid_argument("invalid asset schema");
			value->uid = rows->getUInt64(1);
			value->slot = rows->getUInt(2);
			value->value = recordFrom<Data>(rows->getString(4));
		}
	}
	Completion perform(const Request &request)
	{
		Completion out{};
		out.operation = request.operation;
		out.session = request.session;
		out.correlation = request.correlation;
		out.logout = request.logout;
		if (request.operation == Operation::kLogin)
			return login(request, out);
		auto found = _leases.find(request.session);
		if (found == _leases.end())
		{
			out.code = Code::ACCOUNT_LEASE_LOST;
			return out;
		}
		auto &lease = found->second;
		_db->setAutoCommit(false);
		if (!fence(lease))
		{
			_db->rollback();
			_db->setAutoCommit(true);
			out.code = Code::ACCOUNT_LEASE_LOST;
			return out;
		}
		out.character = request.character;
		auto &record = out.character;
		if (request.operation == Operation::kCreate)
		{
			if (!validRecord(record) || lease.character || record.char_id || record.revision)
				throw std::invalid_argument("invalid new character");
			auto slots = prepare("SELECT slot FROM `character` WHERE account_id=? FOR UPDATE");
			slots->setUInt64(1, lease.account);
			Rows rows(slots->executeQuery());
			std::set<int> occupied;
			while (rows->next())
				occupied.insert(rows->getInt(1));
			const int slot = !occupied.count(0) ? 0 : !occupied.count(1) ? 1
			                                                             : 2;
			if (slot == 2)
			{
				_db->rollback();
				_db->setAutoCommit(true);
				out.code = Code::ACCOUNT_CONFLICT;
				return out;
			}
			auto insert = prepare("INSERT INTO `character`(account_id,slot,name) VALUES(?,?,?)");
			insert->setUInt64(1, lease.account);
			insert->setInt(2, slot);
			insert->setString(3, std::string(record.player.name.data, record.player.name.size()));
			(void)insert->executeUpdate();
			record.char_id = insertedId();
			auto data = prepare("INSERT INTO character_data(char_id,schema_ver,revision,payload) VALUES(?,1,0,?)");
			data->setUInt64(1, record.char_id);
			data->setString(2, payload(record.player));
			(void)data->executeUpdate();
			writeAssets("character_pet", record.char_id, record.pets);
			writeAssets("character_item", record.char_id, record.items);
		}
		else if (request.operation == Operation::kSelect)
		{
			if (lease.character)
				throw std::invalid_argument("character already selected");
			auto statement = prepare("SELECT d.schema_ver,d.revision,d.payload FROM character_data d JOIN `character` c ON c.char_id=d.char_id WHERE c.account_id=? AND c.char_id=?");
			statement->setUInt64(1, lease.account);
			statement->setUInt64(2, record.char_id);
			Rows rows(statement->executeQuery());
			if (!rows->next())
			{
				_db->rollback();
				_db->setAutoCommit(true);
				out.code = Code::ACCOUNT_NOT_FOUND;
				return out;
			}
			record.schema_ver = rows->getUInt(1);
			record.revision = rows->getUInt64(2);
			record.player = recordFrom<SA::Domain::PlayerData>(rows->getString(3));
			readAssets<SA::Domain::PetData>("character_pet", record.char_id, record.pets);
			readAssets<SA::Domain::ItemData>("character_item", record.char_id, record.items);
			if (!validRecord(record))
				throw std::invalid_argument("invalid stored record");
		}
		else if (request.operation == Operation::kSave)
		{
			if (!validRecord(record) || !record.char_id || record.char_id != lease.character)
				throw std::invalid_argument("invalid save ownership");
			auto statement = prepare("UPDATE character_data SET payload=?,revision=revision+1 WHERE char_id=? AND schema_ver=1 AND revision=?");
			statement->setString(1, payload(record.player));
			statement->setUInt64(2, record.char_id);
			statement->setUInt64(3, record.revision);
			if (statement->executeUpdate() != 1)
			{
				_db->rollback();
				_db->setAutoCommit(true);
				out.code = Code::ACCOUNT_CONFLICT;
				return out;
			}
			writeAssets("character_pet", record.char_id, record.pets);
			writeAssets("character_item", record.char_id, record.items);
			++record.revision;
		}
		if (request.operation == Operation::kSelect || request.operation == Operation::kCreate)
		{
			auto update = prepare("UPDATE account_session SET char_id=? WHERE account_id=? AND token=?");
			update->setUInt64(1, record.char_id);
			update->setUInt64(2, lease.account);
			update->setString(3, lease.token);
			(void)update->executeUpdate();
		}
		const bool release = request.logout || request.operation == Operation::kRelease;
		if (release)
			clearFence(lease);
		_db->commit();
		_db->setAutoCommit(true);
		if (request.operation == Operation::kSelect || request.operation == Operation::kCreate)
			lease.character = record.char_id;
		out.code = Code::ACCOUNT_OK;
		if (release)
		{
			// A Redis outage after COMMIT may delay re-login until TTL, but never undo a durable save.
			try
			{
				(void)redisLease(lease, true);
			}
			catch (const std::exception &)
			{
			}
			_leases.erase(found);
		}
		return out;
	}
	void complete(Completion out)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_completions.push_back(std::move(out));
	}
	void refresh()
	{
		if (Mono::now() - _heartbeat < std::chrono::seconds(5))
			return;
		_heartbeat = Mono::now();
		for (auto it = _leases.begin(); it != _leases.end();)
		{
			bool owned = false;
			try
			{
				owned = redisLease(it->second, false);
				if (owned)
				{
					auto statement = prepare("UPDATE account_session SET lease_until=CURRENT_TIMESTAMP(6)+INTERVAL 30 SECOND WHERE account_id=? AND token=? AND lease_until>CURRENT_TIMESTAMP(6)");
					statement->setUInt64(1, it->second.account);
					statement->setString(2, it->second.token);
					owned = statement->executeUpdate() == 1;
				}
			}
			catch (const std::exception &)
			{
				owned = false;
			}
			if (owned)
			{
				++it;
				continue;
			}
			Completion out{};
			out.operation = Operation::kLeaseLost;
			out.session = it->first;
			out.code = Code::ACCOUNT_LEASE_LOST;
			complete(out);
			it = _leases.erase(it);
		}
	}
	void run()
	{
		for (;;)
		{
			Request request;
			{
				std::unique_lock<std::mutex> lock(_mutex);
				_cv.wait_for(lock, std::chrono::seconds(1), [&]
				             { return _stopping || !_requests.empty(); });
				if (_requests.empty())
				{
					if (_stopping)
						break;
					lock.unlock();
					refresh();
					continue;
				}
				request = std::move(_requests.front());
				_requests.pop_front();
			}
			Completion out{};
			out.operation = request.operation;
			out.session = request.session;
			out.correlation = request.correlation;
			out.logout = request.logout;
			try
			{
				refresh();
				out = perform(request);
			}
			catch (const sql::SQLException &exception)
			{
				out.code = exception.getErrorCode() == 1062 ? Code::ACCOUNT_CONFLICT : Code::ACCOUNT_UNAVAILABLE;
			}
			catch (const std::invalid_argument &)
			{
				out.code = Code::ACCOUNT_INVALID;
			}
			catch (const std::exception &)
			{
				out.code = Code::ACCOUNT_UNAVAILABLE;
			}
			if (out.code != Code::ACCOUNT_OK)
			{
				try
				{
					_db->rollback();
					_db->setAutoCommit(true);
				}
				catch (const std::exception &)
				{
				}
			}
			OPENSSL_cleanse(request.login.password.data, sizeof(request.login.password.data));
			complete(std::move(out));
			--_pending;
		}
		// Unselected accounts can be released immediately. Selected accounts require an explicit save.
		for (const auto &entry : _leases)
			if (!entry.second.character)
				try
				{
					clearFence(entry.second);
					(void)redisLease(entry.second, true);
				}
				catch (const std::exception &)
				{
				}
	}
};
} // namespace

std::unique_ptr<Service> open(const Settings &settings, std::string &error)
{
	if (settings.mysql_ca.empty() || settings.mysql_password.empty() || settings.redis_password.empty())
	{
		error = "MySQL CA and database/Redis credentials are required";
		return nullptr;
	}
	auto service = std::make_unique<MySqlService>(settings);
	if (!service->start(error))
		return nullptr;
	return service;
}
} // namespace SA::SessionStorage
