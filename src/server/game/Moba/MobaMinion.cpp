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
#include <cmath>
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
    uint32 SpawnStreamId = 0;            // lane/team stream used for wave spacing
    uint32 FormationIndex = 0;           // order inside the wave for local spacing/chase slots
    ObjectGuid ForcedTarget;
    uint32 ForcedTargetExpireTime = 0;
    uint32 NextFlockUpdateTime = 0;
    uint32 Level = MobaStartLevel;
};

constexpr float MinionWaypointArriveDist = 4.0f;   // distance at which a waypoint counts as reached
constexpr float MinionLaneTargetRange = 16.0f;
constexpr uint32 MinionFlockPointId = 0x4D0BAF10;
constexpr uint32 MinionFlockUpdateMs = 450;
constexpr float MinionFlockNeighborRange = 8.0f;
constexpr float MinionFlockSeparationRange = 3.0f;
constexpr float MinionFlockStepDistance = 5.0f;
constexpr float MinionFlockPathWeight = 4.0f;
constexpr float MinionFlockSeparationWeight = 4.5f;
constexpr float MinionFlockAlignmentWeight = 0.6f;
constexpr float MinionFlockCohesionWeight = 0.25f;
constexpr float MinionFlockMinForwardDot = 0.25f;

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

MinionState* FindMinionState(Creature const* minion)
{
    if (!minion)
        return nullptr;

    auto itr = MinionStates.find(GetMinionKey(minion));
    return itr != MinionStates.end() ? &itr->second : nullptr;
}

// stream id = laneIndex * 2 + team  ->  /2 recovers the lane index
constexpr uint32 LaneIndexFromStream(uint32 streamId) { return streamId / 2u; }

bool SameLane(MinionState const& a, MinionState const& b)
{
    return LaneIndexFromStream(a.SpawnStreamId) == LaneIndexFromStream(b.SpawnStreamId);
}

bool Normalize2d(float& x, float& y)
{
    float const length = std::sqrt(x * x + y * y);
    if (length <= 0.0001f)
        return false;

    x /= length;
    y /= length;
    return true;
}

