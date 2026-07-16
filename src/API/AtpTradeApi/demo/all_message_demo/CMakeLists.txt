cmake_minimum_required(VERSION 3.20)
project(trade_api_demo)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/WtCxxStandard.cmake")
WT_SETUP_CXX_STANDARD()

message(STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message(STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})

if (MSVC)
  add_definitions(/MP)
  add_definitions(/wd4244 /wd4267 /wd4018 /wd4355 /wd4800 /wd4251 /wd4996 /wd4146 /wd4305 /wd4819 /wd4275)

  add_compile_options(/wd4828)
  add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
 
endif (MSVC)


file(GLOB_RECURSE DEMO_INCLUDE "${PROJECT_SOURCE_DIR}/../../include/*.h")
source_group("trade_api_demo" FILES  ${DEMO_INCLUDE})
AUX_SOURCE_DIRECTORY(. TRADE_API_DEMO_SRC_LIST)
add_executable(trade_api_demo  ${DEMO_INCLUDE}  ${TRADE_API_DEMO_SRC_LIST})
WT_CONFIGURE_CXX_TARGET(trade_api_demo)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
     file(GLOB_RECURSE LINK_SO "${PROJECT_SOURCE_DIR}/../../lib/*.lib")
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
     file(GLOB_RECURSE LINK_SO "${PROJECT_SOURCE_DIR}/../../lib/*.so")
endif()

target_link_libraries(trade_api_demo  ${LINK_SO} )
target_include_directories(trade_api_demo PRIVATE ${PROJECT_SOURCE_DIR}/../../include/  ${PROJECT_SOURCE_DIR}/../../include/trade )
target_compile_options(trade_api_demo PRIVATE $<$<CXX_COMPILER_ID:MSVC>:  /Zi /Od>)
target_link_options(trade_api_demo PRIVATE $<$<CXX_COMPILER_ID:MSVC>: /DEBUG >)

