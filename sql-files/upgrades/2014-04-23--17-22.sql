#1384588175
ALTER TABLE `char` ADD `uniqueitem_counter` bigint(20) unsigned NOT NULL default '0' AFTER `unban_time`;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1384588175);