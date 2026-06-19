-- Guerilla is declared as MAP_BATTLEGROUND in Map.dbc.
-- Battleground/arena-style maps are instanced by Map.dbc type and do not use
-- `instance_template`, which is for dungeon/raid maps.
DELETE FROM `instance_template` WHERE `map` = 900;
