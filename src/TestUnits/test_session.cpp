#include "../Includes/WTSSessionInfo.hpp"
#include "gtest/gtest/gtest.h"

USING_NS_WTP;

TEST(test_session, test_allday)
{
	WTSSessionInfo* sInfo = WTSSessionInfo::create("ALLDAY", "ALLDAY", 0);
	sInfo->addTradingSection(0, 2400);

	EXPECT_EQ(sInfo->timeToMinutes(0), 0);
	EXPECT_EQ(sInfo->minuteToTime(0), 0);
	EXPECT_EQ(sInfo->minuteToTime(1440), 0);

	EXPECT_EQ(sInfo->getOpenTime(), 0);
	EXPECT_EQ(sInfo->getCloseTime(true), 2400);

	EXPECT_EQ(sInfo->getTradingMins(), 1440);
	EXPECT_EQ(sInfo->getTradingSeconds(), 1440*60);

	EXPECT_TRUE(sInfo->isInTradingTime(900));
	EXPECT_TRUE(sInfo->isInTradingTime(0));
	EXPECT_TRUE(sInfo->isInTradingTime(2359));

	EXPECT_TRUE(sInfo->isLastOfSection(0));
	EXPECT_TRUE(sInfo->isFirstOfSection(0));

	EXPECT_EQ(sInfo->offsetTime(0, true), 0);
	EXPECT_EQ(sInfo->offsetTime(0, false), 2400);

	sInfo->release();
}

TEST(test_session, test_midnight_close_boundary)
{
	WTSSessionInfo* sInfo = WTSSessionInfo::create("NIGHT", "NIGHT", 0);
	sInfo->addTradingSection(2100, 0);

	EXPECT_EQ(sInfo->offsetTime(0, false), 2400);
	EXPECT_TRUE(sInfo->isLastOfSection(0));
	EXPECT_EQ(sInfo->timeToMinutes(0), 180);
	EXPECT_EQ(sInfo->timeToSeconds(0), 180*60 - 1);

	sInfo->release();
}
