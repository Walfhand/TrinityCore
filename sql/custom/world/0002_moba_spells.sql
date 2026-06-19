SET @SPELL_RAGE_GUARD := 900100;
SET @SPELL_RAGE_GUARD_AURA := 900101;

DELETE FROM `spell_script_names` WHERE `spell_id` IN (@SPELL_RAGE_GUARD, @SPELL_RAGE_GUARD_AURA);
DELETE FROM `spell_dbc` WHERE `Id` IN (@SPELL_RAGE_GUARD, @SPELL_RAGE_GUARD_AURA);

-- MOBA - Garde rageuse
-- Server-side custom spell entry. Client-side DBC/MPQ patch is still needed for final icon/tooltip polish.
INSERT INTO `spell_dbc` (
    `Id`, `Attributes`, `AttributesEx`, `CastingTimeIndex`, `ProcChance`,
    `BaseLevel`, `SpellLevel`, `DurationIndex`, `RangeIndex`,
    `Effect1`, `EffectDieSides1`, `EffectImplicitTargetA1`,
    `SpellName`, `DmgMultiplier1`, `DmgMultiplier2`, `DmgMultiplier3`, `SchoolMask`
) VALUES
(
    @SPELL_RAGE_GUARD, 0, 0, 1, 101,
    1, 1, 0, 1,
    3, 1, 1,
    'Garde rageuse', 1, 1, 1, 1
),
(
    @SPELL_RAGE_GUARD_AURA, 0, 0, 1, 101,
    1, 1, 32, 1,
    6, 1, 1,
    'Garde rageuse - bouclier', 1, 1, 1, 1
);

UPDATE `spell_dbc`
SET
    `EffectApplyAuraName1` = 69, -- SPELL_AURA_SCHOOL_ABSORB
    `EffectMiscValue1` = 127     -- all spell schools
WHERE `Id` = @SPELL_RAGE_GUARD_AURA;

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(@SPELL_RAGE_GUARD, 'spell_moba_rage_guard');
