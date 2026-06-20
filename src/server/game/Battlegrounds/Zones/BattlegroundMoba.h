/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef __BATTLEGROUNDMOBA_H
#define __BATTLEGROUNDMOBA_H

#include "Battleground.h"
#include "MobaGame.h"

class BattlegroundMoba : public Battleground
{
public:
    BattlegroundMoba();

    void AddPlayer(Player* player) override;
    void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;
    void StartingEventCloseDoors() override;
    void StartingEventOpenDoors() override;
    void HandleKillUnit(Creature* creature, Player* killer) override;
    void HandleKillPlayer(Player* victim, Player* killer) override;
    bool SetupBattleground() override;
    void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

private:
    void PostUpdateImpl(uint32 diff) override;
    void CheckWinConditions() override;
    void StartMobaMatch();

    Moba::MatchController _moba;
};

#endif
