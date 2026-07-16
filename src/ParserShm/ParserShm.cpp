/*!
 * \file ParserShm.cpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief 
 */
#include "ParserShm.h"
#include "../Includes/WTSVariant.hpp"
#include "../Includes/WTSDataDef.hpp"
#include "../Includes/IBaseDataMgr.h"
#include "../Includes/WTSContractInfo.hpp"
#include "../Share/AtomicCompat.hpp"
#include "../Share/CpuHelper.hpp"
#include "../Share/StrUtil.hpp"

#include <boost/bind/bind.hpp>

 //By Wesley @ 2022.01.05
#include "../Share/fmtlib.h"
#include <cstdint>
#include <exception>

template<typename... Args>
inline void write_log(IParserSpi* sink, WTSLogLevel ll, const char* format, const Args&... args) noexcept
{
	if (sink == NULL)
		return;

	static thread_local char buffer[512] = { 0 };
	fmtutil::format_to(buffer, format, args...);

	sink->handleParserLog(ll, buffer);
}

#define UDP_MSG_SUBSCRIBE	0x100
#define UDP_MSG_PUSHTICK	0x200
#define UDP_MSG_PUSHORDQUE	0x201	//委托队列
#define UDP_MSG_PUSHORDDTL	0x202	//委托明细
#define UDP_MSG_PUSHTRANS	0x203	//逐笔成交

#define NODATA_FLAG 0xfffffffffffffffe

namespace
{
uint64_t load_readable_index(ParserShm::CastQueue* queue)
{
	return wt::atomic_load_u64(queue->_readable, std::memory_order_acquire);
}
}


extern "C"
{
	EXPORT_FLAG IParserApi* createParser()
	{
		ParserShm* parser = new ParserShm();
		return parser;
	}

	EXPORT_FLAG void deleteParser(IParserApi* &parser)
	{
		if (NULL != parser)
		{
			delete parser;
			parser = NULL;
		}
	}
};



ParserShm::ParserShm()
	: _stopped(false)
	, _sink(NULL)
	, _queue(NULL)
	, _check_span(0)
{
}


ParserShm::~ParserShm()
{
}

bool ParserShm::init( WTSVariant* config )
{
	_path = config->getCString("path");
	_gpsize = config->getUInt32("gpsize", 1000);
	_check_span = config->getUInt32("check_span", 0);
	_cpu = config->getUInt32("cpu", 0);
	return true;
}

void ParserShm::release()
{
	disconnect();
	if (_thrd_parser)
		_thrd_parser->join();
}

