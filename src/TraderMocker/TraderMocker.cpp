#include "TraderMocker.h"

#include "../Includes/WTSVariant.hpp"
#include "../Includes/WTSDataDef.hpp"
#include "../Includes/WTSTradeDef.hpp"
#include "../Includes/WTSContractInfo.hpp"
#include "../Includes/IBaseDataMgr.h"

#include "../Share/TimeUtils.hpp"
#include "../Share/decimal.h"
#include "../Share/StrUtil.hpp"
#include "../Share/FilesystemCompat.hpp"

#include <boost/bind/bind.hpp>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
namespace rj = rapidjson;

//By Wesley @ 2022.01.05
#include "../Share/fmtlib.h"
#include <algorithm>
#include <limits>
template<typename... Args>
inline void write_log(ITraderSpi* sink, WTSLogLevel ll, const char* format, const Args&... args)
{
	if (sink == NULL)
		return;

	const char* buffer = fmtutil::format(format, args...);

	sink->handleTraderLog(ll, buffer);
}

namespace
{
double openVolumeLotStep(WTSCommodityInfo* commInfo) noexcept
{
	if (commInfo == NULL)
		return 0.0;

	const double lotsTick = commInfo->getLotsTick();
	if (decimal::gt(lotsTick))
		return lotsTick;

	return 0.0;
}

double openVolumeMinLots(WTSCommodityInfo* commInfo) noexcept
{
	if (commInfo == NULL)
		return 0.0;

	const double minLots = commInfo->getMinLots();
	if (decimal::gt(minLots))
		return minLots;

	return openVolumeLotStep(commInfo);
}

bool isValidOpenVolumeLot(WTSCommodityInfo* commInfo, double volume) noexcept
{
	const double step = openVolumeLotStep(commInfo);
	const double minLots = openVolumeMinLots(commInfo);
	return decimal::gt(step) && !decimal::lt(volume, minLots) && decimal::eq(decimal::mod(volume, step), 0);
}

uint32_t volumeStepToUInt(double step) noexcept
{
	if (!decimal::gt(step))
		return 0;

	if (decimal::lt(step, 1.0))
		return 1;

	return static_cast<uint32_t>(step + 0.5);
}

uint32_t alignVolumeDown(uint32_t volume, uint32_t step) noexcept
{
	if (step <= 1)
		return volume;

	return volume / step * step;
}

uint32_t alignVolumeUp(uint32_t volume, uint32_t step) noexcept
{
	if (step <= 1)
		return volume;

	const uint32_t remainder = volume % step;
	if (remainder == 0)
		return volume;

	const uint32_t delta = step - remainder;
	if (volume > std::numeric_limits<uint32_t>::max() - delta)
		return alignVolumeDown(std::numeric_limits<uint32_t>::max(), step);

	return volume + delta;
}

uint32_t volumeToUIntFloor(double volume) noexcept
{
	if (!decimal::gt(volume))
		return 0;

	const double maxVolume = static_cast<double>(std::numeric_limits<uint32_t>::max());
	if (decimal::gt(volume, maxVolume))
		return std::numeric_limits<uint32_t>::max();

	return static_cast<uint32_t>(volume);
}

bool isValidCloseVolumeLot(WTSCommodityInfo* commInfo, double validQty, double volume) noexcept
{
	const double step = openVolumeLotStep(commInfo);
	if (!decimal::gt(step) || decimal::le(step, 1.0))
		return true;

	if (decimal::eq(decimal::mod(volume, step), 0))
		return true;

	const double oddLots = decimal::mod(validQty, step);
	if (!decimal::gt(oddLots))
		return false;

	return decimal::eq(decimal::mod(volume, step), oddLots);
}

uint32_t matchVolumeStep(WTSOrderInfo* ordInfo, WTSCommodityInfo* commInfo) noexcept
{
	const uint32_t lotStep = volumeStepToUInt(openVolumeLotStep(commInfo));
	if (lotStep <= 1 || ordInfo == NULL)
		return 1;

	if (ordInfo->getOffsetType() == WOT_OPEN)
		return lotStep;

	// Dirty simulated positions may be odd lots. Let close orders clear them.
	return decimal::eq(decimal::mod(ordInfo->getVolLeft(), static_cast<double>(lotStep)), 0) ? lotStep : 1;
}

}

extern "C"
{
	EXPORT_FLAG ITraderApi* createTrader()
	{
		TraderMocker *instance = new TraderMocker();
		return instance;
	}

	EXPORT_FLAG void deleteTrader(ITraderApi* &trader)
	{
		if (NULL != trader)
		{
			delete trader;
			trader = NULL;
		}
	}
}

std::vector<uint32_t> splitVolume(uint32_t vol, uint32_t minQty = 1, uint32_t maxQty = 100, uint32_t qtyTick = 1)
{
	std::vector<uint32_t> ret;
	if (vol == 0)
		return ret;

	if (qtyTick == 0)
		qtyTick = 1;

	vol = alignVolumeDown(vol, qtyTick);
	if (vol == 0)
		return ret;

	minQty = std::max(minQty, qtyTick);
	minQty = alignVolumeUp(minQty, qtyTick);

	if (maxQty == 0)
		maxQty = minQty;

	maxQty = std::max(maxQty, minQty);
	maxQty = alignVolumeDown(maxQty, qtyTick);
	if (maxQty < minQty)
		maxQty = minQty;

	uint32_t length = (maxQty - minQty) / qtyTick + 1;
	if (vol <= minQty)
	{
		ret.emplace_back(vol);
	}
	else
	{
		uint32_t left = vol;
		srand((uint32_t)time(NULL));
		while (left > 0)
		{
			uint32_t curVol = minQty + ((uint32_t)rand() % length) * qtyTick;

			if (curVol >= left)
				curVol = left;

			if (curVol == 0)
				continue;

			ret.emplace_back(curVol);
			left -= curVol;
		}
	}

	return ret;
}
TraderMocker::TraderMocker()
	: _terminated(false)
	, _listener(NULL)
	, _ticks(NULL)
	, _orders(NULL)
	, _awaits(NULL)
	, _trades(NULL)
	, _b_socket(NULL)
	, _max_tick_time(0)
	, _last_match_time(0)
	, _init_balance(0)
{
	_auto_order_id = (uint32_t)((TimeUtils::getLocalTimeNow() - TimeUtils::makeTime(20200101, 0)) / 1000 * 100);
	_auto_trade_id = (uint32_t)((TimeUtils::getLocalTimeNow() - TimeUtils::makeTime(20200101, 0)) / 1000 * 300);
	_auto_entrust_id = (uint32_t)((TimeUtils::getLocalTimeNow() - TimeUtils::makeTime(20200101, 0)) / 1000 * 100);
}


