#include <boost/asio.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <type_traits>

#include "../Includes/FasterDefs.h"
#include "../Includes/WTSMarcos.h"

#define private public
#include "../WtBtCore/HftMocker.h"
#undef private

#include "../Includes/WTSContractInfo.hpp"
#include "gtest/gtest.h"

namespace
{
constexpr const char* kCode = "SSE.STK.600000";

class TestReplayer final : public HisDataReplayer
{
public:
	~TestReplayer()
	{
		if (_bd_mgr._fullcode_map != nullptr)
		{
			_bd_mgr._fullcode_map->release();
			_bd_mgr._fullcode_map = nullptr;
		}
	}
};

void addCommodity(TestReplayer& replayer, const char* product)
{
	WTSCommodityInfo* commodity = WTSCommodityInfo::create(product, product, "SSE", "TEST", "TEST");
	replayer._bd_mgr._fullpid_map->add((std::string("SSE.") + product).c_str(), commodity, false);
}

void addOrder(HftMocker& mocker, uint32_t localId, bool isBuy, double left, const char* code = kCode)
{
	auto order = std::make_shared<HftMocker::OrderInfo>();
	order->_localid = localId;
	order->_isBuy = isBuy;
	order->_left = left;
	order->_total = left;
	wt_strcpy_bounded(order->_code, code);
	mocker._orders[localId] = order;
}

TEST(HftMockerOrderInfo, DefaultConstructionIsDeterministicAndCopyIsForbidden)
{
	EXPECT_TRUE(std::is_default_constructible<HftMocker::OrderInfo>::value);
	EXPECT_FALSE(std::is_copy_constructible<HftMocker::OrderInfo>::value);
	EXPECT_FALSE(std::is_copy_assignable<HftMocker::OrderInfo>::value);

	HftMocker::OrderInfo order;
	EXPECT_FALSE(order._isBuy);
	EXPECT_STREQ("", order._code);
	EXPECT_DOUBLE_EQ(0.0, order._price);
	EXPECT_DOUBLE_EQ(0.0, order._total);
	EXPECT_DOUBLE_EQ(0.0, order._left);
	EXPECT_STREQ("", order._usertag);
	EXPECT_EQ(0U, order._localid);
	EXPECT_FALSE(order._proced_after_placed);
}

TEST(HftMockerOrderInfo, AcceptsMaximumLengthCodeAndUserTag)
{
	const std::string code = "SSE.PRODUCT12345678.12345678901";
	const std::string userTag(31, 't');
	ASSERT_EQ(31U, code.size());

	TestReplayer replayer;
	addCommodity(replayer, "PRODUCT12345678");
	HftMocker mocker(&replayer, "hft-order-boundary-test");

	OrderIDs ids = mocker.stra_buy(code.c_str(), 1.0, 1.0, userTag.c_str());
	ASSERT_EQ(1U, ids.size());
	ASSERT_EQ(1U, mocker._orders.size());
	EXPECT_STREQ(code.c_str(), mocker._orders.begin()->second->_code);
	EXPECT_STREQ(userTag.c_str(), mocker._orders.begin()->second->_usertag);
}

TEST(HftMockerOrderInfo, RejectsCodeThatDoesNotFitTheOrderBuffer)
{
	const std::string code = "SSE.PRODUCT123456789.12345678901";
	ASSERT_EQ(32U, code.size());

	TestReplayer replayer;
	addCommodity(replayer, "PRODUCT123456789");
	HftMocker mocker(&replayer, "hft-order-boundary-test");

	EXPECT_TRUE(mocker.stra_buy(code.c_str(), 1.0, 1.0, "tag").empty());
	EXPECT_TRUE(mocker._orders.empty());
}

TEST(HftMockerOrderInfo, RejectsUserTagThatDoesNotFitTheOrderBuffer)
{
	TestReplayer replayer;
	addCommodity(replayer, "STK");
	HftMocker mocker(&replayer, "hft-order-boundary-test");
	const std::string userTag(32, 't');

	EXPECT_TRUE(mocker.stra_buy(kCode, 1.0, 1.0, userTag.c_str()).empty());
	EXPECT_TRUE(mocker._orders.empty());
}
}

TEST(HftMockerOrders, UndoneUsesSignedRemainingQuantity)
{
	HftMocker mocker(nullptr, "hft-order-test");
	addOrder(mocker, 1, true, 3.5);
	addOrder(mocker, 2, false, 1.25);
	addOrder(mocker, 3, true, 0.0);
	addOrder(mocker, 4, true, 9.0, "SSE.STK.600001");

	EXPECT_DOUBLE_EQ(2.25, mocker.stra_get_undone(kCode));
}

TEST(HftMockerOrders, ZeroCancelQuantitySelectsAllMatchingOrders)
{
	HftMocker mocker(nullptr, "hft-order-test");
	addOrder(mocker, 1, false, 1.0);
	addOrder(mocker, 2, false, 1.0);
	addOrder(mocker, 3, false, 1.0);
	addOrder(mocker, 4, true, 1.0);

	EXPECT_EQ(3U, mocker.stra_cancel(kCode, false, 0).size());
}

TEST(HftMockerOrders, NegativeCancelQuantityIsNormalizedDefensively)
{
	HftMocker mocker(nullptr, "hft-order-test");
	addOrder(mocker, 1, false, 1.0);
	addOrder(mocker, 2, false, 1.0);

	EXPECT_EQ(2U, mocker.stra_cancel(kCode, false, -1.5).size());
}

TEST(HftMockerOrders, UndoneWaitsForTheOrderMutex)
{
	using namespace std::chrono_literals;

	HftMocker mocker(nullptr, "hft-order-test");
	addOrder(mocker, 1, true, 1.0);
	mocker._mtx_ords.lock();
	auto result = std::async(std::launch::async, [&mocker]() {
		return mocker.stra_get_undone(kCode);
	});

	EXPECT_EQ(std::future_status::timeout, result.wait_for(50ms));
	mocker._mtx_ords.unlock();
	ASSERT_EQ(std::future_status::ready, result.wait_for(1s));
	EXPECT_DOUBLE_EQ(1.0, result.get());
}
