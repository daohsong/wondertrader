#pragma once

#include <cstdint>
#include <ctime>
#include <limits>

namespace xele
{
constexpr std::uint64_t mdEpoch = 1546272000;
constexpr std::uint64_t mdMillisecondsMask = 0x000f;
constexpr std::uint32_t halfSecondMilliseconds = 500;

inline bool decodeSnapDateTime(std::uint64_t encodedTime,
	std::uint32_t& actionDate, std::uint32_t& actionTime) noexcept
{
	const std::uint64_t seconds = (encodedTime >> 4U) + mdEpoch;
	if (seconds > static_cast<std::uint64_t>((std::numeric_limits<std::time_t>::max)()))
		return false;

	const std::time_t timestamp = static_cast<std::time_t>(seconds);
	std::tm localTime{};
#ifdef _WIN32
	if (::localtime_s(&localTime, &timestamp) != 0)
		return false;
#else
	if (::localtime_r(&timestamp, &localTime) == nullptr)
		return false;
#endif

	const std::uint32_t milliseconds =
		(encodedTime & mdMillisecondsMask) == 0 ? 0 : halfSecondMilliseconds;
	actionDate = static_cast<std::uint32_t>((localTime.tm_year + 1900) * 10000
		+ (localTime.tm_mon + 1) * 100 + localTime.tm_mday);
	actionTime = static_cast<std::uint32_t>((localTime.tm_hour * 10000
		+ localTime.tm_min * 100 + localTime.tm_sec) * 1000 + milliseconds);
	return true;
}
}
