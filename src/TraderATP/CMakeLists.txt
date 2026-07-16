cmake_minimum_required(VERSION 3.21)
project(TraderATP LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(TraderATP SHARED
	TraderATP.cpp
	TraderATP.h
)
wt_set_target_bin_output_directory(TraderATP)
target_link_libraries(TraderATP
	PRIVATE WT::fmt
	PUBLIC WT::Vendor::ATP WT::BoostThread WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(TraderATP PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
