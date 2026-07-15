
#1. 确定CMake的最低版本需求
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

#2. 确定工程名
PROJECT(TestUnits LANGUAGES CXX)


#7. 添加源码
SET(SRCS
	gtest/gtest-death-test.cc
	gtest/gtest-filepath.cc
	gtest/gtest-port.cc
	gtest/gtest-printers.cc
	gtest/gtest-test-part.cc
	gtest/gtest-typed-test.cc
	gtest/gtest.cc
	main.cpp
	test_asio_compat.cpp
	test_atomic_compat.cpp
	test_basedata_defaults.cpp
	test_codehelper.cpp
	test_compile_warnings_kvcache.cpp
	test_compile_warnings_shareblocks.cpp
	test_compile_warnings_shmqueue.cpp
	test_compile_warnings_threadpool.cpp
	test_contract_identifiers.cpp
	test_cpuhelper.cpp
	test_current_dir_compat.cpp
	test_dllhelper.cpp
	test_fastestmap.cpp
	test_filesystem_compat.cpp
	test_fmt.cpp
	test_header_self_contained_asio_compat.cpp
	test_header_self_contained_eventnotifier_wtcore.cpp
	test_header_self_contained_eventnotifier_wtuftcore.cpp
	test_header_self_contained_filesystem_compat.cpp
	test_header_self_contained_stdutils.cpp
	test_header_self_contained_udpcaster.cpp
	test_header_self_contained_wtdtrunner.cpp
	test_header_self_contained_wtfiltermgr.cpp
	test_inihelper.cpp
	test_kvcache.cpp
	test_lmdb.cpp
	test_object_pool.cpp
	test_platform_pause.cpp
	test_session.cpp
	test_sel_task_schedule.cpp
	test_shm.cpp
	test_timeutils_format.cpp
	test_tradermocker_matching.cpp
	test_utils.cpp
)
SET(LIBS
	WtDtCore
	WtCore
    WTSTools
	WTSUtils
    WtShareHelper)
IF (MSVC)
ELSE()
    LIST(APPEND LIBS WT::Platform ${FS_LIB})
	IF(WIN32)
		LIST(APPEND LIBS iconv)
	ENDIF()
ENDIF()

ADD_LIBRARY(TestDllHelperFixture SHARED dllhelper_fixture.cpp)

ADD_EXECUTABLE(TestUnits ${SRCS})
TARGET_INCLUDE_DIRECTORIES(TestUnits PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/gtest")
TARGET_LINK_LIBRARIES(TestUnits PRIVATE WT::fmt WT::RapidJSON WT::BoostThread)
TARGET_LINK_LIBRARIES(TestUnits PRIVATE ${LIBS})
TARGET_COMPILE_DEFINITIONS(TestUnits PRIVATE
	WT_DLLHELPER_FIXTURE_PATH="$<TARGET_FILE:TestDllHelperFixture>"
)
ADD_DEPENDENCIES(TestUnits TestDllHelperFixture)

SET(WT_BT_CORE_TEST_SRCS
	gtest/gtest-death-test.cc
	gtest/gtest-filepath.cc
	gtest/gtest-port.cc
	gtest/gtest-printers.cc
	gtest/gtest-test-part.cc
	gtest/gtest-typed-test.cc
	gtest/gtest.cc
	main.cpp
	test_expctamocker_constructor.cpp
	test_cfgloader_null.cpp
	test_hftmocker_orders.cpp
	test_hisdatareplayer_l2.cpp
	test_idatawriter_lifecycle.cpp
	test_signalhook.cpp
	test_stacktracer.cpp
	test_uftmocker_orders.cpp
	test_wtdatawriter_final.cpp
	test_wtdatawriterad_final.cpp
	test_wtsvariant_format.cpp
)

ADD_EXECUTABLE(WtBtCoreTests ${WT_BT_CORE_TEST_SRCS})
TARGET_INCLUDE_DIRECTORIES(WtBtCoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/gtest")
TARGET_LINK_LIBRARIES(WtBtCoreTests PRIVATE
	WtBtCore
	WTSTools
	WTSUtils
	WT::fmt
	WT::RapidJSON
	WT::BoostThread
	WT::Platform
	${FS_LIB}
)

ADD_TEST(
	NAME WonderTrader.TestUnits
	COMMAND ${CMAKE_COMMAND}
		-DWT_TEST_EXECUTABLE=$<TARGET_FILE:TestUnits>
		-DWT_TEST_SANDBOX_ROOT=${CMAKE_CURRENT_BINARY_DIR}/sandboxes
		-P ${CMAKE_CURRENT_LIST_DIR}/RunTestInSandbox.cmake
)
SET_TESTS_PROPERTIES(WonderTrader.TestUnits PROPERTIES
	LABELS unit
	TIMEOUT 180
)

ADD_TEST(
	NAME WonderTrader.WtBtCoreTests
	COMMAND ${CMAKE_COMMAND}
		-DWT_TEST_EXECUTABLE=$<TARGET_FILE:WtBtCoreTests>
		-DWT_TEST_SANDBOX_ROOT=${CMAKE_CURRENT_BINARY_DIR}/sandboxes
		-P ${CMAKE_CURRENT_LIST_DIR}/RunTestInSandbox.cmake
)
SET_TESTS_PROPERTIES(WonderTrader.WtBtCoreTests PROPERTIES
	LABELS "unit;backtest"
	TIMEOUT 180
)

IF (MSVC)
ELSE()
	SET_TARGET_PROPERTIES(TestUnits PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON)
ENDIF ()
