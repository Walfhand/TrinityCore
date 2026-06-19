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

namespace Moba
{
// MOBA combat stats layered on top of the (hidden) base WoW class. Bonuses are tracked so
// they can be re-applied after a GiveLevel rebuilds the base, and removed when the match ends.
struct MobaStats
{
    uint32 BonusHealth = 0;
    uint32 AttackDamage = 0;
    uint32 SpellPower = 0;
    uint32 Armor = 0;
    uint32 MagicResist = 0;
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
    MobaStats AppliedStats;
    uint32 MatchElapsedMs = 0;
    uint32 PassiveGoldTimerMs = 0;
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
void OnMinionKilled(Unit* killer, Creature* minion);    // last-hit gold + shared XP
uint32 GetPlayerMobaLevel(Player const* player);

// Periodic tick (driven once per world update by a thin script hook).
void UpdatePassiveGold(uint32 diff);
}

#endif
