/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "BattlegroundNA.h"
#include "Creature.h"
#include "Log.h"
#include "Pet.h"
#include "Player.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "WorldStatePackets.h"

BattlegroundNA::BattlegroundNA()
{
    BgObjects.resize(BG_NA_OBJECT_MAX);
    BgCreatures.resize(BG_NA_CREATURE_MAX);

    StartDelayTimes[BG_STARTING_EVENT_FIRST]  = BG_START_DELAY_NONE;
    StartDelayTimes[BG_STARTING_EVENT_SECOND] = BG_START_DELAY_NONE;
    StartDelayTimes[BG_STARTING_EVENT_THIRD]  = BG_START_DELAY_NONE;
    StartDelayTimes[BG_STARTING_EVENT_FOURTH] = BG_START_DELAY_NONE;
}

void BattlegroundNA::PostUpdateImpl(uint32 diff)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    _events.Update(diff);

    while (uint32 eventId = _events.ExecuteEvent())
    {
        switch (eventId)
        {
            case BG_NA_EVENT_REMOVE_DOORS:
                for (uint32 i = BG_NA_OBJECT_DOOR_1; i <= BG_NA_OBJECT_DOOR_2; ++i)
                    DelObject(i);
                break;
            default:
                break;
        }
    }
}

void BattlegroundNA::AddPlayer(Player* player)
{
    Arena::AddPlayer(player);

    if (player)
    {
        player->SetFaction(Moba::GetFactionForTeamId(player->GetBGTeam()));

        if (Pet* pet = player->GetPet())
            pet->SetFaction(player->GetFaction());
    }

    SpawnMobaNexuses();

    if (player)
        player->GetSession()->SendNotification("MOBA: tentative de spawn des Nexus Blue/Red.");
}

void BattlegroundNA::RemovePlayer(Player* player, ObjectGuid /*guid*/, uint32 /*team*/)
{
    if (!player)
        return;

    player->SetFactionForRace(player->GetRace());

    if (Pet* pet = player->GetPet())
        pet->SetFaction(player->GetFaction());
}

void BattlegroundNA::StartingEventCloseDoors()
{
    for (uint32 i = BG_NA_OBJECT_DOOR_1; i <= BG_NA_OBJECT_DOOR_4; ++i)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
}

void BattlegroundNA::StartingEventOpenDoors()
{
    for (uint32 i = BG_NA_OBJECT_DOOR_1; i <= BG_NA_OBJECT_DOOR_2; ++i)
        DoorOpen(i);
    _events.ScheduleEvent(BG_NA_EVENT_REMOVE_DOORS, BG_NA_REMOVE_DOORS_TIMER);

    for (uint32 i = BG_NA_OBJECT_BUFF_1; i <= BG_NA_OBJECT_BUFF_2; ++i)
        SpawnBGObject(i, 60);

    SpawnMobaNexuses();
}

void BattlegroundNA::SpawnMobaNexuses()
{
    if (GetBGCreature(BG_NA_CREATURE_BLUE_NEXUS, false) || GetBGCreature(BG_NA_CREATURE_RED_NEXUS, false))
        return;

    Position const* blueStart = GetTeamStartPosition(GetTeamIndexByTeamId(BG_NA_MOBA_TEAM_BLUE));
    Position const* redStart = GetTeamStartPosition(GetTeamIndexByTeamId(BG_NA_MOBA_TEAM_RED));

    if (!blueStart || !redStart)
    {
        TC_LOG_ERROR("bg.battleground", "MOBA: Nexus spawn failed, missing start positions in Nagrand Arena BG instance {}", GetInstanceID());
        return;
    }

    TC_LOG_ERROR("bg.battleground", "MOBA: spawning Nexus objectives in Nagrand Arena BG instance {}", GetInstanceID());

    if (Creature* nexus = AddCreature(BG_NA_CREATURE_TYPE_BLUE_NEXUS, BG_NA_CREATURE_BLUE_NEXUS,
        blueStart->GetPositionX(), blueStart->GetPositionY(), blueStart->GetPositionZ(), blueStart->GetOrientation(), TEAM_NEUTRAL, RESPAWN_IMMEDIATELY))
    {
        nexus->SetFaction(Moba::GetFactionForTeamId(BG_NA_MOBA_TEAM_BLUE));
        TC_LOG_ERROR("bg.battleground", "MOBA: Blue Nexus spawned in Nagrand Arena BG instance {}", GetInstanceID());
        nexus->Yell("Nexus Blue en ligne.", LANG_UNIVERSAL, nullptr);
    }
    else
        TC_LOG_ERROR("bg.battleground", "MOBA: Blue Nexus spawn failed in Nagrand Arena BG instance {}", GetInstanceID());

    if (Creature* nexus = AddCreature(BG_NA_CREATURE_TYPE_RED_NEXUS, BG_NA_CREATURE_RED_NEXUS,
        redStart->GetPositionX(), redStart->GetPositionY(), redStart->GetPositionZ(), redStart->GetOrientation(), TEAM_NEUTRAL, RESPAWN_IMMEDIATELY))
    {
        nexus->SetFaction(Moba::GetFactionForTeamId(BG_NA_MOBA_TEAM_RED));
        TC_LOG_ERROR("bg.battleground", "MOBA: Red Nexus spawned in Nagrand Arena BG instance {}", GetInstanceID());
        nexus->Yell("Detruis le Nexus Red pour gagner.", LANG_UNIVERSAL, nullptr);
    }
    else
        TC_LOG_ERROR("bg.battleground", "MOBA: Red Nexus spawn failed in Nagrand Arena BG instance {}", GetInstanceID());
}

