/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_MINION_H
#define GAME_MOBA_MINION_H

#include "Define.h"
#include "ObjectGuid.h"

#include <vector>

class Creature;
class Unit;
struct Position;

namespace Moba
{
inline constexpr float MinionAggroRange = 18.0f;
inline constexpr float MinionLeashRange = 22.0f;            // a locked target is kept until it leaves this range
inline constexpr float MinionChampionAggroAlertRange = 24.0f;
inline constexpr float MinionLaneLeashRange = 20.0f;        // drop the chase if the minion strays this far from its lane corridor
inline constexpr uint32 MinionForcedAggroDurationMs = 3000;
// LoL caster minions attack at 550 range while turrets attack at 750. With our turret range at 18 yd,
// the equivalent caster distance is 18 * 550 / 750 = 13.2 yd.
inline constexpr float MinionCasterAttackRange = 13.2f;

// Register the lane path the minion should walk, in its own travel order (blue: blue->red,
// red: red->blue). The minion follows it waypoint by waypoint and never backtracks.
void RegisterMinionLanePath(Creature* minion, std::vector<Position> const& path, uint32 spawnStreamId, uint32 formationIndex);
// Called when the minion reaches a lane waypoint (POINT_MOTION_TYPE) to advance to the next.
void OnMinionReachedWaypoint(Creature* minion, uint32 pointId);
// True if the minion has strayed too far from every lane waypoint (chased off-lane).
bool IsMinionOffLane(Creature const* minion);
void RegisterMinionLevel(Creature* minion, uint32 level);
uint32 GetRegisteredMinionLevel(Creature const* minion);
void ApplyMinionCombatTuning(Creature* minion, uint32 level);
void ClearMinionState(Creature* minion);
void NotifyChampionAggro(Unit* attacker, Unit* victim);
// Returns the target a minion should attack given its current victim. Implements
// League-style target locking: the current target is kept unless it becomes invalid
// or a strictly higher-priority target appears.
Unit* SelectMinionTarget(Creature* minion, Unit* currentVictim);
bool UpdateMinionLaneFlocking(Creature* minion);
void MoveMinionToCombatTarget(Creature* minion, Unit* target);
void ResumeMinionLaneMovement(Creature* minion);
}

#endif
