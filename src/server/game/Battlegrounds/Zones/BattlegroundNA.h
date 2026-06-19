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
#ifndef __BATTLEGROUNDNA_H
#define __BATTLEGROUNDNA_H

#include "Arena.h"
#include "EventMap.h"
#include "MobaLane.h"
#include "MobaRules.h"

enum BattlegroundNAObjectTypes
{
    BG_NA_OBJECT_DOOR_1         = 0,
    BG_NA_OBJECT_DOOR_2         = 1,
    BG_NA_OBJECT_DOOR_3         = 2,
    BG_NA_OBJECT_DOOR_4         = 3,
    BG_NA_OBJECT_BUFF_1         = 4,
    BG_NA_OBJECT_BUFF_2         = 5,
    BG_NA_OBJECT_MAX            = 6
};

enum BattlegroundNACreatureTypes
{
    BG_NA_CREATURE_BLUE_NEXUS   = 0,
    BG_NA_CREATURE_RED_NEXUS    = 1,
    BG_NA_CREATURE_MAX          = 2
};

enum BattlegroundNAGameObjects
{
    BG_NA_OBJECT_TYPE_DOOR_1    = 183978,
    BG_NA_OBJECT_TYPE_DOOR_2    = 183980,
    BG_NA_OBJECT_TYPE_DOOR_3    = 183977,
    BG_NA_OBJECT_TYPE_DOOR_4    = 183979,
    BG_NA_OBJECT_TYPE_BUFF_1    = 184663,
    BG_NA_OBJECT_TYPE_BUFF_2    = 184664
};

enum BattlegroundNACreatures
{
    BG_NA_CREATURE_TYPE_BLUE_NEXUS = Moba::NpcBlueNexus,
    BG_NA_CREATURE_TYPE_RED_NEXUS  = Moba::NpcRedNexus
};

enum BattlegroundNAMobaTeams
{
    BG_NA_MOBA_TEAM_BLUE        = Moba::BlueTeamId,
    BG_NA_MOBA_TEAM_RED         = Moba::RedTeamId
};

inline constexpr Seconds BG_NA_REMOVE_DOORS_TIMER    = 5s;

enum BattlegroundNAEvents
{
    BG_NA_EVENT_REMOVE_DOORS     = 1,
    BG_NA_EVENT_SPAWN_MOBA_WAVE  = 2
};

class BattlegroundNA : public Arena
{
    public:
        BattlegroundNA();

        /* inherited from BattlegroundClass */
        void AddPlayer(Player* player) override;
        void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;
        void StartingEventCloseDoors() override;
        void StartingEventOpenDoors() override;

        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;
        void HandleKillUnit(Creature* creature, Player* killer) override;
        bool SetupBattleground() override;
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

    private:
        void PostUpdateImpl(uint32 diff) override;
        void CheckWinConditions() override;
        void SpawnMobaNexuses();
        void SpawnMobaMinionWave();
        bool BuildMobaLaneConfig();
        uint32 GetWinnerForDestroyedNexus(Creature const* creature);

        EventMap _events;
        Moba::LaneConfig _mobaLane;
};
#endif
