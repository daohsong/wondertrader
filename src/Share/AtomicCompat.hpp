/*!
 * \file AtomicCompat.hpp
 * \project WonderTrader
 *
 * \brief Lock-free atomic access for naturally aligned shared-memory integers.
 */
#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <type_traits>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace wt
{
namespace atomic_compat_detail
{
template <typename T>
struct is_supported_integer : std::bool_constant<
	std::is_same_v<std::remove_cv_t<T>, std::uint32_t>
	|| std::is_same_v<std::remove_cv_t<T>, std::uint64_t>>
{
};

inline bool valid_load_order(std::memory_order order) noexcept
{
	return order == std::memory_order_relaxed
		|| order == std::memory_order_consume
		|| order == std::memory_order_acquire
		|| order == std::memory_order_seq_cst;
}

inline bool valid_store_order(std::memory_order order) noexcept
{
	return order == std::memory_order_relaxed
		|| order == std::memory_order_release
		|| order == std::memory_order_seq_cst;
}

inline bool valid_rmw_order(std::memory_order order) noexcept
{
	return order == std::memory_order_relaxed
		|| order == std::memory_order_consume
		|| order == std::memory_order_acquire
		|| order == std::memory_order_release
		|| order == std::memory_order_acq_rel
		|| order == std::memory_order_seq_cst;
}

inline void require_valid_order(bool valid) noexcept
{
	assert(valid);
	if (!valid)
		std::terminate();
}

#if defined(__GNUC__) || defined(__clang__)
static_assert(__atomic_always_lock_free(sizeof(std::uint32_t), nullptr),
	"WonderTrader shared-memory IPC requires lock-free 32-bit compiler atomics");
static_assert(__atomic_always_lock_free(sizeof(std::uint64_t), nullptr),
	"WonderTrader shared-memory IPC requires lock-free 64-bit compiler atomics");

inline int builtin_order(std::memory_order order) noexcept
{
	switch (order)
	{
	case std::memory_order_relaxed: return __ATOMIC_RELAXED;
	case std::memory_order_consume: return __ATOMIC_CONSUME;
	case std::memory_order_acquire: return __ATOMIC_ACQUIRE;
	case std::memory_order_release: return __ATOMIC_RELEASE;
	case std::memory_order_acq_rel: return __ATOMIC_ACQ_REL;
	case std::memory_order_seq_cst: return __ATOMIC_SEQ_CST;
	}
	std::terminate();
}
#elif defined(_MSC_VER)
static_assert(sizeof(std::uint32_t) == sizeof(long),
	"WonderTrader shared-memory IPC requires 32-bit Interlocked operations");
static_assert(sizeof(std::uint64_t) == sizeof(__int64),
	"WonderTrader shared-memory IPC requires 64-bit Interlocked operations");

inline volatile long* interlocked_address(std::uint32_t& value) noexcept
{
	return reinterpret_cast<volatile long*>(&value);
}

inline volatile long* interlocked_address(const std::uint32_t& value) noexcept
{
	return interlocked_address(const_cast<std::uint32_t&>(value));
}

inline volatile __int64* interlocked_address(std::uint64_t& value) noexcept
{
	return reinterpret_cast<volatile __int64*>(&value);
}

inline volatile __int64* interlocked_address(const std::uint64_t& value) noexcept
{
	return interlocked_address(const_cast<std::uint64_t&>(value));
}
#elif defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free,
	"WonderTrader shared-memory IPC requires lock-free 32-bit atomic_ref");
static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free,
	"WonderTrader shared-memory IPC requires lock-free 64-bit atomic_ref");
#else
#error "WonderTrader shared-memory IPC needs GCC/Clang lock-free builtins, MSVC Interlocked, or lock-free std::atomic_ref"
#endif
}

template <typename T>
constexpr std::size_t atomic_required_alignment() noexcept
{
	static_assert(atomic_compat_detail::is_supported_integer<T>::value,
		"atomic_required_alignment only supports uint32_t and uint64_t");
#if !defined(__GNUC__) && !defined(__clang__) && !defined(_MSC_VER) \
	&& defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
	return std::atomic_ref<std::remove_cv_t<T>>::required_alignment;
#else
	return alignof(std::remove_cv_t<T>);
#endif
}

inline bool atomic_is_aligned(const void* address, std::size_t alignment) noexcept
{
	return address != nullptr
		&& alignment != 0
		&& reinterpret_cast<std::uintptr_t>(address) % alignment == 0;
}

template <typename T>
inline void atomic_require_aligned(const T* address) noexcept
{
	static_assert(atomic_compat_detail::is_supported_integer<T>::value,
		"atomic_require_aligned only supports uint32_t and uint64_t");
	const bool aligned = atomic_is_aligned(address, atomic_required_alignment<T>());
	assert(aligned);
	if (!aligned)
		std::terminate();
}

inline std::uint32_t atomic_load_u32(
	const std::uint32_t& value,
	std::memory_order order = std::memory_order_seq_cst) noexcept
{
	atomic_compat_detail::require_valid_order(atomic_compat_detail::valid_load_order(order));
	atomic_require_aligned(&value);
#if defined(__GNUC__) || defined(__clang__)
	return __atomic_load_n(&value, atomic_compat_detail::builtin_order(order));
#elif defined(_MSC_VER)
	(void)order;
	return static_cast<std::uint32_t>(_InterlockedCompareExchange(
		atomic_compat_detail::interlocked_address(value), 0, 0));
#else
	return std::atomic_ref<const std::uint32_t>(value).load(order);
#endif
}

