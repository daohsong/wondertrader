/*!
 * \file ParserZMQ.cpp
 * \project	WonderTrader
 *
 * \author Codex
 * \date 2025/12/17
 *
 * \brief ZMQ quote parser for MiniQMT/xtquant bridge
 *
 * 模块工作流程：
 * 1）init()：从 WT 的配置节点读取 ZMQ 地址与端口（quote_host/quote_port、ctrl_host/ctrl_port），并写入成员变量。
 *    - quote 通道：SUB 连接到行情 PUB（默认 tcp://<host>:5556）。
 *    - ctrl 通道：REQ 连接到控制 REP（默认 tcp://<host>:5555），用于向服务器发送订阅增删/清空指令。
 *
 * 2）registerSpi()：注册 IParserSpi 回调指针（_sink_）。
 *    - 通过 _sink_->handleParserLog() 输出日志；
 *    - 通过 _sink_->handleQuote() 把 tick 推送给 WT；
 *    - 通过 _sink_->getBaseDataMgr() 获取合约信息以便做代码规范化。
 *
 * 3）connect()：
 *    - 创建 ZMQ context；
 *    - 创建 SUB socket（订阅全部 topic，避免跨线程变更订阅导致 sockopt 竞争），并连接行情 PUB；
 *    - 创建 ctrl REQ socket（best-effort），并连接控制端口；
 *    - 将本地 _subs_ 作为初始订阅集合，向 ctrl 端发送 "add"（服务器侧维护实际订阅列表）；
 *    - 启动后台线程 worker_loop() 持续接收并解析行情；
 *    - 向上层回调 WPE_Connect / WPE_Login 事件。
 *
 * 4）subscribe()/unsubscribe()：
 *    - 先把输入 code 规范化为 WT 标准码（normalize_code），并在互斥锁保护下更新 _subs_；
 *    - 再通过 ctrl 通道发送 "add"/"remove" 指令（服务器侧按 codes 更新订阅）。
 *
 * 5）worker_loop()：
 *    - 从 SUB socket 接收消息，兼容两种格式：
 *      a) multipart：第一帧是 topic，最后一帧是 JSON payload；
 *      b) single-frame：整帧即 JSON payload；
 *    - parse_quote() 校验 JSON 与 type=="quote"，并做一次 best-effort 的本地订阅过滤；
 *    - fill_tick() 将 JSON 字段映射到 WTSTickStruct/WTSTickData，并通过 _sink_->handleQuote() 推送给 WT。
 *
 * 6）disconnect()/release()/析构：
 *    - stop_worker() 停止线程、关闭 socket、释放 context；
 *    - 通过事件回调通知上层断开（WPE_Close）。
 *
 * 注意：
 * - _subs_ 由 _mtx_ 保护；worker_loop() 仅在 topic 非空且 _subs_ 非空时做本地过滤。
 * - ctrl 通道为 best-effort：发送失败只记录日志，不影响行情接收线程继续运行。
 */

#include "ParserZMQ.h"

#include "../Includes/WTSDataDef.hpp"
#include "../Includes/WTSContractInfo.hpp"
#include "../Includes/WTSVariant.hpp"
#include "../Includes/IBaseDataMgr.h"
#include "../Share/TimeUtils.hpp"
#include "../Share/fmtlib.h"

#include <cstring>
#include <cstdio>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using namespace std::chrono_literals;

extern "C"
{
	EXPORT_FLAG IParserApi* createParser()
	{
		// 输入：无（由 WT 插件加载器/工厂在运行时调用）。
		// 输出：返回新创建的 ParserZMQ 实例指针（IParserApi*）。
		// 逻辑：作为动态库导出的工厂函数，负责创建行情解析器对象。
		return new ParserZMQ();
	}

	EXPORT_FLAG void deleteParser(IParserApi*& parser)
	{
		// 输入：parser 引用（可能为 nullptr）。
		// 输出：无；若 parser 非空则 delete 并将引用置为 nullptr，避免悬挂指针。
		// 逻辑：与 createParser() 配套的销毁函数，供插件卸载/释放阶段调用。
		if (parser != nullptr)
		{
			delete parser;
			parser = nullptr;
		}
	}
}

namespace
{
	template<typename... Args>
	inline void write_log(IParserSpi* sink, WTSLogLevel ll, const char* format, const Args&... args)
	{
		// 输入：sink 为日志接收回调；ll 为日志级别；format/args 为 fmt 风格格式化参数。
		// 输出：无返回值；副作用是调用 sink->handleParserLog() 输出格式化后的日志字符串。
		// 逻辑：避免使用固定长度缓冲区（fmt::format_to 到 char* 会造成越界写），改为安全地生成临时字符串。
		if (sink == nullptr)
			return;
		try
		{
			auto msg = fmt::format(format, args...);
			sink->handleParserLog(ll, msg.c_str());
		}
		catch (...)
		{
			sink->handleParserLog(LL_ERROR, "[ParserZMQ] 日志格式化失败");
		}
	}

	std::string make_endpoint(const std::string& host, int32_t port)
	{
		// 输入：host（IP/域名），port（端口号）。
		// 输出：形如 "tcp://<host>:<port>" 的 ZMQ endpoint 字符串。
		// 逻辑：统一生成 ZMQ 连接地址，避免各处手写拼接。
		return fmtutil::format("tcp://{}:{}", host, port);
	}

