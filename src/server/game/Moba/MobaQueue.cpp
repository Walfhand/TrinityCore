/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaQueue.h"

#include "MobaArchetypes.h"
#include "MobaRules.h"

namespace Moba
{
namespace
{
BattlemasterJoinHandler g_battlemasterJoinHandler = nullptr;
}

void SetBattlemasterJoinHandler(BattlemasterJoinHandler handler)
{
    g_battlemasterJoinHandler = handler;
}

bool HandleBattlemasterJoin(Player* player, uint32 battlemasterListId)
{
    if (!g_battlemasterJoinHandler || !player || !IsMobaBattlemasterListId(battlemasterListId))
        return false;

    g_battlemasterJoinHandler(player, battlemasterListId);
    return true;
}

bool IsMobaBattlemasterListId(uint32 battlemasterListId)
{
    return battlemasterListId >= BATTLEGROUND_MOBA && battlemasterListId < BATTLEGROUND_MOBA + ArchetypeCount;
}

uint32 GetMobaBattlemasterListIdForArchetype(uint32 archetypeIndex)
{
    if (archetypeIndex >= ArchetypeCount)
        archetypeIndex = 0;

    return BATTLEGROUND_MOBA + archetypeIndex;
}

uint32 GetArchetypeIndexForBattlemasterListId(uint32 battlemasterListId)
{
    if (!IsMobaBattlemasterListId(battlemasterListId))
        return 0;

    return battlemasterListId - BATTLEGROUND_MOBA;
}

BattlegroundTypeId GetCanonicalBattlegroundTypeId(BattlegroundTypeId bgTypeId)
{
    return IsMobaBattlemasterListId(bgTypeId) ? BATTLEGROUND_MOBA : bgTypeId;
}
}
