/*!
 * \file ParserUDP.h
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief 
 */
#pragma once
#include "../Includes/IParserApi.h"
#include "../Share/AsioCompat.hpp"
#include "../Share/StdUtils.hpp"

#include <queue>

#include <boost/array.hpp>

USING_NS_WTP;

class ParserUDP : public IParserApi
{
public:
	ParserUDP();
	~ParserUDP();

	//IQuoteParser ½Ó¿Ú
public:
	virtual bool init(WTSVariant* config) override;

	virtual void release() override;

	virtual bool connect() override;

	virtual bool disconnect() override;

	virtual bool isConnected() override;

	virtual void subscribe(const CodeSet &vecSymbols) override;
	virtual void unsubscribe(const CodeSet &vecSymbols) override;

	virtual void registerSpi(IParserSpi* listener) override;


private:
	void	handle_read(const wt_asio::error_code& e, std::size_t bytes_transferred, bool isBroad);
	void	handle_write(const wt_asio::error_code& e);

	bool	reconnect(uint32_t flag = 3);

	void	subscribe();

	void	extract_buffer(uint32_t length, bool isBroad);

private:
	void	doOnConnected();
	void	doOnDisconnected();

	void	do_send();

private:
	std::string	_hots;
	int			_bport;
	int			_sport;
	uint32_t	_gpsize;

	wt_asio::udp_endpoint	_broad_ep;
	wt_asio::udp_endpoint	_server_ep;
	wt_asio::io_context		_io_service;

	wt_asio::io_strand		_strand;

	wt_asio::udp_socket*	_b_socket;
	wt_asio::udp_socket*	_s_socket;
	bool				_s_inited;

	boost::array<char, 1024> _b_buffer;
	boost::array<char, 1024> _s_buffer;

	IParserSpi*				_sink;
	bool					_stopped;
	bool					_connecting;

	CodeSet					_set_subs;

	StdThreadPtr			_thrd_parser;

	StdUniqueMutex			_mtx_queue;
	std::queue<std::string>	_send_queue;
};
