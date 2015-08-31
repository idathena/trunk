#1396893866
ALTER TABLE `bonus_script` MODIFY `char_id` int(11) unsigned NOT NULL;
ALTER TABLE `bonus_script` MODIFY `script` text NOT NULL;
ALTER TABLE `bonus_script` MODIFY `tick` int(11) unsigned NOT NULL default '0';
ALTER TABLE `bonus_script` MODIFY `flag` smallint(5) unsigned NOT NULL default '0';
ALTER TABLE `bonus_script` MODIFY `type` tinyint(1) unsigned NOT NULL default '0';
ALTER TABLE `bonus_script` MODIFY `icon` smallint(3) NOT NULL default '-1';
INSERT INTO `sql_updates` (`timestamp`) VALUES (1396893866);
