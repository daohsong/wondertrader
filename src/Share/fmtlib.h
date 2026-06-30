#pragma once

#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif
#include <spdlog/fmt/bundled/format.h>
#include <type_traits>

//	By patch @ 2026.06.24
//	fmt 10+ 不再隐式格式化枚举(需显式 formatter)，spdlog 1.17 内置 fmt 12
//	这里提供一个通用枚举 formatter，按底层整型输出，覆盖全部枚举，避免逐个调用点改动
namespace fmt
{
	template<typename E, typename Char>
	struct formatter<E, Char, std::enable_if_t<std::is_enum<E>::value>>
		: formatter<std::underlying_type_t<E>, Char>
	{
		template<typename FormatContext>
		auto format(E val, FormatContext& ctx) const -> decltype(ctx.out())
		{
			return formatter<std::underlying_type_t<E>, Char>::format(
				static_cast<std::underlying_type_t<E>>(val), ctx);
		}
	};
}

namespace fmtutil
{
	template<typename... Args>
	inline char* format_to(char* buffer, const char* format, const Args& ...args) noexcept
	{
		char* s = fmt::format_to(buffer, fmt::runtime(format), args...);
		s[0] = '\0';
		return s;
	}

	template<int BUFSIZE=512, typename... Args>
	inline const char* format(const char* format, const Args& ...args) noexcept
	{
		thread_local static char buffer[BUFSIZE];
		char* s = fmt::format_to(buffer, fmt::runtime(format), args...);
		s[0] = '\0';
		return buffer;
	}
}
