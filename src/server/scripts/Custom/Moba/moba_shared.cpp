/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"

#include "Player.h"
#include "SpellHistory.h"
#include "WorldSession.h"

namespace Moba
{
HeroKit const HeroKits[] =
{
    {
        "Briseur - engage / controle",
        "Kit Briseur applique.",
        { 100, 78, 1715, 6673, 0 } // Charge, Heroic Strike, Hamstring, Battle Shout
    },
    {
        "Arcaniste - burst / kite",
        "Kit Arcaniste applique.",
        { 116, 2136, 122, 1953, 0 } // Frostbolt, Fire Blast, Frost Nova, Blink
    },
    {
        "Gardien - sustain / support",
        "Kit Gardien applique.",
        { 635, 21084, 20271, 465, 0 } // Holy Light, Seal of Righteousness, Judgement, Devotion Aura
    }
};

std::size_t const HeroKitCount = sizeof(HeroKits) / sizeof(HeroKits[0]);

namespace
{
void RemovePrototypeSpells(Player* player)
{
    for (std::size_t i = 0; i < HeroKitCount; ++i)
        for (uint32 spellId : HeroKits[i].Spells)
            if (spellId && player->HasSpell(spellId))
                player->RemoveSpell(spellId, false, false);
}
}

void ApplyHeroKit(Player* player, HeroKit const& kit)
{
    if (player->GetLevel() != PrototypeLevel)
        player->GiveLevel(PrototypeLevel);

    player->SetFreeTalentPoints(0);
    RemovePrototypeSpells(player);

    for (uint32 spellId : kit.Spells)
        if (spellId)
            player->LearnSpell(spellId, false);

    ResetForMatch(player);
    player->SaveToDB();

    player->GetSession()->SendNotification("%s", kit.Message);
}

void ResetForMatch(Player* player)
{
    if (player->isDead())
        player->ResurrectPlayer(1.0f);

    player->SetHealth(player->GetMaxHealth());
    player->SetPower(POWER_MANA, player->GetMaxPower(POWER_MANA));
    player->SetPower(POWER_ENERGY, player->GetMaxPower(POWER_ENERGY));
    player->SetPower(POWER_RAGE, 0);
    player->GetSpellHistory()->ResetAllCooldowns();
}
}
