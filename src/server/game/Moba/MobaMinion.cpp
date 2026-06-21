/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaMinion.h"

#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "MobaRules.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Position.h"
#include "Unit.h"

#include <algorithm>
#include <limits>
#include <list>
#include <unordered_map>
#include <vector>

namespace Moba
{
namespace
{
struct MinionState
{
    std::vector<Position> Path;          // lane waypoints in this minion's travel order
    uint32 PathIndex = 0;                // current target waypoint; only ever increases (no backtracking)
    ObjectGuid ForcedTarget;
    uint32 ForcedTargetExpireTime = 0;
    uint32 Level = MobaStartLevel;
};

constexpr float MinionWaypointArriveDist = 4.0f;   // distance at which a waypoint counts as reached

// Minions move a touch slower than champions (LoL: ~325 vs ~330-340 MS). Player run rate is 1.0
// (7 yd/s), so 0.9 keeps minions just behind a champion who walks the lane with them.
constexpr float MinionRunSpeedRate = 0.90f;

std::unordered_map<uint64, MinionState> MinionStates;

struct MinionCombatTuning
{
    uint8 Level = MobaStartLevel;
    uint32 Health = 1;
    uint32 Armor = 0;
    float MinDamage = 1.0f;
    float MaxDamage = 2.0f;
    uint32 AttackTimeMs = 2000;
};

uint64 GetMinionKey(Creature const* minion)
{
    return minion->GetGUID().GetCounter();
}

uint32 ClampMinionLevel(uint32 level)
{
    return std::clamp<uint32>(level, MobaStartLevel, MobaMaxLevel);
}

uint32 ScaleUInt(uint32 base, uint32 perLevel, uint32 levelIndex)
{
    return base + perLevel * levelIndex;
}

float ScaleFloat(float base, float perLevel, uint32 levelIndex)
{
    return base + perLevel * float(levelIndex);
}

MinionCombatTuning GetMinionCombatTuning(MinionType type, uint32 level)
{
    level = ClampMinionLevel(level);
    uint32 const levelIndex = level - MobaStartLevel;

    switch (type)
    {
        // Stats and time-based scaling mirror League of Legends (minions have ~0 resistances; the
        // caster is a ranged glass cannon, the siege a high-HP, high-damage backline unit).
        case MinionType::Caster:
            return { uint8(level), ScaleUInt(284, 19, levelIndex), 0,
                ScaleFloat(19.0f, 6.0f, levelIndex), ScaleFloat(23.0f, 6.3f, levelIndex), 1500 };
        case MinionType::Siege:
            return { uint8(level), ScaleUInt(835, 295, levelIndex), 0,
                ScaleFloat(36.0f, 5.0f, levelIndex), ScaleFloat(40.0f, 5.3f, levelIndex), 1100 };
        case MinionType::Melee:
        default:
            return { uint8(level), ScaleUInt(465, 64, levelIndex), ScaleUInt(0, 1, levelIndex),
                ScaleFloat(10.0f, 4.0f, levelIndex), ScaleFloat(13.0f, 4.3f, levelIndex), 900 };
    }
}

uint32 GetUnitMobaTeam(Unit const* unit)
{
    if (!unit)
        return InvalidTeamId;

    if (Player const* player = unit->ToPlayer())
        return player->GetBGTeam();

    if (Creature const* creature = unit->ToCreature())
    {
        if (IsMinionEntry(creature->GetEntry()))
            return GetTeamIdForMinionEntry(creature->GetEntry());

        if (IsTowerEntry(creature->GetEntry()))
            return GetTeamIdForTowerEntry(creature->GetEntry());

        if (IsNexusEntry(creature->GetEntry()))
            return GetTeamIdForNexusEntry(creature->GetEntry());
    }

    return InvalidTeamId;
}

bool IsMobaChampion(Unit const* unit)
{
    Player const* player = unit ? unit->ToPlayer() : nullptr;
    return player && player->GetBattleground() && IsTeamId(player->GetBGTeam());
}

bool IsEnemyOfMinion(Creature const* minion, Unit const* candidate)
{
    uint32 const minionTeam = GetTeamIdForMinionEntry(minion->GetEntry());
    uint32 const candidateTeam = GetUnitMobaTeam(candidate);
    return IsTeamId(minionTeam) && IsTeamId(candidateTeam) && candidateTeam != minionTeam;
}

bool IsAlliedChampionForMinion(Creature const* minion, Unit const* candidate)
{
    Player const* player = candidate ? candidate->ToPlayer() : nullptr;
    return player && player->GetBattleground() && player->GetBGTeam() == GetTeamIdForMinionEntry(minion->GetEntry());
}

bool IsAlliedMinionForMinion(Creature const* minion, Unit const* candidate)
{
    Creature const* creature = candidate ? candidate->ToCreature() : nullptr;
    return creature && IsMinionEntry(creature->GetEntry()) &&
        GetTeamIdForMinionEntry(creature->GetEntry()) == GetTeamIdForMinionEntry(minion->GetEntry());
}

uint32 GetTargetPriority(Creature const* minion, Unit const* candidate)
{
    if (!candidate || !IsEnemyOfMinion(minion, candidate))
        return 0;

    Unit const* victim = candidate->GetVictim();

    // League target priority (highest first):
    //   90 enemy minion hitting an allied champion
    //   80 enemy minion hitting an allied minion
    //   70 enemy structure hitting an allied minion
    //   60 closest enemy minion
    //   30 closest enemy champion
    //   10 enemy structure
    // The "enemy champion hitting an allied champion" case sits above all of these
    // and is handled separately as a forced target (see NotifyChampionAggro).
    if (candidate->GetTypeId() == TYPEID_PLAYER)
        return 30;

    Creature const* creature = candidate->ToCreature();
    if (!creature)
        return 10;

    if (IsMinionEntry(creature->GetEntry()))
    {
        if (IsAlliedChampionForMinion(minion, victim))
            return 90;

        if (IsAlliedMinionForMinion(minion, victim))
            return 80;

        return 60;
    }

    if (IsTowerEntry(creature->GetEntry()))
        return 50;   // push the enemy tower: after clearing enemy minions, before chasing champions

    if (IsNexusEntry(creature->GetEntry()))
    {
        if (IsAlliedMinionForMinion(minion, victim))
            return 70;

        return 10;
    }

    return 10;
}

Unit* GetForcedTarget(Creature* minion)
{
    auto itr = MinionStates.find(GetMinionKey(minion));
    if (itr == MinionStates.end() || itr->second.ForcedTarget.IsEmpty())
        return nullptr;

    if (GameTime::GetGameTimeMS() > itr->second.ForcedTargetExpireTime)
    {
        itr->second.ForcedTarget.Clear();
        return nullptr;
    }

    Unit* target = ObjectAccessor::GetUnit(*minion, itr->second.ForcedTarget);
    if (!target || !target->IsAlive() || !minion->IsValidAttackTarget(target) ||
        !minion->IsWithinDistInMap(target, MinionChampionAggroAlertRange) || !minion->IsWithinLOSInMap(target))
    {
        itr->second.ForcedTarget.Clear();
        return nullptr;
    }

    return target;
}

void SetForcedTarget(Creature* minion, Unit* target)
{
    if (!minion || !target)
        return;

    MinionState& state = MinionStates[GetMinionKey(minion)];
    state.ForcedTarget = target->GetGUID();
    state.ForcedTargetExpireTime = GameTime::GetGameTimeMS() + MinionForcedAggroDurationMs;
}
}

void RegisterMinionLanePath(Creature* minion, std::vector<Position> const& path)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()))
        return;

    MinionState& state = MinionStates[GetMinionKey(minion)];
    state.Path = path;
    state.PathIndex = 0;
}

