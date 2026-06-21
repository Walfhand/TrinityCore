-- Sorcier (mage d'entropie) - custom spells.
-- Keep these IDs aligned with src/server/game/Moba/MobaSorcier.h, the C++ SpellScripts
-- (src/server/scripts/Custom/Moba/moba_sorcier_spells.cpp), and the client Spell.dbc patch
-- (tools/client-patch). See tools/client-patch/README.md for the full pipeline.

SET @SPELL_ENTROPY_BOLT := 900200;  -- Q  single-target nuke
SET @SPELL_ENTROPY_RIFT := 900201;  -- W  ground-target AoE skillshot + slow
SET @SPELL_VOID_STEP    := 900202;  -- E  blink (real leap) + slow
SET @SPELL_CATACLYSM    := 900203;  -- R  consume gauge, PBAoE burst
SET @SPELL_ENTROPY_MARK := 900204;  -- stacking mark aura (on enemies)
SET @SPELL_ENTROPY_SLOW := 900205;  -- shared slow debuff
SET @SPELL_ENTROPY_PASSIVE := 900206; -- spellbook display entry for the mark passive
SET @SPELL_INSTABILITY_PASSIVE := 900207; -- spellbook display entry explaining the Instability gauge
SET @SPELL_ENTROPY_BASIC_ATTACK := 900208; -- spellbook/action-bar command for the ranged basic attack
SET @SPELL_ENTROPY_BASIC_ATTACK_VISUAL := 900209; -- hidden visual-only missile for the ranged basic attack

DELETE FROM `spell_script_names` WHERE `spell_id` IN
    (@SPELL_ENTROPY_BOLT, @SPELL_ENTROPY_RIFT, @SPELL_VOID_STEP, @SPELL_CATACLYSM,
     @SPELL_ENTROPY_BASIC_ATTACK, @SPELL_ENTROPY_BASIC_ATTACK_VISUAL);
DELETE FROM `spell_dbc` WHERE `Id` IN
    (@SPELL_ENTROPY_BOLT, @SPELL_ENTROPY_RIFT, @SPELL_VOID_STEP, @SPELL_CATACLYSM,
     @SPELL_ENTROPY_MARK, @SPELL_ENTROPY_SLOW, @SPELL_ENTROPY_PASSIVE, @SPELL_INSTABILITY_PASSIVE,
     @SPELL_ENTROPY_BASIC_ATTACK, @SPELL_ENTROPY_BASIC_ATTACK_VISUAL);

-- Q: Decharge instable - instant shadow nuke on one enemy. Damage/scaling done in C++.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`, `Effect1`, `EffectDieSides1`, `EffectImplicitTargetA1`,
     `SpellName`, `SchoolMask`, `DmgClass`)
VALUES
    (@SPELL_ENTROPY_BOLT, 0, 1, 101, 1, 1, 0, 4, 2, 1, 6, 'Decharge instable', 32, 1);

-- Basic attack command: no direct damage/projectile. It only selects a target and starts the
-- Sorcier MOBA ranged auto-attack loop; the white hit is driven by MobaProgression.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`, `Effect1`, `EffectDieSides1`, `EffectImplicitTargetA1`,
     `SpellName`)
VALUES
    (@SPELL_ENTROPY_BASIC_ATTACK, 0, 1, 101, 1, 1, 0, 3, 3, 1, 6, 'Trait d''entropie');

-- Basic attack visual: Arcane Barrage-like missile. Its spell hit is prevented in C++;
-- only the separate white auto-attack damage is allowed through.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`, `Effect1`, `EffectDieSides1`, `EffectImplicitTargetA1`,
     `SpellName`, `SchoolMask`, `DmgClass`)
VALUES
    (@SPELL_ENTROPY_BASIC_ATTACK_VISUAL, 0, 1, 101, 1, 1, 0, 3, 2, 1, 6, 'Trait d''entropie visuel', 64, 1);

-- W: Faille d'entropie - ground-targeted AoE. The spell itself only deals damage;
-- the C++ SpellScript applies the shared slow aura (900205) to avoid inheriting any
-- Shadowfury stun aura behavior/client display from the cloned row.
-- Target 16 = TARGET_UNIT_DEST_AREA_ENEMY, radius index 14 (~Shadowfury). Damage overridden in C++.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`,
     `Effect1`, `EffectDieSides1`, `EffectImplicitTargetA1`, `EffectRadiusIndex1`,
     `SpellName`, `SchoolMask`, `DmgClass`)
