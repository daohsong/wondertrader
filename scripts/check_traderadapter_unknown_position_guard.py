#!/usr/bin/env python3
"""Verify TraderAdapter blocks ready when upstream returns nonzero unknown positions."""
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "WtCore" / "TraderAdapter.cpp"
PLUGIN_SOURCES = [
    ROOT / "src" / "TraderATP" / "TraderATP.cpp",
    ROOT / "src" / "TraderCTP" / "TraderCTP.cpp",
    ROOT / "src" / "TraderOES" / "TraderOES.cpp",
    ROOT / "src" / "TraderXTP" / "TraderXTP.cpp",
]


def main() -> int:
    text = SOURCE.read_text(encoding="utf-8-sig")
    required = [
        "hasUnknownPositions",
        "missing contract info for nonzero upstream position",
        "Position query blocked by unknown upstream positions",
        "return;",
        "_state = AS_POSITION_QRYED",
    ]
    missing = [item for item in required if item not in text]
    if missing:
        print("TraderAdapter unknown-position guard is incomplete:")
        for item in missing:
            print(f"  missing: {item}")
        return 1

    guard_pos = text.index("hasUnknownPositions")
    ready_pos = text.index("_state = AS_POSITION_QRYED")
    if guard_pos > ready_pos:
        print("Unknown-position guard must run before AS_POSITION_QRYED transition")
        return 1

    missing_plugins = []
    for source in PLUGIN_SOURCES:
        plugin_text = source.read_text(encoding="utf-8-sig")
        if "BT_UNKNOWN" not in plugin_text:
            missing_plugins.append(str(source.relative_to(ROOT)))
    if missing_plugins:
        print("Trader plugins must forward unknown-contract positions to TraderAdapter:")
        for path in missing_plugins:
            print(f"  missing BT_UNKNOWN unknown-position path: {path}")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
