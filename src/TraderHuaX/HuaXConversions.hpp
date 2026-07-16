#pragma once

#include <cmath>
#include <limits>
#include <type_traits>

namespace wt::huax
{
template<typename VolumeType>
bool tryConvertOrderVolume(double source, VolumeType& destination) noexcept
{
	static_assert(std::is_integral<VolumeType>::value, "HuaX order volume must be integral");
	static_assert(std::numeric_limits<VolumeType>::digits <= std::numeric_limits<double>::digits,
		"HuaX order volume must be exactly representable by double");

	if (!std::isfinite(source) || source < 0.0 || std::trunc(source) != source)
		return false;

	const double maximum = static_cast<double>((std::numeric_limits<VolumeType>::max)());
	if (source > maximum)
		return false;

	destination = static_cast<VolumeType>(source);
	return true;
}
}
