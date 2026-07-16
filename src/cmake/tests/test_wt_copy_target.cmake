cmake_minimum_required(VERSION 3.20)

if(NOT WT_COPY_TARGET_HELPER OR NOT WT_COPY_TARGET_TEST_ROOT)
	message(FATAL_ERROR "WT_COPY_TARGET_HELPER and WT_COPY_TARGET_TEST_ROOT are required")
endif()

set(WT_COPY_TARGET_TEST_SOURCE_DIR "${WT_COPY_TARGET_TEST_ROOT}/src")
set(WT_COPY_TARGET_TEST_BINARY_DIR "${WT_COPY_TARGET_TEST_ROOT}/build")
file(REMOVE_RECURSE "${WT_COPY_TARGET_TEST_ROOT}")
file(MAKE_DIRECTORY "${WT_COPY_TARGET_TEST_SOURCE_DIR}")
file(WRITE "${WT_COPY_TARGET_TEST_SOURCE_DIR}/producer.cpp" "int wt_copy_target_value() { return 42; }\n")
file(WRITE "${WT_COPY_TARGET_TEST_SOURCE_DIR}/consumer.cpp" "int main() { return 0; }\n")
file(WRITE "${WT_COPY_TARGET_TEST_SOURCE_DIR}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(WtCopyTargetTest LANGUAGES CXX)
set(WT_ARCH x64)
include("@WT_COPY_TARGET_HELPER@")
add_library(producer SHARED producer.cpp)
wt_set_target_output_directory(producer WtProducer)
add_executable(consumer consumer.cpp)
wt_set_target_output_directory(consumer WtConsumer)
wt_make_target_subdirectories(consumer plugins logs)
wt_add_deployment_target(consumer_deploy consumer MANAGED_SUBDIRECTORIES plugins)
wt_copy_and_deploy_target_if_exists(consumer consumer_deploy producer plugins)
]=])
file(READ "${WT_COPY_TARGET_TEST_SOURCE_DIR}/CMakeLists.txt" WT_COPY_TARGET_TEST_CMAKELISTS)
string(CONFIGURE "${WT_COPY_TARGET_TEST_CMAKELISTS}" WT_COPY_TARGET_TEST_CMAKELISTS @ONLY)
file(WRITE "${WT_COPY_TARGET_TEST_SOURCE_DIR}/CMakeLists.txt" "${WT_COPY_TARGET_TEST_CMAKELISTS}")

execute_process(
	COMMAND "${CMAKE_COMMAND}" -S "${WT_COPY_TARGET_TEST_SOURCE_DIR}" -B "${WT_COPY_TARGET_TEST_BINARY_DIR}" -DCMAKE_BUILD_TYPE=Release
	RESULT_VARIABLE WT_COPY_TARGET_CONFIGURE_RESULT
	OUTPUT_VARIABLE WT_COPY_TARGET_CONFIGURE_OUTPUT
	ERROR_VARIABLE WT_COPY_TARGET_CONFIGURE_ERROR
)
if(NOT WT_COPY_TARGET_CONFIGURE_RESULT EQUAL 0)
	message(FATAL_ERROR "WtCopyTarget configure failed:\n${WT_COPY_TARGET_CONFIGURE_OUTPUT}\n${WT_COPY_TARGET_CONFIGURE_ERROR}")
endif()
execute_process(
	COMMAND "${CMAKE_COMMAND}" --build "${WT_COPY_TARGET_TEST_BINARY_DIR}" --target consumer
	RESULT_VARIABLE WT_COPY_TARGET_BUILD_RESULT
	OUTPUT_VARIABLE WT_COPY_TARGET_BUILD_OUTPUT
	ERROR_VARIABLE WT_COPY_TARGET_BUILD_ERROR
)
if(NOT WT_COPY_TARGET_BUILD_RESULT EQUAL 0)
	message(FATAL_ERROR "WtCopyTarget build failed:\n${WT_COPY_TARGET_BUILD_OUTPUT}\n${WT_COPY_TARGET_BUILD_ERROR}")