	std::string upper_copy(const std::string& src)
	{
		std::string ret = src;
		std::transform(ret.begin(), ret.end(), ret.begin(), [](unsigned char c) { return (char)std::toupper(c); });
		return ret;
	}

	bool split_xt_suffix_code(const std::string& code, std::string& exchg, std::string& raw)
	{
		const auto pos = code.find('.');
		if (pos == std::string::npos || code.find('.', pos + 1) != std::string::npos)
			return false;

		const std::string lhs = code.substr(0, pos);
		const std::string rhs = upper_copy(code.substr(pos + 1));
		if (rhs == "SH")
			exchg = "SSE";
		else if (rhs == "SZ")
			exchg = "SZSE";
		else if (rhs == "BJ")
			exchg = "BSE";
		else
			return false;

		raw = lhs;
		return !raw.empty();
	}

	void split_std_code(const std::string& fullCode, std::string& exchg, std::string& rawCode)
	{
		if (fullCode.empty())
			return;

		std::string xtExchg;
		std::string xtRaw;
		if (split_xt_suffix_code(fullCode, xtExchg, xtRaw))
		{
			exchg = xtExchg;
			rawCode = xtRaw;
			return;
		}

		std::vector<std::string> items;
		items.reserve(4);
		size_t start = 0;
		while (true)
		{
			auto pos = fullCode.find('.', start);
			if (pos == std::string::npos)
			{
				items.emplace_back(fullCode.substr(start));
				break;
			}
			items.emplace_back(fullCode.substr(start, pos - start));
			start = pos + 1;
		}
		if (items.size() >= 2)
		{
			exchg = items.front();
			rawCode = items.back();
		}
	}
}

ParserZMQ::ParserZMQ()
{
	// 输入：无。
	// 输出：构造一个未初始化/未连接的 ParserZMQ 实例；不在此处创建网络与线程资源。
	// 逻辑：保持构造函数轻量，参数读取在 init()，连接与线程启动在 connect()。
}

ParserZMQ::~ParserZMQ()
{
	// 输入：无。
	// 输出：析构时确保后台线程与 ZMQ 资源被释放。
	// 逻辑：调用 stop_worker()，停止接收线程、关闭 socket、释放 context，避免资源泄漏。
	stopping_.store(true);
	stop_worker();
}

bool ParserZMQ::init(WTSVariant* config)
{
	// 输入：config（WT 侧传入的配置对象，期望包含 host/port 等参数）。
	// 输出：true 表示成功读取并保存配置；false 表示 config 为空，无法初始化。
	// 逻辑：
	// - 读取 quote_host/quote_port（兼容 host/port 作为别名），作为行情 PUB 的连接地址；
	// - 读取 ctrl_host/ctrl_port，作为控制 REP 的连接地址；若 ctrl_host 为空则继承 quote_host；
	// - 仅保存参数，不在此处发起网络连接或启动线程。
	if (config == nullptr)
		return false;

	_quote_host_ = config->getCString("quote_host");
	if (_quote_host_.empty())
		_quote_host_ = config->getCString("host", "192.168.1.8");
	_quote_port_ = config->getInt32("quote_port");
	if (_quote_port_ == 0)
		_quote_port_ = config->getInt32("port", 5556);

	_ctrl_host_ = config->getCString("ctrl_host");
	if (_ctrl_host_.empty())
		_ctrl_host_ = _quote_host_;
	_ctrl_port_ = config->getInt32("ctrl_port");
	if (_ctrl_port_ == 0)
		_ctrl_port_ = 5555;

	_ctrl_timeout_ms_ = config->getInt32("ctrl_timeout_ms");
	if (_ctrl_timeout_ms_ <= 0)
		_ctrl_timeout_ms_ = 5000;
	_stats_enable_ = config->getBoolean("stats_enable");
	_stats_path_ = config->getCString("stats_path");
	_case_id_ = config->getCString("case_id", "WTZMQ2_LIVE");
	if (_stats_enable_ && _stats_path_.empty())
		_stats_path_ = fmtutil::format("/tmp/{}_parser.jsonl", _case_id_);
	_latency_warn_ms_ = config->getInt32("latency_warn_ms");
	if (_latency_warn_ms_ <= 0)
		_latency_warn_ms_ = 300;
	_latency_error_ms_ = config->getInt32("latency_error_ms");
	if (_latency_error_ms_ <= 0)
		_latency_error_ms_ = 1000;

	return true;
}

void ParserZMQ::release()
{
	// 输入：无。
	// 输出：无返回值；副作用是停止线程/释放资源。
	// 逻辑：ParserAdapter::release() 会在调用本方法后通过 deleteParser() 销毁实例，
	// 因此这里不能自删除，否则 shutdown 时会形成重复销毁。
	write_simple_stats("parser_release", "", "", "", 0, now_ns());
	disconnect();
	detach_sink();
}

