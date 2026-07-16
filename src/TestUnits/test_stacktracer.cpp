#include "gtest/gtest.h"

#include <string>
#include <vector>

#include "../WTSUtils/StackTracer/StackTracer.h"
#include "../WTSUtils/StackTracer/StackTracerInternal.h"

#ifndef _WIN32
TEST(StackTracer, FormatsMangledAndFallbackSymbolLines)
{
	const std::string demangled = wt::stacktrace_detail::format_symbol_line(
		"./module(_Z3foov+0x15) [0x123]");
	EXPECT_NE(std::string::npos, demangled.find("foo()"));
	EXPECT_NE(std::string::npos, demangled.find("0x15"));

	const std::string fallback = wt::stacktrace_detail::format_symbol_line(
		"./module(not_mangled+0x2) [0x456]");
	EXPECT_NE(std::string::npos, fallback.find("not_mangled"));
	EXPECT_EQ("unparsed symbol", wt::stacktrace_detail::format_symbol_line("unparsed symbol"));
	EXPECT_TRUE(wt::stacktrace_detail::format_symbol_line(nullptr).empty());
}

TEST(StackTracer, FormatsLongSymbolLinesWithoutTruncation)
{
	const std::string module(4096, 'm');
	const std::string formatted = wt::stacktrace_detail::format_symbol_line(
		(module + "(_Z3foov+0x1) [0x2]").c_str());

	EXPECT_EQ(0U, formatted.find(module));
	EXPECT_NE(std::string::npos, formatted.find("foo()"));
	EXPECT_GT(formatted.size(), module.size());
}
#endif

TEST(StackTracer, CapturesWithoutNullCallbackMessages)
{
	std::vector<std::string> messages;
	print_stack_trace([&messages](const char* message) {
		ASSERT_NE(nullptr, message);
		messages.emplace_back(message);
	});

	EXPECT_FALSE(messages.empty());
	EXPECT_NO_THROW(print_stack_trace(TracerLogCallback{}));
}
