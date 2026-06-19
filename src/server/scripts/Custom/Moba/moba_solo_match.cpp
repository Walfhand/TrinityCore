/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"
#include "moba_match_mgr.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "BattlegroundQueue.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

namespace Moba
{
namespace
{
BattlegroundQueueTypeId GetPrototypeQueueTypeId(PvPDifficultyEntry const* bracketEntry)
{
    return BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_NA, bracketEntry->GetBracketId(), 0);
}

BattlegroundQueueTypeId GetArenaCleanupQueueTypeId(PvPDifficultyEntry const* bracketEntry)
{
    return BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_AA, bracketEntry->GetBracketId(), ARENA_TYPE_2v2);
}

void ClearPrototypeQueues(Player* player, PvPDifficultyEntry const* bracketEntry)
{
    ClearQueueStatuses(player, GetPrototypeQueueTypeId(bracketEntry), GetArenaCleanupQueueTypeId(bracketEntry));
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

    ResetForMatch(player);

    Battleground* bg = sBattlegroundMgr->CreateNewBattleground(BATTLEGROUND_NA, bracketEntry, ARENA_TYPE_2v2, false);
    if (!bg)
    {
        TC_LOG_ERROR("scripts", "MOBA match: CreateNewBattleground failed for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : creation arene refusee.");
        return false;
    }

    // Keep the arena in queue status while the native popup is pending.
    // Trinity refuses to leave an arena queue once its status is WAIT_JOIN+.
    bg->SetStatus(STATUS_WAIT_QUEUE);

    BattlegroundQueueTypeId const bgQueueTypeId = GetPrototypeQueueTypeId(bracketEntry);
    PlayerMatchAssignment const assignment = CreateSoloMatch(player, bg->GetInstanceID(), bgQueueTypeId);
    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bracketEntry, false, false, 0, 0);
    if (!ginfo)
    {
        TC_LOG_ERROR("scripts", "MOBA match: AddGroup failed for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : entree en file refusee.");
        AbandonPlayerMatch(player);
        return false;
    }

    ginfo->Team = ::Team(assignment.TeamId);
    ginfo->IsInvitedToBGInstanceGUID = bg->GetInstanceID();
    ginfo->RemoveInviteTime = GameTime::GetGameTimeMS() + INVITE_ACCEPT_WAIT_TIME;

    uint32 const queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);
    if (queueSlot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
    {
        TC_LOG_ERROR("scripts", "MOBA match: no free queue slot for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : aucune file disponible.");
        bgQueue.RemovePlayer(player->GetGUID(), false);
        AbandonPlayerMatch(player);
        return false;
    }

    player->SetInviteForBattlegroundQueueType(bgQueueTypeId, bg->GetInstanceID());
    bg->IncreaseInvitedCount(assignment.TeamId);

    uint32 const avgTime = bgQueue.GetAverageQueueWaitTime(ginfo);
    WorldPackets::Battleground::BattlefieldStatusQueued queuedStatus;
    BattlegroundMgr::BuildBattlegroundStatusQueued(&queuedStatus, bg, queueSlot, ginfo->JoinTime, bgQueueTypeId, avgTime);
    player->SendDirectMessage(queuedStatus.Write());

    SetPlayerMatchState(player, MatchState::Invited);

    WorldPackets::Battleground::BattlefieldStatusNeedConfirmation battlefieldStatus;
    BattlegroundMgr::BuildBattlegroundStatusNeedConfirmation(&battlefieldStatus, bg, queueSlot, INVITE_ACCEPT_WAIT_TIME, bgQueueTypeId);
    player->SendDirectMessage(battlefieldStatus.Write());

    TC_LOG_INFO("scripts", "MOBA match: player {} invited to Nagrand Arena BG instance {}", player->GetName(), bg->GetInstanceID());
    player->GetSession()->SendNotification("Match trouve. Accepte la popup pour entrer.");
    return true;
}
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
