# MOBA client-patch tooling

Reusable tooling + documented process for adding **custom spells** (and other DBC
rows) to the WoW 3.3.5a client patch, with no external GUI tool. The goal: never
redo this reverse-engineering work again.

## Why a client patch is needed

A custom spell has three aligned pieces (see `AGENTS.md` > Spell Strategy):

1. **Server** - a `spell_dbc` row in `sql/custom` (the server injects the SpellInfo)
   plus a `spell_script_names` row.
2. **Logic** - a C++ `SpellScript` in `src/server/scripts/Custom/Moba` (base damage
   + AD/AP ratios read from the MOBA stats, cooldown, cost, effects), registered in
   the custom script loader.
3. **Client** - a row in the client `Spell.dbc` (+ `SkillLineAbility.dbc`) so the
   action button renders, the cast animation/range exist, and the tooltip/icon show.
   The client ships these DBCs inside an MPQ patch.

The server alone is not enough: if the client `Spell.dbc` does not know the spell ID,
the button is blank and the cast is rejected client-side.

## The client patch

`client-patches/frFR/patch-frFR-4.MPQ` is the tracked, generated patch. It currently
contains only:

```
DBFilesClient\Spell.dbc
DBFilesClient\SkillLineAbility.dbc
```

No BLP icons: custom spells reference an **existing** `SpellIconID`, so no art work.

Install (see `client-patches/README.md`): copy it to
`docker/client/.../Data/frFR/patch-frFR-4.MPQ`, delete the client `Cache` folder,
restart the client.

## Spell.dbc layout (3.3.5a)

- Header: `WDBC`, then `recordCount, fieldCount(=234), recordSize(=936), stringBlockSize`.
- Records are 234 little-endian 4-byte fields. String fields hold a byte offset into
  the string block (0 = empty string).
- Field map we need (0-based):
  - `0`   = spell **ID**
  - `133` = **SpellIconID** (e.g. Shadow Bolt uses 213)
  - `136..151` = **SpellName** per locale (16 columns). The frFR client reads
    **column 2**, i.e. field **138**. (enUS is column 0 = field 136.)
  - `153..168` = **SpellRank** per locale (frFR = field 155); set to 0 (empty).
- Custom rows are **appended at the end** (the existing rage-guard 900100 / 900101
  rows sit at the last indices), then `recordCount` is bumped and the string block
  extended with any new strings.

### Recommended way to add a row: clone a reference spell

Pick a vanilla spell whose visual/animation/range fit the ability (e.g. `686`
Shadow Bolt for a shadow projectile), copy its 936-byte record, then change:

- field `0` -> the custom ID (MOBA custom spells live in the `900xxx` range)
- field `138` (and `136`) -> offset of the new name appended to the string block
- field `155` -> 0 (no rank text)
- field `133` -> a different `SpellIconID` if desired

Everything else (cast time, range, visual, school) is inherited from the reference,
which keeps the client row consistent with the server `spell_dbc` definition.

## MPQ read/write: `storm.py`

`storm.py` is a ctypes wrapper over **StormLib** (`brew install stormlib`; auto-detected
or `STORMLIB_PATH`). It exposes:

- `storm.read_file(mpq_path, "DBFilesClient\\Spell.dbc")` -> bytes (use this to extract
  the current DBCs; `mpyq` mis-decompresses large files like Spell.dbc, StormLib does not).
- `storm.build_archive(out_path, {"DBFilesClient\\Spell.dbc": local_path, ...})` -> builds
  a v2 MPQ with a listfile (archived names use backslashes).

Verify a built patch with `file patch.MPQ` (`MoPaQ (MPQ) archive`).

## End-to-end recipe for a new custom spell

1. Choose an ID (`900xxx`) and keep it identical across all four places below.
2. **Server SQL** (`sql/custom/world/...`): add the `spell_dbc` row + `spell_script_names`
   (model on `0002_moba_spells.sql`). Re-import with `make db-custom`.
3. **C++**: add the `SpellScript` in `src/server/scripts/Custom/Moba`, register it.
4. **Client**: extract `Spell.dbc` + `SkillLineAbility.dbc` from the patch with
   `storm.read_file`, append the cloned/edited spell row (and a `SkillLineAbility`
   row so it is learnable), write the DBCs, repackage with `storm.build_archive`
   into `client-patches/frFR/patch-frFR-4.MPQ`, and install to the client.
5. Rebuild the core (`make image`) and recreate the worldserver.

Keep server SQL, C++ constants, and client `Spell.dbc` IDs aligned at all times.
