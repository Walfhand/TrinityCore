# AGENTS.md

## Project Goal

This fork turns TrinityCore 3.3.5a into a MOBA-style game mode.

The target experience is not classic WoW progression. Players should enter a lobby, pick a heroic archetype, queue into a match, play on a MOBA map with teams, minions, gold, XP, items, and win by destroying the enemy nexus.

## Current Direction

Keep the gameplay work modular and map-agnostic. Match rules, teams, minions, nexus logic, gold, XP, and archetype systems should not be hardcoded to one specific map unless absolutely necessary for a short-term test.

Prefer this shape:

- Shared gameplay state in dedicated MOBA systems.
- Map/position data isolated in configuration-like structures.
- NPC scripts kept thin when possible.
- Match lifecycle logic separated from lobby/gossip UI.
- Custom spells tracked in both server SQL/C++ and client patch assets when the client must display them.

## Short-Term Gameplay Plan

Build the actual match loop before adding many classes.

1. Stable match state
   - Blue and Red teams.
   - One nexus per team.
   - Clean match start/end.
   - Clean player leave/decline/queue cleanup.
   - Solo queue remains possible for testing.

2. Minion economy
   - Regular minion waves.
   - Gold on minion kill.
   - Later: distinguish last-hit gold from assist/nearby participation.
   - Minion AI should move toward objectives, fight enemy minions first, and switch intelligently when a player attacks or is exposed.

3. Player progression inside a match
   - Match level starts at 1.
   - Add XP from minions, players, and objectives.
   - Add level-up stat growth.
   - Add spell scaling by level/stats.
   - Later: allow skill point choices per level.

4. Shop
   - Vendor usable at base.
   - Start with a small item set.
   - Items should support core MOBA stats: health, armor, magic resistance, attack damage, spell power, movement speed, cooldown reduction, sustain.

5. Archetype depth
   - Each archetype should have a clear role and mechanic.
   - Start with one polished archetype before expanding all classes.
   - Keep the kit limited: roughly 4 active spells plus 1 to 3 passives.

## Archetype Code Layout

Each archetype with a unique mechanic gets its OWN module (constants, custom spell IDs, the mechanic
logic, and its per-player runtime state). Do NOT put archetype-specific constants/logic in the generic
files (`MobaRules.h`, `MobaProgression`). Reference example: `src/server/game/Moba/MobaSorcier.{h,cpp}`
(the "Entropy" mage and its Instability gauge, in namespace `Moba::Sorcier`). The generic systems stay
generic: `MobaRules.h` = shared constants/helpers, `MobaProgression` = common level/xp/gold/stat layer.
Shared per-archetype stat curves (health/AD/AP/armor/MR/attack-speed base+growth) live as data in
`MobaArchetypes` (`ArchetypeStatCurve`), computed on the LoL increasing-growth curve.

Spell SCRIPTS are also split per archetype: `src/server/scripts/Custom/Moba/moba_<archetype>_spells.cpp`
(e.g. `moba_sorcier_spells.cpp`, `moba_briseur_spells.cpp`), each with its own `AddSC_moba_<archetype>_spells()`
registered in `custom_script_loader.cpp`. Do not put one archetype's spells in another's (or in a shared file).

## Archetype Design Rules

Each archetype should define:

- Role: bruiser, assassin, mage, marksman, support, tank, etc.
- Resource: rage, energy, mana, runic power, custom logic if needed.
- Core loop: what the player is trying to do every fight.
- Strengths and weaknesses.
- 4 active spells max for the first playable version.
- 1 to 3 passives.
- Scaling rules for each spell.

Example Briseur direction:

- Role: melee bruiser.
- Resource: rage.
- Identity: gets stronger by staying in combat.
- Defensive tool: `Garde rageuse`.
- Engage tool: charge/leap.
- Area pressure: shout, cleave, stomp, or shockwave-like spell.
- Finisher: consumes rage for burst or execute pressure.

Possible passive:

```text
Fureur tenace
When the Briseur takes damage from an enemy hero, gain a stack of Fureur for a few seconds.
Each stack increases damage and rage generation.
At max stacks, the next ability applies a slow or bonus effect.
```

## Spell Strategy

Use three spell sources:

- Existing player spells for fast prototyping.
- Existing creature/boss spells for visuals and unusual mechanics.
- Fully custom spells when the MOBA mechanic needs exact behavior.

Do not over-invest in custom DBC work until the gameplay idea is proven. Prototype with existing spells first, then replace with custom spells once the kit feels worth keeping.

When adding a fully custom spell:

- Add server SQL in `sql/custom`.
- Add C++ spell logic in `src/server/scripts/Custom/Moba` when needed.
- Register scripts in the custom script loader.
- Add/update client MPQ patches if the spell must appear correctly in the spellbook/action bars/tooltips.
- Keep generated client MPQs under `client-patches/` so they can be tracked.
- Keep the server spell IDs, SQL rows, C++ constants, and client `Spell.dbc`
  rows aligned. For the current process, see `client-patches/README.md`.
- Reusable tooling + the full documented pipeline (Spell.dbc field map, StormLib
  MPQ read/write via `storm.py`, the clone-a-reference-row recipe) lives in
  `tools/client-patch/`. Use it instead of re-deriving the DBC/MPQ format.

## Stats And Scaling

Long term, use a MOBA stats layer instead of relying only on vanilla WoW stats.

Important MOBA stats:

- Max health.
- Health regeneration.
- Attack damage.
- Spell power.
- Armor.
- Magic resistance.
- Movement speed.
- Cooldown reduction.
- Armor/magic penetration.
- Crit, if it becomes useful.

