cmake_minimum_required(VERSION 3.10)

PROJECT(TraderZMQ LANGUAGES CXX)
SET(CMAKE_CXX_STANDARD 17)

SET(SRC
	${PROJECT_SOURCE_DIR}/TraderZMQ.cpp
	${PROJECT_SOURCE_DIR}/TraderZMQ.h
)

SET(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/build_${PLATFORM}/${CMAKE_BUILD_TYPE}/bin)

FIND_PATH(ZMQ_INCLUDE_DIR NAMES zmq.h PATHS
	${INCS}
	${CMAKE_SOURCE_DIR}/../../wt_deps/include
	/usr/local/include
	/opt/homebrew/include
	/usr/include
	/opt/homebrew/opt/zeromq/include
)
IF(NOT ZMQ_INCLUDE_DIR)
	MESSAGE(FATAL_ERROR "zmq.h not found; please install zeromq dev headers or place them under wt_deps/include")
ENDIF()

FIND_PATH(RAPIDJSON_INCLUDE_DIR NAMES rapidjson/document.h PATHS
	${INCS}
	${CMAKE_SOURCE_DIR}/../../wt_deps/include
	${PROJECT_SOURCE_DIR}/../../../wt_deps/include
	/usr/local/include
	/opt/homebrew/include
	/usr/include
)
IF(NOT RAPIDJSON_INCLUDE_DIR)
	MESSAGE(FATAL_ERROR "rapidjson not found; please install it or place headers under wt_deps/include/rapidjson")
ENDIF()

FIND_PATH(CPPZMQ_INCLUDE_DIR NAMES zmq.hpp PATHS
	${INCS}
	${CMAKE_SOURCE_DIR}/../../cppzmq
	${PROJECT_SOURCE_DIR}/../../../cppzmq
	/usr/local/include
	/opt/homebrew/include
	/usr/include
)
IF(NOT CPPZMQ_INCLUDE_DIR)
	MESSAGE(FATAL_ERROR "cppzmq (zmq.hpp) not found; please init submodule or ensure it exists under ../../cppzmq")
ENDIF()

INCLUDE_DIRECTORIES(${INCS} ${RAPIDJSON_INCLUDE_DIR} ${ZMQ_INCLUDE_DIR} ${CPPZMQ_INCLUDE_DIR})
LINK_DIRECTORIES(${LNKS})
ADD_LIBRARY(TraderZMQ SHARED ${SRC})
IF (MSVC)
ELSE (GNUCC)
	TARGET_COMPILE_OPTIONS(TraderZMQ PRIVATE -isystem ${ZMQ_INCLUDE_DIR} -isystem ${CPPZMQ_INCLUDE_DIR})
ENDIF()

FIND_LIBRARY(ZMQ_LIB NAMES zmq libzmq PATHS ${LNKS} ${CMAKE_SOURCE_DIR}/../../wt_deps/lib /usr/local/lib /opt/homebrew/lib /usr/lib /usr/lib/x86_64-linux-gnu /opt/homebrew/opt/zeromq/lib)
IF(NOT ZMQ_LIB)
	MESSAGE(FATAL_ERROR "libzmq not found; please install zeromq or place it under wt_deps/lib")
ENDIF()

IF (MSVC)
ELSE (GNUCC)
	SET(LIBS
		${ZMQ_LIB}
		pthread
		dl
	)
	TARGET_LINK_LIBRARIES(TraderZMQ ${LIBS})
ENDIF()

IF (MSVC)
ELSE (GNUCC)
	SET_TARGET_PROPERTIES(TraderZMQ PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		C_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN 1)
ENDIF()
