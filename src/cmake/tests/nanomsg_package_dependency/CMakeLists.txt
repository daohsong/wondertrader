cmake_minimum_required(VERSION 3.20)
project(WonderTraderNanomsgPackageDependency LANGUAGES CXX)

if(NOT WT_DEPENDENCIES_MODULE)
	message(FATAL_ERROR "WT_DEPENDENCIES_MODULE is required")
endif()

set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/prefix")
set(WT_DEPS_ROOT "")
include("${WT_DEPENDENCIES_MODULE}")
WT_SETUP_NANOMSG_DEPENDENCY()

if(NOT TARGET WT::Nanomsg OR NOT TARGET nanomsg)
	message(FATAL_ERROR "WT::Nanomsg did not consume the config package target")
endif()

get_target_property(WT_NANOMSG_LINK_LIBRARIES WT_NANOMSG INTERFACE_LINK_LIBRARIES)
if(NOT "nanomsg" IN_LIST WT_NANOMSG_LINK_LIBRARIES)
	message(FATAL_ERROR "WT::Nanomsg does not publish the config package target")
endif()
