/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_LANE_H
#define GAME_MOBA_LANE_H

#include "Define.h"
#include "Duration.h"
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
    std::vector<float> SpawnOffsets;
    Seconds FirstWaveDelay = 5s;
    Seconds WaveInterval = 30s;
};

LaneConfig BuildSingleLaneConfig(char const* name, Position const& blueSpawn, Position const& redSpawn);
void SpawnMinionWave(Map* map, LaneConfig const& lane, uint32 instanceId);
}

#endif
