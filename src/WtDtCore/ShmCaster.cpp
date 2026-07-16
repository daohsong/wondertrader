#include "ShmCaster.h"
#include "../Includes/WTSVariant.hpp"
#include "../Includes/WTSDataDef.hpp"
#include "../Share/AtomicCompat.hpp"
#include "../Share/StdUtils.hpp"
#include "../Share/BoostFile.hpp"
#include "../WTSTools/WTSLogger.h"

#include <cstdint>
#include <exception>
#include <new>

namespace
{
uint64_t next_writable_index(ShmCaster::CastQueue* queue)
{
	return wt::atomic_fetch_add_u64(queue->_writable, 1, std::memory_order_relaxed);
}

void publish_readable_index(ShmCaster::CastQueue* queue, uint64_t wIdx)
{
	wt::atomic_store_u64(queue->_readable, wIdx, std::memory_order_release);
}
}

bool ShmCaster::init(WTSVariant* cfg)
{
	if (cfg == NULL)
		return false;

	if (!cfg->getBoolean("active"))
		return false;

	_path = cfg->getCString("path");

	const bool file_exists = StdFile::exists(_path.c_str());
	bool needs_initialize = !file_exists;
	{
		BoostFile bf;
		if (!file_exists)
		{
			if (!bf.create_new_file(_path.c_str()) || !bf.truncate_file(sizeof(CastQueue)))
			{
				WTSLogger::error("ShmCaster failed to create {}", _path.c_str());
				bf.close_file();
				return false;
			}
		}
		else
		{
			const uint64_t file_size = BoostFile::get_file_size(_path.c_str());
			if (file_size == 0)
			{
				if (!bf.open_existing_file(_path.c_str()) || !bf.truncate_file(sizeof(CastQueue)))
				{
					WTSLogger::error("ShmCaster failed to resize empty file {}", _path.c_str());
					bf.close_file();
					return false;
				}
				needs_initialize = true;
			}
			else if (file_size != sizeof(CastQueue))
			{
				WTSLogger::error("ShmCaster rejected {} with size {}, expected {}",
					_path.c_str(), file_size, sizeof(CastQueue));
				return false;
			}
		}

		bf.close_file();
	}

	_mapfile.reset(new BoostMappingFile);
	try
	{
		if (!_mapfile->map(_path.c_str()))
		{
			WTSLogger::error("ShmCaster failed to map {}", _path.c_str());
			_mapfile.reset();
			return false;
		}
	}
	catch (const std::exception& error)
	{
		WTSLogger::error("ShmCaster failed to map {}: {}", _path.c_str(), error.what());
		_mapfile.reset();
		return false;
	}

	void* queue_base = _mapfile->addr();
	if (!wt::atomic_is_aligned(queue_base, alignof(CastQueue)))
	{
		WTSLogger::error("ShmCaster mapping base is not {}-byte aligned", alignof(CastQueue));
		_queue = NULL;
		_mapfile.reset();
		return false;
	}

	if (needs_initialize)
		new(queue_base) CastQueue();

	const wt::shm_wire::ValidationError validation =
		wt::shm_wire::validate_cast_queue<8 * 1024>(queue_base, _mapfile->size());
	if (validation != wt::shm_wire::ValidationError::ok)
	{
		WTSLogger::error("ShmCaster rejected {}: {}", _path.c_str(),
			wt::shm_wire::validation_error_message(validation));
		_queue = NULL;
		_mapfile.reset();
		return false;
	}

	_queue = static_cast<CastQueue*>(queue_base);
	if (!needs_initialize)
	{
		// Publish the initializing state before resetting indices. Consumers
		// retain their mapping across producer restarts and must not observe a
		// partially reset queue as belonging to the previous producer.
		wt::atomic_store_u32(_queue->_pid, 0, std::memory_order_release);
		wt::atomic_store_u64(_queue->_writable, 0, std::memory_order_relaxed);
		wt::atomic_store_u64(_queue->_readable, UINT64_MAX, std::memory_order_release);
	}

#ifdef _MSC_VER
	const uint32_t process_id = static_cast<uint32_t>(_getpid());
#else
	const uint32_t process_id = static_cast<uint32_t>(getpid());
#endif
	wt::atomic_store_u32(_queue->_pid, process_id, std::memory_order_release);

	_inited = true;
	WTSLogger::info("ShmCaste initialized @ {}", _path.c_str());

	return true;
}

void ShmCaster::broadcast(WTSTickData* curTick)
{
	if (curTick == NULL || _queue == NULL || !_inited)
		return;

	/*
	 *	先移动写的下标，然后写入数据
	 *	写完了以后，再移动读的下标
	 */
	uint64_t wIdx = next_writable_index(_queue);
	uint64_t realIdx = wIdx % _queue->_capacity;
	_queue->_items[realIdx]._type = 0;
	memcpy(&_queue->_items[realIdx]._tick, &curTick->getTickStruct(), sizeof(WTSTickStruct));
	publish_readable_index(_queue, wIdx);
}

void ShmCaster::broadcast(WTSOrdQueData* curOrdQue)
{
	if (curOrdQue == NULL || _queue == NULL || !_inited)
		return;

	/*
	 *	先移动写的下标，然后写入数据
	 *	写完了以后，再移动读的下标
	 */
	uint64_t wIdx = next_writable_index(_queue);
	uint64_t realIdx = wIdx % _queue->_capacity;
	_queue->_items[realIdx]._type = 1;
	memcpy(&_queue->_items[realIdx]._queue, &curOrdQue->getOrdQueStruct(), sizeof(WTSOrdQueStruct));
	publish_readable_index(_queue, wIdx);
}

void ShmCaster::broadcast(WTSOrdDtlData* curOrdDtl)
{
	if (curOrdDtl == NULL || _queue == NULL || !_inited)
		return;

	/*
	 *	先移动写的下标，然后写入数据
	 *	写完了以后，再移动读的下标
	 */
	uint64_t wIdx = next_writable_index(_queue);
	uint64_t realIdx = wIdx % _queue->_capacity;
	_queue->_items[realIdx]._type = 2;
	memcpy(&_queue->_items[realIdx]._order, &curOrdDtl->getOrdDtlStruct(), sizeof(WTSOrdDtlStruct));
	publish_readable_index(_queue, wIdx);
}

void ShmCaster::broadcast(WTSTransData* curTrans)
{
	if (curTrans == NULL || _queue == NULL || !_inited)
		return;

	/*
	 *	先移动写的下标，然后写入数据
	 *	写完了以后，再移动读的下标
	 */
	uint64_t wIdx = next_writable_index(_queue);
	uint64_t realIdx = wIdx % _queue->_capacity;
	_queue->_items[realIdx]._type = 3;
	memcpy(&_queue->_items[realIdx]._trans, &curTrans->getTransStruct(), sizeof(WTSTransStruct));
	publish_readable_index(_queue, wIdx);
}
