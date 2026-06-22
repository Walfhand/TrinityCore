/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_LANE_H
#define GAME_MOBA_LANE_H

#include "Define.h"
#include "Duration.h"
#include "MobaRules.h"
#include "Position.h"

#include <vector>

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
    std::vector<Position> Waypoints;     // lane path in Blue->Red order; Red walks it reversed
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

struct MinionSpawn
{
    uint32 SpawnStreamId = 0;
    uint32 TeamId = InvalidTeamId;
    MinionType Type = MinionType::Melee;
    Position SpawnPosition;
    std::vector<Position> Path;
    uint32 UpgradeLevel = 0;
    Milliseconds Delay = 0ms;
};

LaneConfig BuildSingleLaneConfig(char const* name, Position const& blueSpawn, Position const& redSpawn);

// Plans a wave for the given 1-based wave number and elapsed match time.
MinionWavePlan PlanMinionWave(uint32 waveNumber, uint32 elapsedMs);

// Time before the next wave for the given elapsed match time (30s -> 25s -> 20s).
Milliseconds GetWaveInterval(uint32 elapsedMs);

std::vector<MinionSpawn> BuildMinionWaveSpawns(LaneConfig const& lane, MinionWavePlan const& plan, uint32 laneIndex);
void SpawnMinion(Map* map, uint32 instanceId, MinionSpawn const& spawn);
}

#endif
