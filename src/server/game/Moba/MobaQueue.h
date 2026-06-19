/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_QUEUE_H
#define GAME_MOBA_QUEUE_H

class Player;

namespace Moba
{
// Dependency-inversion seam between the core battlemaster-join hook (game lib) and the MOBA
// matchmaking implementation (scripts). Scripts register a handler at load; the core hook
// calls it for BATTLEGROUND_MOBA so the engine never falls into the native BG queue for MOBA
// (which assigns teams by faction and only pops when both teams reach MinPlayersPerTeam).
using BattlemasterJoinHandler = void (*)(Player* player);

void SetBattlemasterJoinHandler(BattlemasterJoinHandler handler);
bool HandleBattlemasterJoin(Player* player);   // true if a MOBA handler consumed the join
}

#endif
