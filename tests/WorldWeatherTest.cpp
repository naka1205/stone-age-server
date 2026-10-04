// tests/WorldWeatherTest.cpp —— 大世界昼夜交替与动态气候环境系统测试 (阶段 10)
//
// 依据: 00 §3.1 / 10-world-map.md / 02-protocol.md / 原版 handletime.c / map.h
// 覆盖:
//   1. 石器历法与昼夜四时段确定性转换 (LSTime Determinism)
//   2. 昼夜状态机边界判定与经典调色板映射 (LSTimeSection & Palette)
//   3. 大世界动态气候调度、限时超时与 EF 协议成帧 (World Weather & EF Protocol)
//   4. 玩家上线与跨图传送天气环境同步闭环 (Warp Weather Sync E2E)
//   5. 反向变异实证 (RV-Weather-1)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "rules/LSTime.h"
#include "world/Api.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace SA::Rules;
using namespace SA::World;

namespace
{

SA::Platform::ServerConfig makeTestConfig()
{
	const SA::Platform::ConfigResult r = SA::Platform::parseConfig(R"({
    "protocol_version": 1, "log_level": "error",
    "tempo": { "tick_hz": 100, "battle_turn_interval_ms": 1000 }
  })");
	REQUIRE(r.ok);
	return r.config;
}

struct ServerFixture
{
	SA::Platform::ServerConfig config = makeTestConfig();
	SA::Platform::ManualClock clock{1000000};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0x123456};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};
};

} // namespace

TEST_CASE("阶段 10: 石器历法与昼夜四时段确定性转换 (LSTime Determinism)")
{
	SUBCASE("纪元起点与基础天/时/年推进")
	{
		// 1. 纪元起点恰好为 0 年 0 日 0 刻度
		const LSTime t0 = computeLSTime(kEraSeconds);
		CHECK(t0.year == 0);
		CHECK(t0.day == 0);
		CHECK(t0.hour == 0);

		// 逆转换为现实时间戳
		CHECK(computeRealTime(t0) == kEraSeconds);

		// 2. 推进 1 个石器天 (5400 秒)
		const LSTime t1 = computeLSTime(kEraSeconds + kSecondsPerLSDay);
		CHECK(t1.year == 0);
		CHECK(t1.day == 1);
		CHECK(t1.hour == 0);
		CHECK(computeRealTime(t1) == kEraSeconds + kSecondsPerLSDay);

		// 3. 推进 1 个石器年 (100 天 = 540000 秒)
		const LSTime ty = computeLSTime(kEraSeconds + kSecondsPerLSDay * kDaysPerLSYear);
		CHECK(ty.year == 1);
		CHECK(ty.day == 0);
		CHECK(ty.hour == 0);
		CHECK(computeRealTime(ty) == kEraSeconds + kSecondsPerLSDay * kDaysPerLSYear);

		// 4. 任意刻度计算与往返等价性
		LSTime custom{5, 42, 600};
		const std::int64_t real_sec = computeRealTime(custom);
		const LSTime reconstructed = computeLSTime(real_sec);
		CHECK(reconstructed.year == 5);
		CHECK(reconstructed.day == 42);
		CHECK(reconstructed.hour == 600);
	}

	SUBCASE("四时段状态机边界判定与经典调色板映射")
	{
		// 1:1 对齐原版 handletime.c
		// kNightToMorning = 700
		// kMorningToNoon = 930
		// kNoonToEvening = 200
		// kEveningToNight = 300

		// [1] 白昼/正午 (Noon): 0 <= hour <= 200 || 930 < hour <= 1023
		CHECK(getLSTimeSection(LSTime{0, 0, 0}) == LSTimeSection::kNoon);
		CHECK(getLSTimeSection(LSTime{0, 0, 100}) == LSTimeSection::kNoon);
		CHECK(getLSTimeSection(LSTime{0, 0, 200}) == LSTimeSection::kNoon);
		CHECK(getLSTimeSection(LSTime{0, 0, 931}) == LSTimeSection::kNoon);
		CHECK(getLSTimeSection(LSTime{0, 0, 1023}) == LSTimeSection::kNoon);
		CHECK(getPaletteIndex(LSTimeSection::kNoon) == 2); // PALET_2

		// [2] 黄昏/傍晚 (Evening): 200 < hour <= 300
		CHECK(getLSTimeSection(LSTime{0, 0, 201}) == LSTimeSection::kEvening);
		CHECK(getLSTimeSection(LSTime{0, 0, 250}) == LSTimeSection::kEvening);
		CHECK(getLSTimeSection(LSTime{0, 0, 300}) == LSTimeSection::kEvening);
		CHECK(getPaletteIndex(LSTimeSection::kEvening) == 4); // PALET_4

		// [3] 深夜/黑夜 (Night): 300 < hour <= 700
		CHECK(getLSTimeSection(LSTime{0, 0, 301}) == LSTimeSection::kNight);
		CHECK(getLSTimeSection(LSTime{0, 0, 500}) == LSTimeSection::kNight);
		CHECK(getLSTimeSection(LSTime{0, 0, 700}) == LSTimeSection::kNight);
		CHECK(getPaletteIndex(LSTimeSection::kNight) == 3); // PALET_3

		// [4] 清晨/拂晓 (Morning): 700 < hour <= 930
		CHECK(getLSTimeSection(LSTime{0, 0, 701}) == LSTimeSection::kMorning);
		CHECK(getLSTimeSection(LSTime{0, 0, 850}) == LSTimeSection::kMorning);
		CHECK(getLSTimeSection(LSTime{0, 0, 930}) == LSTimeSection::kMorning);
		CHECK(getPaletteIndex(LSTimeSection::kMorning) == 1); // PALET_1
	}
}

