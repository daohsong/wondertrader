#pragma once

#include <cstddef>
#include <string>

namespace ModuleNameCompat
{
namespace detail
{
inline bool starts_with(const std::string& text, const char* prefix)
{
	if (prefix == nullptr || prefix[0] == '\0')
		return true;

	std::size_t idx = 0;
	while (prefix[idx] != '\0')
	{
		if (idx >= text.size() || text[idx] != prefix[idx])
			return false;
		++idx;
	}
	return true;
}

inline char ascii_lower(char ch)
{
	if (ch >= 'A' && ch <= 'Z')
		return static_cast<char>(ch - 'A' + 'a');
	return ch;
}

inline bool chars_equal(char lhs, char rhs, bool ignoreCase)
{
	return ignoreCase ? ascii_lower(lhs) == ascii_lower(rhs) : lhs == rhs;
}

inline bool ends_with(const std::string& text, const char* suffix, bool ignoreCase)
{
	if (suffix == nullptr || suffix[0] == '\0')
		return true;

	std::size_t len = 0;
	while (suffix[len] != '\0')
		++len;

	if (len > text.size())
		return false;

	const std::size_t offset = text.size() - len;
	for (std::size_t idx = 0; idx < len; ++idx)
	{
		if (!chars_equal(text[offset + idx], suffix[idx], ignoreCase))
			return false;
	}
	return true;
}

inline std::size_t basename_offset(const std::string& module)
{
	const std::size_t pos = module.find_last_of("/\\");
	return pos == std::string::npos ? 0 : pos + 1;
}
}

inline bool has_module_suffix(const std::string& basename, bool ignoreCase)
{
	return detail::ends_with(basename, ".dll", ignoreCase)
		|| detail::ends_with(basename, ".so", ignoreCase)
		|| detail::ends_with(basename, ".dylib", ignoreCase);
}

inline std::string module_basename(const char* name)
{
	std::string module = name == nullptr ? std::string() : std::string(name);
	return module.substr(detail::basename_offset(module));
}

inline std::string wrap_module_name(const char* name, const char* unixPrefix, const char* suffix,
	bool useUnixPrefix, bool ignoreSuffixCase = false)
{
	std::string module = name == nullptr ? std::string() : std::string(name);
	const std::size_t basenamePos = detail::basename_offset(module);
	if (basenamePos >= module.size())
		return module;

	std::string basename = module.substr(basenamePos);
	if (has_module_suffix(basename, ignoreSuffixCase))
		return module;

	if (useUnixPrefix && unixPrefix != nullptr && unixPrefix[0] != '\0'
		&& !detail::starts_with(basename, unixPrefix))
	{
		basename.insert(0, unixPrefix);
	}

	if (suffix != nullptr && suffix[0] != '\0')
		basename.append(suffix);

	return module.substr(0, basenamePos) + basename;
}
}
