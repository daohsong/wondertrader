#!/usr/bin/env sh
set -eu

despath=${1:-}
if [ "$despath" = "" ]; then
	despath="../wtpy"
fi

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
root=${WT_BUILD_BIN:-"$script_dir/src/build_all/build_arm64/Release/bin"}
target="$despath/wtpy/wrapper/darwin"
folders="Loader WtBtPorter WtDtPorter WtPorter"
copied=0

if [ ! -d "$root" ]; then
	echo "error: build bin directory does not exist: $root" >&2
	echo "hint: build first or set WT_BUILD_BIN=/path/to/Release/bin" >&2
	exit 1
fi

echo "source bin path is $root"
echo "wtpy path is $despath"
echo "darwin wrapper path is $target"

mkdir -p "$target" "$target/executer" "$target/parsers" "$target/traders"

copy_dylib() {
	src=$1
	dst_dir=$2

	mkdir -p "$dst_dir"
	echo "copy: $src -> $dst_dir/$(basename "$src")"
	cp -f "$src" "$dst_dir/"
	copied=$((copied + 1))
}

for folder in $folders
do
	src_dir="$root/$folder"
	if [ ! -d "$src_dir" ]; then
		echo "skip: $src_dir does not exist"
		continue
	fi

	found=0
	for dylib in "$src_dir"/*.dylib
	do
		if [ -f "$dylib" ]; then
			copy_dylib "$dylib" "$target"
			found=1
		fi
	done

	for child in "$src_dir"/*
	do
		if [ ! -d "$child" ]; then
			continue
		fi

		child_name=$(basename "$child")
		child_found=0
		for dylib in "$child"/*.dylib
		do
			if [ -f "$dylib" ]; then
				copy_dylib "$dylib" "$target/$child_name"
				child_found=1
				found=1
			fi
		done

		if [ "$child_found" -eq 0 ]; then
			echo "skip: $child has no .dylib files"
		fi
	done

	if [ "$found" -eq 0 ]; then
		echo "skip: $src_dir has no .dylib files"
	fi
done

echo "copied $copied dylib file(s)"