void RegisterMinionLevel(Creature* minion, uint32 level)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()))
        return;

    MinionStates[GetMinionKey(minion)].Level = ClampMinionLevel(level);
}

uint32 GetRegisteredMinionLevel(Creature const* minion)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()))
        return MobaStartLevel;

    auto itr = MinionStates.find(GetMinionKey(minion));
    if (itr == MinionStates.end())
        return MobaStartLevel;

    return ClampMinionLevel(itr->second.Level);
}

void ApplyMinionCombatTuning(Creature* minion, uint32 level)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()))
        return;

    MinionCombatTuning const tuning = GetMinionCombatTuning(GetMinionType(minion->GetEntry()), level);

    minion->SetSpeedRate(MOVE_RUN, MinionRunSpeedRate);   // slightly slower than champions (LoL pacing)
    minion->SetLevel(tuning.Level);
    minion->SetCreateHealth(tuning.Health);
    minion->SetMaxHealth(tuning.Health);
    minion->SetHealth(tuning.Health);
    minion->SetArmor(int32(tuning.Armor));
    minion->SetAttackTime(BASE_ATTACK, tuning.AttackTimeMs);
    minion->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, tuning.MinDamage);
    minion->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, tuning.MaxDamage);
    minion->UpdateDamagePhysical(BASE_ATTACK);
}

void ClearMinionState(Creature* minion)
{
    if (minion)
        MinionStates.erase(GetMinionKey(minion));
}

void NotifyChampionAggro(Unit* attacker, Unit* victim)
{
    Player* attackingPlayer = attacker ? attacker->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
    Player* victimPlayer = victim ? victim->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;

    if (!attackingPlayer || !victimPlayer || attackingPlayer == victimPlayer)
        return;

    if (!attackingPlayer->GetBattleground() || attackingPlayer->GetBattleground() != victimPlayer->GetBattleground())
        return;

    if (!IsTeamId(attackingPlayer->GetBGTeam()) || !IsTeamId(victimPlayer->GetBGTeam()) ||
        attackingPlayer->GetBGTeam() == victimPlayer->GetBGTeam())
        return;

    std::list<Unit*> nearbyUnits;
    Trinity::AnyUnitInObjectRangeCheck check(victimPlayer, MinionChampionAggroAlertRange);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(victimPlayer, nearbyUnits, check);
    Cell::VisitAllObjects(victimPlayer, searcher, MinionChampionAggroAlertRange);

    for (Unit* nearby : nearbyUnits)
    {
        Creature* minion = nearby ? nearby->ToCreature() : nullptr;
        if (!minion || !IsMinionEntry(minion->GetEntry()))
            continue;

        if (GetTeamIdForMinionEntry(minion->GetEntry()) != victimPlayer->GetBGTeam())
            continue;

        if (minion->IsValidAttackTarget(attackingPlayer) && minion->IsWithinLOSInMap(attackingPlayer))
            SetForcedTarget(minion, attackingPlayer);
    }
}

