#1398477600
CREATE TABLE IF NOT EXISTS `markets` (
  `name` varchar(32) NOT NULL default '',
  `nameid` smallint(5) unsigned NOT NULL,
  `price` int(11) unsigned NOT NULL,
  `amount` smallint(5) unsigned NOT NULL,
  `flag` tinyint(2) unsigned NOT NULL default '0',
  PRIMARY KEY (`name`,`nameid`)
) ENGINE=MyISAM default CHARSET=latin1;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1398477600);
