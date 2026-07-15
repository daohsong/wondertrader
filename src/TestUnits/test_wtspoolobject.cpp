#include "gtest/gtest.h"

#include <atomic>
#include <cstdint>

#include "../Includes/WTSObject.hpp"

namespace
{
class PoolProbe final : public wtp::WTSPoolObject<PoolProbe>
{
	friend PoolType;

public:
	static PoolProbe* create()
	{
		return allocate();
	}

	static void resetCounts()
	{
		constructions.store(0);
		destructions.store(0);
	}

	static std::atomic<uint32_t> constructions;
	static std::atomic<uint32_t> destructions;

private:
	PoolProbe()
	{
		constructions.fetch_add(1);
	}

	~PoolProbe() override
	{
		destructions.fetch_add(1);
	}
};

std::atomic<uint32_t> PoolProbe::constructions{0};
std::atomic<uint32_t> PoolProbe::destructions{0};
}

TEST(WTSPoolObject, LastReleaseDestroysExactlyOnceAndAllowsReuse)
{
	PoolProbe::resetCounts();
	constexpr uint32_t iterations = 1000;
	for (uint32_t index = 0; index < iterations; ++index)
	{
		PoolProbe* object = PoolProbe::create();
		ASSERT_NE(object, nullptr);
		EXPECT_EQ(object->retainCount(), 1u);
		EXPECT_EQ(object->retain(), 2u);
		object->release();
		EXPECT_EQ(PoolProbe::destructions.load(), index);
		object->release();
		EXPECT_EQ(PoolProbe::destructions.load(), index + 1);
	}

	EXPECT_EQ(PoolProbe::constructions.load(), iterations);
	EXPECT_EQ(PoolProbe::destructions.load(), iterations);
}
