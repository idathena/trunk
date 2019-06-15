#1560073680
ALTER TABLE `elemental`
	CHANGE COLUMN `atk1` `atk` MEDIUMint(6) unsigned NOT NULL default '0',
	DROP COLUMN `atk2`,
	CHANGE COLUMN `class` `kind` smallint(1) NOT NULL,
	CHANGE COLUMN `mode` `scale` smallint(1) NOT NULL;

--
-- Table structure for table `elemental_sc`
--

CREATE TABLE IF NOT EXISTS `elemental_sc` (
  `ele_id` int(11) unsigned NOT NULL,
  `char_id` int(11) unsigned NOT NULL,
  `type` smallint(11) unsigned NOT NULL,
  `tick` int(11) NOT NULL,
  `val1` int(11) NOT NULL default '0',
  `val2` int(11) NOT NULL default '0',
  `val3` int(11) NOT NULL default '0',
  `val4` int(11) NOT NULL default '0',
  KEY (`ele_id`),
  KEY (`char_id`),
  PRIMARY KEY (`ele_id`,`char_id`, `type`)
) ENGINE=MyISAM;

INSERT INTO `sql_updates` (`timestamp`) VALUES (1560073680);
