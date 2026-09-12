#ifndef __SA_SessionStorageApi_H__
#define __SA_SessionStorageApi_H__
#include "domain/character_data.sa.h"
#include "transport/login.sa.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace SA::SessionStorage
{
struct Settings
{
	std::string mysql_host = "127.0.0.1";
	int mysql_port = 13306;
	std::string mysql_user = "stoneage";
	std::string mysql_password;
	std::string mysql_ca;
	std::string redis_host = "127.0.0.1";
	int redis_port = 16379;
	std::string redis_password;
};
enum class Operation
{
	kLogin,
	kCreate,
	kSelect,
	kSave,
	kRelease,
	kLeaseLost
};
struct Request
{
	Operation operation = Operation::kLogin;
	std::uint64_t session = 0;
	std::uint64_t correlation = 0;
	SA::Transport::LoginRequest login{};
	SA::Domain::CharacterRecord character{};
	bool logout = false;
};
struct Completion
{
	Operation operation = Operation::kLogin;
	std::uint64_t session = 0;
	std::uint64_t correlation = 0;
	SA::Transport::AccountCode code = SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE;
	SA::Domain::CharacterRecord character{};
	SA::IDL::FixedVec<SA::Transport::CharacterSummary, 2> characters{};
	bool logout = false;
};
class Service
{
  public:
	virtual ~Service() = default;
	// Copies immutable requests; never waits for database or Redis I/O.
	virtual bool submit(Request request) = 0;
	virtual std::vector<Completion> poll() = 0;
	virtual bool idle() const = 0;
};
// Called before the tick loop. Startup validates MySQL, TLS, Redis and schema version.
std::unique_ptr<Service> open(const Settings &settings, std::string &error);
bool loadSettingsFile(const std::string &path, Settings &settings, std::string &error);
bool validRecord(const SA::Domain::CharacterRecord &record);
} // namespace SA::SessionStorage
#endif
