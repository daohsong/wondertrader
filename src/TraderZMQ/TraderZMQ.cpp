#include "TraderZMQ.h"

#include "../Includes/IBaseDataMgr.h"
#include "../Includes/WTSCollection.hpp"
#include "../Includes/WTSDataDef.hpp"
#include "../Includes/WTSContractInfo.hpp"
#include "../Includes/WTSTradeDef.hpp"
#include "../Includes/WTSVariant.hpp"
#include "../Share/TimeUtils.hpp"
#include "../Share/fmtlib.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <vector>

using namespace std::chrono_literals;

extern "C"
{
	EXPORT_FLAG ITraderApi* createTrader()
	{
		return new TraderZMQ();
	}

	EXPORT_FLAG void deleteTrader(ITraderApi*& trader)
	{
		if (trader != nullptr)
		{
			delete trader;
			trader = nullptr;
		}
	}
}

namespace
{
	inline const char* level_name(WTSLogLevel ll)
	{
		switch (ll)
		{
		case LL_DEBUG: return "DEBUG";
		case LL_INFO: return "INFO";
		case LL_WARN: return "WARN";
		case LL_ERROR: return "ERROR";
		case LL_FATAL: return "FATAL";
		default: return "UNKNOWN";
		}
	}

	template<typename... Args>
	inline void write_boot_log(WTSLogLevel ll, const char* fmtstr, const Args&... args)
	{
		try
		{
			const std::string msg = fmt::format(fmt::runtime(fmtstr), args...);
			std::fprintf(stderr, "[TraderZMQ][%s] %s\n", level_name(ll), msg.c_str());
		}
		catch (...)
		{
			std::fprintf(stderr, "[TraderZMQ][%s] %s\n", level_name(ll), fmtstr);
		}
	}

	template<typename... Args>
	inline void write_log(ITraderSpi* sink, WTSLogLevel ll, const char* fmtstr, const Args&... args)
	{
		if (ll == LL_DEBUG)
		{
			try
			{
				std::string msg = fmt::format(fmt::runtime(fmtstr), args...);
				std::fprintf(stderr, "[TraderZMQ][%s] %s\n", level_name(ll), msg.c_str());
			}
			catch (...)
			{
				std::fprintf(stderr, "[TraderZMQ][%s] %s\n", level_name(ll), fmtstr);
			}
			return;
		}

		if (sink == nullptr)
			return;

		try
		{
			std::string msg = fmt::format(fmt::runtime(fmtstr), args...);
			sink->handleTraderLog(ll, msg.c_str());
		}
		catch (...)
		{
			sink->handleTraderLog(ll, fmtstr);
		}
	}

	std::string make_endpoint(const std::string& host, int32_t port)
	{
		return fmtutil::format("tcp://{}:{}", host, port);
	}

	std::string upper_copy(const std::string& src)
	{
		std::string ret = src;
		std::transform(ret.begin(), ret.end(), ret.begin(), [](unsigned char c) { return (char)std::toupper(c); });
		return ret;
	}

	std::string trim_trailing_zeros(std::string text)
	{
		const size_t dot = text.find('.');
		if (dot == std::string::npos)
			return text;

		while (!text.empty() && text.back() == '0')
			text.pop_back();
		if (!text.empty() && text.back() == '.')
			text.pop_back();
		if (text == "-0")
			text = "0";
		return text;
	}

	std::string format_number(double value, int precision)
	{
		if (!std::isfinite(value))
			return fmt::format("{}", value);

		char buffer[64] = {0};
		std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
		return trim_trailing_zeros(buffer);
	}

	std::string get_string(const rapidjson::Value& obj, std::initializer_list<const char*> keys)
	{
		for (const char* key : keys)
		{
			if (!obj.HasMember(key))
				continue;

			const auto& value = obj[key];
			if (value.IsString())
				return value.GetString();
			if (value.IsInt64())
				return fmtutil::format("{}", value.GetInt64());
			if (value.IsUint64())
				return fmtutil::format("{}", value.GetUint64());
			if (value.IsInt())
				return fmtutil::format("{}", value.GetInt());
			if (value.IsUint())
				return fmtutil::format("{}", value.GetUint());
		}

		return {};
	}

	double get_number(const rapidjson::Value& obj, std::initializer_list<const char*> keys, double defVal = 0.0)
	{
		for (const char* key : keys)
		{
			if (!obj.HasMember(key))
				continue;

			const auto& value = obj[key];
			if (value.IsNumber())
				return value.GetDouble();
			if (value.IsString())
				return std::atof(value.GetString());
		}

		return defVal;
	}

	int64_t get_int64(const rapidjson::Value& obj, std::initializer_list<const char*> keys, int64_t defVal = 0)
	{
		for (const char* key : keys)
		{
			if (!obj.HasMember(key))
				continue;

			const auto& value = obj[key];
			if (value.IsInt64())
				return value.GetInt64();
			if (value.IsUint64())
				return (int64_t)value.GetUint64();
			if (value.IsInt())
				return value.GetInt();
			if (value.IsUint())
				return value.GetUint();
			if (value.IsString())
				return std::strtoll(value.GetString(), nullptr, 10);
		}

		return defVal;
	}

	uint64_t get_uint64(const rapidjson::Value& obj, std::initializer_list<const char*> keys, uint64_t defVal = 0)
	{
		for (const char* key : keys)
		{
			if (!obj.HasMember(key))
				continue;

			const auto& value = obj[key];
			if (value.IsUint64())
				return value.GetUint64();
			if (value.IsInt64() && value.GetInt64() > 0)
				return (uint64_t)value.GetInt64();
			if (value.IsUint())
				return value.GetUint();
			if (value.IsInt() && value.GetInt() > 0)
				return (uint64_t)value.GetInt();
			if (value.IsString())
				return std::strtoull(value.GetString(), nullptr, 10);
		}

		return defVal;
	}

	bool has_any(const rapidjson::Value& obj, std::initializer_list<const char*> keys)
	{
		for (const char* key : keys)
		{
			if (obj.HasMember(key))
				return true;
		}

		return false;
	}

	int64_t elapsed_ms(uint64_t beginNs, uint64_t endNs)
	{
		if (beginNs == 0 || endNs < beginNs)
			return -1;
		return (int64_t)((endNs - beginNs) / 1000000ULL);
	}

	std::string summarize_request(const rapidjson::Document& req, const char* rawJson)
	{
		const std::string reqType = get_string(req, {"type"});
		if (reqType.empty())
			return rawJson == nullptr ? std::string() : std::string(rawJson);

		std::string summary = fmt::format("type={}", reqType);
		auto append_text = [&summary](const char* key, const std::string& value) {
			if (!value.empty())
				summary += fmt::format(" {}={}", key, value);
		};
		auto append_number = [&summary](const char* key, double value, int precision) {
			summary += fmt::format(" {}={}", key, format_number(value, precision));
		};

		append_text("req_id", get_string(req, {"req_id"}));
		append_text("user_tag", get_string(req, {"user_tag"}));
		append_text("account_id", get_string(req, {"account_id"}));

		if (reqType == "order_req")
		{
			append_text("exchg", get_string(req, {"exchg"}));
			append_text("code", get_string(req, {"code"}));
			append_text("direction", get_string(req, {"direction"}));
			append_text("offset", get_string(req, {"offset"}));
			if (has_any(req, {"price"}))
				append_number("price", get_number(req, {"price"}), 6);
			if (has_any(req, {"volume"}))
				append_number("volume", get_number(req, {"volume"}), 4);
			append_text("price_type", get_string(req, {"price_type"}));
			append_text("order_flag", get_string(req, {"order_flag"}));
			return summary;
		}

		if (reqType == "cancel_req")
		{
			append_text("order_id", get_string(req, {"order_id"}));
			return summary;
		}

		return rawJson == nullptr ? summary : std::string(rawJson);
	}

	void normalize_code_fields(const rapidjson::Value& obj, std::string& exchg, std::string& code)
	{
		exchg = upper_copy(get_string(obj, {"exchg", "exchange"}));
		code = get_string(obj, {"code", "full_code", "raw_code"});

		if (code.empty())
			return;

		if (code.find('.') != std::string::npos)
		{
			std::vector<std::string> parts;
			size_t begin = 0;
			while (begin <= code.size())
			{
				size_t pos = code.find('.', begin);
				if (pos == std::string::npos)
				{
					parts.emplace_back(code.substr(begin));
					break;
				}

				parts.emplace_back(code.substr(begin, pos - begin));
				begin = pos + 1;
			}

			if (parts.size() >= 3)
			{
				if (exchg.empty())
					exchg = upper_copy(parts.front());
				code = parts.back();
			}
			else if (parts.size() == 2)
			{
				const std::string suffix = upper_copy(parts.back());
				if (suffix == "SH")
				{
					if (exchg.empty())
						exchg = "SSE";
					code = parts.front();
				}
				else if (suffix == "SZ")
				{
					if (exchg.empty())
						exchg = "SZSE";
					code = parts.front();
				}
			}
		}

		if (exchg == "SH")
			exchg = "SSE";
		else if (exchg == "SZ")
			exchg = "SZSE";
	}

	WTSOrderState map_status(const std::string& raw)
	{
		const std::string s = upper_copy(raw);
		if (s == "WOS_SUBMITTING" || s == "ORDER_UNREPORTED" || s == "ORDER_WAIT_REPORTING")
			return WOS_Submitting;
		if (s == "WOS_NOTTRADED_QUEUING" || s == "ORDER_REPORTED")
			return WOS_NotTraded_Queuing;
		if (s == "WOS_PARTTRADED_QUEUING" || s == "ORDER_PART_SUCC")
			return WOS_PartTraded_Queuing;
		if (s == "WOS_PARTTRADED_NOTQUEUING" || s == "ORDER_PARTSUCC_CANCEL")
			return WOS_PartTraded_NotQueuing;
		if (s == "WOS_ALLTRADED" || s == "ORDER_SUCCEEDED")
			return WOS_AllTraded;
		if (s == "WOS_NOTTRADED_NOTQUEUING")
			return WOS_NotTraded_NotQueuing;
		if (s == "WOS_CANCELED" || s == "ORDER_CANCELED" || s == "ORDER_JUNK")
			return WOS_Canceled;
		if (s == "WOS_CANCELLING")
			return WOS_Cancelling;
		if (s == "WOS_NOTTOUCHED")
			return WOS_Nottouched;
		if (s == "WOS_UNKNOWN" || s == "ORDER_UNKNOWN")
			return WOS_NotTraded_NotQueuing;
		return WOS_NotTraded_NotQueuing;
	}

	WTSDirectionType map_direction(const std::string& raw, WTSDirectionType defVal = WDT_LONG)
	{
		if (raw.empty())
			return defVal;

		const std::string s = upper_copy(raw);
		if (s == "SHORT" || s == "SELL")
			return WDT_SHORT;
		return WDT_LONG;
	}

	WTSOffsetType map_offset(const std::string& raw, WTSOffsetType defVal = WOT_OPEN)
	{
		if (raw.empty())
			return defVal;

		const std::string s = upper_copy(raw);
		if (s == "CLOSE")
			return WOT_CLOSE;
		if (s == "CLOSETODAY" || s == "CLOSE_TODAY")
			return WOT_CLOSETODAY;
		if (s == "CLOSEYESTERDAY" || s == "CLOSE_YESTERDAY")
			return WOT_CLOSEYESTERDAY;
		return WOT_OPEN;
	}

	WTSPriceType map_price_type(const std::string& raw, WTSPriceType defVal = WPT_ANYPRICE)
	{
		if (raw.empty())
			return defVal;

		const std::string s = upper_copy(raw);
		if (s == "LIMIT")
			return WPT_LIMITPRICE;
		if (s == "BEST")
			return WPT_BESTPRICE;
		return WPT_ANYPRICE;
	}

	WTSOrderFlag map_order_flag(const std::string& raw, WTSOrderFlag defVal = WOF_NOR)
	{
		if (raw.empty())
			return defVal;

		const std::string s = upper_copy(raw);
		if (s == "FAK")
			return WOF_FAK;
		if (s == "FOK")
			return WOF_FOK;
		return WOF_NOR;
	}

	void split_ts(uint64_t ts, uint32_t& date, uint32_t& time_ms)
	{
		time_t sec = (time_t)(ts / 1000);
		uint32_t ms = (uint32_t)(ts % 1000);
		struct tm tm_local;
#ifdef _MSC_VER
		localtime_s(&tm_local, &sec);
#else
		localtime_r(&sec, &tm_local);
#endif
		date = (tm_local.tm_year + 1900) * 10000 + (tm_local.tm_mon + 1) * 100 + tm_local.tm_mday;
		time_ms = (tm_local.tm_hour * 10000 + tm_local.tm_min * 100 + tm_local.tm_sec) * 1000 + ms;
	}

