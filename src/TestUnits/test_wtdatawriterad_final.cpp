#include "gtest/gtest.h"

#include <type_traits>

#include "../WtDataStorageAD/WtDataWriterAD.h"

TEST(IDataWriterLifecycle, AdvancedWriterIsFinal)
{
	EXPECT_TRUE(std::is_final<WtDataWriterAD>::value);
}
