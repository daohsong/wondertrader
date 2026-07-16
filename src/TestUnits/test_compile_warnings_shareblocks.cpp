#include "../WtShareHelper/ShareBlocks.h"
#include "../WtShareHelper/WtShareHelper.h"
#include "../Share/AtomicCompat.hpp"

#include "gtest/gtest/gtest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#if __has_include(<filesystem>)
#include <filesystem>
#endif

#if defined(_MSC_VER)
#include <process.h>
#else
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
constexpr uint32_t TEST_CMD_BLOCK_CAPACITY = shareblock::CMD_BLOCK_CAPACITY;

template<typename Queue>
using readable_field_t = std::remove_reference_t<decltype(std::declval<Queue&>()._readable)>;

template<typename Queue>
using writable_field_t = std::remove_reference_t<decltype(std::declval<Queue&>()._writable)>;

template<typename Queue>
using readable_value_t = std::remove_cv_t<readable_field_t<Queue>>;

template<typename Queue>
using writable_value_t = std::remove_cv_t<writable_field_t<Queue>>;

bool cmd_index_addresses_are_aligned(shareblock::CmdBlock& block)
{
	constexpr std::size_t alignment = wt::atomic_required_alignment<uint32_t>();
	return wt::atomic_is_aligned(&block._readable, alignment)
		&& wt::atomic_is_aligned(&block._writable, alignment);
}

uint32_t test_load_relaxed(uint32_t& value)
{
	return wt::atomic_load_u32(value, std::memory_order_relaxed);
}

uint32_t test_load_acquire(uint32_t& value)
{
	return wt::atomic_load_u32(value, std::memory_order_acquire);
}

void test_store_relaxed(uint32_t& target, uint32_t value)
{
	wt::atomic_store_u32(target, value, std::memory_order_relaxed);
}

void test_store_release(uint32_t& target, uint32_t value)
{
	wt::atomic_store_u32(target, value, std::memory_order_release);
}

uint32_t current_pid_for_test()
{
#if defined(_MSC_VER)
	return static_cast<uint32_t>(_getpid());
#else
	return static_cast<uint32_t>(getpid());
#endif
}

std::string unique_cmd_name(const char* suffix)
{
	static uint32_t nextId = 0;
	std::ostringstream oss;
	oss << "test_compile_warnings_shareblocks_" << current_pid_for_test() << "_" << nextId++ << "_" << suffix;
	return oss.str();
}

std::string temp_cmd_path(const std::string& name)
{
#if __has_include(<filesystem>)
	return (std::filesystem::temp_directory_path() / (name + ".cmd")).string();
#else
	return std::string("/tmp/") + name + ".cmd";
#endif
}

void remove_file_if_exists(const std::string& path)
{
#if __has_include(<filesystem>)
	std::error_code ec;
	std::filesystem::remove(path, ec);
#else
	std::remove(path.c_str());
#endif
}

bool path_exists(const std::string& path)
{
#if __has_include(<filesystem>)
	std::error_code ec;
	return std::filesystem::exists(path, ec);
#else
	return std::ifstream(path, std::ios::binary).good();
#endif
}

void remove_file_and_expect_absent(const std::string& path)
{
	remove_file_if_exists(path);
	EXPECT_FALSE(path_exists(path));
}

uintmax_t cmd_file_size(const std::string& path)
{
#if __has_include(<filesystem>)
	std::error_code ec;
	return std::filesystem::file_size(path, ec);
#else
	return static_cast<uintmax_t>(BoostFile::get_file_size(path.c_str()));
#endif
}

void create_empty_cmd_file(const std::string& path)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	ASSERT_TRUE(out.good());
}

void write_cmd_file_with_capacity(const std::string& path, uint32_t capacity)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	ASSERT_TRUE(out.good());

	shareblock::CmdBlock block;
	block._capacity = capacity;
	out.write(reinterpret_cast<const char*>(&block), sizeof(block));
	ASSERT_TRUE(out.good());
}

void write_short_cmd_file_with_capacity(const std::string& path, uint32_t capacity)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	ASSERT_TRUE(out.good());
	out.write(reinterpret_cast<const char*>(&capacity), sizeof(capacity));
	ASSERT_TRUE(out.good());
}

uint32_t read_cmd_file_capacity(const std::string& path)
{
	uint32_t capacity = 0;
	std::ifstream in(path, std::ios::binary);
	EXPECT_TRUE(in.good());
	if (cmd_file_size(path) >= sizeof(shareblock::CmdBlock))
		in.seekg(static_cast<std::streamoff>(offsetof(shareblock::CmdBlock, _capacity)));
	in.read(reinterpret_cast<char*>(&capacity), sizeof(capacity));
	EXPECT_TRUE(in.good());
	return capacity;
}

