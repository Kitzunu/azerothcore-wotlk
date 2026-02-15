-- Add Arena Spectator commands to database
-- This enables the commands to show in .help and be properly recognized

DELETE FROM `command` WHERE `name` IN (
    'spect',
    'spect version',
    'spect reset',
    'spect spectate',
    'spect watch',
    'spect leave'
);

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('spect', 0, 'Syntax: .spect $subcommand\nType .spect to see the list of possible subcommands or .help spect $subcommand to see info on subcommands.'),
('spect version', 0, 'Syntax: .spect version #version\nVerify addon version for arena spectating.'),
('spect reset', 0, 'Syntax: .spect reset\nReset various values related to spectating.'),
('spect spectate', 0, 'Syntax: .spect spectate #name\nBegin spectating the given player in an arena match.'),
('spect watch', 0, 'Syntax: .spect watch #name\nSwitch to watching a different player in the same arena.'),
('spect leave', 0, 'Syntax: .spect leave\nLeave the arena you are currently spectating.');

