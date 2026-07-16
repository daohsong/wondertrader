function(WT_VALIDATE_CXX_STANDARD WT_STANDARD)
	if(NOT "${WT_STANDARD}" MATCHES "^(17|20|23)$")
		message(FATAL_ERROR "WT_CXX_STANDARD must be one of: 17, 20, 23; got '${WT_STANDARD}'")
	endif()
endfunction()

macro(WT_SETUP_CXX_STANDARD)
	set(WT_CXX_STANDARD "17" CACHE STRING "C++ standard: 17, 20, or 23")
	set_property(CACHE WT_CXX_STANDARD PROPERTY STRINGS 17 20 23)
	WT_VALIDATE_CXX_STANDARD("${WT_CXX_STANDARD}")

	set(CMAKE_CXX_STANDARD "${WT_CXX_STANDARD}")
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)
endmacro()

function(WT_CONFIGURE_CXX_TARGET WT_TARGET)
	if(NOT TARGET "${WT_TARGET}")
		message(FATAL_ERROR "Cannot configure C++ standard for unknown target '${WT_TARGET}'")
	endif()

	get_target_property(WT_TARGET_IMPORTED "${WT_TARGET}" IMPORTED)
	if(WT_TARGET_IMPORTED)
		return()
	endif()

	get_target_property(WT_TARGET_TYPE "${WT_TARGET}" TYPE)
	if(WT_TARGET_TYPE STREQUAL "UTILITY")
		return()
	elseif(WT_TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
		target_compile_features("${WT_TARGET}" INTERFACE cxx_std_17)
		return()
	elseif(WT_TARGET_TYPE STREQUAL "EXECUTABLE")
		target_compile_features("${WT_TARGET}" PRIVATE cxx_std_17)
	else()
		# WonderTrader library headers are consumed throughout the source tree, so
		# publish the project's C++17 minimum to their consumers.
		target_compile_features("${WT_TARGET}" PUBLIC cxx_std_17)
	endif()

	if(MSVC)
		target_compile_options("${WT_TARGET}" PRIVATE
			"$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
		)
	endif()

	set_target_properties("${WT_TARGET}" PROPERTIES
		CXX_STANDARD "${WT_CXX_STANDARD}"
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
	)
endfunction()

function(WT_CONFIGURE_CXX_TARGETS_IN_DIRECTORY WT_DIRECTORY WT_OUTPUT_COUNT)
	set(WT_CONFIGURED_COUNT 0)

	get_property(WT_DIRECTORY_TARGETS DIRECTORY "${WT_DIRECTORY}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(WT_TARGET IN LISTS WT_DIRECTORY_TARGETS)
		get_target_property(WT_TARGET_TYPE "${WT_TARGET}" TYPE)
		get_target_property(WT_TARGET_IMPORTED "${WT_TARGET}" IMPORTED)
		if(NOT WT_TARGET_IMPORTED AND NOT WT_TARGET_TYPE STREQUAL "UTILITY")
			WT_CONFIGURE_CXX_TARGET("${WT_TARGET}")
			math(EXPR WT_CONFIGURED_COUNT "${WT_CONFIGURED_COUNT} + 1")
		endif()
	endforeach()

	get_property(WT_SUBDIRECTORIES DIRECTORY "${WT_DIRECTORY}" PROPERTY SUBDIRECTORIES)
	foreach(WT_SUBDIRECTORY IN LISTS WT_SUBDIRECTORIES)
		WT_CONFIGURE_CXX_TARGETS_IN_DIRECTORY("${WT_SUBDIRECTORY}" WT_SUBDIRECTORY_TARGET_COUNT)
		math(EXPR WT_CONFIGURED_COUNT "${WT_CONFIGURED_COUNT} + ${WT_SUBDIRECTORY_TARGET_COUNT}")
	endforeach()

	set(${WT_OUTPUT_COUNT} "${WT_CONFIGURED_COUNT}" PARENT_SCOPE)
endfunction()
