#include <boost/asio.hpp>

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

#include "../Includes/FasterDefs.h"

#define private public
#include "../WtBtCore/HisDataReplayer.h"
#undef private

#include "../Includes/WTSContractInfo.hpp"
#include "../Includes/WTSDataDef.hpp"
#include "../Includes/WTSSessionInfo.hpp"
#include "gtest/gtest.h"

namespace
{
constexpr uint32_t kDate = 20260715;
constexpr const char* kCode = "SSE.600000";
constexpr const char* kTickCode = "SSE.STK.600000";

template <typename Item>
Item makeItem(uint32_t actionTime)
{
	Item item{};
	item.action_date = kDate;
	item.action_time = actionTime;
	return item;
}

class RecordingDataSink final : public IDataSink
{
public:
	void handle_tick(const char*, WTSTickData*, uint32_t) override { events.emplace_back("tick"); }
	void handle_order_queue(const char*, WTSOrdQueData*) override { events.emplace_back("queue"); }
	void handle_order_detail(const char*, WTSOrdDtlData*) override { events.emplace_back("detail"); }
	void handle_transaction(const char*, WTSTransData*) override { events.emplace_back("transaction"); }
	void handle_bar_close(const char*, const char*, uint32_t, WTSBarStruct*) override {}
	void handle_schedule(uint32_t, uint32_t) override {}
	void handle_init() override {}
	void handle_session_begin(uint32_t) override {}
	void handle_session_end(uint32_t) override {}

	std::vector<std::string> events;
};

template <typename Item>
void prepareList(HisDataReplayer::HftDataList<Item>& list, std::initializer_list<Item> items)
{
	list._code = kCode;
	list._date = kDate;
	list._items.assign(items.begin(), items.end());
	list._count = list._items.size();
	list._cursor = UINT_MAX;
}

void prepareStockSession(HisDataReplayer& replayer)
{
	WTSSessionInfo* session = WTSSessionInfo::create("TEST", "test session");
	session->addTradingSection(930, 1130);
	session->addTradingSection(1300, 1500);
	replayer._bd_mgr._session_map->add("TEST", session, false);

	WTSCommodityInfo* commodity = WTSCommodityInfo::create("STK", "stock", "SSE", "TEST", "TEST");
	commodity->setSessionInfo(session);
	replayer._bd_mgr._fullpid_map->add("SSE.STK", commodity, false);
}
}

TEST(HisDataReplayerL2, SingleTransactionIsVisibleAndExhaustionIsSafe)
{
	HisDataReplayer replayer;
	RecordingDataSink sink;
	replayer._listener = &sink;
	replayer._cur_tdate = kDate;
	replayer._terminated = false;
	replayer._trans_sub_map[kCode];
	prepareList(replayer._trans_cache[kCode], {makeItem<WTSTransStruct>(93000000)});

	const uint64_t start = static_cast<uint64_t>(kDate) * 10000ULL + 900;
	const uint64_t end = static_cast<uint64_t>(kDate) * 10000ULL + 1000;
	EXPECT_TRUE(replayer.replayHftDatas(start, end));
	EXPECT_EQ((std::vector<std::string>{"transaction"}), sink.events);
	EXPECT_EQ(2U, replayer._trans_cache[kCode]._cursor);
}

TEST(HisDataReplayerL2, StartTimeKeepsTheItemAtTheBoundary)
{
	HisDataReplayer replayer;
	RecordingDataSink sink;
	replayer._listener = &sink;
	replayer._cur_tdate = kDate;
	replayer._terminated = false;
	replayer._orddtl_sub_map[kCode];
	prepareList(replayer._orddtl_cache[kCode], {
		makeItem<WTSOrdDtlStruct>(92900000),
		makeItem<WTSOrdDtlStruct>(93000000),
		makeItem<WTSOrdDtlStruct>(93100000),
	});

	const uint64_t start = static_cast<uint64_t>(kDate) * 10000ULL + 930;
	const uint64_t end = static_cast<uint64_t>(kDate) * 10000ULL + 931;
	EXPECT_TRUE(replayer.replayHftDatas(start, end));
	EXPECT_EQ((std::vector<std::string>{"detail"}), sink.events);
}

TEST(HisDataReplayerL2, L2OnlyReplayPreservesEventOrder)
{
	HisDataReplayer replayer;
	RecordingDataSink sink;
	replayer._listener = &sink;
	replayer._cur_tdate = kDate;
	replayer._terminated = false;
	replayer._orddtl_sub_map[kCode];
	replayer._trans_sub_map[kCode];
	replayer._ordque_sub_map[kCode];

	prepareList(replayer._orddtl_cache[kCode], {
		makeItem<WTSOrdDtlStruct>(93000000),
		makeItem<WTSOrdDtlStruct>(93000000),
	});
	prepareList(replayer._trans_cache[kCode], {
		makeItem<WTSTransStruct>(93000000),
		makeItem<WTSTransStruct>(93000000),
	});
	prepareList(replayer._ordque_cache[kCode], {
		makeItem<WTSOrdQueStruct>(93000000),
		makeItem<WTSOrdQueStruct>(93000000),
	});

	const uint64_t start = static_cast<uint64_t>(kDate) * 10000ULL + 900;
	const uint64_t end = static_cast<uint64_t>(kDate) * 10000ULL + 1000;
	EXPECT_TRUE(replayer.replayHftDatas(start, end));
	EXPECT_EQ((std::vector<std::string>{
		"detail", "detail",
		"transaction", "transaction",
		"queue", "queue",
	}), sink.events);
}

