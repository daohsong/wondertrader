
#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtTester LANGUAGES CXX)



SET(LIBS
	WtBtCore
	WTSTools
	WTSUtils
)

IF(MSVC)
ELSE()
	LIST(APPEND LIBS
		${FS_LIB}
		WT::Platform
		)
	if(WIN32)
		LIST(APPEND LIBS iconv)
	ENDIF()
ENDIF()

#添加源码
SET(SRCS
	WtBtRunner.cpp
)
IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()

#9. 生成可执行文件
ADD_EXECUTABLE(WtBtRunner ${SRCS})
TARGET_LINK_LIBRARIES(WtBtRunner PRIVATE WT::fmt)
TARGET_LINK_LIBRARIES(WtBtRunner PRIVATE ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtBtRunner PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

#ADD_EXECUTABLE(TestBtPorter ../TestBtPorter/TestBtPorter.cpp)
#TARGET_LINK_LIBRARIES(TestBtPorter ${LIBS})

