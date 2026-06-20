/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_RULES_H
#define GAME_MOBA_RULES_H

#include "Define.h"
#include "SharedDefines.h"

namespace Moba
{
enum class Team
{
    Blue,
    Red
};

enum class MatchState
{
    None,
    Queued,
    Invited,
    Preparing,
    InProgress,
    Finished
};

enum class MinionType
{
    Melee,
    Caster,
    Siege
};

enum Constants
{
    NpcTextDefault = 1,
    PrototypeLevel = 10,
    NpcBlueNexus = 900001,
    NpcRedNexus = 900002,
    NpcBlueMinion = 900003,
    NpcRedMinion = 900004,
    NpcBlueMinionCaster = 900005,
    NpcRedMinionCaster = 900006,
    NpcBlueMinionSiege = 900007,
    NpcRedMinionSiege = 900008,
    NpcBlueTower = 900010,
    NpcRedTower = 900011,
    ActionJoinMatch = 1100,
    ActionJoinDevSolo = 1101,
    MapGmIsland = 1,
    MapSoloTest = 36,
    MaxTeamSize = 20,
    MobaStartLevel = 1,
    MobaMaxLevel = 18,
    MobaStartGold = 500,
    MobaMeleeMinionGold = 20,
    MobaCasterMinionGold = 17,
    MobaSiegeMinionGold = 45,
    MobaMeleeMinionXp = 35,
    MobaCasterMinionXp = 28,
    MobaSiegeMinionXp = 90
};

inline constexpr uint32 BlueTeamId = ALLIANCE;
inline constexpr uint32 RedTeamId = HORDE;
inline constexpr uint32 InvalidTeamId = 0;

inline constexpr uint32 MobaCopperPerGold = 10000;   // money widget shows 1 MOBA gold as 1 gold piece
inline constexpr float MobaXpShareRange = 45.0f;     // allies within this range of a dying minion share its XP

// Passive gold income, modeled on League of Legends (which grants 20.4 gold / 10s from 1:50).
inline constexpr uint32 MobaPassiveGoldStartMs = 110000;    // first trickle at 1:50 into the match
inline constexpr uint32 MobaPassiveGoldIntervalMs = 10000;  // granted every 10 seconds
inline constexpr uint32 MobaPassiveGoldAmount = 20;         // gold per interval (LoL is 20.4)

inline constexpr uint8 MobaNexusLevel = 1;            // low level so any champion lands hits reliably
inline constexpr uint32 MobaNexusHealth = 10000;      // a lot of HP so it is a real objective, not instant

// Towers, modeled on League of Legends turrets.
inline constexpr uint8 MobaTowerLevel = 1;
inline constexpr uint32 MobaTowerHealth = 4000;            // tanky structure
inline constexpr float MobaTowerRange = 30.0f;             // attack range (LoL ~750 units)
inline constexpr uint32 MobaTowerAttackIntervalMs = 1000;
inline constexpr uint32 MobaTowerDamageVsMinion = 350;     // shreds minions
inline constexpr uint32 MobaTowerDamageVsChampion = 120;   // base damage; ramps on consecutive shots
inline constexpr float MobaTowerRampPerShot = 0.40f;       // +40% per consecutive shot on a champion
inline constexpr float MobaTowerRampMax = 1.20f;           // capped at +120% (220% total)
inline constexpr uint32 MobaTowerRampResetMs = 5000;       // ramp resets 5s after the last champion hit
inline constexpr uint32 MobaTowerShotSpell = 5176;         // visual bolt for the tower shot (placeholder)

inline bool IsTeamId(uint32 teamId)
{
    return teamId == BlueTeamId || teamId == RedTeamId;
}

inline bool IsNexusEntry(uint32 entry)
{
    return entry == NpcBlueNexus || entry == NpcRedNexus;
}

inline bool IsTowerEntry(uint32 entry)
{
    return entry == NpcBlueTower || entry == NpcRedTower;
}

inline uint32 GetTeamIdForTowerEntry(uint32 entry)
{
    if (entry == NpcBlueTower)
        return BlueTeamId;
    if (entry == NpcRedTower)
        return RedTeamId;
    return InvalidTeamId;
}

inline uint32 GetTowerEntry(uint32 teamId)
{
    if (teamId == BlueTeamId)
        return NpcBlueTower;
    if (teamId == RedTeamId)
        return NpcRedTower;
    return 0;
}

inline bool IsMinionEntry(uint32 entry)
{
    switch (entry)
    {
        case NpcBlueMinion:
        case NpcRedMinion:
        case NpcBlueMinionCaster:
        case NpcRedMinionCaster:
        case NpcBlueMinionSiege:
        case NpcRedMinionSiege:
            return true;
        default:
            return false;
    }
}

inline MinionType GetMinionType(uint32 entry)
{
    switch (entry)
    {
        case NpcBlueMinionCaster:
        case NpcRedMinionCaster:
            return MinionType::Caster;
        case NpcBlueMinionSiege:
        case NpcRedMinionSiege:
            return MinionType::Siege;
        default:
            return MinionType::Melee;
    }
}

inline uint32 GetTeamId(Team team)
{
    return team == Team::Blue ? BlueTeamId : RedTeamId;
}

inline uint32 GetEnemyTeamId(uint32 teamId)
{
    if (teamId == BlueTeamId)
        return RedTeamId;

    if (teamId == RedTeamId)
        return BlueTeamId;

    return InvalidTeamId;
}

inline uint32 GetFactionForTeamId(uint32 teamId)
{
    return teamId == BlueTeamId ? FACTION_ALLIANCE_GENERIC : FACTION_HORDE_GENERIC;
}

inline uint32 GetNexusEntryForTeamId(uint32 teamId)
{
    if (teamId == BlueTeamId)
        return NpcBlueNexus;

    if (teamId == RedTeamId)
        return NpcRedNexus;

    return 0;
}

inline uint32 GetTeamIdForNexusEntry(uint32 nexusEntry)
{
    if (nexusEntry == NpcBlueNexus)
        return BlueTeamId;

    if (nexusEntry == NpcRedNexus)
        return RedTeamId;

    return InvalidTeamId;
}

inline uint32 GetMinionEntry(uint32 teamId, MinionType type)
{
    if (!IsTeamId(teamId))
        return 0;

    bool const blue = teamId == BlueTeamId;
    switch (type)
    {
        case MinionType::Caster:
            return blue ? NpcBlueMinionCaster : NpcRedMinionCaster;
        case MinionType::Siege:
            return blue ? NpcBlueMinionSiege : NpcRedMinionSiege;
        case MinionType::Melee:
        default:
            return blue ? NpcBlueMinion : NpcRedMinion;
    }
}

inline uint32 GetTeamIdForMinionEntry(uint32 minionEntry)
{
    switch (minionEntry)
    {
        case NpcBlueMinion:
        case NpcBlueMinionCaster:
        case NpcBlueMinionSiege:
            return BlueTeamId;
        case NpcRedMinion:
        case NpcRedMinionCaster:
        case NpcRedMinionSiege:
            return RedTeamId;
        default:
            return InvalidTeamId;
    }
}

inline uint32 GetWinnerTeamIdForDestroyedNexus(uint32 nexusEntry)
{
    if (nexusEntry == NpcBlueNexus)
        return RedTeamId;

    if (nexusEntry == NpcRedNexus)
        return BlueTeamId;

    return InvalidTeamId;
}

inline bool IsOwnNexus(uint32 playerTeamId, uint32 nexusEntry)
{
    return playerTeamId == GetTeamIdForNexusEntry(nexusEntry);
}

inline char const* GetMatchStateName(MatchState state)
{
    switch (state)
    {
        case MatchState::Queued:
            return "queued";
        case MatchState::Invited:
            return "invited";
        case MatchState::Preparing:
            return "preparing";
        case MatchState::InProgress:
            return "in_progress";
        case MatchState::Finished:
            return "finished";
        case MatchState::None:
        default:
            return "none";
    }
}
}

#endif
