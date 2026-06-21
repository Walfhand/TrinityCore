/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

// Sorcier (mage d'entropie) spell scripts. One spell-script file per archetype; the mechanic itself
// (the Instability gauge) lives in the game-lib module src/server/game/Moba/MobaSorcier.{h,cpp}.

#include "moba_shared.h"
#include "moba_match_mgr.h"

#include "MobaSorcier.h"
#include "Cell.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "WorldSession.h"

#include <chrono>
#include <list>

namespace
{
// --- Shared helpers for the entropy kit -----------------------------------------------------

uint32 MobaSpellPower(Player* caster)
{
    if (Moba::MobaPlayerState const* state = Moba::GetPlayerState(caster))
        return state->Stats.SpellPower;
    return 0;
}

// Scripted magic damage with a combat log (mirrors the tower's pattern; raw for now, like the tower).
void DealMagicDamage(Player* caster, Unit* target, uint32 damage, uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    SpellSchoolMask const school = info ? info->GetSchoolMask() : SPELL_SCHOOL_MASK_SHADOW;
    SpellNonMeleeDamage log(caster, target, spellId, school);
    log.damage = damage;
    caster->SendSpellNonMeleeDamageLog(&log);
    Unit::DealDamage(caster, target, damage, nullptr, SPELL_DIRECT_DAMAGE, school, info, false);
}

// Alive, attackable enemies of the caster within `radius` of a center object.
void CollectEnemies(Player* caster, WorldObject* center, float radius, std::list<Unit*>& out)
{
    Trinity::AnyUnitInObjectRangeCheck check(center, radius);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(center, out, check);
    Cell::VisitAllObjects(center, searcher, radius);
    // Structures (towers/nexus) are excluded: champion spells never damage them, only auto-attacks do.
    out.remove_if([caster](Unit* u)
    {
        return !u || !caster->IsValidAttackTarget(u)
            || Moba::IsTowerEntry(u->GetEntry()) || Moba::IsNexusEntry(u->GetEntry());
    });
}

// Passive "Marque d'entropie": each ability adds a stack; the 3rd stack detonates for bonus damage.
void ApplyEntropyMark(Player* caster, Unit* target)
{
    caster->CastSpell(target, Moba::Sorcier::SpellEntropyMark, true);
    if (target->GetAuraCount(Moba::Sorcier::SpellEntropyMark) >= 3)
    {
        uint32 const detonate = 40 + uint32(0.30f * float(MobaSpellPower(caster)));
        DealMagicDamage(caster, target, detonate, Moba::Sorcier::SpellEntropyMark);
        target->RemoveAurasDueToSpell(Moba::Sorcier::SpellEntropyMark);
    }
}

// Q - Decharge instable: single-target shadow nuke; damage scales with the gauge, builds it on cast.
uint32 constexpr BoltBaseDamage = 55;
uint32 constexpr BoltDamagePerLevel = 9;
float constexpr BoltApRatio = 0.55f;
std::chrono::milliseconds constexpr BoltCooldown = std::chrono::milliseconds(3000);

class spell_moba_entropy_bolt : public SpellScriptLoader
{
public:
    spell_moba_entropy_bolt() : SpellScriptLoader("spell_moba_entropy_bolt") { }

    class script_impl : public SpellScript
    {
        PrepareSpellScript(script_impl);

        void HandleDamage(SpellEffIndex /*effIndex*/)
        {
            Player* caster = GetCaster()->ToPlayer();
            Unit* target = GetHitUnit();
            if (!caster || !target)
                return;

            uint32 const level = Moba::GetPlayerMobaLevel(caster);
            float const base = float(BoltBaseDamage) + float(BoltDamagePerLevel) * float(level > 0 ? level - 1 : 0);
            float const apBonus = BoltApRatio * float(MobaSpellPower(caster));
            float const mult = Moba::Sorcier::GetInstabilityDamageMultiplier(caster);

            SetHitDamage(int32((base + apBonus) * mult));
            ApplyEntropyMark(caster, target);
        }

        void HandleAfterCast()
        {
            if (Player* caster = GetCaster()->ToPlayer())
            {
                Moba::Sorcier::AddInstability(caster, Moba::Sorcier::InstabilityPerCast);
                caster->GetSpellHistory()->AddCooldown(Moba::Sorcier::SpellEntropyBolt, 0, BoltCooldown);
            }
        }

