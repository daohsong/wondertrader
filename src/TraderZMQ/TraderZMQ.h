#pragma once

#include "../Includes/ITraderApi.h"
#include "../Share/StdUtils.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <rapidjson/document.h>
#include <zmq.hpp>

USING_NS_WTP;

class TraderZMQ : public ITraderApi
{
public:
	TraderZMQ();
	~TraderZMQ() override;

public:
	bool init(WTSVariant* params) override;
	void release() override;
	void registerSpi(ITraderSpi* listener) override;

	void connect() override;
	void disconnect() override;
	bool isConnected() override { return _connected.load(); }

	bool makeEntrustID(char* buffer, int length) override;
	int login(const char* user, const char* pass, const char* productInfo) override;
	int logout() override;

	int orderInsert(WTSEntrust* entrust) override;
	int orderAction(WTSEntrustAction* action) override;
	int queryAccount() override;
	int queryPositions() override;
	int queryOrders() override;
	int queryTrades() override;
	int querySettlement(uint32_t uDate) override;

public:
	struct OrderContext
	{
		std::string req_id;
		std::string order_id;
		std::string user_tag;
		std::string exchg;
		std::string code;

		WTSDirectionType direction{WDT_LONG};
		WTSOffsetType offset{WOT_OPEN};
		WTSPriceType price_type{WPT_ANYPRICE};
		WTSOrderFlag order_flag{WOF_NOR};

		double price{0.0};
		double volume{0.0};

		bool has_direction{false};
		bool has_offset{false};
		bool has_price_type{false};
		bool has_order_flag{false};
		bool has_price{false};
		bool has_volume{false};
		bool submitting_emitted{false};
		bool terminal_emitted{false};
		bool has_last_order_snapshot{false};
		WTSOrderState last_order_state{WOS_Submitting};
		double last_vol_traded{0.0};
		double last_vol_left{0.0};
	};

private:
	bool isOperational() const;

	void startWorker();
	void stopWorker();
	void workerLoop();
	void startCallbackDispatcher();
	void stopCallbackDispatcher();
	void callbackLoop();
	void enqueueCallback(std::function<void()> job);
	void dispatchCallbackSync(std::function<void()> job);
	bool isCallbackThread() const;
	void beginOrderApi();
	void endOrderApi();

	bool sendRequest(const rapidjson::Document& req, rapidjson::Document& ack, const char* expectedType = nullptr);
	bool probeLogin(std::string& errMsg);

	OrderContext makeContext(const WTSEntrust* entrust) const;
	OrderContext resolveContext(const rapidjson::Value& obj) const;
	void storeContext(const OrderContext& ctx);
	void markSubmittingEmitted(const OrderContext& ctx);
	bool hasSubmittingEmitted(const OrderContext& ctx) const;
	void markTerminalEmitted(const OrderContext& ctx);
	bool hasTerminalEmitted(const OrderContext& ctx) const;
	void markOrderSnapshot(const OrderContext& ctx, WTSOrderState state, double volTraded, double volLeft);
	bool isDuplicateOrderUpdate(const OrderContext& ctx, WTSOrderState state, double volTraded, double volLeft) const;
	void enqueueSyntheticSubmitting(const OrderContext& ctx);
	void drainPendingSubmitting();
	void dispatchSyntheticSubmitting(const OrderContext& ctx);

	void emitSubmittingIfNeeded(const OrderContext& ctx, WTSOrderState actualState);
	void emitSubmittingOrder(const OrderContext& ctx);
	void emitSyntheticErrorOrder(const OrderContext& ctx, const char* errMsg);

	void handleOrderUpdate(const rapidjson::Document& doc);
	void handleTradeUpdate(const rapidjson::Document& doc);
	void handleOrderError(const rapidjson::Document& doc);
	uint64_t nowNs() const;
	void writeStatsEvent(const char* eventType, const rapidjson::Value* payload, const char* reqType,
		uint64_t ubuntuSendTsNs, uint64_t ubuntuRecvTsNs);

private:
	std::string _host;
	int32_t _req_port{5557};
	int32_t _pub_port{5558};
	std::string _account_id;
	int32_t _ack_timeout_ms{5000};
	int32_t _sub_timeout_ms{1000};
	bool _probe_login{false};
	bool _stats_enable{false};
	std::string _stats_path;
	std::string _case_id{"WTZMQ2_LIVE"};
	int32_t _order_ack_warn_ms{5000};
	int32_t _trade_update_warn_ms{10000};

	ITraderSpi* _listener{nullptr};

	std::unique_ptr<zmq::context_t> _ctx;
	std::unique_ptr<zmq::socket_t> _req;
	std::unique_ptr<zmq::socket_t> _sub;

	std::atomic<bool> _running{false};
	std::atomic<bool> _connected{false};
	std::atomic<bool> _logged_in{false};
	std::atomic<bool> _stopping{false};
	std::atomic<uint64_t> _id_seed{0};

	StdThreadPtr _worker;
	StdThreadPtr _callback_worker;
	mutable StdUniqueMutex _mtx_sock;
	mutable StdUniqueMutex _mtx_maps;
	mutable StdUniqueMutex _mtx_pending;
	mutable StdUniqueMutex _mtx_stats;
	mutable std::recursive_mutex _mtx_callback;
	mutable std::mutex _mtx_callback_queue;
	std::condition_variable _cv_callback;
	std::deque<std::function<void()>> _callback_queue;
	std::atomic<bool> _callback_running{false};
	std::atomic<uint32_t> _order_api_active{0};
	std::thread::id _callback_thread_id;

	std::unordered_map<std::string, OrderContext> _req_ctx;
	std::unordered_map<std::string, OrderContext> _order_ctx;
	std::deque<OrderContext> _pending_submitting;
};
