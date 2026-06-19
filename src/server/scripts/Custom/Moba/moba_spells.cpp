/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 */

#include "moba_shared.h"

#include "Player.h"
#include "SpellHistory.h"
#include "SpellScript.h"
#include "WorldSession.h"

#include <chrono>

namespace
{
uint32 constexpr RageGuardCost = 300; // 30 rage, stored internally as tenths.
uint32 constexpr RageGuardShieldPct = 20;
std::chrono::seconds constexpr RageGuardCooldown = std::chrono::seconds(12);

class spell_moba_rage_guard : public SpellScriptLoader
{
public:
    spell_moba_rage_guard() : SpellScriptLoader("spell_moba_rage_guard") { }

    class spell_moba_rage_guard_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_moba_rage_guard_SpellScript);

        bool Validate(SpellInfo const* /*spellInfo*/) override
        {
            return ValidateSpellInfo({ Moba::SPELL_MOBA_RAGE_GUARD_AURA });
        }

        SpellCastResult CheckCast()
        {
            Player* player = GetCaster()->ToPlayer();
            if (!player)
                return SPELL_FAILED_BAD_TARGETS;

            if (player->GetPowerType() != POWER_RAGE || player->GetPower(POWER_RAGE) < RageGuardCost)
                return SPELL_FAILED_NO_POWER;

            if (player->GetSpellHistory()->HasCooldown(Moba::SPELL_MOBA_RAGE_GUARD))
                return SPELL_FAILED_NOT_READY;

            return SPELL_CAST_OK;
        }

        void HandleDummy(SpellEffIndex /*effIndex*/)
        {
            Player* player = GetCaster()->ToPlayer();
            if (!player)
                return;

            player->ModifyPower(POWER_RAGE, -int32(RageGuardCost));

            int32 absorbAmount = CalculatePct(player->GetMaxHealth(), RageGuardShieldPct);
            CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
            args.AddSpellMod(SPELLVALUE_BASE_POINT0, absorbAmount);
            player->CastSpell(player, Moba::SPELL_MOBA_RAGE_GUARD_AURA, args);

            player->GetSpellHistory()->AddCooldown(Moba::SPELL_MOBA_RAGE_GUARD, 0, RageGuardCooldown);
            player->GetSession()->SendNotification("Garde rageuse absorbe %u degats.", uint32(absorbAmount));
        }

        void Register() override
        {
            OnCheckCast += SpellCheckCastFn(spell_moba_rage_guard_SpellScript::CheckCast);
            OnEffectHitTarget += SpellEffectFn(spell_moba_rage_guard_SpellScript::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_moba_rage_guard_SpellScript();
    }
};
}

void AddSC_moba_spells()
{
    new spell_moba_rage_guard();
}
