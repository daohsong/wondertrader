#include "../WTSTools/WTSBaseDataMgr.h"
#include "../Includes/WTSContractInfo.hpp"
#include "gtest/gtest/gtest.h"

#include <cstdio>
#include <fstream>
#include <string>

USING_NS_WTP;

namespace
{
std::string write_temp_file(const char* name, const char* content)
{
	std::string path = std::string("/tmp/") + name;
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << content;
	out.close();
	return path;
}
}

TEST(test_basedata_defaults, fill_stock_like_product_defaults_when_fields_missing)
{
	const std::string sessions = write_temp_file("wt_test_sessions.json", R"({
		"SD0930": {
			"name": "stock day",
			"offset": 0,
			"sections": [
				{"from": 930, "to": 1130},
				{"from": 1300, "to": 1500}
			]
		}
	})");
	const std::string commodities = write_temp_file("wt_test_comms.json", R"({
		"SSE": {
			"CB": {"name": "bond", "covermode": 0, "pricemode": 1, "volscale": 1, "session": "SD0930"},
			"STK": {"name": "stock", "covermode": 0, "pricemode": 1, "volscale": 1, "session": "SD0930"},
			"ETF": {"name": "fund", "covermode": 0, "pricemode": 1, "volscale": 1, "session": "SD0930"},
			"IDX": {"name": "index", "covermode": 0, "pricemode": 1, "volscale": 1, "session": "SD0930"},
			"FUT": {"name": "future", "category": 1, "covermode": 0, "pricemode": 0, "volscale": 10, "session": "SD0930"}
		}
	})");

	WTSBaseDataMgr mgr;
	ASSERT_TRUE(mgr.loadSessions(sessions.c_str()));
	ASSERT_TRUE(mgr.loadCommodities(commodities.c_str()));

	WTSCommodityInfo* cb = mgr.getCommodity("SSE", "CB");
	ASSERT_NE(cb, nullptr);
	EXPECT_DOUBLE_EQ(cb->getLotsTick(), 10.0);
	EXPECT_DOUBLE_EQ(cb->getMinLots(), 10.0);
	EXPECT_DOUBLE_EQ(cb->getPriceTick(), 0.001);
	EXPECT_EQ(cb->getCategoty(), CC_Stock);
	EXPECT_EQ(cb->getTradingMode(), TM_Long);

	WTSCommodityInfo* stk = mgr.getCommodity("SSE", "STK");
	ASSERT_NE(stk, nullptr);
	EXPECT_DOUBLE_EQ(stk->getLotsTick(), 100.0);
	EXPECT_DOUBLE_EQ(stk->getMinLots(), 100.0);
	EXPECT_DOUBLE_EQ(stk->getPriceTick(), 0.01);
	EXPECT_EQ(stk->getCategoty(), CC_Stock);
	EXPECT_EQ(stk->getTradingMode(), TM_LongT1);

	WTSCommodityInfo* etf = mgr.getCommodity("SSE", "ETF");
	ASSERT_NE(etf, nullptr);
	EXPECT_DOUBLE_EQ(etf->getLotsTick(), 100.0);
	EXPECT_DOUBLE_EQ(etf->getMinLots(), 100.0);
	EXPECT_DOUBLE_EQ(etf->getPriceTick(), 0.001);
	EXPECT_EQ(etf->getCategoty(), CC_Stock);
	EXPECT_EQ(etf->getTradingMode(), TM_LongT1);

	WTSCommodityInfo* idx = mgr.getCommodity("SSE", "IDX");
	ASSERT_NE(idx, nullptr);
	EXPECT_DOUBLE_EQ(idx->getLotsTick(), 1.0);
	EXPECT_DOUBLE_EQ(idx->getMinLots(), 1.0);
	EXPECT_DOUBLE_EQ(idx->getPriceTick(), 0.01);
	EXPECT_EQ(idx->getCategoty(), CC_Stock);
	EXPECT_EQ(idx->getTradingMode(), TM_None);

	WTSCommodityInfo* fut = mgr.getCommodity("SSE", "FUT");
	ASSERT_NE(fut, nullptr);
	EXPECT_DOUBLE_EQ(fut->getLotsTick(), 1.0);
	EXPECT_DOUBLE_EQ(fut->getMinLots(), 1.0);
	EXPECT_DOUBLE_EQ(fut->getPriceTick(), 0.0);
	EXPECT_EQ(fut->getCategoty(), CC_Future);
	EXPECT_EQ(fut->getTradingMode(), TM_Both);

	std::remove(sessions.c_str());
	std::remove(commodities.c_str());
}

TEST(test_basedata_defaults, explicit_values_override_product_defaults)
{
	const std::string sessions = write_temp_file("wt_test_sessions_explicit.json", R"({
		"SD0930": {
			"name": "stock day",
			"offset": 0,
			"sections": [
				{"from": 930, "to": 1130},
				{"from": 1300, "to": 1500}
			]
		}
	})");
	const std::string commodities = write_temp_file("wt_test_comms_explicit.json", R"({
		"SSE": {
			"CB": {
				"name": "bond",
				"category": 0,
				"covermode": 0,
				"pricemode": 1,
				"volscale": 1,
				"session": "SD0930",
				"lotstick": 20,
				"minlots": 30,
				"pricetick": 0.02,
				"trademode": 2
			}
		}
	})");

	WTSBaseDataMgr mgr;
	ASSERT_TRUE(mgr.loadSessions(sessions.c_str()));
	ASSERT_TRUE(mgr.loadCommodities(commodities.c_str()));

	WTSCommodityInfo* cb = mgr.getCommodity("SSE", "CB");
	ASSERT_NE(cb, nullptr);
	EXPECT_DOUBLE_EQ(cb->getLotsTick(), 20.0);
	EXPECT_DOUBLE_EQ(cb->getMinLots(), 30.0);
	EXPECT_DOUBLE_EQ(cb->getPriceTick(), 0.02);
	EXPECT_EQ(cb->getCategoty(), CC_Stock);
	EXPECT_EQ(cb->getTradingMode(), TM_LongT1);

	std::remove(sessions.c_str());
	std::remove(commodities.c_str());
}
