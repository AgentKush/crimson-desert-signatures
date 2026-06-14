# Crimson Desert archive formats (reference)

Crimson Desert stores game data in Pearl Abyss container formats. This is a **pointer**, not a from-scratch spec — the authoritative implementations are the community tools below, which already parse and repack these. Don't duplicate them; build on them.

> ⚠️ This file deliberately does not invent byte layouts. Where a layout isn't stated here, read it from the source of the tools listed at the bottom and contribute corrections.

## Known containers

| Ext | Role (confirmed by community tooling) |
|---|---|
| `PAZ` | The archive that holds packed game files. Parsed/repacked by Lazorr's Unpacker; delta-patched by CDUMM. |
| `PAMT` | Metadata / index table describing entries inside the PAZ set (paths → locations). |
| `PAPGT` | Integrity/hash registry ("page table") — must be rebuilt after edits or the game rejects files. |
| `PATHC` | Pearl Abyss format reported in community reverse-engineering of the archive set; layout TBD here. |
| `PABGB` | Pearl Abyss format reported alongside the above; layout TBD here. |
| `PABGH` | Pearl Abyss format reported alongside the above; layout TBD here. |

The first three (`PAZ`/`PAMT`/`PAPGT`) are the ones mod managers actively read, write, and re-checksum. The last three are documented as existing in community RE work; capture their exact structure from the tools/source before relying on it.

## How edits stay valid
The integrity chain matters: editing data inside a `PAZ` requires updating the `PAMT` sizes/paths and rebuilding the `PAPGT` hash registry, or the game refuses to load. CDUMM automates exactly this ("Rebuilds the PAPGT integrity chain so the game accepts the modified files"). If you write your own pipeline, mirror that step.

## Build on these (prior art)
- **Lazorr — Crimson Desert Unpacker** — PAZ parsing/repacking. <https://www.nexusmods.com/crimsondesert/mods/62>
- **faisalkindi — CDUMM** — delta PAZ/PAMT patching + PAPGT rebuild + conflict detection (MIT, Python). <https://github.com/faisalkindi/CrimsonDesert-UltimateModsManager>
- **hzeemr — CrimsonForge** — modding studio that browses ~1.4M files and writes back with valid checksums. <https://github.com/hzeemr/crimsonforge>
- **PhorgeForge — JSON Mod Manager** — JSON byte-patch format. <https://www.nexusmods.com/crimsondesert/mods/113>

## How to contribute here
If you verify a field layout (from the tools' source or your own RE of a file you own), add it as a clearly-sourced table and cite where it came from. Same rule as signatures: **don't guess** — a wrong offset corrupts people's game files.
