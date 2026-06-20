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
  - `MobaQueue.{h,cpp}` — battlemaster-join seam: scripts register the custom matchmaking
    handler; the core join hook calls it (dependency inversion, no game->script dependency).
- Thin script hooks (script lib): `src/server/scripts/Custom/Moba/`
  - `moba_lobby.cpp`, `moba_match_mgr.{h,cpp}`, `moba_minion.cpp`, `moba_nexus.cpp`,
    `moba_solo_match.cpp`, `moba_spells.cpp`, `moba_shared.h`.
- SQL: `sql/custom/world/0001_moba_lobby.sql`, `0002_moba_spells.sql`.
- Guerilla BG SQL: `0003_guerilla_game_tele.sql`, `0004_guerilla_instance_template.sql`,
  `0005_guerilla_battleground_template.sql`.

The guiding rule: **core files only get thin hooks that call into the subsystem**; never
embed MOBA logic in an upstream file. When updating TrinityCore, re-apply the edits below.

---

## Core edits (re-apply these after a TrinityCore update)

### 1. `src/server/game/Battlegrounds/Zones/BattlegroundMoba.{cpp,h}` — MOBA match host
The MOBA mode has its own battleground class on map `900` (`guerilla`). The BG class owns a
`Moba::MatchController _moba` and acts as a thin adapter:
- `PostUpdateImpl` → `_moba.Update(diff)` (ticks nexus/wave logic).
- `StartingEventOpenDoors` → `StartMobaMatch()` which builds a `Moba::ArenaLayout` from
  Guerilla start/nexus positions and calls `_moba.Start(...)`.
- `HandleKillUnit` → `_moba.OnUnitKilled(...)` → `EndBattleground(winner)` on nexus death.
- `AddPlayer`/`RemovePlayer` set the team faction via `Moba::GetFactionForTeamId`.
- `CheckWinConditions` is owned by the MOBA controller, so vanilla population wins are disabled.

**Why:** the prototype no longer depends on `BATTLEGROUND_NA` / Nagrand Arena. Map `900`
is advertised through battleground type `BATTLEGROUND_MOBA = 12`.
**Next cleanup:** `StartMobaMatch` still has hardcoded positions; replace this with a
data-driven `MobaMapConfig` when adding more maps.

### 2. `src/server/shared/SharedDefines.h` — battleground id
Adds `BATTLEGROUND_MOBA = 12`.

**Why:** battleground type ids are used as compact array indexes in several BG systems.
Using the map id (`900`) as a BG type id would overrun those arrays.

### 3. `src/server/game/Battlegrounds/BattlegroundMgr.cpp` — BG factory
Registers `BattlegroundMoba` in both template and instance creation paths.

### 4. `src/server/game/DataStores/DBCStores.cpp` — `GetBattlegroundBracketByLevel`
Added a `minEntry` fallback: when the requested level is **below every bracket** on the
map, return the lowest bracket instead of `nullptr`.

**Why:** MOBA champions are level 1, below the arena bracket minimum (10). The core BG
port handler re-resolves the bracket from the player level on accept; without this clamp
it returns null and the "Enter" port silently fails.
**Scope:** generic (all maps), but "clamp to nearest bracket" is sane default behavior.

### 5. `src/server/game/Handlers/BattleGroundHandler.cpp` — two MOBA hooks
- `HandleBattlefieldLeaveOpcode`: the "no leave while in combat" guard also passes when
  `bg->GetTypeID() == BATTLEGROUND_MOBA` (champions are almost always in combat with minions,
  so the vanilla rule would make "Leave Arena" do nothing until the match ends).
- `HandleBattlemasterJoinOpcode`: when `bgTypeId == BATTLEGROUND_MOBA`, delegate to
  `Moba::HandleBattlemasterJoin(_player)` and return, bypassing the native queue. MOBA uses
  its own matchmaking (custom blue/red teams, dev-solo, 1v1 pop); the native queue assigns
  teams by faction and only pops when both sides reach `MinPlayersPerTeam`, so it never pops
  for a MOBA match. The handler is registered by scripts via the `MobaQueue` seam.

### 6. `src/server/game/Entities/Object/Object.cpp` — `WorldObject::IsValidAttackTarget`
Early-out guard: a player in a battleground cannot target a creature that is
`Moba::IsOwnNexus(playerTeam, creatureEntry)`.

**Why:** you must never be able to attack/target your own Nexus.
(Also includes `#include "MobaRules.h"`.)

### 7. `src/server/game/Entities/Unit/Unit.cpp` — `Unit::UpdateDisplayPower`
Added `FORM_BATTLESTANCE` / `FORM_DEFENSIVESTANCE` / `FORM_BERSERKERSTANCE` to the cases
that display `POWER_RAGE`.

**Why:** the Briseur archetype uses warrior stances and the rage resource; without this
the power bar can flip away from rage on stance change.

### 8. `src/server/game/Entities/Player/Player.cpp` — `Player::RepopAtGraveyard`
Early-out guard at the top: if `Moba::BlocksGraveyardResurrect(this)` (a dead champion in an
active MOBA match), call `SpawnCorpseBones()` (turns the corpse `BuildPlayerRepop` just created
into non-reclaimable bones), clear `m_deathTimer`, remove `PLAYER_FLAGS_IS_OUT_OF_BOUNDS` and
return — skipping the vanilla auto-resurrect / graveyard teleport entirely.
(Also includes `#include "MobaProgression.h"`.)

**Why:** on the custom MOBA map there is no graveyard and the position can sit below the map's
min-height, so vanilla `RepopAtGraveyard` auto-resurrects the player on release ("instant rez").
The guard lets a dead champion release into a free-roaming spectator ghost while reserving the
only respawn to the level-scaled match timer (`Moba::UpdateRespawns`, respawns at the team base).

### 9. `src/server/game/Handlers/MiscHandler.cpp` — `WorldSession::HandleReclaimCorpse`
Early-out guard after the `IsAlive()` check: if `Moba::BlocksGraveyardResurrect(_player)`, return.
(Also includes `#include "MobaProgression.h"`.)

**Why:** MOBA maps are instanceable, so the corpse-reclaim delay is `0` and the released ghost sits
on its own corpse — letting the player reclaim it for an instant self-rez. This blocks that path so
respawn stays governed only by the match timer.

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

1. Re-apply edits 1–7 above (search for `Moba` / `MOBA` / `BATTLEGROUND_MOBA` in those files).
2. New files under `src/server/game/Moba/` and `src/server/scripts/Custom/Moba/` need no
   action — `CollectSourceFiles` re-globs them automatically.
3. Rebuild (`make image`) and re-import custom SQL if changed (`make db-custom`).
