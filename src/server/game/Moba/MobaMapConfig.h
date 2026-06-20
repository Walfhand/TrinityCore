/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_MAP_CONFIG_H
#define GAME_MOBA_MAP_CONFIG_H

#include "Define.h"
#include "Position.h"

#include <vector>

namespace Moba
{
// Data-driven layout for a MOBA map: bases (spawn + nexus) and lane paths. Loaded from the
// world DB (moba_map, moba_lane_point), so retuning a map needs only SQL + a restart.
struct TowerSpawn
{
    uint32 Team = 0;       // team id (Blue/Red)
    uint32 Lane = 0;       // lane index (0/1/2), or a special value for nexus towers
    uint32 Ord = 0;        // 0 = outermost (destroyed first), increasing toward the base
    Position Pos;
};

struct MapLayout
{
    Position BlueBase;                            // Blue (Alliance) champion spawn + nexus
    Position RedBase;                             // Red (Horde) champion spawn + nexus
    std::vector<std::vector<Position>> Lanes;     // each lane is a path in Blue->Red order
    std::vector<TowerSpawn> Towers;              // towers on this map (data-driven)
};

// Load all map layouts from the world DB (replaces the in-memory cache).
void LoadMobaMaps();
// Returns the layout for a map id (lazy-loaded on first use), or nullptr if none is defined.
MapLayout const* GetMobaMapLayout(uint32 mapId);
}

#endif
