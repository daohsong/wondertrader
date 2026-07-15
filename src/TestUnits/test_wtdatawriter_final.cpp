#include "gtest/gtest.h"

#include <type_traits>

#include "../WtDataStorage/WtDataWriter.h"

TEST(IDataWriterLifecycle, StandardWriterIsFinal)
{
	EXPECT_TRUE(std::is_final<WtDataWriter>::value);
}
