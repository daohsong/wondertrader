
#include "StackTracer.h"
#include "StackTracerInternal.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

#ifdef _WIN32
#	ifdef _MSC_VER
#include "StackWalker.h"
void print_stack_trace(TracerLogCallback cb)
{
	if (!cb)
		return;

	cb("Uncaught exception");
	StackWalker sw(cb);
	sw.ShowCallstack();
}
#	else //_GCC
void print_stack_trace(TracerLogCallback cb) {
	cb("Cannot print stack trace due to being build on windows with GCC");
}
#	endif
#else
#include <cerrno>
#include <execinfo.h>
#include <cxxabi.h>

namespace wt::stacktrace_detail
{
std::string format_symbol_line(const char* symbol)
{
	if (symbol == nullptr)
		return std::string();

	const std::string line(symbol);
	const std::size_t beginName = line.find('(');
	const std::size_t endOffset = line.find(')', beginName == std::string::npos ? 0 : beginName + 1);
	if (beginName == std::string::npos || endOffset == std::string::npos || beginName >= endOffset)
		return line;

	const std::size_t beginOffset = line.find('+', beginName + 1);
	const bool hasOffset = beginOffset != std::string::npos && beginOffset < endOffset;
	const std::size_t nameEnd = hasOffset ? beginOffset : endOffset;
	if (nameEnd <= beginName + 1)
		return line;

	const std::string mangledName = line.substr(beginName + 1, nameEnd - beginName - 1);
	std::string displayName = mangledName;
	int status = 0;
	std::unique_ptr<char, decltype(&std::free)> demangled(
		abi::__cxa_demangle(mangledName.c_str(), nullptr, nullptr, &status), &std::free);
	if (status == 0 && demangled)
		displayName = demangled.get();

	const std::string offset = hasOffset
		? line.substr(beginOffset + 1, endOffset - beginOffset - 1)
		: std::string();
	return line.substr(0, beginName) + " ( " + displayName + " + " + offset + ") "
		+ line.substr(endOffset + 1);
}
}

void print_stack_trace(TracerLogCallback cb) {
	if (!cb)
		return;

	constexpr std::size_t maxFrames = 128;
	std::array<void*, maxFrames> addrlist{};

	// retrieve current stack addresses
	const int captured = backtrace(addrlist.data(), static_cast<int>(addrlist.size()));
	const unsigned int addrlen = captured > 0 ? static_cast<unsigned int>(captured) : 0U;

	if (addrlen == 0) {
		cb("no trace fetched");
		return;
	}

	// resolve addresses into strings containing "filename(function+address)",
	// Actually it will be ## program address function + offset
	// this array must be free()-ed
	char **symbollist = backtrace_symbols(addrlist.data(), static_cast<int>(addrlen));
	if (symbollist == nullptr) {
		cb("symbol resolution failed");
		return;
	}

	// iterate over the returned symbol lines. skip the first, it is the
	// address of this function.
	const unsigned int firstFrame = addrlen > 4 ? 4U : 0U;
	for (unsigned int i = firstFrame; i < addrlen; i++) {
		const std::string formatted = wt::stacktrace_detail::format_symbol_line(symbollist[i]);
		cb(formatted.c_str());
	}
	free(symbollist);
}
#endif // _WINDOWS
