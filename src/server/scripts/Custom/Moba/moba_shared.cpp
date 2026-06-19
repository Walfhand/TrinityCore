/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"
#include "moba_match_mgr.h"

#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <vector>

namespace Moba
{
Archetype const Archetypes[] =
{
    {
        "Briseur - bruiser melee (Rage)",
        "Archetype Briseur applique.",
        POWER_RAGE,
        // Charge, Heroic Strike, Hamstring, Battle Shout, Thunder Clap
        // + the 3 warrior stances (so abilities are usable) + Swords + Shield proficiency
        { 100, 78, 1715, 6673, 6343, 2457, 71, 2458, 201, 9116, SPELL_MOBA_RAGE_GUARD, 0 },
        { 2, 1, 3, 6, 4, 0, 7, 9, 0, 0, 5, 0 },
        { 20977, 20984, 0 },                      // Recruit's Shortsword + Recruit's Shield (req level 1)
        { SKILL_SWORDS, SKILL_DEFENSE, SKILL_SHIELD, 0, 0, 0, 0, 0 },
        2457                                      // enter Battle Stance on pick
    },
    {
        "Arcaniste - mage burst (Mana)",
        "Archetype Arcaniste applique.",
        POWER_MANA,
        // Frostbolt, Fire Blast, Frost Nova, Blink + Staves proficiency
        { 116, 2136, 122, 1953, 227, 0, 0, 0 },
        { 1, 2, 3, 4, 0, 0, 0, 0 },
        { 20978, 0, 0 },                          // Apprentice's Staff (req level 1)
        { SKILL_STAVES, SKILL_DEFENSE, 0, 0, 0, 0, 0, 0 },
        0
    },
    {
        "Gardien - tank/support (Mana)",
        "Archetype Gardien applique.",
        POWER_MANA,
        // Holy Light, Seal of Righteousness, Judgement, Devotion Aura, Hammer of Justice + Maces + Shield
        { 635, 21084, 20271, 465, 853, 198, 9116, 0 },
        { 1, 2, 3, 0, 4, 0, 0, 0 },
        { 20981, 20984, 0 },                      // Neophyte's Mace + Recruit's Shield (req level 1)
        { SKILL_MACES, SKILL_DEFENSE, SKILL_SHIELD, 0, 0, 0, 0, 0 },
        0
    },
    {
        "Assassin - melee burst (Energie)",
        "Archetype Assassin applique.",
        POWER_ENERGY,
        // Sinister Strike, Eviscerate, Kick, Sprint, Stealth + Daggers proficiency
        { 1752, 2098, 1766, 2983, 1784, 1180, 0, 0 },
        { 1, 2, 4, 5, 3, 0, 0, 0 },
        { 2092, 0, 0 },                           // Worn Dagger (req level 1)
        { SKILL_DAGGERS, SKILL_DEFENSE, 0, 0, 0, 0, 0, 0 },
        0
    },
    {
        "Rodeur - marksman distance (Mana)",
        "Archetype Rodeur applique.",
        POWER_MANA,
        // Auto Shot, Arcane Shot, Concussive Shot, Multi-Shot, Hunter's Mark + Bows proficiency
        { 75, 3044, 5116, 2643, 1130, 264, 0, 0 },
        { 0, 1, 2, 4, 3, 0, 0, 0 },
        { 20980, 0, 0 },                          // Warder's Shortbow (req level 1)
        { SKILL_BOWS, SKILL_DEFENSE, 0, 0, 0, 0, 0, 0 },
        0
    },
    {
        "Sorcier - DoT distance (Mana)",
        "Archetype Sorcier applique.",
        POWER_MANA,
        // Shadow Bolt, Corruption, Immolate, Fear, Curse of Agony + Staves proficiency
        { 686, 172, 348, 5782, 980, 227, 0, 0 },
        { 1, 2, 3, 5, 4, 0, 0, 0 },
        { 20978, 0, 0 },                          // Apprentice's Staff (req level 1)
        { SKILL_STAVES, SKILL_DEFENSE, 0, 0, 0, 0, 0, 0 },
        0
    }
};

std::size_t const ArchetypeCount = sizeof(Archetypes) / sizeof(Archetypes[0]);

namespace
{
// Remove every learned spell so no trace of the original class remains.
void WipeSpellbook(Player* player)
{
    std::vector<uint32> spellIds;
    spellIds.reserve(player->GetSpellMap().size());
    for (auto const& spellPair : player->GetSpellMap())
        spellIds.push_back(spellPair.first);

    for (uint32 spellId : spellIds)
        player->RemoveSpell(spellId, false, false);
}

// Free the weapon slots so the archetype's starter gear can be equipped.
void ClearWeaponSlots(Player* player)
{
    for (uint8 slot : { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND, EQUIPMENT_SLOT_RANGED })
        if (player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
}

void EquipStarterItem(Player* player, uint32 itemId)
{
    if (!itemId)
        return;

    uint16 dest = 0;
    if (player->CanEquipNewItem(NULL_SLOT, dest, itemId, false) == EQUIP_ERR_OK)
        player->EquipNewItem(dest, itemId, true);
    else
        TC_LOG_ERROR("scripts", "MOBA archetype: cannot equip item {} on {}", itemId, player->GetName());
}

void ConfigurePower(Player* player, Powers power, uint32 maxPower, uint32 currentPower)
{
    player->SetMaxPower(power, maxPower);
    player->SetPower(power, currentPower);
}

// Swap the player's resource to the archetype's power and keep inactive resources hidden.
void ApplyArchetypePower(Player* player, Powers power)
{
    player->SetPowerType(power);

    switch (power)
    {
        case POWER_MANA:
            ConfigurePower(player, POWER_MANA, 1000, 1000);
            ConfigurePower(player, POWER_RAGE, 0, 0);
            ConfigurePower(player, POWER_ENERGY, 0, 0);
            break;
        case POWER_RAGE:
            ConfigurePower(player, POWER_MANA, 0, 0);
            ConfigurePower(player, POWER_RAGE, 1000, 0); // 100 rage shown
            ConfigurePower(player, POWER_ENERGY, 0, 0);
            break;
        case POWER_ENERGY:
            ConfigurePower(player, POWER_MANA, 0, 0);
            ConfigurePower(player, POWER_RAGE, 0, 0);
            ConfigurePower(player, POWER_ENERGY, 100, 100);
            break;
        default:
            break;
    }
}

uint32 GetArchetypeIndex(Archetype const& archetype)
{
    for (std::size_t i = 0; i < ArchetypeCount; ++i)
        if (&Archetypes[i] == &archetype)
            return uint32(i);

    return 0;
}
}

// Champions are always at the absolute weapon/defense skill cap, regardless of their MOBA
// level. This must be re-applied after every GiveLevel: the engine's UpdateSkillsForLevel
// (called inside GiveLevel) otherwise drops level-dependent skills back to level * 5.
void MaxArchetypeSkills(Player* player, uint32 archetypeIndex)
{
    if (!player || archetypeIndex >= ArchetypeCount)
        return;

    Archetype const& archetype = Archetypes[archetypeIndex];
    uint16 const maxSkill = sWorld->GetConfigMaxSkillValue();

    for (uint32 skillId : archetype.Skills)
    {
        if (!skillId)
            continue;

        if (skillId == SKILL_SHIELD)
            player->SetSkill(skillId, 0, 1, 1);                 // shield is a proficiency flag (1/1)
        else
            player->SetSkill(skillId, 0, maxSkill, maxSkill);
    }
}

void ApplyArchetype(Player* player, Archetype const& archetype)
{
    // Block native XP gain so kills/quests never move the bar; the MOBA systems drive it.
    player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN);

