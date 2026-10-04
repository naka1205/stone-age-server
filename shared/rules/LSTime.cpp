// shared/rules/LSTime.cpp —— L3 石器历法与昼夜流动规则纯函数实现
//
// 1:1 复刻原版 handletime.c。

#include "rules/LSTime.h"

namespace SA::Rules
{

LSTime computeLSTime(std::int64_t real_seconds) noexcept
{
	if (real_seconds < kEraSeconds)
	{
		return LSTime{0, 0, 0};
	}

	const std::int64_t ls_seconds = real_seconds - kEraSeconds;
	const std::int64_t seconds_per_year = kSecondsPerLSDay * kDaysPerLSYear;

	const auto year = static_cast<std::int32_t>(ls_seconds / seconds_per_year);
	const std::int64_t ls_days = ls_seconds / kSecondsPerLSDay;
	const auto day = static_cast<std::int32_t>(ls_days % kDaysPerLSYear);

	const std::int64_t remainder_seconds = ls_seconds % kSecondsPerLSDay;
	const auto hour = static_cast<std::int32_t>(
	    (remainder_seconds * kHoursPerLSDay) / kSecondsPerLSDay);

	return LSTime{year, day, hour};
}

std::int64_t computeRealTime(const LSTime &ls_time) noexcept
{
	if (ls_time.year < 0 || ls_time.day < 0 || ls_time.hour < 0)
	{
		return kEraSeconds;
	}

	const std::int64_t day_offset =
	    static_cast<std::int64_t>(ls_time.year) * kDaysPerLSYear + ls_time.day;
	const std::int64_t hour_seconds =
	    (static_cast<std::int64_t>(ls_time.hour) * kSecondsPerLSDay + kHoursPerLSDay - 1) /
	    kHoursPerLSDay;

	return kEraSeconds + (day_offset * kSecondsPerLSDay) + hour_seconds;
}

LSTimeSection getLSTimeSection(const LSTime &ls_time) noexcept
{
	const std::int32_t hour = ls_time.hour;

	// 1:1 对齐原版 handletime.c:getLSTime
	if (hour > kNightToMorning && hour <= kMorningToNoon)
	{
		return LSTimeSection::kMorning; // 700 < hour <= 930
	}
	if (hour > kNoonToEvening && hour <= kEveningToNight)
	{
		return LSTimeSection::kEvening; // 200 < hour <= 300
	}
	if (hour > kEveningToNight && hour <= kNightToMorning)
	{
		return LSTimeSection::kNight; // 300 < hour <= 700
	}
	return LSTimeSection::kNoon; // 930 < hour <= 1023 || 0 <= hour <= 200
}

const char *getLSTimeSectionName(LSTimeSection section) noexcept
{
	switch (section)
	{
	case LSTimeSection::kNight:
		return "Night";
	case LSTimeSection::kMorning:
		return "Morning";
	case LSTimeSection::kNoon:
		return "Noon";
	case LSTimeSection::kEvening:
		return "Evening";
	}
	return "Unknown";
}

std::int32_t getPaletteIndex(LSTimeSection section) noexcept
{
	switch (section)
	{
	case LSTimeSection::kNight:
		return 3; // PALET_3
	case LSTimeSection::kMorning:
		return 1; // PALET_1
	case LSTimeSection::kNoon:
		return 2; // PALET_2
	case LSTimeSection::kEvening:
		return 4; // PALET_4
	}
	return 2;
}

} // namespace SA::Rules
