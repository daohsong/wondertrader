
#1. 确定CMake的最低版本需求
cmake_minimum_required(VERSION 3.20)

#2. 确定工程名
project(WtBtCore LANGUAGES CXX)

#7. 添加源码
SET(SRCS
	CtaMocker.cpp
	EventNotifier.cpp
	ExecMocker.cpp
	HftMocker.cpp
	HisDataMgr.cpp
	HisDataReplayer.cpp
	MatchEngine.cpp
	SelMocker.cpp
	UftMocker.cpp
	WtHelper.cpp
)
SET(HDRS
	CtaMocker.h
	EventNotifier.h
	ExecMocker.h
	HftMocker.h
	HisDataMgr.h
	HisDataReplayer.h
	MatchEngine.h
	SelMocker.h
	UftMocker.h
	WtHelper.h
)


ADD_LIBRARY(WtBtCore STATIC ${SRCS} ${HDRS})
SET_TARGET_PROPERTIES(WtBtCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
TARGET_LINK_LIBRARIES(WtBtCore PUBLIC WT::fmt WT::Boost PRIVATE WT::RapidJSON)
