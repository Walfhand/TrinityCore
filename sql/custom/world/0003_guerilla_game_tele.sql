DELETE FROM `game_tele` WHERE `id` = 900900 OR `name` = 'guerilla';

INSERT INTO `game_tele` (
    `id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`
) VALUES (
    900900, 3200.0, 2133.33, 100.0, 0.0, 900, 'guerilla'
);
