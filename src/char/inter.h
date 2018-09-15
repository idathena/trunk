// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _INTER_SQL_H_
#define _INTER_SQL_H_

struct accreg;
#include "../common/conf.h"
#include "../common/mmo.h"
#include "../common/sql.h"

struct Inter_Config {
	char cfgFile[128];                // Inter-Config file
	struct config_t cfg;              // Config
	struct s_storage_table *storages; // Storage name & table information
	uint8 storage_count;              // Number of available storage
};

extern struct Inter_Config interserv_config;

int inter_init_sql(const char *file);
void inter_final(void);
int inter_parse_frommap(int fd);
int inter_mapif_init(int fd);
int mapif_send_gmaccounts(void);
int mapif_disconnectplayer(int fd, int account_id, int char_id, int reason);
void mapif_parse_accinfo2(bool success, int map_fd, int u_fd, int u_aid, int account_id, const char *userid, const char *user_pass, const char *email, const char *last_ip, const char *lastlogin, const char *pin_code, const char *birthdate, int group_id, int logincount, int state);

int inter_log(char *fmt,...);

#define inter_cfgName "conf/inter_athena.conf"

extern unsigned int party_share_level;

extern Sql *sql_handle;
extern Sql *lsql_handle;

int inter_accreg_tosql(int account_id, int char_id, struct accreg *reg, int type);

#endif /* _INTER_SQL_H_ */