void ParserZMQ::registerSpi(IParserSpi* listener)
{
	// 输入：listener（WT 侧回调接口指针）。
	// 输出：无；副作用是保存到成员 _sink_，后续用于日志输出、合约查询、行情回调与事件通知。
	// 逻辑：WT 在创建/初始化 Parser 后会注册回调对象，本实现不做额外处理。
	StdUniqueLock lock(_sink_mtx_);
	_sink_ = listener;
}

bool ParserZMQ::connect()
{
	// 输入：无（使用 init() 已保存的 host/port 以及已注册的 _sink_）。
	// 输出：true 表示已进入“运行中”状态（线程已启动并开始接收）；重复调用在 running_ 为 true 时直接返回 true。
	// 逻辑：
	// - 初始化 ZMQ context；
	// - 创建 SUB socket 连接行情 PUB，并订阅全部 topic（服务器端通过 ctrl 通道控制订阅集合）；
	// - 创建 ctrl REQ socket 连接控制端口，并尝试发送当前订阅集合的 "add"；
	// - 启动 worker 线程；
	// - 通过 _sink_ 通知上层连接与登录事件。
	if (running_.load())
		return true;
	stopping_.store(false);

	if (!ctx_)
		ctx_ = std::make_unique<zmq::context_t>(1);

	sub_sock_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::sub);
	sub_sock_->set(zmq::sockopt::linger, 0);
	sub_sock_->set(zmq::sockopt::rcvtimeo, 1000);
	// Server side manages subscription list through control channel.
	// Subscribe to all topics here to avoid cross-thread sockopt changes.
	sub_sock_->set(zmq::sockopt::subscribe, "");

	sub_sock_->connect(make_endpoint(_quote_host_, _quote_port_));
	write_log(get_sink(),
		LL_INFO,
		"[ParserZMQ] connect quote_endpoint={} ctrl_endpoint={} stats_enable={} stats_path={} case_id={}",
		make_endpoint(_quote_host_, _quote_port_).c_str(),
		make_endpoint(_ctrl_host_, _ctrl_port_).c_str(),
		_stats_enable_ ? "true" : "false",
		_stats_path_.c_str(),
		_case_id_.c_str());

	// control channel (best effort)
	{
		StdUniqueLock lock(_ctrl_mtx_);
		ctrl_sock_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::req);
		ctrl_sock_->set(zmq::sockopt::linger, 0);
		ctrl_sock_->set(zmq::sockopt::sndtimeo, _ctrl_timeout_ms_);
		ctrl_sock_->set(zmq::sockopt::rcvtimeo, _ctrl_timeout_ms_);
		// 允许在超时后继续发送下一条请求（避免 REQ 进入 EFSM 状态导致后续订阅永远失败）
		try
		{
#ifdef ZMQ_REQ_RELAXED
			ctrl_sock_->set(zmq::sockopt::req_relaxed, 1);
#endif
#ifdef ZMQ_REQ_CORRELATE
			ctrl_sock_->set(zmq::sockopt::req_correlate, 1);
#endif
		}
		catch (...)
		{
		}
		ctrl_sock_->connect(make_endpoint(_ctrl_host_, _ctrl_port_));
	}

	send_control(_subs_, "add");

	running_.store(true);
	start_worker();

	IParserSpi* sink = get_sink();
	if (sink)
	{
		sink->handleEvent(WPE_Connect, 0);
		sink->handleEvent(WPE_Login, 0);
	}

	return true;
}

bool ParserZMQ::disconnect()
{
	// 输入：无。
	// 输出：true（本实现为 best-effort 断开；即使 ctrl 清空失败也会继续停止线程并返回 true）。
	// 逻辑：
	// - 通过 ctrl 通道发送 "clear" 清空服务器订阅（best-effort）；
	// - stop_worker() 停止后台线程并释放 socket/context；
	// - 通知上层关闭事件（WPE_Close）。
	write_simple_stats("parser_disconnect", "", "", "", 0, now_ns());
	IParserSpi* sink = get_sink();
	send_control("clear");
	stop_worker();
	if (sink)
	{
		sink->handleEvent(WPE_Close, 0);
		registerSpi(sink);
	}
	return true;
}

bool ParserZMQ::isConnected()
{
	// 输入：无。
	// 输出：running_；true 表示 worker 线程处于运行状态且 socket 已创建（语义上“已连接”）。
	// 逻辑：WT 用于轮询查询连接状态，本实现用 running_ 作为连接状态标记。
	return running_.load();
}

void ParserZMQ::subscribe(const CodeSet& vecSymbols)
{
	// 输入：vecSymbols（WT 请求订阅的代码集合，可能是 rawCode/altCode/stdCode 混合）。
	// 输出：无；副作用：
	// - 更新本地订阅集合 _subs_；
	// - 通过 ctrl 通道向服务器发送 "add" 指令，要求服务器开始推送这些代码的行情。
	// 逻辑：先 normalize_code() 统一代码格式，再在互斥锁下更新集合，最后一次性发送控制消息。
	CodeSet normCodes;
	{
		StdUniqueLock lock(_mtx_);
		for (const auto& code : vecSymbols)
		{
			auto norm = normalize_code(code);
			if (norm.empty())
				continue;
			_subs_.insert(norm);
			normCodes.insert(norm);
		}
	}
	send_control(normCodes, "add");
	write_log(get_sink(), LL_INFO, "[ParserZMQ] subscribe requested input_count={} normalized_count={} total_subs={}",
		vecSymbols.size(), normCodes.size(), _subs_.size());
}

