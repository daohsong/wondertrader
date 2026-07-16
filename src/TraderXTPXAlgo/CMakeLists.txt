cmake_minimum_required(VERSION 3.21)
project(TraderXTPXAlgo LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(TraderXTPXAlgo SHARED
	TraderXTPXAlgo.cpp
	TraderXTPXAlgo.h
)
wt_set_target_bin_output_directory(TraderXTPXAlgo)
target_link_libraries(TraderXTPXAlgo
	PRIVATE WT::fmt
	PUBLIC WT::Vendor::XTPXAlgo WT::BoostThread WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(TraderXTPXAlgo PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
