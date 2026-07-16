cmake_minimum_required(VERSION 3.21)
project(WtWarningsPolicy LANGUAGES CXX)

if(NOT WT_WARNINGS_MODULE)
	message(FATAL_ERROR "WT_WARNINGS_MODULE is required")
endif()

include("${WT_WARNINGS_MODULE}")
WT_SETUP_WARNINGS()

if(NOT TARGET WT::Warnings)
	message(FATAL_ERROR "WT::Warnings alias was not created")
endif()

add_library(project_target STATIC project.cpp)
add_library(vendor_target STATIC vendor.cpp)
add_library(imported_target STATIC IMPORTED)

set(WT_WARNING_EXCLUDED_TARGETS vendor_target)
WT_CONFIGURE_WARNING_TARGET(project_target)
WT_CONFIGURE_WARNING_TARGET(vendor_target)
WT_CONFIGURE_WARNING_TARGET(imported_target)

get_target_property(WT_PROJECT_WARNINGS project_target WT_WARNINGS_ENABLED)
if(NOT WT_PROJECT_WARNINGS)
	message(FATAL_ERROR "Project target did not receive the warning policy")
endif()

get_target_property(WT_PROJECT_LINKS project_target LINK_LIBRARIES)
if(NOT WtWarnings IN_LIST WT_PROJECT_LINKS)
	message(FATAL_ERROR "Project target does not link WtWarnings: ${WT_PROJECT_LINKS}")
endif()

get_target_property(WT_VENDOR_EXCLUDED vendor_target WT_WARNINGS_EXCLUDED)
if(NOT WT_VENDOR_EXCLUDED)
	message(FATAL_ERROR "Excluded vendor target was not marked as excluded")
endif()

get_target_property(WT_VENDOR_LINKS vendor_target LINK_LIBRARIES)
if(WT_VENDOR_LINKS AND WtWarnings IN_LIST WT_VENDOR_LINKS)
	message(FATAL_ERROR "Excluded vendor target inherited WtWarnings")
endif()

get_target_property(WT_IMPORTED_LINKS imported_target INTERFACE_LINK_LIBRARIES)
if(WT_IMPORTED_LINKS AND WtWarnings IN_LIST WT_IMPORTED_LINKS)
	message(FATAL_ERROR "Imported target inherited WtWarnings")
endif()

get_target_property(WT_WARNING_OPTIONS WtWarnings INTERFACE_COMPILE_OPTIONS)
if(WT_ENABLE_WARNINGS AND NOT WT_WARNING_OPTIONS)
	message(FATAL_ERROR "Enabled warning policy has no compile options")
endif()
if(WT_WARNINGS_AS_ERRORS AND NOT "${WT_WARNING_OPTIONS}" MATCHES "(-Werror|/WX)")
	message(FATAL_ERROR "Warnings-as-errors did not propagate through WtWarnings")
endif()
