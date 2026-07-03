#!/usr/bin/env python3
"""Reject C/C++ source files that contain CRLF or mixed line endings."""

from __future__ import annotations

import sys
from pathlib import Path


def has_cr_line_endings(path: Path) -> bool:
    try:
        data = path.read_bytes()
    except FileNotFoundError:
        return False

    return b"\r" in data


def main(argv: list[str]) -> int:
    failed = []

    for name in argv:
        path = Path(name)
        if has_cr_line_endings(path):
            failed.append(name)

    if failed:
        print("C/C++ source files must use LF line endings:")
        for name in failed:
            print(f"  {name}")
        print("Convert these files to LF before committing.")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