TraderMocker::~TraderMocker()
{
	if (_orders)
		_orders->release();

	if (_trades)
		_trades->release();

	if (_ticks)
		_ticks->release();

	if (_awaits)
		_awaits->release();
}

uint32_t TraderMocker::makeTradeID()
{
	return ++_auto_trade_id;
}

uint32_t TraderMocker::makeOrderID()
{
	return ++_auto_order_id;
}

bool TraderMocker::makeEntrustID(char* buffer, int length)
{
	if (buffer == NULL || length == 0)
		return false;

	try
	{
		fmtutil::format_to(buffer, "me.{}.{}.{}", TimeUtils::getCurDate(), _mocker_id, _auto_entrust_id++);
		return true;
	}
	catch (...)
	{

	}

	return false;
}

bool TraderMocker::has_await_order(const char* fullcode) const
{
	if (fullcode == NULL || _awaits == NULL)
		return false;

	for (auto it = _awaits->begin(); it != _awaits->end(); it++)
	{
		WTSOrderInfo* ordInfo = (WTSOrderInfo*)it->second;
		if (ordInfo == NULL || decimal::le(ordInfo->getVolLeft(), 0))
			continue;

		WTSContractInfo* ct = ordInfo->getContractInfo();
		if (ct != NULL && strcmp(ct->getFullCode(), fullcode) == 0)
			return true;

		if (ct == NULL && strlen(ordInfo->getExchg()) > 0 && strlen(ordInfo->getCode()) > 0)
		{
			thread_local static char orderFullCode[64] = { 0 };
			fmtutil::format_to(orderFullCode, "{}.{}", ordInfo->getExchg(), ordInfo->getCode());
			if (strcmp(orderFullCode, fullcode) == 0)
				return true;
		}
	}

	return false;
}

void TraderMocker::refresh_await_code(const char* fullcode)
{
	if (fullcode == NULL || strlen(fullcode) == 0)
		return;

	if (!has_await_order(fullcode))
		_codes.erase(fullcode);
}

