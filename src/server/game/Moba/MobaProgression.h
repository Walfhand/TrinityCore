/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_PROGRESSION_H
#define GAME_MOBA_PROGRESSION_H

#include "Define.h"
#include "MobaRules.h"

class Player;
class Creature;
class Unit;
class SpellInfo;

namespace Moba
{
// Controlled MOBA combat stats. In `Stats` these are the absolute target values for the current
// level (from the archetype curve); in `AppliedStats` they are the bonus we layered over the WoW
// base to reach those targets (tracked so we can re-bridge after a GiveLevel and undo at match end).
struct MobaStats
{
    uint32 Health = 0;
    uint32 Armor = 0;
    uint32 MagicResist = 0;
    uint32 AttackPower = 0;
    uint32 SpellPower = 0;
};

// Per-player state for a single MOBA match: team, archetype, the 1->18 progression and the
// derived stats. Owned by the progression registry (keyed by player GUID) for the match.
struct MobaPlayerState
{
    uint32 MatchId = 0;
    uint32 InstanceId = 0;
    uint32 TeamId = InvalidTeamId;
    uint32 ArchetypeIndex = 0;
    uint32 Level = MobaStartLevel;
    uint32 Xp = 0;
    uint32 Gold = MobaStartGold;
    MobaStats Stats;
    MobaStats AppliedStats;          // bonus layered for the WIPED stats (health/armor/MR); reset each GiveLevel
    uint32 AppliedAttackPower = 0;   // attack power/spell power/armor persist across GiveLevel (UNIT_MOD
    uint32 AppliedSpellPower = 0;    // modifiers survive stat recomputes), so they are tracked separately
    int32 AppliedArmorBonus = 0;     // signed: armor can be pushed below the natural WoW value
    uint32 MatchElapsedMs = 0;
    uint32 PassiveGoldTimerMs = 0;
    uint32 RespawnAtMs = 0;          // 0 = alive; otherwise the GameTimeMS at which to respawn
    uint32 KillStreak = 0;           // consecutive champion kills without dying (sizes the shutdown bounty)
    uint32 LastDamagerKey = 0;       // last enemy champion that damaged this player (GUID counter); 0 = none
    uint32 LastDamageMs = 0;         // GameTimeMS of that damage, for the kill-credit window
    bool ProgressInitialized = false;
};

// State registry --------------------------------------------------------------------------
MobaPlayerState* GetPlayerState(Player* player);
MobaPlayerState const* GetPlayerState(Player const* player);
MobaPlayerState& EnsurePlayerState(Player* player);     // create-or-get; used when a match is assigned
void RemovePlayerProgress(Player* player);              // drop applied stats + erase state (match cleanup)

// Per-player progression ------------------------------------------------------------------
void SetPlayerArchetype(Player* player, uint32 archetypeIndex);
void InitializePlayerMatchProgress(Player* player);     // reset to level 1 and push to client at match start
void ReapplyPlayerMatchState(Player* player);           // re-apply stats + re-sync client after a reconnect
void OnMinionKilled(Unit* killer, Creature* minion);    // last-hit gold + shared XP
void OnChampionKilled(Player* killer, Player* victim);  // PvP kill: bounty to killer + assists, streak/shutdown
void NoteChampionDamage(Unit* attacker, Unit* victim); // record the last enemy champion that hit a champion (kill credit)
uint32 GetPlayerMobaLevel(Player const* player);

// True for a dead champion in an active match: it must never auto-resurrect or teleport to a
// graveyard. It releases into a free-roaming spectator ghost; only the match timer respawns it.
bool BlocksGraveyardResurrect(Player const* player);

// True while a champion is in a match: native WoW XP must not apply (the MOBA drives the bar).
// Used by a core hook instead of PLAYER_FLAGS_NO_XP_GAIN, which would hide the client XP bar.
bool SuppressesNativeXp(Player const* player);

// True if a champion's harmful spell must NOT be cast on a structure (tower/nexus): structures are
// only damageable by auto-attacks. Ranged auto-attacks and beneficial spells are allowed. Core hook.
bool BlocksSpellOnStructure(Unit* caster, SpellInfo const* spellInfo, Unit* target);

// Periodic ticks (driven once per world update by a thin script hook).
void UpdatePassiveGold(uint32 diff);
void UpdateRespawns(uint32 diff);    // dead champions respawn at base after a level-scaled timer
}

#endif
