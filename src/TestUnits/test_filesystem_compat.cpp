#include "../Share/FilesystemCompat.hpp"
#include "gtest/gtest/gtest.h"

#include <chrono>
#include <fstream>
#include <string>
#include <system_error>

TEST(test_filesystem_compat, exposes_std_filesystem_alias_for_real_file_operations)
{
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const wt::fs::path path = wt::fs::temp_directory_path() /
		("wondertrader_filesystem_compat_test_" + std::to_string(suffix) + ".txt");

	std::error_code ec;
	wt::fs::remove(path, ec);

	{
		std::ofstream output(path.string(), std::ios::binary);
		ASSERT_TRUE(output.is_open());
		output << "filesystem compat";
	}

	EXPECT_TRUE(wt::fs::exists(path));
	EXPECT_EQ(17u, wt::fs::file_size(path));

	wt::fs::remove(path, ec);
	EXPECT_FALSE(wt::fs::exists(path));
}
