#1475526420
-- ----------------------------
-- Table structure for `mail_attachments`
-- ----------------------------
CREATE TABLE IF NOT EXISTS `mail_attachments` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `index` smallint(5) unsigned NOT NULL DEFAULT '0',
  `nameid` smallint(5) unsigned NOT NULL DEFAULT '0',
  `amount` int(11) unsigned NOT NULL DEFAULT '0',
  `refine` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `attribute` tinyint(4) unsigned NOT NULL DEFAULT '0',
  `identify` smallint(6) NOT NULL DEFAULT '0',
  `card0` smallint(5) unsigned NOT NULL DEFAULT '0',
  `card1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `card2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `card3` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_id0` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_val0` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_parm0` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `option_id1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_val1` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_parm1` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `option_id2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_val2` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_parm2` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `option_id3` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_val3` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_parm3` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `option_id4` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_val4` smallint(5) unsigned NOT NULL DEFAULT '0',
  `option_parm4` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `unique_id` bigint(20) unsigned NOT NULL DEFAULT '0',
  `bound` tinyint(1) unsigned NOT NULL DEFAULT '0',
   PRIMARY KEY (`id`,`index`)
) ENGINE=MyISAM;

INSERT INTO `mail_attachments`
(`id`,`index`,`nameid`,`amount`,`refine`,`attribute`,`identify`,`card0`,`card1`,`card2`,`card3`,`option_id0`,`option_val0`,`option_parm0`,`option_id1`,`option_val1`,`option_parm1`,`option_id2`,`option_val2`,`option_parm2`,`option_id3`,`option_val3`,`option_parm3`,`option_id4`,`option_val4`,`option_parm4`,`unique_id`,`bound`)
SELECT
`id`,'0',`nameid`,`amount`,`refine`,`attribute`,`identify`,`card0`,`card1`,`card2`,`card3`,`option_id0`,`option_val0`,`option_parm0`,`option_id1`,`option_val1`,`option_parm1`,`option_id2`,`option_val2`,`option_parm2`,`option_id3`,`option_val3`,`option_parm3`,`option_id4`,`option_val4`,`option_parm4`,`unique_id`,`bound`
FROM `mail`
WHERE `nameid` <> 0;

ALTER TABLE `mail`
	DROP COLUMN `nameid`,
	DROP COLUMN `amount`,
	DROP COLUMN `refine`,
	DROP COLUMN `attribute`,
	DROP COLUMN `identify`,
	DROP COLUMN `card0`,
	DROP COLUMN `card1`,
	DROP COLUMN `card2`,
	DROP COLUMN `card3`,
	DROP COLUMN `option_id0`,
	DROP COLUMN `option_val0`,
	DROP COLUMN `option_parm0`,
	DROP COLUMN `option_id1`,
	DROP COLUMN `option_val1`,
	DROP COLUMN `option_parm1`,
	DROP COLUMN `option_id2`,
	DROP COLUMN `option_val2`,
	DROP COLUMN `option_parm2`,
	DROP COLUMN `option_id3`,
	DROP COLUMN `option_val3`,
	DROP COLUMN `option_parm3`,
	DROP COLUMN `option_id4`,
	DROP COLUMN `option_val4`,
	DROP COLUMN `option_parm4`,
	DROP COLUMN `unique_id`,
	DROP COLUMN `bound`;

ALTER TABLE `mail`
	MODIFY `message` varchar(500) NOT NULL DEFAULT '';

ALTER TABLE `mail`
	ADD COLUMN `type` smallint(5) NOT NULL DEFAULT '0';

INSERT INTO `sql_updates` (`timestamp`) VALUES (1475526420);
