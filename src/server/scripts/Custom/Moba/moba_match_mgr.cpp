/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_match_mgr.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "BattlegroundQueue.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "WorldSession.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Moba
{
namespace
{
struct PlayerMatchRecord
{
    MatchState State = MatchState::None;
    uint32 MatchId = 0;
    uint32 InstanceId = 0;
    uint32 TeamId = InvalidTeamId;
    BattlegroundQueueTypeId QueueId = BATTLEGROUND_QUEUE_NONE;
};

struct MatchRecord
{
    MatchState State = MatchState::Queued;
    uint32 MatchId = 0;
    uint32 InstanceId = 0;
    std::vector<uint64> BlueRoster;
    std::vector<uint64> RedRoster;
};

std::unordered_map<uint64, PlayerMatchRecord> PlayerMatches;
std::unordered_map<uint32, MatchRecord> Matches;
std::vector<uint64> WaitingPlayers;
uint32 NextMatchId = 1;

uint64 GetPlayerKey(Player const* player)
{
    return player->GetGUID().GetCounter();
}

std::vector<uint64>& GetRosterForTeam(MatchRecord& match, uint32 teamId)
{
    return teamId == BlueTeamId ? match.BlueRoster : match.RedRoster;
}

void RemovePlayerFromMatch(uint64 playerKey, PlayerMatchRecord const& playerRecord)
{
    WaitingPlayers.erase(std::remove(WaitingPlayers.begin(), WaitingPlayers.end(), playerKey), WaitingPlayers.end());

    auto matchItr = Matches.find(playerRecord.MatchId);
    if (matchItr == Matches.end())
        return;

    MatchRecord& match = matchItr->second;
    std::vector<uint64>& roster = GetRosterForTeam(match, playerRecord.TeamId);
    roster.erase(std::remove(roster.begin(), roster.end(), playerKey), roster.end());

    if (match.BlueRoster.empty() && match.RedRoster.empty())
        Matches.erase(matchItr);
}

void SetPlayerMatchState(Player* player, PlayerMatchRecord& record, MatchState state)
{
    record.State = state;

    if (auto matchItr = Matches.find(record.MatchId); matchItr != Matches.end())
        matchItr->second.State = state;

    TC_LOG_INFO("scripts", "MOBA match: player {} match {} state {}", player->GetName(), record.MatchId, GetMatchStateName(state));
}

bool IsWaitingRecord(PlayerMatchRecord const& record)
{
    return record.MatchId == 0 && record.State == MatchState::Queued;
}

Player* FindOnlinePlayer(uint64 playerKey)
{
    return ObjectAccessor::FindPlayerByLowGUID(ObjectGuid::LowType(playerKey));
}

void AssignPlayerToMatch(Player* player, uint32 matchId, uint32 instanceId, uint32 teamId, BattlegroundQueueTypeId queueId)
{
    uint64 const playerKey = GetPlayerKey(player);
    PlayerMatchRecord& playerRecord = PlayerMatches[playerKey];
    playerRecord.MatchId = matchId;
    playerRecord.InstanceId = instanceId;
    playerRecord.TeamId = teamId;
    playerRecord.QueueId = queueId;
    SetPlayerMatchState(player, playerRecord, MatchState::Queued);
}

bool IsMatchReadyToStart(MatchRecord const& match)
{
    std::vector<uint64> roster;
    roster.insert(roster.end(), match.BlueRoster.begin(), match.BlueRoster.end());
    roster.insert(roster.end(), match.RedRoster.begin(), match.RedRoster.end());

    if (roster.empty())
        return false;

    for (uint64 playerKey : roster)
    {
        Player* member = FindOnlinePlayer(playerKey);
        if (!member || !member->InBattleground())
            return false;

        Battleground* bg = member->GetBattleground();
        if (!bg || bg->GetInstanceID() != match.InstanceId)
            return false;
    }

    return true;
}

void SetMatchRosterState(MatchRecord const& match, MatchState state)
{
    std::vector<uint64> roster;
    roster.insert(roster.end(), match.BlueRoster.begin(), match.BlueRoster.end());
    roster.insert(roster.end(), match.RedRoster.begin(), match.RedRoster.end());

    for (uint64 playerKey : roster)
    {
        Player* member = FindOnlinePlayer(playerKey);
        auto recordItr = PlayerMatches.find(playerKey);
        if (!member || recordItr == PlayerMatches.end())
            continue;

        SetPlayerMatchState(member, recordItr->second, state);
    }
}

void ClearPlayerMatch(Player* player, char const* reason)
{
    if (!player)
        return;

    uint64 const playerKey = GetPlayerKey(player);
    auto itr = PlayerMatches.find(playerKey);
    if (itr == PlayerMatches.end())
        return;

    PlayerMatchRecord const record = itr->second;
    TC_LOG_INFO("scripts", "MOBA match: player {} cleared from match {} state {} ({})", player->GetName(), record.MatchId, GetMatchStateName(record.State), reason);
    ClearQueueStatus(player, record.QueueId);
    RemovePlayerFromMatch(playerKey, record);
    PlayerMatches.erase(itr);
}
}

void ClearQueueStatus(Player* player, BattlegroundQueueTypeId queueId)
{
    if (!player || queueId == BATTLEGROUND_QUEUE_NONE)
        return;

    uint32 const queueSlot = player->GetBattlegroundQueueIndex(queueId);
    if (queueSlot < PLAYER_MAX_BATTLEGROUND_QUEUES)
    {
        WorldPackets::Battleground::BattlefieldStatusNone battlefieldStatus;
        BattlegroundMgr::BuildBattlegroundStatusNone(&battlefieldStatus, queueSlot);
        player->SendDirectMessage(battlefieldStatus.Write());
    }

    BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(queueId);
    GroupQueueInfo ginfo;
    if (queue.GetPlayerGroupInfoData(player->GetGUID(), &ginfo))
        queue.RemovePlayer(player->GetGUID(), true);

    player->RemoveBattlegroundQueueId(queueId);
}

void ClearQueueStatuses(Player* player, BattlegroundQueueTypeId firstQueueId, BattlegroundQueueTypeId secondQueueId)
{
    ClearQueueStatus(player, firstQueueId);
    ClearQueueStatus(player, secondQueueId);
}

bool HasActiveMatchState(Player* player)
{
    auto itr = PlayerMatches.find(GetPlayerKey(player));
    if (itr == PlayerMatches.end())
        return false;

    if (IsWaitingRecord(itr->second))
        return true;

    if (player->InBattleground() || player->InBattlegroundQueue())
        return true;

    TC_LOG_INFO("scripts", "MOBA match: clearing stale state {} for player {}", GetMatchStateName(itr->second.State), player->GetName());
    ClearPlayerMatch(player, "stale state");
    return false;
}

void QueueWaitingPlayer(Player* player)
{
    uint64 const playerKey = GetPlayerKey(player);
    PlayerMatchRecord& playerRecord = PlayerMatches[playerKey];
    playerRecord.MatchId = 0;
    playerRecord.InstanceId = 0;
    playerRecord.TeamId = InvalidTeamId;
    playerRecord.QueueId = BATTLEGROUND_QUEUE_NONE;
    playerRecord.State = MatchState::Queued;

    if (std::find(WaitingPlayers.begin(), WaitingPlayers.end(), playerKey) == WaitingPlayers.end())
        WaitingPlayers.push_back(playerKey);

    TC_LOG_INFO("scripts", "MOBA match: player {} queued for 1v1", player->GetName());
}

Player* TakeWaitingOpponent(Player* player)
{
    uint64 const playerKey = GetPlayerKey(player);

    for (auto itr = WaitingPlayers.begin(); itr != WaitingPlayers.end();)
    {
        uint64 const opponentKey = *itr;
        if (opponentKey == playerKey)
        {
            itr = WaitingPlayers.erase(itr);
            continue;
        }

        auto recordItr = PlayerMatches.find(opponentKey);
        Player* opponent = FindOnlinePlayer(opponentKey);
        if (recordItr == PlayerMatches.end() || !IsWaitingRecord(recordItr->second) || !opponent || opponent->InBattleground() || opponent->InBattlegroundQueue())
        {
            if (opponent)
                ClearPlayerMatch(opponent, "stale waiting queue");
            else if (recordItr != PlayerMatches.end())
                PlayerMatches.erase(recordItr);

            itr = WaitingPlayers.erase(itr);
            continue;
        }

        WaitingPlayers.erase(itr);
        return opponent;
    }

    return nullptr;
}

DuoMatchAssignments CreateDuoMatch(Player* firstPlayer, Player* secondPlayer, uint32 instanceId, BattlegroundQueueTypeId queueId)
{
    uint32 const matchId = NextMatchId++;
    uint32 const firstTeamId = urand(0, 1) == 0 ? BlueTeamId : RedTeamId;
    uint32 const secondTeamId = firstTeamId == BlueTeamId ? RedTeamId : BlueTeamId;

    uint64 const firstKey = GetPlayerKey(firstPlayer);
    uint64 const secondKey = GetPlayerKey(secondPlayer);
    WaitingPlayers.erase(std::remove(WaitingPlayers.begin(), WaitingPlayers.end(), firstKey), WaitingPlayers.end());
    WaitingPlayers.erase(std::remove(WaitingPlayers.begin(), WaitingPlayers.end(), secondKey), WaitingPlayers.end());

    MatchRecord& match = Matches[matchId];
    match.MatchId = matchId;
    match.InstanceId = instanceId;
    GetRosterForTeam(match, firstTeamId).push_back(firstKey);
    GetRosterForTeam(match, secondTeamId).push_back(secondKey);

    AssignPlayerToMatch(firstPlayer, matchId, instanceId, firstTeamId, queueId);
    AssignPlayerToMatch(secondPlayer, matchId, instanceId, secondTeamId, queueId);

    TC_LOG_INFO("scripts", "MOBA match: created 1v1 match {} instance {}: {}={}, {}={}",
        matchId, instanceId,
        firstPlayer->GetName(), firstTeamId == BlueTeamId ? "blue" : "red",
        secondPlayer->GetName(), secondTeamId == BlueTeamId ? "blue" : "red");

    return { { matchId, firstTeamId }, { matchId, secondTeamId } };
}

void SetPlayerMatchState(Player* player, MatchState state)
{
    auto itr = PlayerMatches.find(GetPlayerKey(player));
    if (itr == PlayerMatches.end())
        return;

    SetPlayerMatchState(player, itr->second, state);
}

void MarkPlayerMatchInProgress(Player* player)
{
    if (!player->InBattleground())
        return;

    auto itr = PlayerMatches.find(GetPlayerKey(player));
    if (itr == PlayerMatches.end())
        return;

    PlayerMatchRecord& record = itr->second;
    auto matchItr = Matches.find(record.MatchId);
    if (matchItr == Matches.end())
        return;

    if (Battleground* bg = player->GetBattleground())
    {
        if (bg->GetStatus() == STATUS_WAIT_QUEUE)
        {
            SetPlayerMatchState(player, record, MatchState::Preparing);

            if (!IsMatchReadyToStart(matchItr->second))
            {
                player->GetSession()->SendNotification("En attente de l'autre joueur...");
                return;
            }

            bg->StartBattleground();
            SetMatchRosterState(matchItr->second, MatchState::InProgress);
            return;
        }
    }

    SetPlayerMatchState(player, record, MatchState::InProgress);
}

void AbandonPlayerMatch(Player* player)
{
    ClearPlayerMatch(player, "player abandoned");
}
}