void ParserZMQ::unsubscribe(const CodeSet& vecSymbols)
{
	// 输入：vecSymbols（WT 请求取消订阅的代码集合）。
	// 输出：无；副作用：
	// - 从本地订阅集合 _subs_ 中移除；
	// - 通过 ctrl 通道向服务器发送 "remove" 指令，要求服务器停止推送这些代码的行情。
	// 逻辑：同 subscribe()，先规范化代码，再更新集合并发送控制消息。
	CodeSet normCodes;
	{
		StdUniqueLock lock(_mtx_);
		for (const auto& code : vecSymbols)
		{
			auto norm = normalize_code(code);
			if (norm.empty())
				continue;
			_subs_.erase(norm);
			normCodes.insert(norm);
		}
	}
	send_control(normCodes, "remove");
	write_log(get_sink(), LL_INFO, "[ParserZMQ] unsubscribe requested input_count={} normalized_count={} total_subs={}",
		vecSymbols.size(), normCodes.size(), _subs_.size());
}

std::string ParserZMQ::normalize_code(const std::string& code) const
{
	// 输入：code（可能是 stdCode，如 "SSE.STK.600000"；也可能是 "SSE.600000"/"600000.SH" 或仅 "600000"）。
	// 输出：返回尽量规范化后的 stdCode（形如 "<exchg>.<product>.<code>"），失败则返回原值。
	// 逻辑：
	// - 若已包含 >=2 个 '.'，认为已是 stdCode，直接返回；
	// - 否则尝试通过 _sink_->getBaseDataMgr() 查询合约，将 "SSE.600000"/"600000" 扩展成完整 pid（如 "SSE.STK.600000"）。
	if (code.empty())
		return code;

	// Already stdCode like SSE.STK.600000
	size_t dotCnt = 0;
	for (char c : code)
		if (c == '.')
			++dotCnt;
	if (dotCnt >= 2)
		return code;

	// Try to expand fullAltCode (SSE.600000) to stdCode via base data mgr
	IParserSpi* sink = get_sink();
	if (!sink)
		return code;
	auto* mgr = sink->getBaseDataMgr();
	if (!mgr)
		return code;

	std::string exchg;
	std::string raw;
	auto pos = code.find('.');
	if (split_xt_suffix_code(code, exchg, raw))
	{
	}
	else if (pos != std::string::npos)
	{
		exchg = code.substr(0, pos);
		raw = code.substr(pos + 1);
	}
	else
	{
		raw = code;
	}

	if (raw.empty())
		return code;

	exchg = upper_copy(exchg);
	if (exchg == "SH")
		exchg = "SSE";
	else if (exchg == "SZ")
		exchg = "SZSE";
	else if (exchg == "BJ")
		exchg = "BSE";

	WTSContractInfo* cInfo = mgr->getContract(raw.c_str(), exchg.c_str());
	if (!cInfo)
		return code;

	const char* fullPid = cInfo->getFullPid(); // e.g. SSE.STK
	if (fullPid == nullptr || strlen(fullPid) == 0)
		return code;

	return fmtutil::format("{}.{}", fullPid, raw);
}

bool ParserZMQ::send_control(const CodeSet& codes, const char* action)
{
	// 输入：
	// - codes：代码集合；action：控制动作字符串（"add"/"remove"/"clear"）。
	// 输出：
	// - true：收到控制通道的应答（REP）；
	// - false：ctrl_sock_ 不存在、无需发送（add 且 codes 为空）、发送/等待超时或发生异常。
	// 逻辑：
	// - 构造 JSON：{ "action": "<action>", "codes": [ ... ] }（clear 时 codes 为空数组）；
	// - 通过 REQ socket 发送，并 poll 等待 REP；
	// - 成功/失败均记录日志（best-effort，不影响行情接收线程继续运行）。
	StdUniqueLock lock(_ctrl_mtx_);
	if (!ctrl_sock_)
		return false;

	if (codes.empty() && strcmp(action, "add") == 0)
		return false;

	rapidjson::Document doc;
	doc.SetObject();
	auto& alloc = doc.GetAllocator();
	doc.AddMember("action", rapidjson::Value(action, alloc), alloc);
	if (!codes.empty())
	{
		rapidjson::Value arr(rapidjson::kArrayType);
		for (const auto& code : codes)
		{
			arr.PushBack(rapidjson::Value(code.c_str(), alloc), alloc);
		}
		doc.AddMember("codes", arr, alloc);
	}
	else
	{
		doc.AddMember("codes", rapidjson::kArrayType, alloc);
	}

	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
	doc.Accept(writer);

	zmq::message_t msg(sb.GetString(), sb.GetSize());
	try
	{
		auto sent = ctrl_sock_->send(msg, zmq::send_flags::none);
		if (!sent)
		{
			write_log(get_sink(), LL_WARN, "[ParserZMQ] control action {} 发送失败(codes={})", action, codes.size());
			return false;
		}
		zmq::pollitem_t items[] = { {*ctrl_sock_, 0, ZMQ_POLLIN, 0} };
		zmq::poll(&items[0], 1, std::chrono::milliseconds(_ctrl_timeout_ms_));
		if (items[0].revents & ZMQ_POLLIN)
		{
			zmq::message_t resp;
			auto received = ctrl_sock_->recv(resp, zmq::recv_flags::none);
			if (!received)
			{
				write_log(get_sink(), LL_WARN, "[ParserZMQ] control action {} response receive failed(codes={})", action, codes.size());
				return false;
			}
			while (ctrl_sock_->get(zmq::sockopt::rcvmore))
			{
				received = ctrl_sock_->recv(resp, zmq::recv_flags::none);
				if (!received)
				{
					write_log(get_sink(), LL_WARN, "[ParserZMQ] control action {} multipart response receive failed(codes={})", action, codes.size());
					return false;
				}
			}
			write_log(get_sink(), LL_INFO, "[ParserZMQ] control action {} ok, codes={}, resp_bytes={}", action, codes.size(), resp.size());
			return true;
		}
		write_log(get_sink(), LL_WARN, "[ParserZMQ] control action {} timeout(codes={})", action, codes.size());
	}
	catch (const zmq::error_t& e)
	{
		write_log(get_sink(), LL_ERROR, "[ParserZMQ] control send failed: {}", e.what());
	}
	return false;
}

