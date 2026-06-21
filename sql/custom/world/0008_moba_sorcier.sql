-- Sorcier (mage d'entropie) - custom spells.
-- Keep these IDs aligned with src/server/game/Moba/MobaRules.h (SpellMobaEntropyBolt),
-- the C++ SpellScript (spell_moba_entropy_bolt), and the client Spell.dbc patch
-- (tools/client-patch). See tools/client-patch/README.md for the full pipeline.

SET @SPELL_ENTROPY_BOLT := 900200;

DELETE FROM `spell_script_names` WHERE `spell_id` = @SPELL_ENTROPY_BOLT;
DELETE FROM `spell_dbc` WHERE `Id` = @SPELL_ENTROPY_BOLT;

-- Decharge instable: instant shadow nuke on a single enemy. The C++ script overrides the damage
-- (base + level scaling + AP ratio, amplified by the Instability gauge) and the cooldown.
INSERT INTO `spell_dbc` (
    `Id`, `Attributes`, `AttributesEx`, `CastingTimeIndex`, `ProcChance`,
    `BaseLevel`, `SpellLevel`, `DurationIndex`, `RangeIndex`,
    `Effect1`, `EffectDieSides1`, `EffectBasePoints1`, `EffectImplicitTargetA1`,
    `SpellName`, `SchoolMask`, `DmgClass`
) VALUES
(
    @SPELL_ENTROPY_BOLT, 0, 0, 1, 101,
    1, 1, 0, 4,
    2, 1, 0, 6,
    'Decharge instable', 32, 1
);

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(@SPELL_ENTROPY_BOLT, 'spell_moba_entropy_bolt');
