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
#include "MobaMapConfig.h"
#include "MobaQueue.h"
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
    return BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_MOBA, bracketEntry->GetBracketId(), 0);
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

    Battleground* bg = sBattlegroundMgr->CreateNewBattleground(BATTLEGROUND_MOBA, bracketEntry, 0, false);
    if (!bg)
    {
        TC_LOG_ERROR("scripts", "MOBA match: CreateNewBattleground failed");
        for (Player* player : players)
            player->GetSession()->SendNotification("Erreur prototype : creation arene refusee.");
        AbandonMatchPlayers(players);
        return false;
    }

    uint32 const redCount = uint32(players.size()) - blueCount;
    uint32 const maxPerTeam = std::max({ blueCount, redCount, 1u });
    bg->SetMaxPlayersPerTeam(maxPerTeam);
    bg->SetMaxPlayers(maxPerTeam * 2);

    // Disable the native "not enough players, closing in X min" premature finish: a MOBA
    // match (especially dev-solo) legitimately has an empty/short team. Win is nexus-only.
    bg->SetMinPlayersPerTeam(0);
    bg->SetMinPlayers(0);

    // Port each team to its base (data-driven, loaded from the world DB by map id).
    if (Moba::MapLayout const* map = Moba::GetMobaMapLayout(bg->GetMapId()))
    {
        bg->SetTeamStartPosition(TEAM_ALLIANCE, map->BlueBase);
        bg->SetTeamStartPosition(TEAM_HORDE, map->RedBase);
    }

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

    TC_LOG_INFO("scripts", "MOBA match: {} players invited to Guerilla BG instance {} (blue {} / red {})",
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
    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_MOBA);
    if (!bgTemplate)
    {
        TC_LOG_ERROR("scripts", "MOBA solo: Guerilla battleground template introuvable");
        player->GetSession()->SendNotification("Erreur prototype : template BG MOBA introuvable.");
        return nullptr;
    }

    // Resolve the battleground bracket from the player's real WoW level. The core BG port handler
    // does the same on accept, so both must agree (champions enter at PrototypeLevel >= 10).
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
        // In an active match: keep the MOBA state so a reconnect resumes it (the BG keeps the
        // player offline meanwhile). Only abandon while still in queue/lobby.
        if (player->InBattleground())
            return;

        Moba::AbandonPlayerMatch(player);
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        // Reconnect into a running match: re-apply MOBA stats and re-sync the client (XP bar,
        // money) since the reloaded Player object lost the in-memory bonuses.
        if (player->InBattleground())
            Moba::ReapplyPlayerMatchState(player);
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

class moba_passive_gold_world : public WorldScript
{
public:
    moba_passive_gold_world() : WorldScript("moba_passive_gold_world") { }

    void OnUpdate(uint32 diff) override
    {
        Moba::UpdatePassiveGold(diff);
        Moba::UpdateRespawns(diff);
    }
};

namespace
{
// Called by the core battlemaster-join hook (via the MobaQueue seam) when a player queues for
// the MOBA battleground from the standard BG list. Routes into our custom matchmaking instead
// of the native queue: instant pop in dev-solo mode, otherwise normal 1v1+ matchmaking.
void DispatchMobaBattlemasterJoin(Player* player)
{
    if (Moba::IsDevSoloModeEnabled())
        Moba::QueueDevSoloTest(player);
    else
        Moba::QueueMobaMatch(player);
}
}

void AddSC_moba_solo_match()
{
    new moba_match_player_script();
    new moba_passive_gold_world();
    Moba::SetBattlemasterJoinHandler(&DispatchMobaBattlemasterJoin);
}
