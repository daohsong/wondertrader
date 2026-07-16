#pragma once
#include <climits>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#endif

class CpuHelper
{
public:
	static uint32_t get_cpu_cores()
	{
		static uint32_t cores = std::thread::hardware_concurrency();
		return cores;
	}

#ifdef _WIN32
	static bool bind_core(uint32_t i)
	{
		uint32_t cores = get_cpu_cores();
		if (i >= cores)
			return false;
		if (i >= sizeof(DWORD_PTR) * CHAR_BIT)
			return false;

		HANDLE hThread = GetCurrentThread();
		DWORD_PTR mask = SetThreadAffinityMask(hThread, (DWORD_PTR{1} << i));
		return (mask != 0);
	}
#elif defined(__linux__)
	static bool bind_core(uint32_t i)
	{
		uint32_t cores = get_cpu_cores();
		if (i >= cores)
			return false;
		if (i >= CPU_SETSIZE)
			return false;

		cpu_set_t mask;
		CPU_ZERO(&mask);
		CPU_SET(i, &mask);
		return (pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) == 0);
	}
#else
	static bool bind_core(uint32_t i)
	{
		(void)i;
		return false;
	}
#endif
};
