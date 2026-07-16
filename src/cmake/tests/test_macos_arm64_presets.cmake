if(NOT WT_PRESETS_FILE)
	message(FATAL_ERROR "WT_PRESETS_FILE is required")
endif()
if(NOT EXISTS "${WT_PRESETS_FILE}")
	message(FATAL_ERROR "macOS arm64 presets file is missing: ${WT_PRESETS_FILE}")
endif()

file(READ "${WT_PRESETS_FILE}" WT_PRESETS_JSON)

function(WT_FIND_PRESET WT_KIND WT_NAME WT_OUTPUT)
	string(JSON WT_PRESET_COUNT LENGTH "${WT_PRESETS_JSON}" "${WT_KIND}")
	math(EXPR WT_PRESET_LAST "${WT_PRESET_COUNT} - 1")
	foreach(WT_PRESET_INDEX RANGE 0 ${WT_PRESET_LAST})
		string(JSON WT_PRESET_NAME GET "${WT_PRESETS_JSON}" "${WT_KIND}" ${WT_PRESET_INDEX} name)
		if(WT_PRESET_NAME STREQUAL WT_NAME)
			set(${WT_OUTPUT} ${WT_PRESET_INDEX} PARENT_SCOPE)
			return()
		endif()
	endforeach()
	message(FATAL_ERROR "Missing ${WT_KIND} entry '${WT_NAME}'")
endfunction()

function(WT_ASSERT_PRESET_MISSING WT_KIND WT_NAME)
	string(JSON WT_PRESET_COUNT LENGTH "${WT_PRESETS_JSON}" "${WT_KIND}")
	math(EXPR WT_PRESET_LAST "${WT_PRESET_COUNT} - 1")
	foreach(WT_PRESET_INDEX RANGE 0 ${WT_PRESET_LAST})
		string(JSON WT_PRESET_NAME GET "${WT_PRESETS_JSON}" "${WT_KIND}" ${WT_PRESET_INDEX} name)
		if(WT_PRESET_NAME STREQUAL WT_NAME)
			message(FATAL_ERROR "Unexpected ${WT_KIND} entry '${WT_NAME}'")
		endif()
	endforeach()
endfunction()

function(WT_ASSERT_JSON_VALUE WT_EXPECTED)
	set(WT_PATH ${ARGN})
	string(JSON WT_ACTUAL GET "${WT_PRESETS_JSON}" ${WT_PATH})
	if(NOT "${WT_ACTUAL}" STREQUAL "${WT_EXPECTED}")
		string(JOIN "." WT_PATH_TEXT ${WT_PATH})
		message(FATAL_ERROR "Expected ${WT_PATH_TEXT}='${WT_EXPECTED}', got '${WT_ACTUAL}'")
	endif()
endfunction()

WT_FIND_PRESET(configurePresets macos-arm64-base WT_BASE_INDEX)
WT_ASSERT_JSON_VALUE(ON configurePresets ${WT_BASE_INDEX} hidden)
WT_ASSERT_JSON_VALUE(arm64 configurePresets ${WT_BASE_INDEX} cacheVariables CMAKE_OSX_ARCHITECTURES)
WT_ASSERT_JSON_VALUE(ON configurePresets ${WT_BASE_INDEX} cacheVariables BUILD_TESTING)
WT_ASSERT_JSON_VALUE(ON configurePresets ${WT_BASE_INDEX} cacheVariables WT_ENABLE_WARNINGS)
WT_ASSERT_JSON_VALUE(BASELINE configurePresets ${WT_BASE_INDEX} cacheVariables WT_WARNING_LEVEL)

foreach(WT_PRESET_NAME IN ITEMS
	macos-arm64-debug
	macos-arm64-cxx20
	macos-arm64-cxx23
	macos-arm64-asan
)
	WT_FIND_PRESET(configurePresets ${WT_PRESET_NAME} WT_CONFIGURE_INDEX)
	WT_FIND_PRESET(buildPresets ${WT_PRESET_NAME} WT_BUILD_INDEX)
	WT_FIND_PRESET(testPresets ${WT_PRESET_NAME} WT_TEST_INDEX)
	WT_ASSERT_JSON_VALUE(${WT_PRESET_NAME} buildPresets ${WT_BUILD_INDEX} configurePreset)
	WT_ASSERT_JSON_VALUE(${WT_PRESET_NAME} testPresets ${WT_TEST_INDEX} configurePreset)
endforeach()

foreach(WT_PRESET_KIND IN ITEMS configurePresets buildPresets testPresets)
	WT_ASSERT_PRESET_MISSING(${WT_PRESET_KIND} macos-arm64-release)
endforeach()

