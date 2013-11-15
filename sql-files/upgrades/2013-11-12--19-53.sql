#1383162785
ALTER TABLE `account_data` MODIFY `base_exp` tinyint(4) unsigned NOT NULL default '100';
ALTER TABLE `account_data` MODIFY `base_drop` tinyint(4) unsigned NOT NULL default '100';
ALTER TABLE `account_data` MODIFY `base_death` tinyint(4) unsigned NOT NULL default '100';
INSERT INTO `sql_updates` (`timestamp`) VALUES (1383162785);