	void extract_datetime(const rapidjson::Value& obj, uint32_t& date, uint32_t& time_ms)
	{
		date = 0;
		time_ms = 0;

		const uint64_t ts = get_uint64(obj, {"ts", "timestamp"});
		if (ts != 0)
		{
			split_ts(ts, date, time_ms);
			return;
		}

		date = (uint32_t)get_int64(obj, {"trade_date", "action_date", "order_date", "date"}, 0);
		time_ms = (uint32_t)get_int64(obj, {"trade_time", "action_time", "order_time", "time"}, 0);
		if (time_ms > 0 && time_ms <= 235959)
			time_ms *= 1000;
	}

	bool is_terminal_state(WTSOrderState state)
	{
		return state == WOS_AllTraded || state == WOS_Canceled;
	}

	bool is_buy_action(WTSDirectionType direction, WTSOffsetType offset)
	{
		return (direction == WDT_LONG && offset == WOT_OPEN) || (direction == WDT_SHORT && offset != WOT_OPEN);
	}

	bool nearly_equal(double lhs, double rhs)
	{
		return std::fabs(lhs - rhs) <= 1e-9;
	}

	void merge_missing(TraderZMQ::OrderContext& dst, const TraderZMQ::OrderContext& src)
	{
		if (dst.req_id.empty() && !src.req_id.empty())
			dst.req_id = src.req_id;
		if (dst.order_id.empty() && !src.order_id.empty())
			dst.order_id = src.order_id;
		if (dst.user_tag.empty() && !src.user_tag.empty())
			dst.user_tag = src.user_tag;
		if (dst.exchg.empty() && !src.exchg.empty())
			dst.exchg = src.exchg;
		if (dst.code.empty() && !src.code.empty())
			dst.code = src.code;

		if (!dst.has_direction && src.has_direction)
		{
			dst.direction = src.direction;
			dst.has_direction = true;
		}
		if (!dst.has_offset && src.has_offset)
		{
			dst.offset = src.offset;
			dst.has_offset = true;
		}
		if (!dst.has_price_type && src.has_price_type)
		{
			dst.price_type = src.price_type;
			dst.has_price_type = true;
		}
		if (!dst.has_order_flag && src.has_order_flag)
		{
			dst.order_flag = src.order_flag;
			dst.has_order_flag = true;
		}
		if (!dst.has_price && src.has_price)
		{
			dst.price = src.price;
			dst.has_price = true;
		}
		if (!dst.has_volume && src.has_volume)
		{
			dst.volume = src.volume;
			dst.has_volume = true;
		}

		dst.submitting_emitted = dst.submitting_emitted || src.submitting_emitted;
		dst.terminal_emitted = dst.terminal_emitted || src.terminal_emitted;
		if (!dst.has_last_order_snapshot && src.has_last_order_snapshot)
		{
			dst.has_last_order_snapshot = true;
			dst.last_order_state = src.last_order_state;
			dst.last_vol_traded = src.last_vol_traded;
			dst.last_vol_left = src.last_vol_left;
		}
	}

	void override_with(TraderZMQ::OrderContext& dst, const TraderZMQ::OrderContext& src)
	{
		if (!src.req_id.empty())
			dst.req_id = src.req_id;
		if (!src.order_id.empty())
			dst.order_id = src.order_id;
		if (!src.user_tag.empty())
			dst.user_tag = src.user_tag;
		if (!src.exchg.empty())
			dst.exchg = src.exchg;
		if (!src.code.empty())
			dst.code = src.code;

		if (src.has_direction)
		{
			dst.direction = src.direction;
			dst.has_direction = true;
		}
		if (src.has_offset)
		{
			dst.offset = src.offset;
			dst.has_offset = true;
		}
		if (src.has_price_type)
		{
			dst.price_type = src.price_type;
			dst.has_price_type = true;
		}
		if (src.has_order_flag)
		{
			dst.order_flag = src.order_flag;
			dst.has_order_flag = true;
		}
		if (src.has_price)
		{
			dst.price = src.price;
			dst.has_price = true;
		}
		if (src.has_volume)
		{
			dst.volume = src.volume;
			dst.has_volume = true;
		}

		dst.submitting_emitted = dst.submitting_emitted || src.submitting_emitted;
		dst.terminal_emitted = dst.terminal_emitted || src.terminal_emitted;
		if (src.has_last_order_snapshot)
		{
			dst.has_last_order_snapshot = true;
			dst.last_order_state = src.last_order_state;
			dst.last_vol_traded = src.last_vol_traded;
			dst.last_vol_left = src.last_vol_left;
		}
	}

	TraderZMQ::OrderContext context_from_json(const rapidjson::Value& obj)
	{
		TraderZMQ::OrderContext ctx;
		ctx.req_id = get_string(obj, {"req_id"});
		ctx.order_id = get_string(obj, {"order_id"});
		ctx.user_tag = get_string(obj, {"user_tag"});
		normalize_code_fields(obj, ctx.exchg, ctx.code);

		if (has_any(obj, {"direction"}))
		{
			ctx.direction = map_direction(get_string(obj, {"direction"}));
			ctx.has_direction = true;
		}
		if (has_any(obj, {"offset"}))
		{
			ctx.offset = map_offset(get_string(obj, {"offset"}));
			ctx.has_offset = true;
		}
		if (has_any(obj, {"price_type"}))
		{
			ctx.price_type = map_price_type(get_string(obj, {"price_type"}));
			ctx.has_price_type = true;
		}
		if (has_any(obj, {"order_flag"}))
		{
			ctx.order_flag = map_order_flag(get_string(obj, {"order_flag"}));
			ctx.has_order_flag = true;
		}
		if (has_any(obj, {"price"}))
		{
			ctx.price = get_number(obj, {"price"});
			ctx.has_price = true;
		}
		if (has_any(obj, {"volume", "qty"}))
		{
			ctx.volume = get_number(obj, {"volume", "qty"});
			ctx.has_volume = true;
		}

		return ctx;
	}

	WTSContractInfo* resolve_contract(ITraderSpi* listener, const std::string& code, const std::string& exchg)
	{
		if (listener == nullptr || code.empty() || exchg.empty())
			return nullptr;

		IBaseDataMgr* mgr = listener->getBaseDataMgr();
		if (mgr == nullptr)
			return nullptr;

		return mgr->getContract(code.c_str(), exchg.c_str());
	}

	WTSCommodityInfo* resolve_commodity(ITraderSpi* listener, const std::string& code, const std::string& exchg)
	{
		WTSContractInfo* cInfo = resolve_contract(listener, code, exchg);
		return cInfo == nullptr ? nullptr : cInfo->getCommInfo();
	}

	bool is_stock_like(ITraderSpi* listener, const std::string& code, const std::string& exchg)
	{
		WTSCommodityInfo* commInfo = resolve_commodity(listener, code, exchg);
		return commInfo != nullptr && commInfo->isStock();
	}

	bool is_long_only_stock(ITraderSpi* listener, const std::string& code, const std::string& exchg)
	{
		WTSCommodityInfo* commInfo = resolve_commodity(listener, code, exchg);
		return commInfo != nullptr && commInfo->isStock() && !commInfo->canShort();
	}

	void normalize_stock_side(ITraderSpi* listener, const std::string& code, const std::string& exchg, const std::string& rawDirection, WTSDirectionType& direction, WTSOffsetType& offset)
	{
		if (!is_stock_like(listener, code, exchg))
			return;

		bool sideIsBuy = rawDirection.empty() ? is_buy_action(direction, offset) : !(upper_copy(rawDirection) == "SELL" || upper_copy(rawDirection) == "SHORT");
		if (offset == WOT_OPEN && !sideIsBuy && is_long_only_stock(listener, code, exchg))
			offset = WOT_CLOSE;

		if (offset == WOT_OPEN)
			direction = sideIsBuy ? WDT_LONG : WDT_SHORT;
		else
			direction = sideIsBuy ? WDT_SHORT : WDT_LONG;
	}

	const char* offset_token(WTSOffsetType offset)
	{
		switch (offset)
		{
		case WOT_CLOSE: return "CLOSE";
		case WOT_CLOSETODAY: return "CLOSETODAY";
		case WOT_CLOSEYESTERDAY: return "CLOSEYESTERDAY";
		default: return "OPEN";
		}
	}
}

TraderZMQ::TraderZMQ() = default;

TraderZMQ::~TraderZMQ()
{
	_stopping.store(true);
	stopWorker();
}

bool TraderZMQ::init(WTSVariant* params)
{
	if (params == nullptr)
	{
		write_boot_log(LL_ERROR, "init failed: params is null");
		return false;
	}

	_host = params->getCString("host");
	_req_port = params->getInt32("req_port");
	if (_req_port == 0)
		_req_port = params->getInt32("port");

	_pub_port = params->getInt32("pub_port");
	_account_id = params->getCString("account_id");

	if (_host.empty())
	{
		write_boot_log(LL_ERROR, "init failed: missing required field `host`");
		return false;
	}

	if (_req_port <= 0)
	{
		write_boot_log(LL_ERROR, "init failed: missing required field `req_port` (or legacy alias `port`)");
		return false;
	}

	if (_pub_port <= 0)
	{
		write_boot_log(LL_ERROR, "init failed: missing required field `pub_port`");
		return false;
	}

	if (_account_id.empty())
	{
		write_boot_log(LL_ERROR, "init failed: missing required field `account_id`");
		return false;
	}

	_ack_timeout_ms = params->getInt32("ack_timeout_ms");
	if (_ack_timeout_ms <= 0)
		_ack_timeout_ms = 5000;

	_sub_timeout_ms = params->getInt32("sub_timeout_ms");
	if (_sub_timeout_ms <= 0)
		_sub_timeout_ms = 1000;

	_probe_login = params->getBoolean("probe_login");
	_stats_enable = params->getBoolean("stats_enable");
	_stats_path = params->getCString("stats_path");
	_case_id = params->getCString("case_id", "WTZMQ2_LIVE");
	if (_stats_enable && _stats_path.empty())
		_stats_path = fmtutil::format("/tmp/{}_trader.jsonl", _case_id);
	_order_ack_warn_ms = params->getInt32("order_ack_warn_ms");
	if (_order_ack_warn_ms <= 0)
		_order_ack_warn_ms = 5000;
	_trade_update_warn_ms = params->getInt32("trade_update_warn_ms");
	if (_trade_update_warn_ms <= 0)
		_trade_update_warn_ms = 10000;
	write_boot_log(LL_INFO,
		"init success: host={} req_port={} pub_port={} account_id={} ack_timeout_ms={} sub_timeout_ms={} probe_login={} stats_enable={}",
		_host,
		_req_port,
		_pub_port,
		_account_id,
		_ack_timeout_ms,
		_sub_timeout_ms,
		_probe_login,
		_stats_enable);
	return true;
}

void TraderZMQ::release()
{
	write_log(_listener, LL_INFO, "[TraderZMQ] release called");
	_stopping.store(true);
	stopWorker();
	delete this;
}

void TraderZMQ::registerSpi(ITraderSpi* listener)
{
	_listener = listener;
	write_log(_listener, LL_INFO, "[TraderZMQ] registerSpi listener={}", listener != nullptr ? "set" : "null");
}

bool TraderZMQ::isOperational() const
{
	return _connected.load() && _logged_in.load();
}

void TraderZMQ::connect()
{
	if (_connected.load())
	{
		write_log(_listener, LL_WARN, "[TraderZMQ] connect ignored: already connected");
		return;
	}
	_stopping.store(false);

	write_log(_listener, LL_INFO,
		"[TraderZMQ] connecting host={} req_port={} pub_port={} account_id={}",
		_host.c_str(),
		_req_port,
		_pub_port,
		_account_id.c_str());

	try
	{
		std::lock_guard<StdUniqueMutex> lock(_mtx_sock);
		if (!_ctx)
			_ctx = std::make_unique<zmq::context_t>(1);

		_req = std::make_unique<zmq::socket_t>(*_ctx, zmq::socket_type::req);
		_req->set(zmq::sockopt::linger, 0);
		_req->set(zmq::sockopt::sndtimeo, _ack_timeout_ms);
		_req->set(zmq::sockopt::rcvtimeo, _ack_timeout_ms);
		try
		{
#ifdef ZMQ_REQ_RELAXED
			_req->set(zmq::sockopt::req_relaxed, 1);
#endif
#ifdef ZMQ_REQ_CORRELATE
			_req->set(zmq::sockopt::req_correlate, 1);
#endif
		}
		catch (...)
		{
		}
		_req->connect(make_endpoint(_host, _req_port));

		_sub = std::make_unique<zmq::socket_t>(*_ctx, zmq::socket_type::sub);
		_sub->set(zmq::sockopt::linger, 0);
		_sub->set(zmq::sockopt::rcvtimeo, _sub_timeout_ms);
		_sub->set(zmq::sockopt::subscribe, "");
		_sub->connect(make_endpoint(_host, _pub_port));
	}
	catch (const zmq::error_t& e)
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] connect failed: {}", e.what());
		stopWorker();
		if (_listener)
		{
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying WTE_Connect failure");
			_listener->handleEvent(WTE_Connect, -1);
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notified WTE_Connect failure");
		}
		return;
	}

	_running = true;
	_connected = true;
	startWorker();

	if (_listener)
	{
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying WTE_Connect success");
		_listener->handleEvent(WTE_Connect, 0);
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notified WTE_Connect success");
	}

	write_log(_listener, LL_INFO, "[TraderZMQ] connect success");
}

