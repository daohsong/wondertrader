cmake_minimum_required(VERSION 3.21)
project(TraderHuaX LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(TraderHuaX SHARED
	TraderHuaX.cpp
	TraderHuaX.h
)
wt_set_target_bin_output_directory(TraderHuaX)
target_link_libraries(TraderHuaX
	PRIVATE WT::fmt
	PUBLIC WT::Vendor::HuaXTrader WT::BoostThread WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(TraderHuaX PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
