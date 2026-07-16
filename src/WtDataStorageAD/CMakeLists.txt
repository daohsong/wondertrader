#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtDataStorageAD LANGUAGES CXX)


#7. 添加源码
SET(SRCS
	WtBtDtReaderAD.cpp
	WtDataReaderAD.cpp
	WtDataWriterAD.cpp
	WtRdmDtReaderAD.cpp
)
SET(HDRS
	DataDefineAD.h
	LMDBKeys.h
	WtBtDtReaderAD.h
	WtDataReaderAD.h
	WtDataWriterAD.h
	WtRdmDtReaderAD.h
)

ADD_LIBRARY(WtDataStorageAD SHARED ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(WtDataStorageAD PRIVATE WT::fmt)

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
TARGET_LINK_LIBRARIES(WtDataStorageAD PUBLIC ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtDataStorageAD PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

