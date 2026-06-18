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

enum Constants
{
    NpcTextDefault = 1,
    PrototypeLevel = 10,
    NpcBlueNexus = 900001,
    NpcRedNexus = 900002,
    ActionJoinSoloTest = 1100,
    MapGmIsland = 1,
    MapSoloTest = 36
};

inline constexpr uint32 BlueTeamId = ALLIANCE;
inline constexpr uint32 RedTeamId = HORDE;
inline constexpr uint32 InvalidTeamId = 0;

inline bool IsTeamId(uint32 teamId)
{
    return teamId == BlueTeamId || teamId == RedTeamId;
}

inline bool IsNexusEntry(uint32 entry)
{
    return entry == NpcBlueNexus || entry == NpcRedNexus;
}

inline uint32 GetTeamId(Team team)
{
    return team == Team::Blue ? BlueTeamId : RedTeamId;
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
}

#endif
