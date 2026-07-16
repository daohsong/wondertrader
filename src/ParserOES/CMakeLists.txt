cmake_minimum_required(VERSION 3.21)
project(ParserOES LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

add_library(ParserOES SHARED
	ParserOES.cpp
	ParserOES.h
)
wt_set_target_bin_output_directory(ParserOES)
target_link_libraries(ParserOES
	PRIVATE WT::fmt
	PUBLIC WT::Vendor::OES WT::BoostThread WT::Platform ${FS_LIB}
)

if(NOT MSVC)
	set_target_properties(ParserOES PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON
	)
endif()