bool ParserShm::connect()
{
	_thrd_parser.reset(new StdThread([this]() {

		write_log(_sink, LL_INFO, "[ParserShm] loading {} ...", _path);
		while (!StdFile::exists(_path.c_str()))
		{
			if (_stopped)
				return;

			write_log(_sink, LL_WARN, "[ParserShm] {} not exist yet, waiting for 2 seconds", _path);
			std::this_thread::sleep_for(std::chrono::seconds(2));
			continue;
		}

		void* queue_base = NULL;
		wt::shm_wire::ValidationError validation = wt::shm_wire::ValidationError::unexpected_size;
		const auto initialize_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!_stopped && std::chrono::steady_clock::now() < initialize_deadline)
		{
			_mapfile.reset(new BoostMappingFile);
			try
			{
				if (_mapfile->map(_path.c_str()))
				{
					queue_base = _mapfile->addr();
					validation = wt::shm_wire::validate_cast_queue<8 * 1024>(
						queue_base, _mapfile->size());
					if (validation == wt::shm_wire::ValidationError::ok)
						break;
					if (validation != wt::shm_wire::ValidationError::bad_magic
						&& validation != wt::shm_wire::ValidationError::unexpected_size)
						break;
				}
			}
			catch (const std::exception&)
			{
				validation = wt::shm_wire::ValidationError::unexpected_size;
			}

			_mapfile.reset();
			queue_base = NULL;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		if (validation != wt::shm_wire::ValidationError::ok)
		{
			write_log(_sink, LL_ERROR, "[ParserShm] rejected {}: {}", _path,
				wt::shm_wire::validation_error_message(validation));
			_queue = NULL;
			_mapfile.reset();
			return;
		}
		_queue = static_cast<CastQueue*>(queue_base);
		uint32_t cast_pid = wt::atomic_load_u32(_queue->_pid, std::memory_order_acquire);
		const auto pid_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!_stopped && cast_pid == 0 && std::chrono::steady_clock::now() < pid_deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			cast_pid = wt::atomic_load_u32(_queue->_pid, std::memory_order_acquire);
		}
		if (cast_pid == 0)
		{
			write_log(_sink, LL_ERROR, "[ParserShm] {} remained in initializing state", _path);
			_queue = NULL;
			_mapfile.reset();
			return;
		}

		if (_sink)
		{
			_sink->handleEvent(WPE_Connect, 0);
			_sink->handleEvent(WPE_Login, 0);
		}
		write_log(_sink, LL_INFO, "[ParserShm] {} loaded, start to receiving", _path);

		/*
		 *	By Wesley @ 2023.12.28
		 *	新增一个绑核的逻辑
		 */
		if (_cpu > 0)
			CpuHelper::bind_core(_cpu - 1);

		uint64_t lastIdx = UINT64_MAX;
		while(!_stopped)
		{
			//如果pid不同，说明datakit重启了
			const uint32_t next_pid = wt::atomic_load_u32(_queue->_pid, std::memory_order_acquire);
			if (next_pid == 0)
			{
				std::this_thread::yield();
				continue;
			}
			if(cast_pid != next_pid)
			{
				lastIdx = UINT64_MAX;
				write_log(_sink, LL_WARN, "ShareMemory queue has been reset justnow");
				cast_pid = next_pid;
			}

			const uint64_t readable = load_readable_index(_queue);
			
			if (readable == UINT64_MAX)	//刚分配好，还没数据进来
			{
				lastIdx = NODATA_FLAG;
				if(_check_span != 0)
					std::this_thread::sleep_for(std::chrono::microseconds(_check_span));
				continue;
			}

			if (lastIdx == UINT64_MAX)	//有数据，第一次检查，则直接定位到最后一条数据
			{
				lastIdx = readable;
				if (_check_span != 0)
					std::this_thread::sleep_for(std::chrono::microseconds(_check_span));
				continue;
			}
			else if (lastIdx == NODATA_FLAG)	//之前没数据的时候检查了一次，现在有数据了，从0开始读取
			{
				lastIdx = 0;
			}
			else if (lastIdx >= readable)	//没有新的数据进来
			{
				if (_check_span != 0)
					std::this_thread::sleep_for(std::chrono::microseconds(_check_span));
				continue;
			}
			else
			{
				lastIdx++;	//普通情况，下标递增
			}

			DataItem& item = _queue->_items[lastIdx % _queue->_capacity];
			switch (item._type)
			{
			case 0:
			{
				WTSContractInfo* cInfo = _bd_mgr->getContract(item._tick.code, item._tick.exchg);
				if(cInfo == NULL)
					break;

				const char* fullCode = cInfo->getFullCode();
				bool isSubbed = !_subbed.empty() && _subbed[cInfo->getTotalIndex()];
				if (isSubbed)
				{
					WTSTickData* newData = WTSTickData::create(item._tick);
					newData->setContractInfo(cInfo);
					if (_sink)
						_sink->handleQuote(newData, 0);
					newData->release();

					static uint32_t recv_cnt = 0;
					recv_cnt++;
					if (recv_cnt % _gpsize == 0)
						write_log(_sink, LL_DEBUG, "[ParserShm] {} ticks received in total", recv_cnt);
				}
			}
			break;
			case 1:
			{
				WTSContractInfo* cInfo = _bd_mgr->getContract(item._queue.code, item._queue.exchg);
				if (cInfo == NULL)
					break;

				const char* fullCode = cInfo->getFullCode();
				bool isSubbed = !_subbed.empty() && _subbed[cInfo->getTotalIndex()];
				if (isSubbed)
				{
					WTSOrdQueData* newData = WTSOrdQueData::create(item._queue);
					newData->setContractInfo(cInfo);
					if (_sink)
						_sink->handleOrderQueue(newData);
					newData->release();

					static uint32_t recv_cnt = 0;
					recv_cnt++;
					if (recv_cnt % _gpsize == 0)
						write_log(_sink, LL_DEBUG, "[ParserShm] {} queues received in total", recv_cnt);
				}
			}
			break;
			case 2:
			{
				WTSContractInfo* cInfo = _bd_mgr->getContract(item._order.code, item._order.exchg);
				if(cInfo == NULL)
					break;

				const char* fullCode = cInfo->getFullCode();
				bool isSubbed = !_subbed.empty() && _subbed[cInfo->getTotalIndex()];
				if (isSubbed)
				{
					WTSOrdDtlData* newData = WTSOrdDtlData::create(item._order);
					newData->setContractInfo(cInfo);
					if (_sink)
						_sink->handleOrderDetail(newData);
					newData->release();

					static uint32_t recv_cnt = 0;
					recv_cnt++;
					if (recv_cnt % _gpsize == 0)
						write_log(_sink, LL_DEBUG, "[ParserShm] {} orders received in total", recv_cnt);
				}
			}
			break;
			case 3:
			{
				WTSContractInfo* cInfo = _bd_mgr->getContract(item._trans.code, item._trans.exchg);
				if (cInfo == NULL)
					break;

				const char* fullCode = cInfo->getFullCode();
				bool isSubbed = !_subbed.empty() && _subbed[cInfo->getTotalIndex()];
				if (isSubbed)
				{
					WTSTransData* newData = WTSTransData::create(item._trans);
					newData->setContractInfo(cInfo);
					if (_sink)
						_sink->handleTransaction(newData);
					newData->release();

					static uint32_t recv_cnt = 0;
					recv_cnt++;
					if (recv_cnt % _gpsize == 0)
						write_log(_sink, LL_DEBUG, "[ParserShm] {} transactions received in total", recv_cnt);
				}
			}
			break;
			default:
				break;
			}
		}
	}));

	return true;
}

