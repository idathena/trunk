#1559387580
ALTER TABLE `markets` MODIFY `name` varchar(50) NOT NULL DEFAULT '';

INSERT INTO `sql_updates` (`timestamp`) VALUES (1559387580);