void TraderZMQ::disconnect()
{
	write_log(_listener, LL_INFO, "[TraderZMQ] disconnect called");
	_stopping.store(true);
	stopWorker();
	if (_listener)
	{
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying WTE_Close");
		_listener->handleEvent(WTE_Close, 0);
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notified WTE_Close");
	}
}

bool TraderZMQ::makeEntrustID(char* buffer, int length)
{
	if (buffer == nullptr || length <= 0)
		return false;

	const uint64_t now = TimeUtils::getLocalTimeNow();
	const uint64_t seq = _id_seed.fetch_add(1) + 1;
	std::snprintf(buffer, (size_t)length, "mqmt.%llu.%llu",
		(unsigned long long)now,
		(unsigned long long)seq);
	return true;
}

bool TraderZMQ::probeLogin(std::string& errMsg)
{
	write_log(_listener, LL_INFO, "[TraderZMQ] probe_login started");
	rapidjson::Document reqDoc;
	reqDoc.SetObject();
	auto& alloc = reqDoc.GetAllocator();
	const uint64_t seq = _id_seed.fetch_add(1) + 1;
	const std::string reqId = fmtutil::format("account_req.{}.{}", (unsigned long long)TimeUtils::getLocalTimeNow(), (unsigned long long)seq);
	reqDoc.AddMember("type", rapidjson::Value("account_req", alloc), alloc);
	reqDoc.AddMember("req_id", rapidjson::Value(reqId.c_str(), alloc), alloc);
	reqDoc.AddMember("case_id", rapidjson::Value(_case_id.c_str(), alloc), alloc);
	const std::string accountReqEventId = fmtutil::format("A:{}:{}:REQ", _case_id, reqId);
	reqDoc.AddMember("event_id", rapidjson::Value(accountReqEventId.c_str(), alloc), alloc);
	reqDoc.AddMember("ubuntu_account_req_create_ts_ns", nowNs(), alloc);
	reqDoc.AddMember("account_id", rapidjson::Value(_account_id.c_str(), alloc), alloc);

	rapidjson::Document ack;
	if (!sendRequest(reqDoc, ack, "account_ack"))
	{
		errMsg = "account_req timeout";
		write_log(_listener, LL_ERROR, "[TraderZMQ] probe_login failed: {}", errMsg.c_str());
		return false;
	}

	const bool ok = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
	if (!ok)
	{
		errMsg = get_string(ack, {"error_msg"});
		if (errMsg.empty())
			errMsg = "account_ack rejected";
		write_log(_listener, LL_ERROR, "[TraderZMQ] probe_login rejected: {}", errMsg.c_str());
		return false;
	}

	write_log(_listener, LL_INFO, "[TraderZMQ] probe_login success");
	return true;
}

int TraderZMQ::login(const char* user, const char* /*pass*/, const char* /*productInfo*/)
{
	if (!_connected.load())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] login failed: channel not connected");
		return -1;
	}

	write_log(_listener, LL_INFO, "[TraderZMQ] login started account_id={} user={}", _account_id.c_str(), user == nullptr ? "" : user);

	if (_probe_login)
	{
		std::string errMsg;
		if (!probeLogin(errMsg))
		{
			_logged_in = false;
			if (_listener)
			{
				write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying login failure");
				_listener->onLoginResult(false, errMsg.c_str(), 0);
				write_log(_listener, LL_DEBUG, "[TraderZMQ] notified login failure");
			}
			return -1;
		}
	}

	_logged_in = true;
	if (_listener)
	{
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying login success");
		_listener->onLoginResult(true, "", TimeUtils::getCurDate());
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notified login success");
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] login success trading_day={}", TimeUtils::getCurDate());
	return 0;
}

int TraderZMQ::logout()
{
	_logged_in = false;
	write_log(_listener, LL_INFO, "[TraderZMQ] logout called");
	if (_listener)
	{
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying WTE_Logout");
		_listener->handleEvent(WTE_Logout, 0);
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notified WTE_Logout");
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onLogout");
		_listener->onLogout();
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onLogout");
	}
	return 0;
}

TraderZMQ::OrderContext TraderZMQ::makeContext(const WTSEntrust* entrust) const
{
	OrderContext ctx;
	if (entrust == nullptr)
		return ctx;

	ctx.req_id = entrust->getEntrustID();
	ctx.user_tag = entrust->getUserTag();
	ctx.exchg = upper_copy(entrust->getExchg());
	ctx.code = entrust->getCode();
	ctx.direction = entrust->getDirection();
	ctx.offset = entrust->getOffsetType();
	ctx.price_type = entrust->getPriceType();
	ctx.order_flag = entrust->getOrderFlag();
	ctx.price = entrust->getPrice();
	ctx.volume = entrust->getVolume();
	ctx.has_direction = true;
	ctx.has_offset = true;
	ctx.has_price_type = true;
	ctx.has_order_flag = true;
	ctx.has_price = true;
	ctx.has_volume = true;
	return ctx;
}

TraderZMQ::OrderContext TraderZMQ::resolveContext(const rapidjson::Value& obj) const
{
	OrderContext result;
	OrderContext explicitCtx = context_from_json(obj);

	{
		std::lock_guard<StdUniqueMutex> lock(_mtx_maps);
		if (!explicitCtx.req_id.empty())
		{
			auto it = _req_ctx.find(explicitCtx.req_id);
			if (it != _req_ctx.end())
				merge_missing(result, it->second);
		}

		if (!explicitCtx.order_id.empty())
		{
			auto it = _order_ctx.find(explicitCtx.order_id);
			if (it != _order_ctx.end())
				merge_missing(result, it->second);
		}
	}

	override_with(result, explicitCtx);
	if (!result.code.empty() && !result.exchg.empty() && (result.has_direction || result.has_offset))
	{
		const std::string rawDirection = get_string(obj, {"direction"});
		normalize_stock_side(_listener, result.code, result.exchg, rawDirection, result.direction, result.offset);
	}
	return result;
}

void TraderZMQ::storeContext(const OrderContext& ctx)
{
	if (ctx.req_id.empty() && ctx.order_id.empty())
		return;

	std::lock_guard<StdUniqueMutex> lock(_mtx_maps);
	OrderContext merged;

	if (!ctx.req_id.empty())
	{
		auto it = _req_ctx.find(ctx.req_id);
		if (it != _req_ctx.end())
			merge_missing(merged, it->second);
	}

	if (!ctx.order_id.empty())
	{
		auto it = _order_ctx.find(ctx.order_id);
		if (it != _order_ctx.end())
			merge_missing(merged, it->second);
	}

	override_with(merged, ctx);

	if (!merged.req_id.empty())
		 _req_ctx[merged.req_id] = merged;
	if (!merged.order_id.empty())
		 _order_ctx[merged.order_id] = merged;
}

void TraderZMQ::markSubmittingEmitted(const OrderContext& ctx)
{
	std::lock_guard<StdUniqueMutex> lock(_mtx_maps);

	if (!ctx.req_id.empty())
	{
		auto it = _req_ctx.find(ctx.req_id);
		if (it != _req_ctx.end())
			it->second.submitting_emitted = true;
	}

	if (!ctx.order_id.empty())
	{
		auto it = _order_ctx.find(ctx.order_id);
		if (it != _order_ctx.end())
			it->second.submitting_emitted = true;
	}
}

bool TraderZMQ::hasSubmittingEmitted(const OrderContext& ctx) const
{
	std::lock_guard<StdUniqueMutex> lock(_mtx_maps);

	if (!ctx.order_id.empty())
	{
		auto it = _order_ctx.find(ctx.order_id);
		if (it != _order_ctx.end())
			return it->second.submitting_emitted;
	}

	if (!ctx.req_id.empty())
	{
		auto it = _req_ctx.find(ctx.req_id);
		if (it != _req_ctx.end())
			return it->second.submitting_emitted;
	}

	return ctx.submitting_emitted;
}

void TraderZMQ::markTerminalEmitted(const OrderContext& ctx)
{
	std::lock_guard<StdUniqueMutex> lock(_mtx_maps);

	if (!ctx.req_id.empty())
	{
		auto it = _req_ctx.find(ctx.req_id);
		if (it != _req_ctx.end())
			it->second.terminal_emitted = true;
	}

	if (!ctx.order_id.empty())
	{
		auto it = _order_ctx.find(ctx.order_id);
		if (it != _order_ctx.end())
			it->second.terminal_emitted = true;
	}
}

bool TraderZMQ::hasTerminalEmitted(const OrderContext& ctx) const
{
	std::lock_guard<StdUniqueMutex> lock(_mtx_maps);

	if (!ctx.order_id.empty())
	{
		auto it = _order_ctx.find(ctx.order_id);
		if (it != _order_ctx.end())
			return it->second.terminal_emitted;
	}

	if (!ctx.req_id.empty())
	{
		auto it = _req_ctx.find(ctx.req_id);
		if (it != _req_ctx.end())
			return it->second.terminal_emitted;
	}

	return ctx.terminal_emitted;
}

void TraderZMQ::markOrderSnapshot(const OrderContext& ctx, WTSOrderState state, double volTraded, double volLeft)
{
	std::lock_guard<StdUniqueMutex> lock(_mtx_maps);

	auto apply = [&](OrderContext& item)
	{
		item.has_last_order_snapshot = true;
		item.last_order_state = state;
		item.last_vol_traded = volTraded;
		item.last_vol_left = volLeft;
	};

	if (!ctx.req_id.empty())
	{
		auto it = _req_ctx.find(ctx.req_id);
		if (it != _req_ctx.end())
			apply(it->second);
	}

	if (!ctx.order_id.empty())
	{
		auto it = _order_ctx.find(ctx.order_id);
		if (it != _order_ctx.end())
			apply(it->second);
	}
}

bool TraderZMQ::isDuplicateOrderUpdate(const OrderContext& ctx, WTSOrderState state, double volTraded, double volLeft) const
{
	std::lock_guard<StdUniqueMutex> lock(_mtx_maps);

	auto same_snapshot = [&](const OrderContext& item)
	{
		return item.has_last_order_snapshot
			&& item.last_order_state == state
			&& nearly_equal(item.last_vol_traded, volTraded)
			&& nearly_equal(item.last_vol_left, volLeft);
	};

	if (!ctx.order_id.empty())
	{
		auto it = _order_ctx.find(ctx.order_id);
		if (it != _order_ctx.end())
			return same_snapshot(it->second);
	}

	if (!ctx.req_id.empty())
	{
		auto it = _req_ctx.find(ctx.req_id);
		if (it != _req_ctx.end())
			return same_snapshot(it->second);
	}

	return false;
}

void TraderZMQ::emitSubmittingOrder(const OrderContext& ctx)
{
	if (_listener == nullptr || ctx.user_tag.empty() || ctx.code.empty())
		return;

	WTSOrderInfo* ord = WTSOrderInfo::create();
	ord->setExchange(ctx.exchg.c_str());
	ord->setCode(ctx.code.c_str());
	ord->setUserTag(ctx.user_tag.c_str());
	if (!ctx.req_id.empty())
		ord->setEntrustID(ctx.req_id.c_str());
	if (!ctx.order_id.empty())
		ord->setOrderID(ctx.order_id.c_str());
	if (ctx.has_direction)
		ord->setDirection(ctx.direction);
	if (ctx.has_offset)
		ord->setOffsetType(ctx.offset);
	if (ctx.has_price_type)
		ord->setPriceType(ctx.price_type);
	if (ctx.has_order_flag)
		ord->setOrderFlag(ctx.order_flag);
	if (ctx.has_price)
		ord->setPrice(ctx.price);
	if (ctx.has_volume)
	{
		ord->setVolume(ctx.volume);
		ord->setVolLeft(ctx.volume);
	}

	ord->setOrderState(WOS_Submitting);
	WTSContractInfo* cInfo = resolve_contract(_listener, ctx.code, ctx.exchg);
	if (cInfo != nullptr)
		ord->setContractInfo(cInfo);

	uint32_t date = 0, timeMs = 0;
	TimeUtils::getDateTime(date, timeMs);
	ord->setOrderDate(date);
	ord->setOrderTime(TimeUtils::makeTime(date, timeMs));

	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying synthetic onPushOrder submitting req_id={} order_id={}", ctx.req_id.c_str(), ctx.order_id.c_str());
	{
		std::lock_guard<std::recursive_mutex> callbackLock(_mtx_callback);
		_listener->onPushOrder(ord);
	}
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified synthetic onPushOrder submitting req_id={} order_id={}", ctx.req_id.c_str(), ctx.order_id.c_str());
	ord->release();
}

void TraderZMQ::emitSubmittingIfNeeded(const OrderContext& ctx, WTSOrderState actualState)
{
	if (hasSubmittingEmitted(ctx))
		return;

	if (actualState != WOS_Submitting)
		emitSubmittingOrder(ctx);

	markSubmittingEmitted(ctx);
}

