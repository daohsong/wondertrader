#include "../Share/WtKVCache.hpp"
#include "../Share/IniHelper.hpp"
#include "../Share/TimeUtils.hpp"
#include "../Share/fmtlib.h"
#include "gtest/gtest/gtest.h"

#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

USING_NS_WTP;

namespace
{
class ScopedCacheFile
{
public:
	explicit ScopedCacheFile(const char* testName, const char* extension = ".dat")
	{
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		_path = (std::filesystem::current_path() /
			("wt_kvcache_" + std::string(testName) + "_" + std::to_string(stamp) + extension)).string();
		remove();
	}

	~ScopedCacheFile()
	{
		remove();
	}

	const char* c_str() const
	{
		return _path.c_str();
	}

private:
	void remove() const
	{
		std::error_code ec;
		std::filesystem::remove(_path, ec);
	}

	std::string _path;
};

std::string make_reopen_key(const char* prefix, uint32_t index)
{
	char key[32] = { 0 };
	std::snprintf(key, sizeof(key), "%s_key_%03u", prefix, index);
	return key;
}

std::string make_reopen_value(const char* prefix, uint32_t index)
{
	char value[32] = { 0 };
	std::snprintf(value, sizeof(value), "%s_val_%03u", prefix, index);
	return value;
}

uint64_t checksum_append(uint64_t checksum, const char* text)
{
	constexpr uint64_t prime = 1099511628211ull;
	while (*text != '\0')
	{
		checksum ^= static_cast<unsigned char>(*text);
		checksum *= prime;
		++text;
	}
	return checksum;
}

uint64_t expected_reopen_checksum(uint32_t oldCount, uint32_t newCount)
{
	uint64_t checksum = 1469598103934665603ull;
	for (uint32_t i = 0; i < oldCount; ++i)
		checksum = checksum_append(checksum, make_reopen_value("old", i).c_str());
	for (uint32_t i = 0; i < newCount; ++i)
		checksum = checksum_append(checksum, make_reopen_value("new", i).c_str());
	return checksum;
}

bool cache_value_matches(WtKVCache& cache, const std::string& key, const std::string& expected)
{
	return std::strcmp(expected.c_str(), cache.get(key.c_str())) == 0;
}

#if !defined(_WIN32)
bool child_write_old_keys(const char* filename, uint32_t date, uint32_t oldCount, uint32_t)
{
	WtKVCache cache;
	if (!cache.init(filename, date))
		return false;

	for (uint32_t i = 0; i < oldCount; ++i)
	{
		const std::string key = make_reopen_key("old", i);
		const std::string value = make_reopen_value("old", i);
		cache.put(key.c_str(), value.c_str());
	}

	return cache.size() == oldCount &&
		cache.capacity() == SIZE_STEP * 2 &&
		cache_value_matches(cache, make_reopen_key("old", 0), make_reopen_value("old", 0)) &&
		cache_value_matches(cache, make_reopen_key("old", oldCount - 1), make_reopen_value("old", oldCount - 1));
}

bool child_verify_old_and_append_new(const char* filename, uint32_t date, uint32_t oldCount, uint32_t newCount)
{
	WtKVCache cache;
	if (!cache.init(filename, date))
		return false;

	if (cache.size() != oldCount || cache.capacity() != SIZE_STEP * 2)
		return false;

	for (uint32_t i = 0; i < oldCount; ++i)
	{
		if (!cache_value_matches(cache, make_reopen_key("old", i), make_reopen_value("old", i)))
			return false;
	}

	for (uint32_t i = 0; i < newCount; ++i)
	{
		const std::string key = make_reopen_key("new", i);
		const std::string value = make_reopen_value("new", i);
		cache.put(key.c_str(), value.c_str());
	}

	return cache.size() == oldCount + newCount &&
		cache.capacity() == SIZE_STEP * 2 &&
		cache_value_matches(cache, make_reopen_key("new", newCount - 1), make_reopen_value("new", newCount - 1));
}

bool child_verify_all_reopened_keys(const char* filename, uint32_t date, uint32_t oldCount, uint32_t newCount)
{
	WtKVCache cache;
	if (!cache.init(filename, date))
		return false;

	if (cache.size() != oldCount + newCount || cache.capacity() != SIZE_STEP * 2)
		return false;

	uint64_t checksum = 1469598103934665603ull;
	for (uint32_t i = 0; i < oldCount; ++i)
	{
		const std::string value = make_reopen_value("old", i);
		const char* actual = cache.get(make_reopen_key("old", i).c_str());
		if (std::strcmp(value.c_str(), actual) != 0)
			return false;
		checksum = checksum_append(checksum, actual);
	}

	for (uint32_t i = 0; i < newCount; ++i)
	{
		const std::string value = make_reopen_value("new", i);
		const char* actual = cache.get(make_reopen_key("new", i).c_str());
		if (std::strcmp(value.c_str(), actual) != 0)
			return false;
		checksum = checksum_append(checksum, actual);
	}

	return checksum == expected_reopen_checksum(oldCount, newCount);
}

using ReopenChildFunc = bool (*)(const char*, uint32_t, uint32_t, uint32_t);

void expect_reopen_child_exit_zero(const char* phase, ReopenChildFunc func, const char* filename,
	uint32_t date, uint32_t oldCount, uint32_t newCount)
{
	const pid_t pid = fork();
	ASSERT_GE(pid, 0) << phase << " fork failed: " << std::strerror(errno);

	if (pid == 0)
		_exit(func(filename, date, oldCount, newCount) ? 0 : 1);

	int status = 0;
	ASSERT_EQ(pid, waitpid(pid, &status, 0)) << phase << " waitpid failed: " << std::strerror(errno);
	ASSERT_TRUE(WIFEXITED(status)) << phase << " did not exit normally";
	ASSERT_EQ(0, WEXITSTATUS(status)) << phase << " exited with failure";
}
#endif
}

