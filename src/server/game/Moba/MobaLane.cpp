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
#include "Unit.h"

#include <algorithm>

namespace Moba
{
namespace
{
// Wave cadence / scaling thresholds, mirroring League of Legends timings.
constexpr uint32 SecondPhaseMs = 14u * 60u * 1000u;  // 14:00 -> siege every 2nd wave, 25s interval
constexpr uint32 ThirdPhaseMs = 25u * 60u * 1000u;   // 25:00 -> siege every wave
constexpr uint32 ThirtyMinMs = 30u * 60u * 1000u;    // 30:00 -> 20s interval
constexpr uint32 FirstUpgradeMs = 90u * 1000u;       // first waves stay level 1
constexpr uint32 UpgradePeriodMs = 90u * 1000u;      // an upgrade every 90s
constexpr uint32 MaxUpgrades = 30u;

constexpr uint32 FirstSiegeWave = 3u;                // first siege leaves at 1:30 (wave 3)

constexpr uint32 MinionSpawnStepMs = 1000u;          // one visible server tick between minions

uint32 GetMinionLevelForUpgrade(uint32 upgradeLevel)
{
    return std::min<uint32>(MobaStartLevel + upgradeLevel, MobaMaxLevel);
}

uint32 GetSpawnStreamId(uint32 laneIndex, uint32 teamId)
{
    return laneIndex * 2u + (teamId == RedTeamId ? 1u : 0u);
}

MinionSpawn BuildMinionSpawn(uint32 spawnStreamId, uint32 teamId, MinionType type, Position const& start,
    std::vector<Position> const& path, uint32 upgradeLevel, uint32 spawnIndex)
{
    MinionSpawn spawn;
    spawn.SpawnStreamId = spawnStreamId;
    spawn.FormationIndex = spawnIndex;
    spawn.TeamId = teamId;
    spawn.Type = type;
    spawn.Path = path;
    spawn.SpawnPosition = start;
    spawn.UpgradeLevel = upgradeLevel;
    spawn.Delay = Milliseconds(spawnIndex * MinionSpawnStepMs);
    return spawn;
}

void AppendMinionSpawns(std::vector<MinionSpawn>& spawns, uint32 spawnStreamId, uint32 teamId, MinionType type, uint32 count,
    Position const& start, std::vector<Position> const& path, uint32 upgradeLevel,
    uint32& spawnIndex)
{
    for (uint32 i = 0; i < count; ++i)
        spawns.push_back(BuildMinionSpawn(spawnStreamId, teamId, type, start, path, upgradeLevel, spawnIndex++));
}

void SpawnLaneMinion(Map* map, uint32 instanceId, MinionSpawn const& spawn)
{
    uint32 const teamId = spawn.TeamId;
    MinionType const type = spawn.Type;
    uint32 const entry = GetMinionEntry(teamId, type);
    if (!map || !entry)
        return;

    TempSummon* minion = map->SummonCreature(entry, spawn.SpawnPosition, nullptr, 0);
    if (!minion)
    {
        TC_LOG_ERROR("bg.battleground", "MOBA: minion {} spawn failed in BG instance {}", entry, instanceId);
        return;
    }

    minion->SetFaction(GetFactionForTeamId(teamId));
    uint32 const minionLevel = GetMinionLevelForUpgrade(spawn.UpgradeLevel);
    RegisterMinionLevel(minion, minionLevel);
    ApplyMinionCombatTuning(minion, minionLevel);
    RegisterMinionLanePath(minion, spawn.Path, spawn.SpawnStreamId, spawn.FormationIndex);
    ResumeMinionLaneMovement(minion);   // start walking the lane toward the first forward waypoint
}

void AppendTeamWaveSpawns(std::vector<MinionSpawn>& spawns, uint32 spawnStreamId, uint32 teamId, Position const& start, std::vector<Position> const& path, MinionWavePlan const& plan)
{
    uint32 spawnIndex = 0;
    AppendMinionSpawns(spawns, spawnStreamId, teamId, MinionType::Melee, plan.MeleeCount, start, path, plan.UpgradeLevel, spawnIndex);
    AppendMinionSpawns(spawns, spawnStreamId, teamId, MinionType::Siege, plan.SiegeCount, start, path, plan.UpgradeLevel, spawnIndex);
    AppendMinionSpawns(spawns, spawnStreamId, teamId, MinionType::Caster, plan.CasterCount, start, path, plan.UpgradeLevel, spawnIndex);
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
    return lane;
}

MinionWavePlan PlanMinionWave(uint32 waveNumber, uint32 elapsedMs)
{
    MinionWavePlan plan;
    plan.MeleeCount = 3;
    plan.CasterCount = 3;

    uint32 const siegeCadence = elapsedMs < SecondPhaseMs ? 3u : (elapsedMs < ThirdPhaseMs ? 2u : 1u);
    if (waveNumber >= FirstSiegeWave && (waveNumber % siegeCadence) == 0)
    {
        plan.SiegeCount = 1;
        if (elapsedMs >= ThirtyMinMs)
            plan.CasterCount = 2;  // League trims casters to 2 once siege waves stack up
    }

    if (elapsedMs >= FirstUpgradeMs)
        plan.UpgradeLevel = std::min<uint32>(((elapsedMs - FirstUpgradeMs) / UpgradePeriodMs) + 1, MaxUpgrades);

    return plan;
}

Milliseconds GetWaveInterval(uint32 elapsedMs)
{
    if (elapsedMs < SecondPhaseMs)
        return Milliseconds(30000);

    if (elapsedMs < ThirtyMinMs)
        return Milliseconds(25000);

    return Milliseconds(20000);
}

std::vector<MinionSpawn> BuildMinionWaveSpawns(LaneConfig const& lane, MinionWavePlan const& plan, uint32 laneIndex)
{
    std::vector<MinionSpawn> spawns;

    // Blue walks the lane as authored (Blue->Red); Red walks it reversed (Red->Blue).
    std::vector<Position> bluePath = lane.Waypoints;
    bluePath.push_back(lane.BlueDestination);

    std::vector<Position> redPath(lane.Waypoints.rbegin(), lane.Waypoints.rend());
    redPath.push_back(lane.RedDestination);

    spawns.reserve((plan.MeleeCount + plan.CasterCount + plan.SiegeCount) * 2);
    AppendTeamWaveSpawns(spawns, GetSpawnStreamId(laneIndex, BlueTeamId), BlueTeamId, lane.BlueSpawn, bluePath, plan);
    AppendTeamWaveSpawns(spawns, GetSpawnStreamId(laneIndex, RedTeamId), RedTeamId, lane.RedSpawn, redPath, plan);

    return spawns;
}

void SpawnMinion(Map* map, uint32 instanceId, MinionSpawn const& spawn)
{
    SpawnLaneMinion(map, instanceId, spawn);
}
}