endif()

set(WT_COPY_TARGET_CONSUMER_DIR "${WT_COPY_TARGET_TEST_BINARY_DIR}/build_x64/Release/bin/WtConsumer")
set(WT_COPY_TARGET_PRODUCER_DIR "${WT_COPY_TARGET_TEST_BINARY_DIR}/build_x64/Release/bin/WtProducer")
foreach(WT_EXPECTED_DIR IN ITEMS
	"${WT_COPY_TARGET_CONSUMER_DIR}"
	"${WT_COPY_TARGET_PRODUCER_DIR}"
	"${WT_COPY_TARGET_CONSUMER_DIR}/plugins"
	"${WT_COPY_TARGET_CONSUMER_DIR}/logs"
)
	if(NOT IS_DIRECTORY "${WT_EXPECTED_DIR}")
		message(FATAL_ERROR "Expected output directory was not created: ${WT_EXPECTED_DIR}")
	endif()
endforeach()
file(GLOB WT_CONSUMER_FILES "${WT_COPY_TARGET_CONSUMER_DIR}/*consumer*")
file(GLOB WT_PRODUCER_FILES "${WT_COPY_TARGET_PRODUCER_DIR}/*producer*")
file(GLOB WT_COPIED_FILES "${WT_COPY_TARGET_CONSUMER_DIR}/plugins/*producer*")
foreach(WT_EXPECTED_FILES IN ITEMS WT_CONSUMER_FILES WT_PRODUCER_FILES WT_COPIED_FILES)
	if(NOT ${WT_EXPECTED_FILES})
		message(FATAL_ERROR "Expected artifact set is empty: ${WT_EXPECTED_FILES}")
	endif()
endforeach()

file(WRITE "${WT_COPY_TARGET_CONSUMER_DIR}/plugins/stale-plugin" "stale\n")
file(WRITE "${WT_COPY_TARGET_TEST_SOURCE_DIR}/producer.cpp" "int wt_copy_target_value() { return 43; }\n")
execute_process(
	COMMAND "${CMAKE_COMMAND}" --build "${WT_COPY_TARGET_TEST_BINARY_DIR}" --target consumer_deploy
	RESULT_VARIABLE WT_COPY_TARGET_DEPLOY_RESULT
	OUTPUT_VARIABLE WT_COPY_TARGET_DEPLOY_OUTPUT
	ERROR_VARIABLE WT_COPY_TARGET_DEPLOY_ERROR
)
if(NOT WT_COPY_TARGET_DEPLOY_RESULT EQUAL 0)
	message(FATAL_ERROR "WtCopyTarget deployment failed:\n${WT_COPY_TARGET_DEPLOY_OUTPUT}\n${WT_COPY_TARGET_DEPLOY_ERROR}")
endif()
if(EXISTS "${WT_COPY_TARGET_CONSUMER_DIR}/plugins/stale-plugin")
	message(FATAL_ERROR "Development deployment retained a stale managed plugin")
endif()
file(GLOB WT_UPDATED_PRODUCER_FILES "${WT_COPY_TARGET_PRODUCER_DIR}/*producer*")
file(GLOB WT_UPDATED_COPIED_FILES "${WT_COPY_TARGET_CONSUMER_DIR}/plugins/*producer*")
list(GET WT_UPDATED_PRODUCER_FILES 0 WT_UPDATED_PRODUCER)
list(GET WT_UPDATED_COPIED_FILES 0 WT_UPDATED_COPY)
file(SHA256 "${WT_UPDATED_PRODUCER}" WT_UPDATED_PRODUCER_HASH)
file(SHA256 "${WT_UPDATED_COPY}" WT_UPDATED_COPY_HASH)
if(NOT WT_UPDATED_PRODUCER_HASH STREQUAL WT_UPDATED_COPY_HASH)
	message(FATAL_ERROR "Development deployment did not refresh the producer artifact")
endif()
