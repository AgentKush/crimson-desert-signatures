# Capturing signatures from a protected binary

`CrimsonDesert.exe` (build `1.0.0.1744`) is **packed / anti-tampered**: its on-disk PE has a tiny real `.text` (~36 KB) and oversized, renamed, runtime-decrypted sections (`.xcode` ~74 MB executable, `.sbss` ~299 MB writable+executable). The real game code does not exist in readable form on disk — it's decrypted into memory when the game runs.

**Consequence:** you cannot build signatures by loading the `.exe` into IDA/Ghidra statically. Signatures must be captured from the **live process**, after the game has decrypted its own code, from inside an injected ASI plugin (the normal modding path) — never by defeating or stripping the protection.

This is single-player interoperability work. Keep it to a copy you own; don't attempt anything that targets anti-tamper itself or that would matter for online play.

## The loop

### 1. Confirm the live layout
Build and inject [`examples/cdsig_probe.cpp`](../examples/cdsig_probe.cpp) (see [`first-asi-plugin.md`](first-asi-plugin.md)). On launch it prints the in-memory section table. You should see `.xcode` / `.sbss` flagged executable — that's the decrypted code `cdsig::scan` will search. This confirms the approach works on your build.

### 2. Find the data address (dynamic)
For a gameplay value (player X/Y/Z, stamina, camera distance), find its **memory address** first:
- **Cheat Engine** is the usual tool: scan for the value, move/change it in-game, narrow to the address.
- Note: anti-tamper can interfere with external debuggers. Prefer reading memory from **inside** your injected ASI when external tools are blocked. Do **not** try to circumvent the protection — if a tool is blocked, switch to in-process reading, don't fight the DRM.

### 3. Find the code that touches it
- Cheat Engine's "Find out what writes to this address" gives you an instruction address inside the responsible function.
- From that instruction, walk to the function's start (or a stable nearby anchor).

### 4. Build a pattern
- Take ~10–20 bytes of opcodes at the anchor. Mask operands that change between builds (addresses, `rip`-relative displacements) with `??`.
- If you anchored on a `call`/`lea` to the function, record the `offset` to the displacement and set `resolve: "rip32"` so consumers can follow it to the real target.

### 5. Verify uniqueness
- Put the candidate in `cdsig_probe.cpp`'s `TEST_SIGNATURE`, re-inject, and read `signature matches:`. **You want exactly 1.** If 0 → too strict/wrong; if >1 → lengthen it.

### 6. Record it
- Add the entry to [`signatures/signatures.json`](../signatures/signatures.json) with `verified: true`, the exact `gameVersion`, your handle, and a `notes` line on where the anchor is.
- Run `python tools/validate_signatures.py`, open a PR.

## Why patterns beat offsets here
A raw address (`base + 0x1234560`) is dead the moment the game updates. A signature re-locates the function in the new build by its code shape — so when build `1.0.0.1744` becomes `1.0.0.18xx`, one person re-verifies the pattern (or tweaks it) and **every** ASI mod that resolves via `cdsig` keeps working. That's the whole point of this repo.

## Reality check
Anti-tamper can virtualize or mutate some functions, which makes a few targets hard or unstable to pattern. The functions the community already hooks (camera, player position, interaction timers) are reachable in plain x86-64 in memory, so start there. Treat "this one won't pattern cleanly" as expected for a protected title, not failure.
