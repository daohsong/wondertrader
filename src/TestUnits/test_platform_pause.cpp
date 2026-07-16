#include "../Share/PlatformPause.hpp"
#include "../Share/SpinMutex.hpp"
#include "gtest/gtest/gtest.h"

#include <thread>
#include <vector>

TEST(test_platform_pause, platform_pause_is_noexcept_callable)
{
	static_assert(noexcept(wt_platform_pause()));
	wt_platform_pause();

	SUCCEED();
}

TEST(test_platform_pause, spinlock_protects_multithreaded_increment)
{
	SpinMutex mutex;
	int value = 0;

	constexpr int threadCount = 4;
	constexpr int incrementsPerThread = 10000;
	std::vector<std::thread> threads;
	threads.reserve(threadCount);

	for (int i = 0; i < threadCount; ++i)
	{
		threads.emplace_back([&mutex, &value]() {
			for (int j = 0; j < incrementsPerThread; ++j)
			{
				SpinLock lock(mutex);
				++value;
			}
		});
	}

	for (std::thread& thread : threads)
		thread.join();

	EXPECT_EQ(threadCount * incrementsPerThread, value);
}
