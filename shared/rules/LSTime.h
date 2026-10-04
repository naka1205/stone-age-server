// shared/rules/LSTime.h —— L3 石器历法与昼夜流动规则纯函数
//
// 1:1 复刻原版 handletime.h / handletime.c 中的石器历法规范与四时段状态机。
// 严守 01 §4 / 05 §1.5 约束：纯函数，禁止包含 <chrono> / <ctime>，严禁依赖 I/O。

#ifndef __SA_RULES_LSTIME_H__
#define __SA_RULES_LSTIME_H__

#include <cstdint>
#include <string_view>

namespace SA::Rules
{

// 石器时代历法常量（原版 handletime.c 对齐）
// 纪元起点：912766409 + 5400
inline constexpr std::int64_t kEraSeconds = 912766409 + 5400;

// 现实 5400 秒（90 分钟 = 1.5 小时）为石器时代 1 天
inline constexpr std::int64_t kSecondsPerLSDay = 5400;

// 石器时代 1 天划分为 1024 个刻度（LS Hour，0..1023）
inline constexpr std::int32_t kHoursPerLSDay = 1024;

// 石器时代 1 年包含 100 天（0..99）
inline constexpr std::int32_t kDaysPerLSYear = 100;

// 时段刻度分界线（handletime.h 对齐）
// NIGHT_TO_MORNING = 700
// MORNING_TO_NOON  = 930
// NOON_TO_EVENING  = 200
// EVENING_TO_NIGHT = 300
inline constexpr std::int32_t kNightToMorning = 700;
inline constexpr std::int32_t kMorningToNoon = 930;
inline constexpr std::int32_t kNoonToEvening = 200;
inline constexpr std::int32_t kEveningToNight = 300;

// 石器时间模型
struct LSTime
{
	std::int32_t year = 0; // 石器年
	std::int32_t day = 0;  // 石器日（0..99）
	std::int32_t hour = 0; // 石器时刻度（0..1023）
};

// 昼夜四大时段（1:1 对齐原版 LSTIME_SECTION）
enum class LSTimeSection : std::uint8_t
{
	kNight = 0,   // 黑夜：300 < hour <= 700
	kMorning = 1, // 清晨：700 < hour <= 930
	kNoon = 2,    // 正午/白昼：930 < hour <= 1023 || 0 <= hour <= 200
	kEvening = 3, // 黄昏/傍晚：200 < hour <= 300
};

// 将现实时间戳（自 Unix Epoch 秒数）转换为石器时间
LSTime computeLSTime(std::int64_t real_seconds) noexcept;

// 将石器时间逆转换为现实时间戳秒数
std::int64_t computeRealTime(const LSTime &ls_time) noexcept;

// 获取石器时间对应的时段状态
LSTimeSection getLSTimeSection(const LSTime &ls_time) noexcept;

// 时段文本标识
const char *getLSTimeSectionName(LSTimeSection section) noexcept;

// 获取时段对应的默认调色板编号（PALET_0..15）
// 原版规范映射：
// kNight -> 3 (沉夜浓墨)
// kMorning -> 1 (晨曦朝霞)
// kNoon -> 2 (正午亮昼)
// kEvening -> 4 (夕阳暮霭)
std::int32_t getPaletteIndex(LSTimeSection section) noexcept;

} // namespace SA::Rules

#endif // __SA_RULES_LSTIME_H__