#if !defined(_MSC_VER)
template<typename Func>
bool run_in_child(Func func)
{
	const pid_t pid = fork();
	if (pid < 0)
		return false;
	if (pid == 0)
		_exit(func() ? 0 : 1);

	int status = 0;
	if (waitpid(pid, &status, 0) != pid)
		return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool write_signal(int fd)
{
	const char ch = 'x';
	return write(fd, &ch, 1) == 1;
}

bool read_signal(int fd)
{
	char ch = 0;
	return read(fd, &ch, 1) == 1;
}

void close_fd(int fd)
{
	if (fd >= 0)
		close(fd);
}
#endif
}

TEST(test_compile_warnings_shareblocks, cmd_block_indices_are_plain_uint32_fields)
{
	using Block = shareblock::CmdBlock;

	static_assert(std::is_same_v<readable_value_t<Block>, uint32_t>);
	static_assert(std::is_same_v<writable_value_t<Block>, uint32_t>);
	static_assert(!std::is_volatile_v<readable_field_t<Block>>);
	static_assert(!std::is_volatile_v<writable_field_t<Block>>);

	SUCCEED();
}

TEST(test_compile_warnings_shareblocks, cmd_block_index_layout_is_stable_and_aligned)
{
	using Block = shareblock::CmdBlock;

	static_assert(offsetof(Block, _wire) == 0);
	static_assert(offsetof(Block, _capacity) == 32);
	static_assert(offsetof(Block, _readable) == 36);
	static_assert(offsetof(Block, _writable) == 40);
	static_assert(offsetof(Block, _cmdpid) == 44);
	static_assert(offsetof(Block, _commands) == 48);
	static_assert(sizeof(Block) == 8752);
	static_assert(offsetof(Block, _readable) % alignof(uint32_t) == 0);
	static_assert(offsetof(Block, _writable) % alignof(uint32_t) == 0);

	std::unique_ptr<Block> block(new Block());
	EXPECT_TRUE(cmd_index_addresses_are_aligned(*block));
	EXPECT_EQ(wt::shm_wire::ValidationError::ok,
		wt::shm_wire::validate_command_block<static_cast<int>(TEST_CMD_BLOCK_CAPACITY)>(
			block.get(), sizeof(*block)));
}

TEST(test_compile_warnings_shareblocks, command_wire_header_rejects_version_mismatch)
{
	shareblock::CmdBlock block;
	block._wire._version++;
	EXPECT_EQ(wt::shm_wire::ValidationError::bad_version,
		wt::shm_wire::validate_command_block<static_cast<int>(TEST_CMD_BLOCK_CAPACITY)>(
			&block, sizeof(block)));
}

TEST(test_compile_warnings_shareblocks, domain_wire_rejects_out_of_bounds_values)
{
	auto block = std::make_unique<shareblock::ShmBlock>();
	block->_count = 1;
	block->_sections[0]._state = 1;
	block->_sections[0]._count = 1;
	block->_sections[0]._offset = 4;
	block->_sections[0]._keys[0]._type = shareblock::SMVT_DOUBLE;
	block->_sections[0]._keys[0]._offset = 0;

	EXPECT_EQ(wt::shm_wire::ValidationError::bad_bounds,
		wt::shm_wire::validate_shm_block(block.get(), sizeof(*block)));
}

TEST(test_compile_warnings_shareblocks, domain_wire_requires_naturally_aligned_values)
{
	constexpr std::uint32_t alignedValuesCapability = wt::shm_wire::kCapabilityAlignedValues;
	EXPECT_NE(0u, wt::shm_wire::kShmBlockCapabilities & alignedValuesCapability);

	auto block = std::make_unique<shareblock::ShmBlock>();
	block->_count = 1;
	block->_sections[0]._state = 1;
	block->_sections[0]._count = 1;
	block->_sections[0]._offset = 12;
	block->_sections[0]._keys[0]._type = shareblock::SMVT_DOUBLE;
	block->_sections[0]._keys[0]._offset = 4;

	EXPECT_EQ(wt::shm_wire::ValidationError::bad_bounds,
		wt::shm_wire::validate_shm_block(block.get(), sizeof(*block)));

	block->_sections[0]._offset = 16;
	block->_sections[0]._keys[0]._offset = 8;
	EXPECT_EQ(wt::shm_wire::ValidationError::ok,
		wt::shm_wire::validate_shm_block(block.get(), sizeof(*block)));

	block->_wire._capabilities &= ~alignedValuesCapability;
	EXPECT_EQ(wt::shm_wire::ValidationError::bad_capabilities,
		wt::shm_wire::validate_shm_block(block.get(), sizeof(*block)));
}

