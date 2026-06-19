/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "BattlegroundQueue.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <unordered_map>

namespace Moba
{
namespace
{
struct MatchRecord
{
    MatchState State = MatchState::None;
    uint32 InstanceId = 0;
    uint32 TeamId = InvalidTeamId;
    BattlegroundQueueTypeId QueueId = BATTLEGROUND_QUEUE_NONE;
};

std::unordered_map<uint64, MatchRecord> PlayerMatches;

uint64 GetPlayerKey(Player const* player)
{
    return player->GetGUID().GetCounter();
}

void SetMatchState(Player* player, MatchRecord& record, MatchState state)
{
    record.State = state;
    TC_LOG_INFO("scripts", "MOBA match: player {} state {}", player->GetName(), GetMatchStateName(state));
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

BattlegroundQueueTypeId GetPrototypeQueueTypeId(PvPDifficultyEntry const* bracketEntry)
{
    return BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_NA, bracketEntry->GetBracketId(), 0);
}

BattlegroundQueueTypeId GetArenaCleanupQueueTypeId(PvPDifficultyEntry const* bracketEntry)
{
    return BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_AA, bracketEntry->GetBracketId(), ARENA_TYPE_2v2);
}

void ClearQueueStatus(Player* player, BattlegroundQueueTypeId queueId)
{
    if (queueId == BATTLEGROUND_QUEUE_NONE)
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

void ClearPrototypeQueues(Player* player, PvPDifficultyEntry const* bracketEntry)
{
    ClearQueueStatus(player, GetPrototypeQueueTypeId(bracketEntry));
    ClearQueueStatus(player, GetArenaCleanupQueueTypeId(bracketEntry));
}

bool HasActiveMatchState(Player* player)
{
    auto itr = PlayerMatches.find(GetPlayerKey(player));
    if (itr == PlayerMatches.end())
        return false;

    if (player->InBattleground() || player->InBattlegroundQueue())
        return true;

    TC_LOG_INFO("scripts", "MOBA match: clearing stale state {} for player {}", GetMatchStateName(itr->second.State), player->GetName());
    PlayerMatches.erase(itr);
    return false;
}

void ClearPlayerMatch(Player* player, char const* reason)
{
    if (!player)
        return;

    auto itr = PlayerMatches.find(GetPlayerKey(player));
    if (itr == PlayerMatches.end())
        return;

    TC_LOG_INFO("scripts", "MOBA match: player {} cleared from state {} ({})", player->GetName(), GetMatchStateName(itr->second.State), reason);
    ClearQueueStatus(player, itr->second.QueueId);
    PlayerMatches.erase(itr);
}

bool InviteSoloTestMatch(Player* player, PvPDifficultyEntry const* bracketEntry)
{
    if (player->InBattleground())
    {
        player->GetSession()->SendNotification("Tu es deja en match.");
        return false;
    }

    ClearPrototypeQueues(player, bracketEntry);

    if (HasActiveMatchState(player))
    {
        player->GetSession()->SendNotification("Tu es deja en file ou en match MOBA.");
        return false;
    }

    if (player->InBattlegroundQueue())
    {
        player->GetSession()->SendNotification("Tu es deja en file.");
        return false;
    }

    MatchRecord& record = PlayerMatches[GetPlayerKey(player)];
    SetMatchState(player, record, MatchState::Queued);
    record.TeamId = SelectAutoTeam();
    TC_LOG_INFO("scripts", "MOBA match: player {} assigned to {} team", player->GetName(), record.TeamId == BlueTeamId ? "blue" : "red");

    ResetForMatch(player);

    Battleground* bg = sBattlegroundMgr->CreateNewBattleground(BATTLEGROUND_NA, bracketEntry, ARENA_TYPE_2v2, false);
    if (!bg)
    {
        TC_LOG_ERROR("scripts", "MOBA match: CreateNewBattleground failed for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : creation arene refusee.");
        ClearPlayerMatch(player, "battleground creation failed");
        return false;
    }

    record.InstanceId = bg->GetInstanceID();
    bg->StartBattleground();

    BattlegroundQueueTypeId const bgQueueTypeId = GetPrototypeQueueTypeId(bracketEntry);
    record.QueueId = bgQueueTypeId;
    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bracketEntry, false, false, 0, 0);
    if (!ginfo)
    {
        TC_LOG_ERROR("scripts", "MOBA match: AddGroup failed for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : entree en file refusee.");
        ClearPlayerMatch(player, "queue add failed");
        return false;
    }

    ginfo->Team = ::Team(record.TeamId);
    ginfo->IsInvitedToBGInstanceGUID = bg->GetInstanceID();
    ginfo->RemoveInviteTime = GameTime::GetGameTimeMS() + INVITE_ACCEPT_WAIT_TIME;

    uint32 const queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);
    if (queueSlot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
    {
        TC_LOG_ERROR("scripts", "MOBA match: no free queue slot for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : aucune file disponible.");
        bgQueue.RemovePlayer(player->GetGUID(), false);
        ClearPlayerMatch(player, "no queue slot");
        return false;
    }

    player->SetInviteForBattlegroundQueueType(bgQueueTypeId, bg->GetInstanceID());
    bg->IncreaseInvitedCount(record.TeamId);

    uint32 const avgTime = bgQueue.GetAverageQueueWaitTime(ginfo);
    WorldPackets::Battleground::BattlefieldStatusQueued queuedStatus;
    BattlegroundMgr::BuildBattlegroundStatusQueued(&queuedStatus, bg, queueSlot, ginfo->JoinTime, bgQueueTypeId, avgTime);
    player->SendDirectMessage(queuedStatus.Write());

    SetMatchState(player, record, MatchState::Invited);

    WorldPackets::Battleground::BattlefieldStatusNeedConfirmation battlefieldStatus;
    BattlegroundMgr::BuildBattlegroundStatusNeedConfirmation(&battlefieldStatus, bg, queueSlot, INVITE_ACCEPT_WAIT_TIME, bgQueueTypeId);
    player->SendDirectMessage(battlefieldStatus.Write());

    TC_LOG_INFO("scripts", "MOBA match: player {} invited to Nagrand Arena BG instance {}", player->GetName(), bg->GetInstanceID());
    player->GetSession()->SendNotification("Match trouve. Accepte la popup pour entrer.");
    return true;
}
}

