#pragma once

#include <cstdint>

namespace TaskSchedule
{
enum PeriodValue : uint32_t
{
	Once = 0,
	Minute = 4,
	Daily = 8,
	Weekly,
	Monthly,
	Yearly,
};

inline bool equalsIgnoreCase(const char* lhs, const char* rhs) noexcept
{
	if (lhs == nullptr || rhs == nullptr)
		return false;

	while (*lhs != '\0' && *rhs != '\0')
	{
		char left = *lhs++;
		char right = *rhs++;
		if (left >= 'A' && left <= 'Z')
			left = static_cast<char>(left - 'A' + 'a');
		if (right >= 'A' && right <= 'Z')
			right = static_cast<char>(right - 'A' + 'a');
		if (left != right)
			return false;
	}

	return *lhs == '\0' && *rhs == '\0';
}

inline bool parsePeriod(const char* text, uint32_t& value) noexcept
{
	if (text == nullptr)
		return false;

	if (*text == '\0' || equalsIgnoreCase(text, "none") || equalsIgnoreCase(text, "once"))
		value = Once;
	else if (equalsIgnoreCase(text, "min"))
		value = Minute;
	else if (equalsIgnoreCase(text, "d"))
		value = Daily;
	else if (equalsIgnoreCase(text, "w"))
		value = Weekly;
	else if (equalsIgnoreCase(text, "m"))
		value = Monthly;
	else if (equalsIgnoreCase(text, "y"))
		value = Yearly;
	else
		return false;

	return true;
}

inline bool isOneShotDue(uint32_t configuredDate, uint32_t currentDate) noexcept
{
	return configuredDate == currentDate;
}
}
