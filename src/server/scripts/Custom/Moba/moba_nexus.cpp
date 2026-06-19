/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"

#include "Battleground.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Unit.h"
#include "WorldSession.h"

class npc_moba_nexus : public CreatureScript
{
public:
    npc_moba_nexus() : CreatureScript("npc_moba_nexus") { }

    struct npc_moba_nexusAI : public ScriptedAI
    {
        npc_moba_nexusAI(Creature* creature) : ScriptedAI(creature)
        {
            MakePassive();
        }

        void Reset() override
        {
            MakePassive();
        }

        void MoveInLineOfSight(Unit* /*who*/) override { }

        void AttackStart(Unit* /*who*/) override { }

        void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            if (Creature* creature = attacker ? attacker->ToCreature() : nullptr)
            {
                if (Moba::IsMinionEntry(creature->GetEntry()) &&
                    Moba::GetTeamIdForMinionEntry(creature->GetEntry()) == Moba::GetTeamIdForNexusEntry(me->GetEntry()))
                {
                    damage = 0;
                    return;
                }
            }

            Player* player = attacker ? attacker->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
            if (!player || !player->GetBattleground())
                return;

            if (!Moba::IsOwnNexus(player->GetBGTeam(), me->GetEntry()))
                return;

            damage = 0;
            player->GetSession()->SendNotification("Tu ne peux pas attaquer ton Nexus.");
        }

        void JustDied(Unit* killer) override
        {
            Player* player = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
            me->Yell("Le Nexus est detruit. Victoire !", LANG_UNIVERSAL, player);

            if (player)
                if (Battleground* bg = player->GetBattleground())
                {
                    bg->HandleKillUnit(me, player);
                    return;
                }

            if (BattlegroundMap* bgMap = me->GetMap()->ToBattlegroundMap())
                if (Battleground* bg = bgMap->GetBG())
                {
                    bg->HandleKillUnit(me, nullptr);
                    return;
                }

            if (player && Moba::CompleteSoloNexusObjective(player))
                return;

            if (player)
                player->GetSession()->SendNotification("Nexus detruit hors match actif.");
        }

    private:
        void MakePassive()
        {
            me->SetReactState(REACT_PASSIVE);
            me->SetUnitFlag(UNIT_FLAG_PACIFIED);
            me->AttackStop();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_moba_nexusAI(creature);
    }
};

void AddSC_moba_nexus()
{
    new npc_moba_nexus();
}