VALUES
    (@SPELL_ENTROPY_RIFT, 0, 1, 101, 1, 1, 39, 4,
     2, 1, 16, 14,
     'Faille d''entropie', 32, 1);

-- E: Pas du neant - a real Blink (LEAP effect 29, like spell 1953). Slow/instability added in C++.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`,
     `Effect1`, `EffectImplicitTargetA1`, `EffectImplicitTargetB1`, `EffectRadiusIndex1`,
     `SpellName`, `SchoolMask`)
VALUES
    (@SPELL_VOID_STEP, 0, 1, 101, 1, 1, 0, 1,
     29, 1, 55, 9,
     'Pas du neant', 32);

-- R: Cataclysme - DUMMY self; the script consumes the gauge for a PBAoE burst.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`, `Effect1`, `EffectDieSides1`, `EffectImplicitTargetA1`,
     `SpellName`, `SchoolMask`, `DmgClass`)
VALUES
    (@SPELL_CATACLYSM, 0, 1, 101, 1, 1, 0, 1, 3, 1, 1, 'Cataclysme', 32, 1);

-- Stacking mark aura (max 3, 3s). Stacks/detonation handled in C++.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`, `StackAmount`, `Effect1`, `EffectDieSides1`,
     `EffectApplyAuraName1`, `EffectImplicitTargetA1`, `SpellName`, `SchoolMask`, `DmgClass`)
VALUES
    (@SPELL_ENTROPY_MARK, 0, 1, 101, 1, 1, 31, 4, 3, 6, 1, 4, 6, 'Marque d''entropie', 32, 1);

-- Shared slow debuff: -30% movement speed for 1.5s, applied by E (cast triggered).
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`, `Effect1`, `EffectDieSides1`, `EffectBasePoints1`,
     `EffectMechanic1`, `EffectApplyAuraName1`, `EffectImplicitTargetA1`, `SpellName`, `SchoolMask`, `DmgClass`)
VALUES
    (@SPELL_ENTROPY_SLOW, 0, 1, 101, 1, 1, 39, 4, 6, 0, -30, 11, 33, 6, 'Entropie - ralentissement', 32, 1);

-- Passive display entry (Attributes 64 = SPELL_ATTR0_PASSIVE). Mechanic lives in the active scripts;
-- this exists so the passive shows in the spellbook.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`, `Effect1`, `EffectDieSides1`,
     `EffectApplyAuraName1`, `EffectImplicitTargetA1`, `SpellName`, `SchoolMask`)
VALUES
    (@SPELL_ENTROPY_PASSIVE, 64, 1, 0, 1, 1, 0, 1, 6, 1, 4, 1, 'Marque d''entropie', 32);

-- Passive display entry explaining the Instability gauge (mechanic core). Display-only.
INSERT INTO `spell_dbc`
    (`Id`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `BaseLevel`, `SpellLevel`,
     `DurationIndex`, `RangeIndex`, `Effect1`, `EffectDieSides1`,
     `EffectApplyAuraName1`, `EffectImplicitTargetA1`, `SpellName`, `SchoolMask`)
VALUES
    (@SPELL_INSTABILITY_PASSIVE, 64, 1, 0, 1, 1, 0, 1, 6, 1, 4, 1, 'Instabilite', 32);

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
    (@SPELL_ENTROPY_BOLT, 'spell_moba_entropy_bolt'),
    (@SPELL_ENTROPY_BASIC_ATTACK, 'spell_moba_entropy_basic_attack'),
    (@SPELL_ENTROPY_BASIC_ATTACK_VISUAL, 'spell_moba_entropy_basic_attack_visual'),
    (@SPELL_ENTROPY_RIFT, 'spell_moba_entropy_rift'),
    (@SPELL_VOID_STEP,    'spell_moba_void_step'),
    (@SPELL_CATACLYSM,    'spell_moba_cataclysm');
