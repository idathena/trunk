#1558911240
ALTER TABLE `charlog` ALTER `time` DROP DEFAULT;
ALTER TABLE `interlog` ALTER `time` DROP DEFAULT;
ALTER TABLE `ipbanlist` ALTER `btime` DROP DEFAULT;
ALTER TABLE `ipbanlist` ALTER `rtime` DROP DEFAULT;
ALTER TABLE `login` ALTER `lastlogin` DROP DEFAULT;
ALTER TABLE `login` ALTER `birthdate` DROP DEFAULT;

ALTER TABLE `login` MODIFY `lastlogin` datetime;
ALTER TABLE `login` MODIFY `birthdate` date;

UPDATE `login` SET `lastlogin` = NULL WHERE `lastlogin` = '0000-00-00 00:00:00';
UPDATE `login` SET `birthdate` = NULL WHERE `birthdate` = '0000-00-00';

-- Optional: delete useless entries
-- DELETE FROM `charlog` WHERE `time` = '0000-00-00 00:00:00';
-- DELETE FROM `interlog` WHERE `time` = '0000-00-00 00:00:00';
-- DELETE FROM `ipbanlist` WHERE `btime` = '0000-00-00 00:00:00';
-- DELETE FROM `ipbanlist` WHERE `rtime` = '0000-00-00 00:00:00';

INSERT INTO `sql_updates` (`timestamp`) VALUES (1558911240);

/* NOTE: Use this code in the logs database because the code above are use in main database
ALTER TABLE `atcommandlog` ALTER `atcommand_date` DROP DEFAULT;
ALTER TABLE `branchlog` ALTER `branch_date` DROP DEFAULT;
ALTER TABLE `cashlog` ALTER `time` DROP DEFAULT;
ALTER TABLE `chatlog` ALTER `time` DROP DEFAULT;
ALTER TABLE `feedinglog` ALTER `time` DROP DEFAULT;
ALTER TABLE `loginlog` ALTER `time` DROP DEFAULT;
ALTER TABLE `mvplog` ALTER `mvp_date` DROP DEFAULT;
ALTER TABLE `npclog` ALTER `npc_date` DROP DEFAULT;
ALTER TABLE `picklog` ALTER `time` DROP DEFAULT;
ALTER TABLE `zenylog` ALTER `time` DROP DEFAULT;

-- Optional: delete useless entries
-- DELETE FROM `atcommandlog` WHERE `atcommand_date` = '0000-00-00 00:00:00';
-- DELETE FROM `branchlog` WHERE `branch_date` = '0000-00-00 00:00:00';
-- DELETE FROM `cashlog` WHERE `time` = '0000-00-00 00:00:00';
-- DELETE FROM `chatlog` WHERE `time` = '0000-00-00 00:00:00';
-- DELETE FROM `feedinglog` WHERE `time` = '0000-00-00 00:00:00';
-- DELETE FROM `loginlog` WHERE `time` = '0000-00-00 00:00:00';
-- DELETE FROM `mvplog` WHERE `mvp_date` = '0000-00-00 00:00:00';
-- DELETE FROM `npclog` WHERE `npc_date` = '0000-00-00 00:00:00';
-- DELETE FROM `picklog` WHERE `time` = '0000-00-00 00:00:00';
-- DELETE FROM `zenylog` WHERE `time` = '0000-00-00 00:00:00';
