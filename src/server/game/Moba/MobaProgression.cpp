/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaProgression.h"
#include "MobaArchetypes.h"
#include "MobaMapConfig.h"
#include "MobaSorcier.h"

#include "Battleground.h"
#include "BattlegroundScore.h"
#include "Chat.h"
#include "Creature.h"
#include "GameTime.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "Util.h"
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

// Player feedback: show a center-screen notification AND a chat line, so a missed notification can
// still be read in the chat log. Visual feedback is a temporary stopgap pending a proper UI pass.
void Announce(Player* player, std::string const& message)
{
    player->GetSession()->SendNotification("%s", message.c_str());
    ChatHandler(player->GetSession()).SendSysMessage(message);
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

// LoL increasing-growth curve: per-level gains grow with level, reaching base + 17*growth at lvl 18.
float ComputeCurveStat(float base, float growth, uint32 level)
{
    if (level <= 1)
        return base;

    float const n = float(level - 1);
    return base + growth * n * (0.7025f + 0.0175f * n);
}

void RecalculateStats(MobaPlayerState& state)
{
    ArchetypeStatCurve const& curve = GetArchetypeStatCurve(state.ArchetypeIndex);
    state.Stats.Health      = uint32(ComputeCurveStat(curve.HealthBase, curve.HealthGrowth, state.Level) + 0.5f);
    state.Stats.Armor       = uint32(ComputeCurveStat(curve.ArmorBase, curve.ArmorGrowth, state.Level) + 0.5f);
    state.Stats.MagicResist = uint32(ComputeCurveStat(curve.MagicResistBase, curve.MagicResistGrowth, state.Level) + 0.5f);
    state.Stats.AttackPower = uint32(ComputeCurveStat(curve.AttackPowerBase, curve.AttackPowerGrowth, state.Level) + 0.5f);
    state.Stats.SpellPower  = uint32(ComputeCurveStat(curve.SpellPowerBase, curve.SpellPowerGrowth, state.Level) + 0.5f);
}

// Apply the controlled absolute targets by bridging over the WoW base: the bonus we add equals
// target - (current - previouslyAppliedBonus). AppliedStats stores the bonus actually layered on.
void ApplyStateStats(Player* player, MobaPlayerState& state)
{
    // Health.
    {
        int32 const natural = int32(player->GetMaxHealth()) - int32(state.AppliedStats.Health);
        int32 const bonus   = std::max<int32>(0, int32(state.Stats.Health) - natural);
        int32 const delta   = bonus - int32(state.AppliedStats.Health);
        if (delta)
        {
            player->SetMaxHealth(uint32(std::max<int32>(1, int32(player->GetMaxHealth()) + delta)));
            player->SetHealth(std::min<uint32>(player->GetMaxHealth(), uint32(std::max<int32>(1, int32(player->GetHealth()) + delta))));
        }
        state.AppliedStats.Health = uint32(bonus);
    }

    // Armor. Applied via UNIT_MOD_ARMOR (which UpdateArmor includes) so it survives stat recomputes and
    // can push armor to the exact target even BELOW the natural WoW value (gear + agility).
    {
        int32 const natural = int32(player->GetArmor()) - state.AppliedArmorBonus;
        int32 const bonus   = int32(state.Stats.Armor) - natural;   // signed
        int32 const delta   = bonus - state.AppliedArmorBonus;
        if (delta)
        {
            player->HandleStatFlatModifier(UNIT_MOD_ARMOR, TOTAL_VALUE, float(delta > 0 ? delta : -delta), delta > 0);
            player->UpdateArmor();
        }
        state.AppliedArmorBonus = bonus;
    }

    // Magic resistance (uniform across the magic schools).
    {
        int32 const natural = int32(player->GetResistance(SPELL_SCHOOL_FROST)) - int32(state.AppliedStats.MagicResist);
        int32 const bonus   = std::max<int32>(0, int32(state.Stats.MagicResist) - natural);
        int32 const delta   = bonus - int32(state.AppliedStats.MagicResist);
        if (delta)
            for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
                player->SetResistance(SpellSchools(school), player->GetResistance(SpellSchools(school)) + delta);
        state.AppliedStats.MagicResist = uint32(bonus);
    }

    // Attack power (= LoL attack damage; scales auto-attacks and physical abilities). Unlike health/
    // armor/MR this bonus PERSISTS through GiveLevel, so it is tracked separately and never zeroed.
    {
        int32 const natural = int32(player->GetTotalAttackPowerValue(BASE_ATTACK)) - int32(state.AppliedAttackPower);
        int32 const bonus   = std::max<int32>(0, int32(state.Stats.AttackPower) - natural);
        int32 const delta   = bonus - int32(state.AppliedAttackPower);
        if (delta)
        {
            float const magnitude = float(delta > 0 ? delta : -delta);
            player->HandleStatFlatModifier(UNIT_MOD_ATTACK_POWER, TOTAL_VALUE, magnitude, delta > 0);
            player->HandleStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, TOTAL_VALUE, magnitude, delta > 0);
            player->UpdateAttackPowerAndDamage(false);
            player->UpdateAttackPowerAndDamage(true);
        }
        state.AppliedAttackPower = uint32(bonus);
    }

    // Spell power (= LoL ability power; scales magic abilities). Also persists through GiveLevel.
    {
        int32 const natural = int32(player->GetBaseSpellPowerBonus()) - int32(state.AppliedSpellPower);
        int32 const bonus   = std::max<int32>(0, int32(state.Stats.SpellPower) - natural);
        int32 const delta   = bonus - int32(state.AppliedSpellPower);
        if (delta)
            player->ApplySpellPowerBonus(delta > 0 ? delta : -delta, delta > 0);
        state.AppliedSpellPower = uint32(bonus);
    }

    // Attack speed: set the auto-attack interval directly from the archetype curve (absolute).
    {
        ArchetypeStatCurve const& curve = GetArchetypeStatCurve(state.ArchetypeIndex);
        player->SetAttackTime(BASE_ATTACK, curve.AttackTimeMs);
        player->SetAttackTime(RANGED_ATTACK, curve.AttackTimeMs);
    }
}