bool ParserZMQ::send_control(const char* action)
{
	// 输入：action（控制动作字符串）。
	// 输出：send_control(empty_codes, action) 的结果。
	// 逻辑：为不需要 codes 的动作（如 "clear"）提供便捷调用。
	CodeSet empty;
	return send_control(empty, action);
}

void ParserZMQ::start_worker()
{
	// 输入：无。
	// 输出：无；副作用：创建并启动后台线程 thrd_，线程函数为 worker_loop()。
	// 逻辑：避免重复启动；线程生命周期由 stop_worker() 管理。
	if (thrd_)
		return;
	thrd_.reset(new StdThread([this]() { worker_loop(); }));
}

IParserSpi* ParserZMQ::get_sink() const
{
	StdUniqueLock lock(_sink_mtx_);
	return _sink_;
}

IParserSpi* ParserZMQ::detach_sink()
{
	StdUniqueLock lock(_sink_mtx_);
	IParserSpi* sink = _sink_;
	_sink_ = nullptr;
	return sink;
}

void ParserZMQ::stop_worker()
{
	// 输入：无。
	// 输出：无；副作用：
	// - 将 running_ 置为 false，通知 worker_loop() 退出；
	// - join 等待线程结束；
	// - 关闭并释放 SUB/REQ socket 与 ZMQ context。
	// 逻辑：集中管理线程与网络资源的生命周期，确保可重复 connect()/disconnect() 且不泄漏。
	write_simple_stats("parser_stop_worker", "", "", "", 0, now_ns());
	stopping_.store(true);
	running_.store(false);
	detach_sink();

	if (thrd_ && thrd_->joinable())
		thrd_->join();
	thrd_.reset();
	if (sub_sock_)
		sub_sock_->close();
	{
		StdUniqueLock lock(_ctrl_mtx_);
		if (ctrl_sock_)
			ctrl_sock_->close();
		ctrl_sock_.reset();
	}
	sub_sock_.reset();
	ctx_.reset();
}

void ParserZMQ::worker_loop()
{
	// 输入：无（使用成员 sub_sock_/_sink_/running_ 等）。
	// 输出：无；副作用：持续从 SUB socket 接收行情，解析后通过 _sink_ 推送给 WT。
	// 逻辑：
	// - 循环直到 running_ 为 false；
	// - 兼容 multipart 与 single-frame 消息：
	//   * multipart：第一帧作为 topic，最后一帧作为 payload；
	//   * single-frame：整帧作为 payload；
	// - 首条消息做一次日志输出（便于排查 topic/payload 格式）；
	// - 调用 parse_quote() 做 JSON 校验与字段映射。
	static std::atomic<bool> logged_first{false};
	uint64_t msg_count = 0;
	uint64_t dispatch_ok_count = 0;
	uint64_t last_progress_ns = now_ns();
	while (running_.load())
	{
		try
		{
			zmq::message_t first;
			auto ret = sub_sock_->recv(first, zmq::recv_flags::none);
			if (!ret)
				continue;
			if (!running_.load())
				break;
			const uint64_t ubuntu_rx_ts_ns = now_ns();

			bool more = sub_sock_->get(zmq::sockopt::rcvmore);
			std::string tp;
			std::string pl;
			if (more)
			{
				tp.assign(static_cast<char*>(first.data()), first.size());
				// drain all remaining frames, use last as payload
				while (more)
				{
					zmq::message_t part;
					if (!sub_sock_->recv(part, zmq::recv_flags::none))
						break;
					pl.assign(static_cast<char*>(part.data()), part.size());
					more = sub_sock_->get(zmq::sockopt::rcvmore);
				}
			}
			else
			{
				// single-frame pub: treat first as payload
				pl.assign(static_cast<char*>(first.data()), first.size());
			}

			IParserSpi* sink = get_sink();
			if (!logged_first.exchange(true) && sink)
				write_log(sink, LL_INFO, "[ParserZMQ] first msg topic='{}' payload_size={}", tp.c_str(), pl.size());

			++msg_count;
			if (parse_quote(tp, pl, ubuntu_rx_ts_ns))
				++dispatch_ok_count;
			const uint64_t now = now_ns();
			if (msg_count % 100000 == 0 || now - last_progress_ns >= 30000000000ULL)
			{
				write_log(sink,
					LL_INFO,
					"[ParserZMQ] quote progress rx_msgs={} dispatched={} last_topic='{}' last_payload_size={}",
					(unsigned long long)msg_count,
					(unsigned long long)dispatch_ok_count,
					tp.c_str(),
					pl.size());
				last_progress_ns = now;
			}
		}
		catch (const zmq::error_t& e)
		{
			if (running_.load())
				write_log(get_sink(), LL_ERROR, "[ParserZMQ] recv error: {}", e.what());
		}
	}
}

