-- New characters spawn directly on the Darkmoon Faire grounds in Elwynn Forest (map 0, zone 12), the
-- out-of-match holding area (same spot as Moba::TeleportToLobby). Cleaner than teleporting after login:
-- the character is created at the right place, with no positioning race / limbo on first entry. The lobby
-- fence still handles match exits and reconnections from elsewhere.
UPDATE `playercreateinfo`
SET `map` = 0,
    `zone` = 12,
    `position_x` = -9560.0,
    `position_y` = 80.0,
    `position_z` = 59.0,
    `orientation` = 1.0;
