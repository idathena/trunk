#1467935469
/* NOTE: Use this code in the logs database because the code below are use in main database
ALTER TABLE `picklog`
	ADD COLUMN `option_id0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `card3`,
	ADD COLUMN `option_val0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id0`,
	ADD COLUMN `option_parm0` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val0`,
	ADD COLUMN `option_id1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm0`,
	ADD COLUMN `option_val1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id1`,
	ADD COLUMN `option_parm1` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val1`,
	ADD COLUMN `option_id2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm1`,
	ADD COLUMN `option_val2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id2`,
	ADD COLUMN `option_parm2` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val2`,
	ADD COLUMN `option_id3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm2`,
	ADD COLUMN `option_val3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id3`,
	ADD COLUMN `option_parm3` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val3`,
	ADD COLUMN `option_id4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm3`,
	ADD COLUMN `option_val4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id4`,
	ADD COLUMN `option_parm4` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val4`;

ALTER TABLE `mvplog` MODIFY COLUMN `mvpexp` bigint(20) unsigned NOT NULL default '0';
*/

ALTER TABLE `auction`
	ADD COLUMN `option_id0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `card3`,
	ADD COLUMN `option_val0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id0`,
	ADD COLUMN `option_parm0` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val0`,
	ADD COLUMN `option_id1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm0`,
	ADD COLUMN `option_val1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id1`,
	ADD COLUMN `option_parm1` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val1`,
	ADD COLUMN `option_id2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm1`,
	ADD COLUMN `option_val2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id2`,
	ADD COLUMN `option_parm2` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val2`,
	ADD COLUMN `option_id3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm2`,
	ADD COLUMN `option_val3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id3`,
	ADD COLUMN `option_parm3` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val3`,
	ADD COLUMN `option_id4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm3`,
	ADD COLUMN `option_val4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id4`,
	ADD COLUMN `option_parm4` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val4`;

ALTER TABLE `cart_inventory`
	ADD COLUMN `option_id0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `card3`,
	ADD COLUMN `option_val0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id0`,
	ADD COLUMN `option_parm0` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val0`,
	ADD COLUMN `option_id1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm0`,
	ADD COLUMN `option_val1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id1`,
	ADD COLUMN `option_parm1` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val1`,
	ADD COLUMN `option_id2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm1`,
	ADD COLUMN `option_val2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id2`,
	ADD COLUMN `option_parm2` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val2`,
	ADD COLUMN `option_id3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm2`,
	ADD COLUMN `option_val3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id3`,
	ADD COLUMN `option_parm3` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val3`,
	ADD COLUMN `option_id4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm3`,
	ADD COLUMN `option_val4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id4`,
	ADD COLUMN `option_parm4` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val4`;

ALTER TABLE `guild_storage`
	ADD COLUMN `option_id0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `card3`,
	ADD COLUMN `option_val0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id0`,
	ADD COLUMN `option_parm0` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val0`,
	ADD COLUMN `option_id1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm0`,
	ADD COLUMN `option_val1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id1`,
	ADD COLUMN `option_parm1` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val1`,
	ADD COLUMN `option_id2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm1`,
	ADD COLUMN `option_val2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id2`,
	ADD COLUMN `option_parm2` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val2`,
	ADD COLUMN `option_id3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm2`,
	ADD COLUMN `option_val3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id3`,
	ADD COLUMN `option_parm3` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val3`,
	ADD COLUMN `option_id4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm3`,
	ADD COLUMN `option_val4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id4`,
	ADD COLUMN `option_parm4` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val4`;
	
ALTER TABLE `inventory`
	ADD COLUMN `option_id0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `card3`,
	ADD COLUMN `option_val0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id0`,
	ADD COLUMN `option_parm0` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val0`,
	ADD COLUMN `option_id1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm0`,
	ADD COLUMN `option_val1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id1`,
	ADD COLUMN `option_parm1` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val1`,
	ADD COLUMN `option_id2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm1`,
	ADD COLUMN `option_val2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id2`,
	ADD COLUMN `option_parm2` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val2`,
	ADD COLUMN `option_id3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm2`,
	ADD COLUMN `option_val3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id3`,
	ADD COLUMN `option_parm3` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val3`,
	ADD COLUMN `option_id4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm3`,
	ADD COLUMN `option_val4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id4`,
	ADD COLUMN `option_parm4` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val4`;
	
