#!/usr/bin/env sh
set -eu

script_dir=$(
	CDPATH= cd "$(dirname "$0")" && pwd
)
cd "$script_dir"

CMAKE_BIN=${CMAKE_BIN:-cmake}

if [ -n "${WT_BUILD_JOBS:-}" ]; then
	jobs=$WT_BUILD_JOBS
else
	jobs=
	if command -v nproc >/dev/null 2>&1; then
		jobs=$(nproc 2>/dev/null || true)
	fi
	if [ -z "$jobs" ] && command -v sysctl >/dev/null 2>&1; then
		jobs=$(sysctl -n hw.ncpu 2>/dev/null || true)
	fi
	if [ -z "$jobs" ]; then
		jobs=4
	fi
fi

"$CMAKE_BIN" -S . -B build_all -DCMAKE_BUILD_TYPE=Release
"$CMAKE_BIN" --build build_all --config Release --parallel "$jobs"
