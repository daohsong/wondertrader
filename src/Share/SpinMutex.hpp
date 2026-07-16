#pragma once

#if 0
/*
 *	By Wesley @ 2023.12.15
 *  boos的spinlock总是会死锁，不知道是不是我的用法
 */
#include <boost/smart_ptr/detail/spinlock.hpp>

typedef boost::detail::spinlock SpinMutex;
typedef boost::detail::spinlock::scoped_lock SpinLock;

#else

#include <atomic>
#include <thread>
#ifdef _MSC_VER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <intrin.h>
#include <windows.h>
#endif

class SpinMutex
{
private:
	std::atomic<bool> flag = { false };

public:
	inline void lock() noexcept
	{
		for (;;)
		{
			if (!flag.exchange(true, std::memory_order_acquire))
				break;

			while (flag.load(std::memory_order_relaxed))
			{
				pause();
			}
		}
	}

	inline void unlock() noexcept
	{
		flag.store(false, std::memory_order_release);
	}

private:
	static inline void pause() noexcept
	{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
		_mm_pause();
#elif defined(_MSC_VER) && (defined(_M_ARM) || defined(_M_ARM64))
		__yield();
#elif defined(__i386__) || defined(__x86_64__)
		__builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
		__asm__ __volatile__("yield");
#else
		std::this_thread::yield();
#endif
	}
};

class SpinLock
{
public:
	SpinLock(SpinMutex& mtx) noexcept
		:_mutex(mtx) { _mutex.lock(); }
	SpinLock(const SpinLock&) = delete;
	SpinLock& operator=(const SpinLock&) = delete;
	~SpinLock() noexcept { _mutex.unlock(); }

private:
	SpinMutex&	_mutex;
};
#endif
