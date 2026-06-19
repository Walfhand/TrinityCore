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
#include <list>
#include <unordered_map>

namespace Moba
{
namespace
{
struct MinionState
{
    Position Destination;
    ObjectGuid ForcedTarget;
    uint32 ForcedTargetExpireTime = 0;
};

std::unordered_map<uint64, MinionState> MinionStates;

uint64 GetMinionKey(Creature const* minion)
{
    return minion->GetGUID().GetCounter();
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
    if (!target || !target->IsAlive() || !minion->IsValidAttackTarget(target) || !minion->IsWithinDistInMap(target, MinionChampionAggroAlertRange))
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

void RegisterMinionLaneDestination(Creature* minion, Position const& destination)
{
    if (!minion || !IsMinionEntry(minion->GetEntry()))
        return;

    MinionStates[GetMinionKey(minion)].Destination = destination;
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

        if (minion->IsValidAttackTarget(attackingPlayer))
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
        minion->IsWithinDistInMap(currentVictim, MinionLeashRange))
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
        if (!candidate || candidate == minion || !candidate->IsAlive() || !minion->IsValidAttackTarget(candidate))
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

void ResumeMinionLaneMovement(Creature* minion)
{
    if (!minion || minion->GetVictim())
        return;

    auto itr = MinionStates.find(GetMinionKey(minion));
    if (itr == MinionStates.end())
        return;

    Position const& destination = itr->second.Destination;
    minion->GetMotionMaster()->MovePoint(0, destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());
}
}
