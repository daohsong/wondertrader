/*!
 * \file ParserZMQ.h
 * \project	WonderTrader
 *
 * \author Codex
 * \date 2024/12/XX
 *
 * \brief ZMQ-based quote parser for MiniQMT/xtquant bridge
 */
#pragma once

#include "../Includes/IParserApi.h"
#include "../Share/StdUtils.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <rapidjson/document.h>
#include <zmq.hpp>

USING_NS_WTP;

class ParserZMQ : public IParserApi
{
public:
	ParserZMQ();
	~ParserZMQ() override;

public:	// IParserApi
	bool			init(WTSVariant* config) override;
	void			release() override;

	bool			connect() override;
	bool			disconnect() override;
	bool			isConnected() override;

	void			subscribe(const CodeSet& vecSymbols) override;
	void			unsubscribe(const CodeSet& vecSymbols) override;

	void			registerSpi(IParserSpi* listener) override;

private:
	std::string			normalize_code(const std::string& code) const;
	bool			send_control(const CodeSet& codes, const char* action);
	bool			send_control(const char* action);
	void			start_worker();
	void			stop_worker();
	void			worker_loop();
	IParserSpi*				get_sink() const;
	IParserSpi*				detach_sink();

	bool			parse_quote(const std::string& topic, const std::string& payload, uint64_t ubuntu_rx_ts_ns);
	bool			fill_tick(const rapidjson::Document& doc, const std::string& topic, uint64_t ubuntu_rx_ts_ns);

	bool			try_parse_uint32(const rapidjson::Document& doc, const char* key, uint32_t& outVal);
	bool			try_parse_uint64(const rapidjson::Document& doc, const char* key, uint64_t& outVal);
	double			get_double_or(const rapidjson::Document& doc, const char* key, double defVal = 0.0);
	uint64_t		now_ns() const;
	void			write_quote_stats(const rapidjson::Document& doc, const std::string& topic, const std::string& exchg,
						const std::string& rawCode, uint64_t ubuntu_rx_ts_ns, uint64_t ubuntu_handle_quote_ts_ns);
	void			write_simple_stats(const char* event_type, const std::string& topic, const std::string& exchg,
						const std::string& rawCode, uint64_t ubuntu_rx_ts_ns, uint64_t ubuntu_handle_quote_ts_ns);

private:
	std::string				_quote_host_;
	int32_t					_quote_port_{5556};
	std::string				_ctrl_host_;
	int32_t					_ctrl_port_{5555};
	int32_t					_ctrl_timeout_ms_{5000};
	bool					_stats_enable_{false};
	std::string				_stats_path_;
	std::string				_case_id_{"WTZMQ2_LIVE"};
	int32_t					_latency_warn_ms_{300};
	int32_t					_latency_error_ms_{1000};

	IParserSpi*				_sink_{nullptr};
	CodeSet					_subs_;
	StdUniqueMutex			_mtx_;
	StdUniqueMutex			_ctrl_mtx_;
	StdUniqueMutex			_stats_mtx_;
	mutable StdUniqueMutex	_sink_mtx_;

	std::unique_ptr<zmq::context_t>		ctx_;
	std::unique_ptr<zmq::socket_t>		sub_sock_;
	std::unique_ptr<zmq::socket_t>		ctrl_sock_;
	std::atomic<bool>			running_{false};
	std::atomic<bool>			stopping_{false};
	StdThreadPtr			thrd_;
};
