#include "../Includes/WTSStruct.h"
#include "../Includes/WTSTradeDef.hpp"
#include "gtest/gtest/gtest.h"

#include <cstring>
#include <string>

TEST(WTSStructConversion, fixed_text_fields_are_bounded_and_terminated)
{
	wtp::WTSTickStructOld oldTick;
	std::memset(oldTick.exchg, 'E', sizeof(oldTick.exchg));
	std::memset(oldTick.code, 'C', sizeof(oldTick.code));

	wtp::WTSTickStruct newTick;
	std::memset(newTick.exchg, 'X', sizeof(newTick.exchg));
	std::memset(newTick.code, 'X', sizeof(newTick.code));

	newTick = oldTick;

	static_assert(sizeof(newTick.exchg) > sizeof(oldTick.exchg));
	EXPECT_EQ(std::memcmp(newTick.exchg, oldTick.exchg, sizeof(oldTick.exchg)), 0);
	EXPECT_EQ(newTick.exchg[sizeof(oldTick.exchg)], '\0');
	EXPECT_EQ(std::memcmp(newTick.code, oldTick.code, sizeof(newTick.code) - 1), 0);
	EXPECT_EQ(newTick.code[sizeof(newTick.code) - 1], '\0');
}

TEST(WTSTradeDef, fixed_text_setters_truncate_and_terminate)
{
	const std::string longText(300, 'X');

	wtp::WTSError* error = wtp::WTSError::create(wtp::WEC_UNKNOWN, longText.c_str());
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(std::strlen(error->getMessage()), 255U);
	error->release();

	wtp::WTSOrderInfo* order = wtp::WTSOrderInfo::create();
	ASSERT_NE(order, nullptr);
	order->setStateMsg(longText.c_str());
	EXPECT_EQ(std::strlen(order->getStateMsg()), 63U);
	order->release();

	wtp::WTSAccountInfo* account = wtp::WTSAccountInfo::create();
	ASSERT_NE(account, nullptr);
	account->setCurrency(longText.c_str());
	EXPECT_EQ(std::strlen(account->getCurrency()), 7U);
	account->release();
}
