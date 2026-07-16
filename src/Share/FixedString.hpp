#pragma once

#include <cstddef>
#include <cstring>

namespace wt
{
template <std::size_t Capacity>
bool fits_fixed_string(const char* source) noexcept
{
	return source != nullptr && std::strlen(source) < Capacity;
}

template <std::size_t Capacity>
bool assign_fixed_string(char (&target)[Capacity], const char* source) noexcept
{
	if (!fits_fixed_string<Capacity>(source))
		return false;

	const std::size_t length = std::strlen(source);
	std::memcpy(target, source, length + 1);
	return true;
}
}
