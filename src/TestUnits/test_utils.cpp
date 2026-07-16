#include "../Share/TimeUtils.hpp"
#include "../Share/fmtlib.h"
#include "gtest/gtest/gtest.h"
#include "../Includes/WTSMarcos.h"

#include <cstring>

namespace
{
std::string makePaddedIntegerFormat(uint32_t length)
{
	return fmtutil::format("{{:0{}d}}", length);
}
}

void run_test(uint32_t times, uint32_t len)
{
	char buffer[512] = { 0 };

	const std::string format = makePaddedIntegerFormat(len);

	TimeUtils::Ticker ticker;
	for (uint32_t i = 0; i < times; i++)
	{
		wt_strcpy(buffer, fmtutil::format(format.c_str(), i));
	}
	uint64_t t1 = ticker.nano_seconds();

	ticker.reset();
	for (uint32_t i = 0; i < times; i++)
	{
		const char* value = fmtutil::format(format.c_str(), i);
		std::memcpy(buffer, value, std::strlen(value) + 1);
	}
	uint64_t t2 = ticker.nano_seconds();
	fmt::print("{}-bytes string compare, wt_strcpy: {} - strcpy: {}\n", len, t1, t2);
}

TEST(test_utils, padded_integer_format)
{
	EXPECT_EQ(makePaddedIntegerFormat(1), "{:01d}");
	EXPECT_EQ(makePaddedIntegerFormat(6), "{:06d}");
	EXPECT_EQ(makePaddedIntegerFormat(128), "{:0128d}");
}

TEST(test_utils, copy_str)
{
	run_test(1000000, 16);
	run_test(1000000, 32);
	run_test(1000000, 64);
	run_test(1000000, 128);
}
