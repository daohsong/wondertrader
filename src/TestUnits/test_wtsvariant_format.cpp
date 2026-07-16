#include "gtest/gtest.h"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <string>

#include "../Includes/WTSVariant.hpp"

using wtp::WTSVariant;

namespace
{
struct VariantReleaser
{
	void operator()(WTSVariant* value) const
	{
		if (value != nullptr)
			value->release();
	}
};

using VariantPtr = std::unique_ptr<WTSVariant, VariantReleaser>;
}

TEST(WTSVariantFormat, IntegerLimitsUseExactPortableDecimalText)
{
	VariantPtr value(WTSVariant::createObject());
	value->append("i32_min", std::numeric_limits<int32_t>::min());
	value->append("i32_max", std::numeric_limits<int32_t>::max());
	value->append("u32_max", std::numeric_limits<uint32_t>::max());
	value->append("i64_min", std::numeric_limits<int64_t>::min());
	value->append("i64_max", std::numeric_limits<int64_t>::max());
	value->append("u64_max", std::numeric_limits<uint64_t>::max());

	EXPECT_EQ("-2147483648", value->getString("i32_min"));
	EXPECT_EQ("2147483647", value->getString("i32_max"));
	EXPECT_EQ("4294967295", value->getString("u32_max"));
	EXPECT_EQ("-9223372036854775808", value->getString("i64_min"));
	EXPECT_EQ("9223372036854775807", value->getString("i64_max"));
	EXPECT_EQ("18446744073709551615", value->getString("u64_max"));

	EXPECT_EQ(std::numeric_limits<int64_t>::min(), value->getInt64("i64_min"));
	EXPECT_EQ(std::numeric_limits<int64_t>::max(), value->getInt64("i64_max"));
	EXPECT_EQ(std::numeric_limits<uint64_t>::max(), value->getUInt64("u64_max"));
}

TEST(WTSVariantFormat, RealValuesKeepTenFractionalDigits)
{
	VariantPtr value(WTSVariant::createObject());
	value->append("regular", 1.25);
	value->append("negative_zero", -0.0);

	EXPECT_EQ("1.2500000000", value->getString("regular"));
	EXPECT_EQ("-0.0000000000", value->getString("negative_zero"));
}

TEST(WTSVariantFormat, MaximumRealValueDoesNotOverflowFormattingStorage)
{
	const double maximum = std::numeric_limits<double>::max();
	std::ostringstream expected;
	expected.imbue(std::locale::classic());
	expected << std::fixed << std::setprecision(10) << maximum;

	VariantPtr value(WTSVariant::createObject());
	value->append("maximum", maximum);

	EXPECT_EQ(expected.str(), value->getString("maximum"));
	EXPECT_DOUBLE_EQ(maximum, value->getDouble("maximum"));
}