void MarkPlayerMatchInProgress(Player* player)
{
    auto itr = PlayerMatches.find(GetPlayerKey(player));
    if (itr == PlayerMatches.end())
        return;

    if (!player->InBattleground())
        return;

    SetMatchState(player, itr->second, MatchState::InProgress);
}

void QueueSoloNexusTest(Player* player)
{
    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_NA);
    if (!bgTemplate)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: Nagrand Arena template introuvable");
        player->GetSession()->SendNotification("Erreur prototype : template arene introuvable.");
        return;
    }

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), player->GetLevel());
    if (!bracketEntry)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: no PvP bracket for player {} level {} map {}", player->GetName(), player->GetLevel(), bgTemplate->GetMapId());
        player->GetSession()->SendNotification("Erreur prototype : bracket PvP introuvable.");
        return;
    }

    InviteSoloTestMatch(player, bracketEntry);
}

void AbandonPlayerMatch(Player* player)
{
    ClearPlayerMatch(player, "player abandoned");
}

bool CompleteSoloNexusObjective(Player* /*player*/)
{
    return false;
}
}

class moba_match_player_script : public PlayerScript
{
public:
    moba_match_player_script() : PlayerScript("moba_match_player_script") { }

    void OnLogout(Player* player) override
    {
        Moba::AbandonPlayerMatch(player);
    }

    void OnMapChanged(Player* player) override
    {
        if (player->InBattleground())
        {
            Moba::MarkPlayerMatchInProgress(player);
            return;
        }

        Moba::AbandonPlayerMatch(player);
    }
};

void AddSC_moba_solo_match()
{
    new moba_match_player_script();
}
