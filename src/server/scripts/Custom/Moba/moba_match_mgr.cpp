/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_match_mgr.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "BattlegroundQueue.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"

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

uint32 CountAssignedPlayers(uint32 teamId)
{
    uint32 count = 0;

    for (auto const& [_, record] : PlayerMatches)
        if (record.TeamId == teamId && record.State != MatchState::None && record.State != MatchState::Finished)
            ++count;

    return count;
}

uint32 SelectAutoTeam()
{
    uint32 const blueCount = CountAssignedPlayers(BlueTeamId);
    uint32 const redCount = CountAssignedPlayers(RedTeamId);

    if (blueCount == redCount)
        return urand(0, 1) == 0 ? BlueTeamId : RedTeamId;

    if (blueCount < redCount)
        return BlueTeamId;

    return RedTeamId;
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

    if (player->InBattleground() || player->InBattlegroundQueue())
        return true;

    TC_LOG_INFO("scripts", "MOBA match: clearing stale state {} for player {}", GetMatchStateName(itr->second.State), player->GetName());
    ClearPlayerMatch(player, "stale state");
    return false;
}

PlayerMatchAssignment CreateSoloMatch(Player* player, uint32 instanceId, BattlegroundQueueTypeId queueId)
{
    uint64 const playerKey = GetPlayerKey(player);
    uint32 const matchId = NextMatchId++;
    uint32 const teamId = SelectAutoTeam();

    MatchRecord& match = Matches[matchId];
    match.MatchId = matchId;
    match.InstanceId = instanceId;
    GetRosterForTeam(match, teamId).push_back(playerKey);

    PlayerMatchRecord& playerRecord = PlayerMatches[playerKey];
    playerRecord.MatchId = matchId;
    playerRecord.InstanceId = instanceId;
    playerRecord.TeamId = teamId;
    playerRecord.QueueId = queueId;
    SetPlayerMatchState(player, playerRecord, MatchState::Queued);

    TC_LOG_INFO("scripts", "MOBA match: player {} assigned to {} team in match {}", player->GetName(), teamId == BlueTeamId ? "blue" : "red", matchId);
    return { matchId, teamId };
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

    if (Battleground* bg = player->GetBattleground())
        if (bg->GetStatus() == STATUS_WAIT_QUEUE)
            bg->StartBattleground();

    SetPlayerMatchState(player, MatchState::InProgress);
}

void AbandonPlayerMatch(Player* player)
{
    ClearPlayerMatch(player, "player abandoned");
}
}
