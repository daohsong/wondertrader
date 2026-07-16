cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../WtPlatform.cmake")

if(WT_INVALID_CASE STREQUAL "unsupported_arch")
	WT_NORMALIZE_PLATFORM_ARCH("Linux" "riscv64" "" "" WT_ARCH)
elseif(WT_INVALID_CASE STREQUAL "universal_macos")
	WT_NORMALIZE_PLATFORM_ARCH("Darwin" "arm64" "arm64;x86_64" "" WT_ARCH)
elseif(WT_INVALID_CASE STREQUAL "unsupported_system")
	WT_RESOLVE_PLATFORM_VARS("FreeBSD" "x86_64" WT_SYSTEM WT_SUFFIX WT_RUNTIME_SUBDIR)
elseif(WT_INVALID_CASE STREQUAL "missing_processor")
	WT_NORMALIZE_PLATFORM_ARCH("Linux" "" "" "" WT_ARCH)
else()
	message(FATAL_ERROR "Unknown WT_INVALID_CASE '${WT_INVALID_CASE}'")
endif()

message(FATAL_ERROR "Invalid platform case '${WT_INVALID_CASE}' unexpectedly succeeded")
