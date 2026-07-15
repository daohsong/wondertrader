#include "gtest/gtest.h"

#include <array>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../Share/IniHelper.hpp"

TEST(IniHelper, RoundTripsAllSupportedValueTypes)
{
	IniHelper ini;
	ini.writeString("types", "string", "value");
	ini.writeInt("types", "int", -42);
	ini.writeUInt("types", "uint", 42U);
	ini.writeBool("types", "bool", true);
	ini.writeDouble("types", "double", 3.25);

	EXPECT_EQ(ini.readString("types", "string", "missing"), "value");
	EXPECT_EQ(ini.readInt("types", "int", 0), -42);
	EXPECT_EQ(ini.readUInt("types", "uint", 0), 42U);
	EXPECT_TRUE(ini.readBool("types", "bool", false));
	EXPECT_DOUBLE_EQ(ini.readDouble("types", "double", 0.0), 3.25);
}

TEST(IniHelper, LongPathsRemainDistinctAndPersist)
{
	const std::string section(48, 'S');
	const std::string sharedPrefix(80, 'K');
	const std::string firstKey = sharedPrefix + "-first";
	const std::string secondKey = sharedPrefix + "-second";
	const std::string filename = "/tmp/wt_inihelper_long_paths.ini";

	{
		IniHelper ini;
		ini.writeString(section.c_str(), firstKey.c_str(), "first-value");
		ini.writeString(section.c_str(), secondKey.c_str(), "second-value");
		EXPECT_EQ(ini.readString(section.c_str(), firstKey.c_str(), "missing"), "first-value");
		EXPECT_EQ(ini.readString(section.c_str(), secondKey.c_str(), "missing"), "second-value");
		ini.save(filename.c_str());
	}

	IniHelper loaded;
	loaded.load(filename.c_str());
	EXPECT_EQ(loaded.readString(section.c_str(), firstKey.c_str(), "missing"), "first-value");
	EXPECT_EQ(loaded.readString(section.c_str(), secondKey.c_str(), "missing"), "second-value");
	std::remove(filename.c_str());
}

TEST(IniHelper, MissingOrInvalidPathsReturnDefaults)
{
	IniHelper ini;
	EXPECT_EQ(ini.readString("missing", "value", "default"), "default");
	EXPECT_EQ(ini.readInt("", "", 17), 17);
	EXPECT_EQ(ini.readUInt(nullptr, "key", 23U), 23U);
	EXPECT_TRUE(ini.readBool("section", nullptr, true));
	EXPECT_DOUBLE_EQ(ini.readDouble(nullptr, nullptr, 1.5), 1.5);

	ini.writeString(nullptr, "key", "ignored");
	ini.writeInt("section", nullptr, 42);
	EXPECT_EQ(ini.readString("", "key", "default"), "default");
	EXPECT_EQ(ini.readInt("section", "", 17), 17);
}

TEST(IniHelper, SeparateInstancesDoNotSharePathStorage)
{
	constexpr size_t threadCount = 8;
	std::array<bool, threadCount> succeeded = {};
	std::vector<std::thread> threads;
	threads.reserve(threadCount);

	for (size_t index = 0; index < threadCount; ++index)
	{
		threads.emplace_back([index, &succeeded]() {
			IniHelper ini;
			const std::string section = std::string(80, static_cast<char>('A' + index));
			const std::string key = std::string(80, static_cast<char>('a' + index));
			const std::string value = std::to_string(index);
			for (size_t iteration = 0; iteration < 1000; ++iteration)
			{
				ini.writeString(section.c_str(), key.c_str(), value.c_str());
				if (ini.readString(section.c_str(), key.c_str(), "missing") != value)
					return;
			}
			succeeded[index] = true;
		});
	}

	for (std::thread& thread : threads)
		thread.join();
	for (bool result : succeeded)
		EXPECT_TRUE(result);
}
