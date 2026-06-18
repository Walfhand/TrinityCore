/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#ifndef CUSTOM_MOBA_SHARED_H
#define CUSTOM_MOBA_SHARED_H

#include "Define.h"

#include <cstddef>

class Player;

namespace Moba
{
enum Constants
{
    NpcTextDefault = 1,
    PrototypeLevel = 10,
    NpcNexus = 900001,
    ActionJoinSoloTest = 1100,
    MapGmIsland = 1,
    MapSoloTest = 36
};

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
void QueueSoloNexusTest(Player* player);
bool CompleteSoloNexusObjective(Player* player);
}

#endif
