#include "../WTSUtils/WtLMDB.hpp"
#include "../Share/StrUtil.hpp"
#include "../WtDataStorageAD/LMDBKeys.h"
#include "../Share/fmtlib.h"
#include "gtest/gtest/gtest.h"

#include <cstring>

USING_NS_WTP;

TEST(test_lmdb, test_constructor)
{
	WtLMDB db;
	ASSERT_TRUE(db.open("./testdb"));
}

TEST(test_lmdb, test_query)
{
	{	
		//–¥≤È—Ø
		WtLMDB db(false);
		ASSERT_TRUE(db.open("./testdb"));

		WtLMDBQuery query(db);
		EXPECT_FALSE(db.has_error());
		for(int i = 0; i < 100; i++)
		{
			std::string key = fmtutil::format<64>("key-{:03d}", i + 1);
			std::string val = fmtutil::format<64>("value-{:03d}", i + 1);
			EXPECT_TRUE(query.put(key, val));
			if (db.has_error())
				printf("%s\n", db.errmsg());
		}
		
	}

	{
		//∂¡≤È—Ø
		WtLMDB db(true);
		ASSERT_TRUE(db.open("./testdb"));

		WtLMDBQuery query(db);
		EXPECT_FALSE(db.has_error());
		EXPECT_FALSE(query.put(std::string("key1"), std::string("value1")));

		EXPECT_EQ(query.get(std::string("key-001")), "value-001");

		query.get_lowers(std::string("key-000"), std::string("key-030"), 10, [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 10);
		});

		query.get_lowers(std::string("key-000"), std::string("key-005"), 10, [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 5);
		});

		query.get_uppers(std::string("key-030"), std::string("key-999"), 10, [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 10);
		});

		query.get_uppers(std::string("key-095"), std::string("key-999"), 10, [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 6);
		});

		query.get_range(std::string("key-030"), std::string("key-050"), [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 21);
		});
	}
}

TEST(test_lmdb, fixed_keys_preserve_layout_and_bound_text_fields)
{
	static_assert(sizeof(LMDBHftKey) == MAX_EXCHANGE_LENGTH + MAX_INSTRUMENT_LENGTH + sizeof(uint32_t) * 2);
	static_assert(sizeof(LMDBBarKey) == MAX_EXCHANGE_LENGTH + MAX_INSTRUMENT_LENGTH + sizeof(uint32_t));

	const std::string longExchange(MAX_EXCHANGE_LENGTH + 8, 'E');
	const std::string longCode(MAX_INSTRUMENT_LENGTH + 8, 'C');
	LMDBHftKey hftKey(longExchange.c_str(), longCode.c_str(), 20260716, 93000000);
	EXPECT_EQ(std::strlen(hftKey._exchg), MAX_EXCHANGE_LENGTH - 1U);
	EXPECT_EQ(std::strlen(hftKey._code), MAX_INSTRUMENT_LENGTH - 1U);
	EXPECT_EQ(hftKey._date, reverseEndian(uint32_t{20260716}));
	EXPECT_EQ(hftKey._time, reverseEndian(uint32_t{93000000}));

	LMDBBarKey barKey(longExchange.c_str(), longCode.c_str(), 2026071615);
	EXPECT_EQ(std::strlen(barKey._exchg), MAX_EXCHANGE_LENGTH - 1U);
	EXPECT_EQ(std::strlen(barKey._code), MAX_INSTRUMENT_LENGTH - 1U);
	EXPECT_EQ(barKey._bartime, reverseEndian(uint32_t{2026071615}));
}

static uint32_t testReverseEndian(uint32_t src)
{
	uint32_t x = (src & 0x000000FF) << 24;
	uint32_t y = (src & 0x0000FF00) << 8;
	uint32_t z = (src & 0x00FF0000) >> 8;
	uint32_t w = (src & 0xFF000000) >> 24;
	return x + y + z + w;
}

std::string makeData(uint32_t v, bool trans)
{
	if (trans)
		v = testReverseEndian(v);

	return std::move(std::string((const char*)&v, sizeof(uint32_t)));
}

TEST(test_lmdb, test_endian)
{
	{
		//–¥≤È—Ø
		WtLMDB db(false);
		ASSERT_TRUE(db.open("./endiandb"));

		WtLMDBQuery query(db);
		EXPECT_FALSE(db.has_error());
		for (int i = 0; i < 100; i++)
		{
			uint32_t d = 20220101 + i;
			std::string key = makeData(d, true);
			std::string val = makeData(d, false);
			EXPECT_TRUE(query.put(key, val));
			if (db.has_error())
				printf("%s\n", db.errmsg());
		}

	}

	{
		//∂¡≤È—Ø
		WtLMDB db(true);
		ASSERT_TRUE(db.open("./endiandb"));

		WtLMDBQuery query(db);
		EXPECT_FALSE(db.has_error());
		EXPECT_FALSE(query.put(std::string("key1"), std::string("value1")));

		uint32_t d = 20220101;
		std::string v = query.get(makeData(d, true));
		ASSERT_EQ(sizeof(uint32_t), v.size());
		uint32_t iv = 0;
		std::memcpy(&iv, v.data(), sizeof(iv));
		EXPECT_EQ(iv, d);

		printf("Testing getting enough lower data:\r\n");
		query.get_lowers(makeData(0, true), makeData(20220120, true), 10, [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 10);
			for(const std::string& item : ayVals)
			{
				uint32_t iv = *((uint32_t*)item.data());
				printf("%u\r\n", iv);
			}
		});

		printf("\r\nTesting getting no enough lower data:\r\n");
		query.get_lowers(makeData(0, true ), makeData(20220105, true), 10, [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 5);
			for (const std::string& item : ayVals)
			{
				uint32_t iv = *((uint32_t*)item.data());
				printf("%u\r\n", iv);
			}
		});

		printf("\r\nTesting getting enough upper data:\r\n");
		query.get_uppers(makeData(20220150, true), makeData(20229999, true), 10, [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 10);
			for (const std::string& item : ayVals)
			{
				uint32_t iv = *((uint32_t*)item.data());
				printf("%u\r\n", iv);
			}
		});

		printf("\r\nTesting getting no enough upper data:\r\n");
		query.get_uppers(makeData(20220196, true), makeData(20229999, true), 10, [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 5);
			for (const std::string& item : ayVals)
			{
				uint32_t iv = *((uint32_t*)item.data());
				printf("%u\r\n", iv);
			}
		});

		printf("\r\nTesting getting range data:\r\n");
		query.get_range(makeData(20220130, true), makeData(20220150, true), [](const ValueArray& ayKeys, const ValueArray& ayVals) {
			EXPECT_EQ(ayKeys.size(), 21);
			for (const std::string& item : ayVals)
			{
				uint32_t iv = *((uint32_t*)item.data());
				printf("%u\r\n", iv);
			}
		});
	}
}