float Dot2d(float ax, float ay, float bx, float by)
{
    return ax * bx + ay * by;
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

float DistancePointToSegment2d(Unit const* unit, Position const& from, Position const& to)
{
    float const px = unit->GetPositionX();
    float const py = unit->GetPositionY();
    float const ax = from.GetPositionX();
    float const ay = from.GetPositionY();
    float const bx = to.GetPositionX();
    float const by = to.GetPositionY();
    float const vx = bx - ax;
    float const vy = by - ay;
    float const lenSq = vx * vx + vy * vy;

    if (lenSq <= 0.0001f)
    {
        float const dx = px - ax;
        float const dy = py - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

    float const t = std::clamp(((px - ax) * vx + (py - ay) * vy) / lenSq, 0.0f, 1.0f);
    float const closestX = ax + vx * t;
    float const closestY = ay + vy * t;
    float const dx = px - closestX;
    float const dy = py - closestY;
    return std::sqrt(dx * dx + dy * dy);
}

float DistanceToLanePath2d(Unit const* unit, std::vector<Position> const& path)
{
    if (!unit || path.empty())
        return std::numeric_limits<float>::max();

    if (path.size() == 1)
        return unit->GetExactDist2d(&path.front());

    float nearest = std::numeric_limits<float>::max();
    for (uint32 i = 1; i < path.size(); ++i)
        nearest = std::min(nearest, DistancePointToSegment2d(unit, path[i - 1], path[i]));

    return nearest;
}

// A target is valid only if it sits within the lane corridor. Enemy minions are additionally
// restricted to the same lane, so cross-lane minion stacks ignore each other.
bool IsValidLaneTarget(MinionState const& state, Unit const* candidate)
{
    if (!candidate || state.Path.empty())
        return false;

    if (DistanceToLanePath2d(candidate, state.Path) > MinionLaneTargetRange)
        return false;

    Creature const* candidateCreature = candidate->ToCreature();
    if (!candidateCreature || !IsMinionEntry(candidateCreature->GetEntry()))
        return true;   // champion / tower / nexus: valid as long as it sits in the lane corridor

    MinionState const* candidateState = FindMinionState(candidateCreature);
    return candidateState && SameLane(state, *candidateState);
}

bool IsSameLaneAlly(Creature const* minion, MinionState const& state, Creature const* other, MinionState const& otherState)
{
    if (!other || other == minion || !other->IsAlive() || !IsMinionEntry(other->GetEntry()))
        return false;

    if (GetTeamIdForMinionEntry(other->GetEntry()) != GetTeamIdForMinionEntry(minion->GetEntry()))
        return false;

    return SameLane(state, otherState);
}

bool GetLaneForward(Creature const* minion, MinionState const& state, float& x, float& y)
{
    if (!minion || state.Path.empty() || state.PathIndex >= state.Path.size())
        return false;

    Position const& wp = state.Path[state.PathIndex];
    x = wp.GetPositionX() - minion->GetPositionX();
    y = wp.GetPositionY() - minion->GetPositionY();
    if (Normalize2d(x, y))
        return true;

    if (state.PathIndex + 1 >= state.Path.size())
        return false;

    Position const& next = state.Path[state.PathIndex + 1];
    x = next.GetPositionX() - wp.GetPositionX();
    y = next.GetPositionY() - wp.GetPositionY();
    return Normalize2d(x, y);
}

Unit* GetForcedTarget(Creature* minion)
{
    MinionState* state = FindMinionState(minion);
    if (!state || state->ForcedTarget.IsEmpty())
        return nullptr;

    if (GameTime::GetGameTimeMS() > state->ForcedTargetExpireTime)
    {
        state->ForcedTarget.Clear();
        return nullptr;
    }

    Unit* target = ObjectAccessor::GetUnit(*minion, state->ForcedTarget);
    if (!target || !target->IsAlive() || !minion->IsValidAttackTarget(target) ||
        !minion->IsWithinDistInMap(target, MinionChampionAggroAlertRange) || !minion->IsWithinLOSInMap(target) ||
        !IsValidLaneTarget(*state, target))
    {
        state->ForcedTarget.Clear();
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

void RegisterMinionLanePath(Creature* minion, std::vector<Position> const& path, uint32 spawnStreamId, uint32 formationIndex)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()))
        return;

    MinionState& state = MinionStates[GetMinionKey(minion)];
    state.Path = path;
    state.PathIndex = 0;
    state.SpawnStreamId = spawnStreamId;
    state.FormationIndex = formationIndex;
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

    // Without a registered lane there is nothing to validate targets against, so (as before)
    // no target is considered valid.
    MinionState const* state = FindMinionState(minion);
    if (!state || state->Path.empty())
        return nullptr;

    // Validate the current target and remember its priority for target locking.
    uint32 currentPriority = 0;
    if (currentVictim && currentVictim->IsAlive() && minion->IsValidAttackTarget(currentVictim) &&
        minion->IsWithinDistInMap(currentVictim, MinionLeashRange) && minion->IsWithinLOSInMap(currentVictim) &&
        IsValidLaneTarget(*state, currentVictim))
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
            !minion->IsWithinLOSInMap(candidate) || !IsValidLaneTarget(*state, candidate))
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

// Accumulated boid steering contributions from same-lane allies around a minion.
struct FlockSteer
{
    uint32 Neighbors = 0;
    float CenterX = 0.0f, CenterY = 0.0f;
    float SeparationX = 0.0f, SeparationY = 0.0f;
    float AlignmentX = 0.0f, AlignmentY = 0.0f;
};

FlockSteer GatherFlockNeighbors(Creature* minion, MinionState const& state)
{
    FlockSteer acc;

    std::list<Unit*> nearbyUnits;
    Trinity::AnyUnitInObjectRangeCheck check(minion, MinionFlockNeighborRange);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(minion, nearbyUnits, check);
    Cell::VisitAllObjects(minion, searcher, MinionFlockNeighborRange);

    for (Unit* nearby : nearbyUnits)
    {
        Creature const* other = nearby ? nearby->ToCreature() : nullptr;
        if (!other)
            continue;

        MinionState const* otherState = FindMinionState(other);
        if (!otherState || !IsSameLaneAlly(minion, state, other, *otherState))
            continue;

        float dx = minion->GetPositionX() - other->GetPositionX();
        float dy = minion->GetPositionY() - other->GetPositionY();
        float const dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= 0.001f || dist > MinionFlockNeighborRange)
            continue;

        ++acc.Neighbors;
        acc.CenterX += other->GetPositionX();
        acc.CenterY += other->GetPositionY();

        if (dist < MinionFlockSeparationRange)
        {
            float const pressure = (MinionFlockSeparationRange - dist) / MinionFlockSeparationRange;
            acc.SeparationX += (dx / dist) * pressure;
            acc.SeparationY += (dy / dist) * pressure;
        }

        float otherForwardX = 0.0f;
        float otherForwardY = 0.0f;
        if (GetLaneForward(other, *otherState, otherForwardX, otherForwardY))
        {
            acc.AlignmentX += otherForwardX;
            acc.AlignmentY += otherForwardY;
        }
    }

    return acc;
}

