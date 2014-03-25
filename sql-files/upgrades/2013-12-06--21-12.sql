#1383167577
ALTER TABLE `login` ADD `bank_vault` int(11) NOT NULL default '0';
UPDATE `login` INNER JOIN `account_data` ON login.account_id = account_data.account_id SET login.bank_vault = account_data.bank_vault;
DROP TABLE `account_data`;
CREATE TABLE IF NOT EXISTS `bonus_script` (
	`char_id` int(11) NOT NULL,
	`script` varchar(1024) NOT NULL,
	`tick` int(11) NOT NULL,
	`flag` tinyint(3) unsigned NOT NULL default '0',
	`type` tinyint(1) unsigned NOT NULL default '0'
) ENGINE=InnoDB default CHARSET=latin1;
ALTER TABLE `inventory` MODIFY `equip` int(11) unsigned NOT NULL default '0';
ALTER TABLE `storage` MODIFY `equip` int(11) unsigned NOT NULL default '0';
ALTER TABLE `cart_inventory` MODIFY `equip` int(11) unsigned NOT NULL default '0';
ALTER TABLE `guild_storage` MODIFY `equip` int(11) unsigned NOT NULL default '0';
ALTER TABLE `login` ADD `vip_time` int(11) unsigned NULL default '0';
ALTER TABLE `login` ADD `old_group` tinyint(3) NOT NULL default '0';
ALTER TABLE `char` ADD `unban_time` int(11) unsigned NOT NULL default '0';
-- Apply this if you using item_db.sql and item_db_re.sql
ALTER TABLE `item_db` MODIFY `equip_locations` mediumint(7) unsigned NULL default NULL;
ALTER TABLE `item_db_re` MODIFY `equip_locations` mediumint(7) unsigned NULL default NULL;
ALTER TABLE `item_db2` MODIFY `equip_locations` mediumint(7) unsigned NULL default NULL;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1383167577);