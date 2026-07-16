#include <boost/asio.hpp>
#include "../Includes/FasterDefs.h"
#include "../Includes/ITraderApi.h"
#include "../Share/StdUtils.hpp"
#include "../Includes/WTSCollection.hpp"

#define private public
#include "../TraderMocker/TraderMocker.h"
#undef private

#include "../Includes/IBaseDataMgr.h"
#include "../Includes/WTSContractInfo.hpp"
#include "../Includes/WTSDataDef.hpp"
#include "../Includes/WTSTradeDef.hpp"
#include "../TraderMocker/TraderMocker.cpp"
#include "gtest/gtest.h"

#include <atomic>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

USING_NS_WTP;

namespace
{
class TestBaseDataMgr : public IBaseDataMgr
{
public:
	TestBaseDataMgr()
	{
		addContract("SSE", "118027", "CB", CC_Stock, CM_OpenCover, TM_Long);
		addContract("SZSE", "127055", "CB", CC_Stock, CM_OpenCover, TM_Long);
		addContract("SSE", "000001", "STK", CC_Stock, CM_OpenCover, TM_Long);
		addContract("SZSE", "000001", "STK", CC_Stock, CM_OpenCover, TM_Long);
		addContract("SSE", "600001", "STKT1", CC_Stock, CM_OpenCover, TM_LongT1);
		addContract("SHFE", "rb2601", "FUT", CC_Future, CM_CoverToday, TM_Both);
		addContract("DCE", "m2601", "FUT", CC_Future, CM_OpenCover, TM_Both);
		addContract("CZCE", "u2601", "UFUT", CC_Future, CM_UNFINISHED, TM_Both);
		addContract("CME", "TEST", "FUT", CC_Future, CM_None, TM_Both);
	}

	~TestBaseDataMgr()
	{
		for (auto& item : _contracts)
			item.second->release();
		for (auto& item : _commodities)
			item.second->release();
	}

	WTSCommodityInfo* getCommodity(const char* exchgpid) override
	{
		auto it = _commodities.find(exchgpid);
		return it == _commodities.end() ? nullptr : it->second;
	}

	WTSCommodityInfo* getCommodity(const char* exchg, const char* pid) override
	{
		return getCommodity((std::string(exchg) + "." + pid).c_str());
	}

	WTSContractInfo* getContract(const char* code, const char* exchg = "", uint32_t uDate = 0) override
	{
		auto it = _contracts.find(std::string(exchg) + "." + code);
		return it == _contracts.end() ? nullptr : it->second;
	}

	WTSArray* getContracts(const char* exchg = "", uint32_t uDate = 0) override { return nullptr; }
	WTSSessionInfo* getSession(const char* sid) override { return nullptr; }
	WTSSessionInfo* getSessionByCode(const char* code, const char* exchg = "") override { return nullptr; }
	WTSArray* getAllSessions() override { return nullptr; }
	bool isHoliday(const char* pid, uint32_t uDate, bool isTpl = false) override { return false; }
	uint32_t calcTradingDate(const char* stdPID, uint32_t uDate, uint32_t uTime, bool isSession = false) override { return uDate; }
	uint64_t getBoundaryTime(const char* stdPID, uint32_t tDate, bool isSession = false, bool isStart = true) override { return 0; }

private:
	void addContract(const char* exchg, const char* code, const char* product, ContractCategory category, CoverMode coverMode, TradingMode tradingMode)
	{
		std::string fullPid = std::string(exchg) + "." + product;
		WTSCommodityInfo* comm = nullptr;
		auto commIt = _commodities.find(fullPid);
		if (commIt == _commodities.end())
		{
			comm = WTSCommodityInfo::create(product, product, exchg, "TEST", "TEST");
			comm->setCategory(category);
			comm->setCoverMode(coverMode);
			comm->setPriceMode(PM_Both);
			comm->setTradingMode(tradingMode);
			comm->setLotsTick(1);
			comm->setMinLots(1);
			comm->setPriceTick(0.001);
			_commodities.emplace(fullPid, comm);
		}
		else
		{
			comm = commIt->second;
		}

		WTSContractInfo* contract = WTSContractInfo::create(code, code, exchg, product);
		contract->setCommInfo(comm);
		_contracts.emplace(std::string(exchg) + "." + code, contract);
	}

	std::unordered_map<std::string, WTSCommodityInfo*> _commodities;
	std::unordered_map<std::string, WTSContractInfo*> _contracts;
};

class RecordingTraderSpi : public ITraderSpi
{
public:
	explicit RecordingTraderSpi(IBaseDataMgr* bdMgr) : _bdMgr(bdMgr) {}

	struct PositionSnapshot
	{
		std::string fullcode;
		WTSDirectionType direction;
		double pre;
		double newpos;
		double availPre;
		double availNew;
	};

	IBaseDataMgr* getBaseDataMgr() override { return _bdMgr; }
	void handleEvent(WTSTraderEvent e, int32_t ec) override {}
	void onLoginResult(bool bSucc, const char* msg, uint32_t tradingdate) override {}

