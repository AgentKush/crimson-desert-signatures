#!/usr/bin/env python3
"""Validate signatures/signatures.json. Stdlib only. Exit non-zero on error.

Usage:  python tools/validate_signatures.py
"""
import json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DB = ROOT / "signatures" / "signatures.json"

ID_RE = re.compile(r"^[a-z0-9]+(\.[a-z0-9]+)+$")
HEX_RE = re.compile(r"^([0-9A-Fa-f]{2}|\?\??)(\s+([0-9A-Fa-f]{2}|\?\??))*$")
RESOLVE = {"none", "rip32"}
REQUIRED = {"id", "name", "category", "pattern", "offset", "resolve", "verified", "gameVersion"}

def main() -> int:
    errors, warnings = [], []
    try:
        data = json.loads(DB.read_text(encoding="utf-8"))
    except Exception as e:
        print(f"FATAL: cannot parse {DB}: {e}")
        return 1

    target = data.get("target", {})
    if not target.get("module") or not target.get("gameVersion"):
        errors.append("target.module and target.gameVersion are required")

    seen = set()
    entries = data.get("entries", [])
    for i, e in enumerate(entries):
        where = f"entries[{i}] ({e.get('id', '?')})"
        missing = REQUIRED - e.keys()
        if missing:
            errors.append(f"{where}: missing {sorted(missing)}")
            continue
        if not ID_RE.match(e["id"]):
            errors.append(f"{where}: bad id format (want lowercase.dotted)")
        if e["id"] in seen:
            errors.append(f"{where}: duplicate id")
        seen.add(e["id"])
        if e["resolve"] not in RESOLVE:
            errors.append(f"{where}: resolve must be one of {sorted(RESOLVE)}")
        if not isinstance(e["offset"], int):
            errors.append(f"{where}: offset must be an integer")
        if not isinstance(e["verified"], bool):
            errors.append(f"{where}: verified must be a boolean")
        pat = e["pattern"].strip()
        if pat and not HEX_RE.match(pat):
            errors.append(f"{where}: pattern is not valid IDA-style hex/?? bytes")
        if e["verified"] and not pat:
            errors.append(f"{where}: verified=true but pattern is empty")
        if not e["verified"] and pat:
            warnings.append(f"{where}: has a pattern but verified=false (confirm uniqueness, then flip)")

    total = len(entries)
    verified = sum(1 for e in entries if e.get("verified"))
    print(f"DB: {DB.relative_to(ROOT)}")
    print(f"target: {target.get('module')} @ {target.get('gameVersion')}")
    print(f"entries: {total} | verified: {verified} | placeholders: {total - verified}")
    for w in warnings: print(f"  warn: {w}")
    for er in errors: print(f"  ERROR: {er}")
    if errors:
        print("FAILED")
        return 1
    print("OK")
    return 0

if __name__ == "__main__":
    sys.exit(main())
