cmake_minimum_required(VERSION 3.20)
project(WonderTraderNanomsgDependency LANGUAGES CXX)

if(NOT WT_DEPENDENCIES_MODULE)
	message(FATAL_ERROR "WT_DEPENDENCIES_MODULE is required")
endif()

set(CMAKE_DISABLE_FIND_PACKAGE_nanomsg TRUE)
set(WT_DEPS_ROOT "")
set(WT_NANOMSG_INCLUDE_PATHS "${CMAKE_CURRENT_LIST_DIR}/include")
set(WT_NANOMSG_LIBRARY_PATHS "")
set(WT_NANOMSG_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include" CACHE PATH "" FORCE)
set(WT_NANOMSG_LIB "${CMAKE_COMMAND}" CACHE FILEPATH "" FORCE)

include("${WT_DEPENDENCIES_MODULE}")
WT_SETUP_NANOMSG_DEPENDENCY()

if(NOT TARGET WT::Nanomsg)
	message(FATAL_ERROR "WT::Nanomsg fallback provider was not created")
endif()

get_target_property(WT_NANOMSG_LINK_LIBRARIES WT_NANOMSG INTERFACE_LINK_LIBRARIES)
if(NOT "WT_NANOMSG_LIBRARY" IN_LIST WT_NANOMSG_LINK_LIBRARIES)
	message(FATAL_ERROR "WT::Nanomsg does not publish its imported library")
endif()

get_target_property(WT_NANOMSG_INCLUDES WT_NANOMSG INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
if(NOT "${CMAKE_CURRENT_LIST_DIR}/include" IN_LIST WT_NANOMSG_INCLUDES)
	message(FATAL_ERROR "WT::Nanomsg does not publish its headers as system includes")
endif()

get_target_property(WT_NANOMSG_LOCATION WT_NANOMSG_LIBRARY IMPORTED_LOCATION)
if(NOT WT_NANOMSG_LOCATION STREQUAL CMAKE_COMMAND)
	message(FATAL_ERROR "WT::Nanomsg imported library location was not preserved")
endif()
