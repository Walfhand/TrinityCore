/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_match_mgr.h"
#include "moba_shared.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "BattlegroundQueue.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

// Matchmaking and match lifecycle (queue -> invite -> create -> assign -> cleanup).
// Per-player progression (level/xp/gold/stats) lives in the game-lib subsystem
// (Moba::MobaProgression); this file only seeds and tears down that state.
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

    // Seed the (game-lib) progression state; it is fully reset at match start.
    MobaPlayerState& state = EnsurePlayerState(player);
    state.MatchId = matchId;
    state.InstanceId = instanceId;
    state.TeamId = teamId;
    state.ProgressInitialized = false;
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
        if (state == MatchState::InProgress)
            InitializePlayerMatchProgress(member);
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
    RemovePlayerProgress(player);
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
    {
        if (itr->second.QueueId != BATTLEGROUND_QUEUE_NONE && player->GetBattlegroundQueueIndex(itr->second.QueueId) >= PLAYER_MAX_BATTLEGROUND_QUEUES)
        {
            TC_LOG_INFO("scripts", "MOBA match: clearing stale waiting queue for player {}", player->GetName());
            ClearPlayerMatch(player, "stale waiting queue");
            return false;
        }

        return true;
    }

    if (player->InBattleground() || player->InBattlegroundQueue())
        return true;

    TC_LOG_INFO("scripts", "MOBA match: clearing stale state {} for player {}", GetMatchStateName(itr->second.State), player->GetName());
    ClearPlayerMatch(player, "stale state");
    return false;
}

bool QueueWaitingPlayer(Player* player, BattlegroundQueueTypeId queueId, PvPDifficultyEntry const* bracketEntry)
{
    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_MOBA);
    if (!bgTemplate)
    {
        TC_LOG_ERROR("scripts", "MOBA match: waiting queue failed, Guerilla template missing");
        player->GetSession()->SendNotification("Erreur prototype : template BG MOBA introuvable.");
        return false;
    }

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(queueId);
    GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bracketEntry, false, false, 0, 0);
    if (!ginfo)
    {
        TC_LOG_ERROR("scripts", "MOBA match: waiting queue AddGroup failed for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : entree en file refusee.");
        return false;
    }

    uint32 const queueSlot = player->AddBattlegroundQueueId(queueId);
    if (queueSlot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
    {
        TC_LOG_ERROR("scripts", "MOBA match: waiting queue has no free queue slot for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : aucune file disponible.");
        bgQueue.RemovePlayer(player->GetGUID(), false);
        return false;
    }

    uint64 const playerKey = GetPlayerKey(player);
    PlayerMatchRecord& playerRecord = PlayerMatches[playerKey];
    playerRecord.MatchId = 0;
    playerRecord.InstanceId = 0;
    playerRecord.TeamId = InvalidTeamId;
    playerRecord.QueueId = queueId;
    playerRecord.State = MatchState::Queued;

    if (std::find(WaitingPlayers.begin(), WaitingPlayers.end(), playerKey) == WaitingPlayers.end())
        WaitingPlayers.push_back(playerKey);

    WorldPackets::Battleground::BattlefieldStatusQueued queuedStatus;
    BattlegroundMgr::BuildBattlegroundStatusQueued(&queuedStatus, bgTemplate, queueSlot, ginfo->JoinTime, queueId, bgQueue.GetAverageQueueWaitTime(ginfo));
    player->SendDirectMessage(queuedStatus.Write());

    TC_LOG_INFO("scripts", "MOBA match: player {} queued for 1v1", player->GetName());
    return true;
}

std::vector<Player*> TakeWaitingPlayers(uint32 count)
{
    if (count == 0)
        return {};

    std::vector<Player*> ready;

    // Walk the queue once, dropping stale entries and collecting valid players in order.
    for (auto itr = WaitingPlayers.begin(); itr != WaitingPlayers.end();)
    {
        uint64 const waitingKey = *itr;
        auto recordItr = PlayerMatches.find(waitingKey);
        Player* waiting = FindOnlinePlayer(waitingKey);
        if (recordItr == PlayerMatches.end() || !IsWaitingRecord(recordItr->second) || !waiting || waiting->InBattleground())
        {
            itr = WaitingPlayers.erase(itr);

            if (waiting)
                ClearPlayerMatch(waiting, "stale waiting queue");
            else if (recordItr != PlayerMatches.end())
                PlayerMatches.erase(recordItr);

            continue;
        }

        if (recordItr->second.QueueId == BATTLEGROUND_QUEUE_NONE || waiting->GetBattlegroundQueueIndex(recordItr->second.QueueId) >= PLAYER_MAX_BATTLEGROUND_QUEUES)
        {
            itr = WaitingPlayers.erase(itr);
            ClearPlayerMatch(waiting, "stale waiting queue");
            continue;
        }

        ready.push_back(waiting);
        ++itr;
    }

    // Not enough players to fill a match: leave everyone in the queue.
    if (ready.size() < count)
        return {};

    ready.resize(count);
    for (Player* player : ready)
        WaitingPlayers.erase(std::remove(WaitingPlayers.begin(), WaitingPlayers.end(), GetPlayerKey(player)), WaitingPlayers.end());

    return ready;
}

std::vector<PlayerMatchAssignment> CreateMatch(std::vector<Player*> const& players, uint32 blueCount, uint32 instanceId, BattlegroundQueueTypeId queueId)
{
    uint32 const matchId = NextMatchId++;

    MatchRecord& match = Matches[matchId];
    match.MatchId = matchId;
    match.InstanceId = instanceId;

    std::vector<PlayerMatchAssignment> assignments;
    assignments.reserve(players.size());

    for (std::size_t i = 0; i < players.size(); ++i)
    {
        Player* player = players[i];
        uint32 const teamId = i < blueCount ? BlueTeamId : RedTeamId;

        GetRosterForTeam(match, teamId).push_back(GetPlayerKey(player));
        AssignPlayerToMatch(player, matchId, instanceId, teamId, queueId);
        assignments.push_back({ matchId, teamId });

        TC_LOG_INFO("scripts", "MOBA match: match {} instance {}: {} -> {}",
            matchId, instanceId, player->GetName(), teamId == BlueTeamId ? "blue" : "red");
    }

    return assignments;
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

            bg->SetStatus(STATUS_WAIT_JOIN);
            SetMatchRosterState(matchItr->second, MatchState::InProgress);
            return;
        }
    }

    SetPlayerMatchState(player, record, MatchState::InProgress);
    InitializePlayerMatchProgress(player);
}

void AbandonPlayerMatch(Player* player)
{
    ClearPlayerMatch(player, "player abandoned");
}
}
