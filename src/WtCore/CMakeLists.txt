
#1. 确定CMake的最低版本需求
cmake_minimum_required(VERSION 3.20)

#2. 确定工程名
project(WtCore LANGUAGES CXX)

#7. 添加源码
SET(SRCS
	ActionPolicyMgr.cpp
	CtaStraBaseCtx.cpp
	CtaStraContext.cpp
	CtaStrategyMgr.cpp
	EventNotifier.cpp
	HftStraBaseCtx.cpp
	HftStraContext.cpp
	HftStrategyMgr.cpp
	ParserAdapter.cpp
	SelStraBaseCtx.cpp
	SelStraContext.cpp
	SelStrategyMgr.cpp
	TraderAdapter.cpp
	WtArbiExecuter.cpp
	WtCtaEngine.cpp
	WtCtaTicker.cpp
	WtDiffExecuter.cpp
	WtDistExecuter.cpp
	WtDtMgr.cpp
	WtEngine.cpp
	WtExecMgr.cpp
	WtExecuterFactory.cpp
	WtFilterMgr.cpp
	WtHelper.cpp
	WtHftEngine.cpp
	WtHftTicker.cpp
	WtLocalExecuter.cpp
	WtSelEngine.cpp
	WtSelTicker.cpp
)
SET(HDRS
	ActionPolicyMgr.h
	CtaStraBaseCtx.h
	CtaStraContext.h
	CtaStrategyMgr.h
	EventNotifier.h
	HftStraBaseCtx.h
	HftStraContext.h
	HftStrategyMgr.h
	IExecCommand.h
	ITrdNotifySink.h
	ParserAdapter.h
	SelStraBaseCtx.h
	SelStraContext.h
	SelStrategyMgr.h
	TraderAdapter.h
	WtArbiExecuter.h
	WtCtaEngine.h
	WtCtaTicker.h
	WtDiffExecuter.h
	WtDistExecuter.h
	WtDtMgr.h
	WtEngine.h
	WtExecMgr.h
	WtExecuterFactory.h
	WtFilterMgr.h
	WtHelper.h
	WtHftEngine.h
	WtHftTicker.h
	WtLocalExecuter.h
	WtSelEngine.h
	WtSelTicker.h
)


ADD_LIBRARY(WtCore STATIC ${SRCS} ${HDRS})
SET_TARGET_PROPERTIES(WtCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
TARGET_LINK_LIBRARIES(WtCore
	PUBLIC WT::fmt ${FS_LIB} WT::BoostThread
	PRIVATE WT::RapidJSON
)