void RemoveAppliedStateStats(Player* player, MobaPlayerState& state)
{
    if (state.AppliedStats.Health)
    {
        int32 const healthDelta = -int32(state.AppliedStats.Health);
        player->SetMaxHealth(uint32(std::max<int32>(1, int32(player->GetMaxHealth()) + healthDelta)));
        player->SetHealth(std::min<uint32>(player->GetMaxHealth(), player->GetHealth()));
    }

    if (state.AppliedArmorBonus)
    {
        int32 const a = state.AppliedArmorBonus;
        player->HandleStatFlatModifier(UNIT_MOD_ARMOR, TOTAL_VALUE, float(a > 0 ? a : -a), a < 0);
        player->UpdateArmor();
    }

    if (state.AppliedStats.MagicResist)
        for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
            player->SetResistance(SpellSchools(school), player->GetResistance(SpellSchools(school)) - int32(state.AppliedStats.MagicResist));

    if (state.AppliedAttackPower)
    {
        player->HandleStatFlatModifier(UNIT_MOD_ATTACK_POWER, TOTAL_VALUE, float(state.AppliedAttackPower), false);
        player->HandleStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, TOTAL_VALUE, float(state.AppliedAttackPower), false);
        player->UpdateAttackPowerAndDamage(false);
        player->UpdateAttackPowerAndDamage(true);
    }

    if (state.AppliedSpellPower)
        player->ApplySpellPowerBonus(int32(state.AppliedSpellPower), false);

    state.AppliedStats = {};
    state.AppliedAttackPower = 0;
    state.AppliedSpellPower = 0;
    state.AppliedArmorBonus = 0;
}