	void onPushTrade(WTSTradeInfo* tradeRecord) override
	{
		trades.emplace_back(std::string(tradeRecord->getExchg()) + "." + tradeRecord->getCode());
		tradeVolumes.emplace_back(tradeRecord->getVolume());
	}

	void onRspEntrust(WTSEntrust* entrust, WTSError *err) override
	{
		rspEntrustErrors.emplace_back(err != nullptr);
	}

	void onPushOrder(WTSOrderInfo* orderInfo) override
	{
		orderStates.emplace_back(orderInfo->getOrderState());
	}

	void onRspPosition(const WTSArray* ayPositions) override
	{
		positions.clear();
		for (uint32_t idx = 0; idx < ayPositions->size(); idx++)
		{
			WTSPositionItem* item = const_cast<WTSArray*>(ayPositions)->at<WTSPositionItem>(idx);
			PositionSnapshot snapshot;
			snapshot.fullcode = std::string(item->getExchg()) + "." + item->getCode();
			snapshot.direction = item->getDirection();
			snapshot.pre = item->getPrePosition();
			snapshot.newpos = item->getNewPosition();
			snapshot.availPre = item->getAvailPrePos();
			snapshot.availNew = item->getAvailNewPos();
			positions.emplace_back(snapshot);
		}
	}

	std::vector<std::string> trades;
	std::vector<double> tradeVolumes;
	std::vector<bool> rspEntrustErrors;
	std::vector<WTSOrderState> orderStates;
	std::vector<PositionSnapshot> positions;

private:
	IBaseDataMgr* _bdMgr;
};

class TempFile
{
public:
	explicit TempFile(const char* stem)
	{
		char path[256];
		std::snprintf(path, sizeof(path), "/tmp/%s_%u_%p.json", stem, ++_counter, static_cast<void*>(this));
		_path = path;
		std::remove(_path.c_str());
	}

	~TempFile()
	{
		std::remove(_path.c_str());
	}