Unit* SelectMinionTarget(Creature* minion, Unit* currentVictim)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()))
        return nullptr;

    // Call-for-help (an enemy champion attacking an allied champion) always wins.
    if (Unit* forcedTarget = GetForcedTarget(minion))
        return forcedTarget;

    // Validate the current target and remember its priority for target locking.
    uint32 currentPriority = 0;
    if (currentVictim && currentVictim->IsAlive() && minion->IsValidAttackTarget(currentVictim) &&
        minion->IsWithinDistInMap(currentVictim, MinionLeashRange) && minion->IsWithinLOSInMap(currentVictim))
        currentPriority = GetTargetPriority(minion, currentVictim);
    else
        currentVictim = nullptr;

    std::list<Unit*> nearbyUnits;
    Trinity::AnyUnitInObjectRangeCheck check(minion, MinionAggroRange);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(minion, nearbyUnits, check);
    Cell::VisitAllObjects(minion, searcher, MinionAggroRange);

    Unit* bestTarget = nullptr;
    uint32 bestPriority = 0;
    float bestDistance = 0.0f;

    for (Unit* candidate : nearbyUnits)
    {
        if (!candidate || candidate == minion || !candidate->IsAlive() || !minion->IsValidAttackTarget(candidate) ||
            !minion->IsWithinLOSInMap(candidate))
            continue;

        uint32 const priority = GetTargetPriority(minion, candidate);
        if (!priority)
            continue;

        float const distance = minion->GetDistance(candidate);
        if (!bestTarget || priority > bestPriority || (priority == bestPriority && distance < bestDistance))
        {
            bestTarget = candidate;
            bestPriority = priority;
            bestDistance = distance;
        }
    }

    // Target locking: do not abandon a valid target for an equal-or-lower priority one.
    if (currentVictim && (!bestTarget || bestPriority <= currentPriority))
        return currentVictim;

    return bestTarget ? bestTarget : currentVictim;
}

namespace
{
// Advance the waypoint cursor forward only: skip any waypoints the minion already reached
// or overshot (e.g. while chasing a target), so it never walks back toward its own base.
void AdvanceMinionPath(Creature* minion, MinionState& state)
{
    while (state.PathIndex + 1 < state.Path.size() &&
        minion->GetExactDist2d(&state.Path[state.PathIndex + 1]) <= minion->GetExactDist2d(&state.Path[state.PathIndex]))
        ++state.PathIndex;

    if (state.PathIndex + 1 < state.Path.size() &&
        minion->GetExactDist2d(&state.Path[state.PathIndex]) < MinionWaypointArriveDist)
        ++state.PathIndex;
}

void MoveMinionToCurrentWaypoint(Creature* minion, MinionState& state)
{
    if (state.PathIndex >= state.Path.size())
        return;   // reached the enemy nexus; stand and let target selection attack it

    Position const& wp = state.Path[state.PathIndex];
    minion->GetMotionMaster()->MovePoint(state.PathIndex, wp.GetPositionX(), wp.GetPositionY(), wp.GetPositionZ());
}
}

void ResumeMinionLaneMovement(Creature* minion)
{
    if (!minion || minion->GetVictim())
        return;

    auto itr = MinionStates.find(GetMinionKey(minion));
    if (itr == MinionStates.end() || itr->second.Path.empty())
        return;

    AdvanceMinionPath(minion, itr->second);
    MoveMinionToCurrentWaypoint(minion, itr->second);
}

void OnMinionReachedWaypoint(Creature* minion, uint32 pointId)
{
    if (!minion || minion->GetVictim())
        return;

    auto itr = MinionStates.find(GetMinionKey(minion));
    if (itr == MinionStates.end() || itr->second.Path.empty())
        return;

    MinionState& state = itr->second;
    if (pointId == state.PathIndex)
        ++state.PathIndex;

    MoveMinionToCurrentWaypoint(minion, state);
}

bool IsMinionOffLane(Creature const* minion)
{
    if (!minion)
        return false;

    auto itr = MinionStates.find(GetMinionKey(minion));
    if (itr == MinionStates.end() || itr->second.Path.empty())
        return false;

    float nearest = std::numeric_limits<float>::max();
    for (Position const& wp : itr->second.Path)
        nearest = std::min(nearest, minion->GetExactDist2d(&wp));

    return nearest > MinionLaneLeashRange;
}
}
