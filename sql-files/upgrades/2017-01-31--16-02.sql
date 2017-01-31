#1450367880
/* NOTE: Use this code in the logs database because the code below are use in main database
ALTER TABLE `picklog` MODIFY `type` enum('M','P','L','T','V','S','N','C','A','R','G','E','B','O','I','X','D','U','$','F','Z','Y','Q') NOT NULL default 'P';
 */

INSERT INTO `sql_updates` (`timestamp`) VALUES (1450367880);
