#include "gtest/gtest.h"

#include <type_traits>

#include "../Includes/IDataWriter.h"

TEST(IDataWriterLifecycle, KeepsExistingNonVirtualDestructorAbi)
{
	EXPECT_TRUE(std::is_polymorphic<wtp::IDataWriter>::value);
	EXPECT_FALSE(std::has_virtual_destructor<wtp::IDataWriter>::value);
}
