#include "../Share/SpinMutex.hpp"
#include "../Share/BoostFile.hpp"
#include "../Share/StdUtils.hpp"
#include "../Share/BoostMappingFile.hpp"
#include "../Includes/FasterDefs.h"

#define private public
#include "../Share/WtKVCache.hpp"
#undef private

#include "gtest/gtest/gtest.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

USING_NS_WTP;

TEST(test_compile_warnings_kvcache, cache_block_size_is_plain_uint32_and_layout_is_stable)
{
	using CacheBlock = WtKVCache::CacheBlock;
	using SizeType = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<CacheBlock&>()._size)>>;

	static_assert(std::is_same_v<SizeType, uint32_t>);
	static_assert(!std::is_volatile_v<std::remove_reference_t<decltype(std::declval<CacheBlock&>()._size)>>);
	static_assert(offsetof(CacheBlock, _size) == FLAG_SIZE);
	static_assert(offsetof(CacheBlock, _capacity) == FLAG_SIZE + sizeof(uint32_t));
	static_assert(offsetof(CacheBlock, _date) == FLAG_SIZE + sizeof(uint32_t) * 2);
	static_assert(offsetof(CacheBlock, _items) == FLAG_SIZE + sizeof(uint32_t) * 3);

	SUCCEED();
}
