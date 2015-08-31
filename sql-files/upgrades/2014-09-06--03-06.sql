#1388854043
UPDATE `picklog` SET `card0` = 256 WHERE `card0` = -256;

ALTER TABLE `picklog` MODIFY `nameid` smallint(5) unsigned NOT NULL default '0';
ALTER TABLE `picklog` MODIFY `card0` smallint(5) unsigned NOT NULL default '0';
ALTER TABLE `picklog` MODIFY `card1` smallint(5) unsigned NOT NULL default '0';
ALTER TABLE `picklog` MODIFY `card2` smallint(5) unsigned NOT NULL default '0';
ALTER TABLE `picklog` MODIFY `card3` smallint(5) unsigned NOT NULL default '0';
ALTER TABLE `mvplog` MODIFY `prize` smallint(5) unsigned NOT NULL default '0';

/* NOTE: Use this code in the main database because the codes above are use in logs database
INSERT INTO `sql_updates` (`timestamp`) VALUES (1388854043);
 */
