#!/usr/bin/env python3
"""Require every tracked non-binary file to use strict UTF-8 encoding."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


def tracked_files(repo_root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "-C", str(repo_root), "ls-files", "-z"],
        check=True,
        stdout=subprocess.PIPE,
    )
    return [repo_root / os.fsdecode(name) for name in result.stdout.split(b"\0") if name]


def validate(repo_root: Path) -> int:
    failures: list[str] = []
    checked = 0
    skipped_binary = 0

    for path in tracked_files(repo_root):
        data = path.read_bytes()
        relative = path.relative_to(repo_root)

        if data.startswith((b"\xff\xfe", b"\xfe\xff")):
            failures.append(f"{relative}: UTF-16 BOM is not allowed")
            continue

        if b"\0" in data:
            skipped_binary += 1
            continue

        checked += 1
        try:
            data.decode("utf-8", errors="strict")
        except UnicodeDecodeError as error:
            invalid = data[error.start : error.start + 8].hex(" ")
            failures.append(
                f"{relative}: invalid UTF-8 at byte {error.start} ({invalid})"
            )

    if failures:
        print("Tracked text files must use strict UTF-8:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        print(
            f"Checked {checked} text candidates; skipped {skipped_binary} binary files; "
            f"found {len(failures)} encoding errors.",
            file=sys.stderr,
        )
        return 1

    print(
        f"UTF-8 validation passed for {checked} tracked text candidates "
        f"({skipped_binary} binary files skipped)."
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    return validate(args.repo_root.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
