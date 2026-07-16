#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(TraderDumper LANGUAGES CXX)


#7. 添加源码
SET(SRCS
	Dumper.cpp
	TraderAdapter.cpp
	TraderDumper.cpp
	WtHelper.cpp
)
SET(HDRS
	Dumper.h
	PorterDefs.h
	TraderAdapter.h
	TraderDumper.h
	WtHelper.h
)

IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()

ADD_LIBRARY(TraderDumper SHARED ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(TraderDumper PRIVATE WT::fmt)

SET(LIBS
	WTSTools
	WTSUtils
	)
IF(MSVC)
ELSE()
	LIST(APPEND LIBS
		WT::Platform
		${FS_LIB}
		WT::BoostThread
		)
	IF(WIN32)
		LIST(APPEND LIBS
			ws2_32 iconv
		)
	ENDIF()
ENDIF()

TARGET_LINK_LIBRARIES(TraderDumper PUBLIC ${LIBS})
IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(TraderDumper PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()