void BattlegroundNA::HandleAreaTrigger(Player* player, uint32 trigger)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 4536:                                          // buff trigger?
        case 4537:                                          // buff trigger?
            break;
        default:
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }
}

void BattlegroundNA::HandleKillUnit(Creature* creature, Player* killer)
{
    if (!creature || !Moba::IsNexusEntry(creature->GetEntry()))
        return;

    if (GetStatus() != STATUS_IN_PROGRESS && GetStatus() != STATUS_WAIT_JOIN)
        return;

    uint32 winner = GetWinnerForDestroyedNexus(creature);
    if (!Moba::IsTeamId(winner))
    {
        winner = killer ? killer->GetBGTeam() : BG_NA_MOBA_TEAM_BLUE;
        if (!Moba::IsTeamId(winner))
            winner = BG_NA_MOBA_TEAM_BLUE;
    }

    TC_LOG_INFO("bg.battleground", "MOBA: Nexus killed by {} in Nagrand Arena BG instance {}, winner team {}",
        killer ? killer->GetName() : "<unknown>", GetInstanceID(), winner);
    Battleground::EndBattleground(winner);
}

void BattlegroundNA::CheckWinConditions()
{
    // MOBA prototype: Nagrand Arena is used as a Nexus objective map.
    // A solo test player must not instantly win because the other arena team is empty.
}

uint32 BattlegroundNA::GetWinnerForDestroyedNexus(Creature const* creature)
{
    if (!creature)
        return 0;

    if (uint32 winner = Moba::GetWinnerTeamIdForDestroyedNexus(creature->GetEntry()))
        return winner;

    if (Creature const* blueNexus = GetBGCreature(BG_NA_CREATURE_BLUE_NEXUS))
        if (blueNexus->GetGUID() == creature->GetGUID())
            return BG_NA_MOBA_TEAM_RED;

    if (Creature const* redNexus = GetBGCreature(BG_NA_CREATURE_RED_NEXUS))
        if (redNexus->GetGUID() == creature->GetGUID())
            return BG_NA_MOBA_TEAM_BLUE;

    return 0;
}

void BattlegroundNA::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(2577, 1); // BATTLEGROUND_NAGRAND_ARENA_SHOW

    Arena::FillInitialWorldStates(packet);
}

bool BattlegroundNA::SetupBattleground()
{
    // gates
    if (!AddObject(BG_NA_OBJECT_DOOR_1, BG_NA_OBJECT_TYPE_DOOR_1, 4031.854f, 2966.833f, 12.6462f, -2.648788f, 0, 0, 0.9697962f, -0.2439165f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_NA_OBJECT_DOOR_2, BG_NA_OBJECT_TYPE_DOOR_2, 4081.179f, 2874.97f, 12.39171f, 0.4928045f, 0, 0, 0.2439165f, 0.9697962f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_NA_OBJECT_DOOR_3, BG_NA_OBJECT_TYPE_DOOR_3, 4023.709f, 2981.777f, 10.70117f, -2.648788f, 0, 0, 0.9697962f, -0.2439165f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_NA_OBJECT_DOOR_4, BG_NA_OBJECT_TYPE_DOOR_4, 4090.064f, 2858.438f, 10.23631f, 0.4928045f, 0, 0, 0.2439165f, 0.9697962f, RESPAWN_IMMEDIATELY)
    // buffs
        || !AddObject(BG_NA_OBJECT_BUFF_1, BG_NA_OBJECT_TYPE_BUFF_1, 4009.189941f, 2895.250000f, 13.052700f, -1.448624f, 0, 0, 0.6626201f, -0.7489557f, 120)
        || !AddObject(BG_NA_OBJECT_BUFF_2, BG_NA_OBJECT_TYPE_BUFF_2, 4103.330078f, 2946.350098f, 13.051300f, -0.06981307f, 0, 0, 0.03489945f, -0.9993908f, 120))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundNA: Failed to spawn some object!");
        return false;
    }

    return true;
}
