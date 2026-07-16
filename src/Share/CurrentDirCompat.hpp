#pragma once

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

inline std::string wt_system_error_message(int errorCode)
{
#if defined(_MSC_VER)
	char buffer[256] = {};
	if (::strerror_s(buffer, sizeof(buffer), errorCode) == 0)
		return std::string(buffer);
#else
	const char* errorMessage = std::strerror(errorCode);
	if (errorMessage != nullptr)
		return std::string(errorMessage);
#endif
	return "error code " + std::to_string(errorCode);
}

inline std::string wt_current_working_directory()
{
	std::size_t bufferSize = 4096;
	std::vector<char> buffer(bufferSize);

	for (;;)
	{
		errno = 0;
#if defined(_WIN32)
		char* result = _getcwd(buffer.data(), static_cast<int>(buffer.size()));
#else
		char* result = getcwd(buffer.data(), buffer.size());
#endif
		if (result != nullptr)
			return std::string(result);

		if (errno != ERANGE)
			break;

		bufferSize *= 2;
		buffer.resize(bufferSize);
	}

	const int errorCode = errno;
	std::string message = "failed to resolve current working directory";
	if (errorCode != 0)
	{
		message += ": ";
		message += wt_system_error_message(errorCode);
	}
	throw std::runtime_error(message);
}
