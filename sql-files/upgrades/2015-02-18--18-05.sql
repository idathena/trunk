#1395789302
/* NOTE: Use this code in the logs database because the code below are use in main database
ALTER TABLE `picklog` MODIFY `type` ENUM('M','P','L','T','V','S','N','C','A','R','G','E','B','O','I','X','D','U','$') NOT NULL default 'S';
 */

INSERT INTO `sql_updates` (`timestamp`) VALUES (1395789302);
