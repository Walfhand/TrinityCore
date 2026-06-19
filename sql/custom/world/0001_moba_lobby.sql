SET @ENTRY := 900000;

DELETE FROM `creature` WHERE `id` = @ENTRY;
DELETE FROM `creature_template` WHERE `entry` = @ENTRY;
DELETE FROM `creature` WHERE `id` = 900001;
DELETE FROM `creature_template` WHERE `entry` = 900001;
DELETE FROM `creature` WHERE `id` = 900002;
DELETE FROM `creature_template` WHERE `entry` = 900002;
DELETE FROM `creature` WHERE `id` = 900003;
DELETE FROM `creature_template` WHERE `entry` = 900003;
DELETE FROM `creature` WHERE `id` = 900004;
DELETE FROM `creature_template` WHERE `entry` = 900004;

INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `modelid1`, `modelid2`, `modelid3`, `modelid4`,
    `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`,
    `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`, `dmgschool`,
    `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
    `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`,
    `lootid`, `pickpocketloot`, `skinloot`, `PetSpellDataId`, `VehicleId`, `mingold`,
    `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`,
    `ArmorModifier`, `DamageModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
    `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`, `flags_extra`,
    `ScriptName`, `StringId`, `VerifiedBuild`
) VALUES (
    @ENTRY, 0, 0, 0,
    0, 0, 1484, 0, 0, 0,
    'Moba Lobby', 'Prototype Hero Selector', NULL, 0, 80, 80, 0,
    35, 1, 1, 1.14286, 1, 0, 0,
    2000, 2000, 1, 1, 1,
    0, 0, 0, 0, 7, 0,
    0, 0, 0, 0, 0, 0,
    0, '', 0, 1, 1, 1,
    1, 1, 1, 0, 0,
    1, 0, 0, 0,
    'npc_moba_lobby', NULL, 0
);

INSERT INTO `creature` (
    `id`, `map`, `zoneId`, `areaId`, `spawnMask`, `phaseMask`, `modelid`, `equipment_id`,
    `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`,
    `wander_distance`, `currentwaypoint`, `curhealth`, `curmana`, `MovementType`,
    `npcflag`, `unit_flags`, `dynamicflags`, `ScriptName`, `StringId`, `VerifiedBuild`
) VALUES (
    @ENTRY, 1, 876, 876, 1, 1, 0, 0,
    16222.1, 16265.9, 13.2, 1.6, 300,
    0, 0, 1000, 0, 0,
    0, 0, 0, '', NULL, 0
);

INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `modelid1`, `modelid2`, `modelid3`, `modelid4`,
    `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`,
    `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`, `dmgschool`,
    `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
    `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`,
    `lootid`, `pickpocketloot`, `skinloot`, `PetSpellDataId`, `VehicleId`, `mingold`,
    `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`,
    `ArmorModifier`, `DamageModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
    `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`, `flags_extra`,
    `ScriptName`, `StringId`, `VerifiedBuild`
) VALUES (
    900001, 0, 0, 0,
    0, 0, 1484, 0, 0, 0,
    'Blue Nexus', 'Prototype Win Condition', NULL, 0, 80, 80, 0,
    14, 0, 1, 1.14286, 3, 0, 0,
    2000, 2000, 1, 1, 1,
    0, 0, 0, 0, 7, 0,
    0, 0, 0, 0, 0, 0,
    0, '', 0, 1, 120, 1,
    1, 0.1, 1, 0, 0,
    1, 0, 0, 0,
    'npc_moba_nexus', NULL, 0
);

INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `modelid1`, `modelid2`, `modelid3`, `modelid4`,
    `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`,
    `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`, `dmgschool`,
    `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
    `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`,
    `lootid`, `pickpocketloot`, `skinloot`, `PetSpellDataId`, `VehicleId`, `mingold`,
    `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`,
    `ArmorModifier`, `DamageModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
    `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`, `flags_extra`,
    `ScriptName`, `StringId`, `VerifiedBuild`
) VALUES (
    900003, 0, 0, 0,
    0, 0, 1484, 0, 0, 0,
    'Blue Minion', 'Prototype Lane Unit', NULL, 0, 10, 10, 0,
    14, 0, 1, 1.14286, 0.8, 0, 0,
    2000, 2000, 1, 1, 1,
    0, 0, 0, 0, 7, 0,
    0, 0, 0, 0, 0, 0,
    0, '', 0, 1, 4, 1,
    1, 0.5, 1, 0, 0,
    1, 0, 0, 0,
    'npc_moba_minion', NULL, 0
);

INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `modelid1`, `modelid2`, `modelid3`, `modelid4`,
    `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`,
    `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`, `dmgschool`,
    `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
    `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`,
    `lootid`, `pickpocketloot`, `skinloot`, `PetSpellDataId`, `VehicleId`, `mingold`,
    `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`,
    `ArmorModifier`, `DamageModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
    `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`, `flags_extra`,
    `ScriptName`, `StringId`, `VerifiedBuild`
) VALUES (
    900004, 0, 0, 0,
    0, 0, 1484, 0, 0, 0,
    'Red Minion', 'Prototype Lane Unit', NULL, 0, 10, 10, 0,
    14, 0, 1, 1.14286, 0.8, 0, 0,
    2000, 2000, 1, 1, 1,
    0, 0, 0, 0, 7, 0,
    0, 0, 0, 0, 0, 0,
    0, '', 0, 1, 4, 1,
    1, 0.5, 1, 0, 0,
    1, 0, 0, 0,
    'npc_moba_minion', NULL, 0
);

INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `modelid1`, `modelid2`, `modelid3`, `modelid4`,
    `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`,
    `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`, `dmgschool`,
    `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
    `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`,
    `lootid`, `pickpocketloot`, `skinloot`, `PetSpellDataId`, `VehicleId`, `mingold`,
    `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`,
    `ArmorModifier`, `DamageModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
    `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`, `flags_extra`,
    `ScriptName`, `StringId`, `VerifiedBuild`
) VALUES (
    900002, 0, 0, 0,
    0, 0, 1484, 0, 0, 0,
    'Red Nexus', 'Prototype Win Condition', NULL, 0, 80, 80, 0,
    14, 0, 1, 1.14286, 3, 0, 0,
    2000, 2000, 1, 1, 1,
    0, 0, 0, 0, 7, 0,
    0, 0, 0, 0, 0, 0,
    0, '', 0, 1, 120, 1,
    1, 0.1, 1, 0, 0,
    1, 0, 0, 0,
    'npc_moba_nexus', NULL, 0
);