        void Register() override
        {
            OnEffectHitTarget += SpellEffectFn(script_impl::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
            AfterCast += SpellCastFn(script_impl::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override { return new script_impl(); }
};

// W - Faille d'entropie: ground-targeted AoE. The spell row only resolves area damage;
// the script applies the slow with the dedicated 900205 aura so no Shadowfury stun
// behavior/display leaks from the cloned client row.
uint32 constexpr RiftBaseDamage = 60;
uint32 constexpr RiftDamagePerLevel = 8;
float constexpr RiftApRatio = 0.50f;
std::chrono::milliseconds constexpr RiftCooldown = std::chrono::milliseconds(8000);

class spell_moba_entropy_rift : public SpellScriptLoader
{
public:
    spell_moba_entropy_rift() : SpellScriptLoader("spell_moba_entropy_rift") { }

    class script_impl : public SpellScript
    {
        PrepareSpellScript(script_impl);

        // Structures are never valid targets for the AoE (no damage, no slow, no mark on towers/nexus).
        void FilterStructures(std::list<WorldObject*>& targets)
        {
            targets.remove_if([](WorldObject* obj)
            {
                return obj && (Moba::IsTowerEntry(obj->GetEntry()) || Moba::IsNexusEntry(obj->GetEntry()));
            });
        }

        void HandleDamage(SpellEffIndex /*effIndex*/)
        {
            Player* caster = GetCaster()->ToPlayer();
            Unit* target = GetHitUnit();
            if (!caster || !target)
                return;

            uint32 const level = Moba::GetPlayerMobaLevel(caster);
            float const base = float(RiftBaseDamage) + float(RiftDamagePerLevel) * float(level > 0 ? level - 1 : 0);
            float const apBonus = RiftApRatio * float(MobaSpellPower(caster));
            float const mult = Moba::Sorcier::GetInstabilityDamageMultiplier(caster);

            SetHitDamage(int32((base + apBonus) * mult));
            caster->CastSpell(target, Moba::Sorcier::SpellEntropySlow, true);
            ApplyEntropyMark(caster, target);
        }

        void HandleAfterCast()
        {
            if (Player* caster = GetCaster()->ToPlayer())
            {
                Moba::Sorcier::AddInstability(caster, Moba::Sorcier::InstabilityPerCast);
                caster->GetSpellHistory()->AddCooldown(Moba::Sorcier::SpellEntropyRift, 0, RiftCooldown);
            }
        }

        void Register() override
        {
            OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(script_impl::FilterStructures, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
            OnEffectHitTarget += SpellEffectFn(script_impl::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
            AfterCast += SpellCastFn(script_impl::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override { return new script_impl(); }
};

// E - Pas du neant: the leap itself is the engine's LEAP effect (real blink: animation + sound).
// The script only slows enemies at the arrival point and builds a little instability.
float constexpr VoidStepSlowRadius = 8.0f;
std::chrono::milliseconds constexpr VoidStepCooldown = std::chrono::milliseconds(14000);

class spell_moba_void_step : public SpellScriptLoader
{
public:
    spell_moba_void_step() : SpellScriptLoader("spell_moba_void_step") { }

    class script_impl : public SpellScript
    {
        PrepareSpellScript(script_impl);

        void HandleAfterCast()
        {
            Player* caster = GetCaster()->ToPlayer();
            if (!caster)
                return;

            // Runs after the leap, so the caster is already at the arrival point.
            std::list<Unit*> enemies;
            CollectEnemies(caster, caster, VoidStepSlowRadius, enemies);
            for (Unit* enemy : enemies)
                caster->CastSpell(enemy, Moba::Sorcier::SpellEntropySlow, true);

            Moba::Sorcier::AddInstability(caster, Moba::Sorcier::InstabilityPerUtility);
            caster->GetSpellHistory()->AddCooldown(Moba::Sorcier::SpellVoidStep, 0, VoidStepCooldown);
        }

        void Register() override
        {
            AfterCast += SpellCastFn(script_impl::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override { return new script_impl(); }
};

// R - Cataclysme: consume the WHOLE gauge for a PBAoE burst that scales with the consumed instability.
uint32 constexpr CataBaseDamage = 150;
uint32 constexpr CataDamagePerLevel = 25;
float constexpr CataApRatio = 0.80f;
float constexpr CataInstabilityFactor = 0.50f;   // bonus damage per point of consumed instability
float constexpr CataRadius = 12.0f;
std::chrono::milliseconds constexpr CataCooldown = std::chrono::milliseconds(90000);

class spell_moba_cataclysm : public SpellScriptLoader
{
public:
    spell_moba_cataclysm() : SpellScriptLoader("spell_moba_cataclysm") { }

    class script_impl : public SpellScript
    {
        PrepareSpellScript(script_impl);

        void HandleHit(SpellEffIndex /*effIndex*/)
        {
            Player* caster = GetCaster()->ToPlayer();
            if (!caster)
                return;

            uint32 const level = Moba::GetPlayerMobaLevel(caster);
            uint32 const instability = Moba::Sorcier::GetInstability(caster);   // read before it is consumed
            float const base = float(CataBaseDamage) + float(CataDamagePerLevel) * float(level > 0 ? level - 1 : 0);
            float const apBonus = CataApRatio * float(MobaSpellPower(caster));
            float const instabBonus = CataInstabilityFactor * float(instability);
            uint32 const dmg = uint32(base + apBonus + instabBonus);

            std::list<Unit*> enemies;
            CollectEnemies(caster, caster, CataRadius, enemies);
            for (Unit* enemy : enemies)
            {
                DealMagicDamage(caster, enemy, dmg, Moba::Sorcier::SpellCataclysm);
                ApplyEntropyMark(caster, enemy);
            }
        }

        void HandleAfterCast()
        {
            if (Player* caster = GetCaster()->ToPlayer())
            {
                Moba::Sorcier::ResetGauge(caster);   // the gauge is consumed by the ultimate
                caster->GetSpellHistory()->AddCooldown(Moba::Sorcier::SpellCataclysm, 0, CataCooldown);
            }
        }

        void Register() override
        {
            OnEffectHit += SpellEffectFn(script_impl::HandleHit, EFFECT_0, SPELL_EFFECT_DUMMY);
            AfterCast += SpellCastFn(script_impl::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override { return new script_impl(); }
};
}

void AddSC_moba_sorcier_spells()
{
    new spell_moba_entropy_bolt();
    new spell_moba_entropy_rift();
    new spell_moba_void_step();
    new spell_moba_cataclysm();
}
