cmake_minimum_required(VERSION 3.21)
project(TraderDD LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(TraderDD SHARED
	TraderDD.cpp
	TraderDD.h
)
wt_set_target_bin_output_directory(TraderDD)
target_link_libraries(TraderDD
	PRIVATE WT::fmt
	PUBLIC WT::Vendor::DD WT::BoostThread WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(TraderDD PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
