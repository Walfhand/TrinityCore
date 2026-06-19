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
class Creature;

namespace Moba
{
struct MobaStats
{
    uint32 BonusHealth = 0;
    uint32 AttackDamage = 0;
    uint32 SpellPower = 0;
    uint32 Armor = 0;
    uint32 MagicResist = 0;
};

struct MobaPlayerState
{
    uint32 MatchId = 0;
    uint32 InstanceId = 0;
    uint32 TeamId = InvalidTeamId;
    uint32 ArchetypeIndex = 0;
    uint32 Level = MobaStartLevel;
    uint32 Xp = 0;
    uint32 Gold = MobaStartGold;
    MobaStats Stats;
    MobaStats AppliedStats;
    bool ProgressInitialized = false;
};

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
MobaPlayerState* GetPlayerState(Player* player);
MobaPlayerState const* GetPlayerState(Player const* player);
void SetPlayerArchetype(Player* player, uint32 archetypeIndex);
void InitializePlayerMatchProgress(Player* player);
void RewardMinionKill(Player* killer, Creature* minion);
uint32 GetPlayerMobaLevel(Player const* player);
}

#endif
