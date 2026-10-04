// src/world/WorldWeather.h —— 大世界动态气候环境与天气调度器
//
// 依据 00 §3.1 / 10-world-map.md / 02-protocol.md 规范与原版 char.c / map.h。
// 严禁泄露至 include/world/。

#ifndef __SA_WORLD_WORLDWEATHER_H__
#define __SA_WORLD_WORLDWEATHER_H__

#include "world/Api.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace SA::World
{

// 天气变动回调通知：(floor_id, new_weather)
using WeatherChangeCallback = std::function<void(std::uint32_t floor_id, const MapWeather &weather)>;

// 大世界天气调度与状态机
class WorldWeather
{
  public:
	WorldWeather() = default;

	// 设置指定地图/楼层的天气
	// level 会自动钳位到 [1, 5]；若 kind == kNone 或 level == 0，则清除天气
	void setWeather(std::uint32_t floor_id, WeatherKind kind, std::int32_t level,
	                std::int64_t now_ms, std::int64_t duration_ms = 0,
	                std::string_view option = "");

	// 清除指定地图的天气（恢复默认晴朗）
	void clearWeather(std::uint32_t floor_id);

	// 获取指定地图的当前天气状态
	[[nodiscard]] MapWeather getWeather(std::uint32_t floor_id) const;

	// 是否处于晴朗/无天气状态
	[[nodiscard]] bool isClear(std::uint32_t floor_id) const;

	// 周期驱动天气时钟，扫描已超时的限时天气并触发状态刷新
	void tick(std::int64_t now_ms, const WeatherChangeCallback &on_change = nullptr);

	// 格式化 8.0 EF 协议包（对齐原版 lssproto_EF_send）
	// 格式："EF <effect_mask> <level> <option>"
	[[nodiscard]] static std::string formatEffectPacket(WeatherKind kind, std::int32_t level,
	                                                    std::string_view option = "");

	// 解码/校验 EF 协议包
	static bool parseEffectPacket(std::string_view packet, WeatherKind &out_kind,
	                              std::int32_t &out_level, std::string &out_option);

  private:
	std::unordered_map<std::uint32_t, MapWeather> map_weathers_;
};

} // namespace SA::World

#endif // __SA_WORLD_WORLDWEATHER_H__
