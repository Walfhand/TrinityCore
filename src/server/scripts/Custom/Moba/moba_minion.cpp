/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"

#include "Creature.h"
#include "MobaMinion.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "Unit.h"
#include "WorldSession.h"

namespace
{
constexpr float CasterChaseDistance = 15.0f;       // distance a caster keeps from its target
constexpr float CasterCastRange = 18.0f;           // max range a caster will poke from
constexpr uint32 CasterCastIntervalMs = 1800;      // caster "auto-attack" cadence
constexpr uint32 CasterMinionSpell = 5176;         // Wrath: nature bolt used as the ranged attack
}

class npc_moba_minion : public CreatureScript
{
public:
    npc_moba_minion() : CreatureScript("npc_moba_minion") { }

    struct npc_moba_minionAI : public ScriptedAI
    {
        npc_moba_minionAI(Creature* creature) : ScriptedAI(creature), _type(Moba::GetMinionType(creature->GetEntry())) { }

        void Reset() override
        {
            if (uint32 teamId = Moba::GetTeamIdForMinionEntry(me->GetEntry()))
                me->SetFaction(Moba::GetFactionForTeamId(teamId));

            // Passive: the engine must NOT pick targets for us. All target
            // acquisition is manual so the League-style priority is authoritative
            // (no line-of-sight auto-aggro, no threat-based target switching).
            me->SetReactState(REACT_PASSIVE);
            _targetCheckTimer = 0;
            _castTimer = 0;
            Moba::ResumeMinionLaneMovement(me);
        }

        void JustDied(Unit* /*killer*/) override
        {
            Moba::ClearMinionState(me);
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            // Never leash back home: drop the target and keep pushing the lane.
            me->AttackStop();
            Moba::ResumeMinionLaneMovement(me);
        }

        void UpdateAI(uint32 diff) override
        {
            if (_targetCheckTimer <= diff)
            {
                _targetCheckTimer = 500;
                AcquireTarget();
            }
            else
                _targetCheckTimer -= diff;

            if (!me->GetVictim())
                return;

            if (_type == Moba::MinionType::Caster)
                CastAtVictim(diff);
            else
                DoMeleeAttackIfReady();
        }

    private:
        void AcquireTarget()
        {
            Unit* target = Moba::SelectMinionTarget(me, me->GetVictim());
            if (!target)
            {
                if (me->GetVictim())
                {
                    me->AttackStop();
                    Moba::ResumeMinionLaneMovement(me);
                }
                return;
            }

            if (target == me->GetVictim())
                return;

            // Casters poke from range; melee/siege close to melee range.
            bool const melee = _type != Moba::MinionType::Caster;
            if (me->Attack(target, melee))
                me->GetMotionMaster()->MoveChase(target, melee ? 0.0f : CasterChaseDistance);
        }

        void CastAtVictim(uint32 diff)
        {
            if (_castTimer > diff)
            {
                _castTimer -= diff;
                return;
            }

            Unit* victim = me->GetVictim();
            if (victim && me->IsWithinDistInMap(victim, CasterCastRange) && me->IsWithinLOSInMap(victim))
            {
                me->CastSpell(victim, CasterMinionSpell, true);
                _castTimer = CasterCastIntervalMs;
            }
            else
                _castTimer = 200; // not in range/LOS yet, re-check shortly
        }

        Moba::MinionType const _type;
        uint32 _targetCheckTimer = 1000;
        uint32 _castTimer = 0;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_moba_minionAI(creature);
    }
};

class moba_minion_aggro_script : public UnitScript
{
public:
    moba_minion_aggro_script() : UnitScript("moba_minion_aggro_script") { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!damage)
            return;

        Moba::NotifyChampionAggro(attacker, victim);
    }
};

void AddSC_moba_minion()
{
    new npc_moba_minion();
    new moba_minion_aggro_script();
}
