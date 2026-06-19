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
struct HeroKit
{
    char const* Name;
    char const* Message;
    uint32 Spells[5];
};

extern HeroKit const HeroKits[];
extern std::size_t const HeroKitCount;

void ApplyHeroKit(Player* player, HeroKit const& kit);
void ResetForMatch(Player* player);
void QueueMobaMatch(Player* player);
void QueueDevSoloTest(Player* player);
bool IsDevSoloModeEnabled();
uint32 GetConfiguredTeamSize();
void AbandonPlayerMatch(Player* player);
bool CompleteSoloNexusObjective(Player* player);
}

#endif
