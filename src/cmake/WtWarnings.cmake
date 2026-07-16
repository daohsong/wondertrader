include(CheckCXXCompilerFlag)

function(WT_APPEND_SUPPORTED_CXX_WARNING WT_OUTPUT WT_FLAG)
	string(MAKE_C_IDENTIFIER "${WT_FLAG}" WT_FLAG_ID)
	set(WT_FLAG_CACHE "WT_CXX_WARNING_SUPPORTED_${WT_FLAG_ID}")
	check_cxx_compiler_flag("${WT_FLAG}" "${WT_FLAG_CACHE}")
	set(WT_FLAGS ${${WT_OUTPUT}})
	if(${WT_FLAG_CACHE})
		list(APPEND WT_FLAGS "${WT_FLAG}")
	endif()
	set(${WT_OUTPUT} "${WT_FLAGS}" PARENT_SCOPE)
endfunction()

macro(WT_SETUP_WARNINGS)
	option(WT_ENABLE_WARNINGS "Enable the WonderTrader project warning policy" ON)
	option(WT_WARNINGS_AS_ERRORS "Treat enabled WonderTrader warnings as errors" OFF)
	set(WT_WARNING_LEVEL "BASELINE" CACHE STRING "Warning policy level: BASELINE or STRICT")
	set_property(CACHE WT_WARNING_LEVEL PROPERTY STRINGS BASELINE STRICT)
	string(TOUPPER "${WT_WARNING_LEVEL}" WT_WARNING_LEVEL_VALUE)
	if(NOT WT_WARNING_LEVEL_VALUE STREQUAL "BASELINE" AND
		NOT WT_WARNING_LEVEL_VALUE STREQUAL "STRICT")
		message(FATAL_ERROR "WT_WARNING_LEVEL must be BASELINE or STRICT; got '${WT_WARNING_LEVEL}'")
	endif()

	add_library(WtWarnings INTERFACE)
	add_library(WT::Warnings ALIAS WtWarnings)

	set(WT_WARNING_FLAGS "")
	if(WT_ENABLE_WARNINGS)
		if(MSVC)
			list(APPEND WT_WARNING_FLAGS /W3)
			if(WT_WARNING_LEVEL_VALUE STREQUAL "STRICT")
				list(APPEND WT_WARNING_FLAGS /W4 /permissive-)
			endif()
			if(WT_WARNINGS_AS_ERRORS)
				list(APPEND WT_WARNING_FLAGS /WX)
			endif()
		else()
			foreach(WT_WARNING_FLAG IN ITEMS
				-Wdeprecated-declarations
				-Wswitch
				-Wparentheses
				-Wformat
				-Wvla
			)
				WT_APPEND_SUPPORTED_CXX_WARNING(WT_WARNING_FLAGS "${WT_WARNING_FLAG}")
			endforeach()

			if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
				foreach(WT_WARNING_FLAG IN ITEMS
					-Winconsistent-missing-override
					-Wnontrivial-memcall
					-Wdelete-abstract-non-virtual-dtor
					-Wpointer-bool-conversion
					-Wvla-cxx-extension
				)
					WT_APPEND_SUPPORTED_CXX_WARNING(WT_WARNING_FLAGS "${WT_WARNING_FLAG}")
				endforeach()
			endif()

			if(WT_WARNING_LEVEL_VALUE STREQUAL "STRICT")
				foreach(WT_WARNING_FLAG IN ITEMS
					-Wall
					-Wextra
					-Wpedantic
					-Wformat=2
					-Wconversion
					-Wshadow
				)
					WT_APPEND_SUPPORTED_CXX_WARNING(WT_WARNING_FLAGS "${WT_WARNING_FLAG}")
				endforeach()
			endif()

			if(WT_WARNINGS_AS_ERRORS)
				list(APPEND WT_WARNING_FLAGS -Werror)
			endif()
		endif()
	endif()

	foreach(WT_WARNING_FLAG IN LISTS WT_WARNING_FLAGS)
		target_compile_options(WtWarnings INTERFACE
			"$<$<COMPILE_LANGUAGE:CXX>:${WT_WARNING_FLAG}>"
		)
	endforeach()
endmacro()

function(WT_CONFIGURE_WARNING_TARGET WT_TARGET)
	if(NOT TARGET "${WT_TARGET}")
		message(FATAL_ERROR "Cannot configure warnings for unknown target '${WT_TARGET}'")
	endif()

	get_target_property(WT_TARGET_IMPORTED "${WT_TARGET}" IMPORTED)
	get_target_property(WT_TARGET_TYPE "${WT_TARGET}" TYPE)
	if(WT_TARGET_IMPORTED OR WT_TARGET_TYPE STREQUAL "UTILITY" OR
		WT_TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
		return()
	endif()

	get_target_property(WT_TARGET_SOURCE_DIR "${WT_TARGET}" SOURCE_DIR)
	file(REAL_PATH "${PROJECT_SOURCE_DIR}" WT_PROJECT_SOURCE_ROOT)
	file(REAL_PATH "${WT_TARGET_SOURCE_DIR}" WT_TARGET_SOURCE_ROOT)
	string(FIND "${WT_TARGET_SOURCE_ROOT}/" "${WT_PROJECT_SOURCE_ROOT}/" WT_SOURCE_PREFIX)
	if(NOT WT_SOURCE_PREFIX EQUAL 0)
		return()
	endif()

	list(FIND WT_WARNING_EXCLUDED_TARGETS "${WT_TARGET}" WT_EXCLUDED_INDEX)
	if(NOT WT_EXCLUDED_INDEX EQUAL -1)
		set_property(TARGET "${WT_TARGET}" PROPERTY WT_WARNINGS_EXCLUDED TRUE)
		return()
	endif()

	# Append the interface dependency through the target property so this policy
	# remains compatible with legacy targets that use the plain
	# target_link_libraries signature.
	set_property(TARGET "${WT_TARGET}" APPEND PROPERTY LINK_LIBRARIES WtWarnings)
	set_property(TARGET "${WT_TARGET}" PROPERTY WT_WARNINGS_ENABLED TRUE)
endfunction()

function(WT_CONFIGURE_WARNING_TARGETS_IN_DIRECTORY WT_DIRECTORY WT_OUTPUT_COUNT)
	set(WT_CONFIGURED_COUNT 0)

	get_property(WT_DIRECTORY_TARGETS DIRECTORY "${WT_DIRECTORY}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(WT_TARGET IN LISTS WT_DIRECTORY_TARGETS)
		WT_CONFIGURE_WARNING_TARGET("${WT_TARGET}")
		get_target_property(WT_WARNINGS_ENABLED "${WT_TARGET}" WT_WARNINGS_ENABLED)
		if(WT_WARNINGS_ENABLED)
			math(EXPR WT_CONFIGURED_COUNT "${WT_CONFIGURED_COUNT} + 1")
		endif()
	endforeach()

	get_property(WT_SUBDIRECTORIES DIRECTORY "${WT_DIRECTORY}" PROPERTY SUBDIRECTORIES)
	foreach(WT_SUBDIRECTORY IN LISTS WT_SUBDIRECTORIES)
		WT_CONFIGURE_WARNING_TARGETS_IN_DIRECTORY("${WT_SUBDIRECTORY}" WT_SUBDIRECTORY_TARGET_COUNT)
		math(EXPR WT_CONFIGURED_COUNT "${WT_CONFIGURED_COUNT} + ${WT_SUBDIRECTORY_TARGET_COUNT}")
	endforeach()

	set(${WT_OUTPUT_COUNT} "${WT_CONFIGURED_COUNT}" PARENT_SCOPE)
endfunction()
