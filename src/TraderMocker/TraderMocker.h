#pragma once
#include <atomic>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/address.hpp>

#include "../Includes/FasterDefs.h"
#include "../Includes/ITraderApi.h"
#include "../Share/StdUtils.hpp"
#include "../Includes/WTSCollection.hpp"


NS_WTP_BEGIN
	class WTSTickData;
NS_WTP_END

USING_NS_WTP;

/*
 *	仿真交易器
 */
class TraderMocker : public ITraderApi
{
public:
	TraderMocker();
	~TraderMocker();

private:
	/*
	*	撮合 定时器
	*/
	int32_t		match_once();

	uint32_t	makeTradeID();
	uint32_t	makeOrderID();

	void		load_positions();
	void		save_positions();
	bool		has_await_order(const char* fullcode) const;
	void		refresh_await_code(const char* fullcode);


private:
	StdThreadPtr	_thrd_match;
	bool			_terminated;

	StdUniqueMutex		_mutex_api;

	std::atomic<uint32_t>	_auto_trade_id;
	std::atomic<uint32_t>	_auto_order_id;
	std::atomic<uint32_t>	_auto_entrust_id;

	ITraderSpi* _listener;
	IBaseDataMgr*		_bd_mgr;

	StdThreadPtr		_thrd_worker;

	uint32_t		_millisecs;
	uint32_t		_mocker_id;
	bool			_use_newpx;
	double			_init_balance;
	double			_max_qty;
	double			_min_qty;

	WTSArray*		_orders;
	WTSArray*		_trades;
	typedef WTSHashMap<std::string> TickCache;
	TickCache*		_ticks;

	typedef WTSHashMap<std::string> OrderCache;
	OrderCache*			_awaits;
	StdUniqueMutex	_mtx_awaits;

	wt_hashset<std::string>	_codes;

	uint64_t		_max_tick_time;
	uint64_t		_last_match_time;

	typedef struct _PosUnit
	{
		double	_pre_volume;
		double	_pre_frozen;
		double	_new_volume;
		double	_new_frozen;

		double total_volume() const { return _pre_volume + _new_volume; }
		double total_frozen() const { return _pre_frozen + _new_frozen; }
		double pre_avail() const { return _pre_volume > _pre_frozen ? _pre_volume - _pre_frozen : 0; }
		double new_avail() const { return _new_volume > _new_frozen ? _new_volume - _new_frozen : 0; }
	} PosUnit;

	typedef struct _PosItem
	{
		char	_exchg[MAX_INSTRUMENT_LENGTH];
		char	_code[MAX_INSTRUMENT_LENGTH];

		PosUnit		_long;
		PosUnit		_short;

		_PosItem()
		{
			memset(this, 0, sizeof(_PosItem));
		}
	} PosItem;

	wt_hashmap<std::string, PosItem> _positions;
	std::string		_pos_file;

	typedef struct _FrozenItem
	{
		char	_fullcode[64];
		WTSDirectionType	_direction;
		double	_pre;
		double	_new;

		_FrozenItem()
			: _direction(WDT_LONG)
			, _pre(0)
			, _new(0)
		{
			memset(_fullcode, 0, sizeof(_fullcode));
		}

		double total() const { return _pre + _new; }
	} FrozenItem;
	std::unordered_map<std::string, FrozenItem> _frozen_orders;

private:
	int			_udp_port;

	boost::asio::ip::udp::endpoint	_broad_ep;
	boost::asio::io_context			_io_service;

	boost::asio::ip::udp::socket*	_b_socket;

	boost::array<char, 1024> _b_buffer;

	void	handle_read(const boost::system::error_code& e, std::size_t bytes_transferred, bool isBroad);

	void	extract_buffer(uint32_t length, bool isBroad);

	void	reconn_udp();

//////////////////////////////////////////////////////////////////////////
//ITraderApi
public:
	virtual bool init(WTSVariant *params) override;

	virtual void release() override;

	virtual void registerSpi(ITraderSpi *listener) override;

	virtual void connect() override;

	virtual void disconnect() override;

	virtual bool isConnected() override;

	virtual bool makeEntrustID(char* buffer, int length) override;

	virtual int login(const char* user, const char* pass, const char* productInfo) override;

	virtual int logout() override;

	virtual int orderInsert(WTSEntrust* eutrust) override;

	virtual int orderAction(WTSEntrustAction* action) override;

	virtual int queryAccount() override;

	virtual int queryPositions() override;

	virtual int queryOrders() override;

	virtual int queryTrades() override;
};
