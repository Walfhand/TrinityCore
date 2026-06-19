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

class npc_moba_minion : public CreatureScript
{
public:
    npc_moba_minion() : CreatureScript("npc_moba_minion") { }

    struct npc_moba_minionAI : public ScriptedAI
    {
        npc_moba_minionAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            if (uint32 teamId = Moba::GetTeamIdForMinionEntry(me->GetEntry()))
                me->SetFaction(Moba::GetFactionForTeamId(teamId));

            me->SetReactState(REACT_AGGRESSIVE);
            _targetCheckTimer = 1000;
            Moba::ResumeMinionLaneMovement(me);
        }

        void JustDied(Unit* /*killer*/) override
        {
            Moba::ClearMinionState(me);
        }

        void UpdateAI(uint32 diff) override
        {
            if (_targetCheckTimer <= diff)
            {
                _targetCheckTimer = 1000;
                if (Unit* target = Moba::SelectMinionTarget(me))
                {
                    if (target != me->GetVictim())
                        AttackStart(target);
                }
                else if (!me->GetVictim())
                    Moba::ResumeMinionLaneMovement(me);
            }
            else
                _targetCheckTimer -= diff;

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

    private:
        uint32 _targetCheckTimer = 1000;
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
