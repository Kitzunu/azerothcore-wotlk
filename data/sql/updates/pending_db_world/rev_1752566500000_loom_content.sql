--
-- Loom demo content: index rows pointing at the .loom files under the Loom content root.
-- The scripts themselves live in data/loom/scripts/ ; threads in data/loom/threads/ (auto-scanned).

DELETE FROM `creature_loom` WHERE (`entryorguid`=6 AND `source_type`=0) OR (`entryorguid`=30 AND `source_type`=0) OR (`entryorguid`=46 AND `source_type`=0) OR (`entryorguid`=103 AND `source_type`=0) OR (`entryorguid`=197 AND `source_type`=0) OR (`entryorguid`=352 AND `source_type`=0) OR (`entryorguid`=412 AND `source_type`=0) OR (`entryorguid`=597 AND `source_type`=0) OR (`entryorguid`=1449 AND `source_type`=0) OR (`entryorguid`=2060 AND `source_type`=0);
INSERT INTO `creature_loom` (`entryorguid`, `source_type`, `file`, `comment`) VALUES
(6, 0, 'scripts/6_kobold_vermin.loom', 'Kobold Vermin'),
(30, 0, 'scripts/30_forest_spider.loom', 'Forest Spider'),
(46, 0, 'scripts/46_murloc_forager.loom', 'Murloc Forager'),
(103, 0, 'scripts/103_garrick_padfoot.loom', 'Garrick Padfoot'),
(197, 0, 'scripts/197_marshal_mcbride.loom', 'Marshal McBride'),
(352, 0, 'scripts/352_dungar_longdrink.loom', 'Dungar Longdrink'),
(412, 0, 'scripts/412_stitches.loom', 'Stitches'),
(597, 0, 'scripts/597_bloodscalp_berserker.loom', 'Bloodscalp Berserker'),
(1449, 0, 'scripts/1449_witch_doctor_unbagwa.loom', 'Witch Doctor Unbagwa'),
(2060, 0, 'scripts/2060_councilman_smithers.loom', 'Councilman Smithers');
