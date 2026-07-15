#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <cctype>
#include <string>
#include <thread>
#include <vector>

#include "../Share/TimeUtils.hpp"

namespace
{
bool isDigitAt(const std::string& value, size_t index)
{
	return index < value.size() && std::isdigit(static_cast<unsigned char>(value[index])) != 0;
}
}

TEST(TimeUtilsFormat, LocalTimeKeepsItsPublicShape)
{
	const std::string seconds = TimeUtils::getLocalTime(false);
	ASSERT_EQ(seconds.size(), 8U);
	EXPECT_EQ(seconds[2], ':');
	EXPECT_EQ(seconds[5], ':');
	for (size_t index : {0U, 1U, 3U, 4U, 6U, 7U})
		EXPECT_TRUE(isDigitAt(seconds, index));

	const std::string milliseconds = TimeUtils::getLocalTime(true);
	ASSERT_EQ(milliseconds.size(), 12U);
	EXPECT_EQ(milliseconds[2], ':');
	EXPECT_EQ(milliseconds[5], ':');
	EXPECT_EQ(milliseconds[8], ',');
	for (size_t index : {0U, 1U, 3U, 4U, 6U, 7U, 9U, 10U, 11U})
		EXPECT_TRUE(isDigitAt(milliseconds, index));
}

TEST(TimeUtilsFormat, TimestampRoundTripPreservesExactText)
{
	const int64_t withoutMilliseconds = TimeUtils::makeTime(20240115, 123456000);
	const int64_t withOneMillisecond = TimeUtils::makeTime(20240115, 123456001);
	const int64_t withMaxMilliseconds = TimeUtils::makeTime(20240115, 123456999);

	EXPECT_EQ(TimeUtils::timeToString(withoutMilliseconds), "20240115123456");
	EXPECT_EQ(TimeUtils::timeToString(withOneMillisecond), "20240115123456.001");
	EXPECT_EQ(TimeUtils::timeToString(withMaxMilliseconds), "20240115123456.999");
	EXPECT_EQ(TimeUtils::timeToString(0), "");
	EXPECT_EQ(TimeUtils::timeToString(-1), "");
}

TEST(TimeUtilsFormat, Time32AppendsOptionalMillisecondsWithinItsBuffer)
{
	const int64_t timestamp = TimeUtils::makeTime(20240115, 123456007);
	TimeUtils::Time32 time(static_cast<uint64_t>(timestamp));

	EXPECT_STREQ(time.fmt("%Y-%m-%d %H:%M:%S", false), "2024-01-15 12:34:56");
	EXPECT_STREQ(time.fmt("%Y-%m-%d %H:%M:%S", true), "2024-01-15 12:34:56,007");

	const std::string oversizedFormat(1100, 'X');
	EXPECT_STREQ(time.fmt(oversizedFormat.c_str(), false), "");
	EXPECT_STREQ(time.fmt(oversizedFormat.c_str(), true), ",007");
}

TEST(TimeUtilsFormat, Time32FormattingStorageIsIsolatedPerThread)
{
	constexpr size_t threadCount = 8;
	const time_t seconds = static_cast<time_t>(TimeUtils::makeTime(20240115, 123456000) / 1000);
	std::array<std::string, threadCount> expected;
	std::array<std::string, threadCount> actual;
	for (size_t index = 0; index < threadCount; ++index)
	{
		TimeUtils::Time32 time(seconds, static_cast<uint32_t>(index));
		expected[index] = time.fmt("%Y%m%d%H%M%S", true);
	}

	std::atomic<size_t> ready{0};
	std::vector<std::thread> threads;
	threads.reserve(threadCount);
	for (size_t index = 0; index < threadCount; ++index)
	{
		threads.emplace_back([index, seconds, &ready, &actual]() {
			TimeUtils::Time32 time(seconds, static_cast<uint32_t>(index));
			const char* formatted = time.fmt("%Y%m%d%H%M%S", true);
			ready.fetch_add(1, std::memory_order_release);
			while (ready.load(std::memory_order_acquire) != threadCount)
				std::this_thread::yield();
			actual[index] = formatted;
		});
	}

	for (std::thread& thread : threads)
		thread.join();
	EXPECT_EQ(actual, expected);
}
