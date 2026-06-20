/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_TOWER_H
#define GAME_MOBA_TOWER_H

#include "Define.h"

class Creature;
class Map;
class Unit;
struct Position;

namespace Moba
{
// Spawn + tune a tower for a team at a position (level 1, large HP pool, passive react state).
void SpawnTower(Map* map, uint32 teamId, Position const& pos);
void ApplyTowerTuning(Creature* tower);

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