void TraderZMQ::enqueueSyntheticSubmitting(const OrderContext& ctx)
{
	{
		std::lock_guard<StdUniqueMutex> lock(_mtx_pending);
		_pending_submitting.emplace_back(ctx);
	}

	write_log(_listener,
		LL_DEBUG,
		"[TraderZMQ] queued synthetic submitting req_id={} order_id={}",
		ctx.req_id.c_str(),
		ctx.order_id.c_str());
	enqueueCallback([this]() { drainPendingSubmitting(); });
}

void TraderZMQ::dispatchSyntheticSubmitting(const OrderContext& pending)
{
	rapidjson::Document pseudo;
	pseudo.SetObject();
	auto& alloc = pseudo.GetAllocator();
	if (!pending.req_id.empty())
		pseudo.AddMember("req_id", rapidjson::Value(pending.req_id.c_str(), alloc), alloc);
	if (!pending.order_id.empty())
		pseudo.AddMember("order_id", rapidjson::Value(pending.order_id.c_str(), alloc), alloc);

	OrderContext ctx = resolveContext(pseudo);
	override_with(ctx, pending);

	if (hasSubmittingEmitted(ctx))
	{
		write_log(_listener,
			LL_DEBUG,
			"[TraderZMQ] skipped queued synthetic submitting req_id={} order_id={} reason=already_emitted",
			ctx.req_id.c_str(),
			ctx.order_id.c_str());
		return;
	}

	write_log(_listener,
		LL_DEBUG,
		"[TraderZMQ] dispatching queued synthetic submitting req_id={} order_id={}",
		ctx.req_id.c_str(),
		ctx.order_id.c_str());
	emitSubmittingOrder(ctx);
	markSubmittingEmitted(ctx);
	write_log(_listener,
		LL_DEBUG,
		"[TraderZMQ] dispatched queued synthetic submitting req_id={} order_id={}",
		ctx.req_id.c_str(),
		ctx.order_id.c_str());
}

void TraderZMQ::drainPendingSubmitting()
{
	std::deque<OrderContext> pending;
	{
		std::lock_guard<StdUniqueMutex> lock(_mtx_pending);
		if (_pending_submitting.empty())
			return;
		pending.swap(_pending_submitting);
	}

	for (const auto& ctx : pending)
		dispatchSyntheticSubmitting(ctx);
}

void TraderZMQ::emitSyntheticErrorOrder(const OrderContext& ctx, const char* errMsg)
{
	if (_listener == nullptr || ctx.user_tag.empty() || ctx.code.empty())
		return;

	WTSOrderInfo* ord = WTSOrderInfo::create();
	ord->setExchange(ctx.exchg.c_str());
	ord->setCode(ctx.code.c_str());
	ord->setUserTag(ctx.user_tag.c_str());
	if (!ctx.req_id.empty())
		ord->setEntrustID(ctx.req_id.c_str());
	if (!ctx.order_id.empty())
		ord->setOrderID(ctx.order_id.c_str());
	if (ctx.has_direction)
		ord->setDirection(ctx.direction);
	if (ctx.has_offset)
		ord->setOffsetType(ctx.offset);
	if (ctx.has_price_type)
		ord->setPriceType(ctx.price_type);
	if (ctx.has_order_flag)
		ord->setOrderFlag(ctx.order_flag);
	if (ctx.has_price)
		ord->setPrice(ctx.price);
	if (ctx.has_volume)
	{
		ord->setVolume(ctx.volume);
		ord->setVolLeft(ctx.volume);
	}
	ord->setOrderState(WOS_Canceled);
	ord->setError(WOEF_Normal);
	ord->setStateMsg(errMsg == nullptr ? "" : errMsg);

	WTSContractInfo* cInfo = resolve_contract(_listener, ctx.code, ctx.exchg);
	if (cInfo != nullptr)
		ord->setContractInfo(cInfo);

	uint32_t date = 0, timeMs = 0;
	TimeUtils::getDateTime(date, timeMs);
	ord->setOrderDate(date);
	ord->setOrderTime(TimeUtils::makeTime(date, timeMs));

	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying synthetic onPushOrder error req_id={} order_id={}", ctx.req_id.c_str(), ctx.order_id.c_str());
	{
		std::lock_guard<std::recursive_mutex> callbackLock(_mtx_callback);
		_listener->onPushOrder(ord);
	}
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified synthetic onPushOrder error req_id={} order_id={}", ctx.req_id.c_str(), ctx.order_id.c_str());
	ord->release();
}

int TraderZMQ::orderInsert(WTSEntrust* entrust)
{
	if (entrust == nullptr || !isOperational())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] orderInsert rejected: entrust={} operational={}", entrust != nullptr ? "set" : "null", isOperational());
		return -1;
	}

	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert before entry lock entrust=set");
	std::unique_lock<std::recursive_mutex> entryLock(_mtx_callback);
	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert after entry lock entrust=set");
	OrderContext ctx = makeContext(entrust);
	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert after makeContext req_id={} user_tag={} exchg={} code={}", ctx.req_id.c_str(), ctx.user_tag.c_str(), ctx.exchg.c_str(), ctx.code.c_str());
	storeContext(ctx);
	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert after storeContext req_id={} user_tag={}", ctx.req_id.c_str(), ctx.user_tag.c_str());

	const char* direction = entrust->getDirection() == WDT_SHORT ? "SHORT" : "LONG";
	const char* offset = "OPEN";
	switch (entrust->getOffsetType())
	{
	case WOT_CLOSE: offset = "CLOSE"; break;
	case WOT_CLOSETODAY: offset = "CLOSETODAY"; break;
	case WOT_CLOSEYESTERDAY: offset = "CLOSEYESTERDAY"; break;
	default: break;
	}
	std::string directionToken(direction);
	std::string offsetToken(offset);
	if (is_stock_like(_listener, ctx.code, ctx.exchg))
	{
		directionToken = is_buy_action(entrust->getDirection(), entrust->getOffsetType()) ? "BUY" : "SELL";
		offsetToken = offset_token(entrust->getOffsetType());
		direction = directionToken.c_str();
		offset = offsetToken.c_str();
	}

	const char* priceType = "ANY";
	if (entrust->getPriceType() == WPT_LIMITPRICE)
		priceType = "LIMIT";
	else if (entrust->getPriceType() == WPT_BESTPRICE)
		priceType = "BEST";

	write_log(_listener,
		LL_INFO,
		"[TraderZMQ] orderInsert started req_id={} user_tag={} exchg={} code={} direction={} offset={} price={} volume={} price_type={}",
		ctx.req_id.c_str(),
		ctx.user_tag.c_str(),
		ctx.exchg.c_str(),
		ctx.code.c_str(),
		direction,
		offset,
		format_number(ctx.price, 6),
		format_number(ctx.volume, 4),
		priceType);

	rapidjson::Document reqDoc;
	reqDoc.SetObject();
	auto& alloc = reqDoc.GetAllocator();
	reqDoc.AddMember("type", rapidjson::Value("order_req", alloc), alloc);
	reqDoc.AddMember("req_id", rapidjson::Value(ctx.req_id.c_str(), alloc), alloc);
	reqDoc.AddMember("user_tag", rapidjson::Value(ctx.user_tag.c_str(), alloc), alloc);
	reqDoc.AddMember("case_id", rapidjson::Value(_case_id.c_str(), alloc), alloc);
	const std::string orderReqEventId = fmtutil::format("O:{}:{}:REQ", _case_id, ctx.req_id);
	reqDoc.AddMember("event_id", rapidjson::Value(orderReqEventId.c_str(), alloc), alloc);
	reqDoc.AddMember("ubuntu_order_req_create_ts_ns", nowNs(), alloc);
	reqDoc.AddMember("account_id", rapidjson::Value(_account_id.c_str(), alloc), alloc);
	reqDoc.AddMember("code", rapidjson::Value(entrust->getCode(), alloc), alloc);
	reqDoc.AddMember("exchg", rapidjson::Value(entrust->getExchg(), alloc), alloc);
	reqDoc.AddMember("direction", rapidjson::Value(direction, alloc), alloc);
	reqDoc.AddMember("offset", rapidjson::Value(offset, alloc), alloc);
	reqDoc.AddMember("price", entrust->getPrice(), alloc);
	reqDoc.AddMember("volume", entrust->getVolume(), alloc);
	reqDoc.AddMember("price_type", rapidjson::Value(priceType, alloc), alloc);

	if (entrust->getOrderFlag() == WOF_FAK)
		reqDoc.AddMember("order_flag", rapidjson::Value("FAK", alloc), alloc);
	else if (entrust->getOrderFlag() == WOF_FOK)
		reqDoc.AddMember("order_flag", rapidjson::Value("FOK", alloc), alloc);

	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert before beginOrderApi req_id={}", ctx.req_id.c_str());
	beginOrderApi();
	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert after beginOrderApi req_id={} active={}", ctx.req_id.c_str(), _order_api_active.load());
	entryLock.unlock();
	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert after entry unlock req_id={}", ctx.req_id.c_str());

	rapidjson::Document ack;
	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert before sendRequest req_id={}", ctx.req_id.c_str());
	const bool requestOk = sendRequest(reqDoc, ack, "order_ack");
	write_log(_listener, LL_DEBUG, "[TraderZMQ] orderInsert after sendRequest req_id={} ok={}", ctx.req_id.c_str(), requestOk ? "true" : "false");
	if (!requestOk)
	{
		endOrderApi();
		if (_listener)
		{
			WTSError* err = WTSError::create(WEC_ORDERINSERT, "order_ack timeout");
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspEntrust timeout");
			dispatchCallbackSync([this, entrust, err]() {
				if (_listener)
					_listener->onRspEntrust(entrust, err);
			});
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspEntrust timeout");
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onTraderError timeout");
			dispatchCallbackSync([this, err]() {
				if (_listener)
					_listener->onTraderError(err);
			});
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onTraderError timeout");
			err->release();
		}
		return -1;
	}
	endOrderApi();

	const bool ok = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
	if (!ok)
	{
		const std::string errMsg = get_string(ack, {"error_msg"});
		if (_listener)
		{
			WTSError* err = WTSError::create(WEC_ORDERINSERT, errMsg.empty() ? "order rejected" : errMsg.c_str());
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspEntrust rejection");
			dispatchCallbackSync([this, entrust, err]() {
				if (_listener)
					_listener->onRspEntrust(entrust, err);
			});
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspEntrust rejection");
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onTraderError rejection");
			dispatchCallbackSync([this, err]() {
				if (_listener)
					_listener->onTraderError(err);
			});
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onTraderError rejection");
			err->release();
		}
		return -1;
	}

	const std::string ackOrderId = get_string(ack, {"order_id"});
	if (!ackOrderId.empty())
		ctx.order_id = ackOrderId;
	storeContext(ctx);

	if (_listener)
	{
		write_log(_listener, LL_DEBUG, "[TraderZMQ] suppressing synchronous onRspEntrust success req_id={} order_id={}; live order callbacks will follow",
			ctx.req_id.c_str(),
			ctx.order_id.c_str());
		enqueueSyntheticSubmitting(ctx);
	}

	write_log(_listener, LL_INFO,
		"[TraderZMQ] order accepted req_id={} order_id={} user_tag={}",
		ctx.req_id.c_str(),
		ctx.order_id.c_str(),
		ctx.user_tag.c_str());
	return 0;
}

