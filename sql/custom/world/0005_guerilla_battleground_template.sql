DELETE FROM `battleground_template` WHERE `ID` = 12;

INSERT INTO `battleground_template` (
    `ID`, `MinPlayersPerTeam`, `MaxPlayersPerTeam`, `MinLvl`, `MaxLvl`,
    `AllianceStartLoc`, `AllianceStartO`, `HordeStartLoc`, `HordeStartO`,
    `StartMaxDist`, `Weight`, `ScriptName`, `Comment`
) VALUES (
    12, 1, 5, 1, 80,
    900901, 0.785398, 900902, 3.926991,
    0, 1, '', 'Guerilla MOBA'
);
