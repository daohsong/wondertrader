
#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtIndexFactory LANGUAGES CXX)

#7. 添加源码
SET(SRCS
	WtBlkIdxWorker.cpp
	WtIndexFactory.cpp
)
SET(HDRS
	WtBlkIdxWorker.h
	WtIndexFactory.h
)


ADD_LIBRARY(WtIndexFactory SHARED ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(WtIndexFactory PRIVATE WT::fmt WT::Boost)

SET(LIBS
	${FS_LIB}
	WT::Platform
)

IF (WIN32)
ELSE (UNIX)
	TARGET_LINK_LIBRARIES(WtIndexFactory PUBLIC ${LIBS})
	SET_TARGET_PROPERTIES(WtIndexFactory PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