int TraderMocker::orderInsert(WTSEntrust* entrust)
{
	if (entrust == NULL)
	{
		return 0;
	}

	entrust->retain();
	boost::asio::post(_io_service, [this, entrust](){
		WTSContractInfo* ct = entrust->getContractInfo();
		if(ct == NULL)
			ct = _bd_mgr->getContract(entrust->getCode(), entrust->getExchg());

		/*
		 *	1、开仓无需检查
		 *	2、平仓要先检查可平
		 *	3、检查通过了,平仓还要冻结持仓
		 *	4、还要考虑国际期货的问题
		 */

		bool bPass = false;
		std::string msg;
		WTSOrderInfo* ordInfo = NULL;
		uint32_t code_count = 0;
		FrozenItem pendingFrozen;
		bool hasPendingFrozen = false;

		{
			StdUniqueLock awaits_lock(_mtx_awaits);
			do
			{
				if (ct == NULL)
				{
					bPass = false;
					msg = "品种不存在";
					break;
				}
				WTSCommodityInfo* commInfo = ct->getCommInfo();

				//检查价格类型的合法性
				if (entrust->getPriceType() == WPT_ANYPRICE && commInfo->getPriceMode() == PM_Limit)
				{
					bPass = false;
					msg = "价格类型不合法";
					break;
				}

				//检查数量的合法性
				if (entrust->getOffsetType() == WOT_OPEN && !isValidOpenVolumeLot(commInfo, entrust->getVolume()))
				{
					bPass = false;
					const double volumeStep = openVolumeLotStep(commInfo);
					const double minLots = openVolumeMinLots(commInfo);
					msg = decimal::gt(volumeStep) ? fmtutil::format("买入数量必须不小于{}且为{}的整数倍", minLots, volumeStep) : "品种数量规则不合法";
					break;
				}

				//检查方向的合法性
				if(!commInfo->canShort() && entrust->getDirection() == WDT_SHORT)
				{
					bPass = false;
					msg = "股票不能做空";
					break;
				}

				//检查价格的合法性
				if(!decimal::eq(entrust->getPrice(), 0))
				{
					double pricetick = commInfo->getPriceTick();

					if (!decimal::eq(decimal::mod(entrust->getPrice(), pricetick), 0))	//整除的检查方式,先小数相除得到商,然后商取整以后,再跟原来的商相减,如果等于0,则是整除,否则是
					{
						bPass = false;
						msg = "委托价格不合法";
						break;
					}
				}


				//开仓直接通过,不检查资金
				if (entrust->getOffsetType() == WOT_OPEN)
				{
					bPass = true;
					break;
				}

				//如果不需要开平,则直接通过,主要针对国际期货
				if (commInfo->getCoverMode() == CM_None)
				{
					bPass = true;
					break;
				}

				auto logCloseReject = [&](const char* reason, const PosItem* pItem, bool positionChecked) {
					if(pItem != NULL)
					{
						const double longValidQty = pItem->_long.pre_avail() + (commInfo->isT1() ? 0 : pItem->_long.new_avail());
						const double shortValidQty = pItem->_short.pre_avail() + (commInfo->isT1() ? 0 : pItem->_short.new_avail());
						write_log(_listener, LL_ERROR,
							"[TraderMocker]close position rejected: reason={}, fullCode={}, raw_code={}, raw_exchg={}, covermode={}, offset_type={}, direction={}, entrust_volume={}, long_volume={}, long_frozen={}, long_validQty={}, short_volume={}, short_frozen={}, short_validQty={}",
							reason, ct->getFullCode(), entrust->getCode(), entrust->getExchg(), static_cast<int>(commInfo->getCoverMode()), static_cast<int>(entrust->getOffsetType()),
							static_cast<int>(entrust->getDirection()), entrust->getVolume(), pItem->_long.total_volume(), pItem->_long.total_frozen(), longValidQty, pItem->_short.total_volume(),
							pItem->_short.total_frozen(), shortValidQty);
					}
					else if(positionChecked)
					{
						write_log(_listener, LL_ERROR,
							"[TraderMocker]close position rejected: reason={}, fullCode={}, raw_code={}, raw_exchg={}, covermode={}, offset_type={}, direction={}, entrust_volume={}, positions_size={}",
							reason, ct->getFullCode(), entrust->getCode(), entrust->getExchg(), static_cast<int>(commInfo->getCoverMode()), static_cast<int>(entrust->getOffsetType()),
							static_cast<int>(entrust->getDirection()), entrust->getVolume(), _positions.size());
					}
					else
					{
						write_log(_listener, LL_ERROR,
							"[TraderMocker]close position rejected: reason={}, fullCode={}, raw_code={}, raw_exchg={}, covermode={}, offset_type={}, direction={}, entrust_volume={}",
							reason, ct->getFullCode(), entrust->getCode(), entrust->getExchg(), static_cast<int>(commInfo->getCoverMode()), static_cast<int>(entrust->getOffsetType()),
							static_cast<int>(entrust->getDirection()), entrust->getVolume());
					}
				};

				//如果没有持仓或者持仓不够,也要
				auto it = _positions.find(ct->getFullCode());
				if(it == _positions.end())
				{
					logCloseReject("position_missing", NULL, true);
					bPass = false;
					msg = "没有足够的可平仓位";
					break;
				}

				PosItem& pItem = (PosItem&)it->second;
				bool isLong = entrust->getDirection() == WDT_LONG;
				PosUnit& pUnit = isLong ? pItem._long : pItem._short;

				auto calcValidQty = [&]() {
					if (commInfo->getCoverMode() == CM_CoverToday)
					{
						return entrust->getOffsetType() == WOT_CLOSETODAY ? pUnit.new_avail() : pUnit.pre_avail();
					}

					if (commInfo->isT1())
						return pUnit.pre_avail();

					return pUnit.pre_avail() + pUnit.new_avail();
				};

				double validQty = calcValidQty();
				if(decimal::lt(validQty, entrust->getVolume()))
				{
					logCloseReject("volume_frozen_insufficient", &pItem, true);
					bPass = false;
					msg = "没有足够的可平仓位";
					break;
				}

				if (!isValidCloseVolumeLot(commInfo, validQty, entrust->getVolume()))
				{
					bPass = false;
					const double volumeStep = openVolumeLotStep(commInfo);
					msg = decimal::gt(volumeStep) ? fmtutil::format("卖出数量必须符合{}的整手规则，零股部分只能一次性卖出", volumeStep) : "品种数量规则不合法";
					break;
				}

				//冻结持仓
				pendingFrozen = FrozenItem();
				strcpy(pendingFrozen._fullcode, ct->getFullCode());
				pendingFrozen._direction = entrust->getDirection();
				double left = entrust->getVolume();
				auto freezePre = [&]() {
					const double cur = std::min(left, pUnit.pre_avail());
					if (decimal::gt(cur, 0))
					{
						pUnit._pre_frozen += cur;
						pendingFrozen._pre += cur;
						left -= cur;
					}
				};
				auto freezeNew = [&]() {
					const double cur = std::min(left, pUnit.new_avail());
					if (decimal::gt(cur, 0))
					{
						pUnit._new_frozen += cur;
						pendingFrozen._new += cur;
						left -= cur;
					}
				};

				if (commInfo->getCoverMode() == CM_CoverToday)
				{
					if (entrust->getOffsetType() == WOT_CLOSETODAY)
						freezeNew();
					else
						freezePre();
				}
				else if (commInfo->isT1())
					freezePre();
				else
				{
					freezePre();
					freezeNew();
				}
				hasPendingFrozen = decimal::gt(pendingFrozen.total(), 0);

				bPass = true;
				msg = "下单成功";

			} while (false);

			if(bPass)
			{
				ordInfo = WTSOrderInfo::create();
				ordInfo->setContractInfo(ct);
				ordInfo->setCode(entrust->getCode());
				ordInfo->setExchange(entrust->getExchg());
				ordInfo->setDirection(entrust->getDirection());
				ordInfo->setOffsetType(entrust->getOffsetType());
				ordInfo->setUserTag(entrust->getUserTag());
				ordInfo->setPrice(entrust->getPrice());
				thread_local static char str[64];
				fmtutil::format_to(str, "mo.{}.{}", _mocker_id, makeOrderID());
				ordInfo->setOrderID(str);
				ordInfo->setStateMsg(msg.c_str());
				ordInfo->setOrderState(WOS_NotTraded_Queuing);
				ordInfo->setOrderTime(TimeUtils::getLocalTimeNow());
				ordInfo->setVolume(entrust->getVolume());
				ordInfo->setVolLeft(entrust->getVolume());
				ordInfo->setPriceType(entrust->getPriceType());
				ordInfo->setOrderFlag(entrust->getOrderFlag());

				_codes.insert(ct->getFullCode());
				code_count = _codes.size();

				if (_orders == NULL)
					_orders = WTSArray::create();
				_orders->append(ordInfo, false);

				if (_awaits == NULL)
					_awaits = OrderCache::create();

				_awaits->add(ordInfo->getOrderID(), ordInfo, true);
				if (hasPendingFrozen)
					_frozen_orders[ordInfo->getOrderID()] = pendingFrozen;

				save_positions();
			}
		}

		if(bPass)
		{
			if (_listener != NULL)
			{
				StdUniqueLock lock(_mutex_api);
				_listener->onRspEntrust(entrust, NULL);
				_listener->onPushOrder(ordInfo);
				write_log(_listener, LL_INFO, "共有{}个品种有待撮合订单", code_count);
			}
		}
		else
		{
			WTSError* err = WTSError::create(WEC_ORDERINSERT, msg.c_str());
			if (_listener != NULL)
			{
				StdUniqueLock lock(_mutex_api);
				_listener->onRspEntrust(entrust, err);
			}
			err->release();
		}
		entrust->release();
	});

	return 0;
}

