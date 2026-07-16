#include "../Share/AtomicCompat.hpp"

#include "gtest/gtest/gtest.h"

#include <atomic>
#include <cstdint>

TEST(test_atomic_compat, aligned_u32_operations_preserve_requested_protocol)
{
	alignas(wt::atomic_required_alignment<std::uint32_t>()) std::uint32_t value = 0;

	wt::atomic_store_u32(value, 7, std::memory_order_release);
	EXPECT_EQ(7u, wt::atomic_load_u32(value, std::memory_order_acquire));
	EXPECT_EQ(7u, wt::atomic_fetch_add_u32(value, 5, std::memory_order_acq_rel));
	EXPECT_EQ(12u, wt::atomic_load_u32(value, std::memory_order_relaxed));
}

TEST(test_atomic_compat, aligned_u64_operations_preserve_requested_protocol)
{
	alignas(wt::atomic_required_alignment<std::uint64_t>()) std::uint64_t value = 0;

	wt::atomic_store_u64(value, 11, std::memory_order_release);
	EXPECT_EQ(11u, wt::atomic_load_u64(value, std::memory_order_acquire));
	EXPECT_EQ(11u, wt::atomic_fetch_add_u64(value, 9, std::memory_order_acq_rel));
	EXPECT_EQ(20u, wt::atomic_load_u64(value, std::memory_order_relaxed));
}

TEST(test_atomic_compat, runtime_alignment_check_rejects_shifted_addresses)
{
	alignas(8) unsigned char storage[16] = {};
	EXPECT_TRUE(wt::atomic_is_aligned(storage, 8));
	EXPECT_FALSE(wt::atomic_is_aligned(storage + 1, 8));
	EXPECT_FALSE(wt::atomic_is_aligned(nullptr, 8));
}

TEST(test_atomic_compat, illegal_memory_orders_are_rejected_in_release_builds)
{
#if GTEST_HAS_DEATH_TEST
	EXPECT_DEATH({
		std::uint32_t value = 0;
		(void)wt::atomic_load_u32(value, std::memory_order_release);
	}, "");
	EXPECT_DEATH({
		std::uint64_t value = 0;
		wt::atomic_store_u64(value, 1, std::memory_order_acquire);
	}, "");
#else
	SUCCEED();
#endif
}