TEST(HisDataReplayerL2, DailyReplayUsesTheSameStableEventOrder)
{
	HisDataReplayer replayer;
	RecordingDataSink sink;
	replayer._listener = &sink;
	replayer._cur_tdate = kDate;
	replayer._terminated = false;
	replayer._orddtl_sub_map[kCode];
	replayer._trans_sub_map[kCode];
	replayer._ordque_sub_map[kCode];

	prepareList(replayer._orddtl_cache[kCode], {
		makeItem<WTSOrdDtlStruct>(93000000),
		makeItem<WTSOrdDtlStruct>(93000000),
	});
	prepareList(replayer._trans_cache[kCode], {
		makeItem<WTSTransStruct>(93000000),
		makeItem<WTSTransStruct>(93000000),
	});
	prepareList(replayer._ordque_cache[kCode], {
		makeItem<WTSOrdQueStruct>(93000000),
		makeItem<WTSOrdQueStruct>(93000000),
	});

	EXPECT_EQ(6U, replayer.replayHftDatasByDay(kDate));
	EXPECT_EQ((std::vector<std::string>{
		"detail", "detail",
		"transaction", "transaction",
		"queue", "queue",
	}), sink.events);
}

TEST(HisDataReplayerL2, MissingOrderDetailDoesNotDestroyTransactions)
{
	HisDataReplayer replayer;
	RecordingDataSink sink;
	replayer._listener = &sink;
	replayer._mode = "bin";
	replayer._cur_tdate = kDate;
	replayer._terminated = false;
	replayer._orddtl_sub_map[kCode];
	replayer._trans_sub_map[kCode];
	prepareList(replayer._trans_cache[kCode], {makeItem<WTSTransStruct>(93000000)});

	const uint64_t start = static_cast<uint64_t>(kDate) * 10000ULL + 900;
	const uint64_t end = static_cast<uint64_t>(kDate) * 10000ULL + 1000;
	EXPECT_TRUE(replayer.replayHftDatas(start, end));
	EXPECT_EQ((std::vector<std::string>{"transaction"}), sink.events);
}

TEST(HisDataReplayerL2, MissingOrderQueueDoesNotDestroyTransactions)
{
	HisDataReplayer replayer;
	RecordingDataSink sink;
	replayer._listener = &sink;
	replayer._mode = "bin";
	replayer._cur_tdate = kDate;
	replayer._terminated = false;
	replayer._ordque_sub_map[kCode];
	replayer._trans_sub_map[kCode];
	prepareList(replayer._trans_cache[kCode], {makeItem<WTSTransStruct>(93000000)});

	const uint64_t start = static_cast<uint64_t>(kDate) * 10000ULL + 900;
	const uint64_t end = static_cast<uint64_t>(kDate) * 10000ULL + 1000;
	EXPECT_TRUE(replayer.replayHftDatas(start, end));
	EXPECT_EQ((std::vector<std::string>{"transaction"}), sink.events);
}

TEST(HisDataReplayerL2, TickCursorSkipsSectionBreakWithoutReadingPastTheEnd)
{
	HisDataReplayer replayer;
	RecordingDataSink sink;
	prepareStockSession(replayer);
	replayer._listener = &sink;
	replayer._cur_tdate = kDate;
	replayer._terminated = false;
	replayer._tick_sub_map[kTickCode];
	prepareList(replayer._ticks_cache[kTickCode], {
		makeItem<WTSTickStruct>(120000000),
		makeItem<WTSTickStruct>(130000000),
	});

	const uint64_t start = static_cast<uint64_t>(kDate) * 10000ULL + 1200;
	const uint64_t end = static_cast<uint64_t>(kDate) * 10000ULL + 1400;
	EXPECT_TRUE(replayer.replayHftDatas(start, end));
	EXPECT_EQ((std::vector<std::string>{"tick"}), sink.events);
	EXPECT_EQ(3U, replayer._ticks_cache[kTickCode]._cursor);
}

TEST(HisDataReplayerL2, TickAfterCloseExhaustsTheStreamSafely)
{
	HisDataReplayer replayer;
	RecordingDataSink sink;
	prepareStockSession(replayer);
	replayer._listener = &sink;
	replayer._cur_tdate = kDate;
	replayer._terminated = false;
	replayer._tick_sub_map[kTickCode];
	prepareList(replayer._ticks_cache[kTickCode], {makeItem<WTSTickStruct>(160000000)});

	const uint64_t start = static_cast<uint64_t>(kDate) * 10000ULL + 1500;
	const uint64_t end = static_cast<uint64_t>(kDate) * 10000ULL + 1700;
	EXPECT_FALSE(replayer.replayHftDatas(start, end));
	EXPECT_TRUE(sink.events.empty());
	EXPECT_EQ(2U, replayer._ticks_cache[kTickCode]._cursor);
}

TEST(HisDataReplayerTaskSchedule, OneShotRegistrationInitializesTheTaskState)
{
	HisDataReplayer replayer;
	replayer.register_task(42, kDate, 930, "once", "CHINA", "TEST");

	ASSERT_TRUE(replayer._task);
	EXPECT_EQ(42U, replayer._task->_id);
	EXPECT_EQ(kDate, replayer._task->_day);
	EXPECT_EQ(930U, replayer._task->_time);
	EXPECT_EQ(HisDataReplayer::TPT_None, replayer._task->_period);
	EXPECT_EQ(0U, replayer._task->_last_exe_time);
}

TEST(HisDataReplayerTaskSchedule, InvalidPeriodDoesNotRegisterATask)
{
	HisDataReplayer replayer;
	replayer.register_task(42, kDate, 930, "daily-typo", "CHINA", "TEST");

	EXPECT_FALSE(replayer._task);
}
