#1414975503
ALTER TABLE `char` ADD COLUMN `sex` ENUM('M','F','U') NOT NULL default 'U';
ALTER TABLE `char` ADD COLUMN `hotkey_rowshift` tinyint(3) unsigned NOT NULL default '0';

CREATE TABLE `roulette` (
  `index` int(11) NOT NULL default '0',
  `level` smallint(5) unsigned NOT NULL,
  `item_id` smallint(5) unsigned NOT NULL,
  `amount` smallint(5) unsigned NOT NULL default '1',
  `flag` smallint(5) unsigned NOT NULL default '1',
  PRIMARY KEY (`index`)
) ENGINE=MyISAM default CHARSET=latin1;

INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (0,1,675,1,1);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (1,1,671,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (2,1,678,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (3,1,604,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (4,1,522,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (5,1,671,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (6,1,12523,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (7,1,985,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (8,1,984,1,0);

INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (9,2,675,1,1);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (10,2,671,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (11,2,603,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (12,2,608,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (13,2,607,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (14,2,12522,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (15,2,6223,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (16,2,6224,1,0);

INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (17,3,675,1,1);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (18,3,671,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (19,3,12108,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (20,3,617,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (21,3,12514,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (22,3,7444,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (23,3,969,1,0);

INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (24,4,675,1,1);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (25,4,671,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (26,4,616,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (27,4,12516,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (28,4,22777,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (29,4,6231,1,0);

INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (30,5,671,1,1);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (31,5,12246,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (32,5,12263,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (33,5,671,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (34,5,6235,1,0);

INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (35,6,671,1,1);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (36,6,12766,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (37,6,6234,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (38,6,6233,1,0);

INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (39,7,671,1,1);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (40,7,6233,1,0);
INSERT INTO `roulette`(`index`,`level`,`item_id`,`amount`,`flag`) VALUES (41,7,6233,1,0);

INSERT INTO `sql_updates` (`timestamp`) VALUES (1414975503);

/* NOTE: Use this code in the logs database because the code above are use in main database
ALTER TABLE `picklog` CHANGE `type` `type` ENUM('M','P','L','T','V','S','N','C','A','R','G','E','B','O','I','X','D','U','$','F','Z','Y') NOT NULL default 'P';
 */
