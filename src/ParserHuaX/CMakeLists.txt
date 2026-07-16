cmake_minimum_required(VERSION 3.21)
project(ParserHuaX LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(ParserHuaX SHARED
	ParserHuaX.cpp
	ParserHuaX.h
)
wt_set_target_bin_output_directory(ParserHuaX)
target_link_libraries(ParserHuaX
	PRIVATE WT::fmt
	PUBLIC WT::Vendor::HuaXParser WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(ParserHuaX PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
