# Crimson Desert Signatures (`cdsig`)

A community-maintained **engine signature/offset database** for *Crimson Desert* (BlackSpace engine), plus a tiny **header-only runtime scanner** so ASI plugins resolve game functions by **byte-pattern signature instead of hardcoded offsets** — which means **mods survive game patches**.

> **Why this exists.** Crimson Desert has no official mod tools or SDK. The community has great *tooling* (unpackers, mod managers, a full modding studio) but no shared *engine map*. Today every ASI modder re-discovers the same functions, and every game update breaks every hardcoded offset. This repo is the missing foundation: a shared, version-tagged function map + a resolver that makes mods patch-resilient. It's the Crimson Desert equivalent of the "address library" pattern that made Skyrim's SKSE ecosystem sustainable.

---

## ⚠️ Status: framework seeded, signatures wanted

This repo ships the **complete framework** — schema, runtime scanner, an injectable probe, validator, docs. What it does **not** yet ship is verified signature **data**.

**Confirmed by inspecting the install:** the target is `CrimsonDesert.exe` build `1.0.0.1744`, and it is **packed / anti-tampered**. Its on-disk PE has a real `.text` of only ~36 KB; the actual code lives in oversized, renamed, runtime-decrypted sections (`.xcode` ~74 MB executable, `.sbss` ~299 MB writable+executable). So signatures **cannot** be pulled from the on-disk file with a static disassembler — they must be captured from the **live process in memory** via an injected ASI (the normal modding path), using [`examples/cdsig_probe.cpp`](examples/cdsig_probe.cpp) and the workflow in [`docs/capturing-signatures.md`](docs/capturing-signatures.md). This is also exactly why `cdsig` scans the *loaded module*, not the file.

We deliberately do **not** ship invented patterns — a wrong signature resolves to the wrong address and crashes the game. The seed entries in [`signatures/signatures.json`](signatures/signatures.json) are **placeholders** for functions the community already hooks (player position, camera, interaction timers), each `verified: false` with notes on what to look for.

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`docs/capturing-signatures.md`](docs/capturing-signatures.md) for how to capture and submit one.

---

## What's in here

```
crimson-desert-signatures/
├── README.md                     ← you are here
├── CONTRIBUTING.md               ← how to capture & submit a signature
├── LICENSE                       ← MIT
├── signatures/
│   └── signatures.json           ← the database (seeded with placeholders)
├── schema/
│   └── signature.schema.json     ← JSON Schema; validates every entry
├── include/
│   └── cdsig.hpp                 ← header-only C++ AOB scanner (the resolver)
├── examples/
│   └── cdsig_probe.cpp           ← injectable diagnostic ASI: dump live sections + test sigs
├── tools/
│   └── validate_signatures.py    ← CI/local validation of signatures.json
└── docs/
    ├── capturing-signatures.md   ← capture sigs from the LIVE process (packed binary)
    ├── first-asi-plugin.md       ← build & inject your first ASI (VS toolchain)
    ├── archive-formats.md        ← PAZ/PAMT/PAPGT/… — what's known, what's not
    └── cdumm-integration-plan.md ← concrete contribution proposals for CDUMM/CrimsonForge
```

## Quickstart (ASI plugin authors)

1. Drop [`include/cdsig.hpp`](include/cdsig.hpp) into your plugin project (header-only, no dependencies).
2. Resolve a function by signature instead of hardcoding an address:

```cpp
#include "cdsig.hpp"

// Pattern copied from signatures/signatures.json (IDA-style; ?? = wildcard).
auto addr = cdsig::scan("48 8B ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B");
if (addr) {
    // resolved against the live module base for THIS game build → patch-resilient
    MH_CreateHook(reinterpret_cast<void*>(*addr), &Detour, reinterpret_cast<void**>(&oReal));
}
```

3. If the next patch shifts code around, you re-capture **one signature** in the shared DB instead of every mod re-finding its offsets. See [`examples/cdsig_probe.cpp`](examples/cdsig_probe.cpp) for a complete, injectable example and [`docs/first-asi-plugin.md`](docs/first-asi-plugin.md) for building it.

## How to contribute a signature

Short version (full version in [`CONTRIBUTING.md`](CONTRIBUTING.md)):

1. Identify the function in IDA/Ghidra/x64dbg on your own copy.
2. Build a unique byte pattern (mask volatile operands with `??`).
3. Verify it yields exactly **one** match in the current build.
4. Add an entry to `signatures/signatures.json`, run `python tools/validate_signatures.py`, open a PR.

## Responsible use

This is single-player modding interoperability tooling. Please:

- Only run it against a copy of the game **you own**; capture signatures yourself.
- Keep it to **single-player** use. Crimson Desert has no multiplayer or anti-cheat at the time of writing — don't build things that would change that calculus or violate the game's ToS.
- **Do not** commit game binaries, extracted assets, or copyrighted data to this repo — signatures (byte patterns) and offsets only.
- Nothing here is affiliated with or endorsed by Pearl Abyss.

## Relationship to the bigger picture

A shared engine map + patch-resilient resolver is **also** the prerequisite layer for any deeper ambition (a script-extender framework, or — long term — multiplayer experiments). Helping the community here lowers the floor for *everyone* and de-risks the harder projects later. See `../Crimson-Desert-Multiplayer-Feasibility.md` for that context.

## Credits / prior art this builds on

- **Lazorr** — Crimson Desert Unpacker (PAZ parsing/repacking)
- **PhorgeForge** — JSON Mod Manager (byte-patch format)
- **faisalkindi** — CDUMM (delta patching, ASI management, conflict detection)
- **hzeemr** — CrimsonForge (modding studio)
- The wider Nexus/GitHub Crimson Desert modding community

## License

MIT — see [`LICENSE`](LICENSE).
