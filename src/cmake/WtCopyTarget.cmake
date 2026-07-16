function(wt_set_target_output_directory target relative_bin_subdir)
	if(NOT TARGET ${target})
		message(FATAL_ERROR "wt_set_target_output_directory target does not exist: ${target}")
	endif()
	if("${relative_bin_subdir}" STREQUAL "")
		message(FATAL_ERROR "wt_set_target_output_directory requires a non-empty output subdirectory for ${target}")
	endif()

	set(WT_TARGET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build_${WT_ARCH}/$<CONFIG>/bin/${relative_bin_subdir}")
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${WT_TARGET_OUTPUT_DIRECTORY}"
		LIBRARY_OUTPUT_DIRECTORY "${WT_TARGET_OUTPUT_DIRECTORY}"
		ARCHIVE_OUTPUT_DIRECTORY "${WT_TARGET_OUTPUT_DIRECTORY}"
	)
endfunction()

function(wt_set_target_library_output_directory target)
	if(NOT TARGET ${target})
		message(FATAL_ERROR "wt_set_target_library_output_directory target does not exist: ${target}")
	endif()

	set_target_properties(${target} PROPERTIES
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build_${WT_ARCH}/$<CONFIG>/libs"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build_${WT_ARCH}/$<CONFIG>/libs"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build_${WT_ARCH}/$<CONFIG>/bin"
	)
endfunction()

function(wt_set_target_bin_output_directory target)
	if(NOT TARGET ${target})
		message(FATAL_ERROR "wt_set_target_bin_output_directory target does not exist: ${target}")
	endif()

	set(WT_TARGET_BIN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build_${WT_ARCH}/$<CONFIG>/bin")
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${WT_TARGET_BIN_OUTPUT_DIRECTORY}"
		LIBRARY_OUTPUT_DIRECTORY "${WT_TARGET_BIN_OUTPUT_DIRECTORY}"
		ARCHIVE_OUTPUT_DIRECTORY "${WT_TARGET_BIN_OUTPUT_DIRECTORY}"
	)
endfunction()

function(wt_copy_target_if_exists consumer producer subdir)
	if(NOT TARGET ${consumer})
		message(FATAL_ERROR "wt_copy_target_if_exists consumer does not exist: ${consumer}")
	endif()
	if(NOT TARGET ${producer})
		return()
	endif()

	set(WT_COPY_TARGET_DESTINATION "$<TARGET_FILE_DIR:${consumer}>")
	if(NOT "${subdir}" STREQUAL "")
		set(WT_COPY_TARGET_DESTINATION "${WT_COPY_TARGET_DESTINATION}/${subdir}")
	endif()
	add_custom_command(TARGET ${consumer} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${WT_COPY_TARGET_DESTINATION}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:${producer}>" "${WT_COPY_TARGET_DESTINATION}/"
		COMMENT "Copy ${producer} next to ${consumer}"
		VERBATIM
	)
endfunction()

function(wt_add_deployment_target deployment_target consumer)
	set(WT_DEPLOY_MULTI_VALUE_ARGS MANAGED_SUBDIRECTORIES)
	cmake_parse_arguments(WT_DEPLOY "" "" "${WT_DEPLOY_MULTI_VALUE_ARGS}" ${ARGN})
	if(NOT TARGET ${consumer})
		message(FATAL_ERROR "wt_add_deployment_target consumer does not exist: ${consumer}")
	endif()
	if(TARGET ${deployment_target})
		message(FATAL_ERROR "wt_add_deployment_target target already exists: ${deployment_target}")
	endif()

	set(WT_DEPLOY_PREPARE_COMMANDS "")
	foreach(WT_DEPLOY_SUBDIR IN LISTS WT_DEPLOY_MANAGED_SUBDIRECTORIES)
		list(APPEND WT_DEPLOY_PREPARE_COMMANDS
			COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${consumer}>/${WT_DEPLOY_SUBDIR}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${consumer}>/${WT_DEPLOY_SUBDIR}"
		)
	endforeach()
	add_custom_target(${deployment_target}
		${WT_DEPLOY_PREPARE_COMMANDS}
		DEPENDS ${consumer}
		COMMENT "Prepare development deployment for ${consumer}"
		VERBATIM
	)
endfunction()