void NotifyState(Player* player, MobaPlayerState const& state)
{
    Announce(player, Trinity::StringFormat("MOBA: niveau {}, XP {}/{}, gold {}.",
        state.Level, state.Xp, state.Level < MobaMaxLevel ? GetXpForNextLevel(state.Level) : 0, state.Gold));
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
    player->SetFreeTalentPoints(0);                     // no talent system in the MOBA mode
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

        // Snapshot health so a level-up does NOT full-heal (GiveLevel would). LoL-style: you gain the
        // max-health increase but keep your missing health.
        uint32 const preHealth = player->GetHealth();
        uint32 const preMaxHealth = player->GetMaxHealth();
        bool leveled = false;

        while (state.Level < MobaMaxLevel)
        {
            uint32 const next = GetXpForNextLevel(state.Level);
            if (state.Xp < next)
                break;

            state.Xp -= next;
            ++state.Level;
            leveled = true;

            EnsureChampionLevel(player, state);
            RecalculateStats(state);
            ApplyStateStats(player, state);
            UpdateArchetypeSpells(player, state.ArchetypeIndex, state.Level, true);

            if (notify)
                Announce(player, Trinity::StringFormat("Niveau MOBA {} atteint.", state.Level));
        }

        if (leveled)
        {
            uint32 const newMaxHealth = player->GetMaxHealth();
            uint32 const gain = newMaxHealth > preMaxHealth ? newMaxHealth - preMaxHealth : 0;
            player->SetHealth(std::max<uint32>(1, std::min<uint32>(newMaxHealth, preHealth + gain)));
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

// Credit gold to a champion and mirror the total onto the client money widget.
void AddGold(Player* player, MobaPlayerState& state, uint32 gold)
{
    state.Gold += gold;
    player->SetMoney(state.Gold * MobaCopperPerGold);
}

// Last-hit gold: only the champion that landed the killing blow is paid.
void RewardMinionGold(Player* killer, Creature* minion)
{
    MobaPlayerState* state = GetPlayerState(killer);
    if (!state)
        return;

    uint32 const gold = GetMinionGoldReward(minion);
    AddGold(killer, *state, gold);
    Announce(killer, Trinity::StringFormat("+{} gold (dernier coup). Total: {} gold.", gold, state->Gold));
}

uint32 GetChampionKillXp(MobaPlayerState const& victim)
{
    return MobaChampionKillBaseXp + victim.Level * MobaChampionKillXpPerVictimLevel;
}

// Bounty bonus the killer collects for ending a fed enemy's kill streak.
uint32 GetShutdownGold(MobaPlayerState const& victim)
{
    return std::min(victim.KillStreak * MobaChampionShutdownGoldPerStreak, MobaChampionShutdownGoldMax);
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
    Sorcier::ClearPlayer(player);   // drop any archetype runtime state too
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
    state->RespawnAtMs = 0;
    state->KillStreak = 0;
    state->LastDamagerKey = 0;
    state->LastDamageMs = 0;

    EnsureChampionLevel(player, *state);
    RecalculateStats(*state);
    ApplyStateStats(player, *state);
    UpdateArchetypeSpells(player, state->ArchetypeIndex, state->Level, false);
    SyncProgressionToClient(player, *state);
    state->ProgressInitialized = true;
    NotifyState(player, *state);
}

void ReapplyPlayerMatchState(Player* player)
{
    MobaPlayerState* state = GetPlayerState(player);
    if (!state || !state->ProgressInitialized)
        return;

    // A reconnect gives a fresh Player object: the WoW level/XP/money persist in the DB, but the
    // in-memory stat bonuses do not, so re-apply them and re-push the bar/money to the client.
    state->AppliedStats = {};
    EnsureChampionLevel(player, *state);
    MaxArchetypeSkills(player, state->ArchetypeIndex);
    RecalculateStats(*state);
    ApplyStateStats(player, *state);
    UpdateArchetypeSpells(player, state->ArchetypeIndex, state->Level, false);
    SyncProgressionToClient(player, *state);
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

// PvP kill reward (LoL-style). The killer takes the full bounty (base + shutdown for ending the
// victim's streak) and the kill XP; nearby allied champions split an assist reward. Scoreboard
// kills/deaths/assists are handled separately by Battleground::HandleKillPlayer.
void OnChampionKilled(Player* killer, Player* victim)
{
    MobaPlayerState* victimState = GetPlayerState(victim);
    if (!victimState || !victimState->ProgressInitialized || !victim->InBattleground())
        return;

    uint32 const beneficiaryTeam = GetEnemyTeamId(victim->GetBGTeam());
    if (!IsTeamId(beneficiaryTeam))
        return;

    Battleground* bg = victim->GetBattleground();
    if (!bg)
        return;

    // Size the shutdown on the streak the victim had, then end it.
    uint32 const shutdown = GetShutdownGold(*victimState);
    victimState->KillStreak = 0;

    uint32 const killXp = GetChampionKillXp(*victimState);

    // The killer (an enemy champion) takes the full bounty and grows a kill streak.
    if (killer && killer != victim && killer->GetBGTeam() == beneficiaryTeam)
    {
        if (MobaPlayerState* killerState = GetPlayerState(killer); killerState && killerState->ProgressInitialized)
        {
            uint32 const bounty = MobaChampionKillGold + shutdown;
            AddGold(killer, *killerState, bounty);
            ++killerState->KillStreak;
            GrantPlayerXp(killer, *killerState, killXp, false);
            Announce(killer, Trinity::StringFormat("Kill ! +{} gold, +{} xp. Total: {} gold.", bounty, killXp, killerState->Gold));
        }
    }

    // Nearby allied champions (the killer's team) share assist gold and the kill XP.
    for (auto const& itr : bg->GetPlayers())
    {
        Player* ally = ObjectAccessor::GetPlayer(*victim, itr.first);
        if (!ally || ally == killer || !ally->IsAlive() || ally->GetBGTeam() != beneficiaryTeam)
            continue;

        if (!victim->IsWithinDistInMap(ally, MobaChampionRewardRange))
            continue;

        MobaPlayerState* allyState = GetPlayerState(ally);
        if (!allyState || !allyState->ProgressInitialized)
            continue;

        AddGold(ally, *allyState, MobaChampionAssistGold);
        GrantPlayerXp(ally, *allyState, killXp, false);
        Announce(ally, Trinity::StringFormat("Assist ! +{} gold, +{} xp. Total: {} gold.", MobaChampionAssistGold, killXp, allyState->Gold));
    }

    // Death is now credited: clear the recent-damager so the respawn poll does not re-award it.
    victimState->LastDamagerKey = 0;
    victimState->LastDamageMs = 0;
}

// Record the last enemy champion to damage a champion, so a kill can still be credited if a minion
// or tower lands the finishing blow (LoL-style). Called from the UnitScript OnDamage hook.
void NoteChampionDamage(Unit* attacker, Unit* victim)
{
    Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
    if (!victimPlayer || !victimPlayer->InBattleground())
        return;

    MobaPlayerState* victimState = GetPlayerState(victimPlayer);
    if (!victimState || !victimState->ProgressInitialized)
        return;

    Player* attackerPlayer = attacker ? attacker->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
    if (!attackerPlayer || attackerPlayer == victimPlayer || attackerPlayer->GetBGTeam() == victimPlayer->GetBGTeam())
        return;   // only enemy champion damage counts

    MobaPlayerState const* attackerState = GetPlayerState(attackerPlayer);
    if (!attackerState || !attackerState->ProgressInitialized)
        return;

    victimState->LastDamagerKey = attackerPlayer->GetGUID().GetCounter();
    victimState->LastDamageMs = GameTime::GetGameTimeMS();
}

uint32 GetPlayerMobaLevel(Player const* player)
{
    if (MobaPlayerState const* state = GetPlayerState(player))
        return state->Level;

    return MobaStartLevel;
}

bool BlocksGraveyardResurrect(Player const* player)
{
    if (player->IsAlive())
        return false;   // only governs the dead state; alive callers keep vanilla behavior

    MobaPlayerState const* state = GetPlayerState(player);
    return state && state->ProgressInitialized && player->InBattleground();
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
        Announce(player, Trinity::StringFormat("+{} gold (passif). Total: {} gold.", gained, state.Gold));
    }
}

namespace
{
void TeleportToBase(Player* player)
{
    MapLayout const* map = GetMobaMapLayout(player->GetMapId());
    if (!map)
        return;

    Position const& base = player->GetBGTeam() == BlueTeamId ? map->BlueBase : map->RedBase;
    player->TeleportTo(player->GetMapId(), base.GetPositionX(), base.GetPositionY(), base.GetPositionZ(), base.GetOrientation());
}

void RespawnAtBase(Player* player)
{
    player->ResurrectPlayer(1.0f);
    player->SpawnCorpseBones();
    player->SetFullHealth();
    // Rage-style resources (e.g. the Sorcier Instability gauge) start a fresh life empty, not full.
    Powers const power = player->GetPowerType();
    player->SetPower(power, power == POWER_RAGE ? 0 : player->GetMaxPower(power));
    TeleportToBase(player);

    if (MobaPlayerState* state = GetPlayerState(player))
    {
        state->LastDamagerKey = 0;   // a fresh life starts with no pending kill credit
        state->LastDamageMs = 0;
    }
}
}

// Champion death/respawn: detect dead champions (killed by anything) and respawn them at the team
// base after a level-scaled timer. Poll-based so it catches deaths from towers/minions/etc. The
// player is free to release into a roaming ghost meanwhile; all self-resurrect paths (graveyard
// auto-rez, corpse reclaim) are blocked in the core so only this timer ever brings them back.
void UpdateRespawns(uint32 /*diff*/)
{
    uint32 const now = GameTime::GetGameTimeMS();

    for (auto& entry : PlayerStates)
    {
        MobaPlayerState& state = entry.second;
        if (!state.ProgressInitialized)
            continue;

        Player* player = FindOnlinePlayer(entry.first);
        if (!player || !player->InBattleground())
            continue;

        if (player->IsAlive())
        {
            state.RespawnAtMs = 0;
            continue;
        }

        // Dead: start the timer on the first detection.
        if (state.RespawnAtMs == 0)
        {
            // If the finishing blow came from a minion/tower (no player credited via HandleKillPlayer)
            // but an enemy champion damaged us recently, that champion still gets the kill (LoL-style),
            // including the scoreboard line the base HandleKillPlayer would otherwise have written.
            if (state.LastDamagerKey && now - state.LastDamageMs <= MobaKillCreditWindowMs)
            {
                if (Player* damager = FindOnlinePlayer(state.LastDamagerKey))
                {
                    if (Battleground* bg = player->GetBattleground())
                    {
                        bg->UpdatePlayerScore(player, SCORE_DEATHS, 1);
                        bg->UpdatePlayerScore(damager, SCORE_KILLING_BLOWS, 1);
                        bg->UpdatePlayerScore(damager, SCORE_HONORABLE_KILLS, 1);
                    }
                    OnChampionKilled(damager, player);
                }
            }

            uint32 const respawnMs = MobaRespawnBaseMs + state.Level * MobaRespawnPerLevelMs;
            state.RespawnAtMs = now + respawnMs;
            Announce(player, Trinity::StringFormat("Mort. Reapparition dans {} s.", respawnMs / 1000));
            continue;
        }

        if (now < state.RespawnAtMs)
            continue;

        state.RespawnAtMs = 0;
        RespawnAtBase(player);
    }
}
}
