# Contribution plan: CDUMM & CrimsonForge

Concrete, scoped ways to give back to the two main open-source tools, designed so the signature DB and the existing ecosystem reinforce each other. Each item lists the rationale and a realistic first PR.

## CDUMM (`faisalkindi/CrimsonDesert-UltimateModsManager`, MIT, Python)

CDUMM already does delta PAZ/PAMT patching, PAPGT rebuild, 3-level conflict detection, an ASI tab, and **detects hook conflicts between ASI plugins**. That last feature is the natural hook for this repo.

**Proposal A — function-level ASI conflict detection (highest value).**
Today CDUMM can tell that two `.asi` plugins collide. With a shared signature DB it could say *which engine function* they both hook. A plugin that resolves via `cdsig` knows the `id`s it targets; if plugins declare their targeted signature `id`s (e.g. a small sidecar `plugin.modinfo.json` listing `["camera.params.update"]`), CDUMM can cross-reference and report "Plugin X and Plugin Y both hook `camera.params.update`."
- *First PR:* define the optional sidecar field + parse it in the ASI tab + show targeted `id`s per plugin. Pure-additive, no behavior change for existing mods.

**Proposal B — version awareness via the DB.**
CDUMM already warns on game-version mismatches. It could read `signatures.json`'s `target.gameVersion` to warn when installed signature-based plugins were built for a different build than the user's `CrimsonDesert.exe` (whose version CDUMM can read).
- *First PR:* a small helper that reads the exe's `FileVersion` and compares against a bundled/known DB version; surface a one-line warning.

**Proposal C — tests / docs.**
The repo is Python with a `tests/` dir. Contribute unit tests around the delta/PAPGT logic or docs for the `.json` byte-patch format.
- *First PR:* add tests for conflict-detection edge cases, or document the modinfo schema.

## CrimsonForge (`hzeemr/crimsonforge`)

A modding studio (browse ~1.4M files, mesh/audio/font edit, checksum-valid writeback). Less directly tied to runtime signatures, but:

**Proposal D — surface an engine function map.**
If/when the signature DB grows, CrimsonForge could display known engine function `id`s/categories as a reference panel for plugin authors. Lightweight, optional.

**Proposal E — archive-format docs.**
Help document the `PATHC`/`PABGB`/`PABGH` layouts its pipeline understands, feeding [`archive-formats.md`](archive-formats.md). Benefits the whole community.

## Suggested order
1. Land this repo publicly (DB + `cdsig` + probe + docs).
2. Open **CDUMM Proposal A** as the flagship integration — it makes the DB immediately useful to end users via a tool they already run.
3. Contribute archive-format docs (Proposal E) since that knowledge is currently scattered.
4. Layer in version awareness (B) and tests (C).

## Etiquette
Credit prior art (Lazorr, PhorgeForge, faisalkindi, hzeemr) — the ecosystem norm is "share, study, improve, credit." Keep PRs small and additive; don't refactor maintainers' code uninvited.