int32_t TraderMocker::match_once()
{
	StdUniqueLock state_lock(_mtx_awaits);
	if (_terminated || _orders == NULL || _orders->size() == 0 || _ticks == NULL || _awaits == NULL)
		return 0;

	int32_t count = 0;
	std::vector<std::string> codes_to_refresh;
	for (const std::string& fullcode : _codes)
	{
		if (_terminated)
			return count;

		std::string code, exchg;
		auto pos = fullcode.find(".");
		exchg = fullcode.substr(0, pos);
		code = fullcode.substr(pos + 1);

		WTSContractInfo* ct = _bd_mgr->getContract(code.c_str(), exchg.c_str());
		if (ct == NULL)
			continue;

		WTSTickData* curTick = (WTSTickData*)_ticks->grab(fullcode);
		if (curTick && strcmp(curTick->code(), ct->getCode()) == 0 && strcmp(curTick->exchg(), ct->getExchg()) == 0)
		{
			uint64_t tickTime = (uint64_t)curTick->actiondate() * 1000000000 + curTick->actiontime();
			if (decimal::gt(curTick->price(), 0) /*&& tickTime >= _last_match_time*/)
			{
				//开始处理订单
				//处理记录
				std::vector<std::string> to_erase;

				for (auto it = _awaits->begin(); it != _awaits->end(); it++)
				{
					WTSOrderInfo* ordInfo = (WTSOrderInfo*)it->second;
					WTSContractInfo* orderCt = ordInfo->getContractInfo();
					if (orderCt == NULL)
					{
						orderCt = _bd_mgr->getContract(ordInfo->getCode(), ordInfo->getExchg());
						if (orderCt != NULL)
							ordInfo->setContractInfo(orderCt);
					}
					if (orderCt == NULL)
					{
						write_log(_listener, LL_ERROR, "[TraderMocker]order contract missing: orderid={}", ordInfo->getOrderID());
						continue;
					}

					if (strcmp(orderCt->getFullCode(), fullcode.c_str()) != 0)
						continue;

					if (decimal::le(ordInfo->getVolLeft(), 0))
					{
						if (decimal::lt(ordInfo->getVolLeft(), 0))
						{
							write_log(_listener, LL_WARN,
								"[TraderMocker]order left volume corrected before match: orderid={}, fullcode={}, left={}",
								ordInfo->getOrderID(), orderCt->getFullCode(), ordInfo->getVolLeft());
						}
						ordInfo->setVolLeft(0);
						ordInfo->setVolTraded(ordInfo->getVolume());
						ordInfo->setOrderState(WOS_AllTraded);
						ordInfo->setStateMsg("AllTrd");
						to_erase.emplace_back(ordInfo->getOrderID());
						continue;
					}

					WTSCommodityInfo* orderCommInfo = orderCt->getCommInfo();
					if (orderCommInfo == NULL)
					{
						write_log(_listener, LL_ERROR, "[TraderMocker]order commodity missing: orderid={}, fullcode={}", ordInfo->getOrderID(), orderCt->getFullCode());
						continue;
					}

					bool isBuy = (ordInfo->getDirection() == WDT_LONG && ordInfo->getOffsetType() == WOT_OPEN) || (ordInfo->getDirection() != WDT_LONG && ordInfo->getOffsetType() != WOT_OPEN);

					double uPrice, uVolume;
					if (isBuy)
					{
						uPrice = curTick->askprice(0);
						uVolume = curTick->askqty(0);
					}
					else
					{
						uPrice = curTick->bidprice(0);
						uVolume = curTick->bidqty(0);
					}

					if (decimal::eq(uVolume, 0))
						continue;

					if (_use_newpx)
						uPrice = curTick->price();

					if (decimal::eq(uPrice, 0))
						continue;

					double target = ordInfo->getPrice();
					//买入的时候,委托价格小于最新价则不成交,卖出的时候,委托价大于最新价则不成交
					if (ordInfo->getPriceType() == WPT_LIMITPRICE && ((isBuy && decimal::lt(target, uPrice)) || (!isBuy && decimal::gt(target, uPrice))))
						continue;

					PosItem& pItem = _positions[orderCt->getFullCode()];
					//第一次的话要给代码和交易所赋值
					if(strlen(pItem._code) == 0)
					{
						strcpy(pItem._code, orderCt->getCode());
						strcpy(pItem._exchg, orderCt->getExchg());
					}

					if (orderCommInfo->getCoverMode() != CM_None && ordInfo->getOffsetType() != WOT_OPEN)
					{
						const PosUnit& pUnit = ordInfo->getDirection() == WDT_LONG ? pItem._long : pItem._short;
						auto frozenIt = _frozen_orders.find(ordInfo->getOrderID());
						const double frozenAvailable = frozenIt == _frozen_orders.end() ? pUnit.total_frozen() : frozenIt->second.total();
						const double available = std::min(pUnit.total_volume(), frozenAvailable);
						if (decimal::le(available, 0))
						{
							write_log(_listener, LL_ERROR,
								"[TraderMocker]close match skipped: position not available, orderid={}, fullcode={}, volume={}, frozen={}",
								ordInfo->getOrderID(), orderCt->getFullCode(), pUnit.total_volume(), pUnit.total_frozen());
							continue;
						}
						uVolume = std::min(uVolume, available);
					}

					uint32_t maxVolume = (uint32_t)min(uVolume, ordInfo->getVolLeft());
					uint32_t volumeStep = matchVolumeStep(ordInfo, orderCommInfo);
					uint32_t minVolume = volumeStep <= 1 ? 1 : volumeStep;
					maxVolume = alignVolumeDown(maxVolume, volumeStep);
					if (maxVolume == 0 || maxVolume < minVolume)
						continue;

					std::vector<uint32_t> ayVol = splitVolume(maxVolume, std::max((uint32_t)_min_qty, minVolume), (uint32_t)_max_qty, volumeStep);
					if (ayVol.empty())
						continue;

					count++;
					for (uint32_t curVol : ayVol)
					{
						const double volLeftBefore = ordInfo->getVolLeft();
						uint32_t maxCurVol = volumeToUIntFloor(volLeftBefore);
						if (volumeStep > 1)
							maxCurVol = alignVolumeDown(maxCurVol, volumeStep);

						if (maxCurVol == 0)
							break;

						if (curVol > maxCurVol)
						{
							write_log(_listener, LL_WARN,
								"[TraderMocker]match volume clipped: orderid={}, fullcode={}, expect={}, left={}",
								ordInfo->getOrderID(), orderCt->getFullCode(), curVol, volLeftBefore);
							curVol = maxCurVol;
						}

						WTSTradeInfo* trade = WTSTradeInfo::create(orderCt->getCode(), orderCt->getExchg());
						trade->setDirection(ordInfo->getDirection());
						trade->setOffsetType(ordInfo->getOffsetType());
						trade->setContractInfo(orderCt);

						trade->setPrice(uPrice);
						trade->setVolume(curVol);

						trade->setRefOrder(ordInfo->getOrderID());

						char str[64];
						fmtutil::format_to(str, "mt.{}.{}", _mocker_id, makeTradeID());
						trade->setTradeID(str);

						trade->setTradeTime(TimeUtils::getLocalTimeNow());
						trade->setUserTag(ordInfo->getUserTag());

						//更新订单数据
						const double volLeftAfter = std::max(0.0, volLeftBefore - curVol);
						ordInfo->setVolLeft(volLeftAfter);
						ordInfo->setVolTraded(std::max(0.0, ordInfo->getVolume() - volLeftAfter));
						const bool allTraded = decimal::le(ordInfo->getVolLeft(), 0);
						if (allTraded)
						{
							ordInfo->setVolLeft(0);
							ordInfo->setOrderState(WOS_AllTraded);
							ordInfo->setStateMsg("AllTrd");
							to_erase.emplace_back(ordInfo->getOrderID());
						}
						else
						{
							ordInfo->setOrderState(WOS_PartTraded_Queuing);
							ordInfo->setStateMsg("PartTrd");
						}

						if(orderCommInfo->getCoverMode() == CM_None)
						{

						}
						else
						{
							if (ordInfo->getDirection() == WDT_LONG)
							{
								if (ordInfo->getOffsetType() == WOT_OPEN)
								{
									pItem._long._new_volume += curVol;
								}
								else
								{
									double left = curVol;
									auto consumePre = [&]() {
										const double cur = std::min(left, pItem._long._pre_frozen);
										if (decimal::gt(cur, 0))
										{
											pItem._long._pre_volume -= cur;
											pItem._long._pre_frozen -= cur;
											left -= cur;
										}
										return cur;
									};
									auto consumeNew = [&]() {
										const double cur = std::min(left, pItem._long._new_frozen);
										if (decimal::gt(cur, 0))
										{
											pItem._long._new_volume -= cur;
											pItem._long._new_frozen -= cur;
											left -= cur;
										}
										return cur;
									};

									auto frozenIt = _frozen_orders.find(ordInfo->getOrderID());
									if (frozenIt != _frozen_orders.end())
									{
										const double preBefore = pItem._long._pre_frozen;
										const double preTake = std::min(left, frozenIt->second._pre);
										const double preCur = std::min(preTake, preBefore);
										if (decimal::gt(preCur, 0))
										{
											pItem._long._pre_volume -= preCur;
											pItem._long._pre_frozen -= preCur;
											frozenIt->second._pre -= preCur;
											left -= preCur;
										}
										const double newBefore = pItem._long._new_frozen;
										const double newTake = std::min(left, frozenIt->second._new);
										const double newCur = std::min(newTake, newBefore);
										if (decimal::gt(newCur, 0))
										{
											pItem._long._new_volume -= newCur;
											pItem._long._new_frozen -= newCur;
											frozenIt->second._new -= newCur;
											left -= newCur;
										}
									}
									else if (ordInfo->getOffsetType() == WOT_CLOSETODAY)
									{
										consumeNew();
									}
									else
									{
										consumePre();
										consumeNew();
									}
								}
							}
							else
							{
								if (ordInfo->getOffsetType() == WOT_OPEN)
								{
									pItem._short._new_volume += curVol;
								}
								else
								{
									double left = curVol;
									auto consumePre = [&]() {
										const double cur = std::min(left, pItem._short._pre_frozen);
										if (decimal::gt(cur, 0))
										{
											pItem._short._pre_volume -= cur;
											pItem._short._pre_frozen -= cur;
											left -= cur;
										}
										return cur;
									};
									auto consumeNew = [&]() {
										const double cur = std::min(left, pItem._short._new_frozen);
										if (decimal::gt(cur, 0))
										{
											pItem._short._new_volume -= cur;
											pItem._short._new_frozen -= cur;
											left -= cur;
										}
										return cur;
									};

									auto frozenIt = _frozen_orders.find(ordInfo->getOrderID());
									if (frozenIt != _frozen_orders.end())
									{
										const double preBefore = pItem._short._pre_frozen;
										const double preTake = std::min(left, frozenIt->second._pre);
										const double preCur = std::min(preTake, preBefore);
										if (decimal::gt(preCur, 0))
										{
											pItem._short._pre_volume -= preCur;
											pItem._short._pre_frozen -= preCur;
											frozenIt->second._pre -= preCur;
											left -= preCur;
										}
										const double newBefore = pItem._short._new_frozen;
										const double newTake = std::min(left, frozenIt->second._new);
										const double newCur = std::min(newTake, newBefore);
										if (decimal::gt(newCur, 0))
										{
											pItem._short._new_volume -= newCur;
											pItem._short._new_frozen -= newCur;
											frozenIt->second._new -= newCur;
											left -= newCur;
										}
									}
									else if (ordInfo->getOffsetType() == WOT_CLOSETODAY)
									{
										consumeNew();
									}
									else
									{
										consumePre();
										consumeNew();
									}
								}
							}
						}

						if (_listener)
						{
							StdUniqueLock lock(_mutex_api);
							_listener->onPushOrder(ordInfo);
							_listener->onPushTrade(trade);
						}

						if (_trades == NULL)
							_trades = WTSArray::create();

						_trades->append(trade, false);

						if (allTraded)
							break;
					}
				}

				if (!to_erase.empty())
				{
					//write_log(_listener,LL_INFO, "[TraderMocker]触发 %s.%s 开多 %u 条,价格:%u", tick->exchg(), tick->code(), iCount, uPrice);
					for (const std::string& oid : to_erase)
					{
						_awaits->remove(oid);
						_frozen_orders.erase(oid);
					}
					codes_to_refresh.emplace_back(fullcode);
				}
			}
		}

		if(curTick)
			curTick->release();
	}

	_last_match_time = _max_tick_time;

	for (const std::string& fullcode : codes_to_refresh)
		refresh_await_code(fullcode.c_str());

	_ticks->clear();

	if(count > 0)
		save_positions();

	return count;
}

