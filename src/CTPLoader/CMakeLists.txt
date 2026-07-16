
#1. 确定CMake的最低版本需求
cmake_minimum_required(VERSION 3.20)

#2. 确定工程名
project(CTPLoader LANGUAGES CXX)


#7. 添加源码
SET(SRC  
    ${PROJECT_SOURCE_DIR}/CTPLoader.cpp
    ${PROJECT_SOURCE_DIR}/TraderSpi.cpp
    ${PROJECT_SOURCE_DIR}/TraderSpi.h
)


SET(LIBS
    WT::BoostThread
    WT::Platform
    WT::BoostFilesystem
    WT::Platform
    WTSUtils
    ${WT_ICONV_LIB}
)

IF(WIN32)
LIST(APPEND LIBS iconv)
ENDIF()

#9. 生成可执行文件
ADD_LIBRARY(CTPLoader SHARED ${SRC})
TARGET_LINK_LIBRARIES(CTPLoader PRIVATE WT::fmt WT::RapidJSON PUBLIC WT::CTP)
IF (MSVC)
ELSE()
    TARGET_LINK_LIBRARIES(CTPLoader PUBLIC ${LIBS})
ENDIF ()

IF(WT_CTP_STATIC)
    TARGET_LINK_LIBRARIES(CTPLoader PRIVATE WT::CTP_TRADER)
ENDIF()

IF (MSVC)
ELSE()
    SET_TARGET_PROPERTIES(CTPLoader PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()


