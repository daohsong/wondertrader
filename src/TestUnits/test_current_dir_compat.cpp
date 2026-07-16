#include "../Share/CurrentDirCompat.hpp"
#include "../Share/StrUtil.hpp"
#include "gtest/gtest/gtest.h"

#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#else
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
class ScopedCwdRestore
{
public:
	explicit ScopedCwdRestore(int fd)
		: _fd(fd)
	{
	}

	~ScopedCwdRestore()
	{
		if (_fd >= 0)
		{
			const int restoreResult = fchdir(_fd);
			(void)restoreResult;
			close(_fd);
		}
	}

	ScopedCwdRestore(const ScopedCwdRestore&) = delete;
	ScopedCwdRestore& operator=(const ScopedCwdRestore&) = delete;

private:
	int _fd;
};
#endif

TEST(test_current_dir_compat, returns_non_empty_standardisable_path)
{
	const std::string cwd = wt_current_working_directory();
	ASSERT_FALSE(cwd.empty());

	const std::string standardised = StrUtil::standardisePath(cwd);
	EXPECT_FALSE(standardised.empty());
}

TEST(test_current_dir_compat, formats_known_system_error)
{
	const std::string message = wt_system_error_message(EINVAL);
	EXPECT_FALSE(message.empty());
}

TEST(test_current_dir_compat, throws_when_current_directory_cannot_be_resolved)
{
#if defined(_WIN32)
	SUCCEED() << "Deleting the active current directory is not portable on Windows";
	return;
#else
	const int originalFd = open(".", O_RDONLY | O_DIRECTORY);
	ASSERT_GE(originalFd, 0);
	ScopedCwdRestore restoreCwd(originalFd);

	char templatePath[] = "/tmp/wt_current_dir_compat_XXXXXX";
	char* tempDir = mkdtemp(templatePath);
	ASSERT_NE(tempDir, nullptr);

	ASSERT_EQ(chdir(tempDir), 0);
	ASSERT_EQ(rmdir(tempDir), 0);

	EXPECT_THROW((void)wt_current_working_directory(), std::runtime_error);
#endif
}
