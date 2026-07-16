
#1. 确定CMake的最低版本需求
cmake_minimum_required(VERSION 3.20)

#2. 确定工程名
project(WtUftCore LANGUAGES CXX)

#7. 添加源码
SET(SRCS
	ActionPolicyMgr.cpp
	EventNotifier.cpp
	ParserAdapter.cpp
	ShareManager.cpp
	TraderAdapter.cpp
	UftStraContext.cpp
	UftStrategyMgr.cpp
	WtHelper.cpp
	WtUftDtMgr.cpp
	WtUftEngine.cpp
	WtUftTicker.cpp
)
SET(HDRS
	ActionPolicyMgr.h
	EventNotifier.h
	ITrdNotifySink.h
	ParserAdapter.h
	ShareManager.h
	TraderAdapter.h
	UftDataDefs.h
	UftStraContext.h
	UftStrategyMgr.h
	WtHelper.h
	WtUftDtMgr.h
	WtUftEngine.h
	WtUftTicker.h
)


ADD_LIBRARY(WtUftCore STATIC ${SRCS} ${HDRS})
SET_TARGET_PROPERTIES(WtUftCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
TARGET_LINK_LIBRARIES(WtUftCore
	PUBLIC WT::fmt ${FS_LIB}
	PRIVATE WT::RapidJSON
)
