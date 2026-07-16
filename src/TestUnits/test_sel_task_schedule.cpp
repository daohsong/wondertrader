#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "../Includes/FasterDefs.h"

#define private public
#define protected public
#include "../WtCore/WtSelEngine.h"
#undef protected
#undef private

#include "../Includes/IBaseDataMgr.h"
#include "../Includes/WTSSessionInfo.hpp"
#include "../Share/TaskSchedule.hpp"
#include "gtest/gtest.h"

USING_NS_WTP;

namespace
{
constexpr uint32_t kDate = 20260715;

class ScheduleBaseDataMgr final : public IBaseDataMgr
{
public:
	ScheduleBaseDataMgr()
	{
		_session = WTSSessionInfo::create("TEST", "test session");
		_session->addTradingSection(900, 1500);
	}

	~ScheduleBaseDataMgr() { _session->release(); }

	WTSCommodityInfo* getCommodity(const char*) override { return nullptr; }
	WTSCommodityInfo* getCommodity(const char*, const char*) override { return nullptr; }
	WTSContractInfo* getContract(const char*, const char* = "", uint32_t = 0) override { return nullptr; }
	WTSArray* getContracts(const char* = "", uint32_t = 0) override { return nullptr; }
	WTSSessionInfo* getSession(const char*) override { return _session; }
	WTSSessionInfo* getSessionByCode(const char*, const char* = "") override { return _session; }
	WTSArray* getAllSessions() override { return nullptr; }
	bool isHoliday(const char*, uint32_t, bool = false) override { return holiday; }
	uint32_t calcTradingDate(const char*, uint32_t date, uint32_t, bool = false) override { return date; }
	uint64_t getBoundaryTime(const char*, uint32_t, bool = false, bool = true) override { return 0; }

	bool holiday = false;

private:
	WTSSessionInfo* _session;
};

class RecordingSelContext final : public ISelStraCtx
{
public:
	RecordingSelContext() : ISelStraCtx("schedule-test") {}

	uint32_t id() override { return 77; }
	void on_init() override {}
	void on_session_begin(uint32_t) override {}
	void on_session_end(uint32_t) override {}
	void on_tick(const char*, WTSTickData*, bool = true) override {}
	void on_bar(const char*, const char*, uint32_t, WTSBarStruct*) override {}
	bool on_schedule(uint32_t curDate, uint32_t curTime, uint32_t fireTime) override
	{
		{
			std::lock_guard<std::mutex> lock(_mutex);
			date = curDate;
			time = curTime;
			fire = fireTime;
			count++;
		}
		_condition.notify_all();
		return true;
	}
	void on_bar_close(const char*, const char*, WTSBarStruct*) override {}
	void enum_position(FuncEnumSelPositionCallBack) override {}
	double stra_get_position(const char*, bool = false, const char* = "") override { return 0; }
	void stra_set_position(const char*, double, const char* = "") override {}
	double stra_get_price(const char*) override { return 0; }
	double stra_get_day_price(const char*, int = 0) override { return 0; }
	uint32_t stra_get_tdate() override { return 0; }
	uint32_t stra_get_date() override { return 0; }
	uint32_t stra_get_time() override { return 0; }
	double stra_get_fund_data(int = 0) override { return 0; }
	uint64_t stra_get_first_entertime(const char*) override { return 0; }
	uint64_t stra_get_last_entertime(const char*) override { return 0; }
	uint64_t stra_get_last_exittime(const char*) override { return 0; }
	double stra_get_last_enterprice(const char*) override { return 0; }
	const char* stra_get_last_entertag(const char*) override { return ""; }
	double stra_get_position_avgpx(const char*) override { return 0; }
	double stra_get_position_profit(const char*) override { return 0; }
	uint64_t stra_get_detail_entertime(const char*, const char*) override { return 0; }
	double stra_get_detail_cost(const char*, const char*) override { return 0; }
	double stra_get_detail_profit(const char*, const char*, int = 0) override { return 0; }
	WTSCommodityInfo* stra_get_comminfo(const char*) override { return nullptr; }
	WTSSessionInfo* stra_get_sessinfo(const char*) override { return nullptr; }
	WTSKlineSlice* stra_get_bars(const char*, const char*, uint32_t) override { return nullptr; }
	WTSTickSlice* stra_get_ticks(const char*, uint32_t) override { return nullptr; }
	WTSTickData* stra_get_last_tick(const char*) override { return nullptr; }
	std::string stra_get_rawcode(const char*) override { return {}; }
	void stra_sub_ticks(const char*) override {}
	void stra_log_info(const char*) override {}
	void stra_log_debug(const char*) override {}
	void stra_log_error(const char*) override {}

