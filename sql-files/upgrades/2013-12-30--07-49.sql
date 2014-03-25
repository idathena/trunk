#1383205740
ALTER TABLE `bonus_script` MODIFY `tick` int(11) NOT NULL default '0';
ALTER TABLE `bonus_script` ADD `icon` smallint(4) NOT NULL default '-1';
INSERT INTO `sql_updates` (`timestamp`) VALUES (1383205740);