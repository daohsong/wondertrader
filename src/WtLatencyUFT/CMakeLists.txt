#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtLatencyUFT LANGUAGES CXX)


#7. 添加源码
SET(SRCS
	UftLatencyTool.cpp
	main.cpp
)
SET(HDRS
	UftLatencyTool.h
)

IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()


SET(LIBS
	WtUftCore
	WTSTools
    WTSUtils
    WtShareHelper
	)

IF (MSVC)
	LIST(APPEND LIBS ws2_32)
ELSE()
	LIST(APPEND LIBS
		WT::Platform
		WT::BoostThread
		${FS_LIB})
	IF(WIN32)
		LIST(APPEND LIBS
			ws2_32 iconv)
	ENDIF()
ENDIF()

ADD_EXECUTABLE(WtLatencyUFT ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(WtLatencyUFT PRIVATE WT::fmt)
TARGET_LINK_LIBRARIES(WtLatencyUFT PRIVATE ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtLatencyUFT PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()
