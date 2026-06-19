/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_MAP_CONFIG_H
#define GAME_MOBA_MAP_CONFIG_H

#include "Position.h"

namespace Moba
{
// Data-driven layout for a MOBA map: the single source of truth for spawn/nexus positions.
// Used by the battleground host (team port + nexus spawn) and the matchmaker (team start).
struct MapLayout
{
    Position BlueBase;   // Blue (Alliance) champion spawn + nexus
    Position RedBase;    // Red (Horde) champion spawn + nexus
};

// Guerilla (map 900). Coordinates scouted in-game with .gps (ground level).
inline MapLayout const& GetGuerillaLayout()
{
    static MapLayout const layout =
    {
        Position(3317.389160f, 2012.633179f, 9.346647f, 2.410388f),
        Position(3083.737549f, 2249.815674f, 5.446488f, 5.544910f)
    };
    return layout;
}
}

#endif