function(wt_deploy_target_if_exists deployment_target consumer producer subdir)
	if(NOT TARGET ${deployment_target})
		message(FATAL_ERROR "wt_deploy_target_if_exists deployment target does not exist: ${deployment_target}")
	endif()
	if(NOT TARGET ${producer})
		return()
	endif()
	set(WT_DEPLOY_DESTINATION "$<TARGET_FILE_DIR:${consumer}>")
	if(NOT "${subdir}" STREQUAL "")
		set(WT_DEPLOY_DESTINATION "${WT_DEPLOY_DESTINATION}/${subdir}")
	endif()
	add_dependencies(${deployment_target} ${producer})
	add_custom_command(TARGET ${deployment_target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${WT_DEPLOY_DESTINATION}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:${producer}>" "${WT_DEPLOY_DESTINATION}/"
		COMMENT "Deploy ${producer} for ${consumer}"
		VERBATIM
	)
endfunction()

function(wt_copy_and_deploy_target_if_exists consumer deployment_target producer subdir)
	wt_copy_target_if_exists(${consumer} ${producer} "${subdir}")
	wt_deploy_target_if_exists(${deployment_target} ${consumer} ${producer} "${subdir}")
endfunction()

function(wt_make_target_subdirectories consumer)
	if(NOT TARGET ${consumer})
		message(FATAL_ERROR "wt_make_target_subdirectories consumer does not exist: ${consumer}")
	endif()
	foreach(WT_TARGET_SUBDIR IN LISTS ARGN)
		if(NOT "${WT_TARGET_SUBDIR}" STREQUAL "")
			add_custom_command(TARGET ${consumer} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E make_directory
					"$<TARGET_FILE_DIR:${consumer}>/${WT_TARGET_SUBDIR}"
				VERBATIM
			)
		endif()
	endforeach()
endfunction()

function(WT_CONFIGURE_TARGET_OUTPUTS_IN_DIRECTORY source_directory output_count)
	set(WT_OUTPUT_TARGET_COUNT 0)
	get_property(WT_DIRECTORY_TARGETS DIRECTORY "${source_directory}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(WT_OUTPUT_TARGET IN LISTS WT_DIRECTORY_TARGETS)
		get_target_property(WT_OUTPUT_IMPORTED ${WT_OUTPUT_TARGET} IMPORTED)
		get_target_property(WT_OUTPUT_TYPE ${WT_OUTPUT_TARGET} TYPE)
		if(WT_OUTPUT_IMPORTED OR
			WT_OUTPUT_TYPE STREQUAL "INTERFACE_LIBRARY" OR
			WT_OUTPUT_TYPE STREQUAL "UTILITY" OR
			WT_OUTPUT_TYPE STREQUAL "OBJECT_LIBRARY")
			continue()
		endif()

		if(WT_OUTPUT_TYPE STREQUAL "STATIC_LIBRARY" OR
			WT_OUTPUT_TARGET STREQUAL "WTSTools" OR
			WT_OUTPUT_TARGET STREQUAL "WTSUtils")
			wt_set_target_library_output_directory(${WT_OUTPUT_TARGET})
		elseif(WT_OUTPUT_TARGET MATCHES "^(CTPLoader|CTPOptLoader|LoaderRunner)$")
			wt_set_target_output_directory(${WT_OUTPUT_TARGET} Loader)
		elseif(WT_OUTPUT_TARGET MATCHES "^(WtPorter|WtExecMon|TraderDumper|TestExecPorter|TestPorter)$")
			wt_set_target_output_directory(${WT_OUTPUT_TARGET} WtPorter)
		elseif(WT_OUTPUT_TARGET MATCHES "^(WtDtPorter|WtDtHelper|WtDtServo|TestDtPorter)$")
			wt_set_target_output_directory(${WT_OUTPUT_TARGET} WtDtPorter)
		elseif(WT_OUTPUT_TARGET MATCHES "^(WtBtPorter|TestBtPorter)$")
			wt_set_target_output_directory(${WT_OUTPUT_TARGET} WtBtPorter)
		elseif(WT_OUTPUT_TARGET MATCHES "^(WtUftRunner|WtUftStraFact)$")
			wt_set_target_output_directory(${WT_OUTPUT_TARGET} WtUftRunner)
		elseif(WT_OUTPUT_TARGET STREQUAL "TestDllHelperFixture")
			wt_set_target_output_directory(${WT_OUTPUT_TARGET} TestUnits)
		elseif(WT_OUTPUT_TARGET MATCHES "^(WtRunner|WtBtRunner|WtLatencyHFT|WtLatencyUFT|QuoteFactory|TestParser|TestTrader|TestUnits)$")
			wt_set_target_output_directory(${WT_OUTPUT_TARGET} ${WT_OUTPUT_TARGET})
		else()
			wt_set_target_bin_output_directory(${WT_OUTPUT_TARGET})
		endif()
		math(EXPR WT_OUTPUT_TARGET_COUNT "${WT_OUTPUT_TARGET_COUNT} + 1")
	endforeach()

	get_property(WT_OUTPUT_SUBDIRECTORIES DIRECTORY "${source_directory}" PROPERTY SUBDIRECTORIES)
	foreach(WT_OUTPUT_SUBDIRECTORY IN LISTS WT_OUTPUT_SUBDIRECTORIES)
		WT_CONFIGURE_TARGET_OUTPUTS_IN_DIRECTORY("${WT_OUTPUT_SUBDIRECTORY}" WT_OUTPUT_SUBDIRECTORY_COUNT)
		math(EXPR WT_OUTPUT_TARGET_COUNT "${WT_OUTPUT_TARGET_COUNT} + ${WT_OUTPUT_SUBDIRECTORY_COUNT}")
	endforeach()
	set(${output_count} ${WT_OUTPUT_TARGET_COUNT} PARENT_SCOPE)
endfunction()
