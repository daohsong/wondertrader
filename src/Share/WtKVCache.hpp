#pragma once
#include <cstring>
#include <functional>

#include "SpinMutex.hpp"
#include "BoostFile.hpp"
#include "StdUtils.hpp"
#include "BoostMappingFile.hpp"
#include "../Includes/FasterDefs.h"

#define SIZE_STEP 200
#define CACHE_FLAG "&^%$#@!\0"
#define FLAG_SIZE 8

typedef std::shared_ptr<BoostMappingFile> BoostMFPtr;

#pragma warning(disable:4200)

NS_WTP_BEGIN

typedef std::function<void(const char*)> CacheLogger;

class WtKVCache
{
public:
	WtKVCache() {}
	WtKVCache(const WtKVCache&) = delete;
	WtKVCache& operator=(const WtKVCache&) = delete;

private:
	

	typedef struct _CacheItem
	{
		char	_key[64] = { 0 };
		char	_val[64] = { 0 };
	} CacheItem;

	typedef struct CacheBlock
	{
		char		_blk_flag[FLAG_SIZE];
		uint32_t	_size;
		uint32_t	_capacity;
		uint32_t	_date;
		CacheItem	_items[0];
	} CacheBlock;

	typedef struct _CacheBlockPair
	{
		CacheBlock*		_block;
		BoostMFPtr		_file;

		_CacheBlockPair()
		{
			_block = NULL;
			_file = NULL;
		}
	} CacheBlockPair;

	CacheBlockPair	_cache;
	mutable SpinMutex	_lock;
	wt_hashmap<std::string, uint32_t> _indice;
	CacheLogger		_logger = nullptr;

private:
	bool	resize(uint32_t newCap) noexcept
	{
		if (_cache._file == NULL || _cache._block == NULL)
			return false;

		//调用该函数之前,应该保证线程安全了
		CacheBlock* cBlock = _cache._block;
		if (cBlock->_capacity >= newCap)
			return _cache._file->addr() != NULL;

		std::string filename = _cache._file->filename();
		uint64_t uNewSize = sizeof(CacheBlock) + sizeof(CacheItem)*newCap;
		try
		{
			BoostFile f;
			if (!f.open_existing_file(filename.c_str()))
			{
				if (_logger) _logger("Opening cache file failed while resizing");
				return false;
			}
			if (!f.truncate_file((std::size_t)uNewSize))
			{
				if (_logger) _logger("Truncating cache file failed while resizing");
				return false;
			}
			f.close_file();

			BoostMFPtr newFile(new BoostMappingFile());
			if (!newFile->map(filename.c_str()))
			{
				if (_logger) _logger("Mapping cache file failed");
				return false;
			}

			_cache._file = newFile;
			_cache._block = (CacheBlock*)_cache._file->addr();
			_cache._block->_capacity = newCap;
		}
		catch (std::exception&)
		{
			if (_logger) _logger("Got an exception while resizing cache file");
			return false;
		}

		return true;
	}

public:
	bool	init(const char* filename, uint32_t uDate, CacheLogger logger = nullptr) noexcept
	{
		SpinLock guard(_lock);

		_logger = logger;
		_indice.clear();
		_cache._block = NULL;
		bool isNew = false;
		if (!StdFile::exists(filename))
		{
			uint64_t uSize = sizeof(CacheBlock) + sizeof(CacheItem) * SIZE_STEP;
			BoostFile bf;
			bf.create_new_file(filename);
			bf.truncate_file((uint32_t)uSize);
			bf.close_file();

			isNew = true;
		}

		_cache._file.reset(new BoostMappingFile);
		if (!_cache._file->map(filename))
		{
			_cache._file.reset();
			if (_logger) _logger("Mapping cache file failed");
			return false;
		}
		_cache._block = (CacheBlock*)_cache._file->addr();

		if (!isNew &&  _cache._block->_date != uDate)
		{
			 _cache._block->_size = 0;
			 _cache._block->_date = uDate;

			memset(& _cache._block->_items, 0, sizeof(CacheItem)* _cache._block->_capacity);

			if (_logger) _logger("Cache file reset due to a different date");
		}

		if (isNew)
		{
			 _cache._block->_capacity = SIZE_STEP;
			 _cache._block->_size = 0;
			 _cache._block->_date = uDate;
			strcpy( _cache._block->_blk_flag, CACHE_FLAG);
		}
		else
		{
			//检查缓存文件是否有问题,要自动恢复
			do
			{
				uint64_t uSize = sizeof(CacheBlock) + sizeof(CacheItem) *  _cache._block->_capacity;
				uint64_t realSz =  _cache._file->size();
				if (realSz != uSize)
				{
					uint32_t realCap = (uint32_t)((realSz - sizeof(CacheBlock)) / sizeof(CacheItem));
					uint32_t markedSize = _cache._block->_size;
					//文件大小不匹配,一般是因为capacity改了,但是实际没扩容
					//这是做一次扩容即可
					 _cache._block->_capacity = realCap;
					 _cache._block->_size = (realCap < markedSize) ? realCap : markedSize;
				}

			} while (false);
		}

		//这里把索引加到hashmap中
		for (uint32_t i = 0; i < _cache._block->_size; i++)
			_indice[_cache._block->_items[i]._key] = i;

		return true;
	}