//////////////////////////////////////////////////////////////////////////

bool TraderMocker::init(WTSVariant *params)
{
	_millisecs = params->getUInt32("span");
	_use_newpx = params->getBoolean("newpx");
	_mocker_id = params->getUInt32("mockerid");
	_init_balance = params->getDouble("init_balance");
	_max_qty = params->getDouble("maxqty");
	_min_qty = params->getDouble("minqty");

	_udp_port = params->getInt32("udp_port");

	boost::asio::ip::address addr = boost::asio::ip::make_address("0.0.0.0");
	_broad_ep = boost::asio::ip::udp::endpoint(addr, _udp_port);

	if (decimal::le(_max_qty, 0))
		_max_qty = 100;

	if (decimal::le(_min_qty, 0))
		_min_qty = 1;

	if (decimal::lt(_max_qty, _min_qty))
		_max_qty = _min_qty;

	//加载持仓数据
	std::stringstream ss;
	ss << "./mocker_" << _mocker_id << "/";
	std::string path = ss.str();
	wt::fs::create_directories(path.c_str());

	_pos_file = path;
	_pos_file += "positions.json";

	return true;
}

void TraderMocker::load_positions()
{
	_positions.clear();
	_frozen_orders.clear();

	if (!wt::fs::exists(_pos_file.c_str()))
		return;

	std::string json;
	StdFile::read_file_content(_pos_file.c_str(), json);

	rj::Document root;
	root.Parse(json.c_str());
	if (root.HasParseError())
		return;

	uint32_t savedDate = 0;
	if (root.HasMember("trading_date") && root["trading_date"].IsUint())
		savedDate = root["trading_date"].GetUint();
	const uint32_t curDate = TimeUtils::getCurDate();
	const bool rollNewToPre = savedDate != 0 && savedDate < curDate;

	if(root.HasMember("positions"))
	{//读取仓位
		const rj::Value& jPos = root["positions"];
		if (!jPos.IsNull() && jPos.IsArray())
		{
			for (const rj::Value& pItem : jPos.GetArray())
			{
				if (!pItem.IsObject() || !pItem.HasMember("exchg") || !pItem["exchg"].IsString() || !pItem.HasMember("code") || !pItem["code"].IsString()
					|| !pItem.HasMember("long") || !pItem["long"].IsObject()
					|| !pItem.HasMember("short") || !pItem["short"].IsObject())
				{
					write_log(_listener, LL_ERROR, "[TraderMocker]invalid position item found while loading {}", _pos_file.c_str());
					continue;
				}

				const char* exchg = pItem["exchg"].GetString();
				const char* code = pItem["code"].GetString();
				WTSContractInfo* ct = _bd_mgr->getContract(code, exchg);
				if (ct == NULL)
					continue;

				auto parseBucket = [&](const rj::Value& side, const char* name, PosUnit& unit) {
					auto parsePart = [&](const rj::Value& parent, const char* part, double& volume, double& frozen) {
						if (!parent.HasMember(part))
							return true;
						const rj::Value& item = parent[part];
						if (!item.IsObject() || !item.HasMember("volume") || !item["volume"].IsNumber())
							return false;
						volume = item["volume"].GetDouble();
						frozen = (item.HasMember("frozen") && item["frozen"].IsNumber()) ? item["frozen"].GetDouble() : 0;
						return true;
					};

					if (side.HasMember("pre") || side.HasMember("new"))
					{
						if (!parsePart(side, "pre", unit._pre_volume, unit._pre_frozen) || !parsePart(side, "new", unit._new_volume, unit._new_frozen))
						{
							write_log(_listener, LL_ERROR, "[TraderMocker]invalid {} position bucket ignored while loading: fullcode={}", name, ct->getFullCode());
							return false;
						}
					}
					else
					{
						if (!side.HasMember("volume") || !side["volume"].IsNumber())
						{
							write_log(_listener, LL_ERROR, "[TraderMocker]invalid legacy {} position ignored while loading: fullcode={}", name, ct->getFullCode());
							return false;
						}
						unit._pre_volume = side["volume"].GetDouble();
					}

					if (decimal::lt(unit._pre_volume, 0) || decimal::lt(unit._new_volume, 0) || decimal::lt(unit._pre_frozen, 0) || decimal::lt(unit._new_frozen, 0)
						|| decimal::gt(unit._pre_frozen, unit._pre_volume) || decimal::gt(unit._new_frozen, unit._new_volume))
					{
						write_log(_listener, LL_ERROR,
							"[TraderMocker]invalid {} position ignored while loading: fullcode={}, pre_volume={}, pre_frozen={}, new_volume={}, new_frozen={}",
							name, ct->getFullCode(), unit._pre_volume, unit._pre_frozen, unit._new_volume, unit._new_frozen);
						return false;
					}

					// 冻结量只由当前进程内的未完成订单维护；订单不持久化，重启加载时必须释放。
					unit._pre_frozen = 0;
					unit._new_frozen = 0;

					return true;
				};

				auto rollUnit = [](PosUnit& unit) {
					unit._pre_volume += unit._new_volume;
					unit._new_volume = 0;
					unit._pre_frozen = 0;
					unit._new_frozen = 0;
				};

				PosItem pInfo;
				strcpy(pInfo._code, ct->getCode());
				strcpy(pInfo._exchg, ct->getExchg());
				if (!parseBucket(pItem["long"], "long", pInfo._long) || !parseBucket(pItem["short"], "short", pInfo._short))
					continue;

				if (rollNewToPre)
				{
					rollUnit(pInfo._long);
					rollUnit(pInfo._short);
				}

				if (decimal::eq(pInfo._long.total_volume(), 0) && decimal::eq(pInfo._short.total_volume(), 0))
					continue;

				_positions[ct->getFullCode()] = pInfo;
			}
		}
	}

	if (_listener)
		write_log(_listener, LL_INFO, "[TraderMocker]共加载{}条持仓数据", _positions.size());
}

