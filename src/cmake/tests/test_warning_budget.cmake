if(NOT WT_PYTHON OR NOT WT_WARNING_BUDGET_SCRIPT OR NOT WT_SOURCE_ROOT OR NOT WT_WARNING_TEST_ROOT)
	message(FATAL_ERROR "WT_PYTHON, WT_WARNING_BUDGET_SCRIPT, WT_SOURCE_ROOT and WT_WARNING_TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${WT_WARNING_TEST_ROOT}")
file(MAKE_DIRECTORY "${WT_WARNING_TEST_ROOT}")

set(WT_LOG "${WT_WARNING_TEST_ROOT}/build.log")
set(WT_ALLOWED_BASELINE "${WT_WARNING_TEST_ROOT}/allowed.json")
set(WT_EMPTY_BASELINE "${WT_WARNING_TEST_ROOT}/empty.json")
set(WT_SUMMARY "${WT_WARNING_TEST_ROOT}/summary.json")

file(WRITE "${WT_LOG}"
	"${WT_SOURCE_ROOT}/src/demo.cpp:7:3: warning: switch value not handled [-Wswitch]\n"
	"/different/checkout/src/demo.cpp:7:3: warning: switch value not handled [-Wswitch]\n"
	"ld: warning: duplicate library ignored\n"
	"ld: warning: duplicate library ignored\n"
)
file(WRITE "${WT_ALLOWED_BASELINE}" [=[{
  "schema_version": 1,
  "allowed_diagnostics": [
    "src/demo.cpp:7:3:-Wswitch:switch value not handled",
    "ld:0:0:linker-warning:duplicate library ignored"
  ]
}
]=])
file(WRITE "${WT_EMPTY_BASELINE}" [=[{
  "schema_version": 1,
  "allowed_diagnostics": []
}
]=])

execute_process(
	COMMAND "${WT_PYTHON}" "${WT_WARNING_BUDGET_SCRIPT}"
		--log "${WT_LOG}"
		--source-root "${WT_SOURCE_ROOT}"
		--baseline "${WT_ALLOWED_BASELINE}"
		--output "${WT_SUMMARY}"
	RESULT_VARIABLE WT_ALLOWED_RESULT
	ERROR_VARIABLE WT_ALLOWED_ERROR
)
if(NOT WT_ALLOWED_RESULT EQUAL 0)
	message(FATAL_ERROR "Allowed warning budget failed: ${WT_ALLOWED_ERROR}")
endif()

file(READ "${WT_SUMMARY}" WT_SUMMARY_CONTENT)
if(NOT WT_SUMMARY_CONTENT MATCHES "\"unique_count\": 2" OR
	NOT WT_SUMMARY_CONTENT MATCHES "\"new_count\": 0")
	message(FATAL_ERROR "Unexpected normalized warning summary: ${WT_SUMMARY_CONTENT}")
endif()

execute_process(
	COMMAND "${WT_PYTHON}" "${WT_WARNING_BUDGET_SCRIPT}"
		--log "${WT_LOG}"
		--source-root "${WT_SOURCE_ROOT}"
		--baseline "${WT_EMPTY_BASELINE}"
		--output "${WT_SUMMARY}"
	RESULT_VARIABLE WT_REJECTED_RESULT
)
if(WT_REJECTED_RESULT EQUAL 0)
	message(FATAL_ERROR "Empty warning budget accepted new diagnostics")
endif()

file(READ "${WT_SUMMARY}" WT_REJECTED_SUMMARY)
if(NOT WT_REJECTED_SUMMARY MATCHES "\"new_count\": 2")
	message(FATAL_ERROR "Rejected summary did not report both new diagnostics: ${WT_REJECTED_SUMMARY}")
endif()
