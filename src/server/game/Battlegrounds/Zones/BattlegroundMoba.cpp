/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "BattlegroundMoba.h"
#include "BattlegroundPackets.h"
#include "BattlegroundScore.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MobaMapConfig.h"
#include "MobaRules.h"
#include "Pet.h"
#include "Player.h"
#include "WorldSession.h"
#include "WorldStatePackets.h"

namespace
{
// Minimal per-player score so champions appear in the end-of-match scoreboard. The base PvP
// columns (kills/deaths/damage/healing/honor) are shown; no MOBA-specific objective columns yet.
struct BattlegroundMobaScore : public BattlegroundScore
{
    explicit BattlegroundMobaScore(ObjectGuid playerGuid) : BattlegroundScore(playerGuid) { }

    void BuildObjectivesBlock(WorldPackets::Battleground::PVPLogData_Player& /*playerData*/) override { }
};
}

BattlegroundMoba::BattlegroundMoba()
{
    StartDelayTimes[BG_STARTING_EVENT_FIRST]  = BG_START_DELAY_NONE;
    StartDelayTimes[BG_STARTING_EVENT_SECOND] = BG_START_DELAY_NONE;
    StartDelayTimes[BG_STARTING_EVENT_THIRD]  = BG_START_DELAY_NONE;
    StartDelayTimes[BG_STARTING_EVENT_FOURTH] = BG_START_DELAY_NONE;
}

void BattlegroundMoba::PostUpdateImpl(uint32 diff)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    _moba.Update(diff);
}

void BattlegroundMoba::AddPlayer(Player* player)
{
    Battleground::AddPlayer(player);

    if (!player)
        return;

    // Register a score so the player shows up in the end-of-match scoreboard (kept across reconnect).
    if (PlayerScores.find(player->GetGUID()) == PlayerScores.end())
        PlayerScores[player->GetGUID()] = new BattlegroundMobaScore(player->GetGUID());

    player->SetFaction(Moba::GetFactionForTeamId(player->GetBGTeam()));

    if (Pet* pet = player->GetPet())
        pet->SetFaction(player->GetFaction());
}

void BattlegroundMoba::RemovePlayer(Player* player, ObjectGuid /*guid*/, uint32 /*team*/)
{
    if (!player)
        return;

    player->SetFactionForRace(player->GetRace());

    if (Pet* pet = player->GetPet())
        pet->SetFaction(player->GetFaction());
}

void BattlegroundMoba::StartingEventCloseDoors()
{
}

void BattlegroundMoba::StartingEventOpenDoors()
{
    StartMobaMatch();
}

void BattlegroundMoba::StartMobaMatch()
{
    Moba::MapLayout const* map = Moba::GetMobaMapLayout(GetMapId());
    if (!map)
    {
        TC_LOG_ERROR("bg.battleground", "MOBA: no layout configured for map {} (table moba_map). Match not started.", GetMapId());
        return;
    }

    Moba::ArenaLayout layout;
    layout.BlueNexus = map->BlueBase;
    layout.RedNexus = map->RedBase;
    layout.Lanes = map->Lanes;
    for (Moba::TowerSpawn const& tower : map->Towers)
        layout.Towers.push_back({ tower.Team, tower.Lane, tower.Ord, tower.Pos });

    _moba.Start(GetBgMap(), layout);
}

void BattlegroundMoba::HandleKillUnit(Creature* creature, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS && GetStatus() != STATUS_WAIT_JOIN)
        return;

    uint32 const winner = _moba.OnUnitKilled(creature, killer);
    if (!Moba::IsTeamId(winner))
        return;

    TC_LOG_INFO("bg.battleground", "MOBA: nexus destroyed in instance {}, winner team {}", GetInstanceID(), winner);
    EndBattleground(winner);
}

void BattlegroundMoba::CheckWinConditions()
{
    // The MOBA match controller owns win conditions.
}

void BattlegroundMoba::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    Battleground::FillInitialWorldStates(packet);
}

bool BattlegroundMoba::SetupBattleground()
{
    return true;
}
