#include "../Share/CpuHelper.hpp"
#include "gtest/gtest/gtest.h"

#if defined(__linux__)
#include <sched.h>
#endif

TEST(test_cpuhelper, bind_core_rejects_cpu_set_size_boundary)
{
#if defined(__linux__)
	EXPECT_FALSE(CpuHelper::bind_core(CPU_SETSIZE));
#else
	EXPECT_FALSE(CpuHelper::bind_core(0));
#endif
}
