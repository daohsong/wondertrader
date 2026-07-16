#include "../WtCore/WtFilterMgr.h"
#include "../Share/FilesystemCompat.hpp"
#include "gtest/gtest/gtest.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>
#include <sys/types.h>
#include <sys/stat.h>
#include <utility>

namespace
{
void writeStrategyFilter(const wt::fs::path& path, const char* action, double target)
{
	std::ofstream output(path.string(), std::ios::binary | std::ios::trunc);
	ASSERT_TRUE(output.is_open());
	output << "{"
		<< "\"strategy_filters\":{"
		<< "\"alpha\":{"
		<< "\"action\":\"" << action << "\","
		<< "\"target\":" << target
		<< "}"
		<< "}"
		<< "}";
}

wt::fs::file_time_type fileTimeFromSystemTime(std::chrono::system_clock::time_point systemTime)
{
	return wt::fs::file_time_type::clock::now() +
		std::chrono::duration_cast<wt::fs::file_time_type::duration>(
			systemTime - std::chrono::system_clock::now());
}

std::time_t readLastWriteTimeSeconds(const wt::fs::path& path)
{
#ifdef _WIN32
	struct __stat64 statBuf;
	if (_stat64(path.string().c_str(), &statBuf) != 0)
		return static_cast<std::time_t>(-1);
#else
	struct stat statBuf;
	if (::stat(path.string().c_str(), &statBuf) != 0)
		return static_cast<std::time_t>(-1);
#endif
	return static_cast<std::time_t>(statBuf.st_mtime);
}

class ScopedFile
{
public:
	explicit ScopedFile(wt::fs::path path) : _path(std::move(path)) {}
	~ScopedFile()
	{
		std::error_code ec;
		wt::fs::remove(_path, ec);
	}

	const wt::fs::path& path() const { return _path; }

private:
	wt::fs::path _path;
};
}

TEST(test_header_self_contained_wtfiltermgr, compiles_when_included_first)
{
	SUCCEED();
}

TEST(test_wtfiltermgr_reload, preserves_second_granularity_for_filter_file_timestamp)
{
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	ScopedFile filterFile(wt::fs::temp_directory_path() /
		("wondertrader_wtfiltermgr_reload_" + std::to_string(suffix) + ".json"));

	const auto baseSeconds = std::chrono::system_clock::to_time_t(
		std::chrono::system_clock::now()) - 60;
	const auto baseTime = std::chrono::system_clock::from_time_t(baseSeconds);

	writeStrategyFilter(filterFile.path(), "ignore", 0.0);
	wt::fs::last_write_time(
		filterFile.path(), fileTimeFromSystemTime(baseTime + std::chrono::milliseconds(100)));
	const std::time_t firstMtime = readLastWriteTimeSeconds(filterFile.path());
	ASSERT_NE(static_cast<std::time_t>(-1), firstMtime);

	wtp::WtFilterMgr filterMgr;
	filterMgr.load_filters(filterFile.path().string().c_str());

	double target = 7.0;
	EXPECT_TRUE(filterMgr.is_filtered_by_strategy("alpha", target));
	EXPECT_DOUBLE_EQ(7.0, target);

	writeStrategyFilter(filterFile.path(), "redirect", 3.5);
	wt::fs::last_write_time(
		filterFile.path(), fileTimeFromSystemTime(baseTime + std::chrono::milliseconds(500)));
	const std::time_t secondMtime = readLastWriteTimeSeconds(filterFile.path());
	ASSERT_EQ(firstMtime, secondMtime);
	filterMgr.load_filters();

	target = 7.0;
	EXPECT_TRUE(filterMgr.is_filtered_by_strategy("alpha", target));
	EXPECT_DOUBLE_EQ(7.0, target);

	wt::fs::last_write_time(
		filterFile.path(), fileTimeFromSystemTime(baseTime + std::chrono::milliseconds(5000)));
	const std::time_t thirdMtime = readLastWriteTimeSeconds(filterFile.path());
	ASSERT_GT(thirdMtime, secondMtime);
	filterMgr.load_filters();

	target = 7.0;
	EXPECT_FALSE(filterMgr.is_filtered_by_strategy("alpha", target));
	EXPECT_DOUBLE_EQ(3.5, target);
}
