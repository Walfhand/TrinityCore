/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaTower.h"

#include "Battleground.h"
#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "EventProcessor.h"
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
#include "WorldSession.h"

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
    uint32 InstanceId = 0;
    uint32 Lane = 0;
    uint32 Ord = 0;
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

// A tower is "alive" while its state exists (erased on death). Used to compute gating order.
bool HasAliveTower(uint32 instanceId, uint32 team, uint32 lane)
{
    for (auto const& entry : TowerStates)
    {
        TowerState const& s = entry.second;
        if (s.InstanceId == instanceId && s.Team == team && s.Lane == lane)
            return true;
    }
    return false;
}

// Guaranteed damage (no spell hit roll), with the combat log + spell-school impact.
void DealTowerDamage(Creature* tower, Unit* target, uint32 damage)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(MobaTowerShotSpell);
    SpellSchoolMask const school = spellInfo ? spellInfo->GetSchoolMask() : SPELL_SCHOOL_MASK_NORMAL;

    SpellNonMeleeDamage log(tower, target, MobaTowerShotSpell, school);
    log.damage = damage;
    tower->SendSpellNonMeleeDamageLog(&log);
    Unit::DealDamage(tower, target, damage, nullptr, SPELL_DIRECT_DAMAGE, school, spellInfo, false);
}

// Applies the shot's damage when the visual bolt reaches the target (kept on the tower's event
// processor, so it is cancelled if the tower dies first).
class TowerDamageEvent : public BasicEvent
{
public:
    TowerDamageEvent(Creature* tower, ObjectGuid target, uint32 damage) : _tower(tower), _target(target), _damage(damage) { }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        if (Unit* target = ObjectAccessor::GetUnit(*_tower, _target))
            if (target->IsAlive() && _tower->IsValidAttackTarget(target))
                DealTowerDamage(_tower, target, _damage);
        return true;
    }

private:
    Creature* _tower;
    ObjectGuid _target;
    uint32 _damage;
};
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

void SpawnTower(Map* map, uint32 teamId, uint32 lane, uint32 ord, Position const& pos)
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

    TowerState& state = TowerStates[GetTowerKey(tower)];
    state.Team = teamId;
    state.InstanceId = map->GetInstanceId();
    state.Lane = lane;
    state.Ord = ord;
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

    // Flying bolt visual (0 damage so it neither double-hits nor "misses").
    CastSpellExtraArgs args(true);
    args.AddSpellMod(SPELLVALUE_BASE_POINT0, 0);
    tower->CastSpell(target, MobaTowerShotSpell, args);

    // Apply the real (guaranteed) damage when the bolt reaches the target, matching its travel time.
    uint32 delayMs = 0;
    if (spellInfo && spellInfo->Speed > 0.0f)
        delayMs = uint32(tower->GetDistance(target) / spellInfo->Speed * 1000.0f);

    if (delayMs == 0)
        DealTowerDamage(tower, target, damage);
    else
        tower->m_Events.AddEventAtOffset(new TowerDamageEvent(tower, target->GetGUID(), damage), Milliseconds(delayMs));
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

bool IsTowerVulnerable(Creature const* tower)
{
    if (!tower)
        return true;

    auto itr = TowerStates.find(GetTowerKey(tower));
    if (itr == TowerStates.end())
        return true;   // unknown -> attackable

    TowerState const& self = itr->second;

    if (self.Lane == MobaNexusTowerLane)
    {
        // Nexus towers open once at least one lane (0..2) of this team is fully cleared.
        for (uint32 lane = 0; lane < 3; ++lane)
            if (!HasAliveTower(self.InstanceId, self.Team, lane))
                return true;
        return false;
    }

    // Lane tower: invulnerable while any more-outer (lower ord) tower on its lane is alive.
    for (auto const& entry : TowerStates)
    {
        TowerState const& other = entry.second;
        if (other.InstanceId == self.InstanceId && other.Team == self.Team && other.Lane == self.Lane && other.Ord < self.Ord)
            return false;
    }
    return true;
}

bool IsNexusVulnerable(uint32 instanceId, uint32 teamId)
{
    return !HasAliveTower(instanceId, teamId, MobaNexusTowerLane);
}

bool IsStructureAttackBlocked(WorldObject const* attacker, WorldObject const* target)
{
    Creature const* structure = target ? target->ToCreature() : nullptr;
    if (!structure)
        return false;

    uint32 const entry = structure->GetEntry();
    bool const tower = IsTowerEntry(entry);
    bool const nexus = IsNexusEntry(entry);
    if (!tower && !nexus)
        return false;

    uint32 const structureTeam = tower ? GetTeamIdForTowerEntry(entry) : GetTeamIdForNexusEntry(entry);
    uint32 const attackerTeam = GetUnitMobaTeam(attacker ? attacker->ToUnit() : nullptr);

    if (attackerTeam == structureTeam)
        return true;   // never attack your own structures

    if (tower)
        return !IsTowerVulnerable(structure);

    return !IsNexusVulnerable(structure->GetMap()->GetInstanceId(), structureTeam);
}

void OnTowerDestroyed(Creature* tower)
{
    if (!tower)
        return;

    uint32 const teamId = GetTeamIdForTowerEntry(tower->GetEntry());
    char const* msg = teamId == BlueTeamId ? "Une tour bleue est tombee !" : "Une tour rouge est tombee !";

    // Announce to every player in the match, not only those near the tower.
    BattlegroundMap* bgMap = tower->GetMap()->ToBattlegroundMap();
    if (Battleground* bg = bgMap ? bgMap->GetBG() : nullptr)
    {
        for (auto const& itr : bg->GetPlayers())
            if (Player* player = ObjectAccessor::GetPlayer(*tower, itr.first))
                player->GetSession()->SendNotification("%s", msg);
    }

    ClearTowerState(tower);
}
}
