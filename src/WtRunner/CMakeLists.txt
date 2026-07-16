#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtRunner LANGUAGES CXX)
INCLUDE(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)

#7. 添加源码
SET(SRCS
	WtRunner.cpp
	main.cpp
)
SET(HDRS
	WtRunner.h
)

IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()


SET(LIBS
	WtCore
	WTSTools
	WTSUtils
	)

IF (MSVC)
	LIST(APPEND LIBS ws2_32)
ELSE()
	LIST(APPEND LIBS
		WT::Platform
		${FS_LIB}
		WT::BoostThread
		)
	IF(WIN32)
		LIST(APPEND LIBS
			ws2_32 iconv)
	ENDIF()
ENDIF()

ADD_EXECUTABLE(WtRunner ${SRCS} ${HDRS})
wt_set_target_output_directory(WtRunner WtRunner)
TARGET_LINK_LIBRARIES(WtRunner PRIVATE WT::fmt)
TARGET_LINK_LIBRARIES(WtRunner PRIVATE ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtRunner PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

wt_make_target_subdirectories(WtRunner parsers traders executer)
wt_add_deployment_target(WtRunner_deploy WtRunner
	MANAGED_SUBDIRECTORIES parsers traders executer
)

SET(WT_RUNNER_POST_BUILD_DEPENDS
	ParserUDP
	TraderMocker
	WtExeFact
	WtDataStorage
	WtDataStorageAD
	WtRiskMonFact
	WtMsgQue
)
wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy ParserUDP parsers)

IF(TARGET TraderCTP)
	LIST(APPEND WT_RUNNER_POST_BUILD_DEPENDS TraderCTP)
	wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy TraderCTP traders)
ENDIF()

IF(TARGET TraderXTP)
	LIST(APPEND WT_RUNNER_POST_BUILD_DEPENDS TraderXTP)
	wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy TraderXTP traders)
ENDIF()

wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy TraderMocker traders)

IF(TARGET TraderCTPMini)
	LIST(APPEND WT_RUNNER_POST_BUILD_DEPENDS TraderCTPMini)
	wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy TraderCTPMini traders)
ENDIF()

IF(TARGET TraderCTPOpt)
	LIST(APPEND WT_RUNNER_POST_BUILD_DEPENDS TraderCTPOpt)
	wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy TraderCTPOpt traders)
ENDIF()

IF(TARGET TraderFemas)
	LIST(APPEND WT_RUNNER_POST_BUILD_DEPENDS TraderFemas)
	wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy TraderFemas traders)
ENDIF()

wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy WtExeFact executer)
wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy WtDataStorage "")
wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy WtDataStorageAD "")
wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy WtRiskMonFact "")
wt_copy_and_deploy_target_if_exists(WtRunner WtRunner_deploy WtMsgQue "")

add_dependencies(WtRunner ${WT_RUNNER_POST_BUILD_DEPENDS})


