#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(QuoteFactory LANGUAGES CXX)
INCLUDE(${CMAKE_CURRENT_LIST_DIR}/../cmake/WtCopyTarget.cmake)


#7. 添加源码
SET(SRCS
	main.cpp
)

IF(MSVC)
	list (APPEND SRCS ../Common/mdump.cpp)
ENDIF()


SET(LIBS
	WTSTools
	WTSUtils
	WtDtCore)

IF(MSVC)
	LIST(APPEND LIBS ws2_32)
ELSE()
	LIST(APPEND LIBS
		WT::Platform
		WT::BoostFilesystem
        WT::BoostThread
        WT::Platform
		${FS_LIB}
		)
	if(WIN32)
		LIST(APPEND LIBS
			ws2_32 iconv)
	ENDIF()
ENDIF()

#9. 生成可执行文件
ADD_EXECUTABLE(QuoteFactory ${SRCS})
wt_set_target_output_directory(QuoteFactory QuoteFactory)
TARGET_LINK_LIBRARIES(QuoteFactory PRIVATE WT::fmt)
TARGET_LINK_LIBRARIES(QuoteFactory PRIVATE ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(QuoteFactory PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

wt_make_target_subdirectories(QuoteFactory parsers)
wt_add_deployment_target(QuoteFactory_deploy QuoteFactory
	MANAGED_SUBDIRECTORIES parsers
)

SET(WT_QUOTE_FACTORY_POST_BUILD_DEPENDS "")
MACRO(WT_QUOTE_FACTORY_DEPLOY WT_PRODUCER WT_SUBDIR)
	IF(TARGET ${WT_PRODUCER})
		LIST(APPEND WT_QUOTE_FACTORY_POST_BUILD_DEPENDS ${WT_PRODUCER})
		wt_copy_and_deploy_target_if_exists(QuoteFactory QuoteFactory_deploy ${WT_PRODUCER} "${WT_SUBDIR}")
	ENDIF()
ENDMACRO()

WT_QUOTE_FACTORY_DEPLOY(ParserCTP parsers)
WT_QUOTE_FACTORY_DEPLOY(ParserXTP parsers)
WT_QUOTE_FACTORY_DEPLOY(ParserFemas parsers)
WT_QUOTE_FACTORY_DEPLOY(ParserCTPMini parsers)
WT_QUOTE_FACTORY_DEPLOY(WtDataStorage "")

add_dependencies(QuoteFactory ${WT_QUOTE_FACTORY_POST_BUILD_DEPENDS})


