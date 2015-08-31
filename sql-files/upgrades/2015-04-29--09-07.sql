#1400256139
-- Move `bank_vault` value from `login` to `global_reg_value`
-- Please be careful if you're running multi char-server, better you do this step manually to move the `bank_vault`
-- to proper `global_reg_value` tables of char-servers used
INSERT INTO `global_reg_value` (`char_id`, `str`, `value`, `type`, `account_id`)
    SELECT '0', '#BANKVAULT', `bank_vault`, '2', `account_id`
	    FROM `login` WHERE `bank_vault` != 0;

-- Remove `bank_vault` from `login` table, login-server side
ALTER TABLE `login` DROP `bank_vault`;
INSERT INTO `sql_updates` (`timestamp`) VALUES (1400256139);

/* NOTE: Use this code in the logs database because the code above are use in main database
ALTER TABLE `picklog` CHANGE `type` `type` ENUM('M','P','L','T','V','S','N','C','A','R','G','E','B','O','I','X','D','U','$','F') NOT NULL DEFAULT 'P';
 */
