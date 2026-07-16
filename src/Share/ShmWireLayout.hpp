/*!
 * \file ShmWireLayout.hpp
 * \project WonderTrader
 *
 * \brief Versioned, fixed-width layouts shared across WonderTrader processes.
 */
#pragma once

#include "AtomicCompat.hpp"
#include "../Includes/WTSStruct.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <type_traits>

namespace wt
{
namespace shm_wire
{
constexpr std::uint16_t kLayoutVersion = 1;
constexpr std::uint8_t kEndianLittle = 1;
constexpr std::uint8_t kEndianBig = 2;

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) \
	&& __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr std::uint8_t kNativeEndianness = kEndianBig;
#elif defined(_WIN32) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) \
	&& __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
constexpr std::uint8_t kNativeEndianness = kEndianLittle;
#else
#error "WonderTrader cannot determine the target byte order for shared-memory IPC"
#endif

constexpr std::uint64_t kShmBlockMagic = 0x575453484D424C4BULL; // WTSHMBLK
constexpr std::uint64_t kCmdBlockMagic = 0x5754434D44424C4BULL; // WTCMDBLK
constexpr std::uint64_t kCastQueueMagic = 0x5754434153545155ULL; // WTCASTQU

constexpr std::uint32_t kCapabilityFixedWidth = 1u << 0;
constexpr std::uint32_t kCapabilityAtomic32 = 1u << 1;
constexpr std::uint32_t kCapabilityAtomic64 = 1u << 2;
constexpr std::uint32_t kCapabilitySingleProducer = 1u << 3;
constexpr std::uint32_t kCapabilitySingleConsumer = 1u << 4;
constexpr std::uint32_t kCapabilityAlignedValues = 1u << 5;

constexpr std::uint32_t kShmBlockCapabilities = kCapabilityFixedWidth | kCapabilityAtomic64
	| kCapabilityAlignedValues;
constexpr std::uint32_t kCmdBlockCapabilities = kCapabilityFixedWidth
	| kCapabilityAtomic32 | kCapabilityAtomic64
	| kCapabilitySingleProducer | kCapabilitySingleConsumer;
constexpr std::uint32_t kCastQueueCapabilities = kCapabilityFixedWidth
	| kCapabilityAtomic32 | kCapabilityAtomic64
	| kCapabilitySingleProducer | kCapabilitySingleConsumer;

struct alignas(8) WireHeader
{
	std::uint64_t _magic;
	std::uint16_t _version;
	std::uint8_t _endianness;
	std::uint8_t _header_size;
	std::uint32_t _total_size;
	std::uint32_t _payload_offset;
	std::uint32_t _capabilities;
	std::uint64_t _reserved;

