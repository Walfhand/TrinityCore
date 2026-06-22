/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaArchetypes.h"
#include "MobaProgression.h"
#include "MobaSorcier.h"

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
        "Sorcier - mage d'entropie (Instabilite)",
        "Archetype Sorcier applique.",
        POWER_RAGE,                               // rage bar repurposed as the Instability gauge
        // Custom entropy kit: basic attack command, Q nuke, W AoE, E blink, R gauge-dump,
        // two display passives, staff proficiency.
        { Sorcier::SpellEntropyBasicAttack, Sorcier::SpellEntropyBolt, Sorcier::SpellEntropyRift, Sorcier::SpellVoidStep, Sorcier::SpellCataclysm, Sorcier::SpellInstabilityPassive, Sorcier::SpellEntropyPassive, 227, 0 },
        { 1, 1, 2, 3, 6, 1, 1, 0, 0 },
        { 20978, 0, 0 },                          // Apprentice's Staff (req level 1)
        { SKILL_STAVES, SKILL_DEFENSE, 0, 0, 0, 0, 0, 0 },
        0
    }
};

std::size_t const ArchetypeCount = sizeof(Archetypes) / sizeof(Archetypes[0]);

// Controlled combat stats per archetype, LoL-scaled. Order MUST match Archetypes[] above.
// At MOBA level 18 the total equals base + 17 * growth (the LoL curve multiplier is 1.0 there).
// Keep only validated archetypes here: prototype/vanilla kits stay out until they get custom tuning.
ArchetypeStatCurve const ArchetypeStatCurves[] =
{
    // Sorcier (mage)
    {                      560.f,  88.f,     20.f, 3.5f,    30.f, 1.3f,    50.f,  5.f,   28.f, 13.f,   2000 },
};

ArchetypeStatCurve const& GetArchetypeStatCurve(uint32 archetypeIndex)
{
    if (archetypeIndex >= ArchetypeCount)
        archetypeIndex = 0;
    return ArchetypeStatCurves[archetypeIndex];
}

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
        TC_LOG_ERROR("entities.player", "MOBA archetype: cannot equip item {} on {}", itemId, player->GetName());
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
    // Native XP is blocked by a core hook (Moba::SuppressesNativeXp), not by PLAYER_FLAGS_NO_XP_GAIN
    // (which hides the client XP bar). Clear the flag in case an older session set it.
    player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN);

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

void ReapplyArchetypeRuntime(Player* player, uint32 archetypeIndex, uint32 mobaLevel)
{
    if (!player || archetypeIndex >= ArchetypeCount)
        return;

    Archetype const& archetype = Archetypes[archetypeIndex];

    // Login rebuilds a Player from the real WoW class row, so the client can briefly regain the
    // native resource/spellbook. Re-assert only runtime archetype state; do not touch equipment.
    player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN);
    player->SetFreeTalentPoints(0);
    WipeSpellbook(player);
    UpdateArchetypeSpells(player, archetypeIndex, mobaLevel, false);
    MaxArchetypeSkills(player, archetypeIndex);
    ApplyArchetypePower(player, archetype.Power);

    if (archetype.OnApplyCast)
        player->CastSpell(player, archetype.OnApplyCast, true);
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

void RevertToBlank(Player* player)
{
    if (!player)
        return;

    // Out of a match the champion is an empty shell: nothing reveals which archetype it was.
    RemovePlayerProgress(player);             // undo applied stats + match state + per-archetype runtime
    WipeSpellbook(player);                    // 0 spells
    ClearWeaponSlots(player);                 // unequip the starter weapon(s)
    ApplyArchetypePower(player, POWER_MANA);  // neutral resource (the custom UI hides the bar out of match)
    player->SetPower(POWER_MANA, 0);
}

void ClearArchetypeRuntime(Player const* player)
{
    // Each archetype that keeps per-player runtime state clears it here (no-op if it has none).
    Sorcier::ClearPlayer(player);
}

// --- Champion ranged auto-attack dispatch ----------------------------------------------------
// Generic seam for the core combat hooks. Routes to the archetype that auto-attacks at range. Only
// the Sorcier does today; add cases here when another ranged archetype lands.

bool UsesChampionRangedAutoAttack(Player const* player)
{
    if (!player || !player->InBattleground())
        return false;

    MobaPlayerState const* state = GetPlayerState(player);
    if (!state || !state->ProgressInitialized)
        return false;

    return state->ArchetypeIndex == Sorcier::ArchetypeIndex;
}

bool StartChampionRangedAutoAttack(Player* player, Unit* victim)
{
    if (!UsesChampionRangedAutoAttack(player))
        return false;

    Sorcier::StartBasicAttack(player, victim);
    return true;
}

bool HandleChampionRangedAutoAttack(Player* player, Unit* victim, uint8& swingErrorMsg)
{
    if (!UsesChampionRangedAutoAttack(player))
        return false;

    Sorcier::HandleBasicAttackSwing(player, victim, swingErrorMsg);
    return true;
}

bool IsChampionBasicAttackSpell(Player const* champ, uint32 spellId)
{
    MobaPlayerState const* state = GetPlayerState(champ);
    if (!state || !state->ProgressInitialized)
        return false;

    switch (state->ArchetypeIndex)
    {
        case Sorcier::ArchetypeIndex:
            return Sorcier::IsBasicAttackSpell(spellId);
        default:
            return false;
    }
}

bool SuppressesNativeRage(Player const* player)
{
    if (!player || !player->InBattleground())
        return false;

    MobaPlayerState const* state = GetPlayerState(player);
    if (!state || !state->ProgressInitialized)
        return false;

    // The Sorcier repurposes POWER_RAGE as its Instability gauge, so native rage from damage dealt/taken
    // must not feed it. Other rage archetypes (e.g. the Briseur) keep native rage and return false here.
    return state->ArchetypeIndex == Sorcier::ArchetypeIndex;
}
}