bool ParserShm::disconnect()
{
	_stopped = true;

	return true;
}

bool ParserShm::isConnected()
{
	return _queue!=NULL;
}


void ParserShm::subscribe( const CodeSet &vecSymbols )
{
	if (_subbed.empty())
		_subbed.resize(_bd_mgr->getGlobalSize(), false);
	
	for(auto cit = vecSymbols.begin(); cit != vecSymbols.end(); cit++)
	{
		const auto &code = *cit;
		auto ay = StrUtil::split(code, ".");
		WTSContractInfo* cInfo = _bd_mgr->getContract(ay[1].c_str(), ay[0].c_str());
		if(cInfo == NULL)
			continue;

		_subbed[cInfo->getTotalIndex()] = true;
	}
}

void ParserShm::unsubscribe(const CodeSet &setSymbols)
{
	for (auto cit = setSymbols.begin(); cit != setSymbols.end(); cit++)
	{
		const auto &code = *cit;
		auto ay = StrUtil::split(code, ".");
		WTSContractInfo* cInfo = _bd_mgr->getContract(ay[1].c_str(), ay[0].c_str());
		if (cInfo == NULL)
			continue;

		_subbed[cInfo->getTotalIndex()] = false;
	}
}

void ParserShm::registerSpi( IParserSpi* listener )
{
	bool bReplaced = (_sink!=NULL);
	_sink = listener;
	if(bReplaced && _sink)
	{
		write_log(_sink, LL_WARN, "Listener is replaced");
	}

	if (_sink)
	{
		_bd_mgr = _sink->getBaseDataMgr();
		_subbed.resize(_bd_mgr->getGlobalSize(), false);
	}
}
