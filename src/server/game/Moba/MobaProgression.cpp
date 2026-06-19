/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaProgression.h"
#include "MobaArchetypes.h"

#include "Battleground.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <unordered_map>

namespace Moba
{
namespace
{
std::unordered_map<uint64, MobaPlayerState> PlayerStates;
std::unordered_map<uint64, uint32> SelectedArchetypes;   // archetype chosen in the lobby, before a match exists

uint64 GetPlayerKey(Player const* player)
{
    return player->GetGUID().GetCounter();
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
    if (magicResistDelta)
        for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
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

// Sync the WoW level to the MOBA level. GiveLevel rebuilds the base stats from the
// class/level tables and plays the client level-up effect; the MOBA bonus stats are
// re-layered on top afterwards (the rebuild wipes whatever delta we had applied).
void EnsureChampionLevel(Player* player, MobaPlayerState& state)
{
    if (player->GetLevel() == state.Level)
        return;

    player->GiveLevel(state.Level);
    MaxArchetypeSkills(player, state.ArchetypeIndex);   // GiveLevel reset skills to level * 5; force them back to max
    state.AppliedStats = {};
}

// Push the MOBA progression onto the client: the XP bar (on the MOBA curve) and the
// money widget (1 MOBA gold shown as 1 gold piece).
void SyncProgressionToClient(Player* player, MobaPlayerState const& state)
{
    player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN);
    player->SetMoney(state.Gold * MobaCopperPerGold);

    if (state.Level < MobaMaxLevel)
    {
        player->SetUInt32Value(PLAYER_NEXT_LEVEL_XP, GetXpForNextLevel(state.Level));
        player->SetXP(state.Xp);
    }
    else
    {
        // Max MOBA level: freeze the bar full so it does not read as "almost level 2".
        uint32 const full = GetXpForNextLevel(MobaMaxLevel - 1);
        player->SetUInt32Value(PLAYER_NEXT_LEVEL_XP, full);
        player->SetXP(full);
    }
}

// Grant XP to a single champion, resolving any level-ups (stats + spell unlocks) and
// refreshing the client bar.
void GrantPlayerXp(Player* player, MobaPlayerState& state, uint32 xp, bool notify)
{
    if (xp && state.Level < MobaMaxLevel)
    {
        state.Xp += xp;

        while (state.Level < MobaMaxLevel)
        {
            uint32 const next = GetXpForNextLevel(state.Level);
            if (state.Xp < next)
                break;

            state.Xp -= next;
            ++state.Level;

            EnsureChampionLevel(player, state);
            RecalculateStats(state);
            ApplyStateStats(player, state);
            UpdateArchetypeSpells(player, state.ArchetypeIndex, state.Level, true);

            if (notify)
                player->GetSession()->SendNotification("Niveau MOBA %u atteint.", state.Level);
        }
    }

    SyncProgressionToClient(player, state);
}

uint32 GetMinionGoldReward(Creature const* minion)
{
    switch (GetMinionType(minion->GetEntry()))
    {
        case MinionType::Caster: return MobaCasterMinionGold;
        case MinionType::Siege:  return MobaSiegeMinionGold;
        default:                 return MobaMeleeMinionGold;
    }
}

uint32 GetMinionXpReward(Creature const* minion)
{
    switch (GetMinionType(minion->GetEntry()))
    {
        case MinionType::Caster: return MobaCasterMinionXp;
        case MinionType::Siege:  return MobaSiegeMinionXp;
        default:                 return MobaMeleeMinionXp;
    }
}

// Last-hit gold: only the champion that landed the killing blow is paid.
void RewardMinionGold(Player* killer, Creature* minion)
{
    MobaPlayerState* state = GetPlayerState(killer);
    if (!state)
        return;

    uint32 const gold = GetMinionGoldReward(minion);
    state->Gold += gold;
    killer->SetMoney(state->Gold * MobaCopperPerGold);
    killer->GetSession()->SendNotification("+%u gold (dernier coup). Total: %u gold.", gold, state->Gold);
}

// Shared XP: every enemy-team champion within range of the dying minion earns the full XP.
void RewardMinionXp(Creature* minion)
{
    BattlegroundMap* bgMap = minion->GetMap()->ToBattlegroundMap();
    Battleground* bg = bgMap ? bgMap->GetBG() : nullptr;
    if (!bg)
        return;

    uint32 const beneficiaryTeam = GetEnemyTeamId(GetTeamIdForMinionEntry(minion->GetEntry()));
    if (!IsTeamId(beneficiaryTeam))
        return;

    uint32 const xp = GetMinionXpReward(minion);

    for (auto const& itr : bg->GetPlayers())
    {
        Player* player = ObjectAccessor::GetPlayer(*minion, itr.first);
        if (!player || !player->IsAlive() || player->GetBGTeam() != beneficiaryTeam)
            continue;

        if (!minion->IsWithinDistInMap(player, MobaXpShareRange))
            continue;

        if (MobaPlayerState* state = GetPlayerState(player))
            GrantPlayerXp(player, *state, xp, true);
    }
}
}

MobaPlayerState* GetPlayerState(Player* player)
{
    if (!player)
        return nullptr;

    auto itr = PlayerStates.find(GetPlayerKey(player));
    return itr != PlayerStates.end() ? &itr->second : nullptr;
}

MobaPlayerState const* GetPlayerState(Player const* player)
{
    if (!player)
        return nullptr;

    auto itr = PlayerStates.find(GetPlayerKey(player));
    return itr != PlayerStates.end() ? &itr->second : nullptr;
}

MobaPlayerState& EnsurePlayerState(Player* player)
{
    uint64 const playerKey = GetPlayerKey(player);
    MobaPlayerState& state = PlayerStates[playerKey];

    auto archetypeItr = SelectedArchetypes.find(playerKey);
    state.ArchetypeIndex = archetypeItr != SelectedArchetypes.end() ? archetypeItr->second : 0;
    return state;
}

void RemovePlayerProgress(Player* player)
{
    if (!player)
        return;

    uint64 const playerKey = GetPlayerKey(player);
    auto itr = PlayerStates.find(playerKey);
    if (itr == PlayerStates.end())
        return;

    RemoveAppliedStateStats(player, itr->second);
    PlayerStates.erase(itr);
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
    if (!state || state->ProgressInitialized)
        return;

    state->Level = MobaStartLevel;
    state->Xp = 0;
    state->Gold = MobaStartGold;
    state->AppliedStats = {};
    state->MatchElapsedMs = 0;
    state->PassiveGoldTimerMs = 0;

    EnsureChampionLevel(player, *state);
    RecalculateStats(*state);
    ApplyStateStats(player, *state);
    UpdateArchetypeSpells(player, state->ArchetypeIndex, state->Level, false);
    SyncProgressionToClient(player, *state);
    state->ProgressInitialized = true;
    NotifyState(player, *state);
}

void OnMinionKilled(Unit* killer, Creature* minion)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()))
        return;

    if (Player* killerPlayer = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr)
        if (killerPlayer->InBattleground())
            RewardMinionGold(killerPlayer, minion);

    RewardMinionXp(minion);
}

