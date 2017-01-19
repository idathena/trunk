#1450241859
/* NOTE: Use this code in the logs database because the code below are use in main database
ALTER TABLE `chatlog` CHANGE COLUMN `type` `type` ENUM('O','W','P','G','M','C') NOT NULL DEFAULT 'O';
 */

INSERT INTO `sql_updates` (`timestamp`) VALUES (1450241859);
