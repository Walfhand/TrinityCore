/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaGame.h"

#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MobaRules.h"
#include "MobaTower.h"
#include "Player.h"
#include "TemporarySummon.h"

#include <algorithm>
#include <utility>

namespace Moba
{
namespace
{
enum MatchEvents
{
    EVENT_SPAWN_WAVE = 1
};

void SummonNexus(Map* map, uint32 teamId, Position const& position)
{
    uint32 const entry = GetNexusEntryForTeamId(teamId);
    if (!map || !entry)
        return;

    TempSummon* nexus = map->SummonCreature(entry, position, nullptr, 0);
    if (!nexus)
    {
        TC_LOG_ERROR("bg.battleground", "MOBA: nexus {} spawn failed in instance {}", entry, map->GetInstanceId());
        return;
    }

    nexus->SetFaction(GetFactionForTeamId(teamId));
}
}

void MatchController::Start(Map* map, ArenaLayout const& layout)
{
    if (_started || !map)
        return;

    _map = map;
    _layout = layout;

    // One LaneConfig per authored lane: minions spawn at the near-base end and follow the
    // path (Blue forward, Red reversed). Adding a lane = adding a waypoint list in the config.
    _lanes.clear();
    _pendingMinionSpawns.clear();
    for (std::vector<Position> const& waypoints : layout.Lanes)
    {
        if (waypoints.empty())
            continue;

        LaneConfig lane;
        lane.Name = "moba-lane";
        lane.BlueSpawn = waypoints.front();
        lane.RedSpawn = waypoints.back();
        lane.BlueDestination = waypoints.back();
        lane.RedDestination = waypoints.front();
        lane.Waypoints = waypoints;
        _lanes.push_back(lane);
    }

    _waveNumber = 0;
    _elapsedMs = 0;
    _started = true;

    SpawnNexuses();
    for (ArenaTower const& tower : _layout.Towers)
        SpawnTower(_map, tower.Team, tower.Lane, tower.Ord, tower.Pos);

    Seconds const firstWaveDelay = _lanes.empty() ? Seconds(30) : _lanes.front().FirstWaveDelay;
    _events.ScheduleEvent(EVENT_SPAWN_WAVE, firstWaveDelay);

    TC_LOG_INFO("bg.battleground", "MOBA: match started in instance {} ({} lanes)", map->GetInstanceId(), _lanes.size());
}

void MatchController::Update(uint32 diff)
{
    if (!_started)
        return;

    _elapsedMs += diff;
    _events.Update(diff);

    while (uint32 eventId = _events.ExecuteEvent())
    {
        if (eventId == EVENT_SPAWN_WAVE)
        {
            SpawnWave();
            _events.ScheduleEvent(EVENT_SPAWN_WAVE, GetWaveInterval(_elapsedMs));
        }
    }

    ProcessPendingMinionSpawns();
}

uint32 MatchController::OnUnitKilled(Creature* creature, Player* killer)
{
    if (!creature || !IsNexusEntry(creature->GetEntry()))
        return InvalidTeamId;

    uint32 winner = GetWinnerTeamIdForDestroyedNexus(creature->GetEntry());
    if (!IsTeamId(winner) && killer)
        winner = killer->GetBGTeam();

    return IsTeamId(winner) ? winner : InvalidTeamId;
}

void MatchController::SpawnNexuses()
{
    SummonNexus(_map, BlueTeamId, _layout.BlueNexus);
    SummonNexus(_map, RedTeamId, _layout.RedNexus);
}

void MatchController::SpawnWave()
{
    if (!_map)
        return;

    ++_waveNumber;
    MinionWavePlan const plan = PlanMinionWave(_waveNumber, _elapsedMs);
    for (uint32 laneIndex = 0; laneIndex < _lanes.size(); ++laneIndex)
    {
        LaneConfig const& lane = _lanes[laneIndex];
        TC_LOG_INFO("bg.battleground", "MOBA: wave '{}' in BG instance {} (melee {}, caster {}, siege {}, upgrade {})",
            lane.Name, _map->GetInstanceId(), plan.MeleeCount, plan.CasterCount, plan.SiegeCount, plan.UpgradeLevel);

        for (MinionSpawn& spawn : BuildMinionWaveSpawns(lane, plan, laneIndex))
            QueueMinionSpawn(std::move(spawn));
    }

    ProcessPendingMinionSpawns();
}

void MatchController::QueueMinionSpawn(MinionSpawn&& spawn)
{
    PendingMinionSpawn pending;
    pending.DueElapsedMs = _elapsedMs + uint32(spawn.Delay.count());
    pending.Spawn = std::move(spawn);
    _pendingMinionSpawns.push_back(std::move(pending));
}

void MatchController::ProcessPendingMinionSpawns()
{
    if (!_map)
        return;

    uint32 const instanceId = _map->GetInstanceId();
    std::vector<uint32> spawnedStreams;
    auto itr = _pendingMinionSpawns.begin();
    while (itr != _pendingMinionSpawns.end())
    {
        if (itr->DueElapsedMs > _elapsedMs
            || std::find(spawnedStreams.begin(), spawnedStreams.end(), itr->Spawn.SpawnStreamId) != spawnedStreams.end())
        {
            ++itr;
            continue;
        }

        SpawnMinion(_map, instanceId, itr->Spawn);
        spawnedStreams.push_back(itr->Spawn.SpawnStreamId);
        itr = _pendingMinionSpawns.erase(itr);
    }
}
}
