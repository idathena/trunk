#1457638175
CREATE TABLE IF NOT EXISTS `sales` (
  `nameid` smallint(5) unsigned NOT NULL,
  `start` datetime NOT NULL,
  `end` datetime NOT NULL,
  `amount` int(11) NOT NULL,
  PRIMARY KEY (`nameid`)
) ENGINE=MyISAM;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1457638175);
