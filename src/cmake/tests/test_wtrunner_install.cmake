cmake_minimum_required(VERSION 3.21)

foreach(WT_REQUIRED_ARG IN ITEMS WT_BUILD_DIR WT_INSTALL_PREFIX WT_PLUGIN_SMOKE WT_SHARED_SUFFIX)
	if("${${WT_REQUIRED_ARG}}" STREQUAL "")
		message(FATAL_ERROR "${WT_REQUIRED_ARG} is required")
	endif()
endforeach()

file(REMOVE_RECURSE "${WT_INSTALL_PREFIX}")
set(WT_INSTALL_COMMAND "${CMAKE_COMMAND}" --install "${WT_BUILD_DIR}" --prefix "${WT_INSTALL_PREFIX}")
if(WT_INSTALL_CONFIG)
	list(APPEND WT_INSTALL_COMMAND --config "${WT_INSTALL_CONFIG}")
endif()
execute_process(
	COMMAND ${WT_INSTALL_COMMAND}
	RESULT_VARIABLE WT_INSTALL_RESULT
	OUTPUT_VARIABLE WT_INSTALL_OUTPUT
	ERROR_VARIABLE WT_INSTALL_ERROR
)
if(NOT WT_INSTALL_RESULT EQUAL 0)
	message(FATAL_ERROR "WtRunner install failed:\n${WT_INSTALL_OUTPUT}\n${WT_INSTALL_ERROR}")
endif()

set(WT_INSTALL_ROOT "${WT_INSTALL_PREFIX}/bin/WtRunner")
set(WT_SMOKE_PLUGINS
	"${WT_INSTALL_ROOT}/parsers/${WT_SHARED_PREFIX}ParserUDP${WT_SHARED_SUFFIX}"
	"${WT_INSTALL_ROOT}/traders/${WT_SHARED_PREFIX}TraderMocker${WT_SHARED_SUFFIX}"
	"${WT_INSTALL_ROOT}/executer/${WT_SHARED_PREFIX}WtExeFact${WT_SHARED_SUFFIX}"
	"${WT_INSTALL_ROOT}/${WT_SHARED_PREFIX}WtDataStorage${WT_SHARED_SUFFIX}"
	"${WT_INSTALL_ROOT}/${WT_SHARED_PREFIX}WtDataStorageAD${WT_SHARED_SUFFIX}"
	"${WT_INSTALL_ROOT}/${WT_SHARED_PREFIX}WtRiskMonFact${WT_SHARED_SUFFIX}"
	"${WT_INSTALL_ROOT}/${WT_SHARED_PREFIX}WtMsgQue${WT_SHARED_SUFFIX}"
)
# Include every optional plugin selected by the install configuration, while
# retaining the fixed list above so an accidentally empty directory still fails.
file(GLOB WT_OPTIONAL_SMOKE_PLUGINS LIST_DIRECTORIES FALSE
	"${WT_INSTALL_ROOT}/parsers/${WT_SHARED_PREFIX}*${WT_SHARED_SUFFIX}"
	"${WT_INSTALL_ROOT}/traders/${WT_SHARED_PREFIX}*${WT_SHARED_SUFFIX}"
	"${WT_INSTALL_ROOT}/executer/${WT_SHARED_PREFIX}*${WT_SHARED_SUFFIX}"
)
list(APPEND WT_SMOKE_PLUGINS ${WT_OPTIONAL_SMOKE_PLUGINS})
list(REMOVE_DUPLICATES WT_SMOKE_PLUGINS)
foreach(WT_SMOKE_PLUGIN IN LISTS WT_SMOKE_PLUGINS)
	if(NOT EXISTS "${WT_SMOKE_PLUGIN}")
		message(FATAL_ERROR "Installed plugin is missing: ${WT_SMOKE_PLUGIN}")
	endif()
endforeach()

set(WT_SMOKE_COMMAND "${CMAKE_COMMAND}" -E env)
if(WIN32)
	# The installed executable normally supplies this search directory. The
	# standalone smoke loader needs the equivalent explicit DLL search path.
	list(APPEND WT_SMOKE_COMMAND "PATH=${WT_INSTALL_ROOT}\;$ENV{PATH}")
else()
	# Prove that the installed RPATH is sufficient without development-shell
	# library path overrides masking missing runtime dependencies.
	list(APPEND WT_SMOKE_COMMAND
		--unset=LD_LIBRARY_PATH
		--unset=DYLD_LIBRARY_PATH
		--unset=DYLD_FALLBACK_LIBRARY_PATH
	)
endif()
list(APPEND WT_SMOKE_COMMAND "${WT_PLUGIN_SMOKE}")
execute_process(
	COMMAND ${WT_SMOKE_COMMAND} ${WT_SMOKE_PLUGINS}
	WORKING_DIRECTORY "${WT_INSTALL_ROOT}"
	RESULT_VARIABLE WT_SMOKE_RESULT
	OUTPUT_VARIABLE WT_SMOKE_OUTPUT
	ERROR_VARIABLE WT_SMOKE_ERROR
)
if(NOT WT_SMOKE_RESULT EQUAL 0)
	message(FATAL_ERROR "Installed plugin smoke failed:\n${WT_SMOKE_OUTPUT}\n${WT_SMOKE_ERROR}")
endif()
