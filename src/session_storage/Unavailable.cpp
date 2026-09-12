#include "session_storage/Api.h"
namespace SA::SessionStorage
{
std::unique_ptr<Service> open(const Settings &, std::string &error)
{
	error = "Playable mode requires a build with SA_ENABLE_MYSQL_STORAGE=ON";
	return nullptr;
}
} // namespace SA::SessionStorage
