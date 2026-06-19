/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef CUSTOM_MOBA_SHARED_H
#define CUSTOM_MOBA_SHARED_H

#include "Define.h"
#include "MobaArchetypes.h"    // archetype data + apply/spells/skills/reset (game-lib subsystem)
#include "MobaProgression.h"   // per-player match state + progression (game-lib subsystem)

class Player;

namespace Moba
{
// Lobby / matchmaking entry points (implemented in moba_solo_match.cpp).
void QueueMobaMatch(Player* player);
void QueueDevSoloTest(Player* player);
bool IsDevSoloModeEnabled();
uint32 GetConfiguredTeamSize();
bool CompleteSoloNexusObjective(Player* player);
}

#endif