TEST(test_kvcache, put_get_reopen_after_resize)
{
	ScopedCacheFile file("put_get_reopen_after_resize");
	const uint32_t total = SIZE_STEP + 25;

	{
		WtKVCache cache;
		ASSERT_TRUE(cache.init(file.c_str(), 20260706));

		for (uint32_t i = 0; i < total; ++i)
		{
			char key[32] = { 0 };
			char val[32] = { 0 };
			std::snprintf(key, sizeof(key), "key_%03u", i);
			std::snprintf(val, sizeof(val), "val_%03u", i);
			cache.put(key, val);
		}

		EXPECT_EQ(total, cache.size());
		EXPECT_GT(cache.capacity(), SIZE_STEP);
		EXPECT_STREQ("val_000", cache.get("key_000"));
		EXPECT_STREQ("val_224", cache.get("key_224"));
	}

	WtKVCache reopened;
	ASSERT_TRUE(reopened.init(file.c_str(), 20260706));
	EXPECT_EQ(total, reopened.size());
	EXPECT_GT(reopened.capacity(), SIZE_STEP);
	EXPECT_STREQ("val_000", reopened.get("key_000"));
	EXPECT_STREQ("val_224", reopened.get("key_224"));
}

TEST(test_kvcache, sequential_cross_process_reopen_preserves_and_appends)
{
#if defined(_WIN32)
	SUCCEED() << "fork-based cross-process reopen coverage is only enabled on Unix-like platforms";
#else
	ScopedCacheFile file("sequential_cross_process_reopen_preserves_and_appends");
	const uint32_t date = 20260706;
	const uint32_t oldCount = SIZE_STEP + 25;
	const uint32_t newCount = 37;

	expect_reopen_child_exit_zero("A", child_write_old_keys, file.c_str(), date, oldCount, newCount);
	expect_reopen_child_exit_zero("B", child_verify_old_and_append_new, file.c_str(), date, oldCount, newCount);
	expect_reopen_child_exit_zero("C", child_verify_all_reopened_keys, file.c_str(), date, oldCount, newCount);
#endif
}

TEST(test_kvcache, put_if_none_first_write_wins)
{
	ScopedCacheFile file("put_if_none_first_write_wins");
	WtKVCache cache;
	ASSERT_TRUE(cache.init(file.c_str(), 20260706));

	cache.put_if_none("alpha", "first");
	cache.put_if_none("alpha", "second");

	EXPECT_EQ(1u, cache.size());
	EXPECT_STREQ("first", cache.get("alpha"));
}

