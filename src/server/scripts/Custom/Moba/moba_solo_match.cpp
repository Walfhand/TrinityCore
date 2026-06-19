/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"
#include "moba_match_mgr.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "BattlegroundQueue.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <utility>
#include <vector>

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

bool InvitePlayerToMatch(Player* player, Battleground* bg, BattlegroundQueue& bgQueue, BattlegroundQueueTypeId bgQueueTypeId, PvPDifficultyEntry const* bracketEntry, PlayerMatchAssignment const& assignment)
{
    GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bracketEntry, false, false, 0, 0);
    if (!ginfo)
    {
        TC_LOG_ERROR("scripts", "MOBA match: AddGroup failed for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : entree en file refusee.");
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

    player->GetSession()->SendNotification("Match trouve. Accepte la popup pour entrer.");
    return true;
}

void AbandonMatchPlayers(std::vector<Player*> const& players)
{
    for (Player* player : players)
        AbandonPlayerMatch(player);
}

// Generic match launcher: the first `blueCount` players go Blue, the rest Red.
// A single-player match (Red empty) is the dev-solo case.
bool InviteMatch(std::vector<Player*> const& players, uint32 blueCount, PvPDifficultyEntry const* bracketEntry)
{
    if (players.empty())
        return false;

    for (Player* player : players)
        ResetForMatch(player);

    Battleground* bg = sBattlegroundMgr->CreateNewBattleground(BATTLEGROUND_NA, bracketEntry, ARENA_TYPE_2v2, false);
    if (!bg)
    {
        TC_LOG_ERROR("scripts", "MOBA match: CreateNewBattleground failed");
        for (Player* player : players)
            player->GetSession()->SendNotification("Erreur prototype : creation arene refusee.");
        AbandonMatchPlayers(players);
        return false;
    }

    // Nagrand is an arena, so CreateNewBattleground caps the team size from the
    // arena type. Override it so the configured MOBA team size fits the instance.
    uint32 const redCount = uint32(players.size()) - blueCount;
    uint32 const maxPerTeam = std::max({ blueCount, redCount, 1u });
    bg->SetMaxPlayersPerTeam(maxPerTeam);
    bg->SetMaxPlayers(maxPerTeam * 2);

    // Keep the arena in queue status while the native popup is pending.
    // Trinity refuses to leave an arena queue once its status is WAIT_JOIN+.
    bg->SetStatus(STATUS_WAIT_QUEUE);

    BattlegroundQueueTypeId const bgQueueTypeId = GetPrototypeQueueTypeId(bracketEntry);
    std::vector<PlayerMatchAssignment> const assignments = CreateMatch(players, blueCount, bg->GetInstanceID(), bgQueueTypeId);
    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

    for (std::size_t i = 0; i < players.size(); ++i)
    {
        if (!InvitePlayerToMatch(players[i], bg, bgQueue, bgQueueTypeId, bracketEntry, assignments[i]))
        {
            AbandonMatchPlayers(players);
            return false;
        }
    }

    // Register the BG before the client can accept the native popup. The port
    // handler looks up the instance by id before teleporting the player.
    bg->StartBattleground();

    TC_LOG_INFO("scripts", "MOBA match: {} players invited to Nagrand Arena BG instance {} (blue {} / red {})",
        players.size(), bg->GetInstanceID(), blueCount, redCount);
    return true;
}

bool CanQueuePrototypeMatch(Player* player, PvPDifficultyEntry const* bracketEntry)
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

    return true;
}

bool QueueMatchmaking(Player* player, PvPDifficultyEntry const* bracketEntry)
{
    if (!CanQueuePrototypeMatch(player, bracketEntry))
        return false;

    uint32 const teamSize = GetConfiguredTeamSize();

    QueueWaitingPlayer(player);

    std::vector<Player*> players = TakeWaitingPlayers(teamSize * 2);
    if (players.empty())
    {
        player->GetSession()->SendNotification("Tu es en file %uv%u. En attente d'autres joueurs.", teamSize, teamSize);
        return true;
    }

    // Shuffle so queue order does not decide team composition.
    for (std::size_t i = players.size(); i > 1; --i)
        std::swap(players[i - 1], players[urand(0, uint32(i - 1))]);

    return InviteMatch(players, teamSize, bracketEntry);
}

bool QueueDevSoloMatch(Player* player, PvPDifficultyEntry const* bracketEntry)
{
    if (!CanQueuePrototypeMatch(player, bracketEntry))
        return false;

    return InviteMatch({ player }, 1, bracketEntry);
}

PvPDifficultyEntry const* ResolvePrototypeBracket(Player* player)
{
    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_NA);
    if (!bgTemplate)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: Nagrand Arena template introuvable");
        player->GetSession()->SendNotification("Erreur prototype : template arene introuvable.");
        return nullptr;
    }

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), player->GetLevel());
    if (!bracketEntry)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: no PvP bracket for player {} level {} map {}", player->GetName(), player->GetLevel(), bgTemplate->GetMapId());
        player->GetSession()->SendNotification("Erreur prototype : bracket PvP introuvable.");
        return nullptr;
    }

    return bracketEntry;
}
}

bool IsDevSoloModeEnabled()
{
    return sConfigMgr->GetBoolDefault("Moba.DevSoloMode", false);
}

uint32 GetConfiguredTeamSize()
{
    int32 const configured = sConfigMgr->GetIntDefault("Moba.TeamSize", 1);
    if (configured < 1)
        return 1;
    if (configured > int32(MaxTeamSize))
        return MaxTeamSize;
    return uint32(configured);
}

void QueueMobaMatch(Player* player)
{
    if (PvPDifficultyEntry const* bracketEntry = ResolvePrototypeBracket(player))
        QueueMatchmaking(player, bracketEntry);
}

void QueueDevSoloTest(Player* player)
{
    if (!IsDevSoloModeEnabled())
    {
        player->GetSession()->SendNotification("Mode dev solo desactive (Moba.DevSoloMode).");
        return;
    }

    if (PvPDifficultyEntry const* bracketEntry = ResolvePrototypeBracket(player))
        QueueDevSoloMatch(player, bracketEntry);
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
