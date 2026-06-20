/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaTower.h"

#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "MobaRules.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Position.h"
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Unit.h"

#include <algorithm>
#include <list>
#include <unordered_map>

namespace Moba
{
namespace
{
struct TowerState
{
    uint32 Team = InvalidTeamId;
    ObjectGuid ForcedTarget;
    uint32 ForcedTargetExpireMs = 0;
    uint32 RampStacks = 0;          // consecutive champion shots (tower-level; persists across switches)
    uint32 LastChampShotMs = 0;
};

std::unordered_map<uint64, TowerState> TowerStates;

uint64 GetTowerKey(Creature const* tower)
{
    return tower->GetGUID().GetCounter();
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

bool IsEnemyOfTower(Creature const* tower, Unit const* candidate, uint32 towerTeam)
{
    uint32 const candidateTeam = GetUnitMobaTeam(candidate);
    return IsTeamId(candidateTeam) && candidateTeam != towerTeam;
}

// League turret priority: minions before champions, siege > melee > caster. 0 = not a valid target.
uint32 GetTowerTargetPriority(Creature const* tower, Unit const* candidate, uint32 towerTeam)
{
    if (!candidate || !IsEnemyOfTower(tower, candidate, towerTeam))
        return 0;

    if (Creature const* creature = candidate->ToCreature())
    {
        if (IsMinionEntry(creature->GetEntry()))
        {
            switch (GetMinionType(creature->GetEntry()))
            {
                case MinionType::Siege:  return 40;
                case MinionType::Melee:  return 30;
                case MinionType::Caster: return 20;
            }
        }
        return 0;   // other creatures (nexus, etc.) are not tower targets
    }

    if (candidate->GetTypeId() == TYPEID_PLAYER)
        return 10;  // champions only when no minions are present (unless a forced dive target)

    return 0;
}

Unit* GetTowerForcedTarget(Creature* tower)
{
    auto itr = TowerStates.find(GetTowerKey(tower));
    if (itr == TowerStates.end() || itr->second.ForcedTarget.IsEmpty())
        return nullptr;

    if (GameTime::GetGameTimeMS() > itr->second.ForcedTargetExpireMs)
    {
        itr->second.ForcedTarget.Clear();
        return nullptr;
    }

    Unit* target = ObjectAccessor::GetUnit(*tower, itr->second.ForcedTarget);
    if (!target || !target->IsAlive() || !tower->IsValidAttackTarget(target) || !tower->IsWithinDistInMap(target, MobaTowerRange))
    {
        itr->second.ForcedTarget.Clear();
        return nullptr;
    }

    return target;
}
}

void ApplyTowerTuning(Creature* tower)
{
    if (!tower || !IsTowerEntry(tower->GetEntry()))
        return;

    tower->SetLevel(MobaTowerLevel);
    tower->SetCreateHealth(MobaTowerHealth);
    tower->SetMaxHealth(MobaTowerHealth);
    tower->SetHealth(MobaTowerHealth);
    tower->SetReactState(REACT_PASSIVE);

    TowerStates[GetTowerKey(tower)].Team = GetTeamIdForTowerEntry(tower->GetEntry());
}

void SpawnTower(Map* map, uint32 teamId, Position const& pos)
{
    uint32 const entry = GetTowerEntry(teamId);
    if (!map || !entry)
        return;

    TempSummon* tower = map->SummonCreature(entry, pos, nullptr, 0);
    if (!tower)
    {
        TC_LOG_ERROR("bg.battleground", "MOBA: tower {} spawn failed in instance {}", entry, map->GetInstanceId());
        return;
    }

    tower->SetFaction(GetFactionForTeamId(teamId));
    ApplyTowerTuning(tower);
}

Unit* SelectTowerTarget(Creature* tower, Unit* currentVictim)
{
    if (!tower || !IsTowerEntry(tower->GetEntry()))
        return nullptr;

    // Champion-dive aggro always wins.
    if (Unit* forced = GetTowerForcedTarget(tower))
        return forced;

    uint32 const towerTeam = GetTeamIdForTowerEntry(tower->GetEntry());

    // Sticky: keep firing at the current target until it dies or leaves range.
    if (currentVictim && currentVictim->IsAlive() && tower->IsValidAttackTarget(currentVictim) &&
        tower->IsWithinDistInMap(currentVictim, MobaTowerRange))
        return currentVictim;

    std::list<Unit*> nearbyUnits;
    Trinity::AnyUnitInObjectRangeCheck check(tower, MobaTowerRange);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(tower, nearbyUnits, check);
    Cell::VisitAllObjects(tower, searcher, MobaTowerRange);

    Unit* bestTarget = nullptr;
    uint32 bestPriority = 0;
    float bestDistance = 0.0f;

    for (Unit* candidate : nearbyUnits)
    {
        if (!candidate || !candidate->IsAlive() || !tower->IsValidAttackTarget(candidate))
            continue;

        uint32 const priority = GetTowerTargetPriority(tower, candidate, towerTeam);
        if (!priority)
            continue;

        float const distance = tower->GetDistance(candidate);
        if (!bestTarget || priority > bestPriority || (priority == bestPriority && distance < bestDistance))
        {
            bestTarget = candidate;
            bestPriority = priority;
            bestDistance = distance;
        }
    }

    return bestTarget;
}

void TowerShoot(Creature* tower, Unit* target)
{
    if (!tower || !target)
        return;

    TowerState& state = TowerStates[GetTowerKey(tower)];

    uint32 damage = MobaTowerDamageVsMinion;
    if (target->GetTypeId() == TYPEID_PLAYER)
    {
        uint32 const now = GameTime::GetGameTimeMS();
        if (now - state.LastChampShotMs > MobaTowerRampResetMs)
            state.RampStacks = 0;

        float const ramp = std::min(state.RampStacks * MobaTowerRampPerShot, MobaTowerRampMax);
        damage = uint32(MobaTowerDamageVsChampion * (1.0f + ramp));

        ++state.RampStacks;
        state.LastChampShotMs = now;
    }

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(MobaTowerShotSpell);
    SpellSchoolMask const school = spellInfo ? spellInfo->GetSchoolMask() : SPELL_SCHOOL_MASK_NORMAL;

    // Flying bolt visual only (0 damage); the real damage is dealt directly below so a level-1
    // tower never misses (no spell hit roll).
    CastSpellExtraArgs args(true);
    args.AddSpellMod(SPELLVALUE_BASE_POINT0, 0);
    tower->CastSpell(target, MobaTowerShotSpell, args);

    // Guaranteed damage + combat log (towers always hit at full damage).
    SpellNonMeleeDamage log(tower, target, MobaTowerShotSpell, school);
    log.damage = damage;
    tower->SendSpellNonMeleeDamageLog(&log);
    Unit::DealDamage(tower, target, damage, nullptr, SPELL_DIRECT_DAMAGE, school, spellInfo, false);
}

void NotifyTowerAggro(Unit* attacker, Unit* victim)
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

    std::list<Creature*> nearbyTowers;
    Trinity::AllCreaturesOfEntryInRange checkBlue(victimPlayer, GetTowerEntry(victimPlayer->GetBGTeam()), MobaTowerRange);
    Trinity::CreatureListSearcher<Trinity::AllCreaturesOfEntryInRange> searcher(victimPlayer, nearbyTowers, checkBlue);
    Cell::VisitAllObjects(victimPlayer, searcher, MobaTowerRange);

    for (Creature* tower : nearbyTowers)
    {
        if (!tower || !tower->IsAlive() || !tower->IsValidAttackTarget(attackingPlayer))
            continue;

        TowerState& state = TowerStates[GetTowerKey(tower)];
        state.ForcedTarget = attackingPlayer->GetGUID();
        state.ForcedTargetExpireMs = GameTime::GetGameTimeMS() + MobaTowerRampResetMs;
    }
}

void ClearTowerState(Creature* tower)
{
    if (tower)
        TowerStates.erase(GetTowerKey(tower));
}
}
