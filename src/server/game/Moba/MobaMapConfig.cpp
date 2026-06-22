/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaMapConfig.h"

#include "DatabaseEnv.h"
#include "Log.h"
#include "MobaRules.h"

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

    // Per-team height of the invisible shot emitter above the tower base (tune in moba_tower_muzzle,
    // no C++ rebuild). Defaults if the table is empty/missing a team.
    float blueMuzzleDz = MobaTowerMuzzleHeight;
    float redMuzzleDz = MobaTowerMuzzleHeight;
    if (QueryResult result = WorldDatabase.Query("SELECT team, dz FROM moba_tower_muzzle"))
    {
        do
        {
            Field* fields = result->Fetch();
            if (fields[0].GetUInt8() == 0)
                blueMuzzleDz = fields[1].GetFloat();
            else
                redMuzzleDz = fields[1].GetFloat();
        } while (result->NextRow());
    }

    uint32 towers = 0;
    if (QueryResult result = WorldDatabase.Query("SELECT mapId, team, lane, ord, x, y, z, o FROM moba_tower ORDER BY mapId, team, lane, ord"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const mapId = fields[0].GetUInt32();

            auto itr = g_layouts.find(mapId);
            if (itr == g_layouts.end())
                continue;

            bool const isBlue = fields[1].GetUInt8() == 0;
            TowerSpawn tower;
            tower.Team = isBlue ? BlueTeamId : RedTeamId;
            tower.Lane = fields[2].GetUInt32();
            tower.Ord = fields[3].GetUInt32();
            tower.Pos = Position(fields[4].GetFloat(), fields[5].GetFloat(), fields[6].GetFloat(), fields[7].GetFloat());
            tower.MuzzleDz = isBlue ? blueMuzzleDz : redMuzzleDz;
            itr->second.Towers.push_back(tower);
            ++towers;
        } while (result->NextRow());
    }

    g_loaded = true;
    TC_LOG_INFO("server.loading", "MOBA: loaded {} map layout(s), {} lane waypoint(s), {} tower(s)", g_layouts.size(), points, towers);
}

MapLayout const* GetMobaMapLayout(uint32 mapId)
{
    if (!g_loaded)
        LoadMobaMaps();

    auto itr = g_layouts.find(mapId);
    return itr != g_layouts.end() ? &itr->second : nullptr;
}
}
