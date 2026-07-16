#include "../Share/FilesystemCompat.hpp"
#include "gtest/gtest/gtest.h"

TEST(test_header_self_contained_filesystem_compat, compiles_when_included_first)
{
	wt::fs::path path("compat.txt");
	EXPECT_EQ("compat.txt", path.string());
}