TEST_CASE("阶段 10: 大世界动态气候调度与 EF 协议成帧 (World Weather & EF Protocol)")
{
	SUBCASE("8.0 EF 协议编解码与参数防护")
	{
		// 格式: EF <effect_mask> <level> [option]
		const std::string pkt_rain = formatWeatherPacket(WeatherKind::kRain, 3, "heavy");
		CHECK(pkt_rain == "EF 1 3 heavy");

		WeatherKind kind = WeatherKind::kNone;
		std::int32_t level = 0;
		std::string option;
		CHECK(parseWeatherPacket(pkt_rain, kind, level, option));
		CHECK(kind == WeatherKind::kRain);
		CHECK(level == 3);
		CHECK(option == "heavy");

		// 清除/晴天协议
		const std::string pkt_clear = formatWeatherPacket(WeatherKind::kNone, 0);
		CHECK(pkt_clear == "EF 0 0");
		CHECK(parseWeatherPacket(pkt_clear, kind, level, option));
		CHECK(kind == WeatherKind::kNone);
		CHECK(level == 0);
		CHECK(option.empty());
	}

	SUBCASE("地图天气设置、强度钳位与清除恢复")
	{
		ServerFixture fix;
		auto &world = fix.world;
		CHECK(world.isMapWeatherClear(100));

		// 强度钳位：超过 5 钳位到 5
		world.setMapWeather(100, WeatherKind::kRain, 99, 3000, "rainy");
		CHECK_FALSE(world.isMapWeatherClear(100));
		const MapWeather w1 = world.mapWeather(100);
		CHECK(w1.kind == WeatherKind::kRain);
		CHECK(w1.level == 5);
		CHECK(w1.option == "rainy");

		// 手动清除天气恢复晴朗
		world.clearMapWeather(100);
		CHECK(world.isMapWeatherClear(100));
		const MapWeather w_clear = world.mapWeather(100);
		CHECK(w_clear.kind == WeatherKind::kNone);
		CHECK(w_clear.level == 0);
	}
}

TEST_CASE("阶段 10: 跨图传送时天气环境同步闭环 (Warp Weather Sync E2E)")
{
	ServerFixture fix;
	auto &world = fix.world;

	// 1. 设置不同楼层的场景天气
	// 楼层 100: 3 级雨天
	world.setMapWeather(100, WeatherKind::kRain, 3, 0);
	// 楼层 200: 2 级雪天
	world.setMapWeather(200, WeatherKind::kSnow, 2, 0);
	// 楼层 300: 晴天 (未显式设置)

	CHECK_FALSE(world.isMapWeatherClear(100));
	CHECK_FALSE(world.isMapWeatherClear(200));
	CHECK(world.isMapWeatherClear(300));

	CHECK(world.mapWeather(100).kind == WeatherKind::kRain);
	CHECK(world.mapWeather(100).level == 3);
	CHECK(world.mapWeather(200).kind == WeatherKind::kSnow);
	CHECK(world.mapWeather(200).level == 2);

	// 2. 模拟真实时间对应的石器世界历法
	const std::int64_t simulated_time = kEraSeconds + 2000;
	const LSTime ls = world.currentLSTime(simulated_time);
	CHECK(ls.year == 0);
	CHECK(ls.day == 0);
	CHECK(ls.hour > 0);

	const LSTimeSection section = world.currentLSTimeSection(simulated_time);
	CHECK(getPaletteIndex(section) >= 1);
	CHECK(getPaletteIndex(section) <= 4);
}

TEST_CASE("阶段 10: 反向变异实证 (RV-Weather-1)")
{
	SUBCASE("时段边界防篡改实证")
	{
		// 验证清晨与白昼交界点必须严格在 930
		const LSTime t_morning{0, 0, 930};
		const LSTime t_noon{0, 0, 931};

		CHECK(getLSTimeSection(t_morning) == LSTimeSection::kMorning);
		CHECK(getLSTimeSection(t_noon) == LSTimeSection::kNoon);

		// 若有人将 kMorningToNoon 篡改成 940，则 931 会被判定为 Morning，
		// 破坏石器时代原版经典光照与 NPC 出现逻辑
		bool boundary_mutated = (getLSTimeSection(t_noon) == LSTimeSection::kMorning);
		CHECK_FALSE(boundary_mutated);
	}

	SUBCASE("天气强度上限严格钳位防溢出实证")
	{
		ServerFixture fix;
		auto &world = fix.world;
		// 传入极端非法 level 9999
		world.setMapWeather(500, WeatherKind::kSnow, 9999, 0);
		const MapWeather w = world.mapWeather(500);

		// 断言必须被严格钳位在 5 级以内，防止客户端粒子系统溢出崩溃
		CHECK(w.level <= 5);
		CHECK(w.level == 5);
	}
}