    // Champions live on the MOBA level curve (1 -> 18). Starter gear is req-level-1 and the
    // arena bracket is clamped (see GetBattlegroundBracketByLevel) so level 1 can still port.
    if (player->GetLevel() != MobaStartLevel)
        player->GiveLevel(MobaStartLevel);
    else
        player->InitStatsForLevel(true);

    player->SetFreeTalentPoints(0);

    WipeSpellbook(player);

    uint32 const archetypeIndex = GetArchetypeIndex(archetype);
    SetPlayerArchetype(player, archetypeIndex);

    // Learn the unlocked kit (incl. weapon-proficiency spells) BEFORE equipping,
    // otherwise the base class still gates which weapons can be equipped.
    UpdateArchetypeSpells(player, archetypeIndex, MobaStartLevel, false);

    MaxArchetypeSkills(player, archetypeIndex);

    ClearWeaponSlots(player);
    for (uint32 itemId : archetype.Weapons)
        EquipStarterItem(player, itemId);

    ApplyArchetypePower(player, archetype.Power);

    // Enter a starting form/stance if the archetype needs one (e.g. warrior Battle Stance),
    // otherwise its stance-gated abilities stay unusable.
    if (archetype.OnApplyCast)
        player->CastSpell(player, archetype.OnApplyCast, true);

    ResetForMatch(player);

    player->SaveToDB();

    player->GetSession()->SendNotification("%s", archetype.Message);
}

void UpdateArchetypeSpells(Player* player, uint32 archetypeIndex, uint32 mobaLevel, bool notify)
{
    if (!player || archetypeIndex >= ArchetypeCount)
        return;

    Archetype const& archetype = Archetypes[archetypeIndex];
    bool actionBarChanged = false;

    for (std::size_t i = 0; i < sizeof(archetype.Spells) / sizeof(archetype.Spells[0]); ++i)
    {
        uint32 const spellId = archetype.Spells[i];
        if (!spellId)
            continue;

        uint8 const unlockLevel = archetype.SpellUnlockLevels[i];
        bool const unlocked = unlockLevel == 0 || unlockLevel <= mobaLevel;
        bool const known = player->HasSpell(spellId);

        if (unlocked)
        {
            if (!known)
            {
                player->LearnSpell(spellId, false);

                // Drop newly-unlocked active abilities straight onto the action bar (slot = kit
                // index) so the player never has to drag them from the spellbook. Passives and
                // weapon proficiencies are skipped.
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (spellInfo && !spellInfo->IsPassive() && i < MAX_ACTION_BUTTONS)
                {
                    player->addActionButton(uint8(i), spellId, ACTION_BUTTON_SPELL);
                    actionBarChanged = true;
                }

                if (notify && unlockLevel)
                    player->GetSession()->SendNotification("Nouveau sort debloque: %u.", spellId);
            }
        }
        else if (known)
        {
            player->RemoveSpell(spellId, false, false);
            if (i < MAX_ACTION_BUTTONS)
            {
                player->removeActionButton(uint8(i));
                actionBarChanged = true;
            }
        }
    }

    if (actionBarChanged)
        player->SendInitialActionButtons();
}

void ResetForMatch(Player* player)
{
    if (player->isDead())
        player->ResurrectPlayer(1.0f);

    player->SetHealth(player->GetMaxHealth());
    player->SetPower(player->GetPowerType(), player->GetMaxPower(player->GetPowerType()));

    if (player->GetPowerType() == POWER_RAGE)
        player->SetPower(POWER_RAGE, 0);

    player->GetSpellHistory()->ResetAllCooldowns();
}
}
