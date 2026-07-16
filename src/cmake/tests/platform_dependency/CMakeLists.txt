cmake_minimum_required(VERSION 3.20)
project(WonderTraderPlatformDependency LANGUAGES CXX)

if(NOT WT_DEPENDENCIES_MODULE)
	message(FATAL_ERROR "WT_DEPENDENCIES_MODULE is required")
endif()

include("${WT_DEPENDENCIES_MODULE}")
WT_SETUP_PLATFORM_DEPENDENCY()

if(NOT TARGET WT::Platform)
	message(FATAL_ERROR "WT::Platform was not created")
endif()

get_target_property(WT_PLATFORM_LINKS WT_PLATFORM INTERFACE_LINK_LIBRARIES)
if(NOT "Threads::Threads" IN_LIST WT_PLATFORM_LINKS)
	message(FATAL_ERROR "WT::Platform does not publish Threads::Threads")
endif()
foreach(WT_DL_LIBRARY IN LISTS CMAKE_DL_LIBS)
	if(NOT WT_DL_LIBRARY IN_LIST WT_PLATFORM_LINKS)
		message(FATAL_ERROR "WT::Platform does not publish ${WT_DL_LIBRARY}")
	endif()
endforeach()
