/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"
#include "moba_match_mgr.h"

#include "Creature.h"
#include "MobaMinion.h"
#include "MobaTower.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "Unit.h"
#include "WorldSession.h"

#include <algorithm>

namespace
{
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

            ApplyPrototypeTuning();

            // Passive: the engine must NOT pick targets for us. All target
            // acquisition is manual so the League-style priority is authoritative
            // (no line-of-sight auto-aggro, no threat-based target switching).
            me->SetReactState(REACT_PASSIVE);
            _targetCheckTimer = 0;
            _castTimer = 0;
            Moba::ResumeMinionLaneMovement(me);
        }

        void JustDied(Unit* killer) override
        {
            // Gold to the last-hitter, XP shared with nearby enemy-team champions.
            Moba::OnMinionKilled(killer, me);

            Moba::ClearMinionState(me);

            // Remove the corpse quickly so the lane is not littered with bodies.
            me->DespawnOrUnsummon(Milliseconds(3000));
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            // Never leash back home: drop the target and keep pushing the lane.
            me->AttackStop();
            Moba::ResumeMinionLaneMovement(me);
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            // Reached a lane waypoint: advance to the next one (forward only).
            if (type == POINT_MOTION_TYPE)
                Moba::OnMinionReachedWaypoint(me, id);
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
            {
                Moba::UpdateMinionLaneFlocking(me);
                return;
            }

            if (_type == Moba::MinionType::Caster)
                CastAtVictim(diff);
            else
                MeleeAtVictim();
        }

    private:
        void AcquireTarget()
        {
            // Lane leash: if a chase has dragged the minion too far off its lane, abandon the
            // target and return to the lane (it resumes forward, never backward).
            if (me->GetVictim() && Moba::IsMinionOffLane(me))
            {
                me->AttackStop();
                Moba::ResumeMinionLaneMovement(me);
                return;
            }

            Unit* target = Moba::SelectMinionTarget(me, me->GetVictim());
            if (!target)
            {
                // No target in range: stop fighting and keep marching down the lane.
                // The victim may have already been cleared by the engine (target died),
                // so resume based on the movement state, not on GetVictim().
                if (me->GetVictim())
                    me->AttackStop();

                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                    Moba::ResumeMinionLaneMovement(me);

                return;
            }

            if (target == me->GetVictim())
                return;

            // We pass meleeAttack=false so the engine never runs its own auto-swing
            // (which would play the shared, un-mutable weapon sound);
            // the swing is driven manually in MeleeAtVictim (silent damage + a sound-less attack emote).
            if (me->Attack(target, false))
                Moba::MoveMinionToCombatTarget(me, target);
        }

        // Minion-specific silent melee: deal the configured weapon damage directly (no SMSG_ATTACKERSTATEUPDATE,
        // so no shared swing/impact sound) and play a one-shot attack emote for the visible swing animation.
        void MeleeAtVictim()
        {
            Unit* victim = me->GetVictim();
            if (!victim || !me->IsWithinMeleeRange(victim) || !me->isAttackReady(BASE_ATTACK))
                return;

            uint32 const minD = uint32(me->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE));
            uint32 const maxD = uint32(me->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE));
            uint32 const dmg = urand(std::min(minD, maxD), std::max(minD, maxD));
            if (dmg)
                Unit::DealDamage(me, victim, dmg, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);

            me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK_UNARMED);
            me->resetAttackTimer(BASE_ATTACK);
        }

        void CastAtVictim(uint32 diff)
        {
            if (_castTimer > diff)
            {
                _castTimer -= diff;
                return;
            }

            Unit* victim = me->GetVictim();
            if (victim && me->IsWithinDistInMap(victim, Moba::MinionCasterAttackRange) && me->IsWithinLOSInMap(victim))
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

        void ApplyPrototypeTuning()
        {
            Moba::ApplyMinionCombatTuning(me, Moba::GetRegisteredMinionLevel(me));
        }
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
        Moba::NotifyTowerAggro(attacker, victim);
    }
};

void AddSC_moba_minion()
{
    new npc_moba_minion();
    new moba_minion_aggro_script();
}
