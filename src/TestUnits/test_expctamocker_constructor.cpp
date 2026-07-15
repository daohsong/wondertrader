#include "gtest/gtest.h"

#include <type_traits>

#include "../WtBtPorter/ExpCtaMocker.h"

TEST(ExpCtaMockerConstructor, RejectsNotifierInPersistDataPosition)
{
	EXPECT_FALSE((std::is_constructible<ExpCtaMocker,
		HisDataReplayer*, const char*, int32_t, EventNotifier*>::value));
}

TEST(ExpCtaMockerConstructor, AcceptsCompleteTypedArguments)
{
	EXPECT_TRUE((std::is_constructible<ExpCtaMocker,
		HisDataReplayer*, const char*, int32_t, bool, EventNotifier*, bool>::value));
}
