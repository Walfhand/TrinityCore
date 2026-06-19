/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_match_mgr.h"
#include "moba_shared.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "BattlegroundQueue.h"
#include "Creature.h"
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
std::unordered_map<uint64, MobaPlayerState> PlayerStates;
std::unordered_map<uint64, uint32> SelectedArchetypes;
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

uint32 GetXpForNextLevel(uint32 level)
{
    // Gentle early curve for prototype games: first wave should matter, but not
    // instantly snowball into several levels.
    return 80 + level * 45;
}

void RecalculateStats(MobaPlayerState& state)
{
    uint32 const levelBonus = state.Level > 1 ? state.Level - 1 : 0;
    state.Stats.BonusHealth = 180 + levelBonus * 45;
    state.Stats.AttackDamage = 8 + levelBonus * 3;
    state.Stats.SpellPower = 8 + levelBonus * 4;
    state.Stats.Armor = 12 + levelBonus * 2;
    state.Stats.MagicResist = 8 + levelBonus * 2;
}

void ApplyStateStats(Player* player, MobaPlayerState& state)
{
    int32 const healthDelta = int32(state.Stats.BonusHealth) - int32(state.AppliedStats.BonusHealth);
    if (healthDelta)
    {
        player->SetMaxHealth(std::max<int32>(1, int32(player->GetMaxHealth()) + healthDelta));
        player->SetHealth(std::min<uint32>(player->GetMaxHealth(), uint32(std::max<int32>(1, int32(player->GetHealth()) + healthDelta))));
    }

    int32 const armorDelta = int32(state.Stats.Armor) - int32(state.AppliedStats.Armor);
    if (armorDelta)
        player->SetArmor(player->GetArmor() + armorDelta);

    int32 const magicResistDelta = int32(state.Stats.MagicResist) - int32(state.AppliedStats.MagicResist);
    for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
        if (magicResistDelta)
            player->SetResistance(SpellSchools(school), player->GetResistance(SpellSchools(school)) + magicResistDelta);

    state.AppliedStats = state.Stats;
}

void RemoveAppliedStateStats(Player* player, MobaPlayerState& state)
{
    if (state.AppliedStats.BonusHealth)
    {
        int32 const healthDelta = -int32(state.AppliedStats.BonusHealth);
        player->SetMaxHealth(std::max<int32>(1, int32(player->GetMaxHealth()) + healthDelta));
        player->SetHealth(std::min<uint32>(player->GetMaxHealth(), player->GetHealth()));
    }

    if (state.AppliedStats.Armor)
        player->SetArmor(player->GetArmor() - int32(state.AppliedStats.Armor));

    if (state.AppliedStats.MagicResist)
        for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
            player->SetResistance(SpellSchools(school), player->GetResistance(SpellSchools(school)) - int32(state.AppliedStats.MagicResist));

    state.AppliedStats = {};
}

void NotifyState(Player* player, MobaPlayerState const& state)
{
    player->GetSession()->SendNotification("MOBA: niveau %u, XP %u/%u, gold %u.",
        state.Level, state.Xp, state.Level < MobaMaxLevel ? GetXpForNextLevel(state.Level) : 0, state.Gold);
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

    MobaPlayerState& state = PlayerStates[playerKey];
    state.MatchId = matchId;
    state.InstanceId = instanceId;
    state.TeamId = teamId;
    state.ArchetypeIndex = SelectedArchetypes.count(playerKey) ? SelectedArchetypes[playerKey] : 0;
    state.Level = MobaStartLevel;
    state.Xp = 0;
    state.Gold = MobaStartGold;
    state.AppliedStats = {};
    state.ProgressInitialized = false;
    RecalculateStats(state);
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
    if (auto stateItr = PlayerStates.find(playerKey); stateItr != PlayerStates.end())
        RemoveAppliedStateStats(player, stateItr->second);
    RemovePlayerFromMatch(playerKey, record);
    PlayerMatches.erase(itr);
    PlayerStates.erase(playerKey);
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
        if (recordItr == PlayerMatches.end() || !IsWaitingRecord(recordItr->second) || !waiting || waiting->InBattleground() || waiting->InBattlegroundQueue())
        {
            itr = WaitingPlayers.erase(itr);

            if (waiting)
                ClearPlayerMatch(waiting, "stale waiting queue");
            else if (recordItr != PlayerMatches.end())
                PlayerMatches.erase(recordItr);

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

MobaPlayerState* GetPlayerState(Player* player)
{
    if (!player)
        return nullptr;

    auto itr = PlayerStates.find(GetPlayerKey(player));
    if (itr == PlayerStates.end())
        return nullptr;

    return &itr->second;
}

MobaPlayerState const* GetPlayerState(Player const* player)
{
    if (!player)
        return nullptr;

    auto itr = PlayerStates.find(GetPlayerKey(player));
    if (itr == PlayerStates.end())
        return nullptr;

    return &itr->second;
}

void SetPlayerArchetype(Player* player, uint32 archetypeIndex)
{
    if (!player)
        return;

    SelectedArchetypes[GetPlayerKey(player)] = archetypeIndex;

    if (MobaPlayerState* state = GetPlayerState(player))
        state->ArchetypeIndex = archetypeIndex;
}

void InitializePlayerMatchProgress(Player* player)
{
    MobaPlayerState* state = GetPlayerState(player);
    if (!state)
        return;

    if (state->ProgressInitialized)
        return;

    state->Level = MobaStartLevel;
    state->Xp = 0;
    state->Gold = MobaStartGold;
    RecalculateStats(*state);
    ApplyStateStats(player, *state);
    UpdateArchetypeSpells(player, state->ArchetypeIndex, state->Level, false);
    state->ProgressInitialized = true;
    NotifyState(player, *state);
}

void RewardMinionKill(Player* killer, Creature* minion)
{
    if (!killer || !minion || !killer->InBattleground() || !IsMinionEntry(minion->GetEntry()))
        return;

    MobaPlayerState* state = GetPlayerState(killer);
    if (!state)
        return;

    uint32 gold = MobaMeleeMinionGold;
    uint32 xp = MobaMeleeMinionXp;

    switch (GetMinionType(minion->GetEntry()))
    {
        case MinionType::Caster:
            gold = MobaCasterMinionGold;
            xp = MobaCasterMinionXp;
            break;
        case MinionType::Siege:
            gold = MobaSiegeMinionGold;
            xp = MobaSiegeMinionXp;
            break;
        case MinionType::Melee:
        default:
            break;
    }

    state->Gold += gold;

    if (state->Level < MobaMaxLevel)
    {
        state->Xp += xp;
        while (state->Level < MobaMaxLevel)
        {
            uint32 const next = GetXpForNextLevel(state->Level);
            if (state->Xp < next)
                break;

            state->Xp -= next;
            ++state->Level;
            RecalculateStats(*state);
            ApplyStateStats(killer, *state);
            UpdateArchetypeSpells(killer, state->ArchetypeIndex, state->Level, true);
            killer->GetSession()->SendNotification("Niveau MOBA %u atteint.", state->Level);
        }
    }

    killer->GetSession()->SendNotification("+%u gold, +%u XP. Total: %u gold, niveau %u (%u/%u XP).",
        gold, xp, state->Gold, state->Level, state->Xp, state->Level < MobaMaxLevel ? GetXpForNextLevel(state->Level) : 0);
}

uint32 GetPlayerMobaLevel(Player const* player)
{
    if (MobaPlayerState const* state = GetPlayerState(player))
        return state->Level;

    return MobaStartLevel;
}
}
