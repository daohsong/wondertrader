#include "../TraderATP/ATPConversions.hpp"

#include "gtest/gtest.h"

#include <cstdint>
#include <limits>

TEST(test_atp_conversions, price_uses_protocol_fixed_point_scale)
{
	EXPECT_DOUBLE_EQ(0.0, wt::atp::priceToDouble(0));
	EXPECT_DOUBLE_EQ(21.0, wt::atp::priceToDouble(210000));
	EXPECT_DOUBLE_EQ(-1.25, wt::atp::priceToDouble(-12500));
}

TEST(test_atp_conversions, report_index_rejects_values_not_representable_by_sdk_sync_map)
{
	std::int32_t destination = -1;
	EXPECT_TRUE(wt::atp::tryConvertReportIndex(0, destination));
	EXPECT_EQ(0, destination);

	const std::int32_t maximum = (std::numeric_limits<std::int32_t>::max)();
	EXPECT_TRUE(wt::atp::tryConvertReportIndex(maximum, destination));
	EXPECT_EQ(maximum, destination);
	EXPECT_FALSE(wt::atp::tryConvertReportIndex(-1, destination));
	EXPECT_FALSE(wt::atp::tryConvertReportIndex(static_cast<std::int64_t>(maximum) + 1, destination));
}

TEST(test_atp_conversions, pagination_only_requests_nonempty_following_pages)
{
	EXPECT_EQ(0, wt::atp::additionalQueryPageCount(0));
	EXPECT_EQ(0, wt::atp::additionalQueryPageCount(100));
	EXPECT_EQ(1, wt::atp::additionalQueryPageCount(101));
	EXPECT_EQ(1, wt::atp::additionalQueryPageCount(200));
	EXPECT_EQ(2, wt::atp::additionalQueryPageCount(201));
}
