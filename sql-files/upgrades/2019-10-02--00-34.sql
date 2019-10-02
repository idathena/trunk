#1569976467
ALTER TABLE `ipbanlist` MODIFY `list` varchar(15) NOT NULL default '';
DROP TABLE `ragsrvinfo`;

INSERT INTO `sql_updates` (`timestamp`) VALUES (1569976467);