	const char* c_str() const
	{
		return _path.c_str();
	}

private:
	std::string _path;
	static std::atomic<uint32_t> _counter;
};

std::atomic<uint32_t> TempFile::_counter{ 0 };

void prepareTrader(TraderMocker& trader, IBaseDataMgr* bdMgr, ITraderSpi* spi, const char* posFile, uint32_t mockerId, double minQty = 10, double maxQty = 10)
{
	trader._bd_mgr = bdMgr;
	trader._listener = spi;
	trader._max_qty = maxQty;
	trader._min_qty = minQty;
	trader._use_newpx = false;
	trader._mocker_id = mockerId;
	trader._pos_file = posFile;
	trader._orders = WTSArray::create();
	trader._awaits = TraderMocker::OrderCache::create();
	trader._ticks = TraderMocker::TickCache::create();
}

WTSTickData* makeTick(const char* exchg, const char* code, double price, double bidPrice, double bidQty, double askPrice, double askQty)
{
	WTSTickStruct tick = {};
	std::strncpy(tick.exchg, exchg, sizeof(tick.exchg) - 1);
	std::strncpy(tick.code, code, sizeof(tick.code) - 1);
	tick.price = price;
	tick.action_date = 20260707;
	tick.action_time = 93000000;
	tick.bid_prices[0] = bidPrice;
	tick.bid_qty[0] = bidQty;
	tick.ask_prices[0] = askPrice;
	tick.ask_qty[0] = askQty;
	return WTSTickData::create(tick);
}

WTSOrderInfo* makeCloseLongOrder(WTSContractInfo* contract, const char* orderId, double volume)
{
	WTSOrderInfo* order = WTSOrderInfo::create();
	order->setContractInfo(contract);
	order->setExchange(contract->getExchg());
	order->setCode(contract->getCode());
	order->setDirection(WDT_LONG);
	order->setOffsetType(WOT_CLOSE);
	order->setPriceType(WPT_LIMITPRICE);
	order->setPrice(0);
	order->setVolume(volume);
	order->setVolLeft(volume);
	order->setVolTraded(0);
	order->setOrderID(orderId);
	order->setOrderState(WOS_NotTraded_Queuing);
	return order;
}

WTSOrderInfo* makeOpenLongOrder(WTSContractInfo* contract, const char* orderId, double volume)
{
	WTSOrderInfo* order = WTSOrderInfo::create();
	order->setContractInfo(contract);
	order->setExchange(contract->getExchg());
	order->setCode(contract->getCode());
	order->setDirection(WDT_LONG);
	order->setOffsetType(WOT_OPEN);
	order->setPriceType(WPT_LIMITPRICE);
	order->setPrice(100000);
	order->setVolume(volume);
	order->setVolLeft(volume);
	order->setVolTraded(0);
	order->setOrderID(orderId);
	order->setOrderState(WOS_NotTraded_Queuing);
	return order;
}

void setLongPosition(TraderMocker& trader, WTSContractInfo* contract, double volume, double frozen)
{
	TraderMocker::PosItem& pos = trader._positions[contract->getFullCode()];
	std::strncpy(pos._exchg, contract->getExchg(), sizeof(pos._exchg) - 1);
	std::strncpy(pos._code, contract->getCode(), sizeof(pos._code) - 1);
	pos._long._pre_volume = volume;
	pos._long._pre_frozen = frozen;
}

void setLongPositionBuckets(TraderMocker& trader, WTSContractInfo* contract, double preVolume, double preFrozen, double newVolume, double newFrozen)
{
	TraderMocker::PosItem& pos = trader._positions[contract->getFullCode()];
	std::strncpy(pos._exchg, contract->getExchg(), sizeof(pos._exchg) - 1);
	std::strncpy(pos._code, contract->getCode(), sizeof(pos._code) - 1);
	pos._long._pre_volume = preVolume;
	pos._long._pre_frozen = preFrozen;
	pos._long._new_volume = newVolume;
	pos._long._new_frozen = newFrozen;
}

std::string readFile(const char* path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void runTraderIo(TraderMocker& trader)
{
	trader._io_service.restart();
	trader._io_service.run();
}

WTSEntrust* makeEntrust(WTSContractInfo* contract, WTSDirectionType direction, WTSOffsetType offset, double volume)
{
	WTSEntrust* entrust = WTSEntrust::create(contract->getCode(), volume, 0, contract->getExchg());
	entrust->setContractInfo(contract);
	entrust->setDirection(direction);
	entrust->setOffsetType(offset);
	entrust->setPriceType(WPT_LIMITPRICE);
	entrust->setPrice(0);
	return entrust;
}

const RecordingTraderSpi::PositionSnapshot* findPosition(const RecordingTraderSpi& spi, const char* fullcode, WTSDirectionType direction)
{
	for (const auto& pos : spi.positions)
	{
		if (pos.fullcode == fullcode && pos.direction == direction)
			return &pos;
	}
	return nullptr;
}

TEST(test_tradermocker_matching, tick_for_another_contract_does_not_fill_awaiting_order)
{
	TempFile posFile("tradermocker_cross_symbol_positions");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 1);

	WTSContractInfo* orderContract = bdMgr.getContract("118027", "SSE");
	WTSContractInfo* tickContract = bdMgr.getContract("127055", "SZSE");
	ASSERT_NE(orderContract, nullptr);
	ASSERT_NE(tickContract, nullptr);

	setLongPosition(trader, orderContract, 10, 10);
	setLongPosition(trader, tickContract, 0, 0);

	WTSOrderInfo* order = makeCloseLongOrder(orderContract, "test-order", 10);
	trader._orders->append(order, false);
	trader._awaits->add(order->getOrderID(), order, true);
	trader._codes.insert(orderContract->getFullCode());
	trader._codes.insert(tickContract->getFullCode());
	trader._ticks->add(tickContract->getFullCode(), makeTick("SZSE", "127055", 235, 235, 100, 236, 100), false);

	EXPECT_EQ(trader.match_once(), 0);
	EXPECT_EQ(order->getVolLeft(), 10);
	EXPECT_EQ(order->getVolTraded(), 0);
	EXPECT_TRUE(order->isAlive());
	EXPECT_EQ(trader._positions[orderContract->getFullCode()]._long.total_volume(), 10);
	EXPECT_EQ(trader._positions[orderContract->getFullCode()]._long.total_frozen(), 10);
	EXPECT_EQ(trader._positions[tickContract->getFullCode()]._long.total_volume(), 0);
	EXPECT_TRUE(spi.trades.empty());
}

TEST(test_tradermocker_matching, same_raw_code_on_another_exchange_does_not_fill_order)
{
	TempFile posFile("tradermocker_cross_exchange_positions");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 3);

	WTSContractInfo* orderContract = bdMgr.getContract("000001", "SSE");
	WTSContractInfo* tickContract = bdMgr.getContract("000001", "SZSE");
	ASSERT_NE(orderContract, nullptr);
	ASSERT_NE(tickContract, nullptr);

	setLongPosition(trader, orderContract, 10, 10);
	setLongPosition(trader, tickContract, 0, 0);

	WTSOrderInfo* order = makeCloseLongOrder(orderContract, "cross-exchange-order", 10);
	trader._orders->append(order, false);
	trader._awaits->add(order->getOrderID(), order, true);
	trader._codes.insert(orderContract->getFullCode());
	trader._codes.insert(tickContract->getFullCode());
	trader._ticks->add(tickContract->getFullCode(), makeTick("SZSE", "000001", 12.0, 12.0, 100, 12.1, 100), false);

	EXPECT_EQ(trader.match_once(), 0);
	EXPECT_EQ(order->getVolLeft(), 10);
	EXPECT_EQ(order->getVolTraded(), 0);
	EXPECT_TRUE(order->isAlive());
	EXPECT_EQ(trader._positions[orderContract->getFullCode()]._long.total_volume(), 10);
	EXPECT_EQ(trader._positions[tickContract->getFullCode()]._long.total_volume(), 0);
	EXPECT_TRUE(spi.trades.empty());
}