bool ParserZMQ::parse_quote(const std::string& topic, const std::string& payload, uint64_t ubuntu_rx_ts_ns)
{
	// 输入：
	// - topic：ZMQ topic（可能为空；当 publisher 采用 multipart 时通常为代码/主题）；
	// - payload：JSON 文本（UTF-8），期望包含 {"type":"quote", ...}。
	// 输出：
	// - true：通过校验且成功转换并推送 tick；
	// - false：payload 非法、type 不匹配、被本地订阅过滤或填充失败。
	// 逻辑：
	// - 使用 RapidJSON 解析；
	// - 校验 type == "quote"；
	// - 若 topic 非空，则在 _subs_ 非空时做一次 best-effort 本地过滤；
	// - 最终调用 fill_tick() 映射为 WT tick 并回调。
	rapidjson::Document doc;
	doc.Parse(payload.c_str());
	if (!doc.IsObject())
		return false;

	if (!doc.HasMember("type") || !doc["type"].IsString())
		return false;
	if (std::strcmp(doc["type"].GetString(), "quote") != 0)
		return false;

	if (!topic.empty())
	{
		StdUniqueLock lock(_mtx_);
		if (!_subs_.empty() && _subs_.find(topic) == _subs_.end())
			return false;
	}

	return fill_tick(doc, topic, ubuntu_rx_ts_ns);
}

bool ParserZMQ::try_parse_uint32(const rapidjson::Document& doc, const char* key, uint32_t& outVal)
{
	// 输入：doc（已解析的 JSON 对象）、key（字段名）、outVal（引用输出参数）。
	// 输出：true 表示成功从字段解析出 uint32 并写入 outVal；false 表示字段不存在或类型不支持。
	// 逻辑：支持字段为 JSON Uint 或字符串数字（strtoul）。
	if (!doc.HasMember(key))
		return false;
	const auto& val = doc[key];
	if (val.IsUint())
		outVal = val.GetUint();
	else if (val.IsString())
		outVal = (uint32_t)strtoul(val.GetString(), nullptr, 10);
	else
		return false;
	return true;
}

bool ParserZMQ::try_parse_uint64(const rapidjson::Document& doc, const char* key, uint64_t& outVal)
{
	// 输入：doc（已解析的 JSON 对象）、key（字段名）、outVal（引用输出参数）。
	// 输出：true 表示成功从字段解析出 uint64 并写入 outVal；false 表示字段不存在或类型不支持。
	// 逻辑：支持字段为 Uint64、字符串数字（strtoull），或正数 Int64（向 uint64 转换）。
	if (!doc.HasMember(key))
		return false;
	const auto& val = doc[key];
	if (val.IsUint64())
		outVal = val.GetUint64();
	else if (val.IsString())
		outVal = strtoull(val.GetString(), nullptr, 10);
	else if (val.IsInt64() && val.GetInt64() > 0)
		outVal = (uint64_t)val.GetInt64();
	else
		return false;
	return true;
}

double ParserZMQ::get_double_or(const rapidjson::Document& doc, const char* key, double defVal)
{
	// 输入：doc（JSON 对象）、key（字段名）、defVal（缺省值）。
	// 输出：字段存在且可解析为数字则返回其 double 值，否则返回 defVal。
	// 逻辑：支持 JSON Number 与字符串数字（atof），用于兼容桥接层可能的字符串化数值。
	if (!doc.HasMember(key))
		return defVal;
	const auto& val = doc[key];
	if (val.IsNumber())
		return val.GetDouble();
	if (val.IsString())
		return atof(val.GetString());
	return defVal;
}