uint32 GetPlayerMobaLevel(Player const* player)
{
    if (MobaPlayerState const* state = GetPlayerState(player))
        return state->Level;

    return MobaStartLevel;
}

// Passive gold trickle (LoL-style): once a champion has been in the match past the start
// delay, grant a fixed amount of gold every interval. Driven by a thin world-update hook.
void UpdatePassiveGold(uint32 diff)
{
    for (auto& entry : PlayerStates)
    {
        MobaPlayerState& state = entry.second;

        // Timer runs while the champion is physically in the match instance. This does not
        // depend on the progression-init flag, so the trickle is robust to lifecycle timing.
        Player* player = FindOnlinePlayer(entry.first);
        if (!player || !player->InBattleground())
            continue;

        state.MatchElapsedMs += diff;
        if (state.MatchElapsedMs < MobaPassiveGoldStartMs)
            continue;

        state.PassiveGoldTimerMs += diff;
        if (state.PassiveGoldTimerMs < MobaPassiveGoldIntervalMs)
            continue;

        uint32 const intervals = state.PassiveGoldTimerMs / MobaPassiveGoldIntervalMs;
        state.PassiveGoldTimerMs -= intervals * MobaPassiveGoldIntervalMs;
        uint32 const gained = intervals * MobaPassiveGoldAmount;
        state.Gold += gained;
        player->SetMoney(state.Gold * MobaCopperPerGold);
        player->GetSession()->SendNotification("+%u gold (passif). Total: %u gold.", gained, state.Gold);
    }
}
}
