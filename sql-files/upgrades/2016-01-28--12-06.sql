#1435860840
ALTER TABLE `char` ADD `body` smallint(5) unsigned NOT NULL default '0' AFTER `clothes_color`;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1435860840);
