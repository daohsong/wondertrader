
#1. 确定CMake的最低版本需求
cmake_minimum_required(VERSION 3.20)

#2. 确定工程名
project(WtDtCore LANGUAGES CXX)

#7. 添加源码
SET(SRCS
	DataManager.cpp
	IndexWorkerMgr.cpp
	ParserAdapter.cpp
	ShmCaster.cpp
	StateMonitor.cpp
	UDPCaster.cpp
	WtHelper.cpp
)
SET(HDRS
	DataManager.h
	IDataCaster.h
	IndexWorkerMgr.h
	ParserAdapter.h
	ShmCaster.h
	StatHelper.hpp
	StateMonitor.h
	UDPCaster.h
	WtHelper.h
)


ADD_LIBRARY(WtDtCore STATIC ${SRCS} ${HDRS})
SET_TARGET_PROPERTIES(WtDtCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
TARGET_LINK_LIBRARIES(WtDtCore
	PUBLIC WT::BoostFilesystem WT::BoostThread WT::BoostAtomic
	PRIVATE WT::fmt
)