	bool waitForCount(uint32_t expected, std::chrono::milliseconds timeout)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		return _condition.wait_for(lock, timeout, [this, expected]() { return count >= expected; });
	}

	uint32_t count = 0;
	uint32_t date = 0;
	uint32_t time = 0;
	uint32_t fire = 0;

private:
	std::mutex _mutex;
	std::condition_variable _condition;
};

void prepareEngine(WtSelEngine& engine, ScheduleBaseDataMgr& baseData)
{
	engine._base_data_mgr = &baseData;
	engine._cur_date = kDate;
	engine._cur_tdate = kDate;
}
}

TEST(SelTaskSchedule, OneShotTaskFiresOnceOnItsConfiguredDate)
{
	using namespace std::chrono_literals;
	WtSelEngine engine;
	ScheduleBaseDataMgr baseData;
	prepareEngine(engine, baseData);
	auto context = std::make_shared<RecordingSelContext>();
	engine.addContext(context, kDate, 930, TPT_None, true, "CHINA", "TEST");

	engine.on_minute_end(kDate, 929);
	ASSERT_TRUE(context->waitForCount(1, 1s));
	EXPECT_EQ(kDate, context->date);
	EXPECT_EQ(929U, context->time);
	EXPECT_EQ(930U, context->fire);

	engine.on_minute_end(kDate, 929);
	EXPECT_FALSE(context->waitForCount(2, 100ms));
}

TEST(SelTaskSchedule, OneShotTaskDoesNotFireOnAnotherDateOrHoliday)
{
	using namespace std::chrono_literals;
	WtSelEngine engine;
	ScheduleBaseDataMgr baseData;
	prepareEngine(engine, baseData);
	auto context = std::make_shared<RecordingSelContext>();
	engine.addContext(context, kDate, 930, TPT_None, true, "CHINA", "TEST");

	engine.on_minute_end(20260716, 929);
	EXPECT_FALSE(context->waitForCount(1, 100ms));

	baseData.holiday = true;
	engine.on_minute_end(kDate, 929);
	EXPECT_FALSE(context->waitForCount(1, 100ms));
}

TEST(SelTaskSchedule, PeriodParserDistinguishesOneShotFromInvalidInput)
{
	uint32_t value = UINT32_MAX;
	EXPECT_TRUE(TaskSchedule::parsePeriod("once", value));
	EXPECT_EQ(TaskSchedule::Once, value);
	EXPECT_TRUE(TaskSchedule::parsePeriod("NONE", value));
	EXPECT_EQ(TaskSchedule::Once, value);
	EXPECT_TRUE(TaskSchedule::parsePeriod("", value));
	EXPECT_EQ(TaskSchedule::Once, value);
	EXPECT_TRUE(TaskSchedule::parsePeriod("min", value));
	EXPECT_EQ(TaskSchedule::Minute, value);
	EXPECT_FALSE(TaskSchedule::parsePeriod("daily-typo", value));
	EXPECT_FALSE(TaskSchedule::parsePeriod(nullptr, value));
}

TEST(SelTaskSchedule, OneShotDatePredicateRequiresAnExactDate)
{
	EXPECT_TRUE(TaskSchedule::isOneShotDue(kDate, kDate));
	EXPECT_FALSE(TaskSchedule::isOneShotDue(kDate, kDate - 1));
	EXPECT_FALSE(TaskSchedule::isOneShotDue(kDate, kDate + 1));
}
