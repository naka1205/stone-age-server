// src/world/WorldWeather.cpp —— 大世界动态气候环境与天气调度器实现

#include "WorldWeather.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace SA::World
{

void WorldWeather::setWeather(std::uint32_t floor_id, WeatherKind kind, std::int32_t level,
                              std::int64_t now_ms, std::int64_t duration_ms,
                              std::string_view option)
{
	if (kind == WeatherKind::kNone || level <= 0)
	{
		clearWeather(floor_id);
		return;
	}

	const std::int32_t clamped_level = std::clamp(level, 1, 5);

	MapWeather weather;
	weather.floor_id = floor_id;
	weather.kind = kind;
	weather.level = clamped_level;
	weather.start_time_ms = now_ms;
	weather.duration_ms = std::max<std::int64_t>(0, duration_ms);
	weather.option = std::string(option);

	map_weathers_[floor_id] = std::move(weather);
}

void WorldWeather::clearWeather(std::uint32_t floor_id)
{
	map_weathers_.erase(floor_id);
}

MapWeather WorldWeather::getWeather(std::uint32_t floor_id) const
{
	auto it = map_weathers_.find(floor_id);
	if (it != map_weathers_.end())
	{
		return it->second;
	}
	MapWeather clear_weather;
	clear_weather.floor_id = floor_id;
	clear_weather.kind = WeatherKind::kNone;
	clear_weather.level = 0;
	return clear_weather;
}

bool WorldWeather::isClear(std::uint32_t floor_id) const
{
	auto it = map_weathers_.find(floor_id);
	if (it == map_weathers_.end())
	{
		return true;
	}
	return it->second.kind == WeatherKind::kNone || it->second.level <= 0;
}

void WorldWeather::tick(std::int64_t now_ms, const WeatherChangeCallback &on_change)
{
	std::vector<std::uint32_t> expired_floors;

	for (const auto &[floor_id, weather] : map_weathers_)
	{
		if (weather.isExpired(now_ms))
		{
			expired_floors.push_back(floor_id);
		}
	}

	for (const std::uint32_t floor_id : expired_floors)
	{
		map_weathers_.erase(floor_id);
		if (on_change)
		{
			MapWeather clear_weather;
			clear_weather.floor_id = floor_id;
			clear_weather.kind = WeatherKind::kNone;
			clear_weather.level = 0;
			on_change(floor_id, clear_weather);
		}
	}
}

std::string WorldWeather::formatEffectPacket(WeatherKind kind, std::int32_t level,
                                             std::string_view option)
{
	std::ostringstream oss;
	oss << "EF " << static_cast<std::uint32_t>(kind) << " " << level;
	if (!option.empty())
	{
		oss << " " << option;
	}
	return oss.str();
}

bool WorldWeather::parseEffectPacket(std::string_view packet, WeatherKind &out_kind,
                                     std::int32_t &out_level, std::string &out_option)
{
	if (packet.size() < 2 || packet.substr(0, 2) != "EF")
	{
		return false;
	}

	std::string content(packet.substr(2));
	// 移除可能的前置空格
	std::size_t start = content.find_first_not_of(" \t");
	if (start == std::string::npos)
	{
		out_kind = WeatherKind::kNone;
		out_level = 0;
		out_option.clear();
		return true;
	}

	std::istringstream iss(content.substr(start));
	std::uint32_t raw_kind = 0;
	std::int32_t lvl = 0;

	if (!(iss >> raw_kind))
	{
		return false;
	}
	if (!(iss >> lvl))
	{
		lvl = 0;
	}

	out_kind = static_cast<WeatherKind>(raw_kind);
	out_level = lvl;

	std::string opt;
	if (iss >> opt)
	{
		out_option = std::move(opt);
	}
	else
	{
		out_option.clear();
	}

	return true;
}

std::string formatWeatherPacket(WeatherKind kind, std::int32_t level,
                                std::string_view option)
{
	return WorldWeather::formatEffectPacket(kind, level, option);
}

bool parseWeatherPacket(std::string_view packet, WeatherKind &out_kind,
                        std::int32_t &out_level, std::string &out_option)
{
	return WorldWeather::parseEffectPacket(packet, out_kind, out_level, out_option);
}

} // namespace SA::World
