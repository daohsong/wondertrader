#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtExeFact LANGUAGES CXX)

#7. 添加源码
SET(SRCS
	WtDiffMinImpactExeUnit.cpp
	WtExeFact.cpp
	WtMinImpactExeUnit.cpp
	WtOrdMon.cpp
	WtStockMinImpactExeUnit.cpp
	WtStockVWapExeUnit.cpp
	WtTWapExeUnit.cpp
	WtVWapExeUnit.cpp
)
SET(HDRS
	WtDiffMinImpactExeUnit.h
	WtExeFact.h
	WtMinImpactExeUnit.h
	WtOrdMon.h
	WtStockMinImpactExeUnit.h
	WtStockVWapExeUnit.h
	WtTWapExeUnit.h
	WtVWapExeUnit.h
)


ADD_LIBRARY(WtExeFact SHARED ${SRCS} ${HDRS})
TARGET_LINK_LIBRARIES(WtExeFact PUBLIC WT::fmt WT::RapidJSON)

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtExeFact PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

