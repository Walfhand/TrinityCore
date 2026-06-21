/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaSorcier.h"

#include "Chat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "Unit.h"
#include "Util.h"
#include "WorldSession.h"

#include <unordered_map>

namespace Moba::Sorcier
{
namespace
{
// Per-player timers for the Instability gauge. The gauge VALUE lives on the rage power bar; only the
// decay accumulator and the post-cast grace window need storing. Keyed by player GUID counter.
struct Runtime
{
    uint32 GraceMs = 0;       // no-decay window remaining after the last cast
    uint32 DecayCarryMs = 0;  // accumulator so sub-100ms ticks still decay smoothly
};

std::unordered_map<uint64, Runtime> Runtimes;

uint64 KeyOf(Player const* player)
{
    return player->GetGUID().GetCounter();
}

void Notify(Player* player, std::string const& message)
{
    player->GetSession()->SendNotification("%s", message.c_str());
    ChatHandler(player->GetSession()).SendSysMessage(message);
}
}

void AddInstability(Player* player, uint32 amount)
{
    if (!player)
        return;

    uint32 const cur = player->GetPower(POWER_RAGE);
    player->SetPower(POWER_RAGE, std::min<uint32>(InstabilityMax, cur + amount));
    Runtimes[KeyOf(player)].GraceMs = InstabilityDecayGraceMs;   // hold the gauge so casting accumulates
}

uint32 GetInstability(Player const* player)
{
    return player ? player->GetPower(POWER_RAGE) : 0;
}

float GetInstabilityDamageMultiplier(Player const* player)
{
    if (!player)
        return 1.0f;

    float const ratio = float(player->GetPower(POWER_RAGE)) / float(InstabilityMax);
    return 1.0f + ratio * InstabilityMaxDamageBonus;
}

void ResetGauge(Player* player)
{
    if (!player)
        return;

    player->SetPower(POWER_RAGE, 0);
    Runtimes.erase(KeyOf(player));
}

void ClearPlayer(Player const* player)
{
    if (player)
        Runtimes.erase(KeyOf(player));
}

void Update(uint32 diff)
{
    for (auto itr = Runtimes.begin(); itr != Runtimes.end();)
    {
        Player* player = ObjectAccessor::FindPlayerByLowGUID(ObjectGuid::LowType(itr->first));
        if (!player || !player->InBattleground() || !player->IsAlive())
        {
            itr = Runtimes.erase(itr);   // gone / dead: drop it, AddInstability re-registers on next cast
            continue;
        }

        Runtime& rt = itr->second;
        uint32 power = player->GetPower(POWER_RAGE);

        if (power >= InstabilityMax)
        {
            uint32 const backlash = CalculatePct(player->GetMaxHealth(), InstabilityBacklashPctHealth);
            player->SetPower(POWER_RAGE, 0);
            rt.DecayCarryMs = 0;
            Notify(player, Trinity::StringFormat("Surcharge ! L'instabilite explose : {} degats.", backlash));
            // True self-damage: the overload bypasses armor/resistances so it matches the announced value.
            Unit::DealDamage(player, player, backlash, nullptr, SELF_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
            ++itr;
            continue;
        }

        // Grace window after a cast: hold the gauge so spamming ramps it up instead of draining.
        if (rt.GraceMs > 0)
        {
            rt.GraceMs = rt.GraceMs > diff ? rt.GraceMs - diff : 0;
            rt.DecayCarryMs = 0;
            ++itr;
            continue;
        }

        if (power == 0)
        {
            itr = Runtimes.erase(itr);   // settled at empty: nothing to tick until the next cast
            continue;
        }

        rt.DecayCarryMs += diff;
        while (rt.DecayCarryMs >= 100 && power > 0)
        {
            rt.DecayCarryMs -= 100;
            power = power > InstabilityDecayPer100Ms ? power - InstabilityDecayPer100Ms : 0;
        }
        player->SetPower(POWER_RAGE, power);
        ++itr;
    }
}
}
