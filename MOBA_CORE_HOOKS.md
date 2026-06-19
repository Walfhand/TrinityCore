# MOBA Core Hooks

This file is the single source of truth for **every change the MOBA mode makes to
existing upstream TrinityCore files**. Those are the only edits that can conflict when
rebasing/updating against upstream TrinityCore.

Everything else lives in **new files** that never conflict on rebase:

- Gameplay subsystem (game lib): `src/server/game/Moba/`
  - `MobaRules.h` — teams, entries, tuning constants, pure helpers (no state).
  - `MobaArchetypes.{h,cpp}` — archetype data + apply/spell/skill logic.
  - `MobaProgression.{h,cpp}` — per-player match state, XP/gold/stats, passive gold, minion rewards.
  - `MobaGame.{h,cpp}` — per-instance `MatchController` (nexus spawn, wave scheduling).
  - `MobaLane.{h,cpp}` — lane geometry + wave planning.
  - `MobaMinion.{h,cpp}` — minion AI helpers (targeting, lane movement, tuning).
- Thin script hooks (script lib): `src/server/scripts/Custom/Moba/`
  - `moba_lobby.cpp`, `moba_match_mgr.{h,cpp}`, `moba_minion.cpp`, `moba_nexus.cpp`,
    `moba_solo_match.cpp`, `moba_spells.cpp`, `moba_shared.h`.
- SQL: `sql/custom/world/0001_moba_lobby.sql`, `0002_moba_spells.sql`.

The guiding rule: **core files only get thin hooks that call into the subsystem**; never
embed MOBA logic in an upstream file. When updating TrinityCore, re-apply the edits below.

---

## Core edits (re-apply these after a TrinityCore update)

### 1. `src/server/game/Battlegrounds/Zones/BattlegroundNA.{cpp,h}` — MOBA match host
The Nagrand arena is reused as the match backend. The BG class owns a
`Moba::MatchController _moba` and acts as a thin adapter:
- `PostUpdateImpl` → `_moba.Update(diff)` (ticks nexus/wave logic).
- `StartingEventOpenDoors` → `StartMobaMatch()` which translates the arena team start
  positions into a `Moba::ArenaLayout` and calls `_moba.Start(...)`.
- `HandleKillUnit` → `_moba.OnUnitKilled(...)` → `EndBattleground(winner)` on nexus death.
- `AddPlayer`/`RemovePlayer` set the team faction via `Moba::GetFactionForTeamId`.
- `CheckWinConditions` is neutered (arena would otherwise auto-win on an empty enemy team).

**Why:** the prototype runs matches on `BATTLEGROUND_NA`.
**Phase D note:** `StartMobaMatch`'s position translation is the only map-specific glue
left; it will be replaced by a data-driven `MobaMapConfig`.

### 2. `src/server/game/DataStores/DBCStores.cpp` — `GetBattlegroundBracketByLevel`
Added a `minEntry` fallback: when the requested level is **below every bracket** on the
map, return the lowest bracket instead of `nullptr`.

**Why:** MOBA champions are level 1, below the arena bracket minimum (10). The core BG
port handler re-resolves the bracket from the player level on accept; without this clamp
it returns null and the "Enter" port silently fails.
**Scope:** generic (all maps), but "clamp to nearest bracket" is sane default behavior.

### 3. `src/server/game/Handlers/BattleGroundHandler.cpp` — `HandleBattlefieldLeaveOpcode`
The "no leave while in combat" guard now also passes when `bg->GetTypeID() == BATTLEGROUND_NA`.

**Why:** MOBA champions are almost always in combat with minions, so the vanilla rule
would make the "Leave Arena" button do nothing until the match ends.

### 4. `src/server/game/Entities/Object/Object.cpp` — `WorldObject::IsValidAttackTarget`
Early-out guard: a player in a battleground cannot target a creature that is
`Moba::IsOwnNexus(playerTeam, creatureEntry)`.

**Why:** you must never be able to attack/target your own Nexus.
(Also includes `#include "MobaRules.h"`.)

### 5. `src/server/game/Entities/Unit/Unit.cpp` — `Unit::UpdateDisplayPower`
Added `FORM_BATTLESTANCE` / `FORM_DEFENSIVESTANCE` / `FORM_BERSERKERSTANCE` to the cases
that display `POWER_RAGE`.

**Why:** the Briseur archetype uses warrior stances and the rage resource; without this
the power bar can flip away from rage on stance change.

---

## Config (additive, low conflict risk)

### `src/server/worldserver/worldserver.conf.dist`
Adds the `Moba.*` block (e.g. `Moba.TeamSize`, `Moba.DevSoloMode`). Additive only.

## Script registration (designated extension point, not really a "core edit")

### `src/server/scripts/Custom/custom_script_loader.cpp`
`AddCustomScripts()` calls the `AddSC_moba_*()` loaders. This is the standard TrinityCore
custom-script hook; new MOBA script files are registered here.

---

## Maintenance checklist after a TrinityCore update

1. Re-apply edits 1–5 above (search for `Moba` / `MOBA` / `BATTLEGROUND_NA` in those files).
2. New files under `src/server/game/Moba/` and `src/server/scripts/Custom/Moba/` need no
   action — `CollectSourceFiles` re-globs them automatically.
3. Rebuild (`make image`) and re-import custom SQL if changed (`make db-custom`).
