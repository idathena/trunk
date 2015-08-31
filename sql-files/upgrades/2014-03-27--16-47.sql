#1384545461
ALTER TABLE `charlog` ADD `char_id` int(11) unsigned NOT NULL default '0' AFTER `account_id`;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1384545461);