int TraderZMQ::orderAction(WTSEntrustAction* action)
{
	if (action == nullptr || !isOperational())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] orderAction rejected: action={} operational={}", action != nullptr ? "set" : "null", isOperational());
		return -1;
	}

	std::unique_lock<std::recursive_mutex> entryLock(_mtx_callback);
	OrderContext ctx;
	ctx.req_id = action->getEntrustID();
	ctx.order_id = action->getOrderID();
	ctx.user_tag = action->getUserTag();
	ctx.exchg = upper_copy(action->getExchg());
	ctx.code = action->getCode();

	if (ctx.user_tag.empty() || ctx.code.empty() || ctx.exchg.empty())
	{
		rapidjson::Document pseudo;
		pseudo.SetObject();
		auto& alloc = pseudo.GetAllocator();
		if (!ctx.req_id.empty())
			pseudo.AddMember("req_id", rapidjson::Value(ctx.req_id.c_str(), alloc), alloc);
		if (!ctx.order_id.empty())
			pseudo.AddMember("order_id", rapidjson::Value(ctx.order_id.c_str(), alloc), alloc);
		ctx = resolveContext(pseudo);
		if (ctx.req_id.empty())
			ctx.req_id = action->getEntrustID();
		if (ctx.order_id.empty())
			ctx.order_id = action->getOrderID();
	}

	write_log(_listener,
		LL_INFO,
		"[TraderZMQ] orderAction started req_id={} order_id={} user_tag={} exchg={} code={}",
		ctx.req_id.c_str(),
		ctx.order_id.c_str(),
		ctx.user_tag.c_str(),
		ctx.exchg.c_str(),
		ctx.code.c_str());

	rapidjson::Document reqDoc;
	reqDoc.SetObject();
	auto& alloc = reqDoc.GetAllocator();
	const std::string cancelReqId = ctx.req_id.empty() ? action->getEntrustID() : ctx.req_id;
	const std::string cancelOrderId = ctx.order_id.empty() ? action->getOrderID() : ctx.order_id;
	if (cancelOrderId.empty())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] orderAction rejected: missing order_id req_id={}", cancelReqId.c_str());
		return -1;
	}
	reqDoc.AddMember("type", rapidjson::Value("cancel_req", alloc), alloc);
	reqDoc.AddMember("req_id", rapidjson::Value(cancelReqId.c_str(), alloc), alloc);
	reqDoc.AddMember("case_id", rapidjson::Value(_case_id.c_str(), alloc), alloc);
	const std::string cancelEventId = fmtutil::format("C:{}:{}:REQ", _case_id, cancelReqId);
	reqDoc.AddMember("event_id", rapidjson::Value(cancelEventId.c_str(), alloc), alloc);
	reqDoc.AddMember("ubuntu_cancel_req_create_ts_ns", nowNs(), alloc);
	reqDoc.AddMember("account_id", rapidjson::Value(_account_id.c_str(), alloc), alloc);

	char* endp = nullptr;
	const auto oidNum = std::strtoull(cancelOrderId.c_str(), &endp, 10);
	if (endp != nullptr && *endp == '\0')
		reqDoc.AddMember("order_id", (int64_t)oidNum, alloc);
	else
		reqDoc.AddMember("order_id", rapidjson::Value(cancelOrderId.c_str(), alloc), alloc);

	if (!ctx.user_tag.empty())
		reqDoc.AddMember("user_tag", rapidjson::Value(ctx.user_tag.c_str(), alloc), alloc);

	beginOrderApi();
	entryLock.unlock();

	rapidjson::Document ack;
	if (!sendRequest(reqDoc, ack, "cancel_ack"))
	{
		endOrderApi();
		if (_listener)
		{
			WTSError* err = WTSError::create(WEC_ORDERCANCEL, "cancel_ack timeout");
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onTraderError cancel timeout");
			dispatchCallbackSync([this, err]() {
				if (_listener)
					_listener->onTraderError(err);
			});
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onTraderError cancel timeout");
			err->release();
		}
		return -1;
	}
	endOrderApi();

	const bool ok = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
	if (!ok)
	{
		const std::string errMsg = get_string(ack, {"error_msg"});
		if (_listener)
		{
			WTSError* err = WTSError::create(WEC_ORDERCANCEL, errMsg.empty() ? "cancel rejected" : errMsg.c_str());
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onTraderError cancel rejection");
			dispatchCallbackSync([this, err]() {
				if (_listener)
					_listener->onTraderError(err);
			});
			write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onTraderError cancel rejection");
			err->release();
		}
		return -1;
	}
	write_log(_listener, LL_INFO,
		"[TraderZMQ] cancel accepted req_id={} order_id={}",
		cancelReqId.c_str(),
		cancelOrderId.c_str());
	return 0;
}

bool TraderZMQ::sendRequest(const rapidjson::Document& req, rapidjson::Document& ack, const char* expectedType)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	req.Accept(writer);

	std::string reqType;
	if (req.HasMember("type") && req["type"].IsString())
		reqType = req["type"].GetString();
	const std::string reqId = get_string(req, {"req_id"});
	const std::string eventId = get_string(req, {"event_id"});
	const std::string orderId = get_string(req, {"order_id"});
	const std::string userTag = get_string(req, {"user_tag"});

	std::lock_guard<StdUniqueMutex> lock(_mtx_sock);
	if (_ctx == nullptr)
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] req_abort socket_not_ready type={} req_id={} event_id={} order_id={} user_tag={}",
			reqType.c_str(), reqId.c_str(), eventId.c_str(), orderId.c_str(), userTag.c_str());
		return false;
	}

	auto recreate_req_socket = [&]() -> bool
	{
		try
		{
			if (_req)
				_req->close();
		}
		catch (...)
		{
		}
		_req.reset();
		try
		{
			_req = std::make_unique<zmq::socket_t>(*_ctx, zmq::socket_type::req);
			_req->set(zmq::sockopt::linger, 0);
			_req->set(zmq::sockopt::sndtimeo, _ack_timeout_ms);
			_req->set(zmq::sockopt::rcvtimeo, _ack_timeout_ms);
			try
			{
#ifdef ZMQ_REQ_RELAXED
				_req->set(zmq::sockopt::req_relaxed, 1);
#endif
#ifdef ZMQ_REQ_CORRELATE
				_req->set(zmq::sockopt::req_correlate, 1);
#endif
			}
			catch (...)
			{
			}
			_req->connect(make_endpoint(_host, _req_port));
			return true;
		}
		catch (const zmq::error_t& e)
		{
			write_log(_listener, LL_ERROR, "[TraderZMQ] recreate req socket failed: {}", e.what());
			_req.reset();
			return false;
		}
	};

	try
	{
		if (!_req && !recreate_req_socket())
			return false;
		write_log(_listener, LL_INFO,
			"[TraderZMQ] req_send type={} req_id={} event_id={} order_id={} user_tag={} bytes={} timeout_ms={}",
			reqType.c_str(),
			reqId.c_str(),
			eventId.c_str(),
			orderId.c_str(),
			userTag.c_str(),
			buffer.GetSize(),
			_ack_timeout_ms);
		zmq::message_t msg(buffer.GetString(), buffer.GetSize());

		const uint64_t ubuntuSendTsNs = nowNs();
		_req->send(msg, zmq::send_flags::none);

		zmq::message_t rep;
		auto ok = _req->recv(rep, zmq::recv_flags::none);
		const uint64_t ubuntuRecvTsNs = nowNs();
		if (!ok)
		{
			writeStatsEvent("request_timeout", &req, reqType.c_str(), ubuntuSendTsNs, ubuntuRecvTsNs);
			write_log(_listener, LL_ERROR,
				"[TraderZMQ] req_timeout type={} req_id={} event_id={} order_id={} user_tag={} elapsed_ms={} timeout_ms={}",
				reqType.empty() ? "request" : reqType.c_str(),
				reqId.c_str(),
				eventId.c_str(),
				orderId.c_str(),
				userTag.c_str(),
				elapsed_ms(ubuntuSendTsNs, ubuntuRecvTsNs),
				_ack_timeout_ms);
			recreate_req_socket();
			return false;
		}

		ack.Parse((const char*)rep.data(), rep.size());
		if (!ack.IsObject())
		{
			write_log(_listener, LL_ERROR,
				"[TraderZMQ] req_ack_invalid_json type={} req_id={} event_id={} order_id={} user_tag={} bytes={}",
				reqType.c_str(), reqId.c_str(), eventId.c_str(), orderId.c_str(), userTag.c_str(), rep.size());
			return false;
		}
		writeStatsEvent("request_ack", &ack, reqType.c_str(), ubuntuSendTsNs, ubuntuRecvTsNs);

		const std::string ackType = get_string(ack, {"type"});
		const bool ackOk = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
		const int64_t roundTripMs = elapsed_ms(ubuntuSendTsNs, ubuntuRecvTsNs);
		int32_t warnMs = _ack_timeout_ms > 0 ? std::max(1000, _ack_timeout_ms / 2) : 1000;
		if (reqType == "order_req" || reqType == "cancel_req")
			warnMs = _order_ack_warn_ms > 0 ? _order_ack_warn_ms : warnMs;
		const WTSLogLevel ackLevel = roundTripMs >= warnMs ? LL_WARN : LL_INFO;
		write_log(_listener,
			ackLevel,
			"[TraderZMQ] req_ack type={} req_id={} event_id={} ack_type={} ok={} order_id={} user_tag={} round_trip_ms={} win_req_rx_ts_ns={} win_ack_pub_ts_ns={} bridge_seq={}",
			reqType.c_str(),
			reqId.c_str(),
			eventId.c_str(),
			ackType.c_str(),
			ackOk ? "true" : "false",
			get_string(ack, {"order_id"}).c_str(),
			get_string(ack, {"user_tag"}).c_str(),
			roundTripMs,
			get_uint64(ack, {"win_req_rx_ts_ns"}, 0),
			get_uint64(ack, {"win_ack_pub_ts_ns"}, 0),
			get_uint64(ack, {"bridge_seq"}, 0));

		if (expectedType != nullptr && ack.HasMember("type") && ack["type"].IsString())
		{
			const char* actualType = ack["type"].GetString();
			if (std::strcmp(actualType, expectedType) != 0)
			{
				write_log(_listener, LL_WARN, "[TraderZMQ] ack type mismatch, expect={} actual={}", expectedType, actualType);
			}
		}

		return true;
	}
	catch (const zmq::error_t& e)
	{
		write_log(_listener, LL_ERROR,
			"[TraderZMQ] req_zmq_error type={} req_id={} event_id={} order_id={} user_tag={} err={}",
			reqType.c_str(), reqId.c_str(), eventId.c_str(), orderId.c_str(), userTag.c_str(), e.what());
		recreate_req_socket();
		return false;
	}
}

int TraderZMQ::queryAccount()
{
	if (_listener == nullptr || !isOperational())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] queryAccount rejected: listener={} operational={}", _listener != nullptr ? "set" : "null", isOperational());
		return -1;
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] queryAccount started");

	rapidjson::Document reqDoc;
	reqDoc.SetObject();
	auto& alloc = reqDoc.GetAllocator();
	const uint64_t seq = _id_seed.fetch_add(1) + 1;
	const std::string reqId = fmtutil::format("account_req.{}.{}", (unsigned long long)TimeUtils::getLocalTimeNow(), (unsigned long long)seq);
	reqDoc.AddMember("type", rapidjson::Value("account_req", alloc), alloc);
	reqDoc.AddMember("req_id", rapidjson::Value(reqId.c_str(), alloc), alloc);
	reqDoc.AddMember("case_id", rapidjson::Value(_case_id.c_str(), alloc), alloc);
	const std::string queryAccountReqEventId = fmtutil::format("A:{}:{}:REQ", _case_id, reqId);
	reqDoc.AddMember("event_id", rapidjson::Value(queryAccountReqEventId.c_str(), alloc), alloc);
	reqDoc.AddMember("ubuntu_account_req_create_ts_ns", nowNs(), alloc);
	reqDoc.AddMember("account_id", rapidjson::Value(_account_id.c_str(), alloc), alloc);

	rapidjson::Document ack;
	if (!sendRequest(reqDoc, ack, "account_ack"))
		return -1;

	const bool ok = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
	if (!ok)
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] account query rejected: {}", get_string(ack, {"error_msg"}).c_str());
		return -1;
	}

	WTSArray* ay = WTSArray::create();
	WTSAccountInfo* accountInfo = WTSAccountInfo::create();
	const std::string currency = get_string(ack, {"currency"});
	accountInfo->setCurrency(currency.empty() ? "CNY" : currency.c_str());
	accountInfo->setPreBalance(get_number(ack, {"pre_balance"}));
	accountInfo->setBalance(get_number(ack, {"balance"}));
	accountInfo->setAvailable(get_number(ack, {"available"}));
	accountInfo->setDynProfit(get_number(ack, {"dyn_profit"}));
	accountInfo->setCloseProfit(get_number(ack, {"close_profit"}));
	accountInfo->setMargin(get_number(ack, {"margin"}));
	accountInfo->setCommission(get_number(ack, {"commission"}));
	accountInfo->setDeposit(get_number(ack, {"deposit"}));
	accountInfo->setWithdraw(get_number(ack, {"withdraw"}));
	ay->append(accountInfo, false);

	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspAccount count=1");
	dispatchCallbackSync([this, ay]() {
		if (_listener)
			_listener->onRspAccount(ay);
		ay->release();
	});
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspAccount count=1");
	write_log(_listener, LL_INFO, "[TraderZMQ] queryAccount finished");
	return 0;
}

