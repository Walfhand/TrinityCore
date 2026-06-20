/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "Creature.h"
#include "MobaRules.h"
#include "MobaTower.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "Unit.h"

class npc_moba_tower : public CreatureScript
{
public:
    npc_moba_tower() : CreatureScript("npc_moba_tower") { }

    struct npc_moba_towerAI : public ScriptedAI
    {
        npc_moba_towerAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            if (uint32 teamId = Moba::GetTeamIdForTowerEntry(me->GetEntry()))
                me->SetFaction(Moba::GetFactionForTeamId(teamId));

            // Tuning + REACT_PASSIVE: targeting is fully manual (League-style priority).
            Moba::ApplyTowerTuning(me);
            me->SetReactState(REACT_PASSIVE);
            _shotTimer = 0;
            _shieldTimer = 0;
        }

        void JustDied(Unit* /*killer*/) override
        {
            Moba::OnTowerDestroyed(me);
        }

        // Golden bubble while the tower is still protected by a more-outer tower (LoL gating).
        void RefreshShield()
        {
            bool const wantShield = !Moba::IsTowerVulnerable(me);
            bool const hasShield = me->HasAura(Moba::MobaStructureShieldSpell);
            if (wantShield && !hasShield)
            {
                me->AddAura(Moba::MobaStructureShieldSpell, me);
                if (Aura* aura = me->GetAura(Moba::MobaStructureShieldSpell))
                    aura->SetDuration(-1);
            }
            else if (!wantShield && hasShield)
                me->RemoveAurasDueToSpell(Moba::MobaStructureShieldSpell);
        }

        void MoveInLineOfSight(Unit* /*who*/) override { }

        void UpdateAI(uint32 diff) override
        {
            if (_shieldTimer <= diff)
            {
                _shieldTimer = 1000;
                RefreshShield();
            }
            else
                _shieldTimer -= diff;

            if (_shotTimer > diff)
            {
                _shotTimer -= diff;
                return;
            }
            _shotTimer = Moba::MobaTowerAttackIntervalMs;

            Unit* target = Moba::SelectTowerTarget(me, me->GetVictim());
            if (!target)
            {
                if (me->GetVictim())
                    me->AttackStop();
                return;
            }

            // Set the victim for stickiness/facing (ranged, no melee swing) and fire one shot.
            if (target != me->GetVictim())
                me->Attack(target, false);

            Moba::TowerShoot(me, target);
        }

    private:
        uint32 _shotTimer = 0;
        uint32 _shieldTimer = 0;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_moba_towerAI(creature);
    }
};

void AddSC_moba_tower()
{
    new npc_moba_tower();
}
