cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../WtPlatform.cmake")

function(WT_ASSERT_ARCH WT_CASE_NAME WT_SYSTEM_NAME WT_SYSTEM_PROCESSOR WT_OSX_ARCHITECTURES WT_GENERATOR_PLATFORM WT_EXPECTED_ARCH)
	WT_NORMALIZE_PLATFORM_ARCH(
		"${WT_SYSTEM_NAME}"
		"${WT_SYSTEM_PROCESSOR}"
		"${WT_OSX_ARCHITECTURES}"
		"${WT_GENERATOR_PLATFORM}"
		WT_ACTUAL_ARCH
	)
	if(NOT WT_ACTUAL_ARCH STREQUAL WT_EXPECTED_ARCH)
		message(FATAL_ERROR "${WT_CASE_NAME}: expected '${WT_EXPECTED_ARCH}', got '${WT_ACTUAL_ARCH}'")
	endif()
endfunction()

function(WT_ASSERT_PLATFORM_VARS WT_CASE_NAME WT_SYSTEM_NAME WT_ARCH_VALUE WT_EXPECTED_SYSTEM WT_EXPECTED_SUFFIX WT_EXPECTED_RUNTIME_SUBDIR)
	WT_RESOLVE_PLATFORM_VARS(
		"${WT_SYSTEM_NAME}"
		"${WT_ARCH_VALUE}"
		WT_ACTUAL_SYSTEM
		WT_ACTUAL_SUFFIX
		WT_ACTUAL_RUNTIME_SUBDIR
	)
	if(NOT WT_ACTUAL_SYSTEM STREQUAL WT_EXPECTED_SYSTEM)
		message(FATAL_ERROR "${WT_CASE_NAME}: expected system '${WT_EXPECTED_SYSTEM}', got '${WT_ACTUAL_SYSTEM}'")
	endif()
	if(NOT WT_ACTUAL_SUFFIX STREQUAL WT_EXPECTED_SUFFIX)
		message(FATAL_ERROR "${WT_CASE_NAME}: expected suffix '${WT_EXPECTED_SUFFIX}', got '${WT_ACTUAL_SUFFIX}'")
	endif()
	if(NOT WT_ACTUAL_RUNTIME_SUBDIR STREQUAL WT_EXPECTED_RUNTIME_SUBDIR)
		message(FATAL_ERROR "${WT_CASE_NAME}: expected runtime subdir '${WT_EXPECTED_RUNTIME_SUBDIR}', got '${WT_ACTUAL_RUNTIME_SUBDIR}'")
	endif()
endfunction()

WT_ASSERT_ARCH("Linux aarch64" "Linux" "aarch64" "" "" "arm64")
WT_ASSERT_ARCH("Linux x86_64" "Linux" "x86_64" "" "" "x64")
WT_ASSERT_ARCH("Linux i686" "Linux" "i686" "" "" "x86")
WT_ASSERT_ARCH("macOS arm64 explicit" "Darwin" "x86_64" "arm64" "" "arm64")
WT_ASSERT_ARCH("macOS x86_64 explicit" "Darwin" "arm64" "x86_64" "" "x64")
WT_ASSERT_ARCH("macOS processor fallback" "Darwin" "arm64" "" "" "arm64")
WT_ASSERT_ARCH("Windows generator x64" "Windows" "x86" "" "x64" "x64")
WT_ASSERT_ARCH("Windows generator Win32" "Windows" "AMD64" "" "Win32" "x86")
WT_ASSERT_ARCH("Windows generator ARM64" "Windows" "AMD64" "" "ARM64" "arm64")
WT_ASSERT_ARCH("Windows processor fallback" "Windows" "AMD64" "" "" "x64")

WT_ASSERT_PLATFORM_VARS("Linux arm64 vars" "Linux" "arm64" "linux" ".so" "linux/arm64")
WT_ASSERT_PLATFORM_VARS("Linux x64 vars" "Linux" "x86_64" "linux" ".so" "linux/x64")
WT_ASSERT_PLATFORM_VARS("macOS vars" "Darwin" "arm64" "macos" ".dylib" "darwin/arm64")
WT_ASSERT_PLATFORM_VARS("Windows vars" "Windows" "AMD64" "windows" ".dll" "win/x64")
