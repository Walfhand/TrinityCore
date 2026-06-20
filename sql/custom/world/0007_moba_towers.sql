-- MOBA towers: creature templates (Blue/Red) + data-driven positions table.
-- HP/level/faction are tuned at runtime by the npc_moba_tower script; positions live in
-- `moba_tower` (edit + `make db-custom` + restart, no C++ rebuild).

DELETE FROM `creature_template` WHERE `entry` IN (900010, 900011);
INSERT INTO `creature_template`
(`entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`, `KillCredit1`, `KillCredit2`,
 `modelid1`, `modelid2`, `modelid3`, `modelid4`, `name`, `subname`, `IconName`, `gossip_menu_id`,
 `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`,
 `dmgschool`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
 `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`, `lootid`, `pickpocketloot`,
 `skinloot`, `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, `AIName`, `MovementType`, `HoverHeight`,
 `HealthModifier`, `ManaModifier`, `ArmorModifier`, `DamageModifier`, `ExperienceModifier`, `RacialLeader`,
 `movementId`, `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`, `flags_extra`,
 `ScriptName`, `StringId`, `VerifiedBuild`) VALUES
(900010, 0, 0, 0, 0, 0, 1484, 0, 0, 0, 'Blue Tower', 'Prototype Turret', NULL, 0, 1, 1, 0, 14, 0, 1, 1.14286, 2, 0, 0,
 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 'npc_moba_tower', NULL, 0),
(900011, 0, 0, 0, 0, 0, 1484, 0, 0, 0, 'Red Tower', 'Prototype Turret', NULL, 0, 1, 1, 0, 14, 0, 1, 1.14286, 2, 0, 0,
 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 'npc_moba_tower', NULL, 0);

CREATE TABLE IF NOT EXISTS `moba_tower` (
    `mapId` INT UNSIGNED NOT NULL,
    `team`  TINYINT UNSIGNED NOT NULL,   -- 0 = Blue, 1 = Red
    `lane`  INT UNSIGNED NOT NULL,       -- 0/1/2 = lanes, 9 = nexus towers
    `ord`   INT UNSIGNED NOT NULL,       -- 0 = outermost (destroyed first), increasing toward the base
    `x` FLOAT NOT NULL, `y` FLOAT NOT NULL, `z` FLOAT NOT NULL, `o` FLOAT NOT NULL DEFAULT 0,
    PRIMARY KEY (`mapId`, `team`, `lane`, `ord`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Positions added once scouted in-game (.gps), per team/lane in outer->inner order, plus nexus towers (lane 9).
DELETE FROM `moba_tower` WHERE `mapId` = 900;
INSERT INTO `moba_tower` (`mapId`, `team`, `lane`, `ord`, `x`, `y`, `z`, `o`) VALUES
-- Mid lane, Blue (ord 0 = outer, toward center)
(900, 0, 0, 0, 3234.022217, 2099.771729, 5.267854, 2.350700),
(900, 0, 0, 1, 3268.179932, 2065.322510, 6.509532, 2.402536),
(900, 0, 0, 2, 3302.115723, 2032.028687, 7.584385, 2.369549),
-- Mid lane, Red (ord 0 = outer, toward center)
(900, 1, 0, 0, 3167.129883, 2167.021973, 3.757686, 2.284726),
(900, 1, 0, 1, 3134.163330, 2198.048340, 4.080302, 2.378974),
(900, 1, 0, 2, 3099.868164, 2233.228027, 3.002185, 2.301219);