TEST(test_compile_warnings_shareblocks, single_producer_release_acquire_protocol_smoke_test)
{
	shareblock::_CmdBlock<2> block;

	const uint32_t writable = test_load_relaxed(block._writable);
	test_store_relaxed(block._writable, writable + 1);

	shareblock::CmdInfo& command = block._commands[writable % block._capacity];
	command._state = 0;
	std::strcpy(command._command, "reload");

	test_store_release(block._readable, writable);

	const uint32_t readable = test_load_acquire(block._readable);
	const shareblock::CmdInfo& consumed = block._commands[readable % block._capacity];

	EXPECT_EQ(writable, readable);
	EXPECT_STREQ("reload", consumed._command);
}

TEST(test_compile_warnings_shareblocks, single_process_thread_protocol_smoke_test)
{
	shareblock::_CmdBlock<2> block;

	std::thread producer([&block]() {
		const uint32_t writable = test_load_relaxed(block._writable);
		test_store_relaxed(block._writable, writable + 1);

		shareblock::CmdInfo& command = block._commands[writable % block._capacity];
		command._state = 0;
		std::strcpy(command._command, "rebalance");

		test_store_release(block._readable, writable);
	});

	uint32_t readable = UINT32_MAX;
	while (readable == UINT32_MAX)
	{
		readable = test_load_acquire(block._readable);
		std::this_thread::yield();
	}

	const shareblock::CmdInfo& consumed = block._commands[readable % block._capacity];
	EXPECT_EQ(0u, readable);
	EXPECT_STREQ("rebalance", consumed._command);

	producer.join();
}

TEST(test_compile_warnings_shareblocks, init_cmder_rejects_corrupt_zero_capacity_cmd_file)
{
#if !defined(_MSC_VER)
	const std::string name = unique_cmd_name("fresh");
	const std::string path = temp_cmd_path(name);
	remove_file_if_exists(path);
	write_cmd_file_with_capacity(path, 0);

	EXPECT_TRUE(run_in_child([&]() {
		return !::init_cmder(name.c_str(), true, path.c_str()) &&
			!::add_cmd(name.c_str(), "reload");
	}));
	EXPECT_EQ(0u, read_cmd_file_capacity(path));

	remove_file_and_expect_absent(path);
#else
	SUCCEED();
#endif
}

TEST(test_compile_warnings_shareblocks, init_cmder_truncates_real_empty_cmd_file)
{
#if !defined(_MSC_VER)
	const std::string name = unique_cmd_name("empty");
	const std::string path = temp_cmd_path(name);
	remove_file_if_exists(path);
	create_empty_cmd_file(path);
	ASSERT_EQ(0u, cmd_file_size(path));

	EXPECT_TRUE(run_in_child([&]() {
		return ::init_cmder(name.c_str(), true, path.c_str()) &&
			::add_cmd(name.c_str(), "reload");
	}));
	EXPECT_EQ(sizeof(shareblock::CmdBlock), cmd_file_size(path));
	EXPECT_EQ(TEST_CMD_BLOCK_CAPACITY, read_cmd_file_capacity(path));

	remove_file_and_expect_absent(path);
#else
	SUCCEED();
#endif
}

TEST(test_compile_warnings_shareblocks, init_cmder_accepts_current_capacity_cmd_file)
{
#if !defined(_MSC_VER)
	const std::string name = unique_cmd_name("current");
	const std::string path = temp_cmd_path(name);
	remove_file_if_exists(path);
	write_cmd_file_with_capacity(path, TEST_CMD_BLOCK_CAPACITY);

	EXPECT_TRUE(run_in_child([&]() {
		return ::init_cmder(name.c_str(), true, path.c_str()) &&
			::add_cmd(name.c_str(), "reload");
	}));

	remove_file_and_expect_absent(path);
#else
	SUCCEED();
#endif
}

TEST(test_compile_warnings_shareblocks, init_cmder_rejects_short_current_capacity_file_without_rewriting_file)
{
#if !defined(_MSC_VER)
	const std::string name = unique_cmd_name("short_current");
	const std::string path = temp_cmd_path(name);
	remove_file_if_exists(path);
	write_short_cmd_file_with_capacity(path, TEST_CMD_BLOCK_CAPACITY);
	ASSERT_EQ(sizeof(uint32_t), cmd_file_size(path));

	EXPECT_TRUE(run_in_child([&]() {
		return !::init_cmder(name.c_str(), true, path.c_str()) &&
			!::add_cmd(name.c_str(), "reload");
	}));
	EXPECT_EQ(sizeof(uint32_t), cmd_file_size(path));
	EXPECT_EQ(TEST_CMD_BLOCK_CAPACITY, read_cmd_file_capacity(path));

	remove_file_and_expect_absent(path);
#else
	SUCCEED();
#endif
}

