#1362794218
ALTER TABLE `char` ADD COLUMN `moves` int(11) unsigned NOT NULL DEFAULT '0';
INSERT INTO `sql_updates` (`timestamp`) VALUES (1362794218);