bool ParserZMQ::fill_tick(const rapidjson::Document& doc, const std::string& topic, uint64_t ubuntu_rx_ts_ns)
{
	// 输入：
	// - doc：已解析的 JSON 行情对象（type=="quote"）；
	// - topic：ZMQ topic（若 publisher 发送 multipart，则通常为 stdCode）。
	// 输出：true；当前实现无显式失败路径，返回值主要用于与 parse_quote() 的流程接口保持一致。
	// 逻辑（字段映射与兼容策略）：
	// - 先从 JSON 中读取 exchg/code；再优先用 topic 补全 fullCode（支持 "SSE.STK.600000" 等全码）；
	// - 将 fullCode 拆分成 exchg + rawCode，填入 WTSTickStruct::exchg/code；
	// - 读取 price/open/high/low、volume/turnover 等数值字段；
	// - 时间字段 trading_date/action_date/action_time 支持 uint 或字符串；
	// - 五档盘口按 bid_price_1..5 / bid_qty_1..5、ask_price_1..5 / ask_qty_1..5 填充；
	// - 生成 WTSTickData 并回调给 WT，最后释放 tick 对象。
	if (stopping_.load())
		return false;
	const char* exchg_field = doc.HasMember("exchg") && doc["exchg"].IsString() ? doc["exchg"].GetString() : "";
	std::string code_field;
	if (doc.HasMember("code"))
	{
		const auto& cv = doc["code"];
		if (cv.IsString())
			code_field = cv.GetString();
		else if (cv.IsInt64())
			code_field = fmtutil::format("{}", (long long)cv.GetInt64());
		else if (cv.IsUint64())
			code_field = fmtutil::format("{}", (unsigned long long)cv.GetUint64());
	}

	// Determine full stdCode from topic or JSON, then normalize to exchg + raw code
	std::string fullCode = !topic.empty() ? topic : code_field;
	std::string exchg(exchg_field);
	std::string rawCode(code_field);
	split_std_code(fullCode, exchg, rawCode);
	WTSContractInfo* cInfo = nullptr;
	IParserSpi* sink = get_sink();
	if (sink != nullptr)
	{
		IBaseDataMgr* mgr = sink->getBaseDataMgr();
		if (mgr != nullptr)
			cInfo = mgr->getContract(rawCode.c_str(), exchg.c_str());
	}
	if (cInfo == nullptr)
	{
		static std::atomic<uint64_t> missing_contract_count{0};
		const uint64_t count = missing_contract_count.fetch_add(1) + 1;
		if (count <= 10 || count % 1000 == 0)
		{
			write_log(sink,
				LL_WARN,
				"[ParserZMQ] Instrument {}.{} not exists, quote skipped count={} topic={}",
				exchg.c_str(),
				rawCode.c_str(),
				(unsigned long long)count,
				topic.c_str());
		}
		write_quote_stats(doc, topic, exchg, rawCode, ubuntu_rx_ts_ns, now_ns());
		return false;
	}
	if (cInfo->getCommInfo() == nullptr)
	{
		static std::atomic<uint64_t> missing_comm_count{0};
		const uint64_t count = missing_comm_count.fetch_add(1) + 1;
		if (count <= 10 || count % 1000 == 0)
		{
			write_log(sink,
				LL_WARN,
				"[ParserZMQ] Instrument {}.{} has no commodity info, quote skipped count={} topic={}",
				exchg.c_str(),
				rawCode.c_str(),
				(unsigned long long)count,
				topic.c_str());
		}
		write_quote_stats(doc, topic, exchg, rawCode, ubuntu_rx_ts_ns, now_ns());
		return false;
	}

	WTSTickStruct ts;
	memset(&ts, 0, sizeof(WTSTickStruct));
	wt_strcpy(ts.exchg, exchg.c_str());
	wt_strcpy(ts.code, rawCode.c_str());

	ts.price = get_double_or(doc, "price");
	ts.open = get_double_or(doc, "open");
	ts.high = get_double_or(doc, "high");
	ts.low = get_double_or(doc, "low");

	ts.volume = get_double_or(doc, "volume");
	ts.turn_over = get_double_or(doc, "turnover");
	ts.total_volume = get_double_or(doc, "total_volume");
	ts.total_turnover = get_double_or(doc, "total_turnover");

	ts.pre_close = get_double_or(doc, "pre_close");
	ts.pre_settle = get_double_or(doc, "pre_settle");
	ts.upper_limit = get_double_or(doc, "upper_limit");
	ts.lower_limit = get_double_or(doc, "lower_limit");
	bool has_sedimentary = doc.HasMember("sedimentary") && (doc["sedimentary"].IsNumber() || doc["sedimentary"].IsString());
	if (has_sedimentary)
		ts.sedimentary = get_double_or(doc, "sedimentary");
	else
		ts.open_interest = get_double_or(doc, "open_interest");
	ts.diff_interest = get_double_or(doc, "diff_interest");

	uint32_t tdate = 0, adate = 0, atime = 0;
	try_parse_uint32(doc, "trading_date", tdate);
	try_parse_uint32(doc, "action_date", adate);
	try_parse_uint32(doc, "action_time", atime);
	ts.trading_date = tdate;
	ts.action_date = adate;
	ts.action_time = atime;

	// five levels
	for (int i = 0; i < 5; ++i)
	{
		char keyp[32], keyq[32];
		snprintf(keyp, sizeof(keyp), "bid_price_%d", i + 1);
		snprintf(keyq, sizeof(keyq), "bid_qty_%d", i + 1);
		ts.bid_prices[i] = get_double_or(doc, keyp);
		ts.bid_qty[i] = get_double_or(doc, keyq);

		snprintf(keyp, sizeof(keyp), "ask_price_%d", i + 1);
		snprintf(keyq, sizeof(keyq), "ask_qty_%d", i + 1);
		ts.ask_prices[i] = get_double_or(doc, keyp);
		ts.ask_qty[i] = get_double_or(doc, keyq);
	}

	const uint64_t ubuntu_handle_quote_ts_ns = now_ns();
	WTSTickData* tick = WTSTickData::create(ts);
	tick->setContractInfo(cInfo);
	write_simple_stats("parser_sink_dispatch", topic, exchg, rawCode, ubuntu_rx_ts_ns, ubuntu_handle_quote_ts_ns);
	if (!stopping_.load())
	{
		sink = get_sink();
		if (sink)
			sink->handleQuote(tick, 1);
	}
	write_simple_stats("parser_sink_dispatched", topic, exchg, rawCode, ubuntu_rx_ts_ns, now_ns());
	tick->release();
	write_quote_stats(doc, topic, exchg, rawCode, ubuntu_rx_ts_ns, ubuntu_handle_quote_ts_ns);

	return true;
}