TEST(test_compile_warnings_shareblocks, init_cmder_rejects_unmappable_existing_path_without_throwing)
{
#if __has_include(<filesystem>) && !defined(_MSC_VER)
	const std::string name = unique_cmd_name("unmappable");
	const std::string path = temp_cmd_path(name);
	remove_file_if_exists(path);
	ASSERT_TRUE(std::filesystem::create_directory(path));
	ASSERT_TRUE(std::filesystem::is_directory(path));

	EXPECT_TRUE(run_in_child([&]() {
		return !::init_cmder(name.c_str(), true, path.c_str()) &&
			!::add_cmd(name.c_str(), "reload");
	}));
	EXPECT_TRUE(std::filesystem::is_directory(path));

	remove_file_and_expect_absent(path);
#else
	SUCCEED();
#endif
}

TEST(test_compile_warnings_shareblocks, init_cmder_rejects_unexpected_capacity_without_rewriting_file)
{
#if !defined(_MSC_VER)
	const uint32_t invalidCapacities[] = { TEST_CMD_BLOCK_CAPACITY + 1, UINT32_MAX };

	for (uint32_t capacity : invalidCapacities)
	{
		const std::string name = unique_cmd_name("invalid");
		const std::string path = temp_cmd_path(name);
		remove_file_if_exists(path);
		write_cmd_file_with_capacity(path, capacity);

		EXPECT_TRUE(run_in_child([&]() {
			return !::init_cmder(name.c_str(), true, path.c_str()) &&
				!::add_cmd(name.c_str(), "reload");
		}));
		EXPECT_EQ(capacity, read_cmd_file_capacity(path));

		remove_file_and_expect_absent(path);
	}
#else
	SUCCEED();
#endif
}

TEST(test_compile_warnings_shareblocks, reader_and_commander_processes_share_command_queue)
{
#if !defined(_MSC_VER)
	const std::string pathName = unique_cmd_name("reader_path");
	const std::string cmderName = unique_cmd_name("reader_cmder");
	const std::string readerName = unique_cmd_name("reader_reader");
	const std::string path = temp_cmd_path(pathName);
	remove_file_if_exists(path);

	int readyPipe[2] = {-1, -1};
	int proceedPipe[2] = {-1, -1};
	int checkpointPipe[2] = {-1, -1};
	ASSERT_EQ(0, pipe(readyPipe));
	ASSERT_EQ(0, pipe(proceedPipe));
	ASSERT_EQ(0, pipe(checkpointPipe));

	const pid_t readerPid = fork();
	ASSERT_GE(readerPid, 0);
	if (readerPid == 0)
	{
		close_fd(readyPipe[0]);
		close_fd(proceedPipe[1]);
		close_fd(checkpointPipe[0]);

		bool ok = ::init_cmder(readerName.c_str(), false, path.c_str());
		uint32_t lastIdx = UINT32_MAX;
		ok = ok && std::strcmp(::get_cmd(readerName.c_str(), lastIdx), "") == 0 && lastIdx == 999999u;
		ok = ok && write_signal(readyPipe[1]);
		ok = ok && read_signal(proceedPipe[0]);
		ok = ok && std::strcmp(::get_cmd(readerName.c_str(), lastIdx), "reload") == 0 && lastIdx == 0u;
		ok = ok && std::strcmp(::get_cmd(readerName.c_str(), lastIdx), "") == 0 && lastIdx == 0u;
		ok = ok && write_signal(checkpointPipe[1]);
		ok = ok && read_signal(proceedPipe[0]);
		ok = ok && std::strcmp(::get_cmd(readerName.c_str(), lastIdx), "rebalance") == 0 && lastIdx == 1u;

		_exit(ok ? 0 : 1);
	}

	close_fd(readyPipe[1]);
	close_fd(proceedPipe[0]);
	close_fd(checkpointPipe[1]);

	ASSERT_TRUE(read_signal(readyPipe[0]));
	ASSERT_TRUE(run_in_child([&]() {
		return ::init_cmder(cmderName.c_str(), true, path.c_str()) &&
			::add_cmd(cmderName.c_str(), "reload");
	}));
	ASSERT_TRUE(write_signal(proceedPipe[1]));
	ASSERT_TRUE(read_signal(checkpointPipe[0]));
	ASSERT_TRUE(run_in_child([&]() {
		return ::init_cmder(cmderName.c_str(), true, path.c_str()) &&
			::add_cmd(cmderName.c_str(), "rebalance");
	}));
	ASSERT_TRUE(write_signal(proceedPipe[1]));

	int status = 0;
	ASSERT_EQ(readerPid, waitpid(readerPid, &status, 0));
	EXPECT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	close_fd(readyPipe[0]);
	close_fd(proceedPipe[1]);
	close_fd(checkpointPipe[0]);
	remove_file_and_expect_absent(path);
#else
	SUCCEED();
#endif
}
