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
inline constexpr float MinionLeashRange = 22.0f;            // a locked target is kept until it leaves this range
inline constexpr float MinionChampionAggroAlertRange = 24.0f;
inline constexpr uint32 MinionForcedAggroDurationMs = 3000;

void RegisterMinionLaneDestination(Creature* minion, Position const& destination);
void RegisterMinionLevel(Creature* minion, uint32 level);
uint32 GetRegisteredMinionLevel(Creature const* minion);
void ApplyMinionCombatTuning(Creature* minion, uint32 level);
void ClearMinionState(Creature* minion);
void NotifyChampionAggro(Unit* attacker, Unit* victim);
// Returns the target a minion should attack given its current victim. Implements
// League-style target locking: the current target is kept unless it becomes invalid
// or a strictly higher-priority target appears.
Unit* SelectMinionTarget(Creature* minion, Unit* currentVictim);
void ResumeMinionLaneMovement(Creature* minion);
}

#endif