TEST(test_kvcache, put_existing_key_updates_value_size_unchanged)
{
	ScopedCacheFile file("put_existing_key_updates_value_size_unchanged");
	WtKVCache cache;
	ASSERT_TRUE(cache.init(file.c_str(), 20260706));

	cache.put("alpha", "first");
	const uint32_t sizeAfterInsert = cache.size();
	cache.put("alpha", "second");

	EXPECT_EQ(sizeAfterInsert, cache.size());
	EXPECT_STREQ("second", cache.get("alpha"));
}

TEST(test_kvcache, date_change_resets_cache)
{
	ScopedCacheFile file("date_change_resets_cache");

	{
		WtKVCache cache;
		ASSERT_TRUE(cache.init(file.c_str(), 20260706));
		cache.put("alpha", "first");
		ASSERT_EQ(1u, cache.size());
	}

	WtKVCache reopened;
	ASSERT_TRUE(reopened.init(file.c_str(), 20260707));
	EXPECT_EQ(0u, reopened.size());
	EXPECT_FALSE(reopened.has("alpha"));
	EXPECT_STREQ("", reopened.get("alpha"));
}

TEST(test_kvcache, same_object_reinit_date_change_clears_indice)
{
	ScopedCacheFile file("same_object_reinit_date_change_clears_indice");
	WtKVCache cache;
	ASSERT_TRUE(cache.init(file.c_str(), 20260706));
	cache.put("alpha", "first");
	ASSERT_TRUE(cache.has("alpha"));
	ASSERT_EQ(1u, cache.size());

	ASSERT_TRUE(cache.init(file.c_str(), 20260707));
	EXPECT_EQ(0u, cache.size());
	EXPECT_FALSE(cache.has("alpha"));
	EXPECT_STREQ("", cache.get("alpha"));
}

TEST(test_kvcache, reopen_expanded_file_preserves_marked_size)
{
	ScopedCacheFile file("reopen_expanded_file_preserves_marked_size");
	{
		WtKVCache cache;
		ASSERT_TRUE(cache.init(file.c_str(), 20260706));

		for (uint32_t i = 0; i < SIZE_STEP; ++i)
		{
			char key[32] = { 0 };
			char val[32] = { 0 };
			std::snprintf(key, sizeof(key), "key_%03u", i);
			std::snprintf(val, sizeof(val), "val_%03u", i);
			cache.put(key, val);
		}

		ASSERT_EQ(SIZE_STEP, cache.size());
		ASSERT_EQ(SIZE_STEP, cache.capacity());
	}

	const uint64_t headerSize = FLAG_SIZE + sizeof(uint32_t) * 3;
	const uint64_t oldSize = std::filesystem::file_size(file.c_str());
	const uint64_t expandedSize = headerSize + (oldSize - headerSize) * 2;
	BoostFile bf;
	ASSERT_TRUE(bf.open_existing_file(file.c_str()));
	ASSERT_TRUE(bf.truncate_file((std::size_t)expandedSize));
	bf.close_file();

	WtKVCache reopened;
	ASSERT_TRUE(reopened.init(file.c_str(), 20260706));
	EXPECT_EQ(SIZE_STEP, reopened.size());
	EXPECT_EQ(SIZE_STEP * 2, reopened.capacity());
	EXPECT_STREQ("val_000", reopened.get("key_000"));
	EXPECT_STREQ("val_199", reopened.get("key_199"));
}

TEST(test_kvcache, concurrent_insert_unique_keys)
{
	ScopedCacheFile file("concurrent_insert_unique_keys");
	WtKVCache cache;
	ASSERT_TRUE(cache.init(file.c_str(), 20260706));

	const uint32_t threadCount = 8;
	const uint32_t keysPerThread = 96;
	const uint32_t total = threadCount * keysPerThread;
	std::vector<std::thread> threads;
	threads.reserve(threadCount);

	for (uint32_t t = 0; t < threadCount; ++t)
	{
		threads.emplace_back([&, t]() {
			for (uint32_t i = 0; i < keysPerThread; ++i)
			{
				char key[32] = { 0 };
				char val[32] = { 0 };
				std::snprintf(key, sizeof(key), "t%02u_k%03u", t, i);
				std::snprintf(val, sizeof(val), "v%02u_%03u", t, i);
				cache.put(key, val);
			}
		});
	}

	for (auto& thread : threads)
		thread.join();

	EXPECT_EQ(total, cache.size());
	for (uint32_t t = 0; t < threadCount; ++t)
	{
		for (uint32_t i = 0; i < keysPerThread; ++i)
		{
			char key[32] = { 0 };
			char val[32] = { 0 };
			std::snprintf(key, sizeof(key), "t%02u_k%03u", t, i);
			std::snprintf(val, sizeof(val), "v%02u_%03u", t, i);
			EXPECT_TRUE(cache.has(key)) << key;
			EXPECT_STREQ(val, cache.get(key)) << key;
		}
	}
}

