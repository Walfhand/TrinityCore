/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_MAP_CONFIG_H
#define GAME_MOBA_MAP_CONFIG_H

#include "Position.h"

#include <vector>

namespace Moba
{
// Data-driven layout for a MOBA map: the single source of truth for spawn/nexus positions
// and lane paths. Used by the battleground host (team port + nexus spawn + waves) and the
// matchmaker (team start). Adding a map = adding a layout here, no engine changes.
struct MapLayout
{
    Position BlueBase;                    // Blue (Alliance) champion spawn + nexus
    Position RedBase;                     // Red (Horde) champion spawn + nexus
    std::vector<Position> MidLaneWaypoints;   // lane path, Blue->Red order (Red walks it reversed)
};

// Guerilla (map 900). Coordinates scouted in-game with .gps (ground level).
inline MapLayout const& GetGuerillaLayout()
{
    static MapLayout const layout =
    {
        Position(3317.389160f, 2012.633179f, 9.346647f, 2.410388f),
        Position(3083.737549f, 2249.815674f, 5.446488f, 5.544910f),
        {
            Position(3307.284668f, 2018.573975f, 8.487104f),
            Position(3271.917969f, 2057.500732f, 6.153394f),
            Position(3239.360107f, 2091.791504f, 5.109375f),
            Position(3214.851318f, 2117.604736f, 7.437378f),
            Position(3196.680420f, 2136.742676f, 12.615769f),   // bridge: needs vmaps for proper walking
            Position(3180.101318f, 2155.558350f, 6.434991f),
            Position(3145.687012f, 2187.170410f, 4.146327f),
            Position(3119.087158f, 2211.604736f, 1.557615f),
            Position(3084.145264f, 2249.052246f, 5.496557f)
        }
    };
    return layout;
}
}

#endif
