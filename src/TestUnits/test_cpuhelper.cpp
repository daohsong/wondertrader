#include "../Share/CpuHelper.hpp"
#include "gtest/gtest/gtest.h"

TEST(test_cpuhelper, bind_core_rejects_cpu_set_size_boundary)
{
	EXPECT_FALSE(CpuHelper::bind_core(CpuHelper::get_cpu_cores()));
	EXPECT_FALSE(CpuHelper::bind_core(UINT32_MAX));
}
