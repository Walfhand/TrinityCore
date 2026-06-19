/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef CUSTOM_MOBA_SHARED_H
#define CUSTOM_MOBA_SHARED_H

#include "Define.h"
#include "MobaRules.h"

#include <cstddef>

class Player;

namespace Moba
{
enum MobaSpells : uint32
{
    SPELL_MOBA_RAGE_GUARD = 900100,
    SPELL_MOBA_RAGE_GUARD_AURA = 900101
};

// A MOBA archetype fully overrides the player's base class: it wipes the original
// spellbook, swaps the resource (power), equips a starter weapon set, and grants its
// own kit. Spells are curated WoW spells for now; custom spells will replace them later.
struct Archetype
{
    char const* Name;
    char const* Message;
    Powers Power;          // POWER_MANA / POWER_RAGE / POWER_ENERGY
    uint32 Spells[12];     // 0 = empty slot (kit + weapon proficiencies + warrior stances...)
    uint32 Weapons[3];     // starter items to equip (0 = empty); slot auto-resolved
    uint32 Skills[8];      // combat skills to max for the archetype (0 = empty)
    uint32 OnApplyCast;    // 0 = none; spell cast on the player at pick (e.g. enter Battle Stance)
};

extern Archetype const Archetypes[];
extern std::size_t const ArchetypeCount;

void ApplyArchetype(Player* player, Archetype const& archetype);
void ResetForMatch(Player* player);
void QueueMobaMatch(Player* player);
void QueueDevSoloTest(Player* player);
bool IsDevSoloModeEnabled();
uint32 GetConfiguredTeamSize();
void AbandonPlayerMatch(Player* player);
bool CompleteSoloNexusObjective(Player* player);
}

#endif
