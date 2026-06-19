/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaLane.h"

#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MobaMinion.h"
#include "MobaRules.h"
#include "MotionMaster.h"
#include "TemporarySummon.h"
#include "Unit.h"

#include <algorithm>
#include <cmath>

namespace Moba
{
namespace
{
// Wave cadence / scaling thresholds, mirroring League of Legends timings.
constexpr uint32 SecondPhaseMs = 14u * 60u * 1000u;  // 14:00 -> siege every 2nd wave, 25s interval
constexpr uint32 ThirdPhaseMs = 25u * 60u * 1000u;   // 25:00 -> siege every wave
constexpr uint32 ThirtyMinMs = 30u * 60u * 1000u;    // 30:00 -> 20s interval
constexpr uint32 FirstUpgradeMs = 30u * 1000u;       // upgrades begin at 0:30
constexpr uint32 UpgradePeriodMs = 90u * 1000u;      // an upgrade every 90s
constexpr uint32 MaxUpgrades = 30u;

constexpr uint32 FirstSiegeWave = 3u;                // first siege leaves at 1:30 (wave 3)

constexpr float LateralSpacing = 2.5f;               // spread inside a rank
constexpr float RankSpacing = 4.0f;                  // gap between melee / caster / siege ranks
constexpr float HpGrowthPerUpgrade = 0.10f;
constexpr float DamageGrowthPerUpgrade = 0.08f;

struct LaneVectors
{
    float ForwardX = 0.0f;
    float ForwardY = 0.0f;
    float PerpX = 0.0f;
    float PerpY = 0.0f;
};

LaneVectors ComputeLaneVectors(Position const& start, Position const& destination)
{
    float dx = destination.GetPositionX() - start.GetPositionX();
    float dy = destination.GetPositionY() - start.GetPositionY();
    float length = std::sqrt(dx * dx + dy * dy);

    LaneVectors vectors;
    if (length > 0.0f)
    {
        vectors.ForwardX = dx / length;
        vectors.ForwardY = dy / length;
        vectors.PerpX = -vectors.ForwardY;
        vectors.PerpY = vectors.ForwardX;
    }

    return vectors;
}

Position ComputeRankPosition(Position const& start, LaneVectors const& vectors, float backDistance, float lateralOffset)
{
    // Ranks form behind the spawn (opposite the advance direction) and spread sideways.
    return Position(
        start.GetPositionX() - vectors.ForwardX * backDistance + vectors.PerpX * lateralOffset,
        start.GetPositionY() - vectors.ForwardY * backDistance + vectors.PerpY * lateralOffset,
        start.GetPositionZ(), start.GetOrientation());
}

void ApplyMinionUpgrades(Creature* minion, uint32 upgradeLevel)
{
    if (!upgradeLevel)
        return;

    float const hpMult = 1.0f + upgradeLevel * HpGrowthPerUpgrade;
    if (uint32 maxHp = uint32(minion->GetMaxHealth() * hpMult))
    {
        minion->SetMaxHealth(maxHp);
        minion->SetFullHealth();
    }

    float const dmgMult = 1.0f + upgradeLevel * DamageGrowthPerUpgrade;
    minion->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, minion->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE) * dmgMult);
    minion->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, minion->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE) * dmgMult);
}

void SpawnMinion(Map* map, uint32 instanceId, uint32 teamId, MinionType type, Position const& spawnPos, Position const& destination, uint32 upgradeLevel)
{
    uint32 const entry = GetMinionEntry(teamId, type);
    if (!map || !entry)
        return;

    TempSummon* minion = map->SummonCreature(entry, spawnPos, nullptr, 0);
    if (!minion)
    {
        TC_LOG_ERROR("bg.battleground", "MOBA: minion {} spawn failed in BG instance {}", entry, instanceId);
        return;
    }

    minion->SetFaction(GetFactionForTeamId(teamId));
    ApplyMinionUpgrades(minion, upgradeLevel);
    RegisterMinionLaneDestination(minion, destination);
    minion->GetMotionMaster()->MovePoint(0, destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());
}