	WireHeader() noexcept
	{
		atomic_store_u64(_magic, 0, std::memory_order_relaxed);
	}
};

static_assert(sizeof(WireHeader) == 32);
static_assert(alignof(WireHeader) == 8);
static_assert(std::is_standard_layout_v<WireHeader>);
static_assert(std::is_trivially_copyable_v<WireHeader>);
static_assert(offsetof(WireHeader, _magic) == 0);
static_assert(offsetof(WireHeader, _version) == 8);
static_assert(offsetof(WireHeader, _endianness) == 10);
static_assert(offsetof(WireHeader, _header_size) == 11);
static_assert(offsetof(WireHeader, _total_size) == 12);
static_assert(offsetof(WireHeader, _payload_offset) == 16);
static_assert(offsetof(WireHeader, _capabilities) == 20);
static_assert(offsetof(WireHeader, _reserved) == 24);

inline void initialize_header(
	WireHeader& header,
	std::uint64_t magic,
	std::size_t total_size,
	std::size_t payload_offset,
	std::uint32_t capabilities) noexcept
{
	const bool sizes_fit = total_size <= std::numeric_limits<std::uint32_t>::max()
		&& payload_offset <= total_size
		&& payload_offset <= std::numeric_limits<std::uint32_t>::max();
	assert(sizes_fit);
	if (!sizes_fit)
		std::terminate();

	header._version = kLayoutVersion;
	header._endianness = kNativeEndianness;
	header._header_size = static_cast<std::uint8_t>(sizeof(WireHeader));
	header._total_size = static_cast<std::uint32_t>(total_size);
	header._payload_offset = static_cast<std::uint32_t>(payload_offset);
	header._capabilities = capabilities;
	header._reserved = 0;
	atomic_store_u64(header._magic, magic, std::memory_order_release);
}

enum class ValidationError : std::uint8_t
{
	ok = 0,
	null_address,
	unexpected_size,
	base_alignment,
	bad_magic,
	bad_version,
	bad_endianness,
	bad_header_size,
	bad_total_size,
	bad_payload_offset,
	bad_capabilities,
	bad_reserved,
	bad_capacity,
	bad_bounds,
	atomic_alignment
};

inline const char* validation_error_message(ValidationError error) noexcept
{
	switch (error)
	{
	case ValidationError::ok: return "ok";
	case ValidationError::null_address: return "null mapping address";
	case ValidationError::unexpected_size: return "unexpected mapping size";
	case ValidationError::base_alignment: return "mapping base is under-aligned";
	case ValidationError::bad_magic: return "wire magic mismatch";
	case ValidationError::bad_version: return "wire layout version mismatch";
	case ValidationError::bad_endianness: return "wire byte order mismatch";
	case ValidationError::bad_header_size: return "wire header size mismatch";
	case ValidationError::bad_total_size: return "wire total size mismatch";
	case ValidationError::bad_payload_offset: return "wire payload offset mismatch";
	case ValidationError::bad_capabilities: return "wire capabilities mismatch";
	case ValidationError::bad_reserved: return "wire reserved field is nonzero";
	case ValidationError::bad_capacity: return "wire queue capacity mismatch";
	case ValidationError::bad_bounds: return "wire payload bounds are invalid";
	case ValidationError::atomic_alignment: return "wire atomic field is under-aligned";
	}
	return "unknown wire validation error";
}

inline ValidationError validate_header(
	const void* address,
	std::size_t mapped_size,
	std::size_t required_alignment,
	std::uint64_t expected_magic,
	std::size_t expected_size,
	std::size_t expected_payload_offset,
	std::uint32_t expected_capabilities) noexcept
{
	if (address == nullptr)
		return ValidationError::null_address;
	if (mapped_size != expected_size || expected_size > std::numeric_limits<std::uint32_t>::max())
		return ValidationError::unexpected_size;
	if (!atomic_is_aligned(address, required_alignment))
		return ValidationError::base_alignment;

	const auto& header = *static_cast<const WireHeader*>(address);
	if (atomic_load_u64(header._magic, std::memory_order_acquire) != expected_magic)
		return ValidationError::bad_magic;
	if (header._version != kLayoutVersion)
		return ValidationError::bad_version;
	if (header._endianness != kNativeEndianness)
		return ValidationError::bad_endianness;
	if (header._header_size != sizeof(WireHeader))
		return ValidationError::bad_header_size;
	if (header._total_size != expected_size)
		return ValidationError::bad_total_size;
	if (header._payload_offset != expected_payload_offset)
		return ValidationError::bad_payload_offset;
	if (header._capabilities != expected_capabilities)
		return ValidationError::bad_capabilities;
	if (header._reserved != 0)
		return ValidationError::bad_reserved;
	return ValidationError::ok;
}

enum ValueType : std::uint64_t
{
	SMVT_INT32 = 1,
	SMVT_UINT32 = 2,
	SMVT_INT64 = 3,
	SMVT_UINT64 = 4,
	SMVT_DOUBLE = 5,
	SMVT_STRING = 6
};

constexpr int kMaxSectionCount = 64;
constexpr int kMaxKeyCount = 128;
constexpr int kMaxCommandSize = 64;
constexpr std::uint32_t kCommandCapacity = 128;
inline constexpr std::size_t kValueSizes[] = {0, 4, 4, 8, 8, 8, 64};
inline constexpr std::size_t kValueAlignments[] = {1, 4, 4, 8, 8, 8, 1};

struct alignas(8) KeyInfo
{
	char _key[32];
	ValueType _type;
	std::uint32_t _offset;
	std::uint32_t _reserved;
	std::uint64_t _updatetime;
};

struct alignas(8) SectionInfo
{
	char _name[32];
	KeyInfo _keys[kMaxKeyCount];
	std::uint16_t _count;
	std::uint16_t _state;
	std::uint32_t _offset;
	std::uint64_t _updatetime;
	char _data[1024];

