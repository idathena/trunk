// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _LOG_H_
#define _LOG_H_

//#include "map.h"
struct block_list;
struct map_session_data;
struct npc_data;
struct mob_data;
struct item;


typedef enum e_log_chat_type {
	LOG_CHAT_GLOBAL      = 0x01,
	LOG_CHAT_WHISPER     = 0x02,
	LOG_CHAT_PARTY       = 0x04,
	LOG_CHAT_GUILD       = 0x08,
	LOG_CHAT_MAINCHAT    = 0x10,
	LOG_CHAT_CLAN        = 0x20,
	// All
	LOG_CHAT_ALL         = 0xFF,
} e_log_chat_type;


typedef enum e_log_pick_type {
	LOG_TYPE_NONE             = 0,
	LOG_TYPE_TRADE            = 0x000001,
	LOG_TYPE_VENDING          = 0x000002,
	LOG_TYPE_PICKDROP_PLAYER  = 0x000004,
	LOG_TYPE_PICKDROP_MONSTER = 0x000008,
	LOG_TYPE_NPC              = 0x000010,
	LOG_TYPE_SCRIPT           = 0x000020,
	LOG_TYPE_STEAL            = 0x000040,
	LOG_TYPE_CONSUME          = 0x000080,
	LOG_TYPE_PRODUCE          = 0x000100,
	LOG_TYPE_MVP              = 0x000200,
	LOG_TYPE_COMMAND          = 0x000400,
	LOG_TYPE_STORAGE          = 0x000800,
	LOG_TYPE_GSTORAGE         = 0x001000,
	LOG_TYPE_MAIL             = 0x002000,
	LOG_TYPE_AUCTION          = 0x004000,
	LOG_TYPE_BUYING_STORE     = 0x008000,
	LOG_TYPE_OTHER            = 0x010000,
	LOG_TYPE_CASH             = 0x020000,
	LOG_TYPE_BANK             = 0x040000,
	LOG_TYPE_BOUND_REMOVAL    = 0x080000,
	LOG_TYPE_MERGE_ITEM       = 0x100000,
	LOG_TYPE_ROULETTE         = 0x200000,
	LOG_TYPE_QUEST            = 0x400000,
	LOG_TYPE_PRIVATE_AIRSHIP  = 0x800000,
	// Combinations
	LOG_TYPE_LOOT             = LOG_TYPE_PICKDROP_MONSTER|LOG_TYPE_CONSUME,
	// All
	LOG_TYPE_ALL              = 0xFFFFFF,
} e_log_pick_type;


typedef enum e_log_cash_type {
	LOG_CASH_TYPE_CASH  = 0x1,
	LOG_CASH_TYPE_KAFRA = 0x2,
} e_log_cash_type;


typedef enum e_log_feeding_type {
	LOG_FEED_HOMUNCULUS = 0x1,
	LOG_FEED_PET        = 0x2,
} e_log_feeding_type;


// New logs
void log_pick_pc(struct map_session_data *sd, e_log_pick_type type, int amount, struct item *itm);
void log_pick_mob(struct mob_data *md, e_log_pick_type type, int amount, struct item *itm);
void log_zeny(struct map_session_data *sd, e_log_pick_type type, struct map_session_data *src_sd, int amount);
void log_cash(struct map_session_data *sd, e_log_pick_type type, e_log_cash_type cash_type, int amount);
void log_npc(struct map_session_data *sd, const char *message);
void log_npc2(struct npc_data *nd, const char *message);
void log_chat(e_log_chat_type type, int type_id, int src_charid, int src_accid, const char *map, int x, int y, const char *dst_charname, const char *message);
void log_atcommand(struct map_session_data *sd, const char *message);
void log_feeding(struct map_session_data *sd, e_log_feeding_type type, unsigned short nameid);

// Old, but useful logs
void log_branch(struct map_session_data *sd);
void log_mvpdrop(struct map_session_data *sd, int monster_id, unsigned int *log_mvp);

int log_config_read(const char *cfgName);

extern struct Log_Config {
	e_log_pick_type enable_logs;
	int filter;
	bool sql_logs;
	bool log_chat_woe_disable;
	bool cash;
	int rare_items_log,refine_items_log,price_items_log,amount_items_log; //For filter
	int branch, mvpdrop, zeny, commands, npc, chat;
	unsigned feeding : 2;
	char log_branch[64], log_pick[64], log_zeny[64], log_mvpdrop[64], log_gm[64], log_npc[64], log_chat[64], log_cash[64], log_feeding[64];
} log_config;

#ifdef BETA_THREAD_TEST
	struct {
		char **entry;
		int count;
	} logThreadData;
#endif

#endif /* _LOG_H_ */
