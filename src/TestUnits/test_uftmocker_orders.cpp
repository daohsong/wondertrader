#include <boost/asio.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>

#include "../Includes/FasterDefs.h"
#include "../Includes/WTSMarcos.h"

#define private public
#include "../WtBtCore/UftMocker.h"
#undef private

#include "gtest/gtest.h"

namespace
{
constexpr const char* kCode = "SHFE.FUT.rb2601";

void addOrder(UftMocker& mocker, uint32_t localId, bool isLong, uint32_t offset, double left, const char* code = kCode)
{
	auto order = std::make_shared<UftMocker::OrderInfo>();
	order->_localid = localId;
	order->_isLong = isLong;
	order->_offset = offset;
	order->_left = left;
	order->_total = left;
	wt_strcpy_bounded(order->_code, code);
	mocker._orders[localId] = order;
}
}

TEST(UftMockerOrders, UndoneMatchesRealtimeAbsoluteRemainingQuantity)
{
	UftMocker mocker(nullptr, "uft-order-test");
	addOrder(mocker, 1, true, 0, 1.0);   // 开多
	addOrder(mocker, 2, false, 0, 2.0);  // 开空
	addOrder(mocker, 3, true, 1, 3.0);   // 平多
	addOrder(mocker, 4, false, 1, 4.0);  // 平空
	addOrder(mocker, 5, true, 0, 0.0);
	addOrder(mocker, 6, true, 0, 9.0, "SHFE.FUT.ag2601");

	EXPECT_DOUBLE_EQ(10.0, mocker.stra_get_undone(kCode));
}

TEST(UftMockerOrders, CancelAllReturnsEveryMatchingOrderId)
{
	UftMocker mocker(nullptr, "uft-order-test");
	addOrder(mocker, 1, true, 0, 1.0);
	addOrder(mocker, 2, false, 0, 1.0);
	addOrder(mocker, 3, true, 1, 1.0);
	addOrder(mocker, 4, true, 0, 1.0, "SHFE.FUT.ag2601");

	EXPECT_EQ(3U, mocker.stra_cancel_all(kCode).size());
}

TEST(UftMockerOrders, EmptyCodeCancelsAllOrdersLikeRealtime)
{
	UftMocker mocker(nullptr, "uft-order-test");
	addOrder(mocker, 1, true, 0, 1.0);
	addOrder(mocker, 2, true, 0, 1.0, "SHFE.FUT.ag2601");

	EXPECT_EQ(2U, mocker.stra_cancel_all("").size());
}

TEST(UftMockerOrders, OrderQueriesWaitForTheOrderMutex)
{
	using namespace std::chrono_literals;

	UftMocker mocker(nullptr, "uft-order-test");
	addOrder(mocker, 1, true, 0, 1.0);
	mocker._mtx_ords.lock();
	auto result = std::async(std::launch::async, [&mocker]() {
		return mocker.stra_get_undone(kCode);
	});

	EXPECT_EQ(std::future_status::timeout, result.wait_for(50ms));
	mocker._mtx_ords.unlock();
	ASSERT_EQ(std::future_status::ready, result.wait_for(1s));
	EXPECT_DOUBLE_EQ(1.0, result.get());
}
