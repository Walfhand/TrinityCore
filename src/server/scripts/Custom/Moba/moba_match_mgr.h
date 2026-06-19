/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef CUSTOM_MOBA_MATCH_MGR_H
#define CUSTOM_MOBA_MATCH_MGR_H

#include "Define.h"
#include "MobaRules.h"
#include "SharedDefines.h"

#include <vector>

class Player;

namespace Moba
{
struct PlayerMatchAssignment
{
    uint32 MatchId = 0;
    uint32 TeamId = InvalidTeamId;
};

void ClearQueueStatus(Player* player, BattlegroundQueueTypeId queueId);
void ClearQueueStatuses(Player* player, BattlegroundQueueTypeId firstQueueId, BattlegroundQueueTypeId secondQueueId);

bool HasActiveMatchState(Player* player);
void QueueWaitingPlayer(Player* player);
std::vector<Player*> TakeWaitingPlayers(uint32 count);
std::vector<PlayerMatchAssignment> CreateMatch(std::vector<Player*> const& players, uint32 blueCount, uint32 instanceId, BattlegroundQueueTypeId queueId);
void SetPlayerMatchState(Player* player, MatchState state);
void MarkPlayerMatchInProgress(Player* player);
void AbandonPlayerMatch(Player* player);
}

#endif
