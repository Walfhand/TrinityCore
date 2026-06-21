/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "MobaSorcier.h"
#include "MobaProgression.h"

#include "Chat.h"
#include "EventProcessor.h"
#include "ModelIgnoreFlags.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
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

// --- Ranged basic attack ----------------------------------------------------------------------
// The Sorcier never melee-swings: it fires the entropy bolt (900209) on the attack timer and resolves
// a white physical hit when the missile lands. Damage scales on the MOBA AttackPower stat (LoL AD),
// reduced by the target's armor.

uint32 CalculateBasicAttackDamage(Player* player, Unit* target)
{
    MobaPlayerState const* state = GetPlayerState(player);
    if (!state)
        return 0;

    uint32 damage = state->Stats.AttackPower;
    damage = player->MeleeDamageBonusDone(target, damage, RANGED_ATTACK, nullptr, SPELL_SCHOOL_MASK_NORMAL);
    damage = target->MeleeDamageBonusTaken(player, damage, RANGED_ATTACK, nullptr, SPELL_SCHOOL_MASK_NORMAL);
    return Unit::CalcArmorReducedDamage(player, target, damage, nullptr, RANGED_ATTACK);
}

void DealBasicAttackHit(Player* player, Unit* target)
{
    if (!player || !target || !player->IsAlive() || !target->IsAlive() || !player->IsValidAttackTarget(target))
        return;

    uint32 const damage = CalculateBasicAttackDamage(player, target);
    if (!damage)
        return;

    // Report as a non-melee (spell) damage log, NOT SendAttackStateUpdate: SMSG_ATTACKERSTATEUPDATE
    // would force the client to play the staff melee swing. The visible attack is the 900209 missile;
    // this only shows the damage number. Physical school + already-armor-reduced amount, tied to 900208.
    SpellInfo const* info = sSpellMgr->GetSpellInfo(SpellEntropyBasicAttack);
    SpellNonMeleeDamage log(player, target, SpellEntropyBasicAttack, SPELL_SCHOOL_MASK_NORMAL);
    log.damage = damage;
    player->SendSpellNonMeleeDamageLog(&log);
    Unit::DealDamage(player, target, damage, nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, info, false);
}

// The bolt travels client-side; land the white hit after the same travel time so the number matches
// the impact. Speed comes from the reference missile (also applied server-side in LoadSpellInfoCorrections).
class BasicAttackDamageEvent : public BasicEvent
{
public:
    BasicAttackDamageEvent(Player* player, ObjectGuid targetGuid) : _player(player), _targetGuid(targetGuid) { }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        if (Unit* target = ObjectAccessor::GetUnit(*_player, _targetGuid))
            DealBasicAttackHit(_player, target);
        return true;
    }

private:
    Player* _player;
    ObjectGuid _targetGuid;
};

void LaunchBasicAttack(Player* player, Unit* target)
{
    CastSpellExtraArgs args(true);
    args.AddSpellMod(SPELLVALUE_BASE_POINT0, 0);
    player->CastSpell(target, SpellEntropyBasicAttackVisual, args);

    uint32 delayMs = 0;
    if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SpellEntropyBasicAttackVisualRef))
        if (spellInfo->Speed > 0.0f)
            delayMs = uint32(player->GetDistance(target) / spellInfo->Speed * 1000.0f);

    if (delayMs == 0)
        DealBasicAttackHit(player, target);
    else
        player->m_Events.AddEventAtOffset(new BasicAttackDamageEvent(player, target->GetGUID()), Milliseconds(delayMs));
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

bool IsBasicAttackSpell(uint32 spellId)
{
    return spellId == SpellEntropyBasicAttack || spellId == SpellEntropyBasicAttackVisual;
}

void StartBasicAttack(Player* player, Unit* victim)
{
    if (!victim || !victim->IsAlive() || !player->IsValidAttackTarget(victim))
        return;

    if (!player->IsWithinDistInMap(victim, BasicAttackRange) ||
        !player->IsWithinLOSInMap(victim, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2))
    {
        player->SendAttackSwingNotInRange();
        return;
    }

    player->Attack(victim, false);   // false: engage WITHOUT entering the melee-swing state (no staff strike)
}

void HandleBasicAttackSwing(Player* player, Unit* victim, uint8& swingErrorMsg)
{
    if (player->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
        player->Attack(victim, false);

    if (!victim || !victim->IsAlive() || !player->IsValidAttackTarget(victim))
    {
        player->setAttackTimer(BASE_ATTACK, 100);
        return;
    }

    if (!player->isAttackReady(BASE_ATTACK))
        return;

    if (!player->IsWithinDistInMap(victim, BasicAttackRange) ||
        !player->IsWithinLOSInMap(victim, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2))
    {
        player->setAttackTimer(BASE_ATTACK, 100);
        if (swingErrorMsg != 1)
        {
            player->SendAttackSwingNotInRange();
            swingErrorMsg = 1;
        }
        return;
    }

    if (!player->HasInArc(float(M_PI), victim))
    {
        player->setAttackTimer(BASE_ATTACK, 100);
        if (swingErrorMsg != 2)
        {
            player->SendAttackSwingBadFacingAttack();
            swingErrorMsg = 2;
        }
        return;
    }

    swingErrorMsg = 0;
    LaunchBasicAttack(player, victim);
    player->resetAttackTimer(BASE_ATTACK);
}
}