uint64_t ParserZMQ::now_ns() const
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

void ParserZMQ::write_quote_stats(const rapidjson::Document& doc, const std::string& topic, const std::string& exchg,
	const std::string& rawCode, uint64_t ubuntu_rx_ts_ns, uint64_t ubuntu_handle_quote_ts_ns)
{
	if (!_stats_enable_ || _stats_path_.empty())
		return;

	uint64_t win_pub_ts_ns = 0;
	uint64_t win_callback_ts_ns = 0;
	uint64_t bridge_seq = 0;
	try_parse_uint64(doc, "win_pub_ts_ns", win_pub_ts_ns);
	try_parse_uint64(doc, "win_callback_ts_ns", win_callback_ts_ns);
	try_parse_uint64(doc, "bridge_seq", bridge_seq);
	std::string event_id;
	if (doc.HasMember("event_id") && doc["event_id"].IsString())
		event_id = doc["event_id"].GetString();

	const int64_t cross_machine_latency_ms = win_pub_ts_ns > 0 && ubuntu_rx_ts_ns >= win_pub_ts_ns
		? (int64_t)((ubuntu_rx_ts_ns - win_pub_ts_ns) / 1000000ULL)
		: -1;
	const int64_t parser_dispatch_latency_ms = ubuntu_handle_quote_ts_ns >= ubuntu_rx_ts_ns
		? (int64_t)((ubuntu_handle_quote_ts_ns - ubuntu_rx_ts_ns) / 1000000ULL)
		: -1;

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("case_id"); writer.String(_case_id_.c_str());
	writer.Key("event_type"); writer.String("quote_rx");
	writer.Key("event_id"); writer.String(event_id.c_str());
	writer.Key("topic"); writer.String(topic.c_str());
	writer.Key("symbol"); writer.String(rawCode.c_str());
	writer.Key("exchg"); writer.String(exchg.c_str());
	writer.Key("win_callback_ts_ns"); writer.Uint64(win_callback_ts_ns);
	writer.Key("win_pub_ts_ns"); writer.Uint64(win_pub_ts_ns);
	writer.Key("ubuntu_rx_ts_ns"); writer.Uint64(ubuntu_rx_ts_ns);
	writer.Key("ubuntu_handle_quote_ts_ns"); writer.Uint64(ubuntu_handle_quote_ts_ns);
	writer.Key("cross_machine_latency_ms"); writer.Int64(cross_machine_latency_ms);
	writer.Key("parser_dispatch_latency_ms"); writer.Int64(parser_dispatch_latency_ms);
	writer.Key("bridge_seq"); writer.Uint64(bridge_seq);
	writer.Key("latency_warn_ms"); writer.Int(_latency_warn_ms_);
	writer.Key("latency_error_ms"); writer.Int(_latency_error_ms_);
	writer.EndObject();

	try
	{
		StdUniqueLock lock(_stats_mtx_);
		std::ofstream out(_stats_path_, std::ios::out | std::ios::app);
		if (out)
			out << buffer.GetString() << '\n';
	}
	catch (...)
	{
	}
}

void ParserZMQ::write_simple_stats(const char* event_type, const std::string& topic, const std::string& exchg,
	const std::string& rawCode, uint64_t ubuntu_rx_ts_ns, uint64_t ubuntu_handle_quote_ts_ns)
{
	if (!_stats_enable_ || _stats_path_.empty())
		return;

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("case_id"); writer.String(_case_id_.c_str());
	writer.Key("event_type"); writer.String(event_type);
	writer.Key("topic"); writer.String(topic.c_str());
	writer.Key("symbol"); writer.String(rawCode.c_str());
	writer.Key("exchg"); writer.String(exchg.c_str());
	writer.Key("ubuntu_rx_ts_ns"); writer.Uint64(ubuntu_rx_ts_ns);
	writer.Key("ubuntu_handle_quote_ts_ns"); writer.Uint64(ubuntu_handle_quote_ts_ns);
	writer.EndObject();

	try
	{
		StdUniqueLock lock(_stats_mtx_);
		std::ofstream out(_stats_path_, std::ios::out | std::ios::app);
		if (out)
			out << buffer.GetString() << '\n';
	}
	catch (...)
	{
	}
}
