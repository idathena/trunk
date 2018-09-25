#1536825360
ALTER TABLE `guild_position` MODIFY COLUMN `mode` smallint(11) unsigned NOT NULL default '0';

INSERT INTO `sql_updates` (`timestamp`) VALUES (1536825360);
