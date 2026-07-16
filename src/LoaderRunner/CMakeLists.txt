
#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(LoaderRunner LANGUAGES CXX)



SET(LIBS )

IF(MSVC)
ELSE()
	LIST(APPEND LIBS
		WT::Platform)
	if(WIN32)
		LIST(APPEND LIBS iconv)
	ENDIF()
ENDIF()

#添加源码
SET(SRCS
	LoaderRunner.cpp
)
#9. 生成可执行文件
ADD_EXECUTABLE(LoaderRunner ${SRCS})
TARGET_LINK_LIBRARIES(LoaderRunner PRIVATE WT::fmt)
TARGET_LINK_LIBRARIES(LoaderRunner PRIVATE ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(LoaderRunner PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

#ADD_EXECUTABLE(TestBtPorter ../TestBtPorter/TestBtPorter.cpp)
#TARGET_LINK_LIBRARIES(TestBtPorter ${LIBS})

