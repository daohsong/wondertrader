cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../WtCxxStandard.cmake")

if(NOT DEFINED WT_INVALID_STANDARD)
	message(FATAL_ERROR "WT_INVALID_STANDARD is required")
endif()

WT_VALIDATE_CXX_STANDARD("${WT_INVALID_STANDARD}")
message(FATAL_ERROR "Invalid C++ standard '${WT_INVALID_STANDARD}' unexpectedly succeeded")