TEST(test_tradermocker_matching, matching_same_contract_increases_traded_volume)
{
	TempFile posFile("tradermocker_same_symbol_positions");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 2);

	WTSContractInfo* contract = bdMgr.getContract("118027", "SSE");
	ASSERT_NE(contract, nullptr);

	setLongPosition(trader, contract, 10, 10);

	WTSOrderInfo* order = makeCloseLongOrder(contract, "test-order", 10);
	trader._orders->append(order, false);
	trader._awaits->add(order->getOrderID(), order, true);
	trader._codes.insert(contract->getFullCode());
	trader._ticks->add(contract->getFullCode(), makeTick("SSE", "118027", 90.1, 90.1, 10, 90.2, 10), false);

	EXPECT_EQ(trader.match_once(), 1);
	EXPECT_EQ(order->getVolLeft(), 0);
	EXPECT_EQ(order->getVolTraded(), 10);
	EXPECT_FALSE(order->isAlive());
	EXPECT_EQ(trader._awaits->size(), 0u);
	EXPECT_EQ(trader._codes.find(contract->getFullCode()), trader._codes.end());
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_volume(), 0);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_frozen(), 0);
	ASSERT_EQ(spi.trades.size(), 1u);
	EXPECT_EQ(spi.trades[0], "SSE.118027");
}

TEST(test_tradermocker_matching, partial_match_keeps_order_and_code_awaiting)
{
	TempFile posFile("tradermocker_partial_match_positions");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 4);

	WTSContractInfo* contract = bdMgr.getContract("118027", "SSE");
	ASSERT_NE(contract, nullptr);

	setLongPosition(trader, contract, 10, 10);

	WTSOrderInfo* order = makeCloseLongOrder(contract, "partial-order", 10);
	trader._orders->append(order, false);
	trader._awaits->add(order->getOrderID(), order, true);
	trader._codes.insert(contract->getFullCode());
	trader._ticks->add(contract->getFullCode(), makeTick("SSE", "118027", 90.1, 90.1, 4, 90.2, 10), false);

	EXPECT_EQ(trader.match_once(), 1);
	EXPECT_EQ(order->getVolLeft(), 6);
	EXPECT_EQ(order->getVolTraded(), 4);
	EXPECT_TRUE(order->isAlive());
	EXPECT_EQ(trader._awaits->size(), 1u);
	EXPECT_NE(trader._codes.find(contract->getFullCode()), trader._codes.end());
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_volume(), 6);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_frozen(), 6);
	ASSERT_EQ(spi.trades.size(), 1u);
	EXPECT_EQ(spi.trades[0], "SSE.118027");
}

TEST(test_tradermocker_matching, close_match_does_not_overdraw_dirty_position)
{
	TempFile posFile("tradermocker_dirty_position_positions");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 5);

	WTSContractInfo* contract = bdMgr.getContract("118027", "SSE");
	ASSERT_NE(contract, nullptr);

	setLongPosition(trader, contract, 2, 2);

	WTSOrderInfo* order = makeCloseLongOrder(contract, "dirty-position-order", 10);
	trader._orders->append(order, false);
	trader._awaits->add(order->getOrderID(), order, true);
	trader._codes.insert(contract->getFullCode());
	trader._ticks->add(contract->getFullCode(), makeTick("SSE", "118027", 90.1, 90.1, 10, 90.2, 10), false);

	EXPECT_EQ(trader.match_once(), 1);
	EXPECT_EQ(order->getVolLeft(), 8);
	EXPECT_EQ(order->getVolTraded(), 2);
	EXPECT_TRUE(order->isAlive());
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_volume(), 0);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_frozen(), 0);
	EXPECT_NE(trader._codes.find(contract->getFullCode()), trader._codes.end());
	ASSERT_EQ(spi.trades.size(), 1u);
	EXPECT_EQ(spi.trades[0], "SSE.118027");
}

TEST(test_tradermocker_matching, cb_open_match_is_capped_to_remaining_volume)
{
	TempFile posFile("tradermocker_cb_open_cap_remaining");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 13, 20, 100);

	WTSContractInfo* contract = bdMgr.getContract("127055", "SZSE");
	ASSERT_NE(contract, nullptr);
	ASSERT_NE(contract->getCommInfo(), nullptr);
	contract->getCommInfo()->setLotsTick(10);
	contract->getCommInfo()->setMinLots(10);

	WTSEntrust* open = makeEntrust(contract, WDT_LONG, WOT_OPEN, 10);
	open->setPrice(130);
	EXPECT_EQ(trader.orderInsert(open), 0);
	open->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_FALSE(spi.rspEntrustErrors.back());
	ASSERT_EQ(trader._awaits->size(), 1u);
	WTSOrderInfo* order = (WTSOrderInfo*)trader._awaits->begin()->second;

	trader._ticks->add(contract->getFullCode(), makeTick("SZSE", "127055", 124, 123.9, 100, 124, 100), false);
	EXPECT_EQ(trader.match_once(), 1);

	EXPECT_EQ(order->getVolLeft(), 0);
	EXPECT_EQ(order->getVolTraded(), 10);
	EXPECT_FALSE(order->isAlive());
	EXPECT_EQ(trader._awaits->size(), 0u);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long._new_volume, 10);
	ASSERT_EQ(spi.trades.size(), 1u);
	EXPECT_EQ(spi.trades[0], "SZSE.127055");
	ASSERT_EQ(spi.tradeVolumes.size(), 1u);
	EXPECT_EQ(spi.tradeVolumes[0], 10);
}

