cmake_minimum_required(VERSION 3.21)
project(TraderOES LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(TraderOES SHARED
	TraderOES.cpp
	TraderOES.h
)
wt_set_target_bin_output_directory(TraderOES)
target_link_libraries(TraderOES
	PRIVATE WT::fmt
	PUBLIC WT::Vendor::OES WT::BoostThread WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(TraderOES PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
