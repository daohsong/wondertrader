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

#include "PlatformPause.hpp"

#include <atomic>

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
				wt_platform_pause();
			}
		}
	}

	inline void unlock() noexcept
	{
		flag.store(false, std::memory_order_release);
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
