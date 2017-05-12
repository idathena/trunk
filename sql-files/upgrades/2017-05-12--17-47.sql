#1467934919
ALTER TABLE `char` ADD COLUMN `last_login` datetime DEFAULT NULL AFTER `clan_id`;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1467934919);
