/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef CUSTOM_MOBA_MATCH_MGR_H
#define CUSTOM_MOBA_MATCH_MGR_H

#include "Define.h"
#include "MobaRules.h"
#include "SharedDefines.h"

class Player;

namespace Moba
{
struct PlayerMatchAssignment
{
    uint32 MatchId = 0;
    uint32 TeamId = InvalidTeamId;
};

struct DuoMatchAssignments
{
    PlayerMatchAssignment First;
    PlayerMatchAssignment Second;
};

void ClearQueueStatus(Player* player, BattlegroundQueueTypeId queueId);
void ClearQueueStatuses(Player* player, BattlegroundQueueTypeId firstQueueId, BattlegroundQueueTypeId secondQueueId);

bool HasActiveMatchState(Player* player);
void QueueWaitingPlayer(Player* player);
Player* TakeWaitingOpponent(Player* player);
DuoMatchAssignments CreateDuoMatch(Player* firstPlayer, Player* secondPlayer, uint32 instanceId, BattlegroundQueueTypeId queueId);
void SetPlayerMatchState(Player* player, MatchState state);
void MarkPlayerMatchInProgress(Player* player);
void AbandonPlayerMatch(Player* player);
}

#endif
