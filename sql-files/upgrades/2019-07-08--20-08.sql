#1562616480
ALTER TABLE `markets` MODIFY `amount` int(11) unsigned NOT NULL default '0';

INSERT INTO `sql_updates` (`timestamp`) VALUES (1562616480);
