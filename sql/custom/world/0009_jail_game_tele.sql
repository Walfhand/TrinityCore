-- Out-of-match holding area: the Darkmoon Faire grounds in Elwynn Forest (map 0), south-east of Goldshire.
-- Players are sent here on login outside a match and when a match ends (see Moba::TeleportToLobby), and a
-- software fence (Moba::EnforceLobbyFence) leashes them to the grounds. Registered as a .tele target too.
DELETE FROM `game_tele` WHERE `id` = 900901 OR `name` IN ('jail', 'foire');

INSERT INTO `game_tele` (
    `id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`
) VALUES (
    900901, -9560.0, 80.0, 59.0, 1.0, 0, 'foire'
);