int TraderZMQ::queryPositions()
{
	if (_listener == nullptr || !isOperational())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] queryPositions rejected: listener={} operational={}", _listener != nullptr ? "set" : "null", isOperational());
		return -1;
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] queryPositions started");

	rapidjson::Document reqDoc;
	reqDoc.SetObject();
	auto& alloc = reqDoc.GetAllocator();
	const uint64_t seq = _id_seed.fetch_add(1) + 1;
	const std::string reqId = fmtutil::format("positions_req.{}.{}", (unsigned long long)TimeUtils::getLocalTimeNow(), (unsigned long long)seq);
	reqDoc.AddMember("type", rapidjson::Value("positions_req", alloc), alloc);
	reqDoc.AddMember("req_id", rapidjson::Value(reqId.c_str(), alloc), alloc);
	reqDoc.AddMember("case_id", rapidjson::Value(_case_id.c_str(), alloc), alloc);
	const std::string positionsReqEventId = fmtutil::format("P:{}:{}:REQ", _case_id, reqId);
	reqDoc.AddMember("event_id", rapidjson::Value(positionsReqEventId.c_str(), alloc), alloc);
	reqDoc.AddMember("ubuntu_positions_req_create_ts_ns", nowNs(), alloc);
	reqDoc.AddMember("account_id", rapidjson::Value(_account_id.c_str(), alloc), alloc);

	rapidjson::Document ack;
	if (!sendRequest(reqDoc, ack, "positions_ack"))
		return -1;

	const bool ok = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
	if (!ok)
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] positions query rejected: {}", get_string(ack, {"error_msg"}).c_str());
		return -1;
	}

	WTSArray* ayPos = WTSArray::create();
	if (ack.HasMember("positions") && ack["positions"].IsArray())
	{
		for (const auto& posObj : ack["positions"].GetArray())
		{
			std::string exchg;
			std::string code;
			normalize_code_fields(posObj, exchg, code);
			if (code.empty())
				continue;

			const std::string currency = get_string(posObj, {"currency"});
			WTSPositionItem* pItem = WTSPositionItem::create(code.c_str(), currency.empty() ? "CNY" : currency.c_str(), exchg.c_str());
			WTSContractInfo* cInfo = resolve_contract(_listener, code, exchg);
			if (cInfo != nullptr)
				pItem->setContractInfo(cInfo);

			pItem->setDirection(map_direction(get_string(posObj, {"direction"})));

			double prevol = get_number(posObj, {"prevol"});
			double preavail = get_number(posObj, {"preavail"});
			double newvol = get_number(posObj, {"newvol"});
			double newavail = get_number(posObj, {"newavail"});

			if (prevol == 0.0 && newvol == 0.0 && has_any(posObj, {"volume"}))
			{
				newvol = get_number(posObj, {"volume"});
				newavail = get_number(posObj, {"avail"});
			}

			pItem->setPrePosition(prevol);
			pItem->setAvailPrePos(preavail);
			pItem->setNewPosition(newvol);
			pItem->setAvailNewPos(newavail);
			pItem->setPositionCost(get_number(posObj, {"cost"}));
			pItem->setAvgPrice(get_number(posObj, {"avg_price"}));
			pItem->setDynProfit(get_number(posObj, {"dyn_profit"}));
			pItem->setMargin(get_number(posObj, {"margin"}));
			ayPos->append(pItem, false);
		}
	}

	const auto posCnt = ayPos->size();
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspPosition count={}", posCnt);
	dispatchCallbackSync([this, ayPos]() {
		if (_listener)
			_listener->onRspPosition(ayPos);
		ayPos->release();
	});
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspPosition count={}", posCnt);
	write_log(_listener, LL_INFO, "[TraderZMQ] queryPositions finished count={}", posCnt);
	return 0;
}

int TraderZMQ::queryOrders()
{
	if (_listener == nullptr || !isOperational())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] queryOrders rejected: listener={} operational={}", _listener != nullptr ? "set" : "null", isOperational());
		return -1;
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] queryOrders started");

	rapidjson::Document reqDoc;
	reqDoc.SetObject();
	auto& alloc = reqDoc.GetAllocator();
	const uint64_t seq = _id_seed.fetch_add(1) + 1;
	const std::string reqId = fmtutil::format("orders_req.{}.{}", (unsigned long long)TimeUtils::getLocalTimeNow(), (unsigned long long)seq);
	reqDoc.AddMember("type", rapidjson::Value("orders_req", alloc), alloc);
	reqDoc.AddMember("req_id", rapidjson::Value(reqId.c_str(), alloc), alloc);
	reqDoc.AddMember("case_id", rapidjson::Value(_case_id.c_str(), alloc), alloc);
	const std::string ordersReqEventId = fmtutil::format("RO:{}:{}:REQ", _case_id, reqId);
	reqDoc.AddMember("event_id", rapidjson::Value(ordersReqEventId.c_str(), alloc), alloc);
	reqDoc.AddMember("ubuntu_orders_req_create_ts_ns", nowNs(), alloc);
	reqDoc.AddMember("account_id", rapidjson::Value(_account_id.c_str(), alloc), alloc);

	rapidjson::Document ack;
	if (!sendRequest(reqDoc, ack, "orders_ack"))
		return -1;

	const bool ok = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
	if (!ok)
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] orders query rejected: {}", get_string(ack, {"error_msg"}).c_str());
		return -1;
	}

	WTSArray* ayOrders = WTSArray::create();
	if (ack.HasMember("orders") && ack["orders"].IsArray())
	{
		for (const auto& ordObj : ack["orders"].GetArray())
		{
			OrderContext ctx = resolveContext(ordObj);
			storeContext(ctx);

			std::string exchg;
			std::string code;
			normalize_code_fields(ordObj, exchg, code);
			if (code.empty())
				code = ctx.code;
			if (exchg.empty())
				exchg = ctx.exchg;
			if (code.empty())
				continue;

			WTSOrderInfo* ord = WTSOrderInfo::create();
			ord->setExchange(exchg.c_str());
			ord->setCode(code.c_str());
			if (!ctx.user_tag.empty())
				ord->setUserTag(ctx.user_tag.c_str());
			if (!ctx.req_id.empty())
				ord->setEntrustID(ctx.req_id.c_str());
			else
				ord->setEntrustID(get_string(ordObj, {"local_id", "order_id"}).c_str());

			const std::string orderId = get_string(ordObj, {"order_id"});
			if (!orderId.empty())
				ord->setOrderID(orderId.c_str());

			WTSContractInfo* cInfo = resolve_contract(_listener, code, exchg);
			if (cInfo != nullptr)
				ord->setContractInfo(cInfo);

			const std::string directionStr = get_string(ordObj, {"direction"});
			WTSDirectionType direction = map_direction(directionStr, ctx.has_direction ? ctx.direction : WDT_LONG);
			const std::string offsetStr = get_string(ordObj, {"offset"});
			WTSOffsetType offset = map_offset(offsetStr, ctx.has_offset ? ctx.offset : WOT_OPEN);
			normalize_stock_side(_listener, code, exchg, directionStr, direction, offset);
			ord->setDirection(direction);
			ord->setOffsetType(offset);

			const std::string priceTypeStr = get_string(ordObj, {"price_type"});
			ord->setPriceType(map_price_type(priceTypeStr, ctx.has_price_type ? ctx.price_type : WPT_ANYPRICE));
			const std::string orderFlagStr = get_string(ordObj, {"order_flag"});
			ord->setOrderFlag(map_order_flag(orderFlagStr, ctx.has_order_flag ? ctx.order_flag : WOF_NOR));

			const bool hasVolume = has_any(ordObj, {"volume", "qty"});
			const double totalVolume = hasVolume ? get_number(ordObj, {"volume", "qty"}) : (ctx.has_volume ? ctx.volume : 0.0);
			double volTraded = get_number(ordObj, {"vol_traded", "traded_volume"}, 0.0);
			double volLeft = has_any(ordObj, {"vol_left", "left_volume"}) ? get_number(ordObj, {"vol_left", "left_volume"}) : std::max(0.0, totalVolume - volTraded);
			if (!has_any(ordObj, {"vol_traded", "traded_volume"}) && totalVolume > 0.0 && volLeft <= totalVolume)
				volTraded = totalVolume - volLeft;

			ord->setPrice(has_any(ordObj, {"price"}) ? get_number(ordObj, {"price"}) : (ctx.has_price ? ctx.price : 0.0));
			ord->setVolume(totalVolume);
			ord->setVolTraded(volTraded);
			ord->setVolLeft(volLeft);
			ord->setOrderState(map_status(get_string(ordObj, {"status"})));

			uint32_t date = 0, timeMs = 0;
			extract_datetime(ordObj, date, timeMs);
			if (date != 0)
			{
				ord->setOrderDate(date);
				ord->setOrderTime(TimeUtils::makeTime(date, timeMs));
			}

			const std::string errMsg = get_string(ordObj, {"error_msg"});
			if (!errMsg.empty())
				ord->setStateMsg(errMsg.c_str());
			if (get_int64(ordObj, {"error_id"}, 0) != 0)
				ord->setError(WOEF_Normal);

			ayOrders->append(ord, false);
		}
	}

	const auto ordCnt = ayOrders->size();
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspOrders count={}", ordCnt);
	dispatchCallbackSync([this, ayOrders]() {
		if (_listener)
			_listener->onRspOrders(ayOrders);
		ayOrders->release();
	});
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspOrders count={}", ordCnt);
	write_log(_listener, LL_INFO, "[TraderZMQ] queryOrders finished count={}", ordCnt);
	return 0;
}

int TraderZMQ::queryTrades()
{
	if (_listener == nullptr || !isOperational())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] queryTrades rejected: listener={} operational={}", _listener != nullptr ? "set" : "null", isOperational());
		return -1;
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] queryTrades started");

	rapidjson::Document reqDoc;
	reqDoc.SetObject();
	auto& alloc = reqDoc.GetAllocator();
	const uint64_t seq = _id_seed.fetch_add(1) + 1;
	const std::string reqId = fmtutil::format("trades_req.{}.{}", (unsigned long long)TimeUtils::getLocalTimeNow(), (unsigned long long)seq);
	reqDoc.AddMember("type", rapidjson::Value("trades_req", alloc), alloc);
	reqDoc.AddMember("req_id", rapidjson::Value(reqId.c_str(), alloc), alloc);
	reqDoc.AddMember("case_id", rapidjson::Value(_case_id.c_str(), alloc), alloc);
	const std::string tradesReqEventId = fmtutil::format("RT:{}:{}:REQ", _case_id, reqId);
	reqDoc.AddMember("event_id", rapidjson::Value(tradesReqEventId.c_str(), alloc), alloc);
	reqDoc.AddMember("ubuntu_trades_req_create_ts_ns", nowNs(), alloc);
	reqDoc.AddMember("account_id", rapidjson::Value(_account_id.c_str(), alloc), alloc);

	rapidjson::Document ack;
	if (!sendRequest(reqDoc, ack, "trades_ack"))
		return -1;

	const bool ok = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
	if (!ok)
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] trades query rejected: {}", get_string(ack, {"error_msg"}).c_str());
		return -1;
	}

	WTSArray* ayTrades = WTSArray::create();
	if (ack.HasMember("trades") && ack["trades"].IsArray())
	{
		for (const auto& trdObj : ack["trades"].GetArray())
		{
			OrderContext ctx = resolveContext(trdObj);
			storeContext(ctx);

			std::string exchg;
			std::string code;
			normalize_code_fields(trdObj, exchg, code);
			if (code.empty())
				code = ctx.code;
			if (exchg.empty())
				exchg = ctx.exchg;
			if (code.empty())
				continue;

			WTSTradeInfo* trd = WTSTradeInfo::create(code.c_str(), exchg.c_str());
			if (!ctx.user_tag.empty())
				trd->setUserTag(ctx.user_tag.c_str());

			WTSContractInfo* cInfo = resolve_contract(_listener, code, exchg);
			if (cInfo != nullptr)
				trd->setContractInfo(cInfo);

			const std::string tradeId = get_string(trdObj, {"trade_id"});
			if (!tradeId.empty())
				trd->setTradeID(tradeId.c_str());

			const std::string orderId = get_string(trdObj, {"order_id"});
			if (!orderId.empty())
				trd->setRefOrder(orderId.c_str());
			else if (!ctx.order_id.empty())
				trd->setRefOrder(ctx.order_id.c_str());

			const std::string directionStr = get_string(trdObj, {"direction"});
			WTSDirectionType direction = map_direction(directionStr, ctx.has_direction ? ctx.direction : WDT_LONG);
			WTSOffsetType offset = map_offset(get_string(trdObj, {"offset"}), ctx.has_offset ? ctx.offset : WOT_OPEN);
			normalize_stock_side(_listener, code, exchg, directionStr, direction, offset);
			trd->setDirection(direction);
			trd->setOffsetType(offset);
			trd->setPrice(has_any(trdObj, {"price"}) ? get_number(trdObj, {"price"}) : (ctx.has_price ? ctx.price : 0.0));
			trd->setVolume(has_any(trdObj, {"volume", "qty"}) ? get_number(trdObj, {"volume", "qty"}) : (ctx.has_volume ? ctx.volume : 0.0));

			uint32_t date = 0, timeMs = 0;
			extract_datetime(trdObj, date, timeMs);
			if (date != 0)
			{
				trd->setTradeDate(date);
				trd->setTradeTime(TimeUtils::makeTime(date, timeMs));
			}

			ayTrades->append(trd, false);
		}
	}

	const auto trdCnt = ayTrades->size();
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspTrades count={}", trdCnt);
	dispatchCallbackSync([this, ayTrades]() {
		if (_listener)
			_listener->onRspTrades(ayTrades);
		ayTrades->release();
	});
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspTrades count={}", trdCnt);
	write_log(_listener, LL_INFO, "[TraderZMQ] queryTrades finished count={}", trdCnt);
	return 0;
}

