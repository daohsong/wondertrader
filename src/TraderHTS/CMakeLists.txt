cmake_minimum_required(VERSION 3.21)
project(TraderHTS LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(TraderHTS SHARED
	TraderHTS.cpp
	TraderHTS.h
)
wt_set_target_bin_output_directory(TraderHTS)
target_link_libraries(TraderHTS
	PRIVATE WT::fmt WT::RapidJSON
	PUBLIC WT::Vendor::HTS WT::BoostThread WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(TraderHTS PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
