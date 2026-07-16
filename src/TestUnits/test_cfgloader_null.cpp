#include "gtest/gtest.h"

#include <memory>

#include "../Includes/WTSVariant.hpp"
#include "../WTSUtils/WTSCfgLoader.h"

using wtp::WTSVariant;

namespace
{
struct VariantReleaser
{
	void operator()(WTSVariant* value) const
	{
		if (value != nullptr)
			value->release();
	}
};

using VariantPtr = std::unique_ptr<WTSVariant, VariantReleaser>;

void expectNullsAreIgnored(WTSVariant* root)
{
	ASSERT_NE(nullptr, root);
	EXPECT_EQ(3U, root->size());
	EXPECT_EQ(nullptr, root->get("drop"));
	EXPECT_EQ("one", root->getString("keep"));

	WTSVariant* array = root->get("array");
	ASSERT_NE(nullptr, array);
	ASSERT_EQ(2U, array->size());
	EXPECT_EQ("one", array->get(uint32_t{0})->asString());
	EXPECT_EQ("two", array->get(uint32_t{1})->asString());

	WTSVariant* object = root->get("object");
	ASSERT_NE(nullptr, object);
	EXPECT_EQ(1U, object->size());
	EXPECT_EQ(nullptr, object->get("drop"));
	EXPECT_EQ("value", object->getString("keep"));
}
}

TEST(WTSCfgLoaderNull, JsonNullFieldsAndElementsAreIgnored)
{
	VariantPtr root(WTSCfgLoader::load_from_content(
		R"({"keep":"one","drop":null,"array":["one",null,"two"],"object":{"drop":null,"keep":"value"}})"));
	expectNullsAreIgnored(root.get());
}

TEST(WTSCfgLoaderNull, YamlNullFieldsAndElementsAreIgnored)
{
	VariantPtr root(WTSCfgLoader::load_from_content(
		"keep: one\n"
		"drop: null\n"
		"array:\n"
		"  - one\n"
		"  - null\n"
		"  - two\n"
		"object:\n"
		"  drop: ~\n"
		"  keep: value\n",
		true));
	expectNullsAreIgnored(root.get());
}

TEST(WTSCfgLoaderNull, NonObjectRootsAreRejected)
{
	EXPECT_EQ(nullptr, WTSCfgLoader::load_from_content("null"));
	EXPECT_EQ(nullptr, WTSCfgLoader::load_from_content("123"));
	EXPECT_EQ(nullptr, WTSCfgLoader::load_from_content("[]"));
	EXPECT_EQ(nullptr, WTSCfgLoader::load_from_content("null", true));
	EXPECT_EQ(nullptr, WTSCfgLoader::load_from_content("scalar", true));
	EXPECT_EQ(nullptr, WTSCfgLoader::load_from_content("[]", true));
}
