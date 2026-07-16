cmake_minimum_required(VERSION 3.20)
project(WonderTraderZmqDependency LANGUAGES CXX)

if(NOT WT_DEPENDENCIES_MODULE)
	message(FATAL_ERROR "WT_DEPENDENCIES_MODULE is required")
endif()

set(CMAKE_DISABLE_FIND_PACKAGE_cppzmq TRUE)
set(WT_DEPS_ROOT "")
set(WT_ZMQ_INCLUDE_PATHS "${CMAKE_CURRENT_LIST_DIR}/include")
set(WT_CPPZMQ_INCLUDE_PATHS "${CMAKE_CURRENT_LIST_DIR}/include")
set(WT_ZMQ_LIBRARY_PATHS "")
set(WT_ZMQ_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include" CACHE PATH "" FORCE)
set(WT_CPPZMQ_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include" CACHE PATH "" FORCE)
set(WT_ZMQ_LIB "${CMAKE_COMMAND}" CACHE FILEPATH "" FORCE)

include("${WT_DEPENDENCIES_MODULE}")
WT_SETUP_ZMQ_DEPENDENCY()

if(WT_ZMQ_MISSING OR NOT TARGET WT::ZMQ)
	message(FATAL_ERROR "WT::ZMQ fallback provider was not created")
endif()

get_target_property(WT_ZMQ_LINK_LIBRARIES WT_ZMQ INTERFACE_LINK_LIBRARIES)
if(NOT "WT_ZMQ_LIBRARY" IN_LIST WT_ZMQ_LINK_LIBRARIES)
	message(FATAL_ERROR "WT::ZMQ does not publish its imported library")
endif()

get_target_property(WT_ZMQ_INCLUDES WT_ZMQ INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
if(NOT "${CMAKE_CURRENT_LIST_DIR}/include" IN_LIST WT_ZMQ_INCLUDES)
	message(FATAL_ERROR "WT::ZMQ does not publish its headers as system includes")
endif()

get_target_property(WT_ZMQ_LOCATION WT_ZMQ_LIBRARY IMPORTED_LOCATION)
if(NOT WT_ZMQ_LOCATION STREQUAL CMAKE_COMMAND)
	message(FATAL_ERROR "WT::ZMQ imported library location was not preserved")
endif()
