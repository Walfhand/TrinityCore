/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaQueue.h"

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

bool HandleBattlemasterJoin(Player* player)
{
    if (!g_battlemasterJoinHandler || !player)
        return false;

    g_battlemasterJoinHandler(player);
    return true;
}
}
