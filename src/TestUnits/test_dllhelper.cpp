#include "../Share/DLLHelper.hpp"
#include "gtest/gtest/gtest.h"

TEST(test_dllhelper, wrap_module_uses_platform_library_suffix)
{
#ifdef _WIN32
	EXPECT_EQ(DLLHelper::wrap_module("WtPorter"), "WtPorter.dll");
#elif defined(__APPLE__)
	EXPECT_EQ(DLLHelper::wrap_module("WtPorter"), "libWtPorter.dylib");
#else
	EXPECT_EQ(DLLHelper::wrap_module("WtPorter"), "libWtPorter.so");
#endif
}
