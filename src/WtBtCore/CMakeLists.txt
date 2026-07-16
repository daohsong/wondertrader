
#1. 确定CMake的最低版本需求
cmake_minimum_required(VERSION 3.20)

#2. 确定工程名
project(WtBtCore LANGUAGES CXX)

IF(MSVC)
ELSE(GNUCC)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
ENDIF()

SET(LIBRARY_OUTPUT_PATH  ${CMAKE_BINARY_DIR}/build_${PLATFORM}/${CMAKE_BUILD_TYPE}/libs)

#7. 添加源码
file(GLOB SRCS *.cpp)
file(GLOB HDRS *.h)


ADD_LIBRARY(WtBtCore STATIC ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(WtBtCore PUBLIC WT::fmt WT::Boost PRIVATE WT::RapidJSON)
