# Contributing a signature

The whole value of this repo is **verified, unique, version-tagged byte patterns** for engine functions. This guide explains how to capture one and submit it.

## What makes a good signature

A signature is a short sequence of machine-code bytes — taken from the *inside* of a function (or a stable reference to it) — with volatile bytes masked out as wildcards. A good signature is:

- **Unique** in the module for the targeted build (exactly one match).
- **Stable** — taken from instruction *opcodes*, not from operands that change between builds (mask addresses, relative offsets, and register-allocation-sensitive bytes with `??`).
- **Tagged** with the exact game build/version it was captured on.

IDA-style format, space-separated hex, `??` for wildcards:

```
48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ??
```

## Workflow

You can use any of IDA Pro, Ghidra (free), or x64dbg (free). Rough flow:

1. **Find the function.** Locate the behavior you care about (e.g., the function that writes the player's world position). Cheat Engine / x64dbg "find what writes to this address" is a common way in; then move to a static disassembler to read the function.
2. **Pick an anchor.** Choose a distinctive run of bytes at or near the function start. Prefer opcodes with few operands.
3. **Mask the volatile bytes.** Replace anything that's an address, a `rip`-relative displacement, or otherwise build-specific with `??`. Many tools have a "create signature" / "copy pattern" feature.
4. **Verify uniqueness.** Scan the loaded module for your pattern — it must match **exactly once**. If it matches 0 or many times, lengthen/adjust it. (`cdsig::scan` returns the first match; for verification, count all matches.)
5. **Record how to use it.** If you actually need the *address of the function* but anchored from a `call`/`lea`, note the `offset` to the displacement field and set `resolve: "rip32"` so consumers can follow it.

## Add the entry

Edit [`signatures/signatures.json`](signatures/signatures.json). Each entry:

```json
{
  "id": "player.position.write",
  "name": "Player position write",
  "description": "Writes the player actor's world-space position each frame.",
  "category": "player",
  "module": "CrimsonDesert.exe",
  "gameVersion": "1.0.3",
  "pattern": "48 8B 05 ?? ?? ?? ?? F3 0F 10 ??",
  "offset": 0,
  "resolve": "none",
  "verified": true,
  "contributor": "yourname",
  "references": ["https://www.nexusmods.com/crimsondesert/mods/..."],
  "notes": "Anchor at function prologue. Mask is the rip-rel global load."
}
```

Field rules are enforced by [`schema/signature.schema.json`](schema/signature.schema.json):

- `id` — unique, `lowercase.dotted.path`.
- `resolve` — `"none"` (pattern points at what you want), or `"rip32"` (follow a 32-bit rip-relative displacement at `offset`).
- `verified` — only `true` if **you confirmed a single match** on the stated `gameVersion`. Placeholder/unconfirmed entries stay `false`.
- `references` — links to the mod, write-up, or thread the signature came from (optional but encouraged).

## Before you open the PR

```bash
python tools/validate_signatures.py
```

This checks the JSON against the schema, flags duplicate `id`s, and prints a coverage summary. Green = good to go.

## PR checklist

- [ ] `validate_signatures.py` passes.
- [ ] `gameVersion` is the exact build you captured on.
- [ ] Pattern verified to match exactly once on that build.
- [ ] `verified` set honestly (`false` if you couldn't confirm uniqueness).
- [ ] No game binaries / extracted assets committed — patterns and offsets only.

## House rules

- Single-player interoperability only. Don't submit anything whose purpose is to defeat anti-tamper or enable ToS-violating online behavior.
- Capture signatures from your **own** copy. Don't paste proprietary decompiled source.
- Be generous with `notes` — the next person re-verifying after a patch will thank you.