ALTER TABLE `mail`
	ADD COLUMN `option_id0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `card3`,
	ADD COLUMN `option_val0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id0`,
	ADD COLUMN `option_parm0` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val0`,
	ADD COLUMN `option_id1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm0`,
	ADD COLUMN `option_val1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id1`,
	ADD COLUMN `option_parm1` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val1`,
	ADD COLUMN `option_id2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm1`,
	ADD COLUMN `option_val2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id2`,
	ADD COLUMN `option_parm2` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val2`,
	ADD COLUMN `option_id3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm2`,
	ADD COLUMN `option_val3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id3`,
	ADD COLUMN `option_parm3` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val3`,
	ADD COLUMN `option_id4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm3`,
	ADD COLUMN `option_val4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id4`,
	ADD COLUMN `option_parm4` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val4`;
	
ALTER TABLE `storage`
	ADD COLUMN `option_id0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `card3`,
	ADD COLUMN `option_val0` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id0`,
	ADD COLUMN `option_parm0` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val0`,
	ADD COLUMN `option_id1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm0`,
	ADD COLUMN `option_val1` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id1`,
	ADD COLUMN `option_parm1` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val1`,
	ADD COLUMN `option_id2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm1`,
	ADD COLUMN `option_val2` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id2`,
	ADD COLUMN `option_parm2` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val2`,
	ADD COLUMN `option_id3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm2`,
	ADD COLUMN `option_val3` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id3`,
	ADD COLUMN `option_parm3` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val3`,
	ADD COLUMN `option_id4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_parm3`,
	ADD COLUMN `option_val4` SMALLINT(5) UNSIGNED NOT NULL default '0' AFTER `option_id4`,
	ADD COLUMN `option_parm4` TINYINT(3) UNSIGNED NOT NULL default '0' AFTER `option_val4`;

CREATE TABLE IF NOT EXISTS `storage_1` (
  `id` int(11) unsigned NOT NULL auto_increment,
  `account_id` int(11) unsigned NOT NULL default '0',
  `nameid` smallint(5) unsigned NOT NULL default '0',
  `amount` smallint(11) unsigned NOT NULL default '0',
  `equip` int(11) unsigned NOT NULL default '0',
  `identify` smallint(6) unsigned NOT NULL default '0',
  `refine` tinyint(3) unsigned NOT NULL default '0',
  `attribute` tinyint(4) unsigned NOT NULL default '0',
  `card0` smallint(5) unsigned NOT NULL default '0',
  `card1` smallint(5) unsigned NOT NULL default '0',
  `card2` smallint(5) unsigned NOT NULL default '0',
  `card3` smallint(5) unsigned NOT NULL default '0',
  `option_id0` smallint(5) unsigned NOT NULL default '0',
  `option_val0` smallint(5) unsigned NOT NULL default '0',
  `option_parm0` tinyint(3) unsigned NOT NULL default '0',
  `option_id1` smallint(5) unsigned NOT NULL default '0',
  `option_val1` smallint(5) unsigned NOT NULL default '0',
  `option_parm1` tinyint(3) unsigned NOT NULL default '0',
  `option_id2` smallint(5) unsigned NOT NULL default '0',
  `option_val2` smallint(5) unsigned NOT NULL default '0',
  `option_parm2` tinyint(3) unsigned NOT NULL default '0',
  `option_id3` smallint(5) unsigned NOT NULL default '0',
  `option_val3` smallint(5) unsigned NOT NULL default '0',
  `option_parm3` tinyint(3) unsigned NOT NULL default '0',
  `option_id4` smallint(5) unsigned NOT NULL default '0',
  `option_val4` smallint(5) unsigned NOT NULL default '0',
  `option_parm4` tinyint(3) unsigned NOT NULL default '0',
  `expire_time` int(11) unsigned NOT NULL default '0',
  `bound` tinyint(3) unsigned NOT NULL default '0',
  `unique_id` bigint(20) unsigned NOT NULL default '0',
  PRIMARY KEY  (`id`),
  KEY `account_id` (`account_id`)
) ENGINE=MyISAM;

ALTER TABLE `homunculus` MODIFY COLUMN `exp` bigint(20) unsigned NOT NULL default '0';
INSERT INTO `sql_updates` (`timestamp`) VALUES (1467935469);