TEST(test_tradermocker_matching, dirty_negative_left_order_is_removed_from_awaiting_queue)
{
	TempFile posFile("tradermocker_negative_left_awaiting");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 14);

	WTSContractInfo* contract = bdMgr.getContract("127055", "SZSE");
	ASSERT_NE(contract, nullptr);

	WTSOrderInfo* order = makeOpenLongOrder(contract, "negative-left-order", 10);
	order->setVolLeft(-10);
	order->setVolTraded(20);
	trader._orders->append(order, false);
	trader._awaits->add(order->getOrderID(), order, true);
	trader._codes.insert(contract->getFullCode());
	trader._ticks->add(contract->getFullCode(), makeTick("SZSE", "127055", 124, 123.9, 100, 124, 100), false);

	EXPECT_EQ(trader.match_once(), 0);
	EXPECT_EQ(order->getVolLeft(), 0);
	EXPECT_EQ(order->getVolTraded(), 10);
	EXPECT_EQ(order->getOrderState(), WOS_AllTraded);
	EXPECT_FALSE(order->isAlive());
	EXPECT_EQ(trader._awaits->size(), 0u);
	EXPECT_EQ(trader._codes.find(contract->getFullCode()), trader._codes.end());
	EXPECT_TRUE(spi.trades.empty());
	EXPECT_TRUE(spi.tradeVolumes.empty());
}

TEST(test_tradermocker_matching, cancel_close_order_releases_frozen_position_and_refreshes_codes)
{
	TempFile posFile("tradermocker_cancel_positions");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 6);

	WTSContractInfo* contract = bdMgr.getContract("118027", "SSE");
	ASSERT_NE(contract, nullptr);

	setLongPosition(trader, contract, 10, 5);

	WTSOrderInfo* order = makeCloseLongOrder(contract, "cancel-order", 5);
	trader._orders->append(order, false);
	trader._awaits->add(order->getOrderID(), order, true);
	trader._codes.insert(contract->getFullCode());

	WTSEntrustAction* action = WTSEntrustAction::create(contract->getCode(), contract->getExchg());
	ASSERT_NE(action, nullptr);
	action->setContractInfo(contract);
	action->setOrderID(order->getOrderID());
	EXPECT_EQ(trader.orderAction(action), 0);
	action->release();

	EXPECT_EQ(trader._io_service.run(), 1u);
	EXPECT_EQ(order->getOrderState(), WOS_Canceled);
	EXPECT_FALSE(order->isAlive());
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_volume(), 10);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_frozen(), 0);
	EXPECT_EQ(trader._awaits->size(), 0u);
	EXPECT_EQ(trader._codes.find(contract->getFullCode()), trader._codes.end());
}

TEST(test_tradermocker_matching, load_positions_rejects_negative_volume_and_clears_previous_state)
{
	TempFile posFile("tradermocker_negative_load_positions");
	{
		std::ofstream out(posFile.c_str(), std::ios::binary | std::ios::trunc);
		out << R"({
			"positions": [
				{"exchg": "SSE", "code": "118027", "long": {"volume": -10}, "short": {"volume": 0}}
			]
		})";
	}

	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	trader._bd_mgr = &bdMgr;
	trader._listener = &spi;
	trader._pos_file = posFile.c_str();

	WTSContractInfo* contract = bdMgr.getContract("118027", "SSE");
	ASSERT_NE(contract, nullptr);
	setLongPosition(trader, contract, 20, 0);

	trader.load_positions();

	EXPECT_TRUE(trader._positions.empty());
}

TEST(test_tradermocker_matching, save_positions_skips_negative_and_empty_positions)
{
	TempFile posFile("tradermocker_save_positions_guard");

	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	trader._bd_mgr = &bdMgr;
	trader._listener = &spi;
	trader._pos_file = posFile.c_str();

	WTSContractInfo* negativeContract = bdMgr.getContract("118027", "SSE");
	WTSContractInfo* emptyContract = bdMgr.getContract("127055", "SZSE");
	WTSContractInfo* validContract = bdMgr.getContract("000001", "SSE");
	ASSERT_NE(negativeContract, nullptr);
	ASSERT_NE(emptyContract, nullptr);
	ASSERT_NE(validContract, nullptr);

	setLongPosition(trader, negativeContract, -10, 0);
	setLongPosition(trader, emptyContract, 0, 0);
	setLongPosition(trader, validContract, 5, 0);

	trader.save_positions();

	const std::string json = readFile(posFile.c_str());
	EXPECT_EQ(json.find("-10"), std::string::npos);
	EXPECT_EQ(json.find("118027"), std::string::npos);
	EXPECT_EQ(json.find("127055"), std::string::npos);
	EXPECT_NE(json.find("000001"), std::string::npos);
}

