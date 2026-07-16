#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtDtPorter LANGUAGES CXX)
INCLUDE(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)


#7. 添加源码
SET(SRCS
	ExpDumper.cpp
	ExpParser.cpp
	WtDtPorter.cpp
	WtDtRunner.cpp
)
SET(HDRS
	ExpDumper.h
	ExpParser.h
	PorterDefs.h
	WtDtPorter.h
	WtDtRunner.h
)

IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()

ADD_LIBRARY(WtDtPorter SHARED ${SRCS} ${HDRS})
wt_set_target_output_directory(WtDtPorter WtDtPorter)
TARGET_LINK_LIBRARIES(WtDtPorter PRIVATE WT::fmt)

SET(LIBS
	WtDtCore
	WTSTools
	WTSUtils
	)
IF(MSVC)
ELSE()
	LIST(APPEND LIBS
		WT::Platform
		WT::BoostFilesystem
		WT::BoostThread
		${FS_LIB}
		)
	IF(WIN32)
		LIST(APPEND LIBS
			ws2_32 iconv
		)
	ENDIF()
ENDIF()

TARGET_LINK_LIBRARIES(WtDtPorter PUBLIC ${LIBS})
IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtDtPorter PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

wt_make_target_subdirectories(WtDtPorter parsers)
wt_add_deployment_target(WtDtPorter_deploy WtDtPorter
	MANAGED_SUBDIRECTORIES parsers
)

SET(WT_DT_PORTER_POST_BUILD_DEPENDS "")
MACRO(WT_DT_PORTER_DEPLOY WT_PRODUCER WT_SUBDIR)
	IF(TARGET ${WT_PRODUCER})
		LIST(APPEND WT_DT_PORTER_POST_BUILD_DEPENDS ${WT_PRODUCER})
		wt_copy_and_deploy_target_if_exists(WtDtPorter WtDtPorter_deploy ${WT_PRODUCER} "${WT_SUBDIR}")
	ENDIF()
ENDMACRO()

WT_DT_PORTER_DEPLOY(ParserCTP parsers)
WT_DT_PORTER_DEPLOY(ParserXTP parsers)
WT_DT_PORTER_DEPLOY(ParserFemas parsers)
WT_DT_PORTER_DEPLOY(ParserCTPMini parsers)
WT_DT_PORTER_DEPLOY(ParserCTPOpt parsers)
WT_DT_PORTER_DEPLOY(WtDataStorage "")
WT_DT_PORTER_DEPLOY(WtDataStorageAD "")

add_dependencies(WtDtPorter ${WT_DT_PORTER_POST_BUILD_DEPENDS})