// Separation/cohesion can cancel or reverse the lane heading when minions pack tightly. If the
// combined steer no longer points down-lane, inject extra path weight so the wave keeps advancing
// instead of milling in place.
void EnsureForwardBias(float& steerX, float& steerY, float pathX, float pathY)
{
    float const forwardDot = Dot2d(steerX, steerY, pathX, pathY);
    if (forwardDot >= MinionFlockMinForwardDot)
        return;

    float const correction = MinionFlockMinForwardDot - forwardDot + MinionFlockPathWeight;
    steerX += pathX * correction;
    steerY += pathY * correction;
}
}

bool UpdateMinionLaneFlocking(Creature* minion)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()) || minion->GetVictim())
        return false;

    MinionState* statePtr = FindMinionState(minion);
    if (!statePtr || statePtr->Path.empty())
        return false;

    MinionState& state = *statePtr;
    uint32 const now = GameTime::GetGameTimeMS();
    if (now < state.NextFlockUpdateTime)
        return false;

    state.NextFlockUpdateTime = now + MinionFlockUpdateMs;
    AdvanceMinionPath(minion, state);

    float pathX = 0.0f;
    float pathY = 0.0f;
    if (!GetLaneForward(minion, state, pathX, pathY))
        return false;

    FlockSteer flock = GatherFlockNeighbors(minion, state);
    if (!flock.Neighbors)
        return false;

    float centerX = flock.CenterX / float(flock.Neighbors) - minion->GetPositionX();
    float centerY = flock.CenterY / float(flock.Neighbors) - minion->GetPositionY();
    Normalize2d(centerX, centerY);
    Normalize2d(flock.SeparationX, flock.SeparationY);
    Normalize2d(flock.AlignmentX, flock.AlignmentY);

    float steerX = pathX * MinionFlockPathWeight +
        flock.SeparationX * MinionFlockSeparationWeight +
        flock.AlignmentX * MinionFlockAlignmentWeight +
        centerX * MinionFlockCohesionWeight;
    float steerY = pathY * MinionFlockPathWeight +
        flock.SeparationY * MinionFlockSeparationWeight +
        flock.AlignmentY * MinionFlockAlignmentWeight +
        centerY * MinionFlockCohesionWeight;

    EnsureForwardBias(steerX, steerY, pathX, pathY);

    if (!Normalize2d(steerX, steerY))
        return false;

    minion->GetMotionMaster()->MovePoint(MinionFlockPointId,
        minion->GetPositionX() + steerX * MinionFlockStepDistance,
        minion->GetPositionY() + steerY * MinionFlockStepDistance,
        minion->GetPositionZ());
    return true;
}

void MoveMinionToCombatTarget(Creature* minion, Unit* target)
{
    if (!minion || !target || !IsMinionEntry(minion->GetEntry()))
        return;

    if (GetMinionType(minion->GetEntry()) == MinionType::Caster)
        minion->GetMotionMaster()->MoveChase(target, ChaseRange(MinionCasterAttackRange));
    else
        minion->GetMotionMaster()->MoveChase(target);
}

void ResumeMinionLaneMovement(Creature* minion)
{
    if (!minion || minion->GetVictim())
        return;

    MinionState* state = FindMinionState(minion);
    if (!state || state->Path.empty())
        return;

    AdvanceMinionPath(minion, *state);
    MoveMinionToCurrentWaypoint(minion, *state);
}

void OnMinionReachedWaypoint(Creature* minion, uint32 pointId)
{
    if (!minion || minion->GetVictim())
        return;

    MinionState* state = FindMinionState(minion);
    if (!state || state->Path.empty())
        return;

    // Flocking steps move via a sentinel id (MinionFlockPointId), not a path index, so only a
    // genuine lane waypoint advances the cursor; a flock step just re-issues the current waypoint.
    if (pointId == state->PathIndex)
        ++state->PathIndex;

    MoveMinionToCurrentWaypoint(minion, *state);
}

bool IsMinionOffLane(Creature const* minion)
{
    MinionState const* state = FindMinionState(minion);
    if (!state || state->Path.empty())
        return false;

    return DistanceToLanePath2d(minion, state->Path) > MinionLaneLeashRange;
}
}
