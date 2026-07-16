
#1. 确定CMake的最低版本需求
cmake_minimum_required(VERSION 3.20)

#2. 确定工程名
project(WTSTools LANGUAGES CXX)

#7. 添加源码
SET(SRCS
	CsvHelper.cpp
	WTSBaseDataMgr.cpp
	WTSDataFactory.cpp
	WTSHotMgr.cpp
	WTSLogger.cpp
)
SET(HDRS
	CsvHelper.h
	WTSBaseDataMgr.h
	WTSDataFactory.h
	WTSHotMgr.h
	WTSLogger.h
)

ADD_LIBRARY(WTSTools ${SRCS} ${HDRS})
SET_TARGET_PROPERTIES(WTSTools PROPERTIES POSITION_INDEPENDENT_CODE ON)
TARGET_INCLUDE_DIRECTORIES(WTSTools PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../WTSUtils/yamlcpp")
TARGET_LINK_LIBRARIES(WTSTools PUBLIC WT::fmt WT::Boost)