WT_FIND_PRESET(configurePresets macos-arm64-debug WT_DEBUG_INDEX)
WT_ASSERT_JSON_VALUE(Debug configurePresets ${WT_DEBUG_INDEX} cacheVariables CMAKE_BUILD_TYPE)
WT_ASSERT_JSON_VALUE(17 configurePresets ${WT_DEBUG_INDEX} cacheVariables WT_CXX_STANDARD)
WT_ASSERT_JSON_VALUE(ON configurePresets ${WT_DEBUG_INDEX} cacheVariables WT_WARNINGS_AS_ERRORS)

WT_FIND_PRESET(configurePresets macos-arm64-cxx20 WT_CXX20_INDEX)
WT_ASSERT_JSON_VALUE(Release configurePresets ${WT_CXX20_INDEX} cacheVariables CMAKE_BUILD_TYPE)
WT_ASSERT_JSON_VALUE(20 configurePresets ${WT_CXX20_INDEX} cacheVariables WT_CXX_STANDARD)
WT_ASSERT_JSON_VALUE(ON configurePresets ${WT_CXX20_INDEX} cacheVariables WT_WARNINGS_AS_ERRORS)

WT_FIND_PRESET(configurePresets macos-arm64-cxx23 WT_CXX23_INDEX)
WT_ASSERT_JSON_VALUE(Release configurePresets ${WT_CXX23_INDEX} cacheVariables CMAKE_BUILD_TYPE)
WT_ASSERT_JSON_VALUE(23 configurePresets ${WT_CXX23_INDEX} cacheVariables WT_CXX_STANDARD)
WT_ASSERT_JSON_VALUE(ON configurePresets ${WT_CXX23_INDEX} cacheVariables WT_WARNINGS_AS_ERRORS)

WT_FIND_PRESET(configurePresets macos-arm64-asan WT_ASAN_INDEX)
WT_ASSERT_JSON_VALUE(Debug configurePresets ${WT_ASAN_INDEX} cacheVariables CMAKE_BUILD_TYPE)
foreach(WT_ASAN_VARIABLE IN ITEMS
	CMAKE_C_FLAGS
	CMAKE_CXX_FLAGS
	CMAKE_EXE_LINKER_FLAGS
	CMAKE_SHARED_LINKER_FLAGS
)
	string(JSON WT_ASAN_FLAGS GET "${WT_PRESETS_JSON}"
		configurePresets ${WT_ASAN_INDEX} cacheVariables ${WT_ASAN_VARIABLE})
	if(NOT WT_ASAN_FLAGS MATCHES "-fsanitize=address,undefined")
		message(FATAL_ERROR "${WT_ASAN_VARIABLE} does not enable ASan+UBSan: ${WT_ASAN_FLAGS}")
	endif()
endforeach()

WT_FIND_PRESET(testPresets macos-arm64-asan WT_ASAN_TEST_INDEX)
string(JSON WT_ASAN_OPTIONS GET "${WT_PRESETS_JSON}"
	testPresets ${WT_ASAN_TEST_INDEX} environment ASAN_OPTIONS)
if(NOT WT_ASAN_OPTIONS MATCHES "(^|:)strict_string_checks=1(:|$)")
	message(FATAL_ERROR "macos-arm64-asan must enable strict_string_checks")
endif()
if(WT_ASAN_OPTIONS MATCHES "(^|:)detect_leaks=1(:|$)")
	message(FATAL_ERROR
		"macos-arm64-asan must not enable LeakSanitizer, which Apple ASan does not support")
endif()

string(JSON WT_UBSAN_OPTIONS GET "${WT_PRESETS_JSON}"
	testPresets ${WT_ASAN_TEST_INDEX} environment UBSAN_OPTIONS)
if(NOT WT_UBSAN_OPTIONS MATCHES "(^|:)halt_on_error=1(:|$)")
	message(FATAL_ERROR "macos-arm64-asan must stop on unsuppressed UBSan errors")
endif()
if(NOT WT_UBSAN_OPTIONS MATCHES
	"(^|:)suppressions=.*cmake/sanitizers/macos-ubsan\\.supp(:|$)")
	message(FATAL_ERROR "macos-arm64-asan must load the scoped UBSan suppression file")
endif()

get_filename_component(WT_PRESETS_DIRECTORY "${WT_PRESETS_FILE}" DIRECTORY)
set(WT_UBSAN_SUPPRESSIONS_FILE
	"${WT_PRESETS_DIRECTORY}/cmake/sanitizers/macos-ubsan.supp")
if(NOT EXISTS "${WT_UBSAN_SUPPRESSIONS_FILE}")
	message(FATAL_ERROR "Missing macOS UBSan suppression file: ${WT_UBSAN_SUPPRESSIONS_FILE}")
endif()
file(READ "${WT_UBSAN_SUPPRESSIONS_FILE}" WT_UBSAN_SUPPRESSIONS)
if(NOT WT_UBSAN_SUPPRESSIONS MATCHES
	"pointer-overflow:rapidjson/internal/stack\\.h")
	message(FATAL_ERROR "The macOS UBSan suppression must remain scoped to RapidJSON stack.h")
endif()
