cmake_minimum_required(VERSION 3.20)
project(WonderTraderRuntimeVendorDependency LANGUAGES CXX)

if(NOT WT_DEPENDENCIES_MODULE OR NOT WT_VENDOR_TEST_MODE)
	message(FATAL_ERROR "WT_DEPENDENCIES_MODULE and WT_VENDOR_TEST_MODE are required")
endif()

if(WIN32)
	set(WT_SYSTEM windows)
	set(WT_HOST_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdk/x64")
	set(WT_HOST_RUNTIME_PREFIX "")
	set(WT_HOST_RUNTIME_SUFFIX ".dll")
elseif(APPLE)
	set(WT_SYSTEM macos)
	set(WT_HOST_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdk/macos/${CMAKE_SYSTEM_PROCESSOR}")
	set(WT_HOST_RUNTIME_PREFIX "lib")
	set(WT_HOST_RUNTIME_SUFFIX ".dylib")
else()
	set(WT_SYSTEM linux)
	set(WT_HOST_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdk/linux")
	set(WT_HOST_RUNTIME_PREFIX "lib")
	set(WT_HOST_RUNTIME_SUFFIX ".so")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" WT_HOST_PROCESSOR)
if(WT_HOST_PROCESSOR MATCHES "^(x86_64|amd64|x64)$")
	set(WT_ARCH x64)
	set(WT_OPPOSITE_ARCH arm64)
elseif(WT_HOST_PROCESSOR MATCHES "^(aarch64|arm64)$")
	set(WT_ARCH arm64)
	set(WT_OPPOSITE_ARCH x64)
else()
	message(FATAL_ERROR "Unsupported fixture host architecture ${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(WT_VENDOR_ROOT "${CMAKE_CURRENT_BINARY_DIR}/sdk")
file(MAKE_DIRECTORY "${WT_VENDOR_ROOT}/include")
file(WRITE "${WT_VENDOR_ROOT}/include/vendor_md.h" "#pragma once\n")
file(WRITE "${WT_VENDOR_ROOT}/include/vendor_trader.h" "#pragma once\n")

if(WT_VENDOR_TEST_MODE STREQUAL "available" OR WT_VENDOR_TEST_MODE STREQUAL "wrong_arch")
	file(MAKE_DIRECTORY "${WT_HOST_RUNTIME_DIR}")
	foreach(WT_RUNTIME_BASE IN ITEMS vendor_md vendor_trader)
		configure_file(
			"${CMAKE_COMMAND}"
			"${WT_HOST_RUNTIME_DIR}/${WT_HOST_RUNTIME_PREFIX}${WT_RUNTIME_BASE}${WT_HOST_RUNTIME_SUFFIX}"
			COPYONLY
		)
	endforeach()
	if(WIN32)
		# The fixture only configures the imported target, so an architecture-
		# valid placeholder is sufficient to exercise import-library discovery.
		configure_file(
			"${CMAKE_COMMAND}"
			"${WT_HOST_RUNTIME_DIR}/vendor_trader.lib"
			COPYONLY
		)
	endif()
endif()

if(WT_VENDOR_TEST_MODE STREQUAL "wrong_arch")
	set(WT_ARCH "${WT_OPPOSITE_ARCH}")
	set(WT_VENDOR_ENABLE ON)
elseif(WT_VENDOR_TEST_MODE STREQUAL "missing_on")
	set(WT_VENDOR_ENABLE ON)
else()
	set(WT_VENDOR_ENABLE AUTO)
endif()

include("${WT_DEPENDENCIES_MODULE}")
WT_SETUP_RUNTIME_VENDOR_DEPENDENCY(
	NAME Fixture
	PREFIX WT_FIXTURE
	ENABLE "${WT_VENDOR_ENABLE}"
	ROOT "${WT_VENDOR_ROOT}"
	HEADERS vendor_md.h vendor_trader.h
	RUNTIME_BASENAMES vendor_md vendor_trader
	LINK_BASENAMES vendor_trader
	OUT_BUILD WT_BUILD_FIXTURE
)

if(WT_VENDOR_TEST_MODE STREQUAL "available")
	if(NOT WT_BUILD_FIXTURE OR NOT TARGET WT::Vendor::Fixture)
		message(FATAL_ERROR "Available runtime vendor was not enabled")
	endif()
	get_target_property(WT_FIXTURE_INCLUDES WT_VENDOR_Fixture INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
	if(NOT "${WT_VENDOR_ROOT}/include" IN_LIST WT_FIXTURE_INCLUDES)
		message(FATAL_ERROR "Runtime vendor target does not publish its include directory")
	endif()
	get_target_property(WT_FIXTURE_RUNTIMES WT_VENDOR_Fixture WT_RUNTIME_ARTIFACTS)
	list(LENGTH WT_FIXTURE_RUNTIMES WT_FIXTURE_RUNTIME_COUNT)
	if(NOT WT_FIXTURE_RUNTIME_COUNT EQUAL 2)
		message(FATAL_ERROR "Runtime vendor target does not publish both runtime artifacts")
	endif()
	get_target_property(WT_FIXTURE_LINKS WT_VENDOR_Fixture INTERFACE_LINK_LIBRARIES)
	list(LENGTH WT_FIXTURE_LINKS WT_FIXTURE_LINK_COUNT)
	if(NOT WT_FIXTURE_LINK_COUNT EQUAL 1)
		message(FATAL_ERROR "Runtime vendor target does not publish its imported link target")
	endif()
	list(GET WT_FIXTURE_LINKS 0 WT_FIXTURE_LINK_TARGET)
	if(NOT TARGET ${WT_FIXTURE_LINK_TARGET})
		message(FATAL_ERROR "Runtime vendor link entry is not a target: ${WT_FIXTURE_LINK_TARGET}")
	endif()
	get_target_property(WT_FIXTURE_LINK_LOCATION ${WT_FIXTURE_LINK_TARGET} IMPORTED_LOCATION)
	if(NOT WT_FIXTURE_LINK_LOCATION STREQUAL
		"${WT_HOST_RUNTIME_DIR}/${WT_HOST_RUNTIME_PREFIX}vendor_trader${WT_HOST_RUNTIME_SUFFIX}")
		message(FATAL_ERROR "Unexpected imported runtime location: ${WT_FIXTURE_LINK_LOCATION}")
	endif()
	if(WIN32)
		get_target_property(WT_FIXTURE_IMPLIB ${WT_FIXTURE_LINK_TARGET} IMPORTED_IMPLIB)
		if(NOT WT_FIXTURE_IMPLIB STREQUAL "${WT_HOST_RUNTIME_DIR}/vendor_trader.lib")
			message(FATAL_ERROR "Unexpected imported library: ${WT_FIXTURE_IMPLIB}")
		endif()
	elseif(WT_SYSTEM STREQUAL "linux")
		get_target_property(WT_FIXTURE_NO_SONAME ${WT_FIXTURE_LINK_TARGET} IMPORTED_NO_SONAME)
		if(NOT WT_FIXTURE_NO_SONAME)
			message(FATAL_ERROR "Linux imported vendor library must suppress absolute DT_NEEDED paths")
		endif()
	endif()
elseif(WT_VENDOR_TEST_MODE STREQUAL "missing_auto")
	if(WT_BUILD_FIXTURE OR TARGET WT::Vendor::Fixture)
		message(FATAL_ERROR "AUTO mode enabled a vendor with missing runtime artifacts")
	endif()
endif()
