#1392832626
ALTER TABLE `mail` ADD `bound` tinyint(1) unsigned NOT NULL default '0';
INSERT INTO `sql_updates` (`timestamp`) VALUES (1392832626);

/* NOTE: Use this code in the logs database because the codes above are use in main database
ALTER TABLE `picklog` ADD `bound` tinyint(1) unsigned NOT NULL default '0';
 */
