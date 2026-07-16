include(CheckCXXSourceCompiles)

function(WT_SETUP_FILESYSTEM_DEPENDENCY WT_OUTPUT_TARGET)
	if(TARGET WT::Filesystem)
		set(${WT_OUTPUT_TARGET} WT::Filesystem PARENT_SCOPE)
		return()
	endif()

	set(WT_FILESYSTEM_PROBE_SOURCE [[
		#include <filesystem>
		#include <system_error>

		int main()
		{
			std::error_code error;
			const std::filesystem::path path = std::filesystem::current_path(error);
			return error ? 1 : static_cast<int>(path.empty());
		}
	]])

	set(WT_SAVED_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")
	set(CMAKE_REQUIRED_LIBRARIES "")
	# This is a link probe even when a cross toolchain defaults try_compile to
	# static libraries to bypass executable linker checks.
	set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
	check_cxx_source_compiles(
		"${WT_FILESYSTEM_PROBE_SOURCE}"
		WT_FILESYSTEM_LINKS_WITHOUT_EXTRA_LIBRARY
	)

	set(WT_FILESYSTEM_EXTRA_LIBRARY "")
	if(NOT WT_FILESYSTEM_LINKS_WITHOUT_EXTRA_LIBRARY)
		set(CMAKE_REQUIRED_LIBRARIES stdc++fs)
		check_cxx_source_compiles(
			"${WT_FILESYSTEM_PROBE_SOURCE}"
			WT_FILESYSTEM_LINKS_WITH_STDCXXFS
		)
		if(WT_FILESYSTEM_LINKS_WITH_STDCXXFS)
			set(WT_FILESYSTEM_EXTRA_LIBRARY stdc++fs)
		endif()
	endif()
	set(CMAKE_REQUIRED_LIBRARIES "${WT_SAVED_REQUIRED_LIBRARIES}")

	if(NOT WT_FILESYSTEM_LINKS_WITHOUT_EXTRA_LIBRARY
		AND NOT WT_FILESYSTEM_LINKS_WITH_STDCXXFS)
		message(FATAL_ERROR
			"The selected C++${CMAKE_CXX_STANDARD} toolchain cannot link std::filesystem "
			"with either its default libraries or stdc++fs. Compiler: "
			"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. "
			"See CMakeFiles/CMakeError.log for the probe output."
		)
	endif()

	add_library(WT_FILESYSTEM INTERFACE)
	add_library(WT::Filesystem ALIAS WT_FILESYSTEM)
	if(WT_FILESYSTEM_EXTRA_LIBRARY)
		target_link_libraries(WT_FILESYSTEM INTERFACE "${WT_FILESYSTEM_EXTRA_LIBRARY}")
		message(STATUS "std::filesystem requires ${WT_FILESYSTEM_EXTRA_LIBRARY}")
	else()
		message(STATUS "std::filesystem links without an extra compatibility library")
	endif()

	set(${WT_OUTPUT_TARGET} WT::Filesystem PARENT_SCOPE)
endfunction()
