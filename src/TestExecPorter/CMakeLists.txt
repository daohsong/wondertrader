
#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(TestExecPorter LANGUAGES CXX)


#7. 添加源码
SET(SRCS
	main.cpp
)

SET(LIBS
	WtExecMon
)


LIST(APPEND LIBS
    WT::Platform
    )

ADD_EXECUTABLE(TestExecPorter ${SRCS})
TARGET_LINK_LIBRARIES(TestExecPorter ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(TestExecPorter PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()