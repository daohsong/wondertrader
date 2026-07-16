
#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(ParserXeleSkt LANGUAGES CXX)

SET(SRC  
	${PROJECT_SOURCE_DIR}/ParserXeleSkt.cpp
	${PROJECT_SOURCE_DIR}/ParserXeleSkt.h
)


ADD_LIBRARY(ParserXeleSkt SHARED ${SRC})
TARGET_LINK_LIBRARIES(ParserXeleSkt PRIVATE WT::fmt)

IF(BUILD_TESTING)
	ADD_EXECUTABLE(ParserXeleSktTimeTest test_xele_time.cpp)
	ADD_TEST(NAME WonderTrader.ParserXeleSktTime COMMAND ParserXeleSktTimeTest)
	SET_TESTS_PROPERTIES(WonderTrader.ParserXeleSktTime PROPERTIES
		LABELS unit
		TIMEOUT 30)
ENDIF()

IF(MSVC)
	SET(LIBS
		ws2_32
	)
ELSE()
	IF (WIN32)
		SET(LIBS
			WT::BoostThread
			ws2_32
		)
	ELSE(UNIX)
		SET(LIBS
			WT::BoostThread
		)
	ENDIF()
ENDIF()
TARGET_LINK_LIBRARIES(ParserXeleSkt PUBLIC ${LIBS})

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(ParserXeleSkt PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()

