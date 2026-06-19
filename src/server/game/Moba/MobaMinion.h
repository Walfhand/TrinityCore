/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_MINION_H
#define GAME_MOBA_MINION_H

#include "Define.h"
#include "ObjectGuid.h"

class Creature;
class Unit;
struct Position;

namespace Moba
{
inline constexpr float MinionAggroRange = 18.0f;
inline constexpr float MinionChampionAggroAlertRange = 24.0f;
inline constexpr uint32 MinionForcedAggroDurationMs = 3000;

void RegisterMinionLaneDestination(Creature* minion, Position const& destination);
void ClearMinionState(Creature* minion);
void NotifyChampionAggro(Unit* attacker, Unit* victim);
Unit* SelectMinionTarget(Creature* minion);
void ResumeMinionLaneMovement(Creature* minion);
}

#endif
