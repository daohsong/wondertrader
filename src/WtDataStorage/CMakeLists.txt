#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtDataStorage LANGUAGES CXX)


#7. 添加源码
SET(SRCS
	WtBtDtReader.cpp
	WtDataReader.cpp
	WtDataWriter.cpp
	WtRdmDtReader.cpp
)
SET(HDRS
	DataDefine.h
	WtBtDtReader.h
	WtDataReader.h
	WtDataWriter.h
	WtRdmDtReader.h
)

ADD_LIBRARY(WtDataStorage SHARED ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(WtDataStorage PUBLIC WT::fmt PRIVATE WT::RapidJSON)

SET(LIBS
	WTSUtils
)

IF (MSVC)
ELSE()
	LIST(APPEND LIBS
		WT::Platform
		${FS_LIB}
		)
	if(WIN32)
		LIST(APPEND LIBS iconv)
	ENDIF()
ENDIF()
TARGET_LINK_LIBRARIES(WtDataStorage PUBLIC ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtDataStorage PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

