
#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtDtServo LANGUAGES CXX)


#7. 添加源码
SET(SRCS
	ParserAdapter.cpp
	WtDataManager.cpp
	WtDtRunner.cpp
	WtDtServo.cpp
	WtHelper.cpp
)
SET(HDRS
	ParserAdapter.h
	PorterDefs.h
	WtDataManager.h
	WtDtRunner.h
	WtDtServo.h
	WtHelper.h
)

IF(MSVC)
    list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()

ADD_LIBRARY(WtDtServo SHARED ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(WtDtServo PRIVATE WT::fmt)

SET(LIBS
    WTSTools
    WTSUtils
)
IF (MSVC)
ELSE()
    LIST(APPEND LIBS
        WT::Platform
        WT::BoostFilesystem
        )
    IF(WIN32)
        LIST(APPEND LIBS iconv)
    ENDIF()
ENDIF()

TARGET_LINK_LIBRARIES(WtDtServo PUBLIC ${LIBS})
IF (MSVC)
ELSE()
    SET_TARGET_PROPERTIES(WtDtServo PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()