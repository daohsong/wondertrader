#pragma once

#include <cstdint>
#include <limits>

namespace wt::atp
{
constexpr std::int64_t price_scale = 10000;
constexpr std::int64_t query_page_size = 100;

inline double priceToDouble(std::int64_t source) noexcept
{
	return static_cast<double>(source) / static_cast<double>(price_scale);
}

inline bool tryConvertReportIndex(std::int64_t source, std::int32_t& destination) noexcept
{
	if (source < 0 || source > static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)()))
		return false;

	destination = static_cast<std::int32_t>(source);
	return true;
}

inline std::int64_t additionalQueryPageCount(std::int64_t total) noexcept
{
	if (total <= query_page_size)
		return 0;

	return (total - 1) / query_page_size;
}
}
