-- MOBA towers: creature templates (Blue/Red) + data-driven positions table.
-- HP/level/faction are tuned at runtime by the npc_moba_tower script; positions live in
-- `moba_tower` (edit + `make db-custom` + restart, no C++ rebuild).

DELETE FROM `creature_template` WHERE `entry` IN (900010, 900011, 900012);
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
(900010, 0, 0, 0, 0, 0, 60001, 0, 0, 0, 'Blue Tower', 'Prototype Turret', NULL, 0, 1, 1, 0, 14, 0, 1, 1.14286, 0.5, 0, 0,
 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 'npc_moba_tower', NULL, 0),
(900011, 0, 0, 0, 0, 0, 60002, 0, 0, 0, 'Red Tower', 'Prototype Turret', NULL, 0, 1, 1, 0, 14, 0, 1, 1.14286, 0.8, 0, 0,
 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 'npc_moba_tower', NULL, 0),
-- Tiny shot emitter: spawned at each tower's top by SpawnTower. It deliberately uses a real humanoid
-- display (49) scaled down instead of an invisible model: missile visuals need a client-visible caster
-- with normal attachment points. Runtime code still makes it passive/non-attackable.
(900012, 0, 0, 0, 0, 0, 49, 0, 0, 0, 'Tower Muzzle', '', NULL, 0, 1, 1, 0, 35, 0, 1, 1.14286, 0.01, 0, 0,
 2000, 2000, 1, 1, 1, 33555202, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, '', NULL, 0);

CREATE TABLE IF NOT EXISTS `moba_tower` (
    `mapId` INT UNSIGNED NOT NULL,
    `team`  TINYINT UNSIGNED NOT NULL,   -- 0 = Blue, 1 = Red
    `lane`  INT UNSIGNED NOT NULL,       -- 0/1/2 = lanes, 9 = nexus towers
    `ord`   INT UNSIGNED NOT NULL,       -- 0 = outermost (destroyed first), increasing toward the base
    `x` FLOAT NOT NULL, `y` FLOAT NOT NULL, `z` FLOAT NOT NULL, `o` FLOAT NOT NULL DEFAULT 0,
    PRIMARY KEY (`mapId`, `team`, `lane`, `ord`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `moba_tower_muzzle` (
    `team` TINYINT UNSIGNED NOT NULL,     -- 0 = Blue, 1 = Red
    `dz`   FLOAT NOT NULL,                -- vertical offset from tower base to projectile emitter
    PRIMARY KEY (`team`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `moba_tower_muzzle`;
INSERT INTO `moba_tower_muzzle` (`team`, `dz`) VALUES
(0, 9.0),
(1, 9.0);

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
(900, 1, 0, 2, 3099.868164, 2233.228027, 3.002185, 2.301219),
-- Bot lane, Blue (ord 0 = outer)
(900, 0, 2, 0, 3359.132812, 2196.729492, 0.602839, 1.774220),
(900, 0, 2, 1, 3359.267822, 2131.952881, 1.010654, 1.562162),
(900, 0, 2, 2, 3352.971436, 2046.918457, 4.418725, 1.496189),
-- Bot lane, Red (ord 0 = outer)
(900, 1, 2, 0, 3266.476807, 2292.348633, 3.113901, 3.176156),
(900, 1, 2, 1, 3202.049316, 2291.308594, 1.847207, 3.166731),
(900, 1, 2, 2, 3134.850586, 2292.925537, 1.583117, 3.074839),
-- Top lane, Blue (ord 0 = outer)
(900, 0, 1, 0, 3136.085449, 1987.868164, 3.030211, 3.069337),
(900, 0, 1, 1, 3203.080566, 1986.201538, 6.967173, 3.085830),
(900, 0, 1, 2, 3288.046387, 1985.777588, 6.283791, 3.061485),
-- Top lane, Red (ord 0 = outer)
(900, 1, 1, 0, 3038.274902, 2067.944092, 3.582154, 1.638341),
(900, 1, 1, 1, 3037.956543, 2132.352295, 1.908234, 1.515033),
(900, 1, 1, 2, 3042.688477, 2228.635986, 0.531360, 1.359524),
-- Nexus towers (lane 9): 2 per team, guarding the nexus
(900, 0, 9, 0, 3321.654785, 1989.809814, 8.074040, 2.360120),
(900, 0, 9, 1, 3348.795410, 2006.367920, 4.662856, 1.809555),
(900, 1, 9, 0, 3071.308350, 2277.433594, 6.319172, 5.389400),
(900, 1, 9, 1, 3043.724121, 2262.382568, 1.652149, 4.819986);

-- Building-model display info for the custom tower displays (60001/60002). The server validates
-- creature_template.modelid1 against creature_model_info (bounding radius + combat reach), independent
-- of the client DBC. Slightly larger reach than the old turret so attackers stop at the tower base.
DELETE FROM `creature_model_info` WHERE `DisplayID` IN (60001, 60002);
INSERT INTO `creature_model_info`
(`DisplayID`, `BoundingRadius`, `CombatReach`, `Gender`, `DisplayID_Other_Gender`) VALUES
(60001, 3, 4, 2, 0),
(60002, 3, 4, 2, 0);
