#pragma once
#include <stdint.h>
#include <cstddef>
#include <memory>
#include <type_traits>

#include "../Share/BoostMappingFile.hpp"
#include "../Share/ShmWireLayout.hpp"
#include "../Share/fmtlib.h"
#include "../Includes/FasterDefs.h"

USING_NS_WTP;

typedef std::shared_ptr<BoostMappingFile> MappedFilePtr;

namespace shareblock
{
	const char BLK_FLAG[] = "&^%$#@!\0";

	constexpr int FLAG_SIZE = 8;
	constexpr int MAX_SEC_CNT = wt::shm_wire::kMaxSectionCount;
	constexpr int MAX_KEY_CNT = wt::shm_wire::kMaxKeyCount;
	constexpr int MAX_CMD_SIZE = wt::shm_wire::kMaxCommandSize;
	constexpr uint32_t CMD_BLOCK_CAPACITY = wt::shm_wire::kCommandCapacity;

	using ValueType = wt::shm_wire::ValueType;
	constexpr ValueType SMVT_INT32 = wt::shm_wire::SMVT_INT32;
	constexpr ValueType SMVT_UINT32 = wt::shm_wire::SMVT_UINT32;
	constexpr ValueType SMVT_INT64 = wt::shm_wire::SMVT_INT64;
	constexpr ValueType SMVT_UINT64 = wt::shm_wire::SMVT_UINT64;
	constexpr ValueType SMVT_DOUBLE = wt::shm_wire::SMVT_DOUBLE;
	constexpr ValueType SMVT_STRING = wt::shm_wire::SMVT_STRING;
	inline constexpr const std::size_t (&SMVT_SIZES)[7] = wt::shm_wire::kValueSizes;
	inline constexpr const std::size_t (&SMVT_ALIGNMENTS)[7] = wt::shm_wire::kValueAlignments;

	using KeyInfo = wt::shm_wire::KeyInfo;
	using SecInfo = wt::shm_wire::SectionInfo;
	using ShmBlock = wt::shm_wire::ShmBlock;
	using CmdInfo = wt::shm_wire::CommandInfo;
	template <int N = static_cast<int>(CMD_BLOCK_CAPACITY)>
	using _CmdBlock = wt::shm_wire::CommandBlock<N>;
	using CmdBlock = wt::shm_wire::CmdBlock;


	class ShareBlocks
	{
	private:
		ShareBlocks(){}

	public:
		static ShareBlocks& one()
		{
			static ShareBlocks inst;
			return inst;
		}

		typedef void(*FuncLogger)(uint32_t, const char*);
		void	register_logger(FuncLogger logger)
		{
			_logger = logger;
		}

		template<typename ...Args>
		void	write_log(uint32_t lvl, const char* format, const Args&... args) noexcept
		{
			if (!_logger)
				return;

			const char* buffer = fmtutil::format(format, args...);
			_logger(lvl, buffer);
		}

		bool	init_master(const char* name, const char* path = "");
		bool	init_slave(const char* name, const char* path = "");

		bool	update_slave(const char* name, bool bForce);
		bool	release_slave(const char* name);

		std::vector<std::string>	get_sections(const char* domain);
		std::vector<KeyInfo*>		get_keys(const char* domain, const char* section);

		uint64_t get_section_updatetime(const char* domain, const char* section);
		bool	commit_section(const char* domain, const char* section);

		bool	delete_section(const char* domain, const char*section);

		const char* allocate_string(const char* domain, const char* section, const char* key, const char* initVal = "", bool bForceWrite = false);
		int32_t*	allocate_int32(const char* domain, const char* section, const char* key, int32_t initVal = 0, bool bForceWrite = false);
		int64_t*	allocate_int64(const char* domain, const char* section, const char* key, int64_t initVal = 0, bool bForceWrite = false);
		uint32_t*	allocate_uint32(const char* domain, const char* section, const char* key, uint32_t initVal = 0, bool bForceWrite = false);
		uint64_t*	allocate_uint64(const char* domain, const char* section, const char* key, uint64_t initVal = 0, bool bForceWrite = false);
		double*		allocate_double(const char* domain, const char* section, const char* key, double initVal = 0, bool bForceWrite = false);

		bool	set_string(const char* domain, const char* section, const char* key, const char* val);
		bool	set_int32(const char* domain, const char* section, const char* key, int32_t val);
		bool	set_int64(const char* domain, const char* section, const char* key, int64_t val);
		bool	set_uint32(const char* domain, const char* section, const char* key, uint32_t val);
		bool	set_uint64(const char* domain, const char* section, const char* key, uint64_t val);
		bool	set_double(const char* domain, const char* section, const char* key, double val);

		const char*	get_string(const char* domain, const char* section, const char* key, const char* defVal = "");
		int32_t		get_int32(const char* domain, const char* section, const char* key, int32_t defVal = 0);
		int64_t		get_int64(const char* domain, const char* section, const char* key, int64_t defVal = 0);
		uint32_t	get_uint32(const char* domain, const char* section, const char* key, uint32_t defVal = 0);
		uint64_t	get_uint64(const char* domain, const char* section, const char* key, uint64_t defVal = 0);
		double		get_double(const char* domain, const char* section, const char* key, double defVal = 0);

	public:
		bool	init_cmder(const char* name, bool isCmder = false, const char* path = "");
		bool	add_cmd(const char* name, const char* cmd);
		const char*	get_cmd(const char* name, uint32_t& lastIdx);

	private:
		void*	make_valid(const char* domain, const char* section, const char* key, ValueType vType, SecInfo* &secInfo);
		void*	check_valid(const char* domain, const char* section, const char* key, ValueType vType, SecInfo* &secInfo);

	private:
		typedef struct _ShmPair
		{
			MappedFilePtr	_domain;
			ShmBlock*		_block;
			bool			_master;
			uint64_t		_blocktime;

			typedef wt_hashmap<std::string, KeyInfo*>	KVMap;
			typedef struct _KVPair
			{
				uint32_t	_index;
				KVMap		_keys;
			} KVPair;
			typedef wt_hashmap<std::string, KVPair>	SectionMap;
			SectionMap	_sections;

			_ShmPair() :_block(nullptr),_master(false)
			{
			}
		}ShmPair;
		typedef wt_hashmap<std::string, ShmPair>	ShmBlockMap;
		ShmBlockMap		_shm_blocks;

		typedef struct _CmdPair
		{
			MappedFilePtr	_domain;
			CmdBlock*		_block;
			bool			_cmder;
		} CmdPair;
		typedef wt_hashmap<std::string, CmdPair>	CmdBlockMap;
		CmdBlockMap		_cmd_blocks;

		FuncLogger	_logger = nullptr;
	};
}
