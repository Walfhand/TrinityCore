/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_QUEUE_H
#define GAME_MOBA_QUEUE_H

#include "SharedDefines.h"

class Player;

namespace Moba
{
// Dependency-inversion seam between the core battlemaster-join hook (game lib) and the MOBA
// matchmaking implementation (scripts). Scripts register a handler at load; the core hook
// calls it for MOBA battlemaster-list IDs so the engine never falls into the native BG queue
// (which assigns teams by faction and only pops when both teams reach MinPlayersPerTeam).
using BattlemasterJoinHandler = void (*)(Player* player, uint32 battlemasterListId);

void SetBattlemasterJoinHandler(BattlemasterJoinHandler handler);
bool HandleBattlemasterJoin(Player* player, uint32 battlemasterListId);   // true if a MOBA handler consumed the join

bool IsMobaBattlemasterListId(uint32 battlemasterListId);
uint32 GetMobaBattlemasterListIdForArchetype(uint32 archetypeIndex);
uint32 GetArchetypeIndexForBattlemasterListId(uint32 battlemasterListId);
BattlegroundTypeId GetCanonicalBattlegroundTypeId(BattlegroundTypeId bgTypeId);
}

#endif