inline std::uint64_t atomic_load_u64(
	const std::uint64_t& value,
	std::memory_order order = std::memory_order_seq_cst) noexcept
{
	atomic_compat_detail::require_valid_order(atomic_compat_detail::valid_load_order(order));
	atomic_require_aligned(&value);
#if defined(__GNUC__) || defined(__clang__)
	return __atomic_load_n(&value, atomic_compat_detail::builtin_order(order));
#elif defined(_MSC_VER)
	(void)order;
	return static_cast<std::uint64_t>(_InterlockedCompareExchange64(
		atomic_compat_detail::interlocked_address(value), 0, 0));
#else
	return std::atomic_ref<const std::uint64_t>(value).load(order);
#endif
}

inline void atomic_store_u32(
	std::uint32_t& target,
	std::uint32_t value,
	std::memory_order order = std::memory_order_seq_cst) noexcept
{
	atomic_compat_detail::require_valid_order(atomic_compat_detail::valid_store_order(order));
	atomic_require_aligned(&target);
#if defined(__GNUC__) || defined(__clang__)
	__atomic_store_n(&target, value, atomic_compat_detail::builtin_order(order));
#elif defined(_MSC_VER)
	(void)order;
	_InterlockedExchange(atomic_compat_detail::interlocked_address(target), static_cast<long>(value));
#else
	std::atomic_ref<std::uint32_t>(target).store(value, order);
#endif
}

inline void atomic_store_u64(
	std::uint64_t& target,
	std::uint64_t value,
	std::memory_order order = std::memory_order_seq_cst) noexcept
{
	atomic_compat_detail::require_valid_order(atomic_compat_detail::valid_store_order(order));
	atomic_require_aligned(&target);
#if defined(__GNUC__) || defined(__clang__)
	__atomic_store_n(&target, value, atomic_compat_detail::builtin_order(order));
#elif defined(_MSC_VER)
	(void)order;
	_InterlockedExchange64(atomic_compat_detail::interlocked_address(target), static_cast<__int64>(value));
#else
	std::atomic_ref<std::uint64_t>(target).store(value, order);
#endif
}

inline std::uint32_t atomic_fetch_add_u32(
	std::uint32_t& target,
	std::uint32_t value,
	std::memory_order order = std::memory_order_seq_cst) noexcept
{
	atomic_compat_detail::require_valid_order(atomic_compat_detail::valid_rmw_order(order));
	atomic_require_aligned(&target);
#if defined(__GNUC__) || defined(__clang__)
	return __atomic_fetch_add(&target, value, atomic_compat_detail::builtin_order(order));
#elif defined(_MSC_VER)
	(void)order;
	return static_cast<std::uint32_t>(_InterlockedExchangeAdd(
		atomic_compat_detail::interlocked_address(target), static_cast<long>(value)));
#else
	return std::atomic_ref<std::uint32_t>(target).fetch_add(value, order);
#endif
}

inline std::uint64_t atomic_fetch_add_u64(
	std::uint64_t& target,
	std::uint64_t value,
	std::memory_order order = std::memory_order_seq_cst) noexcept
{
	atomic_compat_detail::require_valid_order(atomic_compat_detail::valid_rmw_order(order));
	atomic_require_aligned(&target);
#if defined(__GNUC__) || defined(__clang__)
	return __atomic_fetch_add(&target, value, atomic_compat_detail::builtin_order(order));
#elif defined(_MSC_VER)
	(void)order;
	return static_cast<std::uint64_t>(_InterlockedExchangeAdd64(
		atomic_compat_detail::interlocked_address(target), static_cast<__int64>(value)));
#else
	return std::atomic_ref<std::uint64_t>(target).fetch_add(value, order);
#endif
}
}

// Transitional global spellings retained for existing modules and external
// code while the shared-memory API moves under namespace wt.
template <typename T>
constexpr std::size_t wt_atomic_required_alignment() noexcept
{
	return wt::atomic_required_alignment<T>();
}

inline bool wt_atomic_is_aligned(const void* address, std::size_t alignment) noexcept
{
	return wt::atomic_is_aligned(address, alignment);
}

inline std::uint32_t wt_atomic_load_u32(const std::uint32_t& value, std::memory_order order) noexcept
{
	return wt::atomic_load_u32(value, order);
}

inline std::uint64_t wt_atomic_load_u64(const std::uint64_t& value, std::memory_order order) noexcept
{
	return wt::atomic_load_u64(value, order);
}

inline void wt_atomic_store_u32(std::uint32_t& target, std::uint32_t value, std::memory_order order) noexcept
{
	wt::atomic_store_u32(target, value, order);
}

inline void wt_atomic_store_u64(std::uint64_t& target, std::uint64_t value, std::memory_order order) noexcept
{
	wt::atomic_store_u64(target, value, order);
}

inline std::uint32_t wt_atomic_fetch_add_u32(std::uint32_t& target, std::uint32_t value, std::memory_order order) noexcept
{
	return wt::atomic_fetch_add_u32(target, value, order);
}

inline std::uint64_t wt_atomic_fetch_add_u64(std::uint64_t& target, std::uint64_t value, std::memory_order order) noexcept
{
	return wt::atomic_fetch_add_u64(target, value, order);
}