TEST(test_tradermocker_matching, legacy_position_loads_as_pre_available_position)
{
	TempFile posFile("tradermocker_legacy_position");
	{
		std::ofstream out(posFile.c_str(), std::ios::binary | std::ios::trunc);
		out << R"({
			"positions": [
				{"exchg": "SSE", "code": "600001", "long": {"volume": 10}, "short": {"volume": 0}}
			]
		})";
	}

	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	trader._bd_mgr = &bdMgr;
	trader._listener = &spi;
	trader._pos_file = posFile.c_str();

	trader.load_positions();
	trader.queryPositions();
	runTraderIo(trader);

	const auto* pos = findPosition(spi, "SSE.600001", WDT_LONG);
	ASSERT_NE(pos, nullptr);
	EXPECT_EQ(pos->pre, 10);
	EXPECT_EQ(pos->availPre, 10);
	EXPECT_EQ(pos->newpos, 0);
	EXPECT_EQ(pos->availNew, 0);
}

TEST(test_tradermocker_matching, t1_stock_open_today_is_not_available_for_same_day_close)
{
	TempFile posFile("tradermocker_t1_stock");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 7);

	WTSContractInfo* contract = bdMgr.getContract("600001", "SSE");
	ASSERT_NE(contract, nullptr);

	WTSEntrust* open = makeEntrust(contract, WDT_LONG, WOT_OPEN, 10);
	open->setPrice(10.0);
	EXPECT_EQ(trader.orderInsert(open), 0);
	open->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_FALSE(spi.rspEntrustErrors.back());

	trader._ticks->add(contract->getFullCode(), makeTick("SSE", "600001", 10.0, 9.9, 100, 10.0, 100), false);
	EXPECT_EQ(trader.match_once(), 1);

	trader.queryPositions();
	runTraderIo(trader);
	const auto* pos = findPosition(spi, "SSE.600001", WDT_LONG);
	ASSERT_NE(pos, nullptr);
	EXPECT_EQ(pos->pre, 0);
	EXPECT_EQ(pos->availPre, 0);
	EXPECT_EQ(pos->newpos, 10);
	EXPECT_EQ(pos->availNew, 0);

	WTSEntrust* close = makeEntrust(contract, WDT_LONG, WOT_CLOSE, 10);
	EXPECT_EQ(trader.orderInsert(close), 0);
	close->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_TRUE(spi.rspEntrustErrors.back());
	EXPECT_EQ(trader._awaits->size(), 0u);
}

TEST(test_tradermocker_matching, cover_today_future_close_uses_pre_and_close_today_requires_new)
{
	TempFile posFile("tradermocker_cover_today_future");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 8);

	WTSContractInfo* contract = bdMgr.getContract("rb2601", "SHFE");
	ASSERT_NE(contract, nullptr);
	setLongPosition(trader, contract, 10, 0);

	WTSEntrust* closeToday = makeEntrust(contract, WDT_LONG, WOT_CLOSETODAY, 5);
	EXPECT_EQ(trader.orderInsert(closeToday), 0);
	closeToday->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_TRUE(spi.rspEntrustErrors.back());
	EXPECT_EQ(trader._awaits->size(), 0u);

	WTSEntrust* closePre = makeEntrust(contract, WDT_LONG, WOT_CLOSE, 5);
	EXPECT_EQ(trader.orderInsert(closePre), 0);
	closePre->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_FALSE(spi.rspEntrustErrors.back());
	EXPECT_EQ(trader._awaits->size(), 1u);
}

TEST(test_tradermocker_matching, cover_today_future_close_yesterday_uses_pre_bucket)
{
	TempFile posFile("tradermocker_close_yesterday_future");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 11);

	WTSContractInfo* contract = bdMgr.getContract("rb2601", "SHFE");
	ASSERT_NE(contract, nullptr);
	setLongPositionBuckets(trader, contract, 6, 0, 4, 0);

	WTSEntrust* closeYesterday = makeEntrust(contract, WDT_LONG, WOT_CLOSEYESTERDAY, 5);
	EXPECT_EQ(trader.orderInsert(closeYesterday), 0);
	closeYesterday->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_FALSE(spi.rspEntrustErrors.back());
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long._pre_frozen, 5);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long._new_frozen, 0);

	trader._ticks->add(contract->getFullCode(), makeTick("SHFE", "rb2601", 100, 100, 5, 101, 5), false);
	EXPECT_EQ(trader.match_once(), 1);

	const TraderMocker::PosUnit& afterFill = trader._positions[contract->getFullCode()]._long;
	EXPECT_EQ(afterFill._pre_volume, 1);
	EXPECT_EQ(afterFill._pre_frozen, 0);
	EXPECT_EQ(afterFill._new_volume, 4);
	EXPECT_EQ(afterFill._new_frozen, 0);
}

