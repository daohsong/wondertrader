#pragma once

#ifndef _WIN32

#include <cstddef>
#include <sys/types.h>

namespace signal_hook_detail
{
using WriteFunction = ssize_t (*)(int, const void*, size_t);

bool writeBestEffort(
	int fd,
	const void* data,
	size_t size,
	WriteFunction writer
) noexcept;
}

#endif
