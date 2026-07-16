cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED WT_WORKFLOWS_DIR OR NOT IS_DIRECTORY "${WT_WORKFLOWS_DIR}")
	message(FATAL_ERROR "WT_WORKFLOWS_DIR must name the GitHub Actions workflow directory")
endif()

file(GLOB WT_WORKFLOW_FILES
	LIST_DIRECTORIES FALSE
	"${WT_WORKFLOWS_DIR}/*.yml"
	"${WT_WORKFLOWS_DIR}/*.yaml"
)
list(LENGTH WT_WORKFLOW_FILES WT_WORKFLOW_COUNT)
if(NOT WT_WORKFLOW_COUNT EQUAL 1)
	message(FATAL_ERROR "Expected exactly one GitHub Actions workflow, found ${WT_WORKFLOW_COUNT}: ${WT_WORKFLOW_FILES}")
endif()

set(WT_WORKFLOW_FILE "${WT_WORKFLOWS_DIR}/portability.yml")
if(NOT EXISTS "${WT_WORKFLOW_FILE}")
	message(FATAL_ERROR "The single workflow must be ${WT_WORKFLOW_FILE}")
endif()
file(READ "${WT_WORKFLOW_FILE}" WT_WORKFLOW)

function(WT_REQUIRE_WORKFLOW_TEXT WT_TEXT WT_DESCRIPTION)
	string(FIND "${WT_WORKFLOW}" "${WT_TEXT}" WT_TEXT_INDEX)
	if(WT_TEXT_INDEX EQUAL -1)
		message(FATAL_ERROR "Workflow must ${WT_DESCRIPTION}; missing '${WT_TEXT}'")
	endif()
endfunction()

function(WT_REJECT_WORKFLOW_TEXT WT_TEXT WT_DESCRIPTION)
	string(FIND "${WT_WORKFLOW}" "${WT_TEXT}" WT_TEXT_INDEX)
	if(NOT WT_TEXT_INDEX EQUAL -1)
		message(FATAL_ERROR "Workflow must not ${WT_DESCRIPTION}; found '${WT_TEXT}'")
	endif()
endfunction()

string(REGEX MATCHALL "runs-on:" WT_RUNNER_ENTRIES "${WT_WORKFLOW}")
list(LENGTH WT_RUNNER_ENTRIES WT_RUNNER_COUNT)
if(NOT WT_RUNNER_COUNT EQUAL 3)
	message(FATAL_ERROR "Workflow must define exactly three jobs with runners; found ${WT_RUNNER_COUNT}")
endif()

WT_REQUIRE_WORKFLOW_TEXT("  macos-arm64:" "define the macOS arm64 job")
WT_REQUIRE_WORKFLOW_TEXT("    name: macOS arm64" "name the macOS arm64 job")
WT_REQUIRE_WORKFLOW_TEXT("    runs-on: macos-26" "pin the macOS 26 arm64 runner")
WT_REQUIRE_WORKFLOW_TEXT("macos-arm64-cxx23" "use the macOS C++23 preset")

WT_REQUIRE_WORKFLOW_TEXT("  linux-x86-64:" "define the Linux x86_64 job")
WT_REQUIRE_WORKFLOW_TEXT("    name: Linux x86_64" "name the Linux x86_64 job")
WT_REQUIRE_WORKFLOW_TEXT("    runs-on: ubuntu-latest" "use the latest stable Linux x86_64 runner")
WT_REQUIRE_WORKFLOW_TEXT("linux-x64-cxx23" "use the Linux C++23 preset")

WT_REQUIRE_WORKFLOW_TEXT("  windows-amd64:" "define the Windows amd64 job")
WT_REQUIRE_WORKFLOW_TEXT("    name: Windows amd64" "name the Windows amd64 job")
WT_REQUIRE_WORKFLOW_TEXT("    runs-on: windows-latest" "use the latest stable Windows amd64 runner")
WT_REQUIRE_WORKFLOW_TEXT("windows-x64-cxx23" "use the Windows C++23 preset")

