#include "data/Json.h"
#include "session_storage/Api.h"
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace SA::SessionStorage
{
bool loadSettingsFile(const std::string &path, Settings &settings, std::string &error)
{
	try
	{
		std::ifstream input(path, std::ios::binary);
		if (!input)
			throw std::invalid_argument("storage settings file unavailable");
		const std::string text((std::istreambuf_iterator<char>(input)), {});
		if (text.size() > 65536)
			throw std::invalid_argument("storage settings too large");
		const auto parsed = SA::Data::Json::parse(text);
		if (!parsed.ok || !parsed.value.isObject())
			throw std::invalid_argument("invalid storage settings JSON");
		const auto str = [&](const char *name)
		{
			const auto *v = parsed.value.find(name);
			if (!v || !v->isString() || v->asString().empty())
				throw std::invalid_argument(std::string("missing setting: ") + name);
			return v->asString();
		};
		const auto port = [&](const char *name)
		{
			const auto *v = parsed.value.find(name);
			if (!v || !v->isNumber() || v->asNumber() < 1 || v->asNumber() > 65535 || std::trunc(v->asNumber()) != v->asNumber())
				throw std::invalid_argument(std::string("invalid port: ") + name);
			return static_cast<int>(v->asNumber());
		};
		settings.mysql_host = str("mysql_host");
		settings.mysql_port = port("mysql_port");
		settings.mysql_user = str("mysql_user");
		settings.mysql_password = str("mysql_password");
		settings.mysql_ca = str("mysql_ca");
		settings.redis_host = str("redis_host");
		settings.redis_port = port("redis_port");
		settings.redis_password = str("redis_password");
		return true;
	}
	catch (const std::exception &exception)
	{
		error = exception.what();
		return false;
	}
}

bool validRecord(const SA::Domain::CharacterRecord &record)
{
	const auto &p = record.player;
	if (record.schema_ver != 1 || p.name.empty() || p.name.size() > 31 || p.dir > 7 ||
	    p.level < 1 || p.level > 200 || p.exp < 0 || p.hp < 0 || p.hp > 100000000 ||
	    p.mp < 0 || p.mp > p.max_mp || p.max_mp > 1000000 ||
	    p.default_pet < -1 || p.default_pet > 4 || p.capture_count < 0 ||
	    p.vital < 0 || p.str < 0 || p.tough < 0 || p.dex < 0 ||
	    p.vital > 10000000 || p.str > 10000000 || p.tough > 10000000 || p.dex > 10000000 ||
	    record.pets.size() > 5 || record.items.size() > 54)
		return false;
	std::set<std::uint32_t> slots;
	std::set<std::uint64_t> ids;
	for (const auto &pet : record.pets)
	{
		if (pet.slot >= 5 || !slots.insert(pet.slot).second ||
		    (pet.uid && !ids.insert(pet.uid).second) || pet.value.level < 1 || pet.value.hp < 0 ||
		    pet.value.growth_vital < 0 || pet.value.growth_vital > 255 ||
		    pet.value.growth_str < 0 || pet.value.growth_str > 255 ||
		    pet.value.growth_tough < 0 || pet.value.growth_tough > 255 ||
		    pet.value.growth_dex < 0 || pet.value.growth_dex > 255)
			return false;
	}
	if (p.default_pet >= 0 && !slots.count(static_cast<std::uint32_t>(p.default_pet)))
		return false;
	slots.clear();
	ids.clear();
	for (const auto &item : record.items)
		if (item.slot >= 54 || !slots.insert(item.slot).second ||
		    (item.uid && !ids.insert(item.uid).second) || item.value.item_id <= 0 || item.value.current_pile < 0)
			return false;
	return true;
}
} // namespace SA::SessionStorage
