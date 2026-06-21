/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef GAME_MOBA_SORCIER_H
#define GAME_MOBA_SORCIER_H

#include "Define.h"

class Player;

// Sorcier = the "Entropy" mage. Self-contained archetype module: its constants, custom spell IDs,
// the Instability gauge mechanic and its per-player runtime live here, not in the generic MOBA
// systems. Each archetype with a unique mechanic should get its own module like this one.
namespace Moba::Sorcier
{
inline constexpr uint32 ArchetypeIndex = 5;                  // index into Moba::Archetypes[]

// Instability gauge (stored on the rage power bar; shown 0-100, internal 0-Max).
inline constexpr uint32 InstabilityMax = 1000;
inline constexpr uint32 InstabilityPerCast = 300;            // +30 shown per spell cast (~4 -> overload)
inline constexpr uint32 InstabilityDecayGraceMs = 4000;      // gauge holds (no decay) for 4s after a cast
inline constexpr uint32 InstabilityDecayPer100Ms = 7;        // once decaying, ~70/s shown
inline constexpr float  InstabilityMaxDamageBonus = 0.80f;   // +80% spell damage at a full gauge
inline constexpr uint32 InstabilityBacklashPctHealth = 20;   // overload self-damage (% of max health)
inline constexpr uint32 InstabilityPerUtility = 150;         // +15 shown for the utility blink (E)

// Custom spell IDs (900xxx range).
inline constexpr uint32 SpellEntropyBolt = 900200;           // Q: instability-scaled shadow nuke
inline constexpr uint32 SpellEntropyRift = 900201;           // W: targeted AoE damage + slow
inline constexpr uint32 SpellVoidStep    = 900202;           // E: blink + slow nearby enemies
inline constexpr uint32 SpellCataclysm   = 900203;           // R: consume the gauge for a PBAoE burst
inline constexpr uint32 SpellEntropyMark = 900204;           // stacking mark aura (on enemies), detonates at 3
inline constexpr uint32 SpellEntropySlow = 900205;           // shared slow debuff applied by W/E
inline constexpr uint32 SpellEntropyPassive = 900206;        // spellbook display entry for the mark passive
inline constexpr uint32 SpellInstabilityPassive = 900207;    // spellbook display entry explaining the gauge

void AddInstability(Player* player, uint32 amount);          // raise the gauge + refresh the no-decay grace
uint32 GetInstability(Player const* player);                 // current gauge value (0..InstabilityMax)
float GetInstabilityDamageMultiplier(Player const* player);  // 1.0 .. (1 + InstabilityMaxDamageBonus)
void ResetGauge(Player* player);                            // clear the gauge (e.g. on respawn)
void ClearPlayer(Player const* player);                     // drop runtime state (match cleanup)
void Update(uint32 diff);                                   // decay + overload backlash, driven per world tick
}

#endif