void SpawnRank(Map* map, uint32 instanceId, uint32 teamId, MinionType type, uint32 count, uint32 rankIndex,
    Position const& start, LaneVectors const& vectors, Position const& destination, uint32 upgradeLevel)
{
    if (!count)
        return;

    float const backDistance = rankIndex * RankSpacing;
    for (uint32 i = 0; i < count; ++i)
    {
        float const lateralOffset = (float(i) - float(count - 1) / 2.0f) * LateralSpacing;
        Position const pos = ComputeRankPosition(start, vectors, backDistance, lateralOffset);
        SpawnMinion(map, instanceId, teamId, type, pos, destination, upgradeLevel);
    }
}

void SpawnTeamWave(Map* map, uint32 instanceId, uint32 teamId, Position const& start, Position const& destination, MinionWavePlan const& plan)
{
    LaneVectors const vectors = ComputeLaneVectors(start, destination);

    // Melee leads, casters trail, the siege minion sits at the back of the wave.
    SpawnRank(map, instanceId, teamId, MinionType::Melee, plan.MeleeCount, 0, start, vectors, destination, plan.UpgradeLevel);
    SpawnRank(map, instanceId, teamId, MinionType::Caster, plan.CasterCount, 1, start, vectors, destination, plan.UpgradeLevel);
    SpawnRank(map, instanceId, teamId, MinionType::Siege, plan.SiegeCount, 2, start, vectors, destination, plan.UpgradeLevel);
}
}

LaneConfig BuildSingleLaneConfig(char const* name, Position const& blueSpawn, Position const& redSpawn)
{
    LaneConfig lane;
    lane.Name = name;
    lane.BlueSpawn = blueSpawn;
    lane.RedSpawn = redSpawn;
    lane.BlueDestination = redSpawn;
    lane.RedDestination = blueSpawn;
    return lane;
}

MinionWavePlan PlanMinionWave(uint32 waveNumber, uint32 elapsedMs)
{
    MinionWavePlan plan;
    plan.MeleeCount = 3;
    plan.CasterCount = 3;

    uint32 const siegeCadence = elapsedMs < SecondPhaseMs ? 3u : (elapsedMs < ThirdPhaseMs ? 2u : 1u);
    if (waveNumber >= FirstSiegeWave && (waveNumber % siegeCadence) == 0)
    {
        plan.SiegeCount = 1;
        if (elapsedMs >= ThirtyMinMs)
            plan.CasterCount = 2;  // League trims casters to 2 once siege waves stack up
    }

    if (elapsedMs >= FirstUpgradeMs)
        plan.UpgradeLevel = std::min<uint32>(((elapsedMs - FirstUpgradeMs) / UpgradePeriodMs) + 1, MaxUpgrades);

    return plan;
}

Milliseconds GetWaveInterval(uint32 elapsedMs)
{
    if (elapsedMs < SecondPhaseMs)
        return Milliseconds(30000);

    if (elapsedMs < ThirtyMinMs)
        return Milliseconds(25000);

    return Milliseconds(20000);
}

void SpawnMinionWave(Map* map, LaneConfig const& lane, uint32 instanceId, MinionWavePlan const& plan)
{
    if (!map)
        return;

    TC_LOG_INFO("bg.battleground", "MOBA: wave '{}' in BG instance {} (melee {}, caster {}, siege {}, upgrade {})",
        lane.Name, instanceId, plan.MeleeCount, plan.CasterCount, plan.SiegeCount, plan.UpgradeLevel);

    SpawnTeamWave(map, instanceId, BlueTeamId, lane.BlueSpawn, lane.BlueDestination, plan);
    SpawnTeamWave(map, instanceId, RedTeamId, lane.RedSpawn, lane.RedDestination, plan);
}
}
