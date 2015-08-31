#1389028967
ALTER TABLE `buyingstores` ADD `body_direction` char(1) NOT NULL default '4',
ADD `head_direction` char(1) NOT NULL default '0',
ADD `sit` char(1) NOT NULL default '1';

ALTER TABLE `vendings` ADD `body_direction` char(1) NOT NULL default '4',
ADD `head_direction` char(1) NOT NULL default '0',
ADD `sit` char(1) NOT NULL default '1';

INSERT INTO `sql_updates` (`timestamp`) VALUES (1389028967);
