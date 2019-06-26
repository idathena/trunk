// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _CHAR_SQL_H_
#define _CHAR_SQL_H_

#include "../common/core.h" // CORE_ST_LAST
#include "../common/msg_conf.h"
#include "../common/mmo.h"

enum E_CHARSERVER_ST
{
	CHARSERVER_ST_RUNNING = CORE_ST_LAST,
	CHARSERVER_ST_STARTING,
	CHARSERVER_ST_SHUTDOWN,
	CHARSERVER_ST_LAST
};

struct mmo_charstatus;

#define MAX_MAP_SERVERS 2

struct mmo_map_server {
	int fd;
	uint32 ip;
	uint16 port;
	int users;
	unsigned short mapdata[MAX_MAP_PER_SERVER];
} server[MAX_MAP_SERVERS];

struct online_char_data {
	int account_id;
	int char_id;
	int fd;
	int waiting_disconnect;
	short server; // -2: unknown server, -1: not connected, 0+: id of server
	bool pincode_success;
};
DBMap *online_char_db; // int account_id -> struct online_char_data*

#define DEFAULT_AUTOSAVE_INTERVAL 300 * 1000

#define msg_config_read(cfgName) char_msg_config_read(cfgName)
#define msg_txt(msg_number) char_msg_txt(msg_number)
#define do_final_msg() char_do_final_msg()
int char_msg_config_read(char *cfgName);
const char *char_msg_txt(int msg_number);
void char_do_final_msg(void);

enum e_char_delete {
	CHAR_DEL_EMAIL = 1,
	CHAR_DEL_BIRTHDATE
};

enum e_char_delete_restriction {
	CHAR_DEL_RESTRICT_PARTY = 1,
	CHAR_DEL_RESTRICT_GUILD,
	CHAR_DEL_RESTRICT_ALL
};

enum e_char_del_response {
	CHAR_DELETE_OK = 0,
	CHAR_DELETE_DATABASE,
	CHAR_DELETE_NOTFOUND,
	CHAR_DELETE_BASELEVEL,
	CHAR_DELETE_GUILD,
	CHAR_DELETE_PARTY,
	CHAR_DELETE_TIME,
};

int memitemdata_to_sql(const struct item items[], int max, int id, enum storage_type tableswitch, uint8 stor_id);
bool memitemdata_from_sql(struct s_storage *p, int max, int id, enum storage_type tableswitch, uint8 stor_id);

int mapif_sendall(unsigned char *buf, unsigned int len);
int mapif_sendallwos(int fd, unsigned char *buf, unsigned int len);
int mapif_send(int fd, unsigned char *buf, unsigned int len);
void mapif_on_parse_accinfo(int account_id, int u_fd, int aid, int castergroup, int map_fd);

void disconnect_player(int account_id);
void set_session_flag_(int account_id, int val, bool set);
#define set_session_flag(account_id, val) ( set_session_flag_((account_id), (val), true)  )
#define unset_session_flag(account_id, val) ( set_session_flag_((account_id), (val), false) )

int char_married(int pl1, int pl2);
int char_child(int parent_id, int child_id);
int char_family(int pl1, int pl2, int pl3);

void char_reject(int fd, uint8 errCode);
void char_refuse_delchar(int fd, uint8 errCode);
void char_connectack(int fd, uint8 errCode);
void char_charselres(int fd, uint32 aid, uint8 res);
void char_changemapserv_ack(int fd, bool nok);
void char_send_map_data(int fd, struct mmo_charstatus *cd, uint32 ipl, int map_server_index);

int request_accreg2(int account_id, int char_id);
int save_accreg2(unsigned char *buf, int len);

extern int char_name_option;
extern char char_name_letters[];
extern bool char_gm_read;
extern int autosave_interval;
extern int save_log;
extern char db_path[];
extern char char_db[DB_NAME_LEN];
extern char scdata_db[DB_NAME_LEN];
extern char cart_db[DB_NAME_LEN];
extern char inventory_db[DB_NAME_LEN];
extern char charlog_db[DB_NAME_LEN];
extern char storage_db[DB_NAME_LEN];
extern char interlog_db[DB_NAME_LEN];
extern char reg_db[DB_NAME_LEN];
extern char skill_db[DB_NAME_LEN];
extern char memo_db[DB_NAME_LEN];
extern char guild_db[DB_NAME_LEN];
extern char guild_alliance_db[DB_NAME_LEN];
extern char guild_castle_db[DB_NAME_LEN];
extern char guild_expulsion_db[DB_NAME_LEN];
extern char guild_member_db[DB_NAME_LEN];
extern char guild_position_db[DB_NAME_LEN];
extern char guild_skill_db[DB_NAME_LEN];
extern char guild_storage_db[DB_NAME_LEN];
extern char party_db[DB_NAME_LEN];
extern char pet_db[DB_NAME_LEN];
extern char mail_db[DB_NAME_LEN];
extern char mail_attachment_db[DB_NAME_LEN];
extern char auction_db[DB_NAME_LEN];
extern char quest_db[DB_NAME_LEN];
extern char homunculus_db[DB_NAME_LEN];
extern char skill_homunculus_db[DB_NAME_LEN];
extern char mercenary_db[DB_NAME_LEN];
extern char mercenary_owner_db[DB_NAME_LEN];
extern char ragsrvinfo_db[DB_NAME_LEN];
extern char elemental_db[DB_NAME_LEN];
extern char elemental_scdata_db[DB_NAME_LEN];
extern char interreg_db[32];
extern char skillcooldown_db[DB_NAME_LEN];
extern char bonus_script_db[DB_NAME_LEN];
extern char clan_db[DB_NAME_LEN];
extern char clan_alliance_db[DB_NAME_LEN];
extern char achievement_db[DB_NAME_LEN];

extern int db_use_sqldbs; //Added for sql item_db read for char server [Valaris]

extern int guild_exp_rate;
extern int log_inter;

extern int mail_return_days;
extern int mail_delete_days;

#endif /* _CHAR_SQL_H_ */
