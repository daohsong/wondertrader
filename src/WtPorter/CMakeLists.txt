#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtPorter LANGUAGES CXX)
INCLUDE(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)


#7. 添加源码
SET(SRCS
	ExpCtaContext.cpp
	ExpExecuter.cpp
	ExpHftContext.cpp
	ExpParser.cpp
	ExpSelContext.cpp
	WtPorter.cpp
	WtRtRunner.cpp
)
SET(HDRS
	ExpCtaContext.h
	ExpExecuter.h
	ExpHftContext.h
	ExpParser.h
	ExpSelContext.h
	PorterDefs.h
	WtPorter.h
	WtRtRunner.h
)

IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()

ADD_LIBRARY(WtPorter SHARED ${SRCS} ${HDRS})
wt_set_target_output_directory(WtPorter WtPorter)
TARGET_LINK_LIBRARIES(WtPorter PRIVATE WT::fmt)

SET(LIBS WtCore WTSTools WTSUtils)

IF (MSVC)
	LIST(APPEND LIBS ws2_32)
ELSE()
	LIST(APPEND LIBS
		WT::Platform
		${FS_LIB}
		WT::BoostThread)
	IF(WIN32)
		LIST(APPEND LIBS
			ws2_32 iconv)
	ENDIF()
ENDIF()

TARGET_LINK_LIBRARIES(WtPorter PUBLIC ${LIBS})
IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtPorter PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

wt_make_target_subdirectories(WtPorter parsers traders executer)
wt_add_deployment_target(WtPorter_deploy WtPorter
	MANAGED_SUBDIRECTORIES parsers traders executer
)

SET(WT_PORTER_POST_BUILD_DEPENDS "")
MACRO(WT_PORTER_DEPLOY WT_PRODUCER WT_SUBDIR)
	IF(TARGET ${WT_PRODUCER})
		LIST(APPEND WT_PORTER_POST_BUILD_DEPENDS ${WT_PRODUCER})
		wt_copy_and_deploy_target_if_exists(WtPorter WtPorter_deploy ${WT_PRODUCER} "${WT_SUBDIR}")
	ENDIF()
ENDMACRO()

WT_PORTER_DEPLOY(ParserUDP parsers)
WT_PORTER_DEPLOY(ParserShm parsers)
WT_PORTER_DEPLOY(TraderCTP traders)
WT_PORTER_DEPLOY(TraderXTP traders)
WT_PORTER_DEPLOY(TraderMocker traders)
WT_PORTER_DEPLOY(TraderCTPMini traders)
WT_PORTER_DEPLOY(TraderCTPOpt traders)
WT_PORTER_DEPLOY(TraderFemas traders)
WT_PORTER_DEPLOY(WtExeFact executer)
WT_PORTER_DEPLOY(WtDataStorage "")
WT_PORTER_DEPLOY(WtDataStorageAD "")
WT_PORTER_DEPLOY(WtRiskMonFact "")
WT_PORTER_DEPLOY(WtMsgQue "")

add_dependencies(WtPorter ${WT_PORTER_POST_BUILD_DEPENDS})


