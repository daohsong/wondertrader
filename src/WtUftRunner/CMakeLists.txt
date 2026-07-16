#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(WtUftRunner LANGUAGES CXX)
INCLUDE(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)


#7. 添加源码
SET(SRCS
	WtUftRunner.cpp
	main.cpp
)
SET(HDRS
	WtUftRunner.h
)

IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()


SET(LIBS
	WtUftCore
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

ADD_EXECUTABLE(WtUftRunner ${SRCS} ${HDRS})
wt_set_target_output_directory(WtUftRunner WtUftRunner)
TARGET_LINK_LIBRARIES(WtUftRunner PRIVATE WT::fmt)
TARGET_LINK_LIBRARIES(WtUftRunner PRIVATE ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(WtUftRunner PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

wt_make_target_subdirectories(WtUftRunner parsers traders)
wt_add_deployment_target(WtUftRunner_deploy WtUftRunner
	MANAGED_SUBDIRECTORIES parsers traders
)

SET(WT_UFT_RUNNER_POST_BUILD_DEPENDS "")
MACRO(WT_UFT_RUNNER_DEPLOY WT_PRODUCER WT_SUBDIR)
	IF(TARGET ${WT_PRODUCER})
		LIST(APPEND WT_UFT_RUNNER_POST_BUILD_DEPENDS ${WT_PRODUCER})
		wt_copy_and_deploy_target_if_exists(WtUftRunner WtUftRunner_deploy ${WT_PRODUCER} "${WT_SUBDIR}")
	ENDIF()
ENDMACRO()

WT_UFT_RUNNER_DEPLOY(TraderCTP traders)
WT_UFT_RUNNER_DEPLOY(ParserCTP parsers)

IF(WT_UFT_RUNNER_POST_BUILD_DEPENDS)
	add_dependencies(WtUftRunner ${WT_UFT_RUNNER_POST_BUILD_DEPENDS})
ENDIF()


