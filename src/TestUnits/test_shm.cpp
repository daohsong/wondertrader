#include "gtest/gtest/gtest.h"
#include "../WtShareHelper/WtShareHelper.h"
#include "../WtShareHelper/ShareBlocks.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#if defined(_MSC_VER)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
std::uint32_t shm_test_pid()
{
#if defined(_MSC_VER)
	return static_cast<std::uint32_t>(_getpid());
#else
	return static_cast<std::uint32_t>(getpid());
#endif
}

std::filesystem::path shm_test_path(const char* suffix)
{
	return std::filesystem::current_path()
		/ (std::string("wondertrader_shm_") + std::to_string(shm_test_pid()) + "_" + suffix + ".share");
}
}

//	原始用例依赖 Windows 路径 E:\deploy_uft\uft_test\.share 下、
//	由外部 master 进程预先写入的共享内存数据，在 Linux 下无法直接运行。
//	这里改为：先用 master 端在本地创建并写入 uft_demo 段数据，
//	再用 slave 端读取并校验，使用例自包含、跨平台可运行。
TEST(test_shm, test_sharehelper)
{
	const std::filesystem::path path = shm_test_path("domain");
	std::error_code error;
	std::filesystem::remove(path, error);
	const std::string master_name = "uft_master_" + std::to_string(shm_test_pid());
	const std::string slave_name = "uft_slave_" + std::to_string(shm_test_pid());

	//	master 端创建共享内存并写入数据(bForceWrite=true 保证覆盖旧值)
	ASSERT_TRUE(init_master(master_name.c_str(), path.string().c_str()));
	int32_t* offset = allocate_int32(master_name.c_str(), "uft_demo", "offset", 1, true);
	uint32_t* freq = allocate_uint32(master_name.c_str(), "uft_demo", "freq", 0, true);
	uint32_t* second = allocate_uint32(master_name.c_str(), "uft_demo", "second", 10, true);
	double* lots = allocate_double(master_name.c_str(), "uft_demo", "lots", 1.0, true);
	ASSERT_NE(offset, nullptr);
	ASSERT_NE(freq, nullptr);
	ASSERT_NE(second, nullptr);
	ASSERT_NE(lots, nullptr);
	EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(offset) % alignof(int32_t));
	EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(freq) % alignof(uint32_t));
	EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(second) % alignof(uint32_t));
	EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(lots) % alignof(double));
	*lots = 2.5;
	EXPECT_TRUE(set_double(master_name.c_str(), "uft_demo", "lots", 1.0));
	ASSERT_TRUE(commit_section(master_name.c_str(), "uft_demo"));

	// 使用不同 domain 名强制 slave 重新映射同一 wire 文件。
	ASSERT_TRUE(init_slave(slave_name.c_str(), path.string().c_str()));
	EXPECT_EQ(get_int32(slave_name.c_str(), "uft_demo", "offset"), 1);
	EXPECT_EQ(get_uint32(slave_name.c_str(), "uft_demo", "freq"), 0);
	EXPECT_EQ(get_uint32(slave_name.c_str(), "uft_demo", "second"), 10);
	EXPECT_EQ(get_double(slave_name.c_str(), "uft_demo", "lots"), 1.0);
	EXPECT_TRUE(release_slave(slave_name.c_str()));

#if !defined(_MSC_VER)
	const pid_t child = fork();
	ASSERT_GE(child, 0);
	if (child == 0)
	{
		const std::string child_name = "uft_child_" + std::to_string(static_cast<unsigned long long>(getpid()));
		const bool ok = init_slave(child_name.c_str(), path.string().c_str())
			&& get_int32(child_name.c_str(), "uft_demo", "offset") == 1
			&& get_uint32(child_name.c_str(), "uft_demo", "second") == 10
			&& get_double(child_name.c_str(), "uft_demo", "lots") == 1.0;
		_exit(ok ? 0 : 1);
	}
	int status = 0;
	ASSERT_EQ(child, waitpid(child, &status, 0));
	EXPECT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
#endif

	std::filesystem::remove(path, error);
}

TEST(test_shm, rejects_unversioned_and_wrong_version_domain_files)
{
	const std::filesystem::path path = shm_test_path("invalid");
	std::error_code error;
	std::filesystem::remove(path, error);

	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		std::string zeros(sizeof(shareblock::ShmBlock), '\0');
		output.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
		ASSERT_TRUE(output.good());
	}
	const std::string legacy_name = "uft_legacy_" + std::to_string(shm_test_pid());
	EXPECT_FALSE(init_slave(legacy_name.c_str(), path.string().c_str()));

	auto block = std::make_unique<shareblock::ShmBlock>();
	block->_wire._version++;
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(block.get()), sizeof(*block));
		ASSERT_TRUE(output.good());
	}
	const std::string version_name = "uft_version_" + std::to_string(shm_test_pid());
	EXPECT_FALSE(init_slave(version_name.c_str(), path.string().c_str()));

	std::filesystem::remove(path, error);
}