	inline void clear() noexcept
	{
		SpinLock guard(_lock);
		if (_cache._block == NULL)
			return;
		_indice.clear();

		for (uint32_t i = 0; i < _cache._block->_capacity; i++)
			_cache._block->_items[i] = CacheItem();
		_cache._block->_size = 0;
	}

	inline const char*	get(const char* key) const  noexcept
	{
		thread_local char value[64] = { 0 };
		SpinLock guard(_lock);
		if (_cache._block == NULL)
			return "";

		auto it = _indice.find(key);
		if (it == _indice.end())
			return "";

		wt_strcpy(value, _cache._block->_items[it->second]._val);
		return value;
	}

	void	put(const char* key, const char*val, std::size_t len = 0)  noexcept
	{
		SpinLock guard(_lock);
		if (_cache._block == NULL)
			return;

		auto it = _indice.find(key);
		if (it != _indice.end())
		{
			wt_strcpy(_cache._block->_items[it->second]._val, val, len);
		}
		else
		{
			if (_cache._block->_size == _cache._block->_capacity)
			{
				if (!resize(_cache._block->_capacity * 2))
					return;
			}

			uint32_t idx = _cache._block->_size;
			wt_strcpy(_cache._block->_items[idx]._key, key);
			wt_strcpy(_cache._block->_items[idx]._val, val, len);
			_cache._block->_size = idx + 1;
			_indice[key] = idx;
		}
	}

	void	put_if_none(const char* key, const char*val, std::size_t len = 0)  noexcept
	{
		SpinLock guard(_lock);
		if (_cache._block == NULL)
			return;

		auto it = _indice.find(key);
		if (it != _indice.end())
			return;

		{
			if (_cache._block->_size == _cache._block->_capacity)
			{
				if (!resize(_cache._block->_capacity * 2))
					return;
			}

			uint32_t idx = _cache._block->_size;
			wt_strcpy(_cache._block->_items[idx]._key, key);
			wt_strcpy(_cache._block->_items[idx]._val, val, len);
			_cache._block->_size = idx + 1;
			_indice[key] = idx;
		}
	}

	inline bool	has(const char* key) const  noexcept
	{
		SpinLock guard(_lock);
		return (_indice.find(key) != _indice.end());
	}

	inline uint32_t size() const noexcept
	{
		SpinLock guard(_lock);
		if (_cache._block == NULL)
			return 0;

		return _cache._block->_size;
	}

	inline uint32_t capacity() const noexcept
	{
		SpinLock guard(_lock);
		if (_cache._block == NULL)
			return 0;

		return _cache._block->_capacity;
	}
};

NS_WTP_END