TEST(test_kvcache, concurrent_get_put_resize_same_object)
{
	ScopedCacheFile file("concurrent_get_put_resize_same_object");
	WtKVCache cache;
	ASSERT_TRUE(cache.init(file.c_str(), 20260706));
	cache.put("stable", "value");

	std::mutex stateMutex;
	std::condition_variable stateCondition;
	bool writerStarted = false;
	bool writerDone = false;
	bool readerSawBadValue = false;

	std::thread reader([&]() {
		{
			std::unique_lock<std::mutex> lock(stateMutex);
			stateCondition.wait(lock, [&writerStarted]() { return writerStarted; });
		}

		for (;;)
		{
			const char* value = cache.get("stable");
			if (strcmp(value, "value") != 0)
				readerSawBadValue = true;

			std::unique_lock<std::mutex> lock(stateMutex);
			if (writerDone)
				break;
		}
	});

	std::thread writer([&]() {
		{
			std::lock_guard<std::mutex> lock(stateMutex);
			writerStarted = true;
		}
		stateCondition.notify_one();

		for (uint32_t i = 0; i < SIZE_STEP * 3; ++i)
		{
			char key[32] = { 0 };
			char val[32] = { 0 };
			std::snprintf(key, sizeof(key), "resize_key_%03u", i);
			std::snprintf(val, sizeof(val), "resize_val_%03u", i);
			cache.put(key, val);
		}

		{
			std::lock_guard<std::mutex> lock(stateMutex);
			writerDone = true;
		}
	});

	writer.join();
	reader.join();

	EXPECT_FALSE(readerSawBadValue);
	EXPECT_STREQ("value", cache.get("stable"));
	EXPECT_EQ(SIZE_STEP * 3 + 1, cache.size());
	EXPECT_GT(cache.capacity(), SIZE_STEP);
}

TEST(test_kvcache, test_perform)
{
	ScopedCacheFile cacheFile("perform_cache");
	ScopedCacheFile iniFile("perform_ini", ".ini");
	WtKVCache cache;
	cache.init(cacheFile.c_str(), 20220325, [](const char* msg) {
		printf("%s\n", msg);
	});
	cache.clear();

	IniHelper ini;
	ini.load(iniFile.c_str());
	ini.removeSection("test");
	ini.save();

	uint32_t times = 10000;

	TimeUtils::Ticker ticker;
	char buffer[16] = { 0 };

	//œ»≤‚ ‘simplecache
	for(uint32_t i = 0; i < times; i++)
	{
		char* s = fmt::format_to(buffer, "{}", i);
		s[0] = '\0';
		cache.put(buffer, buffer);
	}

	for (uint32_t i = 0; i < times; i++)
	{
		char* s = fmt::format_to(buffer, "{}", i);
		s[0] = '\0';
		cache.get(buffer);
	}
	uint64_t a = ticker.nano_seconds();

	//‘Ÿ≤‚ ‘ini
	ticker.reset();
	for (uint32_t i = 0; i < times; i++)
	{
		char* s = fmt::format_to(buffer, "{}", i);
		s[0] = '\0';
		ini.writeString("test", buffer, buffer);
		//ini.save();
	}

	for (uint32_t i = 0; i < times; i++)
	{
		char* s = fmt::format_to(buffer, "{}", i);
		s[0] = '\0';
		ini.readString("test", buffer);
	}
	uint64_t b = ticker.nano_seconds();

	fmt::print("cache: {} - ini: {}\n", a, b);
}
