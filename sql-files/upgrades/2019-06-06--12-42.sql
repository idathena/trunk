#1559824920
ALTER TABLE `inventory` ADD COLUMN `equip_switch` int(11) unsigned NOT NULL default '0' AFTER `unique_id`;

INSERT INTO `sql_updates` (`timestamp`) VALUES (1559824920);