	template <typename T>
	T* get(std::uint32_t offset) noexcept
	{
		return reinterpret_cast<T*>(_data + offset);
	}

	SectionInfo() noexcept
	{
		std::memset(this, 0, sizeof(*this));
	}
};

static_assert(offsetof(SectionInfo, _data) % alignof(std::uint64_t) == 0);

struct alignas(8) ShmBlock
{
	WireHeader _wire;
	char _flag[8];
	char _name[32];
	SectionInfo _sections[kMaxSectionCount];
	std::uint64_t _updatetime;
	std::uint32_t _count;
	std::uint32_t _reserved;

	ShmBlock() noexcept
	{
		std::memset(_flag, 0, sizeof(*this) - offsetof(ShmBlock, _flag));
		initialize_header(_wire, kShmBlockMagic, sizeof(*this), offsetof(ShmBlock, _flag), kShmBlockCapabilities);
	}
};

struct alignas(4) CommandInfo
{
	std::uint32_t _state;
	char _command[kMaxCommandSize];

	CommandInfo() noexcept
	{
		std::memset(this, 0, sizeof(*this));
	}
};

template <int N = static_cast<int>(kCommandCapacity)>
struct alignas(8) CommandBlock
{
	static_assert(N > 0);
	WireHeader _wire;
	std::uint32_t _capacity;
	alignas(atomic_required_alignment<std::uint32_t>()) std::uint32_t _readable;
	alignas(atomic_required_alignment<std::uint32_t>()) std::uint32_t _writable;
	std::uint32_t _cmdpid;
	CommandInfo _commands[N];

	CommandBlock() noexcept
		: _capacity(static_cast<std::uint32_t>(N)), _readable(UINT32_MAX), _writable(0), _cmdpid(0)
	{
		initialize_header(_wire, kCmdBlockMagic, sizeof(*this), offsetof(CommandBlock, _commands), kCmdBlockCapabilities);
	}
};

using CmdBlock = CommandBlock<static_cast<int>(kCommandCapacity)>;

struct alignas(8) CastDataItem
{
	std::uint32_t _type;
	std::uint32_t _reserved;
	union
	{
		wtp::WTSTickStruct _tick;
		wtp::WTSOrdQueStruct _queue;
		wtp::WTSOrdDtlStruct _order;
		wtp::WTSTransStruct _trans;
	};

	CastDataItem() noexcept
	{
		std::memset(this, 0, sizeof(*this));
	}
};

template <int N = 8 * 1024>
struct alignas(8) CastQueue
{
	static_assert(N > 0);
	WireHeader _wire;
	std::uint64_t _capacity;
	alignas(atomic_required_alignment<std::uint64_t>()) std::uint64_t _readable;
	alignas(atomic_required_alignment<std::uint64_t>()) std::uint64_t _writable;
	alignas(atomic_required_alignment<std::uint32_t>()) std::uint32_t _pid;
	std::uint32_t _reserved;
	CastDataItem _items[N];

