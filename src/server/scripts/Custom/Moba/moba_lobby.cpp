/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"

#include "Creature.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"

#include <string>

class npc_moba_lobby : public CreatureScript
{
public:
    npc_moba_lobby() : CreatureScript("npc_moba_lobby") { }

    struct npc_moba_lobbyAI : public ScriptedAI
    {
        npc_moba_lobbyAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            for (std::size_t i = 0; i < Moba::HeroKitCount; ++i)
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, Moba::HeroKits[i].Name, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + i);

            uint32 const teamSize = Moba::GetConfiguredTeamSize();
            std::string const matchLabel = "Rejoindre une partie " + std::to_string(teamSize) + "v" + std::to_string(teamSize) + " - Nexus";
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, matchLabel, GOSSIP_SENDER_MAIN, Moba::ActionJoinMatch);

            if (Moba::IsDevSoloModeEnabled())
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "DEV: Match solo (instantane)", GOSSIP_SENDER_MAIN, Moba::ActionJoinDevSolo);

            SendGossipMenuFor(player, Moba::NpcTextDefault, me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);
            CloseGossipMenuFor(player);

            if (action == Moba::ActionJoinMatch)
            {
                Moba::QueueMobaMatch(player);
                return true;
            }

            if (action == Moba::ActionJoinDevSolo)
            {
                Moba::QueueDevSoloTest(player);
                return true;
            }

            if (action < GOSSIP_ACTION_INFO_DEF)
                return true;

            uint32 const kitIndex = action - GOSSIP_ACTION_INFO_DEF;
            if (kitIndex >= Moba::HeroKitCount)
                return true;

            Moba::ApplyHeroKit(player, Moba::HeroKits[kitIndex]);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_moba_lobbyAI(creature);
    }
};

void AddSC_moba_lobby()
{
    new npc_moba_lobby();
}
