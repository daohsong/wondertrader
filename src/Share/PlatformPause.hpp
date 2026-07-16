#pragma once

#ifdef _MSC_VER
#include <intrin.h>
#endif

#if !(defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM) || defined(_M_ARM64))) && \
	!((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || defined(__arm__)))
#include <thread>
#endif

inline void wt_platform_pause() noexcept
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
	_mm_pause();
#elif defined(_MSC_VER) && (defined(_M_ARM) || defined(_M_ARM64))
	__yield();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
	__builtin_ia32_pause();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__aarch64__) || defined(__arm__))
	__asm__ __volatile__("yield");
#else
	std::this_thread::yield();
#endif
}
