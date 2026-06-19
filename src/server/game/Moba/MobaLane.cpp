/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaLane.h"

#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MobaMinion.h"
#include "MobaRules.h"
#include "MotionMaster.h"
#include "TemporarySummon.h"

#include <cmath>

namespace Moba
{
namespace
{
Position OffsetLanePosition(Position const& start, Position const& destination, float laneOffset)
{
    float dx = destination.GetPositionX() - start.GetPositionX();
    float dy = destination.GetPositionY() - start.GetPositionY();
    float length = std::sqrt(dx * dx + dy * dy);
    float perpendicularX = length > 0.0f ? -dy / length : 0.0f;
    float perpendicularY = length > 0.0f ? dx / length : 0.0f;

    return Position(start.GetPositionX() + perpendicularX * laneOffset, start.GetPositionY() + perpendicularY * laneOffset,
        start.GetPositionZ(), start.GetOrientation());
}

void SpawnMinion(Map* map, uint32 instanceId, uint32 teamId, Position const& start, Position const& destination, float laneOffset)
{
    uint32 entry = GetMinionEntryForTeamId(teamId);
    if (!map || !entry)
        return;

    Position spawnPos = OffsetLanePosition(start, destination, laneOffset);
    TempSummon* minion = map->SummonCreature(entry, spawnPos, nullptr, 0);
    if (!minion)
    {
        TC_LOG_ERROR("bg.battleground", "MOBA: minion {} spawn failed in BG instance {}", entry, instanceId);
        return;
    }

    minion->SetFaction(GetFactionForTeamId(teamId));
    RegisterMinionLaneDestination(minion, destination);
    minion->GetMotionMaster()->MovePoint(0, destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());
}
}

LaneConfig BuildSingleLaneConfig(char const* name, Position const& blueSpawn, Position const& redSpawn)
{
    LaneConfig lane;
    lane.Name = name;
    lane.BlueSpawn = blueSpawn;
    lane.RedSpawn = redSpawn;
    lane.BlueDestination = redSpawn;
    lane.RedDestination = blueSpawn;
    lane.SpawnOffsets = { -2.5f, 0.0f, 2.5f };
    return lane;
}

void SpawnMinionWave(Map* map, LaneConfig const& lane, uint32 instanceId)
{
    if (!map)
        return;

    TC_LOG_INFO("bg.battleground", "MOBA: spawning minion wave '{}' in BG instance {}", lane.Name, instanceId);

    for (float offset : lane.SpawnOffsets)
    {
        SpawnMinion(map, instanceId, BlueTeamId, lane.BlueSpawn, lane.BlueDestination, offset);
        SpawnMinion(map, instanceId, RedTeamId, lane.RedSpawn, lane.RedDestination, offset);
    }
}
}