	CastQueue() noexcept
		: _capacity(static_cast<std::uint64_t>(N)), _readable(UINT64_MAX), _writable(0), _pid(0), _reserved(0)
	{
		initialize_header(_wire, kCastQueueMagic, sizeof(*this), offsetof(CastQueue, _items), kCastQueueCapabilities);
	}
};

using DefaultCastQueue = CastQueue<8 * 1024>;

inline ValidationError validate_shm_block(const void* address, std::size_t mapped_size) noexcept
{
	ValidationError error = validate_header(address, mapped_size, alignof(ShmBlock), kShmBlockMagic,
		sizeof(ShmBlock), offsetof(ShmBlock, _flag), kShmBlockCapabilities);
	if (error != ValidationError::ok)
		return error;

	const auto* block = static_cast<const ShmBlock*>(address);
	if (block->_reserved != 0)
		return ValidationError::bad_reserved;
	if (block->_count > kMaxSectionCount)
		return ValidationError::bad_bounds;
	for (std::uint32_t section_index = 0; section_index < block->_count; ++section_index)
	{
		const SectionInfo& section = block->_sections[section_index];
		if (section._count > kMaxKeyCount || section._offset > sizeof(section._data) || section._state > 1)
			return ValidationError::bad_bounds;
		for (std::uint32_t key_index = 0; key_index < section._count; ++key_index)
		{
			const KeyInfo& key = section._keys[key_index];
			const std::size_t type_index = static_cast<std::size_t>(key._type);
			if (key._reserved != 0)
				return ValidationError::bad_reserved;
			if (type_index == 0 || type_index >= (sizeof(kValueSizes) / sizeof(kValueSizes[0]))
				|| key._offset > section._offset
				|| kValueSizes[type_index] > section._offset - key._offset
				|| key._offset % kValueAlignments[type_index] != 0)
				return ValidationError::bad_bounds;
		}
	}
	return ValidationError::ok;
}

template <int N>
inline ValidationError validate_command_block(const void* address, std::size_t mapped_size) noexcept
{
	using Block = CommandBlock<N>;
	ValidationError error = validate_header(address, mapped_size, alignof(Block), kCmdBlockMagic,
		sizeof(Block), offsetof(Block, _commands), kCmdBlockCapabilities);
	if (error != ValidationError::ok)
		return error;

	const auto* block = static_cast<const Block*>(address);
	if (block->_capacity != static_cast<std::uint32_t>(N))
		return ValidationError::bad_capacity;
	if (!atomic_is_aligned(&block->_readable, atomic_required_alignment<std::uint32_t>())
		|| !atomic_is_aligned(&block->_writable, atomic_required_alignment<std::uint32_t>())
		|| !atomic_is_aligned(&block->_cmdpid, atomic_required_alignment<std::uint32_t>()))
		return ValidationError::atomic_alignment;
	return ValidationError::ok;
}

template <int N>
inline ValidationError validate_cast_queue(const void* address, std::size_t mapped_size) noexcept
{
	using Queue = CastQueue<N>;
	ValidationError error = validate_header(address, mapped_size, alignof(Queue), kCastQueueMagic,
		sizeof(Queue), offsetof(Queue, _items), kCastQueueCapabilities);
	if (error != ValidationError::ok)
		return error;

	const auto* queue = static_cast<const Queue*>(address);
	if (queue->_capacity != static_cast<std::uint64_t>(N))
		return ValidationError::bad_capacity;
	if (queue->_reserved != 0)
		return ValidationError::bad_reserved;
	if (!atomic_is_aligned(&queue->_readable, atomic_required_alignment<std::uint64_t>())
		|| !atomic_is_aligned(&queue->_writable, atomic_required_alignment<std::uint64_t>())
		|| !atomic_is_aligned(&queue->_pid, atomic_required_alignment<std::uint32_t>()))
		return ValidationError::atomic_alignment;
	return ValidationError::ok;
}

static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
static_assert(sizeof(wtp::WTSTickStruct) == 512);
static_assert(alignof(wtp::WTSTickStruct) == 8);
static_assert(sizeof(wtp::WTSOrdQueStruct) == 280);
static_assert(alignof(wtp::WTSOrdQueStruct) == 8);
static_assert(sizeof(wtp::WTSOrdDtlStruct) == 104);
static_assert(alignof(wtp::WTSOrdDtlStruct) == 8);
static_assert(sizeof(wtp::WTSTransStruct) == 112);
static_assert(alignof(wtp::WTSTransStruct) == 8);
static_assert(std::is_standard_layout_v<wtp::WTSTickStruct>);
static_assert(std::is_standard_layout_v<wtp::WTSOrdQueStruct>);
static_assert(std::is_standard_layout_v<wtp::WTSOrdDtlStruct>);
static_assert(std::is_standard_layout_v<wtp::WTSTransStruct>);
static_assert(std::is_trivially_copyable_v<wtp::WTSTickStruct>);
static_assert(std::is_trivially_copyable_v<wtp::WTSOrdQueStruct>);
static_assert(std::is_trivially_copyable_v<wtp::WTSOrdDtlStruct>);
static_assert(std::is_trivially_copyable_v<wtp::WTSTransStruct>);
static_assert(offsetof(wtp::WTSTickStruct, code) == 16);
static_assert(offsetof(wtp::WTSTickStruct, price) == 48);
static_assert(offsetof(wtp::WTSTickStruct, trading_date) == 152);
static_assert(offsetof(wtp::WTSTickStruct, bid_prices) == 192);
static_assert(offsetof(wtp::WTSOrdQueStruct, side) == 60);
static_assert(offsetof(wtp::WTSOrdQueStruct, price) == 64);
static_assert(offsetof(wtp::WTSOrdQueStruct, volumes) == 80);
static_assert(offsetof(wtp::WTSOrdDtlStruct, index) == 64);
static_assert(offsetof(wtp::WTSOrdDtlStruct, price) == 72);
static_assert(offsetof(wtp::WTSOrdDtlStruct, orderid) == 96);
static_assert(offsetof(wtp::WTSTransStruct, index) == 64);
static_assert(offsetof(wtp::WTSTransStruct, ttype) == 72);
static_assert(offsetof(wtp::WTSTransStruct, price) == 80);
static_assert(offsetof(wtp::WTSTransStruct, askorder) == 96);

static_assert(sizeof(KeyInfo) == 56);
static_assert(alignof(KeyInfo) == 8);
static_assert(std::is_standard_layout_v<KeyInfo>);
static_assert(std::is_trivially_copyable_v<KeyInfo>);
static_assert(offsetof(KeyInfo, _type) == 32);
static_assert(offsetof(KeyInfo, _offset) == 40);
static_assert(offsetof(KeyInfo, _updatetime) == 48);
static_assert(sizeof(SectionInfo) == 8240);
static_assert(alignof(SectionInfo) == 8);
static_assert(std::is_standard_layout_v<SectionInfo>);
static_assert(std::is_trivially_copyable_v<SectionInfo>);
static_assert(offsetof(SectionInfo, _keys) == 32);
static_assert(offsetof(SectionInfo, _count) == 7200);
static_assert(offsetof(SectionInfo, _updatetime) == 7208);
static_assert(offsetof(SectionInfo, _data) == 7216);
static_assert(sizeof(ShmBlock) == 527448);
static_assert(alignof(ShmBlock) == 8);
static_assert(std::is_standard_layout_v<ShmBlock>);
static_assert(std::is_trivially_copyable_v<ShmBlock>);
static_assert(offsetof(ShmBlock, _flag) == 32);
static_assert(offsetof(ShmBlock, _sections) == 72);
static_assert(offsetof(ShmBlock, _updatetime) == 527432);
static_assert(offsetof(ShmBlock, _count) == 527440);

static_assert(sizeof(CommandInfo) == 68);
static_assert(alignof(CommandInfo) == 4);
static_assert(std::is_standard_layout_v<CommandInfo>);
static_assert(std::is_trivially_copyable_v<CommandInfo>);
static_assert(sizeof(CmdBlock) == 8752);
static_assert(alignof(CmdBlock) == 8);
static_assert(std::is_standard_layout_v<CmdBlock>);
static_assert(std::is_trivially_copyable_v<CmdBlock>);
static_assert(offsetof(CmdBlock, _capacity) == 32);
static_assert(offsetof(CmdBlock, _readable) == 36);
static_assert(offsetof(CmdBlock, _writable) == 40);
static_assert(offsetof(CmdBlock, _cmdpid) == 44);
static_assert(offsetof(CmdBlock, _commands) == 48);

static_assert(sizeof(CastDataItem) == 520);
static_assert(alignof(CastDataItem) == 8);
static_assert(std::is_standard_layout_v<CastDataItem>);
static_assert(std::is_trivially_copyable_v<CastDataItem>);
static_assert(offsetof(CastDataItem, _type) == 0);
static_assert(offsetof(CastDataItem, _tick) == 8);
static_assert(sizeof(DefaultCastQueue) == 4259904);
static_assert(alignof(DefaultCastQueue) == 8);
static_assert(std::is_standard_layout_v<DefaultCastQueue>);
static_assert(std::is_trivially_copyable_v<DefaultCastQueue>);
static_assert(offsetof(DefaultCastQueue, _capacity) == 32);
static_assert(offsetof(DefaultCastQueue, _readable) == 40);
static_assert(offsetof(DefaultCastQueue, _writable) == 48);
static_assert(offsetof(DefaultCastQueue, _pid) == 56);
static_assert(offsetof(DefaultCastQueue, _items) == 64);
}
}
