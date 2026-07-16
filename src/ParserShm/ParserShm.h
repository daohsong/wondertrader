/*!
 * \file ParserShm.h
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief 
 */
#pragma once
#include "../Includes/IParserApi.h"
#include "../Share/StdUtils.hpp"
#include "../Includes/WTSStruct.h"
#include "../Share/BoostMappingFile.hpp"
#include "../Share/ShmWireLayout.hpp"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

USING_NS_WTP;

class ParserShm : public IParserApi
{
public:
	ParserShm();
	~ParserShm();

	using DataItem = wt::shm_wire::CastDataItem;
	template <int N = 8 * 1024>
	using _DataQueue = wt::shm_wire::CastQueue<N>;
	using CastQueue = wt::shm_wire::DefaultCastQueue;

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
	std::string		_path;
	typedef std::shared_ptr<BoostMappingFile> MappedFilePtr;
	MappedFilePtr	_mapfile;
	CastQueue*		_queue;
	uint32_t		_gpsize;
	uint32_t		_check_span;
	uint32_t		_cpu;

	IParserSpi*		_sink;
	bool			_stopped;

	std::vector<bool> _subbed;

	IBaseDataMgr*	_bd_mgr;

	StdThreadPtr	_thrd_parser;
};
