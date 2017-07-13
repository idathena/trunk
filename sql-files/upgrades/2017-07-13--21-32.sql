#1477434595
ALTER TABLE `guild` ADD COLUMN `last_master_change` datetime;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1477434595);
