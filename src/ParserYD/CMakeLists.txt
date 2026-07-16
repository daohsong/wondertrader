cmake_minimum_required(VERSION 3.21)
project(ParserYD LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(ParserYD SHARED
	ParserYD.cpp
	ParserYD.h
)
wt_set_target_bin_output_directory(ParserYD)
target_link_libraries(ParserYD
	PRIVATE WT::fmt
	PUBLIC WT::Vendor::YD WT::BoostThread WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(ParserYD PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
