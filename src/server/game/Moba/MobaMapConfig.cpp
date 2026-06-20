/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaMapConfig.h"

#include "DatabaseEnv.h"
#include "Log.h"

#include <unordered_map>

namespace Moba
{
namespace
{
std::unordered_map<uint32, MapLayout> g_layouts;
bool g_loaded = false;
}

void LoadMobaMaps()
{
    g_layouts.clear();

    if (QueryResult result = WorldDatabase.Query("SELECT mapId, blueX, blueY, blueZ, blueO, redX, redY, redZ, redO FROM moba_map"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const mapId = fields[0].GetUInt32();
            MapLayout& layout = g_layouts[mapId];
            layout.BlueBase = Position(fields[1].GetFloat(), fields[2].GetFloat(), fields[3].GetFloat(), fields[4].GetFloat());
            layout.RedBase  = Position(fields[5].GetFloat(), fields[6].GetFloat(), fields[7].GetFloat(), fields[8].GetFloat());
        } while (result->NextRow());
    }

    uint32 points = 0;
    if (QueryResult result = WorldDatabase.Query("SELECT mapId, lane, idx, x, y, z FROM moba_lane_point ORDER BY mapId, lane, idx"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const mapId = fields[0].GetUInt32();
            uint32 const lane = fields[1].GetUInt32();

            auto itr = g_layouts.find(mapId);
            if (itr == g_layouts.end())
                continue;   // lane points for a map without a base row; ignore

            MapLayout& layout = itr->second;
            if (layout.Lanes.size() <= lane)
                layout.Lanes.resize(lane + 1);

            layout.Lanes[lane].push_back(Position(fields[3].GetFloat(), fields[4].GetFloat(), fields[5].GetFloat()));
            ++points;
        } while (result->NextRow());
    }

    g_loaded = true;
    TC_LOG_INFO("server.loading", "MOBA: loaded {} map layout(s), {} lane waypoint(s)", g_layouts.size(), points);
}

MapLayout const* GetMobaMapLayout(uint32 mapId)
{
    if (!g_loaded)
        LoadMobaMaps();

    auto itr = g_layouts.find(mapId);
    return itr != g_layouts.end() ? &itr->second : nullptr;
}
}
