/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"
#include "moba_match_mgr.h"

#include "MobaArchetypes.h"
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
#include "MobaSorcier.h"
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

void ClearPrototypeQueues(Player* player, PvPDifficultyEntry const* bracketEntry)
{
    ClearQueueStatus(player, GetPrototypeQueueTypeId(bracketEntry));
}

bool InvitePlayerToMatch(Player* player, Battleground* bg, BattlegroundQueue& bgQueue, BattlegroundQueueTypeId bgQueueTypeId, PvPDifficultyEntry const* bracketEntry, PlayerMatchAssignment const& assignment)
{
    GroupQueueInfo* ginfo = nullptr;
    auto queuedPlayerItr = bgQueue.m_QueuedPlayers.find(player->GetGUID());
    if (queuedPlayerItr != bgQueue.m_QueuedPlayers.end())
        ginfo = queuedPlayerItr->second.GroupInfo;

    if (!ginfo)
        ginfo = bgQueue.AddGroup(player, nullptr, bracketEntry, false, false, 0, 0);

    if (!ginfo)
    {
        TC_LOG_ERROR("scripts", "MOBA match: AddGroup failed for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : entree en file refusee.");
        return false;
    }

    // Ensure the player owns a battleground queue slot for this queue id. The slot can be MISSING even when
    // the queue group exists (a previous match can leave the player's two slots occupied/desynced from the
    // queue groups). This is the real cause of the re-tag failure: the old code only added a slot in the
    // "group not found" branch, so a group-found-but-slot-missing state hit "no free queue slot".
    uint32 queueSlot = player->GetBattlegroundQueueIndex(bgQueueTypeId);
    if (queueSlot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
        queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);
    if (queueSlot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
    {
        // Both slots occupied (a stale queue id from a previous match). Release them and retry once.
        ClearAllQueueSlots(player);
        queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);
    }

    if (queueSlot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
    {
        TC_LOG_ERROR("scripts", "MOBA match: no free queue slot for {}", player->GetName());
        player->GetSession()->SendNotification("Erreur prototype : aucune file disponible.");
        bgQueue.RemovePlayer(player->GetGUID(), false);
        return false;
    }

    ginfo->Team = ::Team(assignment.TeamId);
    ginfo->IsInvitedToBGInstanceGUID = bg->GetInstanceID();
    ginfo->RemoveInviteTime = GameTime::GetGameTimeMS() + INVITE_ACCEPT_WAIT_TIME;

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

    BattlegroundQueueTypeId const bgQueueTypeId = GetPrototypeQueueTypeId(bracketEntry);
    if (!QueueWaitingPlayer(player, bgQueueTypeId, bracketEntry))
        return false;

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
        Moba::RevertToBlank(player);   // save a blank character: no kit visible on a later reconnect
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        // Reconnect into a running match: re-apply MOBA stats and re-sync the client (XP bar,
        // money) since the reloaded Player object lost the in-memory bonuses.
        if (player->InBattleground())
        {
            Moba::ReapplyPlayerMatchState(player);
        }
        else
        {
            // Logging in outside a match = blank shell (no class-specific kit). Do NOT teleport here:
            // a teleport issued during login leaves the client in a half-loaded "being teleported" limbo
            // (no collision, unstreamed creatures/NPCs, no aggro). Instead request a guaranteed deferred
            // placement: the lobby fence (world update, runs once the player is fully in world) teleports
            // them to the faire as soon as it is safe, retrying until it takes (robust after char creation).
            Moba::RevertToBlank(player);
            Moba::RequestLobbyPlacement(player);
        }
    }

    void OnMapChanged(Player* player) override
    {
        if (player->InBattleground())
        {
            Moba::MarkPlayerMatchInProgress(player);
            Moba::ReapplyPlayerMatchState(player);
            return;
        }

        Moba::AbandonPlayerMatch(player);
        Moba::RevertToBlank(player);   // left the match -> strip the kit, back to a blank character
        // No teleport here: OnMapChanged also fires on login (it runs on every map enter), and teleporting
        // during the map-enter leaves the client in a half-loaded limbo. Request a guaranteed deferred
        // placement instead; the lobby fence (world update) puts the player on the faire grounds cleanly.
        Moba::RequestLobbyPlacement(player);
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
        Moba::EnforceLobbyFence(diff);   // leash out-of-match players to the faire grounds
        Moba::Sorcier::Update(diff);
    }
};

// Tracks the last enemy champion to damage a champion, so a kill is still credited to that champion
// when a minion or tower lands the finishing blow (LoL-style kill credit).
class moba_combat_tracker : public UnitScript
{
public:
    moba_combat_tracker() : UnitScript("moba_combat_tracker") { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (damage)
            Moba::NoteChampionDamage(attacker, victim);
    }

    // Champions can only damage structures (towers/nexus) with auto-attacks, not spells: a long-range
    // spell would otherwise out-range the tower and poke it safely. Auto-attacks use ModifyMeleeDamage
    // (untouched); minion spells are not from a champion, so they still hit.
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage) override
    {
        if (!target || damage <= 0)
            return;

        uint32 const entry = target->GetEntry();
        if (!Moba::IsTowerEntry(entry) && !Moba::IsNexusEntry(entry))
            return;

        Player* champ = attacker ? attacker->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (champ && champ->InBattleground())
            damage = 0;
    }
};

namespace
{
// Called by the core battlemaster-join hook (via the MobaQueue seam) when a player queues for
// the MOBA battleground from the standard BG list. Routes into our custom matchmaking instead
// of the native queue: instant pop in dev-solo mode, otherwise normal 1v1+ matchmaking.
void DispatchMobaBattlemasterJoin(Player* player, uint32 battlemasterListId)
{
    if (player->InBattleground())
    {
        player->GetSession()->SendNotification("Tu es deja en match.");
        return;
    }

    if (Moba::HasActiveMatchState(player) || player->InBattlegroundQueue())
    {
        player->GetSession()->SendNotification("Tu es deja en file MOBA. Quitte la file avant de changer d'archetype.");
        return;
    }

    uint32 const archetypeIndex = Moba::GetArchetypeIndexForBattlemasterListId(battlemasterListId);
    // Record the archetype from the queue alias; the kit is applied on match entry, not now (stay blank).
    Moba::SetPlayerArchetype(player, archetypeIndex);

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
    new moba_combat_tracker();
    Moba::SetBattlemasterJoinHandler(&DispatchMobaBattlemasterJoin);
}