int TraderZMQ::querySettlement(uint32_t uDate)
{
	if (_listener == nullptr || !isOperational())
	{
		write_log(_listener, LL_ERROR, "[TraderZMQ] querySettlement rejected: listener={} operational={} date={}", _listener != nullptr ? "set" : "null", isOperational(), uDate);
		return -1;
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] querySettlement started date={}", uDate);

	rapidjson::Document reqDoc;
	reqDoc.SetObject();
	auto& alloc = reqDoc.GetAllocator();
	const uint64_t seq = _id_seed.fetch_add(1) + 1;
	const std::string reqId = fmtutil::format("settlement_req.{}.{}", (unsigned long long)TimeUtils::getLocalTimeNow(), (unsigned long long)seq);
	reqDoc.AddMember("type", rapidjson::Value("settlement_req", alloc), alloc);
	reqDoc.AddMember("req_id", rapidjson::Value(reqId.c_str(), alloc), alloc);
	reqDoc.AddMember("case_id", rapidjson::Value(_case_id.c_str(), alloc), alloc);
	const std::string settlementReqEventId = fmtutil::format("RS:{}:{}:REQ", _case_id, reqId);
	reqDoc.AddMember("event_id", rapidjson::Value(settlementReqEventId.c_str(), alloc), alloc);
	reqDoc.AddMember("ubuntu_settlement_req_create_ts_ns", nowNs(), alloc);
	reqDoc.AddMember("account_id", rapidjson::Value(_account_id.c_str(), alloc), alloc);
	reqDoc.AddMember("date", uDate, alloc);

	rapidjson::Document ack;
	if (!sendRequest(reqDoc, ack, "settlement_ack"))
	{
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspSettlementInfo empty due to timeout date={}", uDate);
		dispatchCallbackSync([this, uDate]() {
			if (_listener)
				_listener->onRspSettlementInfo(uDate, "");
		});
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspSettlementInfo empty due to timeout date={}", uDate);
		return -1;
	}

	const bool ok = ack.HasMember("ok") && ack["ok"].IsBool() && ack["ok"].GetBool();
	if (!ok)
	{
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspSettlementInfo empty due to rejection date={}", uDate);
		dispatchCallbackSync([this, uDate]() {
			if (_listener)
				_listener->onRspSettlementInfo(uDate, "");
		});
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspSettlementInfo empty due to rejection date={}", uDate);
		return -1;
	}

	const std::string content = get_string(ack, {"content"});
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onRspSettlementInfo date={} content_size={}", uDate, content.size());
	dispatchCallbackSync([this, uDate, content]() {
		if (_listener)
			_listener->onRspSettlementInfo(uDate, content.c_str());
	});
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onRspSettlementInfo date={} content_size={}", uDate, content.size());
	write_log(_listener, LL_INFO, "[TraderZMQ] querySettlement finished date={}", uDate);
	return 0;
}

bool TraderZMQ::isCallbackThread() const
{
	std::lock_guard<std::mutex> lock(_mtx_callback_queue);
	return _callback_thread_id == std::this_thread::get_id();
}

void TraderZMQ::beginOrderApi()
{
	_order_api_active.fetch_add(1);
}

void TraderZMQ::endOrderApi()
{
	const uint32_t prev = _order_api_active.fetch_sub(1);
	if (prev <= 1)
		_cv_callback.notify_all();
}

void TraderZMQ::startCallbackDispatcher()
{
	if (_callback_worker != nullptr)
		return;

	_callback_running = true;
	write_log(_listener, LL_INFO, "[TraderZMQ] starting callback dispatcher");
	_callback_worker.reset(new StdThread([this]() { callbackLoop(); }));
}

void TraderZMQ::stopCallbackDispatcher()
{
	_callback_running = false;
	_cv_callback.notify_all();

	if (_callback_worker && _callback_worker->joinable())
	{
		if (isCallbackThread())
			_callback_worker->detach();
		else
			_callback_worker->join();
	}
	_callback_worker.reset();

	{
		std::lock_guard<std::mutex> lock(_mtx_callback_queue);
		_callback_thread_id = std::thread::id();
		_callback_queue.clear();
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] callback dispatcher stopped");
}

void TraderZMQ::enqueueCallback(std::function<void()> job)
{
	if (!job)
		return;
	if (_stopping.load())
		return;

	bool runInline = false;
	{
		std::lock_guard<std::mutex> lock(_mtx_callback_queue);
		if (!_callback_running.load())
			runInline = true;
		else
			_callback_queue.emplace_back(std::move(job));
	}

	if (runInline)
	{
		try
		{
			std::lock_guard<std::recursive_mutex> callbackLock(_mtx_callback);
			job();
		}
		catch (const std::exception& e)
		{
			write_log(_listener, LL_ERROR, "[TraderZMQ] inline callback std exception: {}", e.what());
		}
		catch (...)
		{
			write_log(_listener, LL_ERROR, "[TraderZMQ] inline callback unknown exception");
		}
		return;
	}

	_cv_callback.notify_one();
}

void TraderZMQ::dispatchCallbackSync(std::function<void()> job)
{
	if (!job)
		return;

	if (isCallbackThread() || !_callback_running.load())
	{
		std::lock_guard<std::recursive_mutex> callbackLock(_mtx_callback);
		job();
		return;
	}

	struct WaitState
	{
		std::mutex mtx;
		std::condition_variable cv;
		bool done{false};
	};
	auto state = std::make_shared<WaitState>();
	enqueueCallback([job = std::move(job), state]() mutable {
		try
		{
			if (job)
				job();
		}
		catch (...)
		{
		}
		{
			std::lock_guard<std::mutex> lock(state->mtx);
			state->done = true;
		}
		state->cv.notify_one();
	});

	std::unique_lock<std::mutex> lock(state->mtx);
	state->cv.wait(lock, [&state]() { return state->done; });
}

void TraderZMQ::callbackLoop()
{
	{
		std::lock_guard<std::mutex> lock(_mtx_callback_queue);
		_callback_thread_id = std::this_thread::get_id();
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] callback dispatcher loop started");
	while (true)
	{
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(_mtx_callback_queue);
			_cv_callback.wait(lock, [this]() {
				return !_callback_running.load() || (!_callback_queue.empty() && _order_api_active.load() == 0);
			});
			if (!_callback_running.load() && _callback_queue.empty())
				break;
			if (_callback_running.load() && _order_api_active.load() > 0)
				continue;
			job = std::move(_callback_queue.front());
			_callback_queue.pop_front();
		}

		try
		{
			std::lock_guard<std::recursive_mutex> callbackLock(_mtx_callback);
			if (job)
				job();
		}
		catch (const std::exception& e)
		{
			write_log(_listener, LL_ERROR, "[TraderZMQ] callback dispatcher std exception: {}", e.what());
		}
		catch (...)
		{
			write_log(_listener, LL_ERROR, "[TraderZMQ] callback dispatcher unknown exception");
		}
	}
	{
		std::lock_guard<std::mutex> lock(_mtx_callback_queue);
		_callback_thread_id = std::thread::id();
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] callback dispatcher loop exited");
}

void TraderZMQ::startWorker()
{
	if (_worker != nullptr)
	{
		write_log(_listener, LL_WARN, "[TraderZMQ] startWorker ignored: already started");
		return;
	}

	startCallbackDispatcher();
	write_log(_listener, LL_INFO, "[TraderZMQ] starting report worker");
	_worker.reset(new StdThread([this]() { workerLoop(); }));
}

void TraderZMQ::stopWorker()
{
	write_log(_listener, LL_INFO, "[TraderZMQ] stopping report worker");
	_running = false;
	_connected = false;
	_logged_in = false;

	if (_worker && _worker->joinable())
		_worker->join();
	_worker.reset();
	stopCallbackDispatcher();

	{
		std::lock_guard<StdUniqueMutex> lock(_mtx_sock);
		try
		{
			if (_sub)
				_sub->close();
		}
		catch (...)
		{
		}

		try
		{
			if (_req)
				_req->close();
		}
		catch (...)
		{
		}

		_sub.reset();
		_req.reset();
		_ctx.reset();
	}

	{
		std::lock_guard<StdUniqueMutex> lock(_mtx_maps);
		_req_ctx.clear();
		_order_ctx.clear();
	}

	{
		std::lock_guard<StdUniqueMutex> lock(_mtx_pending);
		_pending_submitting.clear();
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] report worker stopped");
}

void TraderZMQ::workerLoop()
{
	write_log(_listener, LL_INFO, "[TraderZMQ] report worker loop started");
	while (_running.load())
	{
		try
		{
			if (_sub == nullptr)
				return;

			zmq::message_t first;
			auto ok = _sub->recv(first, zmq::recv_flags::none);
			if (!ok)
				continue;

			bool more = _sub->get(zmq::sockopt::rcvmore);
			std::string payload;
			if (!more)
			{
				payload.assign(static_cast<const char*>(first.data()), first.size());
			}
			else
			{
				while (more)
				{
					zmq::message_t part;
					if (!_sub->recv(part, zmq::recv_flags::none))
						break;
					payload.assign(static_cast<const char*>(part.data()), part.size());
					more = _sub->get(zmq::sockopt::rcvmore);
				}
			}

			if (payload.empty())
				continue;

			rapidjson::Document doc;
			doc.Parse(payload.c_str());
			if (!doc.IsObject())
			{
				write_log(_listener, LL_WARN, "[TraderZMQ] ignored invalid report payload");
				continue;
			}
			const uint64_t ubuntuEventRecvTsNs = nowNs();

			const std::string type = get_string(doc, {"type"});
			writeStatsEvent(type.c_str(), &doc, "report", 0, ubuntuEventRecvTsNs);
			write_log(_listener,
				LL_DEBUG,
				"[TraderZMQ] received report type={} req_id={} order_id={} user_tag={}",
				type.c_str(),
				get_string(doc, {"req_id"}).c_str(),
				get_string(doc, {"order_id"}).c_str(),
				get_string(doc, {"user_tag"}).c_str());
			if (type == "order_update" || type == "trade_update" || type == "order_error")
			{
				const std::string reportPayload = payload;
				enqueueCallback([this, reportPayload]() {
					rapidjson::Document cbDoc;
					cbDoc.Parse(reportPayload.c_str());
					if (!cbDoc.IsObject())
					{
						write_log(_listener, LL_WARN, "[TraderZMQ] ignored invalid queued report payload");
						return;
					}

					const std::string cbType = get_string(cbDoc, {"type"});
					if (cbType == "order_update")
						handleOrderUpdate(cbDoc);
					else if (cbType == "trade_update")
						handleTradeUpdate(cbDoc);
					else if (cbType == "order_error")
						handleOrderError(cbDoc);
					else
						write_log(_listener, LL_WARN, "[TraderZMQ] ignored unknown queued report type={}", cbType.c_str());
				});
			}
			else
				write_log(_listener, LL_WARN, "[TraderZMQ] ignored unknown report type={}", type.c_str());
		}
		catch (const zmq::error_t& e)
		{
			if (_running.load())
				write_log(_listener, LL_ERROR, "[TraderZMQ] recv failed: {}", e.what());
		}
		catch (const std::exception& e)
		{
			if (_running.load())
				write_log(_listener, LL_ERROR, "[TraderZMQ] report worker std exception: {}", e.what());
		}
		catch (...)
		{
			if (_running.load())
				write_log(_listener, LL_ERROR, "[TraderZMQ] report worker unknown exception");
		}
	}
	write_log(_listener, LL_INFO, "[TraderZMQ] report worker loop exited");
}

