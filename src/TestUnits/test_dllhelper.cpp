#include "../Share/DLLHelper.hpp"
#include "../Share/ModuleNameCompat.hpp"
#include "gtest/gtest/gtest.h"

#include <string>

#ifndef WT_DLLHELPER_FIXTURE_PATH
#define WT_DLLHELPER_FIXTURE_PATH ""
#endif

TEST(test_dllhelper, wrap_module_uses_platform_library_suffix)
{
#ifdef _WIN32
	EXPECT_EQ(DLLHelper::wrap_module("foo"), "foo.dll");
#elif defined(__APPLE__)
	EXPECT_EQ(DLLHelper::wrap_module("foo"), "libfoo.dylib");
#else
	EXPECT_EQ(DLLHelper::wrap_module("foo"), "libfoo.so");
#endif
}

TEST(test_dllhelper, wrap_module_does_not_duplicate_existing_prefix_or_suffix)
{
	EXPECT_EQ(DLLHelper::wrap_module("libfoo.so"), "libfoo.so");
	EXPECT_EQ(DLLHelper::wrap_module("foo.dylib"), "foo.dylib");
#ifdef _WIN32
	EXPECT_EQ(DLLHelper::wrap_module("foo.DLL"), "foo.DLL");
#endif
}

TEST(test_dllhelper, wrap_module_preserves_directories)
{
#ifdef _WIN32
	EXPECT_EQ(DLLHelper::wrap_module("./foo"), "./foo.dll");
#elif defined(__APPLE__)
	EXPECT_EQ(DLLHelper::wrap_module("./foo"), "./libfoo.dylib");
#else
	EXPECT_EQ(DLLHelper::wrap_module("./foo"), "./libfoo.so");
#endif

	EXPECT_EQ(DLLHelper::wrap_module("/opt/x/libfoo.so"), "/opt/x/libfoo.so");
}

TEST(test_dllhelper, wrap_module_honors_empty_unix_prefix)
{
#ifdef _WIN32
	EXPECT_EQ(DLLHelper::wrap_module("foo", ""), "foo.dll");
#elif defined(__APPLE__)
	EXPECT_EQ(DLLHelper::wrap_module("foo", ""), "foo.dylib");
#else
	EXPECT_EQ(DLLHelper::wrap_module("foo", ""), "foo.so");
#endif
}

TEST(test_dllhelper, get_typed_symbol_keeps_legacy_get_symbol_api_available)
{
	EXPECT_EQ(DLLHelper::get_symbol(NULL, "missing"), static_cast<ProcHandle>(NULL));
	EXPECT_FALSE(DLLHelper::last_error().empty());
	typedef void (*Fn)();
	EXPECT_EQ(DLLHelper::get_typed_symbol<Fn>(NULL, "missing"), static_cast<Fn>(NULL));
}

TEST(test_dllhelper, get_typed_symbol_returns_callable_symbol)
{
	const std::string fixturePath = WT_DLLHELPER_FIXTURE_PATH;
	ASSERT_FALSE(fixturePath.empty());

	DllHandle handle = DLLHelper::load_library(fixturePath.c_str());
	ASSERT_NE(handle, static_cast<DllHandle>(NULL)) << DLLHelper::last_error();

	typedef int (*FixtureValueFn)();
	FixtureValueFn func = DLLHelper::get_typed_symbol<FixtureValueFn>(handle, "wt_dllhelper_fixture_value");
	ASSERT_NE(func, static_cast<FixtureValueFn>(NULL)) << DLLHelper::last_error();
	EXPECT_EQ(func(), 20260707);

	DLLHelper::free_library(handle);
}

TEST(test_dllhelper, wrap_module_handles_empty_and_no_letter_names)
{
	EXPECT_EQ(DLLHelper::wrap_module(""), "");
#ifdef _WIN32
	EXPECT_EQ(DLLHelper::wrap_module("123"), "123.dll");
#elif defined(__APPLE__)
	EXPECT_EQ(DLLHelper::wrap_module("123"), "lib123.dylib");
#else
	EXPECT_EQ(DLLHelper::wrap_module("123"), "lib123.so");
#endif
}

TEST(test_dllhelper, module_basename_handles_windows_unix_and_plain_names)
{
	EXPECT_EQ(ModuleNameCompat::module_basename("C:\\wt\\bin\\WtPorter.dll"), "WtPorter.dll");
	EXPECT_EQ(ModuleNameCompat::module_basename("/opt/wt/libWtPorter.so"), "libWtPorter.so");
	EXPECT_EQ(ModuleNameCompat::module_basename("WtPorter.dll"), "WtPorter.dll");
}

TEST(test_dllhelper, module_basename_returns_empty_for_trailing_separators)
{
	EXPECT_EQ(ModuleNameCompat::module_basename("C:\\wt\\bin\\"), "");
	EXPECT_EQ(ModuleNameCompat::module_basename("/opt/wt/"), "");
}
