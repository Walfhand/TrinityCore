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
