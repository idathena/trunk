#1382892428
ALTER TABLE `account_data` ADD `base_exp` tinyint(4) unsigned NOT NULL default '0';
ALTER TABLE `account_data` ADD `base_drop` tinyint(4) unsigned NOT NULL default '0';
ALTER TABLE `account_data` ADD `base_death` tinyint(4) unsigned NOT NULL default '0';
INSERT INTO `sql_updates` (`timestamp`) VALUES (1382892428);