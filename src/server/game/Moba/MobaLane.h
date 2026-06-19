/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_LANE_H
#define GAME_MOBA_LANE_H

#include "Define.h"
#include "Duration.h"
#include "Position.h"

class Map;

namespace Moba
{
struct LaneConfig
{
    char const* Name = "";
    Position BlueSpawn;
    Position RedSpawn;
    Position BlueDestination;
    Position RedDestination;
    // League's first wave leaves the base at 0:30.
    Seconds FirstWaveDelay = 30s;
};

// Composition of a single minion wave, mirroring League of Legends.
struct MinionWavePlan
{
    uint32 MeleeCount = 3;
    uint32 CasterCount = 3;
    uint32 SiegeCount = 0;
    uint32 UpgradeLevel = 0;
};

LaneConfig BuildSingleLaneConfig(char const* name, Position const& blueSpawn, Position const& redSpawn);

// Plans a wave for the given 1-based wave number and elapsed match time.
MinionWavePlan PlanMinionWave(uint32 waveNumber, uint32 elapsedMs);

// Time before the next wave for the given elapsed match time (30s -> 25s -> 20s).
Milliseconds GetWaveInterval(uint32 elapsedMs);

void SpawnMinionWave(Map* map, LaneConfig const& lane, uint32 instanceId, MinionWavePlan const& plan);
}

#endif