TEST(test_tradermocker_matching, unfinished_cover_mode_closes_pre_then_new)
{
	TempFile posFile("tradermocker_unfinished_cover");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 12);

	WTSContractInfo* contract = bdMgr.getContract("u2601", "CZCE");
	ASSERT_NE(contract, nullptr);
	setLongPositionBuckets(trader, contract, 2, 0, 3, 0);

	WTSEntrust* close = makeEntrust(contract, WDT_LONG, WOT_CLOSE, 4);
	EXPECT_EQ(trader.orderInsert(close), 0);
	close->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_FALSE(spi.rspEntrustErrors.back());
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long._pre_frozen, 2);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long._new_frozen, 2);

	trader._ticks->add(contract->getFullCode(), makeTick("CZCE", "u2601", 100, 100, 4, 101, 4), false);
	EXPECT_EQ(trader.match_once(), 1);

	const TraderMocker::PosUnit& afterFill = trader._positions[contract->getFullCode()]._long;
	EXPECT_EQ(afterFill._pre_volume, 0);
	EXPECT_EQ(afterFill._pre_frozen, 0);
	EXPECT_EQ(afterFill._new_volume, 1);
	EXPECT_EQ(afterFill._new_frozen, 0);
}

TEST(test_tradermocker_matching, new_position_format_round_trips_pre_and_new_buckets)
{
	TempFile posFile("tradermocker_new_position_format");
	{
		std::ofstream out(posFile.c_str(), std::ios::binary | std::ios::trunc);
		out << R"({
			"trading_date": )" << TimeUtils::getCurDate() << R"(,
			"positions": [
				{
					"exchg": "SHFE",
					"code": "rb2601",
					"long": {
						"volume": 13,
						"pre": {"volume": 10, "frozen": 2},
						"new": {"volume": 3, "frozen": 1}
					},
					"short": {"volume": 0}
				}
			]
		})";
	}

	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	trader._bd_mgr = &bdMgr;
	trader._listener = &spi;
	trader._pos_file = posFile.c_str();

	trader.load_positions();
	trader.queryPositions();
	runTraderIo(trader);

	const auto* pos = findPosition(spi, "SHFE.rb2601", WDT_LONG);
	ASSERT_NE(pos, nullptr);
	EXPECT_EQ(pos->pre, 10);
	EXPECT_EQ(pos->availPre, 10);
	EXPECT_EQ(pos->newpos, 3);
	EXPECT_EQ(pos->availNew, 3);

	trader.save_positions();
	const std::string json = readFile(posFile.c_str());
	EXPECT_NE(json.find("\"pre\""), std::string::npos);
	EXPECT_NE(json.find("\"new\""), std::string::npos);
	EXPECT_NE(json.find("\"frozen\""), std::string::npos);

	TraderMocker reloaded;
	RecordingTraderSpi reloadedSpi(&bdMgr);
	reloaded._bd_mgr = &bdMgr;
	reloaded._listener = &reloadedSpi;
	reloaded._pos_file = posFile.c_str();
	reloaded.load_positions();
	reloaded.queryPositions();
	runTraderIo(reloaded);

	const auto* reloadedPos = findPosition(reloadedSpi, "SHFE.rb2601", WDT_LONG);
	ASSERT_NE(reloadedPos, nullptr);
	EXPECT_EQ(reloadedPos->pre, 10);
	EXPECT_EQ(reloadedPos->availPre, 10);
	EXPECT_EQ(reloadedPos->newpos, 3);
	EXPECT_EQ(reloadedPos->availNew, 3);
}

TEST(test_tradermocker_matching, load_positions_releases_persisted_frozen_without_orders)
{
	TempFile posFile("tradermocker_release_persisted_frozen");
	{
		std::ofstream out(posFile.c_str(), std::ios::binary | std::ios::trunc);
		out << R"({
			"trading_date": )" << TimeUtils::getCurDate() << R"(,
			"positions": [
				{
					"exchg": "DCE",
					"code": "m2601",
					"long": {
						"volume": 10,
						"pre": {"volume": 6, "frozen": 4},
						"new": {"volume": 4, "frozen": 3}
					},
					"short": {"volume": 0}
				}
			]
		})";
	}

	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	trader._bd_mgr = &bdMgr;
	trader._listener = &spi;
	trader._pos_file = posFile.c_str();

	trader.load_positions();
	trader.queryPositions();
	runTraderIo(trader);

	const auto* pos = findPosition(spi, "DCE.m2601", WDT_LONG);
	ASSERT_NE(pos, nullptr);
	EXPECT_EQ(pos->pre, 6);
	EXPECT_EQ(pos->availPre, 6);
	EXPECT_EQ(pos->newpos, 4);
	EXPECT_EQ(pos->availNew, 4);
	EXPECT_EQ(trader._positions["DCE.m2601"]._long.total_frozen(), 0);
}

