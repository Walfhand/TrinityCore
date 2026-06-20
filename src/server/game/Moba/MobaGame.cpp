/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaGame.h"

#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MobaRules.h"
#include "Player.h"
#include "TemporarySummon.h"

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
    for (LaneConfig const& lane : _lanes)
        SpawnMinionWave(_map, lane, _map->GetInstanceId(), plan);
}
}