void TraderMocker::save_positions()
{
	rj::Document root(rj::kObjectType);
	rj::Document::AllocatorType &allocator = root.GetAllocator();
	root.AddMember("trading_date", TimeUtils::getCurDate(), allocator);

	{//持仓数据保存
		rj::Value jPos(rj::kArrayType);

		for (auto& v : _positions)
		{
			const char* fullcode = v.first.c_str();
			const PosItem& pInfo = v.second;
			if (strlen(pInfo._exchg) == 0 || strlen(pInfo._code) == 0)
			{
				write_log(_listener, LL_ERROR, "[TraderMocker]position with empty code ignored while saving: key={}", fullcode);
				continue;
			}

			auto validUnit = [](const PosUnit& unit) {
				return !decimal::lt(unit._pre_volume, 0) && !decimal::lt(unit._new_volume, 0) && !decimal::lt(unit._pre_frozen, 0) && !decimal::lt(unit._new_frozen, 0)
					&& !decimal::gt(unit._pre_frozen, unit._pre_volume) && !decimal::gt(unit._new_frozen, unit._new_volume);
			};

			if (!validUnit(pInfo._long) || !validUnit(pInfo._short))
			{
				write_log(_listener, LL_ERROR,
					"[TraderMocker]invalid position ignored while saving: key={}, long_volume={}, long_frozen={}, short_volume={}, short_frozen={}",
					fullcode, pInfo._long.total_volume(), pInfo._long.total_frozen(), pInfo._short.total_volume(), pInfo._short.total_frozen());
				continue;
			}

			if (decimal::eq(pInfo._long.total_volume(), 0) && decimal::eq(pInfo._short.total_volume(), 0))
				continue;

			rj::Value pItem(rj::kObjectType);
			pItem.AddMember("exchg", rj::Value(pInfo._exchg, allocator), allocator);
			pItem.AddMember("code", rj::Value(pInfo._code, allocator), allocator);

			auto addUnit = [&](rj::Value& parent, const char* name, const PosUnit& unit) {
				rj::Value dItem(rj::kObjectType);
				dItem.AddMember("volume", unit.total_volume(), allocator);
				dItem.AddMember("frozen", 0, allocator);

				rj::Value preItem(rj::kObjectType);
				preItem.AddMember("volume", unit._pre_volume, allocator);
				preItem.AddMember("frozen", 0, allocator);
				dItem.AddMember("pre", preItem, allocator);

				rj::Value newItem(rj::kObjectType);
				newItem.AddMember("volume", unit._new_volume, allocator);
				newItem.AddMember("frozen", 0, allocator);
				dItem.AddMember("new", newItem, allocator);

				parent.AddMember(rj::Value(name, allocator), dItem, allocator);
			};

			{
				addUnit(pItem, "long", pInfo._long);
			}

			{
				addUnit(pItem, "short", pInfo._short);
			}


			jPos.PushBack(pItem, allocator);
		}

		root.AddMember("positions", jPos, allocator);
	}

	{
		rj::StringBuffer sb;
		rj::PrettyWriter<rj::StringBuffer> writer(sb);
		root.Accept(writer);
		StdFile::write_file_content(_pos_file.c_str(), sb.GetString());

	}
}