TEST(test_tradermocker_matching, load_positions_rolls_new_to_pre_on_next_trading_day)
{
	TempFile posFile("tradermocker_roll_new_to_pre");
	{
		std::ofstream out(posFile.c_str(), std::ios::binary | std::ios::trunc);
		out << R"({
			"trading_date": 20000101,
			"positions": [
				{
					"exchg": "SHFE",
					"code": "rb2601",
					"long": {
						"volume": 7,
						"pre": {"volume": 2, "frozen": 1},
						"new": {"volume": 5, "frozen": 2}
					},
					"short": {"volume": 0}
				}
			]
		})";
	}

	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	trader._bd_mgr = &bdMgr;
	trader._listener = &spi;
	trader._pos_file = posFile.c_str();

	trader.load_positions();
	trader.queryPositions();
	runTraderIo(trader);

	const auto* pos = findPosition(spi, "SHFE.rb2601", WDT_LONG);
	ASSERT_NE(pos, nullptr);
	EXPECT_EQ(pos->pre, 7);
	EXPECT_EQ(pos->availPre, 7);
	EXPECT_EQ(pos->newpos, 0);
	EXPECT_EQ(pos->availNew, 0);
}

TEST(test_tradermocker_matching, open_cover_close_freezes_pre_then_new_and_cancel_releases_remaining)
{
	TempFile posFile("tradermocker_open_cover_freeze");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 9);

	WTSContractInfo* contract = bdMgr.getContract("m2601", "DCE");
	ASSERT_NE(contract, nullptr);
	setLongPositionBuckets(trader, contract, 3, 0, 4, 0);

	WTSEntrust* close = makeEntrust(contract, WDT_LONG, WOT_CLOSE, 5);
	EXPECT_EQ(trader.orderInsert(close), 0);
	close->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_FALSE(spi.rspEntrustErrors.back());
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long._pre_frozen, 3);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long._new_frozen, 2);
	ASSERT_EQ(trader._frozen_orders.size(), 1u);

	trader._ticks->add(contract->getFullCode(), makeTick("DCE", "m2601", 100, 100, 2, 101, 2), false);
	EXPECT_EQ(trader.match_once(), 1);
	TraderMocker::PosUnit& afterFill = trader._positions[contract->getFullCode()]._long;
	EXPECT_EQ(afterFill._pre_volume, 1);
	EXPECT_EQ(afterFill._pre_frozen, 1);
	EXPECT_EQ(afterFill._new_volume, 4);
	EXPECT_EQ(afterFill._new_frozen, 2);

	ASSERT_EQ(trader._awaits->size(), 1u);
	WTSOrderInfo* order = (WTSOrderInfo*)trader._awaits->begin()->second;
	WTSEntrustAction* action = WTSEntrustAction::create(contract->getCode(), contract->getExchg());
	ASSERT_NE(action, nullptr);
	action->setContractInfo(contract);
	action->setOrderID(order->getOrderID());
	EXPECT_EQ(trader.orderAction(action), 0);
	action->release();
	runTraderIo(trader);

	const TraderMocker::PosUnit& afterCancel = trader._positions[contract->getFullCode()]._long;
	EXPECT_EQ(afterCancel._pre_volume, 1);
	EXPECT_EQ(afterCancel._pre_frozen, 0);
	EXPECT_EQ(afterCancel._new_volume, 4);
	EXPECT_EQ(afterCancel._new_frozen, 0);
	EXPECT_TRUE(trader._frozen_orders.empty());
}

TEST(test_tradermocker_matching, cm_none_close_without_position_is_accepted_and_matched)
{
	TempFile posFile("tradermocker_cm_none");
	TestBaseDataMgr bdMgr;
	RecordingTraderSpi spi(&bdMgr);
	TraderMocker trader;
	prepareTrader(trader, &bdMgr, &spi, posFile.c_str(), 10);

	WTSContractInfo* contract = bdMgr.getContract("TEST", "CME");
	ASSERT_NE(contract, nullptr);

	WTSEntrust* close = makeEntrust(contract, WDT_LONG, WOT_CLOSE, 3);
	EXPECT_EQ(trader.orderInsert(close), 0);
	close->release();
	runTraderIo(trader);
	ASSERT_FALSE(spi.rspEntrustErrors.empty());
	EXPECT_FALSE(spi.rspEntrustErrors.back());
	ASSERT_EQ(trader._awaits->size(), 1u);

	trader._ticks->add(contract->getFullCode(), makeTick("CME", "TEST", 100, 100, 3, 101, 3), false);
	EXPECT_EQ(trader.match_once(), 1);
	EXPECT_EQ(trader._awaits->size(), 0u);
	EXPECT_EQ(trader._positions[contract->getFullCode()]._long.total_volume(), 0);
	ASSERT_EQ(spi.trades.size(), 1u);
	EXPECT_EQ(spi.trades[0], "CME.TEST");
}
}