void TraderZMQ::handleOrderUpdate(const rapidjson::Document& doc)
{
	if (_listener == nullptr)
		return;

	OrderContext ctx = resolveContext(doc);
	storeContext(ctx);

	const WTSOrderState state = map_status(get_string(doc, {"status"}));
	emitSubmittingIfNeeded(ctx, state);
	const bool terminalState = is_terminal_state(state);
	if (terminalState && hasTerminalEmitted(ctx))
	{
		write_log(_listener,
			LL_WARN,
			"[TraderZMQ] skipped duplicate terminal order_update req_id={} order_id={} status={}",
			ctx.req_id.c_str(),
			ctx.order_id.c_str(),
			get_string(doc, {"status"}).c_str());
		return;
	}

	std::string exchg;
	std::string code;
	normalize_code_fields(doc, exchg, code);
	if (code.empty())
		code = ctx.code;
	if (exchg.empty())
		exchg = ctx.exchg;
	if (code.empty())
		return;

	WTSOrderInfo* ord = WTSOrderInfo::create();
	ord->setExchange(exchg.c_str());
	ord->setCode(code.c_str());
	if (!ctx.user_tag.empty())
		ord->setUserTag(ctx.user_tag.c_str());
	if (!ctx.req_id.empty())
		ord->setEntrustID(ctx.req_id.c_str());
	else
		ord->setEntrustID(get_string(doc, {"local_id", "order_id"}).c_str());

	const std::string orderId = get_string(doc, {"order_id"});
	if (!orderId.empty())
		ord->setOrderID(orderId.c_str());
	else if (!ctx.order_id.empty())
		ord->setOrderID(ctx.order_id.c_str());

	WTSContractInfo* cInfo = resolve_contract(_listener, code, exchg);
	if (cInfo != nullptr)
		ord->setContractInfo(cInfo);

	const std::string directionStr = get_string(doc, {"direction"});
	WTSDirectionType direction = map_direction(directionStr, ctx.has_direction ? ctx.direction : WDT_LONG);
	WTSOffsetType offset = map_offset(get_string(doc, {"offset"}), ctx.has_offset ? ctx.offset : WOT_OPEN);
	normalize_stock_side(_listener, code, exchg, directionStr, direction, offset);
	ord->setDirection(direction);
	ord->setOffsetType(offset);
	ord->setPriceType(map_price_type(get_string(doc, {"price_type"}), ctx.has_price_type ? ctx.price_type : WPT_ANYPRICE));
	ord->setOrderFlag(map_order_flag(get_string(doc, {"order_flag"}), ctx.has_order_flag ? ctx.order_flag : WOF_NOR));

	const bool hasVolume = has_any(doc, {"volume", "qty"});
	const double totalVolume = hasVolume ? get_number(doc, {"volume", "qty"}) : (ctx.has_volume ? ctx.volume : 0.0);
	double volTraded = get_number(doc, {"vol_traded", "traded_volume"}, 0.0);
	double volLeft = has_any(doc, {"vol_left", "left_volume"}) ? get_number(doc, {"vol_left", "left_volume"}) : std::max(0.0, totalVolume - volTraded);
	if (!has_any(doc, {"vol_traded", "traded_volume"}) && totalVolume > 0.0 && volLeft <= totalVolume)
		volTraded = totalVolume - volLeft;
	if (!terminalState && isDuplicateOrderUpdate(ctx, state, volTraded, volLeft))
	{
		write_log(_listener,
			LL_WARN,
			"[TraderZMQ] skipped duplicate live order_update req_id={} order_id={} status={}",
			ctx.req_id.c_str(),
			ctx.order_id.c_str(),
			get_string(doc, {"status"}).c_str());
		return;
	}

	ord->setPrice(has_any(doc, {"price"}) ? get_number(doc, {"price"}) : (ctx.has_price ? ctx.price : 0.0));
	ord->setVolume(totalVolume);
	ord->setVolTraded(volTraded);
	ord->setVolLeft(volLeft);
	ord->setOrderState(state);

	uint32_t date = 0, timeMs = 0;
	extract_datetime(doc, date, timeMs);
	if (date != 0)
	{
		ord->setOrderDate(date);
		ord->setOrderTime(TimeUtils::makeTime(date, timeMs));
	}

	const std::string errMsg = get_string(doc, {"error_msg"});
	if (!errMsg.empty())
		ord->setStateMsg(errMsg.c_str());
	if (get_int64(doc, {"error_id"}, 0) != 0)
		ord->setError(WOEF_Normal);

	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onPushOrder req_id={} order_id={}", ctx.req_id.c_str(), orderId.empty() ? ctx.order_id.c_str() : orderId.c_str());
	{
		std::lock_guard<std::recursive_mutex> callbackLock(_mtx_callback);
		_listener->onPushOrder(ord);
	}
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onPushOrder req_id={} order_id={}", ctx.req_id.c_str(), orderId.empty() ? ctx.order_id.c_str() : orderId.c_str());
	ord->release();
	markOrderSnapshot(ctx, state, volTraded, volLeft);
	if (terminalState)
		markTerminalEmitted(ctx);

	write_log(_listener, LL_INFO,
		"[TraderZMQ] order_update req_id={} order_id={} status={} user_tag={} vol_traded={} vol_left={} cross_ms={} bridge_seq={}",
		ctx.req_id.c_str(),
		orderId.empty() ? ctx.order_id.c_str() : orderId.c_str(),
		get_string(doc, {"status"}).c_str(),
		ctx.user_tag.c_str(),
		format_number(volTraded, 4),
		format_number(volLeft, 4),
		elapsed_ms(get_uint64(doc, {"win_event_pub_ts_ns"}, 0), nowNs()),
		get_uint64(doc, {"bridge_seq"}, 0));
}

void TraderZMQ::handleTradeUpdate(const rapidjson::Document& doc)
{
	if (_listener == nullptr)
		return;

	OrderContext ctx = resolveContext(doc);
	storeContext(ctx);

	std::string exchg;
	std::string code;
	normalize_code_fields(doc, exchg, code);
	if (code.empty())
		code = ctx.code;
	if (exchg.empty())
		exchg = ctx.exchg;
	if (code.empty())
		return;

	WTSTradeInfo* trd = WTSTradeInfo::create(code.c_str(), exchg.c_str());
	if (!ctx.user_tag.empty())
		trd->setUserTag(ctx.user_tag.c_str());

	WTSContractInfo* cInfo = resolve_contract(_listener, code, exchg);
	if (cInfo != nullptr)
		trd->setContractInfo(cInfo);

	const std::string tradeId = get_string(doc, {"trade_id"});
	if (!tradeId.empty())
		trd->setTradeID(tradeId.c_str());

	const std::string orderId = get_string(doc, {"order_id"});
	if (!orderId.empty())
		trd->setRefOrder(orderId.c_str());
	else if (!ctx.order_id.empty())
		trd->setRefOrder(ctx.order_id.c_str());

	const std::string directionStr = get_string(doc, {"direction"});
	WTSDirectionType direction = map_direction(directionStr, ctx.has_direction ? ctx.direction : WDT_LONG);
	WTSOffsetType offset = map_offset(get_string(doc, {"offset"}), ctx.has_offset ? ctx.offset : WOT_OPEN);
	normalize_stock_side(_listener, code, exchg, directionStr, direction, offset);
	trd->setDirection(direction);
	trd->setOffsetType(offset);
	trd->setPrice(has_any(doc, {"price"}) ? get_number(doc, {"price"}) : (ctx.has_price ? ctx.price : 0.0));
	trd->setVolume(has_any(doc, {"volume", "qty"}) ? get_number(doc, {"volume", "qty"}) : (ctx.has_volume ? ctx.volume : 0.0));

	uint32_t date = 0, timeMs = 0;
	extract_datetime(doc, date, timeMs);
	if (date != 0)
	{
		trd->setTradeDate(date);
		trd->setTradeTime(TimeUtils::makeTime(date, timeMs));
	}

	write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onPushTrade req_id={} trade_id={}", ctx.req_id.c_str(), tradeId.c_str());
	{
		std::lock_guard<std::recursive_mutex> callbackLock(_mtx_callback);
		_listener->onPushTrade(trd);
	}
	write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onPushTrade req_id={} trade_id={}", ctx.req_id.c_str(), tradeId.c_str());
	trd->release();

	write_log(_listener, LL_INFO,
		"[TraderZMQ] trade_update req_id={} order_id={} trade_id={} user_tag={} price={} volume={} cross_ms={} bridge_seq={}",
		ctx.req_id.c_str(),
		orderId.empty() ? ctx.order_id.c_str() : orderId.c_str(),
		tradeId.c_str(),
		ctx.user_tag.c_str(),
		format_number(has_any(doc, {"price"}) ? get_number(doc, {"price"}) : (ctx.has_price ? ctx.price : 0.0), 6),
		format_number(has_any(doc, {"volume", "qty"}) ? get_number(doc, {"volume", "qty"}) : (ctx.has_volume ? ctx.volume : 0.0), 4),
		elapsed_ms(get_uint64(doc, {"win_event_pub_ts_ns"}, 0), nowNs()),
		get_uint64(doc, {"bridge_seq"}, 0));
}

void TraderZMQ::handleOrderError(const rapidjson::Document& doc)
{
	const std::string errMsg = get_string(doc, {"error_msg"});
	const int errId = (int)get_int64(doc, {"error_id"}, -1);

	OrderContext ctx = resolveContext(doc);
	storeContext(ctx);
	const std::string source = upper_copy(get_string(doc, {"error_action", "source", "action"}));
	const bool isCancelError = source == "CANCEL" || ctx.req_id.find("cancel") != std::string::npos;

	if (_listener)
	{
		if (!isCancelError && !ctx.user_tag.empty() && !ctx.code.empty())
		{
			emitSubmittingIfNeeded(ctx, WOS_Canceled);
			if (hasTerminalEmitted(ctx))
			{
				write_log(_listener,
					LL_WARN,
					"[TraderZMQ] skipped duplicate synthetic error order req_id={} order_id={}",
					ctx.req_id.c_str(),
					ctx.order_id.c_str());
			}
			else
			{
				emitSyntheticErrorOrder(ctx, errMsg.c_str());
				markTerminalEmitted(ctx);
			}
		}

		const WTSErroCode ec = isCancelError ? WEC_ORDERCANCEL : WEC_UNKNOWN;
		WTSError* err = WTSError::create(ec, errMsg.empty() ? "order_error" : errMsg.c_str());
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notifying onTraderError req_id={} error_id={}", ctx.req_id.c_str(), errId);
		{
			std::lock_guard<std::recursive_mutex> callbackLock(_mtx_callback);
			_listener->onTraderError(err);
		}
		write_log(_listener, LL_DEBUG, "[TraderZMQ] notified onTraderError req_id={} error_id={}", ctx.req_id.c_str(), errId);
		err->release();
	}

	write_log(_listener, LL_ERROR,
		"[TraderZMQ] order_error error_id={} req_id={} order_id={} user_tag={} cancel_error={} msg={}",
		errId,
		ctx.req_id.c_str(),
		ctx.order_id.c_str(),
		ctx.user_tag.c_str(),
		isCancelError,
		errMsg.c_str());
}

uint64_t TraderZMQ::nowNs() const
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

void TraderZMQ::writeStatsEvent(const char* eventType, const rapidjson::Value* payload, const char* reqType,
	uint64_t ubuntuSendTsNs, uint64_t ubuntuRecvTsNs)
{
	if (!_stats_enable || _stats_path.empty())
		return;

	std::string reqId;
	std::string orderId;
	std::string userTag;
	std::string eventId;
	uint64_t winReqRxTsNs = 0;
	uint64_t winAckPubTsNs = 0;
	uint64_t winEventPubTsNs = 0;
	if (payload != nullptr && payload->IsObject())
	{
		reqId = get_string(*payload, {"req_id"});
		orderId = get_string(*payload, {"order_id"});
		userTag = get_string(*payload, {"user_tag"});
		eventId = get_string(*payload, {"event_id"});
		winReqRxTsNs = get_uint64(*payload, {"win_req_rx_ts_ns"}, 0);
		winAckPubTsNs = get_uint64(*payload, {"win_ack_pub_ts_ns"}, 0);
		winEventPubTsNs = get_uint64(*payload, {"win_event_pub_ts_ns"}, 0);
	}

	int64_t crossMachineLatencyMs = -1;
	const uint64_t winBaseTsNs = winAckPubTsNs != 0 ? winAckPubTsNs : winEventPubTsNs;
	if (winBaseTsNs != 0 && ubuntuRecvTsNs >= winBaseTsNs)
		crossMachineLatencyMs = (int64_t)((ubuntuRecvTsNs - winBaseTsNs) / 1000000ULL);
	int64_t roundTripMs = -1;
	if (ubuntuSendTsNs != 0 && ubuntuRecvTsNs >= ubuntuSendTsNs)
		roundTripMs = (int64_t)((ubuntuRecvTsNs - ubuntuSendTsNs) / 1000000ULL);

	rapidjson::StringBuffer payloadBuffer;
	if (payload != nullptr)
	{
		rapidjson::Writer<rapidjson::StringBuffer> payloadWriter(payloadBuffer);
		payload->Accept(payloadWriter);
	}

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("case_id"); writer.String(_case_id.c_str());
	writer.Key("event_type"); writer.String(eventType == nullptr ? "" : eventType);
	writer.Key("req_type"); writer.String(reqType == nullptr ? "" : reqType);
	writer.Key("event_id"); writer.String(eventId.c_str());
	writer.Key("req_id"); writer.String(reqId.c_str());
	writer.Key("order_id"); writer.String(orderId.c_str());
	writer.Key("user_tag"); writer.String(userTag.c_str());
	writer.Key("ubuntu_send_ts_ns"); writer.Uint64(ubuntuSendTsNs);
	writer.Key("ubuntu_recv_ts_ns"); writer.Uint64(ubuntuRecvTsNs);
	writer.Key("win_req_rx_ts_ns"); writer.Uint64(winReqRxTsNs);
	writer.Key("win_ack_pub_ts_ns"); writer.Uint64(winAckPubTsNs);
	writer.Key("win_event_pub_ts_ns"); writer.Uint64(winEventPubTsNs);
	writer.Key("round_trip_ms"); writer.Int64(roundTripMs);
	writer.Key("cross_machine_latency_ms"); writer.Int64(crossMachineLatencyMs);
	writer.Key("order_ack_warn_ms"); writer.Int(_order_ack_warn_ms);
	writer.Key("trade_update_warn_ms"); writer.Int(_trade_update_warn_ms);
	writer.Key("payload"); writer.String(payloadBuffer.GetString());
	writer.EndObject();

	try
	{
		std::lock_guard<StdUniqueMutex> lock(_mtx_stats);
		std::ofstream out(_stats_path, std::ios::out | std::ios::app);
		if (out)
			out << buffer.GetString() << '\n';
	}
	catch (...)
	{
	}
}