Spell formulas should be explicit and balanceable. Prefer formulas like:

```text
Damage = base + level scaling + attack damage coefficient
Shield = base + level scaling + max health coefficient
```

Example:

```text
Garde rageuse
Costs 30 rage.
Absorbs 80 + 15 per level + 20% max health for 6 seconds.
```

## Economy

Gold should matter early.

Initial rules:

- Passive trickle gold.
- Gold on minion kill.
- Gold on player kill.
- Gold on assist.
- Objective gold later.

Avoid building a huge item shop immediately. Start with 10 to 15 items that make basic builds possible.

## Leveling

Initial rules:

- Players start each match at level 1.
- Max level can be 10 or 18.
- XP comes from minions, players, and objectives.
- Leveling grants stats automatically at first.
- Later, levels should unlock or upgrade spells.

The first implementation can auto-upgrade spells. Add player choices only after the base loop is stable.

## Client Patch Policy

The full WoW client under `docker/client` is ignored and should stay ignored.

Generated client patches that must be preserved belong in:

```text
client-patches/
```

For the current frFR client, install patches into:

```text
docker/client/WINDOWS_World_of_Warcraft_335a/WINDOWS_World of Warcraft 335a/Data/frFR/
```

After changing DBC client patches, close the client and delete the client `Cache` folder before retesting.

## Guerilla Map MPQ Packaging

Source terrain files live outside the repo:

```text
/home/walfhand/Documents/wow-maps/guerilla/
```

The custom map ID is `900`. The expected client archive paths are:

```text
DBFilesClient\Map.dbc
DBFilesClient\AreaTable.dbc
DBFilesClient\BattlemasterList.dbc
DBFilesClient\WorldSafeLocs.dbc
DBFilesClient\PvpDifficulty.dbc
World\Maps\guerilla\guerilla.wdt
World\Maps\guerilla\guerilla.wdl
World\Maps\guerilla\guerilla_27_25.adt
World\Maps\guerilla\guerilla_27_26.adt
World\Maps\guerilla\guerilla_28_25.adt
World\Maps\guerilla\guerilla_28_26.adt
```

Package them into an MPQ with StormLib as `patch-guerilla.MPQ`, then install it into the Docker client as:

```text
docker/client/WINDOWS_World_of_Warcraft_335a/WINDOWS_World of Warcraft 335a/Data/patch-4.MPQ
```

Keep a generated copy at:

```text
/home/walfhand/Documents/wow-maps/patch-guerilla.MPQ
```

The server-side `Map.dbc` must contain map ID `900` with directory `guerilla`, `InstanceType = 3` (`MAP_BATTLEGROUND`), and `AreaTableID = 9000`.

Guerilla is exposed as battleground type `12` (`BATTLEGROUND_MOBA`) rather than using map id `900` as a BG type id. Keep these files aligned:

```text
docker/data/dbc/BattlemasterList.dbc  -> row ID 12, map 900, name Guerilla
docker/data/dbc/WorldSafeLocs.dbc     -> 900901 Blue start, 900902 Red start
docker/data/dbc/PvpDifficulty.dbc     -> map 900 brackets, RangeIndex 0-15 only
sql/custom/world/0005_guerilla_battleground_template.sql
```

The cleanup SQL for the old dungeon-style template is tracked in:

```text
sql/custom/world/0004_guerilla_instance_template.sql
```

It removes stale dungeon instance-template data:

```text
DELETE FROM instance_template WHERE map = 900
```

The generated server map files must be installed in `docker/data/maps/`:

```text
9002527.map
9002528.map
9002627.map
9002628.map
```

`AreaTable.dbc` must also include the parent zone `9000` (`Guerilla`) and the current per-tile subzones:

```text
9001 Guerilla Nord-Ouest -> guerilla_27_25.adt -> 9002527.map
9002 Guerilla Nord-Est  -> guerilla_27_26.adt -> 9002528.map
9003 Guerilla Sud-Ouest -> guerilla_28_25.adt -> 9002627.map
9004 Guerilla Sud-Est   -> guerilla_28_26.adt -> 9002628.map
```

Each ADT stores these zone IDs in every MCNK `areaid`; after changing them, rebuild `patch-guerilla.MPQ`, clear the client `Cache`, regenerate `900*.map` with `mapextractor`, and restart `worldserver`.

Before replacing the client patch, verify:

- The MPQ is reported by `file` as `MoPaQ (MPQ) archive`.
- Listing the MPQ shows the `DBFilesClient\*.dbc` files above, the six map files above, plus optional internal files such as `(listfile)`.
- The archived paths use backslashes and start with `World\Maps\guerilla\`.
- The installed client copy matches the generated MPQ byte-for-byte, for example with `cmp -s`.

The current generated patch was installed on June 19, 2026 as `Data/patch-4.MPQ`. It contains the DBC files listed above, `guerilla.wdt`, `guerilla.wdl`, and the four ADT tiles listed above. After restarting `worldserver`, teleport to the map in game with:

```text
.go xyz 3200 2133.33 100 900
```

The world DB shortcut is tracked in:

```text
sql/custom/world/0003_guerilla_game_tele.sql
```

After applying that SQL and reloading `game_tele` or restarting `worldserver`, use:

```text
.tele guerilla
```

## Recommended Next Implementation Step

Create a `MobaPlayerState` or equivalent match-player state system.

It should track at least:

- Player GUID.
- Team.
- Current archetype.
- Match level.
- XP.
- Gold.
- Derived MOBA stats.
- Runtime flags such as queued, invited, in match, dead/respawning.

Then connect minion kills to gold/XP rewards. This unlocks balancing for spells, levels, items, and the shop.