void TraderMocker::release()
{
	if (_terminated)
		return;

	_terminated = true;

	if (_thrd_match)
	{
		_thrd_match->join();
	}

	if (_thrd_worker)
	{
		_io_service.stop();
		_thrd_worker->join();
	}
}

void TraderMocker::registerSpi(ITraderSpi *listener)
{
	_listener = listener;

	_bd_mgr = listener->getBaseDataMgr();
}

void TraderMocker::reconn_udp()
{
	if (_b_socket != NULL)
	{
		_b_socket->close();
		delete _b_socket;
		_b_socket = NULL;
	}

	_b_socket = new boost::asio::ip::udp::socket(_io_service);

	_b_socket->open(_broad_ep.protocol());
	_b_socket->set_option(boost::asio::ip::udp::socket::reuse_address(true));
	_b_socket->set_option(boost::asio::ip::udp::socket::broadcast(true));
	_b_socket->bind(_broad_ep);


	_b_socket->async_receive_from(boost::asio::buffer(_b_buffer), _broad_ep,
		boost::bind(&TraderMocker::handle_read, this,
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred, true));
}

void TraderMocker::connect()
{
	reconn_udp();

	_thrd_worker.reset(new StdThread(boost::bind(&boost::asio::io_context::run, &_io_service)));

	boost::asio::post(_io_service, [this](){
		//_positions/_frozen_orders 由 _mtx_awaits 保护(与 match_once/orderInsert 一致)。
		//load_positions 会 clear+重建哈希表(rehash),必须持有 _mtx_awaits,
		//否则与撮合线程 match_once 并发改写同一张哈希表,导致桶数组重分配时越界崩溃。
		StdUniqueLock state_lock(_mtx_awaits);
		StdUniqueLock lock(_mutex_api);

		load_positions();

		if (_listener)
			_listener->handleEvent(WTE_Connect, 0);
	});
}

void TraderMocker::disconnect()
{
	if (_terminated)
		return;

	_terminated = true;

	if (_thrd_match)
	{
		_thrd_match->join();
	}
}

bool TraderMocker::isConnected()
{
	return _thrd_match != NULL;
}