WT_REJECT_WORKFLOW_TEXT("matrix:" "use a job matrix")
WT_REJECT_WORKFLOW_TEXT("-debug" "run Debug presets")
WT_REJECT_WORKFLOW_TEXT("-cxx20" "run C++20 presets")
WT_REJECT_WORKFLOW_TEXT("-asan" "run sanitizer presets")
WT_REJECT_WORKFLOW_TEXT("macos-latest" "use the macOS 15 rolling alias")
WT_REJECT_WORKFLOW_TEXT("actions/checkout@v4" "use the deprecated Node.js 20 checkout action")
WT_REJECT_WORKFLOW_TEXT("actions/upload-artifact@v4" "use the deprecated Node.js 20 artifact action")

WT_REQUIRE_WORKFLOW_TEXT("actions/checkout@v6" "check out through the Node.js 24 action")
WT_REQUIRE_WORKFLOW_TEXT("actions/upload-artifact@v7" "archive through the Node.js 24 action")
WT_REQUIRE_WORKFLOW_TEXT("brew install" "install macOS dependencies")
WT_REQUIRE_WORKFLOW_TEXT("apt-get install" "install Linux dependencies")
WT_REQUIRE_WORKFLOW_TEXT("vcpkg install" "install Windows dependencies")
foreach(WT_WINDOWS_BOOST_PORT IN ITEMS
	boost-asio
	boost-circular-buffer
	boost-interprocess
	boost-pool
	boost-property-tree
	boost-xpressive
)
	WT_REQUIRE_WORKFLOW_TEXT("${WT_WINDOWS_BOOST_PORT}" "install the directly included ${WT_WINDOWS_BOOST_PORT} headers")
endforeach()

WT_REQUIRE_WORKFLOW_TEXT("uname -m" "verify Unix runner architectures")
WT_REQUIRE_WORKFLOW_TEXT("PROCESSOR_ARCHITECTURE" "verify the Windows runner architecture")
WT_REQUIRE_WORKFLOW_TEXT("cmake --preset" "configure through CMake presets")
WT_REQUIRE_WORKFLOW_TEXT("cmake --build --preset" "build through CMake presets")
WT_REQUIRE_WORKFLOW_TEXT("ctest --preset" "test through CMake presets")
WT_REQUIRE_WORKFLOW_TEXT("scripts/warning_budget.py" "enforce the macOS normalized warning budget")
WT_REQUIRE_WORKFLOW_TEXT("warning-baseline-macos-arm64.json" "use the committed macOS warning baseline")
WT_REQUIRE_WORKFLOW_TEXT("lipo -archs" "verify Mach-O architecture slices")
WT_REQUIRE_WORKFLOW_TEXT("set -o pipefail" "propagate Unix build failures through tee")
WT_REQUIRE_WORKFLOW_TEXT("$LASTEXITCODE" "propagate Windows command failures through Tee-Object")
WT_REQUIRE_WORKFLOW_TEXT("Select-String -Path $configureLog" "extract structured Windows configure errors from the complete log")
WT_REQUIRE_WORKFLOW_TEXT("Select-String -Path $buildLog" "extract structured Windows build errors from the complete log")
WT_REQUIRE_WORKFLOW_TEXT("warning C[0-9]+" "recognize MSVC warnings promoted to errors")
WT_REQUIRE_WORKFLOW_TEXT("error C[0-9]+" "recognize MSVC compiler errors")
WT_REQUIRE_WORKFLOW_TEXT("error LNK[0-9]+" "recognize MSVC linker errors")
WT_REQUIRE_WORKFLOW_TEXT("error MSB[0-9]+" "recognize MSBuild errors")
WT_REQUIRE_WORKFLOW_TEXT("Get-Content $configureLog -Tail 200" "retain enough configure context when no structured error is found")
WT_REQUIRE_WORKFLOW_TEXT("Get-Content $buildLog -Tail 200" "retain enough build context when no structured error is found")
WT_REQUIRE_WORKFLOW_TEXT("::error title=CMake configure::" "expose Windows configure diagnostics as check annotations")
WT_REQUIRE_WORKFLOW_TEXT("::error title=CMake build::" "expose Windows build diagnostics as check annotations")
WT_REQUIRE_WORKFLOW_TEXT("if: always()" "archive diagnostics even after failure")
