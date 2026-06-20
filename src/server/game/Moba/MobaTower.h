/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_TOWER_H
#define GAME_MOBA_TOWER_H

#include "Define.h"

class Creature;
class Map;
class Unit;
class WorldObject;
struct Position;

namespace Moba
{
// Spawn + tune a tower for a team at a position (level 1, large HP pool, passive react state).
// lane/ord drive the destruction order (gating): ord 0 = outer (destroyed first).
void SpawnTower(Map* map, uint32 teamId, uint32 lane, uint32 ord, Position const& pos);
void ApplyTowerTuning(Creature* tower);

// LoL gating: a lane tower is vulnerable once every more-outer tower on its lane is destroyed;
// nexus towers (lane 9) once a lane is fully cleared; the nexus once its 2 nexus towers fall.
bool IsTowerVulnerable(Creature const* tower);
bool IsNexusVulnerable(uint32 instanceId, uint32 teamId);
// True if an attack on a MOBA structure must be denied (own structure, or not yet vulnerable).
bool IsStructureAttackBlocked(WorldObject const* attacker, WorldObject const* target);

// Called when a tower dies: announce + drop its state (opens the next tower / the nexus).
void OnTowerDestroyed(Creature* tower);

// League-style target selection: sticky current target, champion-dive override, otherwise
// minions (siege > melee > caster) before champions, nearest first, all within tower range.
Unit* SelectTowerTarget(Creature* tower, Unit* currentVictim);

// Fire one shot at the target, applying the consecutive-shot ramp vs champions.
void TowerShoot(Creature* tower, Unit* target);

// Champion-dive aggro: an enemy champion damaging an allied champion near a tower is targeted.
void NotifyTowerAggro(Unit* attacker, Unit* victim);

void ClearTowerState(Creature* tower);
}

#endif
