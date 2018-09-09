#1536064380
ALTER TABLE `homunculus` ADD COLUMN `autofeed` tinyint(2) NOT NULL default '0' AFTER `vaporize`;
ALTER TABLE `pet` ADD COLUMN `autofeed` tinyint(2) NOT NULL default '0' AFTER `incubate`;

INSERT INTO `sql_updates` (`timestamp`) VALUES (1536064380);

/* NOTE: Use this code in the logs database because the code above are use in main database
CREATE TABLE IF NOT EXISTS `feedinglog` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `time` datetime NOT NULL default '0000-00-00 00:00:00',
  `char_id` int(11) NOT NULL,
  `target_id` int(11) NOT NULL,
  `target_class` smallint(11) NOT NULL,
  `type` ENUM('P','H','O') NOT NULL, -- P: Pet, H: Homunculus, O: Other
  `intimacy` int(11) unsigned NOT NULL,
  `item_id` smallint(5) unsigned NOT NULL,
  `map` varchar(11) NOT NULL,
  `x` smallint(5) unsigned NOT NULL,
  `y` smallint(5) unsigned NOT NULL,
  PRIMARY KEY  (`id`)
) ENGINE=MyISAM AUTO_INCREMENT=1;
 */
