#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtLatencyHFT LANGUAGES CXX)


#7. 添加源码
SET(SRCS
	HftLatencyTool.cpp
	main.cpp
)
SET(HDRS
	HftLatencyTool.h
)

IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()


SET(LIBS
	WtCore
	WTSTools
    WTSUtils
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

ADD_EXECUTABLE(WtLatencyHFT ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(WtLatencyHFT PRIVATE WT::fmt)
TARGET_LINK_LIBRARIES(WtLatencyHFT PRIVATE ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtLatencyHFT PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

