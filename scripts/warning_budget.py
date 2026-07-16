#!/usr/bin/env python3
"""Normalize compiler diagnostics and reject warnings outside a committed budget."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path


COMPILER_WARNING = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): warning: "
    r"(?P<message>.*?)(?: \[(?P<option>-W[^\]]+)\])?$"
)
LINKER_WARNING = re.compile(r"^(?P<tool>(?:ld|clang\+\+|c\+\+)): warning: (?P<message>.+)$")


def normalize_path(raw_path: str, source_root: Path) -> str:
    path = Path(raw_path)
    try:
        return path.resolve().relative_to(source_root).as_posix()
    except ValueError:
        marker = "/src/"
        normalized = raw_path.replace("\\", "/")
        marker_index = normalized.find(marker)
        if marker_index != -1:
            return normalized[marker_index + 1 :]
        return normalized


def collect_diagnostics(log_text: str, source_root: Path) -> list[dict[str, object]]:
    unique: dict[str, dict[str, object]] = {}
    for raw_line in log_text.splitlines():
        line = raw_line.strip()
        compiler_match = COMPILER_WARNING.match(line)
        if compiler_match:
            path = normalize_path(compiler_match.group("path"), source_root)
            option = compiler_match.group("option") or "compiler-warning"
            message = compiler_match.group("message").strip()
            diagnostic = {
                "file": path,
                "line": int(compiler_match.group("line")),
                "column": int(compiler_match.group("column")),
                "option": option,
                "message": message,
            }
        else:
            linker_match = LINKER_WARNING.match(line)
            if not linker_match:
                continue
            diagnostic = {
                "file": linker_match.group("tool"),
                "line": 0,
                "column": 0,
                "option": "linker-warning",
                "message": linker_match.group("message").strip(),
            }

        key = diagnostic_key(diagnostic)
        unique[key] = diagnostic

    return [unique[key] for key in sorted(unique)]


def diagnostic_key(diagnostic: dict[str, object]) -> str:
    return "{file}:{line}:{column}:{option}:{message}".format(**diagnostic)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    diagnostics = collect_diagnostics(args.log.read_text(encoding="utf-8", errors="replace"), source_root)
    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    if baseline.get("schema_version") != 1:
        raise ValueError("warning baseline schema_version must be 1")

    allowed = set(baseline.get("allowed_diagnostics", []))
    current_keys = {diagnostic_key(diagnostic) for diagnostic in diagnostics}
    new_keys = sorted(current_keys - allowed)
    option_counts = Counter(str(diagnostic["option"]) for diagnostic in diagnostics)
    summary = {
        "schema_version": 1,
        "unique_count": len(diagnostics),
        "new_count": len(new_keys),
        "option_counts": dict(sorted(option_counts.items())),
        "diagnostics": diagnostics,
        "new_diagnostics": new_keys,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if new_keys:
        print(f"warning budget exceeded: {len(new_keys)} new unique diagnostic(s)", file=sys.stderr)
        for key in new_keys:
            print(f"  {key}", file=sys.stderr)
        return 1

    print(f"warning budget accepted: {len(diagnostics)} unique diagnostic(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
