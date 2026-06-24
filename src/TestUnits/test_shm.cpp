#include "gtest/gtest/gtest.h"
#include "../WtShareHelper/WtShareHelper.h"

//	原始用例依赖 Windows 路径 E:\deploy_uft\uft_test\.share 下、
//	由外部 master 进程预先写入的共享内存数据，在 Linux 下无法直接运行。
//	这里改为：先用 master 端在本地创建并写入 uft_demo 段数据，
//	再用 slave 端读取并校验，使用例自包含、跨平台可运行。
TEST(test_shm, test_sharehelper)
{
	const char* path = "./uft_demo.share";

	//	master 端创建共享内存并写入数据(bForceWrite=true 保证覆盖旧值)
	ASSERT_TRUE(init_master("uft", path));
	ASSERT_NE(allocate_int32("uft", "uft_demo", "offset", 1, true), nullptr);
	ASSERT_NE(allocate_uint32("uft", "uft_demo", "freq", 0, true), nullptr);
	ASSERT_NE(allocate_uint32("uft", "uft_demo", "second", 10, true), nullptr);
	ASSERT_NE(allocate_double("uft", "uft_demo", "lots", 1.0, true), nullptr);
	ASSERT_TRUE(commit_section("uft", "uft_demo"));

	//	slave 端读取(同进程内复用 master 的 block)
	EXPECT_TRUE(init_slave("uft", path));

	EXPECT_EQ(get_int32("uft", "uft_demo", "offset"), 1);
	EXPECT_EQ(get_uint32("uft", "uft_demo", "freq"), 0);
	EXPECT_EQ(get_uint32("uft", "uft_demo", "second"), 10);
	EXPECT_EQ(get_double("uft", "uft_demo", "lots"), 1.0);
}