int TraderMocker::login(const char* user, const char* pass, const char* productInfo)
{
	_thrd_match.reset(new StdThread([this]() {
		while (!_terminated)
		{
			match_once();

			//等待5毫秒
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}));

	boost::asio::post(_io_service, [this](){
		StdUniqueLock lock(_mutex_api);

		if (_listener)
			_listener->onLoginResult(true, "", TimeUtils::getCurDate());
	});

	return 0;
}

int TraderMocker::logout()
{
	return 0;
}

int TraderMocker::orderAction(WTSEntrustAction* action)
{
	if (action == NULL)
		return 0;

	action->retain();

	boost::asio::post(_io_service, [this, action](){
		StdUniqueLock lck(_mtx_awaits);	//一定要把awaits锁起来,不然可能会导致一边撮合一边撤单
		std::string orderID = action->getOrderID();
		WTSOrderInfo* ordInfo = _awaits ? (WTSOrderInfo*)_awaits->grab(orderID) : NULL;

		/*
		 *	撤单也要考虑几个问题
		 *	1、是否处于可以撤销的状态
		 *	2、如果是开仓,则直接撤销
		 *	3、如果是平仓,要释放冻结
		 */
		if(ordInfo == NULL)
		{
			WTSError* err = WTSError::create(WEC_ORDERCANCEL, "订单不存在或者处于不可撤销状态");
			if (_listener)
			{
				StdUniqueLock lock(_mutex_api);
				write_log(_listener, LL_ERROR, "订单{}不存在或者已完成", orderID.c_str());
				_listener->onTraderError(err);
			}
			err->release();
			action->release();
			return;
		}

		WTSContractInfo* ct = ordInfo->getContractInfo();
		if (ct == NULL)
		{
			ct = _bd_mgr->getContract(ordInfo->getCode(), ordInfo->getExchg());
			if (ct != NULL)
				ordInfo->setContractInfo(ct);
		}
		if (ct == NULL)
		{
			WTSError* err = WTSError::create(WEC_ORDERCANCEL, "订单合约不存在");
			if (_listener)
			{
				StdUniqueLock lock(_mutex_api);
				write_log(_listener, LL_ERROR, "订单{}合约不存在,撤单失败", orderID.c_str());
				_listener->onTraderError(err);
			}
			err->release();
			ordInfo->release();
			action->release();
			return;
		}
		std::string fullcode = ct->getFullCode();
		WTSCommodityInfo* commInfo = ct->getCommInfo();
		if (commInfo == NULL)
		{
			WTSError* err = WTSError::create(WEC_ORDERCANCEL, "订单品种不存在");
			if (_listener)
			{
				StdUniqueLock lock(_mutex_api);
				write_log(_listener, LL_ERROR, "订单{}品种不存在,撤单失败", orderID.c_str());
				_listener->onTraderError(err);
			}
			err->release();
			ordInfo->release();
			action->release();
			return;
		}

		bool bPass = false;
		do 
		{
			//开仓委托直接撤单
			if (ordInfo->getOffsetType() == WOT_OPEN)
			{
				bPass = true;
				break;
			}
			
			//不区分开平的,也直接撤销
			if (commInfo->getCoverMode() == CM_None)
			{
				bPass = true;
				break;
			}

			//释放冻结持仓
			PosItem& pItem = _positions[ct->getFullCode()];
			bool isLong = ordInfo->getDirection() == WDT_LONG;
			PosUnit& pUnit = isLong ? pItem._long : pItem._short;
			auto frozenIt = _frozen_orders.find(orderID);
			if (frozenIt != _frozen_orders.end())
			{
				const double preRelease = std::min(pUnit._pre_frozen, frozenIt->second._pre);
				const double newRelease = std::min(pUnit._new_frozen, frozenIt->second._new);
				pUnit._pre_frozen -= preRelease;
				pUnit._new_frozen -= newRelease;
				_frozen_orders.erase(frozenIt);
			}
			else
			{
				double left = ordInfo->getVolLeft();
				auto releasePre = [&]() {
					const double cur = std::min(left, pUnit._pre_frozen);
					if (decimal::gt(cur, 0))
					{
						pUnit._pre_frozen -= cur;
						left -= cur;
					}
				};
				auto releaseNew = [&]() {
					const double cur = std::min(left, pUnit._new_frozen);
					if (decimal::gt(cur, 0))
					{
						pUnit._new_frozen -= cur;
						left -= cur;
					}
				};

				if (ordInfo->getOffsetType() == WOT_CLOSETODAY)
					releaseNew();
				else
				{
					releasePre();
					releaseNew();
				}

				if (decimal::gt(left, 0))
				{
					write_log(_listener, LL_ERROR, "[TraderMocker]cancel releases more than frozen: orderid={}, fullcode={}, frozen={}, vol_left={}",
						orderID.c_str(), ct->getFullCode(), pUnit.total_frozen(), ordInfo->getVolLeft());
				}
			}
			bPass = true;

		} while (false);

		ordInfo->setStateMsg("撤单成功");
		ordInfo->setOrderState(WOS_Canceled);
		//ordInfo->setVolLeft(0);	

		if (_listener)
		{
			StdUniqueLock lock(_mutex_api);
			_listener->onPushOrder(ordInfo);
		}

		if (_awaits)
			_awaits->remove(orderID);
		_frozen_orders.erase(orderID);

		if (!fullcode.empty())
			refresh_await_code(fullcode.c_str());

		ordInfo->release();
		action->release();
		save_positions();
	});

	return 0;
}

int TraderMocker::queryAccount()
{
	boost::asio::post(_io_service, [this](){
		WTSArray* ay = WTSArray::create();
		WTSAccountInfo* accountInfo = WTSAccountInfo::create();
		accountInfo->setCurrency("CNY");
		accountInfo->setBalance(_init_balance);
		accountInfo->setPreBalance(_init_balance);
		accountInfo->setCloseProfit(0);
		accountInfo->setMargin(0);
		accountInfo->setAvailable(_init_balance);
		accountInfo->setCommission(0);
		accountInfo->setFrozenMargin(0);
		accountInfo->setFrozenCommission(0);
		accountInfo->setDeposit(0);
		accountInfo->setWithdraw(0);
		accountInfo->setDynProfit(0);

		ay->append(accountInfo, false);

		if (_listener)
		{
			StdUniqueLock lock(_mutex_api);
			_listener->onRspAccount(ay);
		}

		ay->release();

	});
	return 0;
}

int TraderMocker::queryPositions()
{
	boost::asio::post(_io_service, [this](){
		StdUniqueLock state_lock(_mtx_awaits);
		WTSArray* ayPos = WTSArray::create();

		for(auto& v : _positions)
		{
			const PosItem& pItem = v.second;

			WTSContractInfo* ct = _bd_mgr->getContract(pItem._code, pItem._exchg);
			if(ct == NULL)
				continue;

			WTSCommodityInfo* commInfo = ct->getCommInfo();

			if(pItem._long.total_volume() > 0)
			{
				WTSPositionItem* pInfo = WTSPositionItem::create(pItem._code, commInfo->getCurrency(), pItem._exchg);
				pInfo->setContractInfo(ct);
				pInfo->setDirection(WDT_LONG);
				pInfo->setPrePosition(pItem._long._pre_volume);
				pInfo->setAvailPrePos(pItem._long.pre_avail());
				pInfo->setNewPosition(pItem._long._new_volume);
				pInfo->setAvailNewPos(commInfo->isT1() ? 0 : pItem._long.new_avail());

				ayPos->append(pInfo, false);
			}

			if (pItem._short.total_volume() > 0)
			{
				WTSPositionItem* pInfo = WTSPositionItem::create(pItem._code, commInfo->getCurrency(), pItem._exchg);
				pInfo->setContractInfo(ct);
				pInfo->setDirection(WDT_SHORT);
				pInfo->setPrePosition(pItem._short._pre_volume);
				pInfo->setAvailPrePos(pItem._short.pre_avail());
				pInfo->setNewPosition(pItem._short._new_volume);
				pInfo->setAvailNewPos(commInfo->isT1() ? 0 : pItem._short.new_avail());

				ayPos->append(pInfo, false);
			}
		}

		if (_listener)
		{
			StdUniqueLock lock(_mutex_api);
			_listener->onRspPosition(ayPos);
		}
		ayPos->release();
	});

	return 0;
}

int TraderMocker::queryOrders()
{
	boost::asio::post(_io_service, [this](){
		StdUniqueLock state_lock(_mtx_awaits);
		StdUniqueLock lock(_mutex_api);

		if (_listener)
			_listener->onRspOrders(_orders);
	});

	return 0;
}

int TraderMocker::queryTrades()
{
	boost::asio::post(_io_service, [this](){
		StdUniqueLock state_lock(_mtx_awaits);
		StdUniqueLock lock(_mutex_api);

		if (_listener)
			_listener->onRspTrades(_trades);
	});

	return 0;
}

void TraderMocker::handle_read(const boost::system::error_code& e, std::size_t bytes_transferred, bool isBroad /* = true */)
{
	if (e)
	{
		if (_listener)
			write_log(_listener,LL_ERROR, "[TraderMocker]UDP行情接收出错:{}({})", e.message().c_str(), e.value());

		if (!_terminated)
		{
			std::this_thread::sleep_for(std::chrono::seconds(2));
			reconn_udp();
			return;
		}
	}

	if (_terminated || bytes_transferred <= 0)
		return;

	extract_buffer(bytes_transferred, isBroad);

	if (isBroad && _b_socket)
	{
		_b_socket->async_receive_from(boost::asio::buffer(_b_buffer), _broad_ep,
			boost::bind(&TraderMocker::handle_read, this,
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred, true));
	}
}

#define UDP_MSG_PUSHTICK	0x200
#pragma pack(push,1)

typedef struct UDPPacketHead
{
	uint32_t		_type;
} UDPPacketHead;
//UDP请求包
typedef struct _UDPReqPacket : UDPPacketHead
{
	char			_data[1020];
} UDPReqPacket;

//UDPTick数据包
template <typename T>
struct UDPDataPacket : UDPPacketHead
{
	T			_data;
};
#pragma pack(pop)
typedef UDPDataPacket<WTSTickStruct>	UDPTickPacket;
void TraderMocker::extract_buffer(uint32_t length, bool isBroad /* = true */)
{
	UDPPacketHead* header = (UDPTickPacket*)_b_buffer.data();

	if (header->_type == UDP_MSG_PUSHTICK)
	{
		UDPTickPacket* packet = (UDPTickPacket*)header;
		thread_local static char fullcode[64] = { 0 };
		fmtutil::format_to(fullcode, "{}.{}", packet->_data.exchg, packet->_data.code);
		StdUniqueLock state_lock(_mtx_awaits);
		auto it = _codes.find(fullcode);
		if (it == _codes.end())
			return;

		WTSTickData* curTick = WTSTickData::create(packet->_data);
		
		if (_ticks == NULL)
			_ticks = TickCache::create();

		_ticks->add(fullcode, curTick, false);

		_max_tick_time = max(_max_tick_time, (uint64_t)curTick->actiondate() * 1000000000 + curTick->actiontime());
	}
}
