// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/core.h"
#include "../common/db.h"
#include "../common/malloc.h"
#include "../common/mapindex.h"
#include "../common/mmo.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/timer.h"
#include "../common/utils.h"
#include "../common/cli.h"
#include "../common/random.h"
#include "../common/ers.h"
#include "int_guild.h"
#include "int_homun.h"
#include "int_mail.h"
#include "int_mercenary.h"
#include "int_elemental.h"
#include "int_party.h"
#include "int_storage.h"
#include "char.h"
#include "inter.h"

#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_STARTPOINT 5 //Max number of start point defined in conf
#define MAX_STARTITEM 32 //Max number of items a new players can start with
#define CHAR_MAX_MSG 300 //Max number of msg_conf
static char *msg_table[CHAR_MAX_MSG]; // Login Server messages_conf

char char_db[DB_NAME_LEN] = "char";
char scdata_db[DB_NAME_LEN] = "sc_data";
char cart_db[DB_NAME_LEN] = "cart_inventory";
char inventory_db[DB_NAME_LEN] = "inventory";
char charlog_db[DB_NAME_LEN] = "charlog";
char storage_db[DB_NAME_LEN] = "storage";
char interlog_db[DB_NAME_LEN] = "interlog";
char reg_db[DB_NAME_LEN] = "global_reg_value";
char skill_db[DB_NAME_LEN] = "skill";
char memo_db[DB_NAME_LEN] = "memo";
char guild_db[DB_NAME_LEN] = "guild";
char guild_alliance_db[DB_NAME_LEN] = "guild_alliance";
char guild_castle_db[DB_NAME_LEN] = "guild_castle";
char guild_expulsion_db[DB_NAME_LEN] = "guild_expulsion";
char guild_member_db[DB_NAME_LEN] = "guild_member";
char guild_position_db[DB_NAME_LEN] = "guild_position";
char guild_skill_db[DB_NAME_LEN] = "guild_skill";
char guild_storage_db[DB_NAME_LEN] = "guild_storage";
char party_db[DB_NAME_LEN] = "party";
char pet_db[DB_NAME_LEN] = "pet";
char mail_db[DB_NAME_LEN] = "mail"; // MAIL SYSTEM
char mail_attachment_db[DB_NAME_LEN] = "mail_attachments";
char auction_db[DB_NAME_LEN] = "auction"; // Auctions System
char friend_db[DB_NAME_LEN] = "friends";
char hotkey_db[DB_NAME_LEN] = "hotkey";
char quest_db[DB_NAME_LEN] = "quest";
char homunculus_db[DB_NAME_LEN] = "homunculus";
char skill_homunculus_db[DB_NAME_LEN] = "skill_homunculus";
char mercenary_db[DB_NAME_LEN] = "mercenary";
char mercenary_owner_db[DB_NAME_LEN] = "mercenary_owner";
char ragsrvinfo_db[DB_NAME_LEN] = "ragsrvinfo";
char elemental_db[DB_NAME_LEN] = "elemental";
char interreg_db[32] = "interreg";
char skillcooldown_db[DB_NAME_LEN] = "skillcooldown";
char bonus_script_db[DB_NAME_LEN] = "bonus_script";
char clan_db[DB_NAME_LEN] = "clan";
char clan_alliance_db[DB_NAME_LEN] = "clan_alliance";
char achievement_db[DB_NAME_LEN] = "achievement";

// Show loading/saving messages
int save_log = 1;

static DBMap *char_db_; // int char_id -> struct mmo_charstatus*

char db_path[1024] = "db";

int db_use_sqldbs;

int login_fd = -1, char_fd = -1;
char userid[24];
char passwd[24];
char server_name[20];
char wisp_server_name[NAME_LENGTH] = "Server";
char login_ip_str[128];
uint32 login_ip = 0;
uint16 login_port = 6900;
char char_ip_str[128];
uint32 char_ip = 0;
char bind_ip_str[128];
uint32 bind_ip = INADDR_ANY;
uint16 char_port = 6121;
int char_server_type = 0;
int char_maintenance_min_group_id = 0;
bool char_new = true;
int char_new_display = 0;

bool name_ignoring_case = false; // Allow or not identical name for characters but with a different case by [Yor]
int char_name_option = 0; // Option to know which letters/symbols are authorized in the name of a character (0: all, 1: only those in char_name_letters, 2: all EXCEPT those in char_name_letters) by [Yor]
char unknown_char_name[NAME_LENGTH] = "Unknown"; // Name to use when the requested name cannot be determined
#define TRIM_CHARS "\255\xA0\032\t\x0A\x0D " // The following characters are trimmed regardless because they cause confusion and problems on the servers. [Skotlex]
char char_name_letters[1024] = ""; // List of letters/symbols allowed (or not) in a character name. by [Yor]

int char_del_level = 0; // From which level u can delete character [Lupus]
int char_del_delay = 86400;
int char_del_option =
#if PACKETVER >= 20100803
	CHAR_DEL_BIRTHDATE
#else
	CHAR_DEL_EMAIL
#endif
	;
int char_del_restriction = CHAR_DEL_RESTRICT_ALL;

int log_char = 1;	// Loggin char or not [devil]
int log_inter = 1;	// Loggin inter or not [devil]

// Advanced subnet check [LuzZza]
struct s_subnet {
	uint32 mask;
	uint32 char_ip;
	uint32 map_ip;
} subnet[16];
int subnet_count = 0;

struct char_session_data {
	bool auth; // Whether the session is authed or not
	uint32 account_id, login_id1, login_id2, sex;
	int found_char[MAX_CHARS]; // Ids of chars on this account
	char email[40]; // E-mail (default: a@a.com) by [Yor]
	time_t expiration_time; // # of seconds 1/1/1970 (timestamp): Validity limit of the account (0 = unlimited)
	int group_id; // Permission
	uint8 char_slots; // Total number of characters that can be created
	uint8 chars_vip;
	uint8 chars_billing;
	uint8 clienttype;
	char new_name[NAME_LENGTH];
	char birthdate[10 + 1];  // YYYY-MM-DD
	// Pincode system
	char pincode[PINCODE_LENGTH + 1];
	uint32 pincode_seed;
	time_t pincode_change;
	uint16 pincode_try;
	// Addon system
	unsigned int char_moves[MAX_CHARS]; // Character moves left
	uint8 isvip;
	time_t unban_time[MAX_CHARS];
	int charblock_timer;
	uint8 flag; // &1 - Retrieving guild bound items
};

//Initial items the player with spawn with on the server
struct startitem {
	unsigned short nameid, //Item ID
		amount; //Number of items
	short pos; //Position (Auto-equip)
} start_items[MAX_STARTITEM], start_items_doram[MAX_STARTITEM];

int max_connect_user = -1;
int gm_allow_group = -1;
int autosave_interval = DEFAULT_AUTOSAVE_INTERVAL;
int start_zeny = 0;
int guild_exp_rate = 100;

char default_map[MAP_NAME_LENGTH];
unsigned short default_map_x = 156;
unsigned short default_map_y = 191;

int clan_remove_inactive_days = 14;
int mail_return_days = 15;
int mail_delete_days = 15;

#if PACKETVER_SUPPORTS_PINCODE
// Pincode system
enum pincode_state {
	PINCODE_OK = 0,
	PINCODE_ASK = 1,
	PINCODE_NOTSET = 2,
	PINCODE_EXPIRED = 3,
	PINCODE_NEW = 4,
	PINCODE_ILLEGAL = 5,
#if 0
	PINCODE_KSSN = 6, // Not supported since we do not store KSSN
#endif
	PINCODE_PASSED = 7,
	PINCODE_WRONG = 8
};

bool pincode_enabled = true;
int pincode_changetime = 0;
int pincode_maxtry = 3;
bool pincode_force = true;
bool pincode_allow_repeated = false;
bool pincode_allow_sequential = false;

void pincode_check(int fd, struct char_session_data *sd);
void pincode_change(int fd, struct char_session_data *sd);
void pincode_setnew(int fd, struct char_session_data *sd);
void pincode_sendstate(int fd, struct char_session_data *sd, uint16 state);
void pincode_notifyLoginPinUpdate(int account_id, char *pin);
void pincode_notifyLoginPinError(int account_id);
void pincode_decrypt(uint32 userSeed, char *pin);
int pincode_compare(int fd, struct char_session_data *sd, char *pin);
bool pincode_allowed(char *pincode);
#endif

void mapif_vipack(int mapfd, uint32 aid, uint32 vip_time, uint8 flag, uint32 group_id);
int loginif_reqvipdata(uint32 aid, uint8 flag, int32 timediff, int mapfd);
void loginif_parse_vipack(int fd);
void loginif_parse_changesex(int sex, int acc, int char_id, int class_, int guild_id);
void loginif_parse_ackchangecharsex(int char_id, int sex);

// Addon system
bool char_move_enabled = true;
bool char_movetoused = true;
bool char_moves_unlimited = false;
bool char_rename_party = false;
bool char_rename_guild = false;

void moveCharSlot(int fd, struct char_session_data *sd, unsigned short from, unsigned short to);
void moveCharSlotReply(int fd, struct char_session_data *sd, unsigned short index, short reason);
void char_reqrename_response(int fd, struct char_session_data *sd, bool name_valid);
void char_rename_response(int fd, struct char_session_data *sd, int16 response);
int char_parse_reqrename(int fd, struct char_session_data *sd);
int char_parse_ackrename(int fd, struct char_session_data *sd);

// Custom limits for the fame lists [Skotlex]
int fame_list_size_chemist = MAX_FAME_LIST;
int fame_list_size_smith = MAX_FAME_LIST;
int fame_list_size_taekwon = MAX_FAME_LIST;

// Char-server-side stored fame lists [DracoRPG]
struct fame_list smith_fame_list[MAX_FAME_LIST];
struct fame_list chemist_fame_list[MAX_FAME_LIST];
struct fame_list taekwon_fame_list[MAX_FAME_LIST];

// Bonus Script
void bonus_script_get(int fd); // Get bonus_script data
void bonus_script_save(int fd); // Save bonus_script data

// Check for exit signal
// 0 is saving complete
// other is char_id
unsigned int save_flag = 0;

//Initial position the player will spawn on the server
struct point start_point[MAX_STARTPOINT];
struct point start_point_doram[MAX_STARTPOINT];

//Number of positions read
short start_point_count = 1;
short start_point_count_doram = 1;

int console = 0;

//-----------------------------------------------------
// Auth database
//-----------------------------------------------------
#define AUTH_TIMEOUT 30000

struct auth_node {
	int account_id;
	int char_id;
	uint32 login_id1;
	uint32 login_id2;
	uint32 ip;
	int sex;
	time_t expiration_time; // # of seconds 1/1/1970 (timestamp): Validity limit of the account (0 = unlimited)
	int group_id;
	unsigned changing_mapservers : 1;
	uint8 version;
};

static DBMap *auth_db; // int account_id -> struct auth_node*

//-----------------------------------------------------
// Online User Database
//-----------------------------------------------------

static int chardb_waiting_disconnect(int tid, unsigned int tick, int id, intptr_t data);
enum e_char_del_response char_delete(struct char_session_data *sd, uint32 char_id);

int loginif_isconnected();
#define loginif_check(a) { if(!loginif_isconnected()) return a; }

/**
 * @see DBCreateData
 */
static DBData create_online_char_data(DBKey key, va_list args)
{
	struct online_char_data *character;
	CREATE(character, struct online_char_data, 1);
	character->account_id = key.i;
	character->char_id = -1;
	character->server = -1;
	character->fd = -1;
	character->waiting_disconnect = INVALID_TIMER;
	return db_ptr2data(character);
}

void set_char_charselect(int account_id)
{
	struct online_char_data *character;

	character = (struct online_char_data *)idb_ensure(online_char_db, account_id, create_online_char_data);

	if( character->server > -1 )
		if( server[character->server].users > 0 ) // Prevent this value from going negative.
			server[character->server].users--;

	character->char_id = -1;
	character->server = -1;

	if( character->waiting_disconnect != INVALID_TIMER ) {
		delete_timer(character->waiting_disconnect, chardb_waiting_disconnect);
		character->waiting_disconnect = INVALID_TIMER;
	}

	if( loginif_isconnected() ) {
		WFIFOHEAD(login_fd,6);
		WFIFOW(login_fd,0) = 0x272b;
		WFIFOL(login_fd,2) = account_id;
		WFIFOSET(login_fd,6);
	}

}

void set_char_online(int map_id, int char_id, int account_id)
{
	struct online_char_data *character;
	struct mmo_charstatus *cp;

	//Update DB
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online`='1', `last_login`=NOW() WHERE `char_id`='%d' LIMIT 1", char_db, char_id) )
		Sql_ShowDebug(sql_handle);

	//Check to see for online conflicts
	character = (struct online_char_data *)idb_ensure(online_char_db, account_id, create_online_char_data);
	if( character->char_id != -1 && character->server > -1 && character->server != map_id ) {
		ShowNotice("set_char_online: Character %d:%d marked in map server %d, but map server %d claims to have (%d:%d) online!\n",
			character->account_id, character->char_id, character->server, map_id, account_id, char_id);
		mapif_disconnectplayer(server[character->server].fd, character->account_id, character->char_id, 2);
	}

	//Update state data
	character->char_id = char_id;
	character->server = map_id;

	if( character->server > -1 )
		server[character->server].users++;

	//Get rid of disconnect timer
	if( character->waiting_disconnect != INVALID_TIMER ) {
		delete_timer(character->waiting_disconnect, chardb_waiting_disconnect);
		character->waiting_disconnect = INVALID_TIMER;
	}

	//Set char online in guild cache. If char is in memory, use the guild id on it, otherwise seek it.
	cp = (struct mmo_charstatus *)idb_get(char_db_, char_id);
	inter_guild_CharOnline(char_id, cp ? cp->guild_id : -1);

	//Notify login server
	if( loginif_isconnected() ) {
		WFIFOHEAD(login_fd,6);
		WFIFOW(login_fd,0) = 0x272b;
		WFIFOL(login_fd,2) = account_id;
		WFIFOSET(login_fd,6);
	}
}

void set_char_offline(int char_id, int account_id)
{
	struct online_char_data *character;

	if( char_id == -1 ) {
		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online`='0' WHERE `account_id`='%d'", char_db, account_id) )
			Sql_ShowDebug(sql_handle);
	} else {
		struct mmo_charstatus *cp = (struct mmo_charstatus *)idb_get(char_db_,char_id);
		inter_guild_CharOffline(char_id, cp ? cp->guild_id : -1);
		if( cp )
			idb_remove(char_db_, char_id);

		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online`='0' WHERE `char_id`='%d' LIMIT 1", char_db, char_id) )
			Sql_ShowDebug(sql_handle);
	}

	if( (character = (struct online_char_data *)idb_get(online_char_db, account_id)) != NULL ) {
		//We don't free yet to avoid aCalloc/aFree spamming during char change. [Skotlex]
		if( character->server > -1 )
			if( server[character->server].users > 0 ) // Prevent this value from going negative.
				server[character->server].users--;

		if( character->waiting_disconnect != INVALID_TIMER ) {
			delete_timer(character->waiting_disconnect, chardb_waiting_disconnect);
			character->waiting_disconnect = INVALID_TIMER;
		}

		if( character->char_id == char_id ) {
			character->char_id = -1;
			character->server = -1;
			//Needed if player disconnects completely since Skotlex did not want to free the session
			character->pincode_success = false;
		}

		//FIXME? Why Kevin free'd the online information when the char was effectively in the map-server?
	}

	//Remove char if 1- Set all offline, or 2- character is no longer connected to char-server.
	if( loginif_isconnected() && (char_id == -1 || character == NULL || character->fd == -1) ) {
		WFIFOHEAD(login_fd,6);
		WFIFOW(login_fd,0) = 0x272c;
		WFIFOL(login_fd,2) = account_id;
		WFIFOSET(login_fd,6);
	}
}

/**
 * @see DBApply
 */
static int char_db_setoffline(DBKey key, DBData *data, va_list ap)
{
	struct online_char_data *character = (struct online_char_data *)db_data2ptr(data);
	int server_id = va_arg(ap, int);

	if( server_id == -1 ) {
		character->char_id = -1;
		character->server = -1;
		if( character->waiting_disconnect != INVALID_TIMER ) {
			delete_timer(character->waiting_disconnect, chardb_waiting_disconnect);
			character->waiting_disconnect = INVALID_TIMER;
		}
	} else if( character->server == server_id )
		character->server = -2; //In some map server that we aren't connected to.
	return 0;
}

/**
 * @see DBApply
 */
static int char_db_kickoffline(DBKey key, DBData *data, va_list ap)
{
	struct online_char_data *character = (struct online_char_data *)db_data2ptr(data);
	int server_id = va_arg(ap, int);

	if( server_id > -1 && character->server != server_id )
		return 0;

	//Kick out any connected characters, and set them offline as appropriate.
	if( character->server > -1 )
		mapif_disconnectplayer(server[character->server].fd, character->account_id, character->char_id, 1);
	else if( character->waiting_disconnect == INVALID_TIMER )
		set_char_offline(character->char_id, character->account_id);
	else
		return 0; // fail

	return 1;
}

void set_all_offline(int id)
{
	if( id < 0 )
		ShowNotice("Sending all users offline.\n");
	else
		ShowNotice("Sending users of map-server %d offline.\n",id);
	online_char_db->foreach(online_char_db,char_db_kickoffline,id);

	if( id >= 0 || !loginif_isconnected() )
		return;
	//Tell login-server to also mark all our characters as offline.
	WFIFOHEAD(login_fd,2);
	WFIFOW(login_fd,0) = 0x2737;
	WFIFOSET(login_fd,2);
}

void set_all_offline_sql(void)
{
	//Set all players to 'OFFLINE'
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online` = '0'", char_db) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online` = '0'", guild_member_db) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `connect_member` = '0'", guild_db) )
		Sql_ShowDebug(sql_handle);
}

/**
 * @see DBCreateData
 */
static DBData create_charstatus(DBKey key, va_list args)
{
	struct mmo_charstatus *cp;

	cp = (struct mmo_charstatus *)aCalloc(1,sizeof(struct mmo_charstatus));
	cp->char_id = key.i;
	return db_ptr2data(cp);
}

int mmo_char_tosql(int char_id, struct mmo_charstatus *p)
{
	int i = 0;
	int count = 0;
	int diff = 0;
	char save_status[128]; //For displaying save information. [Skotlex]
	struct mmo_charstatus *cp;
	int errors = 0; //If there are any errors while saving, "cp" will not be updated at the end.
	StringBuf buf;

	if (char_id != p->char_id)
		return 0;

	cp = (struct mmo_charstatus *)idb_ensure(char_db_, char_id, create_charstatus);

	StringBuf_Init(&buf);
	memset(save_status, 0, sizeof(save_status));

	if(
		(p->base_exp != cp->base_exp) || (p->base_level != cp->base_level) ||
		(p->job_level != cp->job_level) || (p->job_exp != cp->job_exp) ||
		(p->zeny != cp->zeny) ||
		(p->last_point.map != cp->last_point.map) ||
		(p->last_point.x != cp->last_point.x) || (p->last_point.y != cp->last_point.y) ||
		(p->max_hp != cp->max_hp) || (p->hp != cp->hp) ||
		(p->max_sp != cp->max_sp) || (p->sp != cp->sp) ||
		(p->status_point != cp->status_point) || (p->skill_point != cp->skill_point) ||
		(p->str != cp->str) || (p->agi != cp->agi) || (p->vit != cp->vit) ||
		(p->int_ != cp->int_) || (p->dex != cp->dex) || (p->luk != cp->luk) ||
		(p->option != cp->option) ||
		(p->party_id != cp->party_id) || (p->guild_id != cp->guild_id) ||
		(p->pet_id != cp->pet_id) || (p->weapon != cp->weapon) || (p->hom_id != cp->hom_id) ||
		(p->ele_id != cp->ele_id) || (p->shield != cp->shield) || (p->head_top != cp->head_top) ||
		(p->head_mid != cp->head_mid) || (p->head_bottom != cp->head_bottom) || (p->delete_date != cp->delete_date) ||
		(p->rename != cp->rename) || (p->robe != cp->robe) || (p->character_moves != cp->character_moves) ||
		(p->show_equip != cp->show_equip) || (p->allow_party != cp->allow_party) || (p->font != cp->font) ||
		(p->uniqueitem_counter != cp->uniqueitem_counter) || (p->hotkey_rowshift != cp->hotkey_rowshift) ||
		(p->clan_id != cp->clan_id) || (p->title_id != cp->title_id)
	) {	//Save status
		unsigned int opt = 0;

		if( p->allow_party )
			opt |= OPT_ALLOW_PARTY; 
		if( p->show_equip )
			opt |= OPT_SHOW_EQUIP;

		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `base_level`='%d',`job_level`='%d',"
			"`base_exp`='%u',`job_exp`='%u',`zeny`='%d',"
			"`max_hp`='%u',`hp`='%u',`max_sp`='%u',`sp`='%u',`status_point`='%d',`skill_point`='%d',"
			"`str`='%d',`agi`='%d',`vit`='%d',`int`='%d',`dex`='%d',`luk`='%d',"
			"`option`='%d',`party_id`='%d',`guild_id`='%d',`pet_id`='%d',`homun_id`='%d',`elemental_id`='%d',"
			"`weapon`='%d',`shield`='%d',`head_top`='%d',`head_mid`='%d',`head_bottom`='%d',"
			"`last_map`='%s',`last_x`='%d',`last_y`='%d',`save_map`='%s',`save_x`='%d',`save_y`='%d',`rename`='%d',"
			"`delete_date`='%lu',`robe`='%d',`moves`='%d',`char_opt`='%u',`font`='%u',`uniqueitem_counter`='%u',"
			"`hotkey_rowshift`='%d',`clan_id`='%d',`title_id`='%lu'"
			" WHERE `account_id`='%d' AND `char_id` = '%d'",
			char_db, p->base_level, p->job_level,
			p->base_exp, p->job_exp, p->zeny,
			p->max_hp, p->hp, p->max_sp, p->sp, p->status_point, p->skill_point,
			p->str, p->agi, p->vit, p->int_, p->dex, p->luk,
			p->option, p->party_id, p->guild_id, p->pet_id, p->hom_id, p->ele_id,
			p->weapon, p->shield, p->head_top, p->head_mid, p->head_bottom,
			mapindex_id2name(p->last_point.map), p->last_point.x, p->last_point.y,
			mapindex_id2name(p->save_point.map), p->save_point.x, p->save_point.y, p->rename,
			(unsigned long)p->delete_date, //FIXME: platform-dependent size
			p->robe,p->character_moves,opt,p->font,p->uniqueitem_counter,p->hotkey_rowshift,p->clan_id,p->title_id,
			p->account_id,p->char_id) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " status");
	}

	//Values that will seldom change (to speed up saving)
	if(
		(p->hair != cp->hair) || (p->hair_color != cp->hair_color) || (p->clothes_color != cp->clothes_color) ||
		(p->body != cp->body) || (p->class_ != cp->class_) ||
		(p->partner_id != cp->partner_id) || (p->father != cp->father) ||
		(p->mother != cp->mother) || (p->child != cp->child) ||
 		(p->karma != cp->karma) || (p->manner != cp->manner) ||
		(p->fame != cp->fame)
	)
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `class`='%d',"
			"`hair`='%d',`hair_color`='%d',`clothes_color`='%d',`body`='%d',"
			"`partner_id`='%d',`father`='%d',`mother`='%d',`child`='%d',"
			"`karma`='%d',`manner`='%d',`fame`='%d'"
			" WHERE `account_id`='%d' AND `char_id` = '%d'",
			char_db, p->class_,
			p->hair, p->hair_color, p->clothes_color, p->body,
			p->partner_id, p->father, p->mother, p->child,
			p->karma, p->manner, p->fame,
			p->account_id, p->char_id) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " status2");
	}

	/* Mercenary Owner */
	if( (p->mer_id != cp->mer_id) ||
		(p->arch_calls != cp->arch_calls) || (p->arch_faith != cp->arch_faith) ||
		(p->spear_calls != cp->spear_calls) || (p->spear_faith != cp->spear_faith) ||
		(p->sword_calls != cp->sword_calls) || (p->sword_faith != cp->sword_faith) )
	{
		if (mercenary_owner_tosql(char_id, p))
			strcat(save_status, " mercenary");
		else
			errors++;
	}

	//Memo points
	if( memcmp(p->memo_point, cp->memo_point, sizeof(p->memo_point)) ) {
		char esc_mapname[NAME_LENGTH * 2 + 1];

		//`memo` (`memo_id`,`char_id`,`map`,`x`,`y`)
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", memo_db, p->char_id) ) {
			Sql_ShowDebug(sql_handle);
			errors++;
		}

		//Insert here.
		StringBuf_Clear(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `%s`(`char_id`,`map`,`x`,`y`) VALUES ", memo_db);
		for( i = 0, count = 0; i < MAX_MEMOPOINTS; ++i ) {
			if( p->memo_point[i].map ) {
				if( count )
					StringBuf_AppendStr(&buf, ",");
				Sql_EscapeString(sql_handle, esc_mapname, mapindex_id2name(p->memo_point[i].map));
				StringBuf_Printf(&buf, "('%d', '%s', '%d', '%d')", char_id, esc_mapname, p->memo_point[i].x, p->memo_point[i].y);
				++count;
			}
		}
		if( count ) {
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) ) {
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
		strcat(save_status, " memo");
	}

	//FIXME: is this neccessary? [ultramage]
	for( i = 0; i < MAX_SKILL; i++ )
		if( (p->skill[i].lv != 0) && (p->skill[i].id == 0) )
			p->skill[i].id = i; //Fix skill tree


	//Skills
	if( memcmp(p->skill, cp->skill, sizeof(p->skill)) ) {
		//`skill` (`char_id`, `id`, `lv`)
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", skill_db, p->char_id) ) {
			Sql_ShowDebug(sql_handle);
			errors++;
		}

		StringBuf_Clear(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `%s`(`char_id`,`id`,`lv`,`flag`) VALUES ", skill_db);
		//Insert here.
		for( i = 0, count = 0; i < MAX_SKILL; ++i ) {
			if( p->skill[i].id != 0 && p->skill[i].flag != SKILL_FLAG_TEMPORARY ) {
				if( p->skill[i].lv == 0 && ( p->skill[i].flag == SKILL_FLAG_PERM_GRANTED || p->skill[i].flag == SKILL_FLAG_PERMANENT ) )
					continue;
				if( p->skill[i].flag != SKILL_FLAG_PERMANENT && p->skill[i].flag != SKILL_FLAG_PERM_GRANTED && (p->skill[i].flag - SKILL_FLAG_REPLACED_LV_0) == 0 )
					continue;
				if( count )
					StringBuf_AppendStr(&buf, ",");
				StringBuf_Printf(&buf, "('%d','%d','%d','%d')", char_id, p->skill[i].id,
					( (p->skill[i].flag == SKILL_FLAG_PERMANENT || p->skill[i].flag == SKILL_FLAG_PERM_GRANTED) ? p->skill[i].lv : p->skill[i].flag - SKILL_FLAG_REPLACED_LV_0),
					p->skill[i].flag == SKILL_FLAG_PERM_GRANTED ? p->skill[i].flag : 0); /* Other flags do not need to be saved */
				++count;
			}
		}
		if( count ) {
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) ) {
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}

		strcat(save_status, " skills");
	}

	diff = 0;
	for( i = 0; i < MAX_FRIENDS; i++ ) {
		if( p->friends[i].char_id != cp->friends[i].char_id ||
			p->friends[i].account_id != cp->friends[i].account_id ) {
			diff = 1;
			break;
		}
	}

	if( diff == 1 ) { //Save friends
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", friend_db, char_id) ) {
			Sql_ShowDebug(sql_handle);
			errors++;
		}

		StringBuf_Clear(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `%s` (`char_id`, `friend_account`, `friend_id`) VALUES ", friend_db);
		for( i = 0, count = 0; i < MAX_FRIENDS; ++i ) {
			if( p->friends[i].char_id > 0 ) {
				if( count )
					StringBuf_AppendStr(&buf, ",");
				StringBuf_Printf(&buf, "('%d','%d','%d')", char_id, p->friends[i].account_id, p->friends[i].char_id);
				count++;
			}
		}
		if( count ) {
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) ) {
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
		strcat(save_status, " friends");
	}

#ifdef HOTKEY_SAVING
	//Hotkeys
	StringBuf_Clear(&buf);
	StringBuf_Printf(&buf, "REPLACE INTO `%s` (`char_id`, `hotkey`, `type`, `itemskill_id`, `skill_lvl`) VALUES ", hotkey_db);
	diff = 0;
	for( i = 0; i < ARRAYLENGTH(p->hotkeys); i++ ) {
		if( memcmp(&p->hotkeys[i], &cp->hotkeys[i], sizeof(struct hotkey)) ) {
			if( diff )
				StringBuf_AppendStr(&buf, ",");// not the first hotkey
			StringBuf_Printf(&buf, "('%d','%u','%u','%u','%u')", char_id, (unsigned int)i, (unsigned int)p->hotkeys[i].type, p->hotkeys[i].id , (unsigned int)p->hotkeys[i].lv);
			diff = 1;
		}
	}
	if( diff ) {
		if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) ) {
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " hotkeys");
	}
#endif
	StringBuf_Destroy(&buf);
	if( save_status[0] != '\0' && save_log )
		ShowInfo("Saved char %d - %s:%s.\n", char_id, p->name, save_status);
	if( !errors )
		memcpy(cp, p, sizeof(struct mmo_charstatus));
	return 0;
}

/// Saves an array of 'item' entries into the specified table.
int memitemdata_to_sql(const struct item items[], int max, int id, enum storage_type tableswitch, uint8 stor_id)
{
	StringBuf buf;
	SqlStmt *stmt;
	int i, j, offset = 0, errors = 0;
	const char *tablename, *selectoption, *printname;
	struct item item; // Temp storage variable
	bool *flag; // Bit array for inventory matching
	bool found;

	switch( tableswitch ) {
		case TABLE_INVENTORY:
			printname = "Inventory";
			tablename = inventory_db;
			selectoption = "char_id";
			break;
		case TABLE_CART:
			printname = "Cart";
			tablename = cart_db;
			selectoption = "char_id";
			break;
		case TABLE_STORAGE:
			printname = inter_premiumStorage_getPrintableName(stor_id);
			tablename = inter_premiumStorage_getTableName(stor_id);
			selectoption = "account_id";
			break;
		case TABLE_GUILD_STORAGE:
			printname = "Guild Storage";
			tablename = guild_storage_db;
			selectoption = "guild_id";
			break;
		default:
			ShowError("Invalid table name!\n");
			return 1;
	}

	// The following code compares inventory with current database values
	// and performs modification/deletion/insertion only on relevant rows.
	// This approach is more complicated than a trivial delete&insert, but
	// it significantly reduces cpu load on the database server.

	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `bound`, `unique_id`");
	if( tableswitch == TABLE_INVENTORY ) {
		StringBuf_Printf(&buf, ", `favorite`");
		offset = 1;
	}
	for( i = 0; i < MAX_SLOTS; ++i )
		StringBuf_Printf(&buf, ", `card%d`", i);
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		StringBuf_Printf(&buf, ", `option_id%d`", i);
		StringBuf_Printf(&buf, ", `option_val%d`", i);
		StringBuf_Printf(&buf, ", `option_parm%d`", i);
	}
	StringBuf_Printf(&buf, " FROM `%s` WHERE `%s`='%d'", tablename, selectoption, id);

	stmt = SqlStmt_Malloc(sql_handle);
	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf)) || SQL_ERROR == SqlStmt_Execute(stmt) ) {
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		StringBuf_Destroy(&buf);
		return 1;
	}

	SqlStmt_BindColumn(stmt, 0, SQLDT_INT,          &item.id,          0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,       &item.nameid,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,        &item.amount,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,         &item.equip,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,         &item.identify,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,         &item.refine,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,         &item.attribute,   0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,         &item.expire_time, 0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 8, SQLDT_UINT,         &item.bound,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 9, SQLDT_UINT64,       &item.unique_id,   0, NULL, NULL);
	if( tableswitch == TABLE_INVENTORY )
		SqlStmt_BindColumn(stmt, 10, SQLDT_CHAR,    &item.favorite,    0, NULL, NULL);
	for( i = 0; i < MAX_SLOTS; ++i )
		SqlStmt_BindColumn(stmt, 10+offset+i,             SQLDT_USHORT, &item.card[i],         0, NULL, NULL);
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		SqlStmt_BindColumn(stmt, 10+offset+MAX_SLOTS+i*3, SQLDT_SHORT,  &item.option[i].id,    0, NULL, NULL);
		SqlStmt_BindColumn(stmt, 11+offset+MAX_SLOTS+i*3, SQLDT_SHORT,  &item.option[i].value, 0, NULL, NULL);
		SqlStmt_BindColumn(stmt, 12+offset+MAX_SLOTS+i*3, SQLDT_CHAR,   &item.option[i].param, 0, NULL, NULL);
	}

	// Bit array indicating which inventory items have already been matched
	flag = (bool *)aCalloc(max, sizeof(bool));

	while( SQL_SUCCESS == SqlStmt_NextRow(stmt) ) {
		found = false;
		// Search for the presence of the item in the char's inventory
		for( i = 0; i < max; ++i ) {
			// Skip empty and already matched entries
			if( items[i].nameid == 0 || flag[i] )
				continue;

			if( items[i].nameid == item.nameid &&
				items[i].card[0] == item.card[0] &&
				items[i].card[2] == item.card[2] &&
				items[i].card[3] == item.card[3] &&
				items[i].unique_id == item.unique_id )
			{ // They are the same item.
				int k;

				ARR_FIND(0, MAX_SLOTS, j, items[i].card[j] != item.card[j]);
				ARR_FIND(0, MAX_ITEM_RDM_OPT, k, (items[i].option[k].id != item.option[k].id || items[i].option[k].value != item.option[k].value || items[i].option[k].param != item.option[k].param));
				if( j == MAX_SLOTS &&
					k == MAX_ITEM_RDM_OPT &&
					items[i].amount == item.amount &&
					items[i].equip == item.equip &&
					items[i].identify == item.identify &&
					items[i].refine == item.refine &&
					items[i].attribute == item.attribute &&
					items[i].expire_time == item.expire_time &&
					items[i].bound == item.bound &&
					(tableswitch != TABLE_INVENTORY || items[i].favorite == item.favorite) )
					; // Do nothing.
				else {
					// Update all fields.
					StringBuf_Clear(&buf);
					StringBuf_Printf(&buf, "UPDATE `%s` SET `amount`='%d', `equip`='%d', `identify`='%d', `refine`='%d',`attribute`='%d', `expire_time`='%u', `bound`='%d', `unique_id`='%"PRIu64"'",
						tablename, items[i].amount, items[i].equip, items[i].identify, items[i].refine, items[i].attribute, items[i].expire_time, items[i].bound, items[i].unique_id);
					if( tableswitch == TABLE_INVENTORY )
						StringBuf_Printf(&buf, ", `favorite`='%d'", items[i].favorite);
					for( j = 0; j < MAX_SLOTS; ++j )
						StringBuf_Printf(&buf, ", `card%d`=%hu", j, items[i].card[j]);
					for( j = 0; j < MAX_ITEM_RDM_OPT; ++j ) {
						StringBuf_Printf(&buf, ", `option_id%d`=%d", j, items[i].option[j].id);
						StringBuf_Printf(&buf, ", `option_val%d`=%d", j, items[i].option[j].value);
						StringBuf_Printf(&buf, ", `option_parm%d`=%d", j, items[i].option[j].param);
					}
					StringBuf_Printf(&buf, " WHERE `id`='%d' LIMIT 1", item.id);

					if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) ) {
						Sql_ShowDebug(sql_handle);
						errors++;
					}
				}

				found = flag[i] = true; // Item dealt with.
				break; // Skip to next item in the db.
			}
		}
		if( !found ) { // Item not present in inventory, remove it.
			if( SQL_ERROR == Sql_Query(sql_handle, "DELETE from `%s` where `id`='%d' LIMIT 1", tablename, item.id) ) {
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
	}
	SqlStmt_Free(stmt);

	StringBuf_Clear(&buf);
	StringBuf_Printf(&buf, "INSERT INTO `%s`(`%s`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `bound`, `unique_id`", tablename, selectoption);
	if( tableswitch == TABLE_INVENTORY )
		StringBuf_Printf(&buf, ", `favorite`");
	for( i = 0; i < MAX_SLOTS; ++i )
		StringBuf_Printf(&buf, ", `card%d`", i);
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		StringBuf_Printf(&buf, ", `option_id%d`", i);
		StringBuf_Printf(&buf, ", `option_val%d`", i);
		StringBuf_Printf(&buf, ", `option_parm%d`", i);
	}
	StringBuf_AppendStr(&buf, ") VALUES ");

	found = false;
	// Insert non-matched items into the db as new items
	for( i = 0; i < max; ++i ) {
		// Skip empty and already matched entries
		if( !(items[i].nameid) || flag[i] )
			continue;

		if( found )
			StringBuf_AppendStr(&buf, ",");
		else
			found = true;

		StringBuf_Printf(&buf, "('%d', '%hu', '%d', '%d', '%d', '%d', '%d', '%u', '%d', '%"PRIu64"'",
			id, items[i].nameid, items[i].amount, items[i].equip, items[i].identify, items[i].refine, items[i].attribute, items[i].expire_time, items[i].bound, items[i].unique_id);
		if( tableswitch == TABLE_INVENTORY )
			StringBuf_Printf(&buf, ", '%d'", items[i].favorite);
		for( j = 0; j < MAX_SLOTS; ++j )
			StringBuf_Printf(&buf, ", '%hu'", items[i].card[j]);
		for( j = 0; j < MAX_ITEM_RDM_OPT; ++j ) {
			StringBuf_Printf(&buf, ", '%d'", items[i].option[j].id);
			StringBuf_Printf(&buf, ", '%d'", items[i].option[j].value);
			StringBuf_Printf(&buf, ", '%d'", items[i].option[j].param);
		}
		StringBuf_AppendStr(&buf, ")");
	}

	if( found && SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) ) {
		Sql_ShowDebug(sql_handle);
		errors++;
	}

	ShowInfo("Saved %s data to table %s for %s: %d\n", printname, tablename, selectoption, id);
	StringBuf_Destroy(&buf);
	aFree(flag);

	return errors;
}

bool memitemdata_from_sql(struct s_storage *p, int max, int id, enum storage_type tableswitch, uint8 stor_id) {
	StringBuf buf;
	SqlStmt *stmt;
	int i, offset = 0;
	struct item item, *storage;
	const char *tablename, *selectoption, *printname;

	switch( tableswitch ) {
		case TABLE_INVENTORY:
			printname = "Inventory";
			tablename = inventory_db;
			selectoption = "char_id";
			storage = p->u.items_inventory;
			break;
		case TABLE_CART:
			printname = "Cart";
			tablename = cart_db;
			selectoption = "char_id";
			storage = p->u.items_cart;
			break;
		case TABLE_STORAGE:
			printname = inter_premiumStorage_getPrintableName(stor_id);
			tablename = inter_premiumStorage_getTableName(stor_id);
			selectoption = "account_id";
			storage = p->u.items_storage;
			break;
		case TABLE_GUILD_STORAGE:
			printname = "Guild Storage";
			tablename = guild_storage_db;
			selectoption = "guild_id";
			storage = p->u.items_guild;
			break;
		default:
			ShowError("Invalid table name!\n");
			return false;
	}

	memset(p, 0, sizeof(struct s_storage)); //Clean up memory
	p->id = id;
	p->type = tableswitch;
	p->stor_id = stor_id;
	p->max_amount = inter_premiumStorage_getMax(p->stor_id);

	stmt = SqlStmt_Malloc(sql_handle);
	if( stmt == NULL ) {
		SqlStmt_ShowDebug(stmt);
		return false;
	}

	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `bound`, `unique_id`");
	if( tableswitch == TABLE_INVENTORY ) {
		StringBuf_Printf(&buf, ", `favorite`");
		offset = 1;
	}
	for( i = 0; i < MAX_SLOTS; ++i )
		StringBuf_Printf(&buf, ", `card%d`", i);
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		StringBuf_Printf(&buf, ", `option_id%d`", i);
		StringBuf_Printf(&buf, ", `option_val%d`", i);
		StringBuf_Printf(&buf, ", `option_parm%d`", i);
	}
	StringBuf_Printf(&buf, " FROM `%s` WHERE `%s`=? ORDER BY `nameid`", tablename, selectoption);

	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
	||  SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &id, 0)
	||  SQL_ERROR == SqlStmt_Execute(stmt)
	)
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		StringBuf_Destroy(&buf);
		return false;
	}

	SqlStmt_BindColumn(stmt, 0,  SQLDT_INT,           &item.id,          0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 1,  SQLDT_USHORT,        &item.nameid,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 2,  SQLDT_SHORT,         &item.amount,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 3,  SQLDT_UINT,          &item.equip,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 4,  SQLDT_CHAR,          &item.identify,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 5,  SQLDT_CHAR,          &item.refine,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 6,  SQLDT_CHAR,          &item.attribute,   0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 7,  SQLDT_UINT,          &item.expire_time, 0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 8,  SQLDT_CHAR,          &item.bound,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 9,  SQLDT_UINT64,        &item.unique_id,   0, NULL, NULL);
	if( tableswitch == TABLE_INVENTORY )
		SqlStmt_BindColumn(stmt, 10, SQLDT_CHAR,      &item.favorite,    0, NULL, NULL);
	for( i = 0; i < MAX_SLOTS; ++i )
		SqlStmt_BindColumn(stmt, 10+offset+i,             SQLDT_USHORT, &item.card[i],         0, NULL, NULL);
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		SqlStmt_BindColumn(stmt, 10+offset+MAX_SLOTS+i*3, SQLDT_SHORT,  &item.option[i].id,    0, NULL, NULL);
		SqlStmt_BindColumn(stmt, 11+offset+MAX_SLOTS+i*3, SQLDT_SHORT,  &item.option[i].value, 0, NULL, NULL);
		SqlStmt_BindColumn(stmt, 12+offset+MAX_SLOTS+i*3, SQLDT_CHAR,   &item.option[i].param, 0, NULL, NULL);
	}

	for( i = 0; i < max && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&storage[i], &item, sizeof(item));

	p->amount = i;
	ShowInfo("Loaded %s data from table %s for %s: %d (total: %d)\n", printname, tablename, selectoption, id, p->amount);

	SqlStmt_FreeResult(stmt);
	SqlStmt_Free(stmt);
	StringBuf_Destroy(&buf);

	return true;
}

/**
 * Returns the correct gender ID for the given character and enum value.
 *
 * If the per-character sex is defined but not supported by the current packetver, the database entries are corrected.
 *
 * @param sd Character data, if available.
 * @param p  Character status.
 * @param sex Character sex (database enum)
 *
 * @retval SEX_MALE if the per-character sex is male
 * @retval SEX_FEMALE if the per-character sex is female
 * @retval SEX_ACCOUNT if the per-character sex is not defined or the current PACKETVER doesn't support it.
 */
int mmo_gender(const struct char_session_data *sd, const struct mmo_charstatus *p, char sex)
{
#if PACKETVER >= 20141016
	(void)sd; (void)p; // Unused
	switch( sex ) {
		case 'M':
			return SEX_MALE;
		case 'F':
			return SEX_FEMALE;
		case 'U':
#if PACKETVER > 20151104
			{
				uint32 account_id;
				char *data, *a_sex;

				if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `account_id` FROM `%s` WHERE `char_id` = '%d'", char_db, p->char_id) )
					Sql_ShowDebug(sql_handle);

				if( SQL_SUCCESS != Sql_NextRow(sql_handle) ) {
					Sql_FreeResult(sql_handle);
					break;
				}

				Sql_GetData(sql_handle, 0, &data, NULL); account_id = atoi(data);
				Sql_FreeResult(sql_handle);

				if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `sex` FROM `login` WHERE `account_id` = '%d'", account_id) )
					Sql_ShowDebug(sql_handle);

				if( SQL_SUCCESS != Sql_NextRow(sql_handle) ) {
					Sql_FreeResult(sql_handle);
					break;
				}

				Sql_GetData(sql_handle, 0, &data, NULL); a_sex = data;
				Sql_FreeResult(sql_handle);
				return (a_sex[0] == 'M' ? SEX_MALE : SEX_FEMALE);
			}
#endif
			break;
	}
#else
	if( sex == 'M' || sex == 'F' ) {
		if( !sd ) { // sd is not available, there isn't much we can do. Just return and print a warning.
			ShowWarning("Character '%s' (CID: %d, AID: %d) has sex '%c', but PACKETVER does not support per-character sex. Defaulting to 'U'.\n", p->name, p->char_id, p->account_id, sex);
			return SEX_ACCOUNT;
		}
		if( (sex == 'M' && sd->sex == SEX_FEMALE) || (sex == 'F' && sd->sex == SEX_MALE) ) {
			ShowWarning("Changing sex of character '%s' (CID: %d, AID: %d) to 'U' due to incompatible PACKETVER.\n", p->name, p->char_id, p->account_id);
			loginif_parse_ackchangecharsex(p->char_id, sd->sex);
		} else
			ShowInfo("Resetting sex of character '%s' (CID: %d, AID: %d) to 'U' due to incompatible PACKETVER.\n", p->name, p->char_id, p->account_id);
		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `sex` = 'U' WHERE `char_id` = '%d'", char_db, p->char_id) )
			Sql_ShowDebug(sql_handle);
	}
#endif
	return SEX_ACCOUNT;
}

int mmo_char_tobuf(uint8 *buf, struct mmo_charstatus *p);

//=====================================================================================================
// Loads the basic character rooster for the given account. Returns total buffer used.
int mmo_chars_fromsql(struct char_session_data *sd, uint8 *buf)
{
	SqlStmt *stmt;
	struct mmo_charstatus p;
	int j = 0, i;
	char last_map[MAP_NAME_LENGTH_EXT];
	char sex[2];

	stmt = SqlStmt_Malloc(sql_handle);
	if( stmt == NULL ) {
		SqlStmt_ShowDebug(stmt);
		return 0;
	}
	memset(&p, 0, sizeof(p));

	for( i = 0; i < MAX_CHARS; i++ ) {
		sd->found_char[i] = -1;
		sd->unban_time[i] = 0;
	}

	// Read char data
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT "
		"`char_id`,`char_num`,`name`,`class`,`base_level`,`job_level`,`base_exp`,`job_exp`,`zeny`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,`max_hp`,`hp`,`max_sp`,`sp`,"
		"`status_point`,`skill_point`,`option`,`karma`,`manner`,`hair`,`hair_color`,"
		"`clothes_color`,`body`,`weapon`,`shield`,`head_top`,`head_mid`,`head_bottom`,`last_map`,`rename`,`delete_date`,"
		"`robe`,`moves`,`unban_time`,`sex`,`hotkey_rowshift`"
		" FROM `%s` WHERE `account_id`='%d' AND `char_num` < '%d'", char_db, sd->account_id, MAX_CHARS)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0,  SQLDT_INT,    &p.char_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1,  SQLDT_UCHAR,  &p.slot, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2,  SQLDT_STRING, &p.name, sizeof(p.name), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 3,  SQLDT_SHORT,  &p.class_, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 4,  SQLDT_UINT,   &p.base_level, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 5,  SQLDT_UINT,   &p.job_level, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 6,  SQLDT_UINT,   &p.base_exp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 7,  SQLDT_UINT,   &p.job_exp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 8,  SQLDT_INT,    &p.zeny, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 9,  SQLDT_SHORT,  &p.str, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 10, SQLDT_SHORT,  &p.agi, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 11, SQLDT_SHORT,  &p.vit, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 12, SQLDT_SHORT,  &p.int_, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 13, SQLDT_SHORT,  &p.dex, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 14, SQLDT_SHORT,  &p.luk, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 15, SQLDT_UINT,   &p.max_hp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 16, SQLDT_UINT,   &p.hp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 17, SQLDT_UINT,   &p.max_sp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 18, SQLDT_UINT,   &p.sp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 19, SQLDT_UINT,   &p.status_point, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 20, SQLDT_UINT,   &p.skill_point, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 21, SQLDT_UINT,   &p.option, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 22, SQLDT_UCHAR,  &p.karma, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 23, SQLDT_SHORT,  &p.manner, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 24, SQLDT_SHORT,  &p.hair, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 25, SQLDT_SHORT,  &p.hair_color, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 26, SQLDT_SHORT,  &p.clothes_color, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 27, SQLDT_SHORT,  &p.body, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 28, SQLDT_SHORT,  &p.weapon, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 29, SQLDT_SHORT,  &p.shield, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 30, SQLDT_SHORT,  &p.head_top, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 31, SQLDT_SHORT,  &p.head_mid, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 32, SQLDT_SHORT,  &p.head_bottom, 0, NULL, NULL)
	||  SQL_ERROR == SqlStmt_BindColumn(stmt, 33, SQLDT_STRING, &last_map, sizeof(last_map), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 34, SQLDT_SHORT,	&p.rename, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 35, SQLDT_UINT32, &p.delete_date, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 36, SQLDT_SHORT,  &p.robe, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 37, SQLDT_UINT,   &p.character_moves, 0, NULL, NULL)
	||  SQL_ERROR == SqlStmt_BindColumn(stmt, 38, SQLDT_LONG,   &p.unban_time, 0, NULL, NULL)
	||  SQL_ERROR == SqlStmt_BindColumn(stmt, 39, SQLDT_ENUM,   &sex, sizeof(sex), NULL, NULL)
	||  SQL_ERROR == SqlStmt_BindColumn(stmt, 40, SQLDT_UCHAR,  &p.hotkey_rowshift, 0, NULL, NULL)
	)
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		return 0;
	}

	for( i = 0; i < MAX_CHARS && SQL_SUCCESS == SqlStmt_NextRow(stmt); i++ ) {
		p.last_point.map = mapindex_name2id(last_map);
		sd->found_char[p.slot] = p.char_id;
		sd->unban_time[p.slot] = p.unban_time;
		p.sex = mmo_gender(sd, &p, sex[0]);
		j += mmo_char_tobuf(WBUFP(buf,j), &p);

		// Addon System
		// Store the required info into the session
		sd->char_moves[p.slot] = p.character_moves;
	}

	memset(sd->new_name, 0, sizeof(sd->new_name));

	SqlStmt_Free(stmt);
	return j;
}

//=====================================================================================================
int mmo_char_fromsql(int char_id, struct mmo_charstatus *p, bool load_everything)
{
	int i;
	char t_msg[128] = "";
	struct mmo_charstatus *cp;
	SqlStmt *stmt;
	char last_map[MAP_NAME_LENGTH_EXT];
	char save_map[MAP_NAME_LENGTH_EXT];
	char point_map[MAP_NAME_LENGTH_EXT];
	struct point tmp_point;
	struct s_skill tmp_skill;
	struct s_friend tmp_friend;
#ifdef HOTKEY_SAVING
	struct hotkey tmp_hotkey;
	int hotkey_num;
#endif
	unsigned int opt;
	char sex[2];

	memset(p, 0, sizeof(struct mmo_charstatus));

	if( save_log )
		ShowInfo("Char load request (%d)\n", char_id);

	stmt = SqlStmt_Malloc(sql_handle);
	if( stmt == NULL ) {
		SqlStmt_ShowDebug(stmt);
		return 0;
	}

	//Read char data
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT "
		"`char_id`,`account_id`,`char_num`,`name`,`class`,`base_level`,`job_level`,`base_exp`,`job_exp`,`zeny`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,`max_hp`,`hp`,`max_sp`,`sp`,"
		"`status_point`,`skill_point`,`option`,`karma`,`manner`,`party_id`,`guild_id`,`pet_id`,`homun_id`,`elemental_id`,`hair`,"
		"`hair_color`,`clothes_color`,`body`,`weapon`,`shield`,`head_top`,`head_mid`,`head_bottom`,`last_map`,`last_x`,`last_y`,"
		"`save_map`,`save_x`,`save_y`,`partner_id`,`father`,`mother`,`child`,`fame`,`rename`,`delete_date`,`robe`,`moves`,"
		"`char_opt`,`font`,`unban_time`,`uniqueitem_counter`,`sex`,`hotkey_rowshift`,`clan_id`,`title_id`"
		" FROM `%s` WHERE `char_id`=? LIMIT 1", char_db)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0,  SQLDT_INT,    &p->char_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1,  SQLDT_INT,    &p->account_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2,  SQLDT_UCHAR,  &p->slot, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 3,  SQLDT_STRING, &p->name, sizeof(p->name), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 4,  SQLDT_SHORT,  &p->class_, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 5,  SQLDT_UINT,   &p->base_level, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 6,  SQLDT_UINT,   &p->job_level, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 7,  SQLDT_UINT,   &p->base_exp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 8,  SQLDT_UINT,   &p->job_exp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 9,  SQLDT_INT,    &p->zeny, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 10, SQLDT_SHORT,  &p->str, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 11, SQLDT_SHORT,  &p->agi, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 12, SQLDT_SHORT,  &p->vit, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 13, SQLDT_SHORT,  &p->int_, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 14, SQLDT_SHORT,  &p->dex, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 15, SQLDT_SHORT,  &p->luk, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 16, SQLDT_UINT,   &p->max_hp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 17, SQLDT_UINT,   &p->hp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 18, SQLDT_UINT,   &p->max_sp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 19, SQLDT_UINT,   &p->sp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 20, SQLDT_UINT,   &p->status_point, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 21, SQLDT_UINT,   &p->skill_point, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 22, SQLDT_UINT,   &p->option, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 23, SQLDT_UCHAR,  &p->karma, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 24, SQLDT_SHORT,  &p->manner, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 25, SQLDT_INT,    &p->party_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 26, SQLDT_INT,    &p->guild_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 27, SQLDT_INT,    &p->pet_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 28, SQLDT_INT,    &p->hom_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 29, SQLDT_INT,	&p->ele_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 30, SQLDT_SHORT,  &p->hair, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 31, SQLDT_SHORT,  &p->hair_color, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 32, SQLDT_SHORT,  &p->clothes_color, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 33, SQLDT_SHORT,  &p->body, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 34, SQLDT_SHORT,  &p->weapon, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 35, SQLDT_SHORT,  &p->shield, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 36, SQLDT_SHORT,  &p->head_top, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 37, SQLDT_SHORT,  &p->head_mid, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 38, SQLDT_SHORT,  &p->head_bottom, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 39, SQLDT_STRING, &last_map, sizeof(last_map), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 40, SQLDT_SHORT,  &p->last_point.x, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 41, SQLDT_SHORT,  &p->last_point.y, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 42, SQLDT_STRING, &save_map, sizeof(save_map), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 43, SQLDT_SHORT,  &p->save_point.x, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 44, SQLDT_SHORT,  &p->save_point.y, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 45, SQLDT_INT,    &p->partner_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 46, SQLDT_INT,    &p->father, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 47, SQLDT_INT,    &p->mother, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 48, SQLDT_INT,    &p->child, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 49, SQLDT_INT,    &p->fame, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 50, SQLDT_SHORT,	&p->rename, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 51, SQLDT_UINT32, &p->delete_date, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 52, SQLDT_SHORT,  &p->robe, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 53, SQLDT_UINT32, &p->character_moves, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 54, SQLDT_UINT,   &opt, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 55, SQLDT_UCHAR,  &p->font, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 56, SQLDT_LONG,   &p->unban_time, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 57, SQLDT_UINT32, &p->uniqueitem_counter, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 58, SQLDT_ENUM,   &sex, sizeof(sex), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 59, SQLDT_UCHAR,  &p->hotkey_rowshift, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 60, SQLDT_INT,    &p->clan_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 61, SQLDT_ULONG,	&p->title_id, 0, NULL, NULL)
	)
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		return 0;
	}
	if( SQL_SUCCESS != SqlStmt_NextRow(stmt) ) {
		ShowError("Requested non-existant character id: %d!\n", char_id);
		SqlStmt_Free(stmt);
		return 0;
	}

	p->sex = mmo_gender(NULL, p, sex[0]);
	p->last_point.map = mapindex_name2id(last_map);
	p->save_point.map = mapindex_name2id(save_map);

	if( p->last_point.map == 0 ) {
		p->last_point.map = mapindex_name2id(default_map);
		p->last_point.x = default_map_x;
		p->last_point.y = default_map_y;
	}

	if( p->save_point.map == 0 ) {
		p->save_point.map = mapindex_name2id(default_map);
		p->save_point.x = default_map_x;
		p->save_point.y = default_map_y;
	}

	strcat(t_msg, " status");

	if (!load_everything) { //For quick selection of data when displaying the char menu
		SqlStmt_Free(stmt);
		return 1;
	}

	//Read memo data
	//`memo` (`memo_id`,`char_id`,`map`,`x`,`y`)
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `map`,`x`,`y` FROM `%s` WHERE `char_id`=? ORDER by `memo_id` LIMIT %d", memo_db, MAX_MEMOPOINTS)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_STRING, &point_map, sizeof(point_map), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_SHORT,  &tmp_point.x, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,  &tmp_point.y, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	for( i = 0; i < MAX_MEMOPOINTS && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i ) {
		tmp_point.map = mapindex_name2id(point_map);
		memcpy(&p->memo_point[i], &tmp_point, sizeof(tmp_point));
	}
	strcat(t_msg, " memo");

	//Read skill
	//`skill` (`char_id`, `id`, `lv`)
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `id`, `lv`,`flag` FROM `%s` WHERE `char_id`=? LIMIT %d", skill_db, MAX_SKILL)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_USHORT, &tmp_skill.id  , 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_UCHAR , &tmp_skill.lv  , 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_UCHAR , &tmp_skill.flag, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	if( tmp_skill.flag != SKILL_FLAG_PERM_GRANTED )
		tmp_skill.flag = SKILL_FLAG_PERMANENT;

	for( i = 0; i < MAX_SKILL && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i ) {
		if( tmp_skill.id < ARRAYLENGTH(p->skill) )
			memcpy(&p->skill[tmp_skill.id], &tmp_skill, sizeof(tmp_skill));
		else
			ShowWarning("mmo_char_fromsql: ignoring invalid skill (id=%u,lv=%u) of character %s (AID=%d,CID=%d)\n", tmp_skill.id, tmp_skill.lv, p->name, p->account_id, p->char_id);
	}
	strcat(t_msg, " skills");

	//Read friends
	//`friends` (`char_id`, `friend_account`, `friend_id`)
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT c.`account_id`, c.`char_id`, c.`name` FROM `%s` c LEFT JOIN `%s` f ON f.`friend_account` = c.`account_id` AND f.`friend_id` = c.`char_id` WHERE f.`char_id`=? LIMIT %d", char_db, friend_db, MAX_FRIENDS)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_INT,    &tmp_friend.account_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_INT,    &tmp_friend.char_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_STRING, &tmp_friend.name, sizeof(tmp_friend.name), NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	for( i = 0; i < MAX_FRIENDS && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->friends[i], &tmp_friend, sizeof(tmp_friend));
	strcat(t_msg, " friends");

#ifdef HOTKEY_SAVING
	//Read hotkeys
	//`hotkey` (`char_id`, `hotkey`, `type`, `itemskill_id`, `skill_lvl`
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `hotkey`, `type`, `itemskill_id`, `skill_lvl` FROM `%s` WHERE `char_id`=?", hotkey_db)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_INT,    &hotkey_num, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_UCHAR,  &tmp_hotkey.type, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_UINT,   &tmp_hotkey.id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 3, SQLDT_USHORT, &tmp_hotkey.lv, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	while( SQL_SUCCESS == SqlStmt_NextRow(stmt) ) {
		if( hotkey_num >= 0 && hotkey_num < MAX_HOTKEYS )
			memcpy(&p->hotkeys[hotkey_num], &tmp_hotkey, sizeof(tmp_hotkey));
		else
			ShowWarning("mmo_char_fromsql: ignoring invalid hotkey (hotkey=%d,type=%u,id=%u,lv=%u) of character %s (AID=%d,CID=%d)\n", hotkey_num, tmp_hotkey.type, tmp_hotkey.id, tmp_hotkey.lv, p->name, p->account_id, p->char_id);
	}
	strcat(t_msg, " hotkeys");
#endif

	//Mercenary owner database
	mercenary_owner_fromsql(char_id, p);
	strcat(t_msg, " mercenary");

	if( save_log )
		ShowInfo("Loaded char (%d - %s): %s\n", char_id, p->name, t_msg); //All data load successfuly!
	SqlStmt_Free(stmt);

	//Load options into proper vars
	if( opt&OPT_ALLOW_PARTY )
		p->allow_party = true;
	if( opt&OPT_SHOW_EQUIP )
		p->show_equip = true;

	cp = idb_ensure(char_db_, char_id, create_charstatus);
	memcpy(cp, p, sizeof(struct mmo_charstatus));
	return 1;
}

//==========================================================================================================
int mmo_char_sql_init(void)
{
	char_db_= idb_alloc(DB_OPT_RELEASE_DATA);

	//the 'set offline' part is now in check_login_conn ...
	//if the server connects to loginserver
	//it will dc all off players
	//and send the loginserver the new state....

	// Force all users offline in sql when starting char-server
	// (useful when servers crashs and don't clean the database)
	set_all_offline_sql();

	return 0;
}

//-----------------------------------
// Function to change chararcter's names
//-----------------------------------
int check_char_name(char *name, char *esc_name)
{
	int i;

	// Check length of character name
	if( name[0] == '\0' )
		return -2; // Empty character name
	/**
	 * The client does not allow you to create names with less than 4 characters, however,
	 * the use of WPE can bypass this, and this fixes the exploit.
	 */
	if( strlen( name ) < 4 )
		return -2;
	// Check content of character name
	if( remove_control_chars(name) )
		return -2; // Control chars in name
	// Check for reserved names
	if( strcmpi(name, wisp_server_name) == 0 )
		return -1; // Nick reserved for internal server messages
	// Check for the channel symbol
	if( name[0] == '#' )
		return -2;
	// Check authorized letters/symbols in the name of the character
	if( char_name_option == 1 ) { // Only letters/symbols in char_name_letters are authorized
		for( i = 0; i < NAME_LENGTH && name[i]; i++ )
			if( strchr(char_name_letters, name[i]) == NULL )
				return -2;
	} else if( char_name_option == 2 ) { // Letters/symbols in char_name_letters are forbidden
		for( i = 0; i < NAME_LENGTH && name[i]; i++ )
			if( strchr(char_name_letters, name[i]) != NULL )
				return -5;
	}
	if( name_ignoring_case ) {
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT 1 FROM `%s` WHERE BINARY `name` = '%s' LIMIT 1", char_db, esc_name) ) {
			Sql_ShowDebug(sql_handle);
			return -2;
		}
	} else {
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT 1 FROM `%s` WHERE `name` = '%s' LIMIT 1", char_db, esc_name) ) {
			Sql_ShowDebug(sql_handle);
			return -2;
		}
	}
	if( Sql_NumRows(sql_handle) > 0 )
		return -1; // Name already exists

	return 0;
}

int rename_char_sql(struct char_session_data *sd, int char_id)
{
	struct mmo_charstatus char_dat;
	char esc_name[NAME_LENGTH * 2 + 1];

	if( !sd->new_name[0] ) // Not ready for rename
		return 2;

	if( !mmo_char_fromsql(char_id, &char_dat, false) ) // Only the short data is needed
		return 2;

	if( !strcmp(sd->new_name, char_dat.name) ) // If the new name is exactly the same as the old one
		return 0;

	if( !char_dat.rename )
		return 1;

	if( !char_rename_party && char_dat.party_id )
		return 6;

	if( !char_rename_guild && char_dat.guild_id )
		return 5;

	Sql_EscapeStringLen(sql_handle, esc_name, sd->new_name, strnlen(sd->new_name, NAME_LENGTH));

	switch( check_char_name(sd->new_name, esc_name) ) {
		case 0:
			break;
		case -1: // Character already exists
			return 4;
		default:
			return 8;
	}

	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `name` = '%s', `rename` = '%d' WHERE `char_id` = '%d'",
		char_db, esc_name, --char_dat.rename, char_id) ) {
		Sql_ShowDebug(sql_handle);
		return 3;
	}

	if( char_dat.party_id ) // Update party and party members with the new player name
		inter_party_charname_changed(char_dat.party_id, char_id, sd->new_name);

	if( char_dat.guild_id ) // Change character's name into guild_db
		inter_guild_charname_changed(char_dat.guild_id, sd->account_id, char_id, sd->new_name);

	safestrncpy(char_dat.name, sd->new_name, NAME_LENGTH);
	memset(sd->new_name, 0, sizeof(sd->new_name));

	if( log_char ) { // Log change
		if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`time`, `char_msg`,`account_id`,`char_id`,`char_num`,`name`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,`hair`,`hair_color`)"
			"VALUES (NOW(), '%s', '%d', '%d', '%d', '%s', '0', '0', '0', '0', '0', '0', '0', '0')",
			charlog_db, "change char name", sd->account_id, char_dat.char_id, char_dat.slot, esc_name) )
			Sql_ShowDebug(sql_handle);
	}

	return 0;
}

/**
 * Creates a new character
 * Return values:
 *  -1: 'Charname already exists'
 *  -2: 'Char creation denied'/ Unknown error
 *  -3: 'You are underaged'
 *  -4: 'You are not elegible to open the Character Slot.'
 *  -5: 'Symbols in Character Names are forbidden'
 *  char_id: Success
 */
#if PACKETVER >= 20120307
#if PACKETVER >= 20151104
int make_new_char_sql(struct char_session_data *sd, char *name_, int slot, int hair_color, int hair_style, short start_job, short unknown, int sex) { //TODO: Unknown byte
#else
int make_new_char_sql(struct char_session_data *sd, char *name_, int slot, int hair_color, int hair_style) {
#endif
	int str = 1, agi = 1, vit = 1, int_ = 1, dex = 1, luk = 1;
#else
int make_new_char_sql(struct char_session_data *sd, char *name_, int str, int agi, int vit, int int_, int dex, int luk, int slot, int hair_color, int hair_style) {
#endif

	char name[NAME_LENGTH];
	char esc_name[NAME_LENGTH * 2 + 1];
	struct point tmp_start_point[MAX_STARTPOINT];
	struct startitem tmp_start_items[MAX_STARTITEM];
	int char_id, flag, k, start_point_idx = rnd()%start_point_count;
	short start_hp, start_sp;

	safestrncpy(name, name_, NAME_LENGTH);
	normalize_name(name,TRIM_CHARS);
	Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));

	memset(tmp_start_point, 0, MAX_STARTPOINT * sizeof(struct point));
	memset(tmp_start_items, 0, MAX_STARTITEM * sizeof(struct startitem));
	memcpy(tmp_start_point, start_point, MAX_STARTPOINT * sizeof(struct point));
	memcpy(tmp_start_items, start_items, MAX_STARTITEM * sizeof(struct startitem));

	flag = check_char_name(name,esc_name);
	if(flag < 0)
		return flag;

	//Check other inputs
#if PACKETVER >= 20120307
#if PACKETVER >= 20151104
	switch(sex) {
			case SEX_FEMALE:
				sex = 'F';
				break;
			case SEX_MALE:
				sex = 'M';
				break;
			default:
				sex = 'U';
				break;
	}
#endif
	if(slot < 0 || slot >= sd->char_slots)
#else
	if((slot < 0 || slot >= sd->char_slots) //Slots
	|| (str + agi + vit + int_ + dex + luk != 6 * 5) //Stats
	|| (str < 1 || str > 9 || agi < 1 || agi > 9 || vit < 1 || vit > 9 || int_ < 1 || int_ > 9 || dex < 1 || dex > 9 || luk < 1 || luk > 9) //Individual stat values
	|| (str + int_ != 10 || agi + luk != 10 || vit + dex != 10)) //Pairs
#endif
#if PACKETVER >= 20100413
		return -4; //Invalid slot
#else
		return -2; //Invalid input
#endif

	//Check char slot
	if(sd->found_char[slot] != -1)
		return -2; //Character account limit exceeded

	start_hp = 40 * (100 + vit) / 100;
	start_sp = 11 * (100 + int_) / 100;

#if PACKETVER >= 20151104
	if(start_job == JOB_SUMMONER) { //Doram race
		start_hp = 60 * (100 + vit) / 100;
		start_sp = 8 * (100 + int_) / 100;
		memset(tmp_start_point, 0, MAX_STARTPOINT * sizeof(struct point));
		memset(tmp_start_items, 0, MAX_STARTITEM * sizeof(struct startitem));
		memcpy(tmp_start_point, start_point_doram, MAX_STARTPOINT * sizeof(struct point));
		memcpy(tmp_start_items, start_items_doram, MAX_STARTITEM * sizeof(struct startitem));
		start_point_idx = rnd()%start_point_count_doram;
	} else if(start_job != JOB_NOVICE) {
		ShowWarning("make_new_char_sql: Detected character creation packet with invalid race type on account: %d.\n", sd->account_id);
		return -2; //Invalid job
	}
#endif

	//Insert the new char entry to the database
#if PACKETVER >= 20151104
	if(SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`account_id`, `char_num`, `name`, `class`, `zeny`, `status_point`, `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`,"
		"`max_sp`, `sp`, `hair`, `hair_color`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`, `sex`) VALUES ("
		"'%d', '%d', '%s', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%u', '%u', '%u', '%u', '%d', '%d', '%s', '%d', '%d', '%s', '%d', '%d', '%c')",
		char_db, sd->account_id, slot, esc_name, start_job, start_zeny, 48, str, agi, vit, int_, dex, luk, start_hp, start_hp, start_sp, start_sp, hair_style, hair_color,
		mapindex_id2name(tmp_start_point[start_point_idx].map), tmp_start_point[start_point_idx].x, tmp_start_point[start_point_idx].y, mapindex_id2name(tmp_start_point[start_point_idx].map), tmp_start_point[start_point_idx].x, tmp_start_point[start_point_idx].y, sex))
#elif PACKETVER >= 20120307
	if(SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`account_id`, `char_num`, `name`, `zeny`, `status_point`, `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`,"
		"`max_sp`, `sp`, `hair`, `hair_color`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES ("
		"'%d', '%d', '%s', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%u', '%u', '%u', '%u', '%d', '%d', '%s', '%d', '%d', '%s', '%d', '%d')",
		char_db, sd->account_id, slot, esc_name, start_zeny, 48, str, agi, vit, int_, dex, luk, start_hp, start_hp, start_sp, start_sp, hair_style, hair_color,
		mapindex_id2name(tmp_start_point[start_point_idx].map), tmp_start_point[start_point_idx].x, tmp_start_point[start_point_idx].y, mapindex_id2name(tmp_start_point[start_point_idx].map), tmp_start_point[start_point_idx].x, tmp_start_point[start_point_idx].y))
#else
	if(SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`account_id`, `char_num`, `name`, `zeny`, `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`,"
		"`max_sp`, `sp`, `hair`, `hair_color`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES ("
		"'%d', '%d', '%s', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%u', '%u', '%u', '%u', '%d', '%d', '%s', '%d', '%d', '%s', '%d', '%d')",
		char_db, sd->account_id, slot, esc_name, start_zeny, str, agi, vit, int_, dex, luk, start_hp, start_hp, start_sp, start_sp, hair_style, hair_color,
		mapindex_id2name(tmp_start_point[start_point_idx].map), tmp_start_point[start_point_idx].x, tmp_start_point[start_point_idx].y, mapindex_id2name(tmp_start_point[start_point_idx].map), tmp_start_point[start_point_idx].x, tmp_start_point[start_point_idx].y))
#endif
	{
		Sql_ShowDebug(sql_handle);
		return -2; //No, stop the procedure!
	}

	//Retrieve the newly auto-generated char id
	char_id = (int)Sql_LastInsertId(sql_handle);

	if(!char_id)
		return -2;

	//Validation success, log result
	if(log_char) {
		if(SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`time`, `char_msg`,`account_id`,`char_id`,`char_num`,`name`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,`hair`,`hair_color`)"
			"VALUES (NOW(), '%s', '%d', '%d', '%d', '%s', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d')",
			charlog_db, "make new char", sd->account_id, char_id, slot, esc_name, str, agi, vit, int_, dex, luk, hair_style, hair_color))
			Sql_ShowDebug(sql_handle);
	}

	//Give the char the default items
	for(k = 0; k <= MAX_STARTITEM && tmp_start_items[k].nameid != 0; k ++) {
		if(SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`char_id`,`nameid`, `amount`, `equip`, `identify`) VALUES ('%d', '%hu', '%d', '%d', '%d')",
			inventory_db, char_id, tmp_start_items[k].nameid, tmp_start_items[k].amount, tmp_start_items[k].pos, 1))
			Sql_ShowDebug(sql_handle);
	}

	ShowInfo("Created char: account: %d, char: %d, slot: %d, name: %s\n", sd->account_id, char_id, slot, name);
	return char_id;
}

/*----------------------------------------------------------------------------------------------------------*/
/* Divorce Players */
/*----------------------------------------------------------------------------------------------------------*/
int divorce_char_sql(int partner_id1, int partner_id2)
{
	unsigned char buf[64];

	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `partner_id`='0' WHERE `char_id`='%d' OR `char_id`='%d' LIMIT 2", char_db, partner_id1, partner_id2) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE (`nameid`='%hu' OR `nameid`='%hu') AND (`char_id`='%d' OR `char_id`='%d') LIMIT 2", inventory_db, WEDDING_RING_M, WEDDING_RING_F, partner_id1, partner_id2) )
		Sql_ShowDebug(sql_handle);

	WBUFW(buf,0) = 0x2b12;
	WBUFL(buf,2) = partner_id1;
	WBUFL(buf,6) = partner_id2;
	mapif_sendall(buf,10);

	return 0;
}

/*----------------------------------------------------------------------------------------------------------*/
/* Delete char - davidsiaw */
/*----------------------------------------------------------------------------------------------------------*/
/* Returns 0 if successful
 * Returns < 0 for error
 */
enum e_char_del_response char_delete(struct char_session_data *sd, uint32 char_id)
{
	char name[NAME_LENGTH];
	char esc_name[NAME_LENGTH * 2 + 1]; //Name needs be escaped
	int account_id, party_id, guild_id, hom_id, base_level, partner_id, father_id, mother_id, elemental_id;
	time_t delete_date;
	char *data;
	size_t len;
	int i;

	ARR_FIND(0, MAX_CHARS, i, sd->found_char[i] == char_id);
	if( i == MAX_CHARS ) { //Such a character does not exist in the account
		ShowInfo("Char deletion aborted: %s, Account ID: %u, Character ID: %u\n", name, sd->account_id, char_id);
		return CHAR_DELETE_NOTFOUND;
	}

	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `name`,`account_id`,`party_id`,`guild_id`,`base_level`,`homun_id`,`partner_id`,`father`,`mother`,`elemental_id`,`delete_date` FROM `%s` WHERE `account_id`='%u' AND `char_id`='%u'", char_db, sd->account_id, char_id) ) {
		Sql_ShowDebug(sql_handle);
		return CHAR_DELETE_DATABASE;
	}

	if( SQL_SUCCESS != Sql_NextRow(sql_handle) ) {
		ShowInfo("Char deletion aborted: %s, Account ID: %u, Character ID: %u\n", name, sd->account_id, char_id);
		Sql_FreeResult(sql_handle);
		return CHAR_DELETE_NOTFOUND;
	}

	Sql_GetData(sql_handle, 0, &data, &len); safestrncpy(name, data, NAME_LENGTH);
	Sql_GetData(sql_handle, 1, &data, NULL); account_id = atoi(data);
	Sql_GetData(sql_handle, 2, &data, NULL); party_id = atoi(data);
	Sql_GetData(sql_handle, 3, &data, NULL); guild_id = atoi(data);
	Sql_GetData(sql_handle, 4, &data, NULL); base_level = atoi(data);
	Sql_GetData(sql_handle, 5, &data, NULL); hom_id = atoi(data);
	Sql_GetData(sql_handle, 6, &data, NULL); partner_id = atoi(data);
	Sql_GetData(sql_handle, 7, &data, NULL); father_id = atoi(data);
	Sql_GetData(sql_handle, 8, &data, NULL); mother_id = atoi(data);
	Sql_GetData(sql_handle, 9, &data, NULL); elemental_id = atoi(data);
	Sql_GetData(sql_handle,10, &data, NULL); delete_date = strtoul(data, NULL, 10);

	Sql_EscapeStringLen(sql_handle, esc_name, name, zmin(len, NAME_LENGTH));
	Sql_FreeResult(sql_handle);

	//Check for config char del condition [Lupus]
	if( (char_del_level > 0 && base_level >= char_del_level) || (char_del_level < 0 && base_level <= -char_del_level) ) {
		ShowInfo("Char deletion aborted: %s, BaseLevel: %i\n", name, base_level);
		return CHAR_DELETE_BASELEVEL;
	}

	if( (char_del_restriction&CHAR_DEL_RESTRICT_GUILD) && guild_id ) { //Character is in guild
		ShowInfo("Char deletion aborted: %s, Guild ID: %i\n", name, guild_id);
		return CHAR_DELETE_GUILD;
	}

	if( (char_del_restriction&CHAR_DEL_RESTRICT_PARTY) && party_id ) { //Character is in party
		ShowInfo("Char deletion aborted: %s, Party ID: %i\n", name, party_id);
		return CHAR_DELETE_PARTY;
	}

	if( char_del_delay > 0 && (!delete_date || delete_date > time(NULL)) ) { //Not queued or delay not yet passed
		ShowInfo("Char deletion aborted: %s, Time was not set or has not been reached ye\n", name);
		return CHAR_DELETE_TIME;
	}

	/* Divorce [Wizputer] */
	if( partner_id )
		divorce_char_sql(char_id, partner_id);

	/* De-addopt [Zephyrus] */
	if( father_id || mother_id ) { //Char is Baby
		unsigned char buf[64];

		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `child`='0' WHERE `char_id`='%d' OR `char_id`='%d'", char_db, father_id, mother_id) )
			Sql_ShowDebug(sql_handle);
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `id` = '410' AND (`char_id`='%d' OR `char_id`='%d')", skill_db, father_id, mother_id) )
			Sql_ShowDebug(sql_handle);

		WBUFW(buf,0) = 0x2b25;
		WBUFL(buf,2) = father_id;
		WBUFL(buf,6) = mother_id;
		WBUFL(buf,10) = char_id; //Baby
		mapif_sendall(buf,14);
	}

	//Make the character leave the party [Skotlex]
	if( party_id )
		inter_party_leave(party_id, account_id, char_id, name);

	/* Delete char's pet */
	//Delete the hatched pet if you have one.
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d' AND `incubate` = '0'", pet_db, char_id) )
		Sql_ShowDebug(sql_handle);

	//Delete all pets that are stored in eggs (inventory + cart)
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` USING `%s` JOIN `%s` ON `pet_id` = `card1`|`card2`<<16 WHERE `%s`.char_id = '%d' AND card0 = 256", pet_db, pet_db, inventory_db, inventory_db, char_id) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` USING `%s` JOIN `%s` ON `pet_id` = `card1`|`card2`<<16 WHERE `%s`.char_id = '%d' AND card0 = 256", pet_db, pet_db, cart_db, cart_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Remove homunculus */
	if( hom_id )
		mapif_homunculus_delete(hom_id);

	/* Remove elemental */
	if( elemental_id )
		mapif_elemental_delete(elemental_id);

	/* Remove mercenary data */
	mercenary_owner_delete(char_id);

	/* Delete char's friends list */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id` = '%d'", friend_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Delete char from other's friend list */
	//NOTE: Won't this cause problems for people who are already online? [Skotlex]
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `friend_id` = '%d'", friend_db, char_id) )
		Sql_ShowDebug(sql_handle);

#ifdef HOTKEY_SAVING
	/* Delete hotkeys */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", hotkey_db, char_id) )
		Sql_ShowDebug(sql_handle);
#endif

	/* Delete inventory */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", inventory_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Delete cart inventory */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", cart_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Delete memo areas */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", memo_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Delete character registry */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `type`=3 AND `char_id`='%d'", reg_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Delete skills */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", skill_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Delete mail attachments (only received) */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `id` IN ( SELECT `id` FROM `%s` WHERE `dest_id`='%d' )", mail_attachment_db, mail_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Delete mails (only received) */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `dest_id`='%d'", mail_db, char_id) )
		Sql_ShowDebug(sql_handle);

#ifdef ENABLE_SC_SAVING
	/* Status changes */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `account_id` = '%d' AND `char_id`='%d'", scdata_db, account_id, char_id) )
		Sql_ShowDebug(sql_handle);
#endif

	/* Bonus Scripts */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id` = '%d'", bonus_script_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Achievement Data */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id` = '%d'", achievement_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Delete character */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", char_db, char_id) )
		Sql_ShowDebug(sql_handle);
	else if( log_char ) {
		if( SQL_ERROR == Sql_Query(sql_handle,
			"INSERT INTO `%s`(`time`, `account_id`, `char_id`, `char_num`, `char_msg`, `name`)"
			"VALUES (NOW(), '%d', '%d', '%d', 'Deleted character', '%s')",
			charlog_db, account_id, char_id, 0, esc_name) )
			Sql_ShowDebug(sql_handle);
	}

	/* No need as we used inter_guild_leave [Skotlex]
	//Also delete info from guildtables.
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", guild_member_db, char_id) )
		Sql_ShowDebug(sql_handle);
	*/

	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `guild_id` FROM `%s` WHERE `char_id` = '%d'", guild_db, char_id) )
		Sql_ShowDebug(sql_handle);
	else if( Sql_NumRows(sql_handle) > 0 )
		mapif_parse_BreakGuild(0, guild_id);
	else if( guild_id )
		inter_guild_leave(guild_id, account_id, char_id); //Leave your guild.

	//Refresh character list cache
	sd->found_char[i] = -1;

	return CHAR_DELETE_OK;
}

//---------------------------------------------------------------------
// This function return the number of online players in all map-servers
//---------------------------------------------------------------------
int count_users(void)
{
	int i, users;

	users = 0;
	for( i = 0; i < ARRAYLENGTH(server); i++ ) {
		if( server[i].fd > 0 )
			users += server[i].users;
	}
	return users;
}

//Writes char data to the buffer in the format used by the client.
//Used in packets 0x6b (chars info) and 0x6d (new char info)
//Returns the size
#define MAX_CHAR_BUF 150 //Max size (for WFIFOHEAD calls)
int mmo_char_tobuf(uint8 *buffer, struct mmo_charstatus *p)
{
	unsigned short offset = 0;
	uint8 *buf;

	if( buffer == NULL || p == NULL )
		return 0;

	buf = WBUFP(buffer,0);
	WBUFL(buf,0) = p->char_id;
#if PACKETVER >= 20170830
	WBUFQ(buf,4) = u64min((uint64)p->base_exp,INT64_MAX);
	offset += 4;
	buf = WBUFP(buffer,offset);
#else
	WBUFL(buf,4) = umin(p->base_exp,INT32_MAX);
#endif
	WBUFL(buf,8) = p->zeny;
#if PACKETVER >= 20170830
	WBUFQ(buf,12) = u64min((uint64)p->job_exp,INT64_MAX);
	offset += 4;
	buf = WBUFP(buffer,offset);
#else
	WBUFL(buf,12) = umin(p->job_exp,INT32_MAX);
#endif
	WBUFL(buf,16) = p->job_level;
	WBUFL(buf,20) = 0; //Probably opt1
	WBUFL(buf,24) = 0; //Probably opt2
	WBUFL(buf,28) = (p->option&~0x40);
	WBUFL(buf,32) = p->karma;
	WBUFL(buf,36) = p->manner;
	WBUFW(buf,40) = umin(p->status_point,INT16_MAX);
	WBUFL(buf,42) = p->hp;
	WBUFL(buf,46) = p->max_hp;
	offset += 4;
	buf = WBUFP(buffer,offset);
	WBUFW(buf,46) = min(p->sp,INT16_MAX);
	WBUFW(buf,48) = min(p->max_sp,INT16_MAX);
	WBUFW(buf,50) = DEFAULT_WALK_SPEED; //p->speed;
	WBUFW(buf,52) = p->class_;
	WBUFW(buf,54) = p->hair;
#if PACKETVER >= 20141022
	WBUFW(buf,56) = p->body;
	offset += 2;
	buf = WBUFP(buffer,offset);
#endif

	//When the weapon is sent and your option is riding, the client crashes on login!?
	WBUFW(buf,56) = (p->option&(0x20|0x80000|0x100000|0x200000|0x400000|0x800000|0x1000000|0x2000000|0x4000000|0x8000000) ? 0 : p->weapon);

	WBUFW(buf,58) = p->base_level;
	WBUFW(buf,60) = umin(p->skill_point,INT16_MAX);
	WBUFW(buf,62) = p->head_bottom;
	WBUFW(buf,64) = p->shield;
	WBUFW(buf,66) = p->head_top;
	WBUFW(buf,68) = p->head_mid;
	WBUFW(buf,70) = p->hair_color;
	WBUFW(buf,72) = p->clothes_color;
	memcpy(WBUFP(buf,74),p->name,NAME_LENGTH);
	WBUFB(buf,98) = min(p->str,UINT8_MAX);
	WBUFB(buf,99) = min(p->agi,UINT8_MAX);
	WBUFB(buf,100) = min(p->vit,UINT8_MAX);
	WBUFB(buf,101) = min(p->int_,UINT8_MAX);
	WBUFB(buf,102) = min(p->dex,UINT8_MAX);
	WBUFB(buf,103) = min(p->luk,UINT8_MAX);
	WBUFW(buf,104) = p->slot;
	WBUFW(buf,106) = (p->rename > 0 ? 0 : 1);
	offset += 2;
#if (PACKETVER >= 20100720 && PACKETVER <= 20100727) || PACKETVER >= 20100803
	mapindex_getmapname_ext(mapindex_id2name(p->last_point.map),(char *)WBUFP(buf,108));
	offset += MAP_NAME_LENGTH_EXT;
#endif
#if PACKETVER >= 20100803
	WBUFL(buf,124) =
#if PACKETVER_CHAR_DELETEDATE
		(p->delete_date ? TOL(p->delete_date - time(NULL)) : 0);
#else
		TOL(p->delete_date);
#endif
	offset += 4;
#endif
#if PACKETVER >= 20110111
	WBUFL(buf,128) = (p->class_ != JOB_SUMMONER && p->class_ != JOB_BABY_SUMMONER ? p->robe : 0);
	offset += 4;
#endif
#if PACKETVER != 20111116 //2011-11-16 wants 136, ask gravity
	#if PACKETVER >= 20110928
		//Change slot feature (0 = disabled, otherwise enabled)
		if( !char_move_enabled )
			WBUFL(buf,132) = 0;
		else if( char_moves_unlimited )
			WBUFL(buf,132) = 1;
		else
			WBUFL(buf,132) = max(0,(int)p->character_moves);
		offset += 4;
	#endif
	#if PACKETVER >= 20111025
		WBUFL(buf,136) = (p->rename > 0 ? 1 : 0);  //(0 = disabled, otherwise displays "Add-Ons" sidebar)
		offset += 4;
	#endif
	#if PACKETVER >= 20141016
		WBUFB(buf,140) = p->sex; //Sex - (0 = female, 1 = male, 99 = login defined)
		offset += 1;
	#endif
#endif

	return 106 + offset;
}

//----------------------------------------
// Tell client how many pages, kRO sends 17 (Yommy)
//----------------------------------------
void char_charlist_notify(int fd, struct char_session_data *sd) {
#if defined(PACKETVER_RE) && PACKETVER >= 20151001 && PACKETVER < 20180103
	WFIFOHEAD(fd,10);
	WFIFOW(fd,0) = 0x9a0;
	WFIFOL(fd,2) = (sd->char_slots > 3 ? sd->char_slots / 3 : 1);
	WFIFOL(fd,6) = sd->char_slots;
	WFIFOSET(fd,10);
#else
	WFIFOHEAD(fd,6);
	WFIFOW(fd,0) = 0x9a0;
	//Pages to req / send them all in 1 until mmo_chars_fromsql can split them up
	WFIFOL(fd,2) = (sd->char_slots > 3 ? sd->char_slots / 3 : 1); //int TotalCnt (nb page to load)
	WFIFOSET(fd,6);
#endif
}

void char_block_character(int fd, struct char_session_data *sd);
int charblock_timer(int tid, unsigned int tick, int id, intptr_t data) {
	struct char_session_data *sd = NULL;
	int i = 0;

	ARR_FIND(0, fd_max, i, session[i] && (sd = (struct char_session_data *)session[i]->session_data) && sd->account_id == id);
	if( sd == NULL || sd->charblock_timer == INVALID_TIMER ) //Has disconected or was required to stop
		return 0;
	if( sd->charblock_timer != tid ) {
		sd->charblock_timer = INVALID_TIMER;
		return 0;
	}
	char_block_character(i,sd);
	return 0;
}

/**
 * 0x20d <PacketLength>.W <TAG_CHARACTER_BLOCK_INFO>24B (HC_BLOCK_CHARACTER)
 * <GID>L <szExpireDate>20B (TAG_CHARACTER_BLOCK_INFO)
 */
void char_block_character(int fd, struct char_session_data *sd) {
	int i = 0, j = 0, len = 4;
	time_t now = time(NULL);

	WFIFOHEAD(fd,4 + MAX_CHARS * 24);
	WFIFOW(fd,0) = 0x20d;
	for( i = 0; i < MAX_CHARS; i++ ) {
		if( sd->found_char[i] == -1 )
			continue;
		if( sd->unban_time[i] ) {
			if( sd->unban_time[i] > now ) {
				char szExpireDate[21];

				WFIFOL(fd,4 + j * 24) = sd->found_char[i];
				timestamp2string(szExpireDate, 20, sd->unban_time[i], "%Y-%m-%d %H:%M:%S");
				memcpy(WFIFOP(fd,8 + j * 24), szExpireDate, 20);
			} else {
				WFIFOL(fd,4 + j * 24) = 0;
				sd->unban_time[i] = 0;
				if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `unban_time`='0' WHERE `char_id`='%d' LIMIT 1", char_db, sd->found_char[i]) )
					Sql_ShowDebug(sql_handle);
			}
			len += 24;
			j++; //Pkt list idx
		}
	}
	WFIFOW(fd,2) = len; //Packet len
	WFIFOSET(fd,len);
	ARR_FIND(0, MAX_CHARS, i, sd->unban_time[i] > now); //sd->charslot only have productible char
	if( i < MAX_CHARS ) {
		sd->charblock_timer = add_timer(gettick() + 10000, //Each 10s resend that list
			charblock_timer, sd->account_id, 0);
	}
}

void mmo_char_send099d(int fd, struct char_session_data *sd) {
	WFIFOHEAD(fd,4 + MAX_CHARS * MAX_CHAR_BUF);
	WFIFOW(fd,0) = 0x99d;
	WFIFOW(fd,2) = mmo_chars_fromsql(sd,WFIFOP(fd,4)) + 4;
	WFIFOSET(fd,WFIFOW(fd,2));
}

//struct PACKET_CH_CHARLIST_REQ {0x0 short PacketType}
void char_parse_req_charlist(int fd, struct char_session_data *sd) {
	mmo_char_send099d(fd,sd);
}

//----------------------------------------
// Function to send characters to a player
//----------------------------------------
int mmo_char_send006b(int fd, struct char_session_data *sd) {
	int j, offset;

#if PACKETVER >= 20100413
	offset = 3;
#else
	offset = 0;
#endif
	if( save_log )
		ShowInfo("Loading Char Data 6b ("CL_BOLD"%d"CL_RESET")\n",sd->account_id);
	j = 24 + offset; //Offset
	WFIFOHEAD(fd,j + MAX_CHARS * MAX_CHAR_BUF);
	WFIFOW(fd,0) = 0x6b;
#if PACKETVER >= 20100413
		WFIFOB(fd,4) = MAX_CHARS; //Max slots
		WFIFOB(fd,5) = MIN_CHARS; //PremiumStartSlot
		WFIFOB(fd,6) = MIN_CHARS + sd->chars_vip; //PremiumEndSlot
		/* this+0x7  char dummy1_beginbilling */
		/* this+0x8  unsigned long code */
		/* this+0xc  unsigned long time1 */
		/* this+0x10  unsigned long time2 */
		/* this+0x14  char dummy2_endbilling[7] */
#endif
	memset(WFIFOP(fd,4 + offset), 0, 20); //Unknown bytes 4-24 7-27
	j += mmo_chars_fromsql(sd,WFIFOP(fd,j));
	WFIFOW(fd,2) = j; //Packet len
	WFIFOSET(fd,j);

	return 0;
}

//-------------------------------------------------
// Notify client about charselect window data [Ind]
//-------------------------------------------------
void mmo_char_send082d(int fd, struct char_session_data *sd) {
	if( save_log )
		ShowInfo("Loading Char Data 82d ("CL_BOLD"%d"CL_RESET")\n",sd->account_id);
	WFIFOHEAD(fd,29);
	WFIFOW(fd,0) = 0x82d;
	WFIFOW(fd,2) = 29;
	WFIFOB(fd,4) = MIN_CHARS; //NormalSlotNum
	WFIFOB(fd,5) = sd->chars_vip; //PremiumSlotNum
	WFIFOB(fd,6) = sd->chars_billing; //BillingSlotNum
	WFIFOB(fd,7) = sd->char_slots; //ProducibleSlotNum
	WFIFOB(fd,8) = MAX_CHARS; //ValidSlotNum
	memset(WFIFOP(fd,9),0,20); //Unused bytes
	WFIFOSET(fd,29);
}

void mmo_char_send(int fd, struct char_session_data *sd) {
#if PACKETVER >= 20130000
	mmo_char_send082d(fd,sd);
	mmo_char_send006b(fd,sd);
	char_charlist_notify(fd,sd);
#else //FIXME: dump from kRO doesn't show 6b transmission
	mmo_char_send006b(fd,sd);
#endif
#if PACKETVER >= 20060819
	char_block_character(fd,sd);
#endif
}

int char_married(int pl1, int pl2)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `partner_id` FROM `%s` WHERE `char_id` = '%d'", char_db, pl1) )
		Sql_ShowDebug(sql_handle);
	else if( SQL_SUCCESS == Sql_NextRow(sql_handle) ) {
		char *data;

		Sql_GetData(sql_handle, 0, &data, NULL);
		if( pl2 == atoi(data) ) {
			Sql_FreeResult(sql_handle);
			return 1;
		}
	}
	Sql_FreeResult(sql_handle);
	return 0;
}

int char_child(int parent_id, int child_id)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `child` FROM `%s` WHERE `char_id` = '%d'", char_db, parent_id) )
		Sql_ShowDebug(sql_handle);
	else if( SQL_SUCCESS == Sql_NextRow(sql_handle) ) {
		char *data;

		Sql_GetData(sql_handle, 0, &data, NULL);
		if( child_id == atoi(data) ) {
			Sql_FreeResult(sql_handle);
			return 1;
		}
	}
	Sql_FreeResult(sql_handle);
	return 0;
}

int char_family(int cid1, int cid2, int cid3)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`partner_id`,`child` FROM `%s` WHERE `char_id` IN ('%d','%d','%d')", char_db, cid1, cid2, cid3) )
		Sql_ShowDebug(sql_handle);
	else while( SQL_SUCCESS == Sql_NextRow(sql_handle) ) {
		int charid;
		int partnerid;
		int childid;
		char *data;

		Sql_GetData(sql_handle, 0, &data, NULL); charid = atoi(data);
		Sql_GetData(sql_handle, 1, &data, NULL); partnerid = atoi(data);
		Sql_GetData(sql_handle, 2, &data, NULL); childid = atoi(data);

		if( (cid1 == charid    && ((cid2 == partnerid && cid3 == childid  ) || (cid2 == childid   && cid3 == partnerid))) ||
			(cid1 == partnerid && ((cid2 == charid    && cid3 == childid  ) || (cid2 == childid   && cid3 == charid   ))) ||
			(cid1 == childid   && ((cid2 == charid    && cid3 == partnerid) || (cid2 == partnerid && cid3 == charid   ))) )
		{
			Sql_FreeResult(sql_handle);
			return childid;
		}
	}
	Sql_FreeResult(sql_handle);
	return 0;
}

//----------------------------------------------------------------------
// Force disconnection of an online player (with account value) by [Yor]
//----------------------------------------------------------------------
void disconnect_player(int account_id)
{
	int i;
	struct char_session_data *sd;

	//Disconnect player if online on char-server
	ARR_FIND(0, fd_max, i, session[i] && (sd = (struct char_session_data *)session[i]->session_data) && sd->account_id == account_id);
	if (i < fd_max)
		set_eof(i);
}

/**
 * Set 'flag' value of char_session_data
 * @param account_id
 * @param value
 * @param set True: set the value by using '|= val', False: unset the value by using '&= ~val'
 */
void set_session_flag_(int account_id, int val, bool set)
{
	int i;
	struct char_session_data *sd;

	ARR_FIND(0, fd_max, i, session[i] && (sd = (struct char_session_data *)session[i]->session_data) && sd->account_id == account_id);
	if (i < fd_max) {
		if (set)
			sd->flag |= val;
		else
			sd->flag &= ~val;
	}
}

static void char_auth_ok(int fd, struct char_session_data *sd)
{
	struct online_char_data *character;

	if ((character = (struct online_char_data *)idb_get(online_char_db, sd->account_id))) {
		//Check if character is not online already. [Skotlex]
		if (character->server > -1) { //Character already online. KICK KICK KICK
			mapif_disconnectplayer(server[character->server].fd, character->account_id, character->char_id, 2);
			if (character->waiting_disconnect == INVALID_TIMER)
				character->waiting_disconnect = add_timer(gettick() + 20000, chardb_waiting_disconnect, character->account_id, 0);
			if (character)
				character->pincode_success = false;
			WFIFOHEAD(fd,3);
			WFIFOW(fd,0) = 0x81;
			WFIFOB(fd,2) = 8;
			WFIFOSET(fd,3);
			return;
		}
		if (character->fd >= 0 && character->fd != fd) { //There's already a connection from this account that hasn't picked a char yet.
			WFIFOHEAD(fd,3);
			WFIFOW(fd,0) = 0x81;
			WFIFOB(fd,2) = 8;
			WFIFOSET(fd,3);
			return;
		}
		character->fd = fd;
	}

	if (loginif_isconnected()) { //Request account data
		WFIFOHEAD(login_fd,6);
		WFIFOW(login_fd,0) = 0x2716;
		WFIFOL(login_fd,2) = sd->account_id;
		WFIFOSET(login_fd,6);
	}

	//Mark session as 'authed'
	sd->auth = true;

	//Set char online on charserver
	set_char_charselect(sd->account_id);

	//Continues when account data is received...
}

int send_accounts_tologin(int tid, unsigned int tick, int id, intptr_t data);
void mapif_server_reset(int id);

/**
 * HZ 0x2b2b
 * Transmist vip data to mapserv
 */
void mapif_vipack(int mapfd, uint32 aid, uint32 vip_time, uint8 flag, uint32 group_id) {
#ifdef VIP_ENABLE
	uint8 buf[15];

	WBUFW(buf,0) = 0x2b2b;
	WBUFL(buf,2) = aid;
	WBUFL(buf,6) = vip_time;
	WBUFB(buf,10) = flag;
	WBUFL(buf,11) = group_id;
	mapif_send(mapfd, buf, 15); //Inform the mapserv back
#endif
}

/**
 * HZ 0x2742
 * Request vip data to loginserv
 * @param aid : account_id to request the vip data
 * @param flag : 0x1 Select info and update old_groupid, 0x2 VIP duration is changed by atcommand or script, 0x8 First request on player login
 * @param timediff : tick to add to vip timestamp
 * @param mapfd: link to mapserv for ack
 * @return 0 if success
 */
int loginif_reqvipdata(uint32 aid, uint8 flag, int32 timediff, int mapfd) {
	loginif_check(-1);
#ifdef VIP_ENABLE
	WFIFOHEAD(login_fd,15);
	WFIFOW(login_fd,0) = 0x2742;
	WFIFOL(login_fd,2) = aid; //AID
	WFIFOB(login_fd,6) = flag; //Flag
	WFIFOL(login_fd,7) = timediff; //req_inc_duration
	WFIFOL(login_fd,11) = mapfd; //req_inc_duration
	WFIFOSET(login_fd,15);
#endif
	return 1;
}

/**
 * AH 0x2743
 * We received the info from login-serv, transmit it to map
 */
void loginif_parse_vipack(int fd) {
#ifdef VIP_ENABLE
	if (RFIFOREST(fd) < 19)
		return;
	else {
		uint32 aid = RFIFOL(fd,2); //AID
		uint32 vip_time = RFIFOL(fd,6); //vip_time
		uint8 flag = RFIFOB(fd,10); //Flag
		uint32 group_id = RFIFOL(fd,11); //New group id
		int mapfd = RFIFOL(fd,15); //Link to mapserv for ack

		RFIFOSKIP(fd,19);
		mapif_vipack(mapfd, aid, vip_time, flag, group_id);
	}
#endif
}

/**
 * Performs the necessary operations when changing a character's sex, such as
 * correcting the job class and unequipping items, and propagating the
 * information to the guild data.
 *
 * @param sex      The new sex (SEX_MALE or SEX_FEMALE).
 * @param acc      The character's account ID.
 * @param char_id  The character ID.
 * @param class_   The character's current job class.
 * @param guild_id The character's guild ID.
 */
void loginif_parse_changesex(int sex, int acc, int char_id, int class_, int guild_id)
{
	//Job modification
	if( class_ == JOB_BARD || class_ == JOB_DANCER )
		class_ = (sex == SEX_MALE ? JOB_BARD : JOB_DANCER);
	else if( class_ == JOB_CLOWN || class_ == JOB_GYPSY )
		class_ = (sex == SEX_MALE ? JOB_CLOWN : JOB_GYPSY);
	else if( class_ == JOB_BABY_BARD || class_ == JOB_BABY_DANCER )
		class_ = (sex == SEX_MALE ? JOB_BABY_BARD : JOB_BABY_DANCER);
	else if( class_ == JOB_MINSTREL || class_ == JOB_WANDERER )
		class_ = (sex == SEX_MALE ? JOB_MINSTREL : JOB_WANDERER);
	else if( class_ == JOB_MINSTREL_T || class_ == JOB_WANDERER_T )
		class_ = (sex == SEX_MALE ? JOB_MINSTREL_T : JOB_WANDERER_T);
	else if( class_ == JOB_BABY_MINSTREL || class_ == JOB_BABY_WANDERER )
		class_ = (sex == SEX_MALE ? JOB_BABY_MINSTREL : JOB_BABY_WANDERER);
	else if( class_ == JOB_KAGEROU || class_ == JOB_OBORO )
		class_ = (sex == SEX_MALE ? JOB_KAGEROU : JOB_OBORO);
	else if( class_ == JOB_BABY_KAGEROU || class_ == JOB_BABY_OBORO )
		class_ = (sex == SEX_MALE ? JOB_BABY_KAGEROU : JOB_BABY_OBORO);

	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `equip` = '0' WHERE `char_id` = '%d'", inventory_db, char_id) )
		Sql_ShowDebug(sql_handle);

	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `class` = '%d', `weapon` = '0', `shield` = '0', `head_top` = '0', `head_mid` = '0', `head_bottom` = '0', `robe` = '0' WHERE `char_id` = '%d'", char_db, class_, char_id) )
		Sql_ShowDebug(sql_handle);

	if( guild_id ) //If there is a guild, update the guild_member data [Skotlex]
		inter_guild_sex_changed(guild_id, acc, char_id, sex);
}

/**
 * Changes a character's sex.
 * The information is updated on database, and the character is kicked if it
 * currently is online.
 *
 * @param char_id The character's ID.
 * @param sex     The new sex.
 */
void loginif_parse_ackchangecharsex(int char_id, int sex)
{
	unsigned char buf[7];
	int class_ = 0, guild_id = 0, account_id = 0;
	char *data;

	//Get character data
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `account_id`,`class`,`guild_id` FROM `%s` WHERE `char_id` = '%d'", char_db, char_id) ) {
		Sql_ShowDebug(sql_handle);
		return;
	}

	if( Sql_NumRows(sql_handle) != 1 || SQL_ERROR == Sql_NextRow(sql_handle) ) {
		Sql_FreeResult(sql_handle);
		return;
	}

	Sql_GetData(sql_handle, 0, &data, NULL); account_id = atoi(data);
	Sql_GetData(sql_handle, 1, &data, NULL); class_ = atoi(data);
	Sql_GetData(sql_handle, 2, &data, NULL); guild_id = atoi(data);
	Sql_FreeResult(sql_handle);

	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `sex` = '%c' WHERE `char_id` = '%d'", char_db, (sex == SEX_MALE ? 'M' : 'F'), char_id) ) {
		Sql_ShowDebug(sql_handle);
		return;
	}

	loginif_parse_changesex(sex, account_id, char_id, class_, guild_id);

	//Disconnect player if online on char-server
	disconnect_player(account_id);

	//Notify all mapservers about this change
	WBUFW(buf,0) = 0x2b0d;
	WBUFL(buf,2) = account_id;
	WBUFB(buf,6) = sex;
	mapif_sendall(buf, 7);
}

int loginif_isconnected() {
	return (login_fd > 0 && session[login_fd] && !session[login_fd]->flag.eof);
}

/// Resets all the data.
void loginif_reset(void)
{
	int id;

	// TODO kick everyone out and reset everything or wait for connect and try to reaquire locks [FlavioJS]
	for( id = 0; id < ARRAYLENGTH(server); ++id )
		mapif_server_reset(id);
	flush_fifos();
	exit(EXIT_FAILURE);
}


/// Checks the conditions for the server to stop.
/// Releases the cookie when all characters are saved.
/// If all the conditions are met, it stops the core loop.
void loginif_check_shutdown(void)
{
	if( runflag != CHARSERVER_ST_SHUTDOWN )
		return;
	runflag = CORE_ST_STOP;
}


/// Called when the connection to Login Server is disconnected.
void loginif_on_disconnect(void)
{
	ShowWarning("Connection to Login Server lost.\n\n");
}


/// Called when all the connection steps are completed.
void loginif_on_ready(void)
{
	int i;

	loginif_check_shutdown();

	// Send online accounts to login server.
	send_accounts_tologin(INVALID_TIMER, gettick(), 0, 0);

	// If no map-server already connected, display a message...
	ARR_FIND(0, ARRAYLENGTH(server), i, server[i].fd > 0 && server[i].map);
	if( i == ARRAYLENGTH(server) )
		ShowStatus("Awaiting maps from map-server.\n");
}


void loginif_parse_reqpincode(int fd, struct char_session_data *sd) {
	if( pincode_enabled ) { // PIN code system enabled
		ShowInfo("Asking to start pincode to AID: %d\n", sd->account_id);
		if( sd->pincode[0] == '\0' ) { // No PIN code has been set yet
			if( pincode_force )
				pincode_sendstate(fd, sd, PINCODE_NEW);
			else
				pincode_sendstate(fd, sd, PINCODE_PASSED);
		} else {
			if( !pincode_changetime || (sd->pincode_change + pincode_changetime) > time(NULL) ) {
				struct online_char_data *node = (struct online_char_data *)idb_get(online_char_db, sd->account_id);

				if( node != NULL && node->pincode_success ) // User has already passed the check                    
					pincode_sendstate(fd, sd, PINCODE_PASSED);
				else // Ask user for his PIN code
					pincode_sendstate(fd, sd, PINCODE_ASK);
			} else // User hasnt changed his PIN code too long
				pincode_sendstate(fd, sd, PINCODE_EXPIRED);
		}
	} else // PIN code system disabled
		pincode_sendstate(fd, sd, PINCODE_OK);
}


int parse_fromlogin(int fd) {
	struct char_session_data *sd = NULL;
	int i;

	// Only process data from the login-server
	if( fd != login_fd ) {
		ShowDebug("parse_fromlogin: Disconnecting invalid session #%d (is not the login-server)\n", fd);
		do_close(fd);
		return 0;
	}

	if( session[fd]->flag.eof ) {
		do_close(fd);
		login_fd = -1;
		loginif_on_disconnect();
		return 0;
	} else if( session[fd]->flag.ping ) { /* We've reached stall time */
		if( DIFF_TICK(last_tick, session[fd]->rdata_tick) > (stall_time * 2) ) { /* We can't wait any longer */
			set_eof(fd);
			return 0;
		} else if( session[fd]->flag.ping != 2 ) { /* We haven't sent ping out yet */
			WFIFOHEAD(fd,2); // Sends a ping packet to login server (will receive pong 0x2718)
			WFIFOW(fd,0) = 0x2719;
			WFIFOSET(fd,2);

			session[fd]->flag.ping = 2;
		}
	}

	sd = (struct char_session_data *)session[fd]->session_data;

	while( RFIFOREST(fd) >= 2 ) {
		uint16 command = RFIFOW(fd,0);

		switch( command ) {
			// Acknowledgement of connect-to-loginserver request
			case 0x2711:
				if( RFIFOREST(fd) < 3 )
					return 0;

				if( RFIFOB(fd,2) ) {
					//printf("connect login server error : %d\n", RFIFOB(fd,2));
					ShowError("Can not connect to login-server.\n");
					ShowError("The server communication passwords (default s1/p1) are probably invalid.\n");
					ShowError("Also, please make sure your login db has the correct communication username/passwords and the gender of the account is S.\n");
					ShowError("The communication passwords are set in map_athena.conf and char_athena.conf\n");
					set_eof(fd);
					return 0;
				} else {
					ShowStatus("Connected to login-server (connection #%d).\n", fd);
					loginif_on_ready();
				}
				RFIFOSKIP(fd,3);
				break;

			// Acknowledgement of account authentication request
			case 0x2713:
				if( RFIFOREST(fd) < 25 )
					return 0;
				{
					uint32 account_id = RFIFOL(fd,2);
					uint32 login_id1 = RFIFOL(fd,6);
					uint32 login_id2 = RFIFOL(fd,10);
					uint8 sex = RFIFOB(fd,14);
					uint8 result = RFIFOB(fd,15);
					int request_id = RFIFOL(fd,16);
					uint8 clienttype = RFIFOB(fd,20);
					int group_id = RFIFOL(fd,21);

					RFIFOSKIP(fd,25);
					if( session_isActive(request_id) && (sd = (struct char_session_data *)session[request_id]->session_data) &&
						!sd->auth && sd->account_id == account_id && sd->login_id1 == login_id1 && sd->login_id2 == login_id2 && sd->sex == sex )
					{
						int client_fd = request_id;

						sd->clienttype = clienttype;
						switch( result ) {
							case 0: // Ok
								// Restrictions apply
								if( char_server_type == CST_MAINTENANCE && group_id < char_maintenance_min_group_id ) {
									char_reject(client_fd, 0);
									break;
								}
								char_auth_ok(client_fd, sd);
								break;
							case 1: // Auth failed
								char_reject(client_fd, 0);
								break;
						}
					}
				}
				break;

			case 0x2717: // Account data
				if( RFIFOREST(fd) < 75 )
					return 0;

				// Find the authenticated session with this account id
				ARR_FIND(0, fd_max, i, session[i] && (sd = (struct char_session_data *)session[i]->session_data) && sd->auth && sd->account_id == RFIFOL(fd,2));
				if( i < fd_max ) {
					memcpy(sd->email, RFIFOP(fd,6), 40);
					sd->expiration_time = (time_t)RFIFOL(fd,46);
					sd->group_id = RFIFOB(fd,50);
					sd->char_slots = RFIFOB(fd,51);
					if( sd->char_slots > MAX_CHARS ) {
						ShowError("Account '%d' `character_slots` column is higher than supported MAX_CHARS (%d), update MAX_CHARS in mmo.h! capping to MAX_CHARS...\n", sd->account_id, sd->char_slots);
						sd->char_slots = MAX_CHARS; // Cap to maximum
					} else if ( sd->char_slots <= 0 ) // No value aka 0 in sql
						sd->char_slots = MIN_CHARS; // Cap to minimum
					safestrncpy(sd->birthdate, (const char *)RFIFOP(fd,52), sizeof(sd->birthdate));
					safestrncpy(sd->pincode, (const char *)RFIFOP(fd,63), sizeof(sd->pincode));
					sd->pincode_change = (time_t)RFIFOL(fd,68);
					sd->isvip = RFIFOB(fd,72);
					sd->chars_vip = RFIFOB(fd,73);
					sd->chars_billing = RFIFOB(fd,74);
					// Continued from char_auth_ok
					if( (!max_connect_user && sd->group_id != gm_allow_group) ||
						(max_connect_user > 0 && count_users() >= max_connect_user && sd->group_id != gm_allow_group) ) {
						// Refuse connection (over populated)
						char_reject(i,0);
					} else { // Send characters to player
						mmo_char_send(i,sd);
#if PACKETVER_SUPPORTS_PINCODE
						loginif_parse_reqpincode(i,sd);
#endif
					}
				}
				RFIFOSKIP(fd,75);
				break;

			// Login-server alive packet
			case 0x2718:
				if( RFIFOREST(fd) < 2 )
					return 0;
				RFIFOSKIP(fd,2);
				session[fd]->flag.ping = 0;
				break;

			// Change sex reply
			case 0x2723:
				if( RFIFOREST(fd) < 7 )
					return 0;
				{
					unsigned char buf[7];
					int char_id = 0, class_ = 0, guild_id = 0;
					int acc = RFIFOL(fd,2);
					int sex = RFIFOB(fd,6);
					struct auth_node *node = (struct auth_node *)idb_get(auth_db, acc);
					SqlStmt *stmt;

					RFIFOSKIP(fd,7);

					// This should never happen
					if( acc <= 0 ) {
						ShowError("Received invalid account id from login server! (aid: %d)\n", acc);
						return 0;
					}

					if( node != NULL )
						node->sex = sex;

					// Get characters
					stmt = SqlStmt_Malloc(sql_handle);
					if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `char_id`,`class`,`guild_id` FROM `%s` WHERE `account_id` = '%d'", char_db, acc)
						|| SQL_ERROR == SqlStmt_Execute(stmt) ) {
						SqlStmt_ShowDebug(stmt);
						SqlStmt_Free(stmt);
					}

					SqlStmt_BindColumn(stmt, 0, SQLDT_INT,   &char_id,  0, NULL, NULL);
					SqlStmt_BindColumn(stmt, 1, SQLDT_SHORT, &class_,   0, NULL, NULL);
					SqlStmt_BindColumn(stmt, 2, SQLDT_INT,   &guild_id, 0, NULL, NULL);

					for( i = 0; i < MAX_CHARS && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
						loginif_parse_changesex(sex, acc, char_id, class_, guild_id);

					SqlStmt_Free(stmt);

					// Disconnect player if online on char-server
					disconnect_player(acc);

					// Notify all mapservers about this change
					WBUFW(buf,0) = 0x2b0d;
					WBUFL(buf,2) = acc;
					WBUFB(buf,6) = sex;
					mapif_sendall(buf, 7);
				}
				break;

			// Reply to an account_reg2 registry request
			case 0x2729:
				if( RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2) )
					return 0;
				{ // Receive account_reg2 registry, forward to map servers.
					unsigned char buf[13 + ACCOUNT_REG2_NUM * sizeof(struct global_reg)];

					memcpy(buf, RFIFOP(fd,0), RFIFOW(fd,2));
					WBUFW(buf,0) = 0x3804; // Map server can now receive all kinds of reg values with the same packet. [Skotlex]
					mapif_sendall(buf, WBUFW(buf,2));
					RFIFOSKIP(fd,RFIFOW(fd,2));
				}
				break;

			// State change of account/ban notification (from login-server)
			case 0x2731:
				if( RFIFOREST(fd) < 11 )
					return 0;
				{ // Send to all map-servers to disconnect the player
					unsigned char buf[11];

					WBUFW(buf,0) = 0x2b14;
					WBUFL(buf,2) = RFIFOL(fd,2);
					WBUFB(buf,6) = RFIFOB(fd,6); // 0: Change of status, 1: Ban
					WBUFL(buf,7) = RFIFOL(fd,7); // Status or final date of a banishment
					mapif_sendall(buf, 11);
				}
				// Disconnect player if online on char-server
				disconnect_player(RFIFOL(fd,2));
				RFIFOSKIP(fd,11);
				break;

			// Login server request to kick a character out. [Skotlex]
			case 0x2734:
				if( RFIFOREST(fd) < 6 )
					return 0;
				{
					int aid = RFIFOL(fd,2);
					struct online_char_data *character = (struct online_char_data *)idb_get(online_char_db, aid);

					RFIFOSKIP(fd,6);
					if( character != NULL ) { // Account is already marked as online!
						if( character->server > -1 ) { // Kick it from the map server it is on.
							mapif_disconnectplayer(server[character->server].fd, character->account_id, character->char_id, 2);
							if (character->waiting_disconnect == INVALID_TIMER)
								character->waiting_disconnect = add_timer(gettick()+AUTH_TIMEOUT, chardb_waiting_disconnect, character->account_id, 0);
						} else { // Manual kick from char server.
							struct char_session_data *tsd;
							int j;

							ARR_FIND( 0, fd_max, j, session[j] && (tsd = (struct char_session_data *)session[j]->session_data) && tsd->account_id == aid );
							if( j < fd_max ) {
								WFIFOHEAD(j,3);
								WFIFOW(j,0) = 0x81;
								WFIFOB(j,2) = 2; // "Someone has already logged in with this id"
								WFIFOSET(j,3);
								set_eof(j);
							} else // Still moving to the map-server
								set_char_offline(-1, aid);
						}
					}
					idb_remove(auth_db, aid); // Reject auth attempts from map-server
				}
				break;

			// IP address update signal from login server
			case 0x2735: {
					unsigned char buf[2];
					uint32 new_ip = 0;

					WBUFW(buf,0) = 0x2b1e;
					mapif_sendall(buf, 2);

					new_ip = host2ip(login_ip_str);
					if( new_ip && new_ip != login_ip )
						login_ip = new_ip; // Update login ip, too.

					new_ip = host2ip(char_ip_str);
					if( new_ip && new_ip != char_ip ) { // Update ip.
						char_ip = new_ip;
						ShowInfo("Updating IP for [%s].\n", char_ip_str);
						// Notify login server about the change
						WFIFOHEAD(fd,6);
						WFIFOW(fd,0) = 0x2736;
						WFIFOL(fd,2) = htonl(char_ip);
						WFIFOSET(fd,6);
					}

					RFIFOSKIP(fd,2);
				}
				break;

			case 0x2737: // Failed accinfo lookup to forward to mapserver
				if( RFIFOREST(fd) < 18 )
					return 0;

				mapif_parse_accinfo2(false, RFIFOL(fd,2), RFIFOL(fd,6), RFIFOL(fd,10), RFIFOL(fd,14),
					NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, 0, 0);
				RFIFOSKIP(fd,18);
				break;

			case 0x2738: // Successful accinfo lookup to forward to mapserver
				if( RFIFOREST(fd) < 183 )
					return 0;

				mapif_parse_accinfo2(true, RFIFOL(fd,167), RFIFOL(fd,171), RFIFOL(fd,175), RFIFOL(fd,179),
					(char *)RFIFOP(fd,2), (char *)RFIFOP(fd,26), (char *)RFIFOP(fd,59),
					(char *)RFIFOP(fd,99), (char *)RFIFOP(fd,119), (char *)RFIFOP(fd,151),
					(char *)RFIFOP(fd,156), RFIFOL(fd,115), RFIFOL(fd,143), RFIFOL(fd,147));
				RFIFOSKIP(fd,183);
				break;

			case 0x2743: loginif_parse_vipack(fd); break;

			default:
				ShowError("Unknown packet 0x%04x received from login-server, disconnecting.\n", command);
				set_eof(fd);
				return 0;
		}
	}

	RFIFOFLUSH(fd);
	return 0;
}

int check_connect_login_server(int tid, unsigned int tick, int id, intptr_t data);
int send_accounts_tologin(int tid, unsigned int tick, int id, intptr_t data);

void do_init_loginif(void)
{
	// establish char-login connection if not present
	add_timer_func_list(check_connect_login_server, "check_connect_login_server");
	add_timer_interval(gettick() + 1000, check_connect_login_server, 0, 0, 10 * 1000);

	// send a list of all online account IDs to login server
	add_timer_func_list(send_accounts_tologin, "send_accounts_tologin");
	add_timer_interval(gettick() + 1000, send_accounts_tologin, 0, 0, 3600 * 1000); //Sync online accounts every hour
}

void do_final_loginif(void)
{
	if( login_fd != -1 ) {
		do_close(login_fd);
		login_fd = -1;
	}
}

int request_accreg2(int account_id, int char_id)
{
	loginif_check(0);

	WFIFOHEAD(login_fd,10);
	WFIFOW(login_fd,0) = 0x272e;
	WFIFOL(login_fd,2) = account_id;
	WFIFOL(login_fd,6) = char_id;
	WFIFOSET(login_fd,10);
	return 1;
}

//Send packet forward to login-server for account saving
int save_accreg2(unsigned char *buf, int len)
{
	loginif_check(0);

	WFIFOHEAD(login_fd,len+4);
	memcpy(WFIFOP(login_fd,4), buf, len);
	WFIFOW(login_fd,0) = 0x2728;
	WFIFOW(login_fd,2) = len + 4;
	WFIFOSET(login_fd,len + 4);
	return 1;
}

void char_read_fame_list(void)
{
	int i;
	char *data;
	size_t len;

	// Empty ranking lists
	memset(smith_fame_list, 0, sizeof(smith_fame_list));
	memset(chemist_fame_list, 0, sizeof(chemist_fame_list));
	memset(taekwon_fame_list, 0, sizeof(taekwon_fame_list));
	// Build Blacksmith ranking list
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`fame`,`name` FROM `%s` WHERE `fame`>0 AND (`class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d') ORDER BY `fame` DESC LIMIT 0,%d", char_db, JOB_BLACKSMITH, JOB_WHITESMITH, JOB_BABY_BLACKSMITH, JOB_MECHANIC, JOB_MECHANIC_T, JOB_BABY_MECHANIC, fame_list_size_smith) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_smith && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		// char_id
		Sql_GetData(sql_handle, 0, &data, NULL);
		smith_fame_list[i].id = atoi(data);
		// fame
		Sql_GetData(sql_handle, 1, &data, &len);
		smith_fame_list[i].fame = atoi(data);
		// name
		Sql_GetData(sql_handle, 2, &data, &len);
		memcpy(smith_fame_list[i].name, data, zmin(len, NAME_LENGTH));
	}
	// Build Alchemist ranking list
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`fame`,`name` FROM `%s` WHERE `fame`>0 AND (`class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d') ORDER BY `fame` DESC LIMIT 0,%d", char_db, JOB_ALCHEMIST, JOB_CREATOR, JOB_BABY_ALCHEMIST, JOB_GENETIC, JOB_GENETIC_T, JOB_BABY_GENETIC, fame_list_size_chemist) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_chemist && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		// char_id
		Sql_GetData(sql_handle, 0, &data, NULL);
		chemist_fame_list[i].id = atoi(data);
		// fame
		Sql_GetData(sql_handle, 1, &data, &len);
		chemist_fame_list[i].fame = atoi(data);
		// name
		Sql_GetData(sql_handle, 2, &data, &len);
		memcpy(chemist_fame_list[i].name, data, zmin(len, NAME_LENGTH));
	}
	// Build Taekwon ranking list
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`fame`,`name` FROM `%s` WHERE `fame`>0 AND (`class`='%d' OR `class`='%d') ORDER BY `fame` DESC LIMIT 0,%d", char_db, JOB_TAEKWON, JOB_BABY_TAEKWON, fame_list_size_taekwon) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_taekwon && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		// char_id
		Sql_GetData(sql_handle, 0, &data, NULL);
		taekwon_fame_list[i].id = atoi(data);
		// fame
		Sql_GetData(sql_handle, 1, &data, &len);
		taekwon_fame_list[i].fame = atoi(data);
		// name
		Sql_GetData(sql_handle, 2, &data, &len);
		memcpy(taekwon_fame_list[i].name, data, zmin(len, NAME_LENGTH));
	}
	Sql_FreeResult(sql_handle);
}

// Send map-servers the fame ranking lists
int char_send_fame_list(int fd)
{
	int i, len = 8;
	unsigned char buf[32000];

	WBUFW(buf,0) = 0x2b1b;

	for (i = 0; i < fame_list_size_smith && smith_fame_list[i].id; i++) {
		memcpy(WBUFP(buf, len), &smith_fame_list[i], sizeof(struct fame_list));
		len += sizeof(struct fame_list);
	}
	//Add blacksmith's block length
	WBUFW(buf, 6) = len;

	for (i = 0; i < fame_list_size_chemist && chemist_fame_list[i].id; i++) {
		memcpy(WBUFP(buf, len), &chemist_fame_list[i], sizeof(struct fame_list));
		len += sizeof(struct fame_list);
	}
	//Add alchemist's block length
	WBUFW(buf, 4) = len;

	for (i = 0; i < fame_list_size_taekwon && taekwon_fame_list[i].id; i++) {
		memcpy(WBUFP(buf, len), &taekwon_fame_list[i], sizeof(struct fame_list));
		len += sizeof(struct fame_list);
	}
	//Add total packet length
	WBUFW(buf, 2) = len;

	if (fd != -1)
		mapif_send(fd, buf, len);
	else
		mapif_sendall(buf, len);

	return 0;
}

void char_update_fame_list(int type, int index, int fame)
{
	unsigned char buf[8];

	WBUFW(buf,0) = 0x2b22;
	WBUFB(buf,2) = type;
	WBUFB(buf,3) = index;
	WBUFL(buf,4) = fame;
	mapif_sendall(buf, 8);
}

/**
 * Send some misc info to new map-server.
 * - Server name for whisper name
 * - Default map
 * HZ 0x2afb <size>.W <status>.B <name>.24B <mapname>.11B <map_x>.W <map_y>.W
 * @param fd
 */
static void char_send_misc(int fd) {
	uint16 offs = 5;
	unsigned char buf[45];

	memset(buf, '\0', sizeof(buf));
	WBUFW(buf,0) = 0x2afb;
	//0: Succes, 1: Failure
	WBUFB(buf,4) = 0;
	//Send name for wisp to player
	memcpy(WBUFP(buf,5), wisp_server_name, NAME_LENGTH);
	//Default map
	memcpy(WBUFP(buf,(offs += NAME_LENGTH)), default_map, MAP_NAME_LENGTH); //29
	WBUFW(buf,(offs += MAP_NAME_LENGTH)) = default_map_x; //41
	WBUFW(buf,(offs += 2)) = default_map_y; //43
	offs += 2;
	//Length
	WBUFW(buf,2) = offs;
	mapif_send(fd, buf, offs);
}

/**
 * Sends maps to all map-server
 * HZ 0x2b04 <size>.W <ip>.L <port>.W { <map>.?B }.?B
 * @param fd
 * @param map_id
 * @param count Number of map from new map-server has
 */
static void char_send_maps(int fd, int map_id, int count, unsigned char *mapbuf) {
	uint16 x;

	if (count == 0) {
		ShowWarning("Map-server %d has NO maps.\n", map_id);
	} else {
		unsigned char buf[16384];

		//Transmitting maps information to the other map-servers
		WBUFW(buf,0) = 0x2b04;
		WBUFW(buf,2) = count * 4 + 10;
		WBUFL(buf,4) = htonl(server[map_id].ip);
		WBUFW(buf,8) = htons(server[map_id].port);
		memcpy(WBUFP(buf,10), mapbuf, count * 4);
		mapif_sendallwos(fd, buf, WBUFW(buf,2));
	}

	//Transmitting the maps of the other map-servers to the new map-server
	for (x = 0; x < ARRAYLENGTH(server); x++) {
		if (server[x].fd > 0 && x != map_id) {
			uint16 i, j;

			WFIFOHEAD(fd,10 + 4 * ARRAYLENGTH(server[x].map));
			WFIFOW(fd,0) = 0x2b04;
			WFIFOL(fd,4) = htonl(server[x].ip);
			WFIFOW(fd,8) = htons(server[x].port);
			j = 0;
			for (i = 0; i < ARRAYLENGTH(server[x].map); i++)
				if (server[x].map[i])
					WFIFOW(fd,10 + (j++) * 4) = server[x].map[i];
			if (j > 0) {
				WFIFOW(fd,2) = j * 4 + 10;
				WFIFOSET(fd,WFIFOW(fd,2));
			}
		}
	}
}

//Loads a character's name and stores it in the buffer given (must be NAME_LENGTH in size)
//Returns 1 on found, 0 on not found (buffer is filled with Unknown char name)
int char_loadName(int char_id, char *name)
{
	char *data;
	size_t len;

	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `name` FROM `%s` WHERE `char_id`='%d'", char_db, char_id) )
		Sql_ShowDebug(sql_handle);
	else if( SQL_SUCCESS == Sql_NextRow(sql_handle) ) {
		Sql_GetData(sql_handle, 0, &data, &len);
		safestrncpy(name, data, NAME_LENGTH);
		return 1;
	}
#if PACKETVER < 20180221
	else
		safestrncpy(name, unknown_char_name, NAME_LENGTH);
#else
	name[0] = '\0';
#endif
	return 0;
}

int search_mapserver(unsigned short map, uint32 ip, uint16 port);

/// Initializes a server structure.
void mapif_server_init(int id)
{
	memset(&server[id], 0, sizeof(server[id]));
	server[id].fd = -1;
}


/// Destroys a server structure.
void mapif_server_destroy(int id)
{
	if( server[id].fd == -1 ) {
		do_close(server[id].fd);
		server[id].fd = -1;
	}
}


/// Resets all the data related to a server.
void mapif_server_reset(int id)
{
	int i,j;
	unsigned char buf[16384];
	int fd = server[id].fd;
	//Notify other map servers that this one is gone. [Skotlex]
	WBUFW(buf,0) = 0x2b20;
	WBUFL(buf,4) = htonl(server[id].ip);
	WBUFW(buf,8) = htons(server[id].port);
	j = 0;
	for( i = 0; i < MAX_MAP_PER_SERVER; i++ )
		if( server[id].map[i] )
			WBUFW(buf,10 + (j++) * 4) = server[id].map[i];
	if( j > 0 ) {
		WBUFW(buf,2) = j * 4 + 10;
		mapif_sendallwos(fd, buf, WBUFW(buf,2));
	}
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `index`='%d'", ragsrvinfo_db, server[id].fd) )
		Sql_ShowDebug(sql_handle);
	online_char_db->foreach(online_char_db, char_db_setoffline, id); //Tag relevant chars as 'in disconnected' server.
	mapif_server_destroy(id);
	mapif_server_init(id);
}


/// Called when the connection to a Map Server is disconnected.
void mapif_on_disconnect(int id)
{
	ShowStatus("Map-server #%d has disconnected.\n", id);
	mapif_server_reset(id);
}


int mapif_parse_reqcharban(int fd) {
	if( RFIFOREST(fd) < 10 + NAME_LENGTH )
		return 0;
	else {
		//int aid = RFIFOL(fd,2); AID of player who as requested the ban
		int32 timediff = RFIFOL(fd,6);
		const char *name = (char *)RFIFOP(fd,10); //Name of the target character
		char esc_name[NAME_LENGTH * 2 + 1];

		RFIFOSKIP(fd,10 + NAME_LENGTH);
		Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `account_id`,`char_id`,`unban_time` FROM `%s` WHERE `name` = '%s'", char_db, esc_name) )
			Sql_ShowDebug(sql_handle);
		else if( Sql_NumRows(sql_handle) == 0 ) {
			return -1; //1-player not found
		} else if( SQL_SUCCESS != Sql_NextRow(sql_handle) ) {
			Sql_ShowDebug(sql_handle);
			Sql_FreeResult(sql_handle);
			return -1;
		} else {
			int t_cid = 0, t_aid = 0;
			char *data;
			time_t unban_time;
			time_t now = time(NULL);
			SqlStmt *stmt = SqlStmt_Malloc(sql_handle);

			Sql_GetData(sql_handle, 0, &data, NULL); t_aid = atoi(data);
			Sql_GetData(sql_handle, 1, &data, NULL); t_cid = atoi(data);
			Sql_GetData(sql_handle, 2, &data, NULL); unban_time = atol(data);
			Sql_FreeResult(sql_handle);

			if( timediff < 0 && unban_time == 0 ) return 0; //Attemp to reduce time of a non banned account ?!?
			else if( unban_time < now ) unban_time = now; //New entry
			unban_time += timediff; //Alterate the time
			if( unban_time < now ) unban_time = 0; //We have totally reduce the time

			if( SQL_SUCCESS != SqlStmt_Prepare(stmt,
				"UPDATE `%s` SET `unban_time` = ? WHERE `char_id` = ? LIMIT 1",
				char_db)
				|| SQL_SUCCESS != SqlStmt_BindParam(stmt,  0, SQLDT_LONG,   (void *)&unban_time,   sizeof(unban_time))
				|| SQL_SUCCESS != SqlStmt_BindParam(stmt,  1, SQLDT_INT,    (void *)&t_cid,        sizeof(t_cid))
				|| SQL_SUCCESS != SqlStmt_Execute(stmt)
				) {
				SqlStmt_ShowDebug(stmt);
				SqlStmt_Free(stmt);
				return -1;
			}
			SqlStmt_Free(stmt);

			//Condition applies; send to all map-servers to disconnect the player
			if( unban_time > now ) {
				unsigned char buf[11];

				WBUFW(buf,0) = 0x2b14;
				WBUFL(buf,2) = t_cid;
				WBUFB(buf,6) = 2;
				WBUFL(buf,7) = (unsigned int)unban_time;
				mapif_sendall(buf, 11);
				//Disconnect player if online on char-server
				disconnect_player(t_aid);
			}
		}
	}
	return 0;
}

int mapif_parse_reqcharunban(int fd) {
	if( RFIFOREST(fd) < 6 + NAME_LENGTH )
		return 0;
	else {
		const char *name = (char *)RFIFOP(fd,6);
		char esc_name[NAME_LENGTH * 2 + 1];

		RFIFOSKIP(fd,6 + NAME_LENGTH);
		Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));
		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `unban_time` = '0' WHERE `name` = '%s' LIMIT 1", char_db, esc_name) ) {
			Sql_ShowDebug(sql_handle);
			return -1;
		}
	}
	return 0;
}


/** 
 * Request from map-server to change an account's status (will just be forwarded to login server)
 * ZH 2b0e <aid>L <charname>24B <opetype>W <timediff>L
 * @param fd: link to mapserv
 */
int mapif_parse_req_alter_acc(int fd) {
	if( RFIFOREST(fd) < 40 )
		return 0;
	else {
		int result = 0; //0-login-server request done, 1-player not found, 2-gm level too low, 3-login-server offline
		char esc_name[NAME_LENGTH * 2 + 1];
		char answer = true;
		int aid = RFIFOL(fd,2); //account_id of who ask (-1 if server itself made this request)
		const char *name = (char *)RFIFOP(fd,6); //Name of the target character
		int operation = RFIFOW(fd,30); //Type of operation @see enum chrif_req_op
		int32 timediff = 0;
		int val1 = 0, sex = SEX_MALE;

		if( operation == CHRIF_OP_LOGIN_BAN || operation == CHRIF_OP_LOGIN_VIP ) {
			timediff = RFIFOL(fd,32);
			val1 = RFIFOL(fd,36);
		} else if( operation == CHRIF_OP_CHANGECHARSEX )
			sex = RFIFOB(fd,32);

		RFIFOSKIP(fd,40);
		Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `account_id`, `char_id` FROM `%s` WHERE `name` = '%s'", char_db, esc_name) )
			Sql_ShowDebug(sql_handle);
		else if( Sql_NumRows(sql_handle) == 0 ) {
			result = 1; //1-player not found
		} else if( SQL_SUCCESS != Sql_NextRow(sql_handle) ) {
			Sql_ShowDebug(sql_handle);
			result = 1;
		} else {
			int t_aid; //Target account id
			int t_cid; //Target char id
			char *data;

			Sql_GetData(sql_handle, 0, &data, NULL); t_aid = atoi(data);
			Sql_GetData(sql_handle, 1, &data, NULL); t_cid = atoi(data);
			Sql_FreeResult(sql_handle);

			if( !loginif_isconnected() ) //6-7 operation doesn't send to login
				result = 3; //3-login-server offline
			//FIXME: need to move this check to login server [ultramage]
				//if( acc != -1 && isGM(acc) < isGM(t_aid) )
					//result = 2; // 2-gm level too low
			else {
				switch( operation ) { //NOTE: See src/map/chrif.h::enum chrif_req_op for the number
					case CHRIF_OP_LOGIN_BLOCK:
						WFIFOHEAD(login_fd,10);
						WFIFOW(login_fd,0) = 0x2724;
						WFIFOL(login_fd,2) = t_aid;
						WFIFOL(login_fd,6) = 5; //New account status
						WFIFOSET(login_fd,10);
						break;
					case CHRIF_OP_LOGIN_BAN:
						WFIFOHEAD(login_fd,10);
						WFIFOW(login_fd, 0) = 0x2725;
						WFIFOL(login_fd, 2) = t_aid;
						WFIFOL(login_fd, 6) = timediff;
						WFIFOSET(login_fd,10);
						break;
					case CHRIF_OP_LOGIN_UNBLOCK:
						WFIFOHEAD(login_fd,10);
						WFIFOW(login_fd,0) = 0x2724;
						WFIFOL(login_fd,2) = t_aid;
						WFIFOL(login_fd,6) = 0; //New account status
						WFIFOSET(login_fd,10);
						break;
					case CHRIF_OP_LOGIN_UNBAN:
						WFIFOHEAD(login_fd,6);
						WFIFOW(login_fd,0) = 0x272a;
						WFIFOL(login_fd,2) = t_aid;
						WFIFOSET(login_fd,6);
						break;
					case CHRIF_OP_LOGIN_CHANGESEX:
						answer = false;
						WFIFOHEAD(login_fd,6);
						WFIFOW(login_fd,0) = 0x2727;
						WFIFOL(login_fd,2) = t_aid;
						WFIFOSET(login_fd,6);
						break;
					case CHRIF_OP_LOGIN_VIP:
						answer = (val1&4); //vip_req val1 = type, &1 login send return, &2 upd timestamp &4 map send answer
						loginif_reqvipdata(t_aid, val1, timediff, fd);
						break;
					case CHRIF_OP_CHANGECHARSEX:
						answer = false;
						loginif_parse_ackchangecharsex(t_cid, sex);
						break;
				} //End switch operation
			} //Login is connected
		}

		//Send answer if a player ask, not if the server ask
		if( aid != -1 && answer ) { //Don't send answer for changesex/changecharsex
			WFIFOHEAD(fd,34);
			WFIFOW(fd,0) = 0x2b0f;
			WFIFOL(fd,2) = aid;
			safestrncpy((char *)WFIFOP(fd,6), name, NAME_LENGTH);
			WFIFOW(fd,30) = operation;
			WFIFOW(fd,32) = result;
			WFIFOSET(fd,34);
		}
	}
	return 1;
}


void mapif_on_parse_accinfo(int account_id, int u_fd, int u_aid, int u_group, int map_fd) {
	WFIFOHEAD(login_fd,22);
	WFIFOW(login_fd,0) = 0x2744;
	WFIFOL(login_fd,2) = account_id;
	WFIFOL(login_fd,6) = u_fd;
	WFIFOL(login_fd,10) = u_aid;
	WFIFOL(login_fd,14) = u_group;
	WFIFOL(login_fd,18) = map_fd;
	WFIFOSET(login_fd,22);
}


int parse_frommap(int fd)
{
	int i, j;
	int id;
	unsigned char *mapbuf;

	ARR_FIND(0, ARRAYLENGTH(server), id, server[id].fd == fd);
	if( id == ARRAYLENGTH(server) ) { //Not a map server
		ShowDebug("parse_frommap: Disconnecting invalid session #%d (is not a map-server)\n", fd);
		do_close(fd);
		return 0;
	}
	if( session[fd]->flag.eof ) {
		do_close(fd);
		server[id].fd = -1;
		mapif_on_disconnect(id);
		return 0;
	}

	while( RFIFOREST(fd) >= 2 ) {
		switch( RFIFOW(fd,0) ) {
			case 0x2736: //Ip address update
				if( RFIFOREST(fd) < 6 )
					return 0;
				server[id].ip = ntohl(RFIFOL(fd,2));
				ShowInfo("Updated IP address of map-server #%d to %d.%d.%d.%d.\n", id, CONVIP(server[id].ip));
				RFIFOSKIP(fd,6);
				break;

			case 0x2afa: //Receiving map names list from the map-server
				if( RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2) )
					return 0;

				memset(server[id].map, 0, sizeof(server[id].map));
				j = 0;
				for( i = 4; i < RFIFOW(fd,2); i += 4 ) {
					server[id].map[j] = RFIFOW(fd,i);
					j++;
				}

				mapbuf = RFIFOP(fd,4);
				RFIFOSKIP(fd,RFIFOW(fd,2));

				ShowStatus("Map-Server %d connected: %d maps, from IP %d.%d.%d.%d port %d.\n",
					id, j, CONVIP(server[id].ip), server[id].port);
				ShowStatus("Map-server %d loading complete.\n", id);

				char_send_misc(fd);
				char_send_fame_list(fd); //Send fame list
				char_send_maps(fd, id, j, mapbuf);
				break;

			case 0x2afc: //Packet command is now used for sc_data request [Skotlex]
				if( RFIFOREST(fd) < 10 )
					return 0;
				{
#ifdef ENABLE_SC_SAVING
					int aid = RFIFOL(fd,2), cid = RFIFOL(fd,6);

					if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `type`, `tick`, `val1`, `val2`, `val3`, `val4` "
						"FROM `%s` WHERE `account_id`='%d' AND `char_id`='%d'",
						scdata_db, aid, cid) )
					{
						Sql_ShowDebug(sql_handle);
						break;
					}
					if( Sql_NumRows(sql_handle) > 0 ) {
						struct status_change_data scdata;
						int count;
						char *data;

						WFIFOHEAD(fd,14 + 50 * sizeof(struct status_change_data));
						WFIFOW(fd,0) = 0x2b1d;
						WFIFOL(fd,4) = aid;
						WFIFOL(fd,8) = cid;
						for( count = 0; count < 50 && SQL_SUCCESS == Sql_NextRow(sql_handle); ++count ) {
							Sql_GetData(sql_handle, 0, &data, NULL); scdata.type = atoi(data);
							Sql_GetData(sql_handle, 1, &data, NULL); scdata.tick = atoi(data);
							Sql_GetData(sql_handle, 2, &data, NULL); scdata.val1 = atoi(data);
							Sql_GetData(sql_handle, 3, &data, NULL); scdata.val2 = atoi(data);
							Sql_GetData(sql_handle, 4, &data, NULL); scdata.val3 = atoi(data);
							Sql_GetData(sql_handle, 5, &data, NULL); scdata.val4 = atoi(data);
							memcpy(WFIFOP(fd,14 + count * sizeof(struct status_change_data)), &scdata, sizeof(struct status_change_data));
						}
						if( count >= 50 )
							ShowWarning("Too many status changes for %d:%d, some of them were not loaded.\n", aid, cid);
						if( count > 0 ) {
							WFIFOW(fd,2) = 14 + count * sizeof(struct status_change_data);
							WFIFOW(fd,12) = count;
							WFIFOSET(fd,WFIFOW(fd,2));
						}
					} else { //No sc (needs a response)
						WFIFOHEAD(fd,14);
						WFIFOW(fd,0) = 0x2b1d;
						WFIFOW(fd,2) = 14;
						WFIFOL(fd,4) = aid;
						WFIFOL(fd,8) = cid;
						WFIFOW(fd,12) = 0;
						WFIFOSET(fd,WFIFOW(fd,2));
					}
					Sql_FreeResult(sql_handle);
#endif
					RFIFOSKIP(fd, 10);
				}
				break;

			case 0x2afe: //Set MAP user count
				if( RFIFOREST(fd) < 4 )
					return 0;
				if( RFIFOW(fd,2) != server[id].users ) {
					server[id].users = RFIFOW(fd,2);
					ShowInfo("User Count: %d (Server: %d)\n", server[id].users, id);
				}
				RFIFOSKIP(fd, 4);
				break;

			case 0x2aff: //Set MAP users
				if( RFIFOREST(fd) < 6 || RFIFOREST(fd) < RFIFOW(fd,2) )
					return 0;
				{
					//@TODO: When data mismatches memory, update guild/party online/offline states.
					server[id].users = RFIFOW(fd,4);
					online_char_db->foreach(online_char_db,char_db_setoffline,id); //Set all chars from this server as 'unknown'
					for( i = 0; i < server[id].users; i++ ) {
						int aid = RFIFOL(fd,6 + i * 8);
						int cid = RFIFOL(fd,6 + i * 8 + 4);
						struct online_char_data *character = idb_ensure(online_char_db, aid, create_online_char_data);

						if( character->server > -1 && character->server != id ) {
							ShowNotice("Set map user: Character (%d:%d) marked on map server %d, but map server %d claims to have (%d:%d) online!\n",
								character->account_id, character->char_id, character->server, id, aid, cid);
							mapif_disconnectplayer(server[character->server].fd, character->account_id, character->char_id, 2);
						}
						character->server = id;
						character->char_id = cid;
					}
					//If any chars remain in -2, they will be cleaned in the cleanup timer.
					RFIFOSKIP(fd,RFIFOW(fd,2));
				}
				break;

			case 0x2b01: //Receive character data from map-server for saving
				if( RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2) )
					return 0;
				{
					int aid = RFIFOL(fd,4), cid = RFIFOL(fd,8), size = RFIFOW(fd,2);
					struct online_char_data *character;

					if( size - 13 != sizeof(struct mmo_charstatus) ) {
						ShowError("parse_from_map (save-char): Size mismatch! %d != %d\n", size - 13, sizeof(struct mmo_charstatus));
						RFIFOSKIP(fd,size);
						break;
					}
					//Check account only if this ain't final save. Final-save goes through because of the char-map reconnect
					if( RFIFOB(fd,12) || RFIFOB(fd,13) || (
						(character = (struct online_char_data *)idb_get(online_char_db, aid)) != NULL &&
						character->char_id == cid) )
					{
						struct mmo_charstatus char_dat;

						memcpy(&char_dat, RFIFOP(fd,13), sizeof(struct mmo_charstatus));
						mmo_char_tosql(cid, &char_dat);
					} else { //This may be valid on char-server reconnection, when re-sending characters that already logged off.
						ShowError("parse_from_map (save-char): Received data for non-existant/offline character (%d:%d).\n", aid, cid);
						set_char_online(id, cid, aid);
					}

					if( RFIFOB(fd,12) ) { //Flag, set character offline after saving. [Skotlex]
						set_char_offline(cid, aid);
						WFIFOHEAD(fd,10);
						WFIFOW(fd,0) = 0x2b21; //Save ack only needed on final save.
						WFIFOL(fd,2) = aid;
						WFIFOL(fd,6) = cid;
						WFIFOSET(fd,10);
					}
					RFIFOSKIP(fd,size);
				}
				break;

			case 0x2b02: //Req char selection
				if( RFIFOREST(fd) < 22 )
					return 0;
				else {
					uint32 account_id = RFIFOL(fd,2);
					uint32 login_id1 = RFIFOL(fd,6);
					uint32 login_id2 = RFIFOL(fd,10);
					uint32 ip = RFIFOL(fd,14);
					int group_id = RFIFOL(fd,18);

					RFIFOSKIP(fd,22);
					if( runflag != CHARSERVER_ST_RUNNING )
						char_charselres(fd, account_id, 0);
					else {
						struct auth_node *node;

						//Create temporary auth entry
						CREATE(node, struct auth_node, 1);
						node->account_id = account_id;
						node->char_id = 0;
						node->login_id1 = login_id1;
						node->login_id2 = login_id2;
						node->group_id = group_id;
						//node->sex = 0;
						node->ip = ntohl(ip);
						//node->expiration_time = 0; //Unlimited/unknown time by default (not display in map-server)
						//node->gmlevel = 0;
						idb_put(auth_db, account_id, node);

						//Set char to "@ char select" in online db [Kevin]
						set_char_charselect(account_id);
						{
							struct online_char_data *character = (struct online_char_data *)idb_get(online_char_db, account_id);

							if( character != NULL )
								character->pincode_success = true;
						}

						char_charselres(fd, account_id, 1);
					}
				}
				break;

			case 0x2b05: //Request "change map server"
				if( RFIFOREST(fd) < 39 )
					return 0;
				{
					int map_id, map_fd = -1;
					struct mmo_charstatus *char_data;
					struct mmo_charstatus char_dat;

					map_id = search_mapserver(RFIFOW(fd,18), ntohl(RFIFOL(fd,24)), ntohs(RFIFOW(fd,28))); //Locate mapserver by ip and port
					if( map_id >= 0 )
						map_fd = server[map_id].fd;
					//Char should just had been saved before this packet, so this should be safe [Skotlex]
					char_data = (struct mmo_charstatus *)uidb_get(char_db_, RFIFOL(fd,14));
					if( char_data == NULL ) { //Really shouldn't happen
						mmo_char_fromsql(RFIFOL(fd,14), &char_dat, true);
						char_data = (struct mmo_charstatus *)uidb_get(char_db_, RFIFOL(fd,14));
					}

					if( runflag == CHARSERVER_ST_RUNNING &&
						session_isActive(map_fd) &&
						char_data )
					{ //Send the map server the auth of this player
						struct online_char_data *data;
						struct auth_node *node;

						//Update the "last map" as this is where the player must be spawned on the new map server
						char_data->last_point.map = RFIFOW(fd,18);
						char_data->last_point.x = RFIFOW(fd,20);
						char_data->last_point.y = RFIFOW(fd,22);
						char_data->sex = RFIFOB(fd,30);

						//Create temporary auth entry
						CREATE(node, struct auth_node, 1);
						node->account_id = RFIFOL(fd,2);
						node->char_id = RFIFOL(fd,14);
						node->login_id1 = RFIFOL(fd,6);
						node->login_id2 = RFIFOL(fd,10);
						node->sex = RFIFOB(fd,30);
						node->expiration_time = 0; //FIXME: (this thing isn't really supported we could as well purge it instead of fixing)
						node->ip = ntohl(RFIFOL(fd,31));
						node->group_id = RFIFOL(fd,35);
						node->changing_mapservers = 1;
						idb_put(auth_db, RFIFOL(fd,2), node);

						data = idb_ensure(online_char_db, RFIFOL(fd,2), create_online_char_data);
						data->char_id = char_data->char_id;
						data->server = map_id; //Update server where char is

						char_changemapserv_ack(fd, 0); //Reply with an ack
					} else
						char_changemapserv_ack(fd, 1); //Reply with nak

					RFIFOSKIP(fd,39);
				}
				break;

			case 0x2b07: //Remove RFIFOL(fd,6) (friend_id) from RFIFOL(fd,2) (char_id) friend list [Ind]
				if( RFIFOREST(fd) < 10 )
					return 0;
				{
					int char_id = RFIFOL(fd,2);
					int friend_id = RFIFOL(fd,6);

					if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d' AND `friend_id`='%d' LIMIT 1",
						friend_db, char_id, friend_id) ) {
						Sql_ShowDebug(sql_handle);
						break;
					}
					RFIFOSKIP(fd,10);
				}
				break;

			case 0x2b08: //Char name request
				if( RFIFOREST(fd) < 6 )
					return 0;
				WFIFOHEAD(fd,30);
				WFIFOW(fd,0) = 0x2b09;
				WFIFOL(fd,2) = RFIFOL(fd,2);
				char_loadName((int)RFIFOL(fd,2), (char *)WFIFOP(fd,6));
				WFIFOSET(fd,30);
				RFIFOSKIP(fd,6);
				break;

			case 0x2b0a: //Request skillcooldown data
				if( RFIFOREST(fd) < 10 )
					return 0;
				{
					int aid = RFIFOL(fd,2);
					int cid = RFIFOL(fd,6);

					if( SQL_ERROR == Sql_Query(sql_handle, "SELECT skill, tick, duration FROM `%s` WHERE `account_id` = '%d' AND `char_id`='%d'",
						skillcooldown_db, aid, cid) )
					{
						Sql_ShowDebug(sql_handle);
						break;
					}
					if( Sql_NumRows(sql_handle) > 0 ) {
						int count;
						char *data;
						struct skill_cooldown_data scd;

						WFIFOHEAD(fd,14 + MAX_SKILLCOOLDOWN * sizeof(struct skill_cooldown_data));
						WFIFOW(fd,0) = 0x2b0b;
						WFIFOL(fd,4) = aid;
						WFIFOL(fd,8) = cid;
						for( count = 0; count < MAX_SKILLCOOLDOWN && SQL_SUCCESS == Sql_NextRow(sql_handle); ++count ) {
							Sql_GetData(sql_handle, 0, &data, NULL); scd.skill_id = atoi(data);
							Sql_GetData(sql_handle, 1, &data, NULL); scd.tick = atoi(data);
							Sql_GetData(sql_handle, 2, &data, NULL); scd.duration = atoi(data);
							memcpy(WFIFOP(fd, 14 + count * sizeof(struct skill_cooldown_data)), &scd, sizeof(struct skill_cooldown_data));
						}
						if( count >= MAX_SKILLCOOLDOWN )
							ShowWarning("Too many skillcooldowns for %d:%d, some of them were not loaded.\n", aid, cid);
						if( count > 0 ) {
							WFIFOW(fd,2) = 14 + count * sizeof(struct skill_cooldown_data);
							WFIFOW(fd,12) = count;
							WFIFOSET(fd,WFIFOW(fd,2));
							//Clear the data once loaded
							if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `account_id` = '%d' AND `char_id`='%d'", skillcooldown_db, aid, cid) )
								Sql_ShowDebug(sql_handle);
						}
					}
					Sql_FreeResult(sql_handle);
					RFIFOSKIP(fd,10);
				}
				break;

			case 0x2b0c: //Map server send information to change an email of an account -> login-server
				if( RFIFOREST(fd) < 86 )
					return 0;
				if( !loginif_isconnected() ) { //Don't send request if no login-server or eof
					WFIFOHEAD(login_fd,86);
					memcpy(WFIFOP(login_fd,0), RFIFOP(fd,0), 86); //0x2722 <account_id>.L <actual_e-mail>.40B <new_e-mail>.40B
					WFIFOW(login_fd,0) = 0x2722;
					WFIFOSET(login_fd,86);
				}
				RFIFOSKIP(fd,86);
				break;

			case 0x2b0e: mapif_parse_req_alter_acc(fd); break;

			case 0x2b10: //Update and send fame ranking list
				if( RFIFOREST(fd) < 11 )
					return 0;
				{
					int cid = RFIFOL(fd,2);
					int fame = RFIFOL(fd,6);
					char type = RFIFOB(fd,10);
					int size;
					struct fame_list *list;
					int player_pos;
					int fame_pos;

					switch( type ) {
						case RANK_BLACKSMITH: size = fame_list_size_smith; list = smith_fame_list; break;
						case RANK_ALCHEMIST: size = fame_list_size_chemist; list = chemist_fame_list; break;
						case RANK_TAEKWON: size = fame_list_size_taekwon; list = taekwon_fame_list; break;
						default:
							size = 0;
							list = NULL;
							break;
					}

					ARR_FIND(0, size, player_pos, list[player_pos].id == cid); //Position of the player
					ARR_FIND(0, size, fame_pos, list[fame_pos].fame <= fame); //Where the player should be

					if( player_pos == size && fame_pos == size )
						; //Not on list and not enough fame to get on it
					else if( fame_pos == player_pos ) { //Same position
						list[player_pos].fame = fame;
						char_update_fame_list(type, player_pos, fame);
					} else { //Move in the list
						if( player_pos == size ) { //New ranker - not in the list
							ARR_MOVE(size - 1, fame_pos, list, struct fame_list);
							list[fame_pos].id = cid;
							list[fame_pos].fame = fame;
							char_loadName(cid, list[fame_pos].name);
						} else { //Already in the list
							if( fame_pos == size )
								--fame_pos; //Move to the end of the list
							ARR_MOVE(player_pos, fame_pos, list, struct fame_list);
							list[fame_pos].fame = fame;
						}
						char_send_fame_list(-1);
					}

					RFIFOSKIP(fd,11);
				}
				break;

			//Divorce chars
			case 0x2b11:
				if( RFIFOREST(fd) < 10 )
					return 0;
				divorce_char_sql(RFIFOL(fd,2), RFIFOL(fd,6));
				RFIFOSKIP(fd,10);
				break;

			case 0x2b13:
				if( RFIFOREST(fd) < RFIFOW(fd, 2) )
					return 0;
				socket_datasync(fd, false);
				RFIFOSKIP(fd,RFIFOW(fd,2));
				break;

			case 0x2b15: //Request to save skill cooldown data
				if( RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2) )
					return 0;
				{
					int count, aid, cid;
					aid = RFIFOL(fd,4);
					cid = RFIFOL(fd,8);
					count = RFIFOW(fd,12);
					if( count > 0 ) {
						struct skill_cooldown_data data;
						StringBuf buf;

						StringBuf_Init(&buf);
						StringBuf_Printf(&buf, "INSERT INTO `%s` (`account_id`, `char_id`, `skill`, `tick`, `duration`) VALUES ", skillcooldown_db);
						for( i = 0; i < count; ++i ) {
							memcpy(&data,RFIFOP(fd, 14 + i * sizeof(struct skill_cooldown_data)), sizeof(struct skill_cooldown_data));
							if( i > 0 )
								StringBuf_AppendStr(&buf, ", ");
							StringBuf_Printf(&buf, "('%d','%d','%d','%d','%d')", aid, cid, data.skill_id, data.tick, data.duration);
						}
						if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
							Sql_ShowDebug(sql_handle);
						StringBuf_Destroy(&buf);
					}
					RFIFOSKIP(fd,RFIFOW(fd,2));
				}
				break;

			case 0x2b16: //Receive rates [Wizputer]
				if( RFIFOREST(fd) < 14 )
					return 0;
				{
					char esc_server_name[sizeof(server_name) * 2 + 1];

					Sql_EscapeString(sql_handle, esc_server_name, server_name);

					if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` SET `index`='%d',`name`='%s',`exp`='%d',`jexp`='%d',`drop`='%d'",
						ragsrvinfo_db, fd, esc_server_name, RFIFOL(fd,2), RFIFOL(fd,6), RFIFOL(fd,10)) )
						Sql_ShowDebug(sql_handle);
					RFIFOSKIP(fd,14);
				}
				break;

			case 0x2b17: //Character disconnected set online 0 [Wizputer]
				if( RFIFOREST(fd) < 6 )
					return 0;
				set_char_offline(RFIFOL(fd,2), RFIFOL(fd,6));
				RFIFOSKIP(fd,10);
				break;

			case 0x2b18: //Reset all chars to offline [Wizputer]
				set_all_offline(id);
				RFIFOSKIP(fd,2);
				break;

			case 0x2b19: //Character set online [Wizputer]
				if( RFIFOREST(fd) < 10 )
					return 0;
				set_char_online(id, RFIFOL(fd,2),RFIFOL(fd,6));
				RFIFOSKIP(fd,10);
				break;

			case 0x2b1a: //Build and send fame ranking lists [DracoRPG]
				if( RFIFOREST(fd) < 2 )
					return 0;
				char_read_fame_list();
				char_send_fame_list(-1);
				RFIFOSKIP(fd,2);
				break;

			case 0x2b1c: //Request to save status change data [Skotlex]
				if( RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2) )
					return 0;
				{
#ifdef ENABLE_SC_SAVING
					int count, aid, cid;

					aid = RFIFOL(fd,4);
					cid = RFIFOL(fd,8);
					count = RFIFOW(fd,12);

					// Whatever comes from the mapserver, now is the time to drop previous entries
					if( Sql_Query(sql_handle, "DELETE FROM `%s` where `account_id` = %d and `char_id` = %d;", scdata_db, aid, cid) != SQL_SUCCESS )
						Sql_ShowDebug(sql_handle);
					else if( count > 0 ) {
						struct status_change_data data;
						StringBuf buf;

						StringBuf_Init(&buf);
						StringBuf_Printf(&buf, "INSERT INTO `%s` (`account_id`, `char_id`, `type`, `tick`, `val1`, `val2`, `val3`, `val4`) VALUES ", scdata_db);
						for( i = 0; i < count; ++i ) {
							memcpy (&data, RFIFOP(fd,14 + i * sizeof(struct status_change_data)), sizeof(struct status_change_data));
							if( i > 0 )
								StringBuf_AppendStr(&buf, ", ");
							StringBuf_Printf(&buf, "('%d','%d','%hu','%d','%d','%d','%d','%d')", aid, cid,
								data.type, data.tick, data.val1, data.val2, data.val3, data.val4);
						}
						if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
							Sql_ShowDebug(sql_handle);
						StringBuf_Destroy(&buf);
					}
#endif
					RFIFOSKIP(fd,RFIFOW(fd,2));
				}
				break;

			case 0x2b23: //map-server alive packet
				WFIFOHEAD(fd,2);
				WFIFOW(fd,0) = 0x2b24;
				WFIFOSET(fd,2);
				RFIFOSKIP(fd,2);
				break;

			case 0x2b26: //Auth request from map-server
				if( RFIFOREST(fd) < 20 )
					return 0;

				{
					int account_id;
					int char_id;
					int login_id1;
					char sex;
					uint32 ip;
					struct auth_node *node;
					struct mmo_charstatus *cd;
					struct mmo_charstatus char_dat;
					bool autotrade;

					account_id = RFIFOL(fd,2);
					char_id    = RFIFOL(fd,6);
					login_id1  = RFIFOL(fd,10);
					sex        = RFIFOB(fd,14);
					ip         = ntohl(RFIFOL(fd,15));
					autotrade  = RFIFOB(fd,19);
					RFIFOSKIP(fd,20);

					node = (struct auth_node *)idb_get(auth_db, account_id);
					cd = (struct mmo_charstatus *)uidb_get(char_db_, char_id);
					if( cd == NULL ) { //Really shouldn't happen.
						mmo_char_fromsql(char_id, &char_dat, true);
						cd = (struct mmo_charstatus *)uidb_get(char_db_, char_id);
					}
					if( runflag == CHARSERVER_ST_RUNNING && autotrade && cd ) {
						uint32 mmo_charstatus_len = sizeof(struct mmo_charstatus) + 25;

						if( cd->sex == SEX_ACCOUNT )
							cd->sex = sex;
						WFIFOHEAD(fd,mmo_charstatus_len);
						WFIFOW(fd,0) = 0x2afd;
						WFIFOW(fd,2) = mmo_charstatus_len;
						WFIFOL(fd,4) = account_id;
						WFIFOL(fd,8) = 0;
						WFIFOL(fd,12) = 0;
						WFIFOL(fd,16) = 0;
						WFIFOL(fd,20) = 0;
						WFIFOB(fd,24) = 0;
						memcpy(WFIFOP(fd,25), cd, sizeof(struct mmo_charstatus));
						WFIFOSET(fd,WFIFOW(fd,2));

						set_char_online(id, char_id, account_id);
					} else if( runflag == CHARSERVER_ST_RUNNING &&
						cd != NULL &&
						node != NULL &&
						node->account_id == account_id &&
						node->char_id == char_id &&
						node->login_id1 == login_id1
#if PACKETVER < 20141016
						&& node->sex == sex
#endif
						/*&& node->ip == ip*/ )
					{ //Auth ok
						uint32 mmo_charstatus_len = sizeof(struct mmo_charstatus) + 25;

						if( cd->sex == SEX_ACCOUNT )
							cd->sex = sex;
						WFIFOHEAD(fd,mmo_charstatus_len);
						WFIFOW(fd,0) = 0x2afd;
						WFIFOW(fd,2) = mmo_charstatus_len;
						WFIFOL(fd,4) = account_id;
						WFIFOL(fd,8) = node->login_id1;
						WFIFOL(fd,12) = node->login_id2;
						//FIXME: Will wrap to negative after "19-Jan-2038, 03:14:07 AM GMT"
						WFIFOL(fd,16) = (uint32)node->expiration_time;
						WFIFOL(fd,20) = node->group_id;
						WFIFOB(fd,24) = node->changing_mapservers;
						memcpy(WFIFOP(fd,25), cd, sizeof(struct mmo_charstatus));
						WFIFOSET(fd,WFIFOW(fd,2));

						//Only use the auth once and mark user online
						idb_remove(auth_db, account_id);
						set_char_online(id, char_id, account_id);
					} else { //Auth failed
						WFIFOHEAD(fd,19);
						WFIFOW(fd,0) = 0x2b27;
						WFIFOL(fd,2) = account_id;
						WFIFOL(fd,6) = char_id;
						WFIFOL(fd,10) = login_id1;
						WFIFOB(fd,14) = sex;
						WFIFOL(fd,15) = htonl(ip);
						WFIFOSET(fd,19);
					}
				}
				break;

			case 0x2b28: mapif_parse_reqcharban(fd); break; //charban

			case 0x2b2a: mapif_parse_reqcharunban(fd); break; //charunban

			//case 0x2b2c: /* Free */; break;

			case 0x2b2d: bonus_script_get(fd); break; //Load data

			case 0x2b2e: bonus_script_save(fd); break;//Save data

			default: {
				//inter-server packet
				int r = inter_parse_frommap(fd);

				if( r == 1 ) break; //Processed
				if( r == 2 ) return 0; //Need more packet

				//No inter server packet. no char server packet -> disconnect
				ShowError("Unknown packet 0x%04x from map server, disconnecting.\n", RFIFOW(fd,0));
				set_eof(fd);
				return 0;
			}
		} //Switch
	} //While

	return 0;
}

void do_init_mapif(void)
{
	int i;
	for( i = 0; i < ARRAYLENGTH(server); ++i )
		mapif_server_init(i);
}

void do_final_mapif(void)
{
	int i;
	for( i = 0; i < ARRAYLENGTH(server); ++i )
		mapif_server_destroy(i);
}

// Searches for the mapserver that has a given map (and optionally ip/port, if not -1).
// If found, returns the server's index in the 'server' array (otherwise returns -1).
int search_mapserver(unsigned short map, uint32 ip, uint16 port)
{
	int i, j;

	for(i = 0; i < ARRAYLENGTH(server); i++)
	{
		if (server[i].fd > 0
		&& (ip == (uint32)-1 || server[i].ip == ip)
		&& (port == (uint16)-1 || server[i].port == port))
		{
			for (j = 0; server[i].map[j]; j++)
				if (server[i].map[j] == map)
					return i;
		}
	}

	return -1;
}

// Initialization process (currently only initialization inter_mapif)
static int char_mapif_init(int fd)
{
	return inter_mapif_init(fd);
}

//--------------------------------------------
// Test to know if an IP come from LAN or WAN.
//--------------------------------------------
int lan_subnetcheck(uint32 ip)
{
	int i;
	ARR_FIND( 0, subnet_count, i, (subnet[i].char_ip & subnet[i].mask) == (ip & subnet[i].mask) );
	if( i < subnet_count ) {
		ShowInfo("Subnet check [%u.%u.%u.%u]: Matches "CL_CYAN"%u.%u.%u.%u/%u.%u.%u.%u"CL_RESET"\n", CONVIP(ip), CONVIP(subnet[i].char_ip & subnet[i].mask), CONVIP(subnet[i].mask));
		return subnet[i].map_ip;
	} else {
		ShowInfo("Subnet check [%u.%u.%u.%u]: "CL_CYAN"WAN"CL_RESET"\n", CONVIP(ip));
		return 0;
	}
}


/// Answers to deletion request (HC_DELETE_CHAR3_RESERVED)
/// @param result
/// 0 (0x718): An unknown error has occurred.
/// 1: none/success
/// 3 (0x719): A database error occurred.
/// 4 (0x71a): To delete a character you must withdraw from the guild.
/// 5 (0x71b): To delete a character you must withdraw from the party.
/// Any (0x718): An unknown error has occurred.
void char_delete2_ack(int fd, int char_id, uint32 result, time_t delete_date)
{ // HC: <0828>.W <char id>.L <Msg:0-5>.L <deleteDate>.L
	WFIFOHEAD(fd,14);
	WFIFOW(fd,0) = 0x828;
	WFIFOL(fd,2) = char_id;
	WFIFOL(fd,6) = result;
	WFIFOL(fd,10) =
#if PACKETVER_CHAR_DELETEDATE
		TOL(delete_date - time(NULL));
#else
		TOL(delete_date);
#endif
	WFIFOSET(fd,14);
}


/// @param result
/// 0 (0x718): An unknown error has occurred.
/// 1: none/success
/// 2 (0x71c): Due to system settings can not be deleted.
/// 3 (0x719): A database error occurred.
/// 4 (0x71d): Deleting not yet possible time.
/// 5 (0x71e): Date of birth do not match.
/// 6 Name does not match.
/// 7 Character Deletion has failed because you have entered an incorrect e-mail address.
/// Any (0x718): An unknown error has occurred.
void char_delete2_accept_ack(int fd, uint32 char_id, uint32 result)
{ // HC: <082a>.W <char id>.L <Msg>.L
#if PACKETVER >= 20130000
	if( result == 1 )
		mmo_char_send(fd,session[fd]->session_data);
#endif
	WFIFOHEAD(fd,10);
	WFIFOW(fd,0) = 0x82a;
	WFIFOL(fd,2) = char_id;
	WFIFOL(fd,6) = result;
	WFIFOSET(fd,10);
}


/// @param result
/// 1 (0x718): none/success, (if char id not in deletion process): An unknown error has occurred.
/// 2 (0x719): A database error occurred.
/// Any (0x718): An unknown error has occurred.
void char_delete2_cancel_ack(int fd, uint32 char_id, uint32 result)
{ // HC: <082c>.W <char id>.L <Msg:1-2>.L
	WFIFOHEAD(fd,10);
	WFIFOW(fd,0) = 0x82c;
	WFIFOL(fd,2) = char_id;
	WFIFOL(fd,6) = result;
	WFIFOSET(fd,10);
}


static void char_delete2_req(int fd, struct char_session_data *sd)
{ // CH: <0827>.W <char id>.L
	int char_id, party_id, guild_id, i;
	char *data;
	time_t delete_date;

	char_id = RFIFOL(fd,2);

	ARR_FIND(0, MAX_CHARS, i, sd->found_char[i] == char_id);
	if( i == MAX_CHARS ) { // Character not found
		char_delete2_ack(fd, char_id, 3, 0);
		return;
	}

	if( SQL_SUCCESS != Sql_Query(sql_handle, "SELECT `delete_date`, `party_id`, `guild_id` FROM `%s` WHERE `char_id`='%d'", char_db, char_id) ||
		SQL_SUCCESS != Sql_NextRow(sql_handle) ) {
		Sql_ShowDebug(sql_handle);
		char_delete2_ack(fd, char_id, 3, 0);
		return;
	}

	Sql_GetData(sql_handle, 0, &data, NULL); delete_date = strtoul(data, NULL, 10);
	Sql_GetData(sql_handle, 1, &data, NULL); party_id = strtoul(data, NULL, 10);
	Sql_GetData(sql_handle, 2, &data, NULL); guild_id = strtoul(data, NULL, 10);

	if( delete_date ) { // Character already queued for deletion
		char_delete2_ack(fd, char_id, 0, 0);
		return;
	}

	if( (char_del_restriction&CHAR_DEL_RESTRICT_GUILD) && guild_id ) { // Character is in guild
		char_delete2_ack(fd, char_id, 4, 0);
		return;
	}

	if( (char_del_restriction&CHAR_DEL_RESTRICT_PARTY) && party_id ) { // Character is in party
		char_delete2_ack(fd, char_id, 5, 0);
		return;
	}

	// Success
	delete_date = time(NULL) + char_del_delay;

	if( SQL_SUCCESS != Sql_Query(sql_handle, "UPDATE `%s` SET `delete_date`='%lu' WHERE `char_id`='%d'",
		char_db, (unsigned long)delete_date, char_id) ) {
		Sql_ShowDebug(sql_handle);
		char_delete2_ack(fd, char_id, 3, 0);
		return;
	}

	char_delete2_ack(fd, char_id, 1, delete_date);
}


/**
 * Check char deletion code
 * @param sd
 * @param delcode E-mail or birthdate
 * @param flag Delete flag
 * @return true:Success, false:Failure
 */
static bool char_check_delchar(struct char_session_data *sd, char *delcode, uint8 flag) {
	// Email check
	if( (flag&CHAR_DEL_EMAIL) &&
		(!stricmp(delcode, sd->email) || // Email does not match or
		(!stricmp("a@a.com", sd->email) && // It is default email and
		!strcmp("", delcode))) ) // User sent an empty email
	{
		ShowInfo(""CL_RED"Char Deleted"CL_RESET" "CL_GREEN"(Email)"CL_RESET".\n");
		return true;
	}
	// Birthdate (YYMMDD)
	if( (flag&CHAR_DEL_BIRTHDATE) &&
		(!strcmp(sd->birthdate + 2, delcode) || // +2 to cut off the century
		(!strcmp("0000-00-00", sd->birthdate) && // It is default birthdate and
		!strcmp("", delcode))) ) // User sent an empty birthdate
	{
		ShowInfo(""CL_RED"Char Deleted"CL_RESET" "CL_GREEN"(Birthdate)"CL_RESET".\n");
		return true;
	}
	return false;
}


static void char_delete2_accept(int fd, struct char_session_data *sd)
{ // CH: <0829>.W <char id>.L <birth date:YYMMDD>.6B
	char birthdate[8 + 1];
	uint32 char_id = RFIFOL(fd,2);

	ShowInfo(CL_RED"Request Char Deletion: "CL_GREEN"%u (%u)"CL_RESET"\n", sd->account_id, char_id);

	// Construct "YY-MM-DD"
	birthdate[0] = RFIFOB(fd,6);
	birthdate[1] = RFIFOB(fd,7);
	birthdate[2] = '-';
	birthdate[3] = RFIFOB(fd,8);
	birthdate[4] = RFIFOB(fd,9);
	birthdate[5] = '-';
	birthdate[6] = RFIFOB(fd,10);
	birthdate[7] = RFIFOB(fd,11);
	birthdate[8] = 0;

	// Only check for birthdate
	if( !char_check_delchar(sd, birthdate, CHAR_DEL_BIRTHDATE) ) {
		char_delete2_accept_ack(fd, char_id, 5);
		return;
	}

	switch( char_delete(sd, char_id) ) {
			case CHAR_DELETE_OK: // Success
				char_delete2_accept_ack(fd, char_id, 1);
				break;
			case CHAR_DELETE_DATABASE: // Data error
			case CHAR_DELETE_NOTFOUND: // Character not found
				char_delete2_accept_ack(fd, char_id, 3);
				break;
			case CHAR_DELETE_PARTY: // In a party
			case CHAR_DELETE_GUILD: // In a guild
			case CHAR_DELETE_BASELEVEL: // Character level config restriction
				char_delete2_accept_ack(fd, char_id, 2);
				break;
			case CHAR_DELETE_TIME: // Not queued or delay not yet passed
				char_delete2_accept_ack(fd, char_id, 4);
				break;
	}
}


static void char_delete2_cancel(int fd, struct char_session_data *sd)
{ // CH: <082b>.W <char id>.L
	uint32 char_id, i;

	char_id = RFIFOL(fd,2);

	ARR_FIND(0, MAX_CHARS, i, sd->found_char[i] == char_id);
	if( i == MAX_CHARS ) { // Character not found
		char_delete2_cancel_ack(fd, char_id, 2);
		return;
	}

	// There is no need to check, whether or not the character was
	// queued for deletion, as the client prints an error message by
	// itself, if it was not the case (@see char_delete2_cancel_ack)
	if( SQL_SUCCESS != Sql_Query(sql_handle, "UPDATE `%s` SET `delete_date`='0' WHERE `char_id`='%d'", char_db, char_id) ) {
		Sql_ShowDebug(sql_handle);
		char_delete2_cancel_ack(fd, char_id, 2);
		return;
	}

	char_delete2_cancel_ack(fd, char_id, 1);
}


int parse_char(int fd)
{
	int i;
	char email[40];
	int map_fd;
	struct char_session_data *sd;
	uint32 ipl = session[fd]->client_addr;

	sd = (struct char_session_data *)session[fd]->session_data;

	if( login_fd < 0 )
		set_eof(fd); //Disconnect any player if no login-server

	if( session[fd]->flag.eof ) {
		if( sd != NULL && sd->auth ) { //Already authed client
			struct online_char_data *data = (struct online_char_data *)idb_get(online_char_db,sd->account_id);

			if( data != NULL && data->fd == fd)
				data->fd = -1;
			if( data == NULL || data->server == -1) //If it is not in any server, send it offline [Skotlex]
				set_char_offline(-1,sd->account_id);
		}
		do_close(fd);
		return 0;
	}

	while( RFIFOREST(fd) >= 2 ) {
		unsigned short cmd;

		//For use in packets that depend on an sd being present [Skotlex]
		#define FIFOSD_CHECK(rest) { if(RFIFOREST(fd) < rest) return 0; if (sd == NULL || !sd->auth) { RFIFOSKIP(fd,rest); return 0; } }

		cmd = RFIFOW(fd,0);
		switch( cmd ) {

			//Request to connect
			//0065 <account id>.L <login id1>.L <login id2>.L <???>.W <sex>.B
			case 0x65:
				if( RFIFOREST(fd) < 17 )
					return 0;
				{
					struct auth_node *node;
					int account_id = RFIFOL(fd,2);
					uint32 login_id1 = RFIFOL(fd,6);
					uint32 login_id2 = RFIFOL(fd,10);
					int sex = RFIFOB(fd,16);

					RFIFOSKIP(fd,17);
					ShowInfo("request connect - account_id:%d/login_id1:%d/login_id2:%d\n",account_id,login_id1,login_id2);

					if( sd ) {
						//Received again auth packet for already authentified account?? Discard it
						//@TODO: Perhaps log this as a hack attempt?
						//@TODO: and perhaps send back a reply?
						break;
					}

					CREATE(session[fd]->session_data,struct char_session_data,1);
					sd = (struct char_session_data *)session[fd]->session_data;
					sd->account_id = account_id;
					sd->login_id1 = login_id1;
					sd->login_id2 = login_id2;
					sd->sex = sex;
					sd->auth = false; //Not authed yet

					//Send back account_id
					WFIFOHEAD(fd,4);
					WFIFOL(fd,0) = account_id;
					WFIFOSET(fd,4);

					if( runflag != CHARSERVER_ST_RUNNING ) {
						char_reject(fd,0);
						break;
					}

					//Search authentification
					node = (struct auth_node *)idb_get(auth_db,account_id);
					if( node != NULL &&
						node->account_id == account_id &&
						node->login_id1  == login_id1 &&
						node->login_id2  == login_id2 /*&&
						node->ip         == ipl*/ )
					{ //Authentication found (coming from map server)
						//Restrictions apply
						if( char_server_type == CST_MAINTENANCE && node->group_id < char_maintenance_min_group_id ) {
							char_reject(fd,0);
							break;
						}
						idb_remove(auth_db,account_id);
						char_auth_ok(fd,sd);
					} else { //Authentication not found (coming from login server)
						if( loginif_isconnected() ) { //Don't send request if no login-server
							WFIFOHEAD(login_fd,23);
							WFIFOW(login_fd,0) = 0x2712; //Ask login-server to authentify an account
							WFIFOL(login_fd,2) = sd->account_id;
							WFIFOL(login_fd,6) = sd->login_id1;
							WFIFOL(login_fd,10) = sd->login_id2;
							WFIFOB(login_fd,14) = sd->sex;
							WFIFOL(login_fd,15) = htonl(ipl);
							WFIFOL(login_fd,19) = fd;
							WFIFOSET(login_fd,23);
						} else //If no login-server, we must refuse connection
							char_reject(fd,0);
					}
				}
				break;

			//Char select
			case 0x66:
				FIFOSD_CHECK(3);
				{
					struct mmo_charstatus char_dat;
					struct mmo_charstatus *cd;
					char *data;
					int char_id;
					struct auth_node *node;
					int server_id = 0;
					int slot = RFIFOB(fd,2);

					RFIFOSKIP(fd,3);
					ARR_FIND(0,ARRAYLENGTH(server),server_id,server[server_id].fd > 0 && server[server_id].map);
					//Not available, tell it to wait (client wont close; char select will respawn)
					//Magic response found by Ind thanks to Yommy <3
					if( server_id == ARRAYLENGTH(server) ) {
						WFIFOHEAD(fd,24);
						WFIFOW(fd,0) = 0x840;
						WFIFOW(fd,2) = 24;
						safestrncpy((char *)WFIFOP(fd,4),"0",20); //We can't send empty (otherwise the list will pop up)
						WFIFOSET(fd,24);
						break;
					}

					//Check if the character exists and is not scheduled for deletion
					if( SQL_SUCCESS != Sql_Query(sql_handle,"SELECT `char_id` FROM `%s` WHERE `account_id`='%d' AND `char_num`='%d' AND `delete_date`=0",char_db,sd->account_id,slot)
					|| SQL_SUCCESS != Sql_NextRow(sql_handle)
					|| SQL_SUCCESS != Sql_GetData(sql_handle,0,&data,NULL) )
					{ //Not found? May be forged packet
						Sql_ShowDebug(sql_handle);
						Sql_FreeResult(sql_handle);
						char_reject(fd,0);
						break;
					}

					char_id = atoi(data);
					Sql_FreeResult(sql_handle);

					//Prevent select a char while retrieving guild bound items
					if( sd->flag&1 ) {
						char_reject(fd,0);
						break;
					}

					//Client doesn't let it get to this point if you're banned, so its a forged packet
					if( sd->found_char[slot] == char_id && sd->unban_time[slot] > time(NULL) ) {
						char_reject(fd,0);
						break;
					}

					//Set char as online prior to loading its data so 3rd party applications will realise the sql data is not reliable
					set_char_online(-2,char_id,sd->account_id);
					if( !mmo_char_fromsql(char_id,&char_dat,true) ) { //Failed? Set it back offline
						set_char_offline(char_id,sd->account_id);
						//Failed to load something. REJECT!
						char_reject(fd,0);
						break; //Jump off this boat
					}

					//Have to switch over to the DB instance otherwise data won't propagate [Kevin]
					cd = (struct mmo_charstatus *)idb_get(char_db_,char_id);
					if( cd->sex == SEX_ACCOUNT )
						cd->sex = sd->sex;

					if( log_char ) {
						char esc_name[NAME_LENGTH * 2 + 1];

						Sql_EscapeStringLen(sql_handle,esc_name,char_dat.name,strnlen(char_dat.name,NAME_LENGTH));
						if( SQL_ERROR == Sql_Query(sql_handle,
							"INSERT INTO `%s`(`time`, `account_id`, `char_id`, `char_num`, `name`) VALUES (NOW(), '%d', '%d', '%d', '%s')",
							charlog_db,sd->account_id,cd->char_id,slot,esc_name) )
							Sql_ShowDebug(sql_handle);
					}

					ShowInfo("Selected char: (Account %d: %d - %s)\n",sd->account_id,slot,char_dat.name);

					//Searching map server
					i = search_mapserver(cd->last_point.map,-1,-1);

					//If map is not found, we check major cities
					if( i < 0 || !cd->last_point.map ) {
						unsigned short j;

						//First check that there's actually a map server online
						ARR_FIND(0,ARRAYLENGTH(server),j,server[j].fd >= 0 && server[j].map);
						if( j == ARRAYLENGTH(server) ) {
							ShowInfo("Connection Closed. No map servers available.\n");
							WFIFOHEAD(fd,3);
							WFIFOW(fd,0) = 0x81;
							WFIFOB(fd,2) = 1; //01 = Server closed
							WFIFOSET(fd,3);
							break;
						}
						if( (i = search_mapserver((j = mapindex_name2id(MAP_PRONTERA)),-1,-1)) >= 0 ) {
							cd->last_point.x = 273;
							cd->last_point.y = 354;
						} else if( (i = search_mapserver((j = mapindex_name2id(MAP_GEFFEN)),-1,-1)) >= 0 ) {
							cd->last_point.x = 120;
							cd->last_point.y = 100;
						} else if( (i = search_mapserver((j = mapindex_name2id(MAP_MORROC)),-1,-1)) >= 0 ) {
							cd->last_point.x = 160;
							cd->last_point.y = 94;
						} else if( (i = search_mapserver((j = mapindex_name2id(MAP_ALBERTA)),-1,-1)) >= 0 ) {
							cd->last_point.x = 116;
							cd->last_point.y = 57;
						} else if( (i = search_mapserver((j = mapindex_name2id(MAP_PAYON)),-1,-1)) >= 0 ) {
							cd->last_point.x = 87;
							cd->last_point.y = 117;
						} else if( (i = search_mapserver((j = mapindex_name2id(MAP_IZLUDE)),-1,-1)) >= 0 ) {
							cd->last_point.x = 94;
							cd->last_point.y = 103;
						} else {
							ShowInfo("Connection Closed. No map server available that has a major city, and unable to find map-server for '%s'.\n",mapindex_id2name(cd->last_point.map));
							WFIFOHEAD(fd,3);
							WFIFOW(fd,0) = 0x81;
							WFIFOB(fd,2) = 1; //01 = Server closed
							WFIFOSET(fd,3);
							break;
						}
						ShowWarning("Unable to find map-server for '%s', sending to major city '%s'.\n",mapindex_id2name(cd->last_point.map),mapindex_id2name(j));
						cd->last_point.map = j;
					}

					//Send NEW auth packet [Kevin]
					//FIXME: is this case even possible? [ultramage]
					if( (map_fd = server[i].fd) < 1 || !session[map_fd] ) {
						ShowError("parse_char: Attempting to write to invalid session %d! Map Server #%d disconnected.\n",map_fd,i);
						server[i].fd = -1;
						memset(&server[i],0,sizeof(struct mmo_map_server));
						//Send server closed.
						WFIFOHEAD(fd,3);
						WFIFOW(fd,0) = 0x81;
						WFIFOB(fd,2) = 1; //01 = Server closed
						WFIFOSET(fd,3);
						break;
					}

					char_send_map_data(fd,cd,ipl,i); //Send player to map

					//Create temporary auth entry
					CREATE(node,struct auth_node,1);
					node->account_id = sd->account_id;
					node->char_id = cd->char_id;
					node->login_id1 = sd->login_id1;
					node->login_id2 = sd->login_id2;
					node->sex = sd->sex;
					node->expiration_time = sd->expiration_time;
					node->group_id = sd->group_id;
					node->ip = ipl;
					idb_put(auth_db,sd->account_id,node);

				}
				break;

			//Create new char
#if PACKETVER >= 20151104
			//S 0a39 <name>.24B <slot>.B <hair color>.W <hair style>.W <class>.W <unknown>.unknown <sex>.B
			case 0xa39:
				FIFOSD_CHECK(36)
#elif PACKETVER >= 20120307
			//S 0970 <name>.24B <slot>.B <hair color>.W <hair style>.W
			case 0x970:
				FIFOSD_CHECK(31);
#else
			//S 0067 <name>.24B <str>.B <agi>.B <vit>.B <int>.B <dex>.B <luk>.B <slot>.B <hair color>.W <hair style>.W
			case 0x67:
				FIFOSD_CHECK(37);
#endif

				if( !char_new ) //Turn character creation on/off [Kevin]
					i = -2;
				else
#if PACKETVER >= 20151104
					i = make_new_char_sql(sd,(char *)RFIFOP(fd,2),RFIFOB(fd,26),RFIFOW(fd,27),RFIFOW(fd,29),RFIFOW(fd,31),RFIFOW(fd,32),RFIFOB(fd,35));
#elif PACKETVER >= 20120307
					i = make_new_char_sql(sd,(char *)RFIFOP(fd,2),RFIFOB(fd,26),RFIFOW(fd,27),RFIFOW(fd,29));
#else
					i = make_new_char_sql(sd,(char *)RFIFOP(fd,2),RFIFOB(fd,26),RFIFOB(fd,27),RFIFOB(fd,28),RFIFOB(fd,29),RFIFOB(fd,30),RFIFOB(fd,31),RFIFOB(fd,32),RFIFOW(fd,33),RFIFOW(fd,35));
#endif

				if( i < 0 ) {
					WFIFOHEAD(fd,3);
					WFIFOW(fd,0) = 0x6e;
					//Others I found [Ind]
					//0x02 = Symbols in Character Names are forbidden
					//0x03 = You are not elegible to open the Character Slot
					//0x0B = This service is only available for premium users
					switch( i ) {
						case -1: WFIFOB(fd,2) = 0x00; break; //'Charname already exists'
						case -2: WFIFOB(fd,2) = 0xFF; break; //'Char creation denied'
						case -3: WFIFOB(fd,2) = 0x01; break; //'You are underaged'
						case -4: WFIFOB(fd,2) = 0x03; break; //'You are not elegible to open the Character Slot.'
						case -5: WFIFOB(fd,2) = 0x02; break; //'Symbols in Character Names are forbidden'
						default:
							ShowWarning("parse_char: Unknown result received from make_new_char_sql!\n");
							WFIFOB(fd,2) = 0xFF;
							break;
					}
					WFIFOSET(fd,3);
				} else {
					int len;
					struct mmo_charstatus char_dat; //Retrieve data

					mmo_char_fromsql(i,&char_dat,false); //Only the short data is needed

					//Send to player
					WFIFOHEAD(fd,2 + MAX_CHAR_BUF);
					WFIFOW(fd,0) = 0x6d;
					len = 2 + mmo_char_tobuf(WFIFOP(fd,2),&char_dat);
					WFIFOSET(fd,len);

					//Add new entry to the chars list
					sd->found_char[char_dat.slot] = i; //The char_id of the new char
				}
#if PACKETVER >= 20151104
				RFIFOSKIP(fd,36);
#elif PACKETVER >= 20120307
				RFIFOSKIP(fd,31);
#else
				RFIFOSKIP(fd,37);
#endif
				break;

			//Delete char
			case 0x68:
			//2004-04-19aSakexe+ langtype 12 char deletion packet
			case 0x1fb:
				if( cmd == 0x68 ) FIFOSD_CHECK(46);
				if( cmd == 0x1fb ) FIFOSD_CHECK(56);
				{
					uint32 cid = RFIFOL(fd,2);

					ShowInfo(CL_RED"Request Char Deletion: "CL_GREEN"%u (%u)"CL_RESET"\n",sd->account_id,cid);
					memcpy(email,RFIFOP(fd,6),40);
					RFIFOSKIP(fd,(cmd == 0x68 ? 46 : 56));

					if( !char_check_delchar(sd, email, char_del_option) ) {
						char_refuse_delchar(fd,0);
						break;
					}

					//Delete character
					switch( char_delete(sd,cid) ) {
						case CHAR_DELETE_OK:
							break;
						case CHAR_DELETE_DATABASE:
						case CHAR_DELETE_BASELEVEL:
						case CHAR_DELETE_TIME:
							char_refuse_delchar(fd,0);
							return 0;
						case CHAR_DELETE_NOTFOUND:
							char_refuse_delchar(fd,1);
							return 0;
						case CHAR_DELETE_GUILD:
						case CHAR_DELETE_PARTY:
							char_refuse_delchar(fd,2);
							return 0;
					}

					//Char successfully deleted
					WFIFOHEAD(fd,2);
					WFIFOW(fd,0) = 0x6f;
					WFIFOSET(fd,2);
				}
				break;

			//Client keep-alive packet (every 12 seconds)
			//R 0187 <account ID>.l
			case 0x187:
				if( RFIFOREST(fd) < 6 )
					return 0;
				RFIFOSKIP(fd,6);
				break;

			//Char rename request without confirmation
			case 0x8fc: char_parse_ackrename(fd,sd); break;

			//Char rename request
			case 0x28d: char_parse_reqrename(fd,sd); break;

			//Confirm change char rename
			case 0x28f: char_parse_ackrename(fd,sd); break;

			//Captcha code request (not implemented)
			//R 07e5 <?>.w <aid>.l
			case 0x7e5:
				WFIFOHEAD(fd,5);
				WFIFOW(fd,0) = 0x7e9;
				WFIFOW(fd,2) = 5;
				WFIFOB(fd,4) = 1;
				WFIFOSET(fd,5);
				RFIFOSKIP(fd,8);
				break;

			//Captcha code check (not implemented)
			//R 07e7 <len>.w <aid>.l <code>.b10 <?>.b14
			case 0x7e7:
				WFIFOHEAD(fd,5);
				WFIFOW(fd,0) = 0x7e9;
				WFIFOW(fd,2) = 5;
				WFIFOB(fd,4) = 1;
				WFIFOSET(fd,5);
				RFIFOSKIP(fd,32);
				break;

			//Deletion timer request
			case 0x827:
				FIFOSD_CHECK(6);
				char_delete2_req(fd,sd);
				RFIFOSKIP(fd,6);
				break;

			//Deletion accept request
			case 0x829:
				FIFOSD_CHECK(12);
				char_delete2_accept(fd,sd);
				RFIFOSKIP(fd,12);
				break;

			//Deletion cancel request
			case 0x82b:
				FIFOSD_CHECK(6);
				char_delete2_cancel(fd,sd);
				RFIFOSKIP(fd,6);
				break;

#if PACKETVER_SUPPORTS_PINCODE
			//Checks the entered pin
			case 0x8b8:
				if( RFIFOREST(fd) < 10 )
					return 0;
				if( pincode_enabled && RFIFOL(fd,2) == sd->account_id )
					pincode_check(fd,sd);
				RFIFOSKIP(fd,10);
				break;

			//Activate PIN system and set first PIN
			case 0x8ba:
				if( RFIFOREST(fd) < 10 )
					return 0;
				if( pincode_enabled && RFIFOL(fd,2) == sd->account_id )
					pincode_setnew(fd,sd);
				RFIFOSKIP(fd,10);
				break;

			//Pincode change request
			case 0x8be:
				if( RFIFOREST(fd) < 14 )
					return 0;
				if( pincode_enabled && RFIFOL(fd,2) == sd->account_id )
					pincode_change(fd,sd);
				RFIFOSKIP(fd,14);
				break;

			//Request for PIN window
			case 0x8c5:
				if( RFIFOREST(fd) < 6 )
					return 0;
				if( pincode_enabled && RFIFOL(fd,2) == sd->account_id ) {
					if( sd->pincode[0] == '\0' )
						pincode_sendstate(fd,sd,PINCODE_NEW);
					else
						pincode_sendstate(fd,sd,PINCODE_ASK);
				}
				RFIFOSKIP(fd,6);
				break;
#endif

			//Character movement request
			case 0x8d4:
				if( RFIFOREST(fd) < 8 )
					return 0;
				moveCharSlot(fd,sd,RFIFOW(fd,2),RFIFOW(fd,4));
				mmo_char_send(fd,sd);
				RFIFOSKIP(fd,8);
				break;

			case 0x9a1:
				if( RFIFOREST(fd) < 2 )
					return 0;
				char_parse_req_charlist(fd,sd);
				RFIFOSKIP(fd,2);
				break;

			//Login as map-server
			case 0x2af8:
				if( RFIFOREST(fd) < 60 )
					return 0;
				else {
					char *l_user = (char *)RFIFOP(fd,2);
					char *l_pass = (char *)RFIFOP(fd,26);

					l_user[23] = '\0';
					l_pass[23] = '\0';
					ARR_FIND( 0,ARRAYLENGTH(server),i,server[i].fd <= 0 );
					if( runflag != CHARSERVER_ST_RUNNING || i == ARRAYLENGTH(server) ||
						strcmp(l_user,userid) != 0 || strcmp(l_pass,passwd) != 0 )
						char_connectack(fd,3); //Fail
					else {
						char_connectack(fd,0); //Success
						server[i].fd = fd;
						server[i].ip = ntohl(RFIFOL(fd,54));
						server[i].port = ntohs(RFIFOW(fd,58));
						server[i].users = 0;
						memset(server[i].map,0,sizeof(server[i].map));
						session[fd]->func_parse = parse_frommap;
						session[fd]->flag.server = 1;
						realloc_fifo(fd,FIFOSIZE_SERVERLINK,FIFOSIZE_SERVERLINK);
						char_mapif_init(fd);
					}

					socket_datasync(fd,true);

					RFIFOSKIP(fd,60);
				}
				return 0; //Avoid processing of followup packets here

			//Unknown packet received
			default:
				ShowError("parse_char: Received unknown packet "CL_WHITE"0x%x"CL_RESET" from ip '"CL_WHITE"%s"CL_RESET"'! Disconnecting!\n",RFIFOW(fd,0),ip2str(ipl,NULL));
				set_eof(fd);
				return 0;
		}
	}

	RFIFOFLUSH(fd);
	return 0;
}

//Console Command Parser [Wizputer]
int parse_console(const char *buf)
{
	char type[64];
	char command[64];
	int n = 0;

	if( (n = sscanf(buf, "%63[^:]:%63[^\n]", type, command)) < 2 )
		if( (n = sscanf(buf, "%63[^\n]", type)) < 1 )
			return -1; //Nothing to do no arg
	if( n != 2 ) { //End string
		ShowNotice("Type: '%s'\n",type);
		command[0] = '\0';
	} else
		ShowNotice("Type of command: '%s' || Command: '%s'\n",type,command);

	if( n == 2 && strcmpi("server", type) == 0 ) {
		if( strcmpi("shutdown", command) == 0 || strcmpi("exit", command) == 0 || strcmpi("quit", command) == 0 )
			runflag = CHARSERVER_ST_SHUTDOWN;
		else if( strcmpi("alive", command) == 0 || strcmpi("status", command) == 0 )
			ShowInfo(CL_CYAN"Console: "CL_BOLD"I'm Alive."CL_RESET"\n");
	} else if( strcmpi("ers_report", type) == 0 )
		ers_report();
	else if( strcmpi("help", type) == 0 ) {
		ShowInfo("Available commands:\n");
		ShowInfo("\t server:shutdown => Stops the server.\n");
		ShowInfo("\t server:alive => Checks if the server is running.\n");
		ShowInfo("\t ers_report => Displays database usage.\n");
	}

	return 0;
}

int mapif_sendall(unsigned char *buf, unsigned int len)
{
	int i, c;

	c = 0;
	for( i = 0; i < ARRAYLENGTH(server); i++ ) {
		int fd;

		if( (fd = server[i].fd) > 0 ) {
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0),buf,len);
			WFIFOSET(fd,len);
			c++;
		}
	}
	return c;
}

int mapif_sendallwos(int sfd, unsigned char *buf, unsigned int len)
{
	int i, c;

	c = 0;
	for( i = 0; i < ARRAYLENGTH(server); i++ ) {
		int fd;

		if( (fd = server[i].fd) > 0 && fd != sfd ) {
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0),buf,len);
			WFIFOSET(fd,len);
			c++;
		}
	}
	return c;
}

int mapif_send(int fd, unsigned char *buf, unsigned int len)
{
	if( fd >= 0 ) {
		int i;

		ARR_FIND(0,ARRAYLENGTH(server),i,fd == server[i].fd);
		if( i < ARRAYLENGTH(server) ) {
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0),buf,len);
			WFIFOSET(fd,len);
			return 1;
		}
	}
	return 0;
}

int broadcast_user_count(int tid, unsigned int tick, int id, intptr_t data)
{
	uint8 buf[6];
	int users = count_users();
	//Only send an update when needed
	static int prev_users = 0;

	if( prev_users == users )
		return 0;
	prev_users = users;

	if( loginif_isconnected() ) {
		//Send number of user to login server
		WFIFOHEAD(login_fd,6);
		WFIFOW(login_fd,0) = 0x2714;
		WFIFOL(login_fd,2) = users;
		WFIFOSET(login_fd,6);
	}

	//Send number of players to all map-servers
	WBUFW(buf,0) = 0x2b00;
	WBUFL(buf,2) = users;
	mapif_sendall(buf,6);

	return 0;
}

/**
 * Load this character's account id into the 'online accounts' packet
 * @see DBApply
 */
static int send_accounts_tologin_sub(DBKey key, DBData *data, va_list ap)
{
	struct online_char_data *character = db_data2ptr(data);
	int *i = va_arg(ap, int*);

	if( character->server > -1 ) {
		WFIFOL(login_fd,8 + (*i) * 4) = character->account_id;
		(*i)++;
		return 1;
	}
	return 0;
}

/**
 * Timered function to send all account_id connected to login-serv
 * @param tid : Timer id
 * @param tick : Scheduled tick
 * @param id : GID linked to that timered call
 * @param data : data transmited for delayed function
 * @return 
 */
int send_accounts_tologin(int tid, unsigned int tick, int id, intptr_t data)
{
	if( loginif_isconnected() ) {
		//Send account list to login server
		int users = online_char_db->size(online_char_db);
		int i = 0;

		WFIFOHEAD(login_fd,8 + users * 4);
		WFIFOW(login_fd,0) = 0x272d;
		online_char_db->foreach(online_char_db,send_accounts_tologin_sub,&i,users);
		WFIFOW(login_fd,2) = 8 + i * 4;
		WFIFOL(login_fd,4) = i;
		WFIFOSET(login_fd,WFIFOW(login_fd,2));
	}
	return 0;
}

int check_connect_login_server(int tid, unsigned int tick, int id, intptr_t data)
{
	if( login_fd > 0 && session[login_fd] != NULL )
		return 0;

	ShowInfo("Attempt to connect to login-server...\n");
	login_fd = make_connection(login_ip,login_port,false,10);
	if( login_fd == -1 ) { //Try again later [Skotlex]
		login_fd = 0;
		return 0;
	}
	session[login_fd]->func_parse = parse_fromlogin;
	session[login_fd]->flag.server = 1;
	realloc_fifo(login_fd,FIFOSIZE_SERVERLINK,FIFOSIZE_SERVERLINK);

	WFIFOHEAD(login_fd,86);
	WFIFOW(login_fd,0) = 0x2710;
	memcpy(WFIFOP(login_fd,2),userid,24);
	memcpy(WFIFOP(login_fd,26),passwd,24);
	WFIFOL(login_fd,50) = 0;
	WFIFOL(login_fd,54) = htonl(char_ip);
	WFIFOW(login_fd,58) = htons(char_port);
	memcpy(WFIFOP(login_fd,60),server_name,20);
	WFIFOW(login_fd,80) = 0;
	WFIFOW(login_fd,82) = char_server_type;
	WFIFOW(login_fd,84) = char_new_display; //Only display (New) if they want to [Kevin]
	WFIFOSET(login_fd,86);

	return 1;
}

#if PACKETVER_SUPPORTS_PINCODE
//------------------------------------------------
//Pincode system
//------------------------------------------------
void pincode_check(int fd, struct char_session_data *sd) {
	char pin[PINCODE_LENGTH + 1];

	memset(pin,0,PINCODE_LENGTH + 1);

	strncpy((char *)pin,(char *)RFIFOP(fd,6),PINCODE_LENGTH);

	pincode_decrypt(sd->pincode_seed,pin);

	if( pincode_compare(fd,sd,pin) )
		pincode_sendstate(fd,sd,PINCODE_PASSED);
}

int pincode_compare(int fd, struct char_session_data *sd, char *pin) {
	if( strcmp(sd->pincode,pin) == 0 ) {
		sd->pincode_try = 0;
		return 1;
	} else {
		pincode_sendstate(fd,sd,PINCODE_WRONG);

		if( pincode_maxtry && ++sd->pincode_try >= pincode_maxtry )
			pincode_notifyLoginPinError(sd->account_id);

		return 0;
	}
}

/**
 * Helper function to check if a new pincode contains illegal characters or combinations
 */
bool pincode_allowed(char *pincode) {
	int i;
	char c, n, compare[PINCODE_LENGTH + 1];

	memset(compare,0,PINCODE_LENGTH + 1);

	//Sanity check for bots to prevent errors
	for( i = 0; i < PINCODE_LENGTH; i++ ) {
		c = pincode[i];
		if( c < '0' || c > '9' )
			return false;
	}

	//Is it forbidden to use only the same character?
	if( !pincode_allow_repeated ) {
		c = pincode[0];
		//Check if the first character equals the rest of the input
		for( i = 0; i < PINCODE_LENGTH; i++ )
			compare[i] = c;
		if( strncmp(pincode,compare,PINCODE_LENGTH + 1) == 0 )
			return false;
	}

	//Is it forbidden to use a sequential combination of numbers?
	if( !pincode_allow_sequential ) {
		c = pincode[0];
		//Check if it is an ascending sequence
		for( i = 0; i < PINCODE_LENGTH; i++ ) {
			n = c + i;
			if( n > '9' )
				compare[i] = '0' + (n - '9') - 1;
			else
				compare[i] = n;
		}
		if( strncmp(pincode,compare,PINCODE_LENGTH + 1) == 0 )
			return false;
		//Check if it is an descending sequence
		for( i = 0; i < PINCODE_LENGTH; i++ ) {
			n = c - i;
			if( n < '0' )
				compare[i] = '9' - ('0' - n) + 1;
			else
				compare[i] = n;
		}
		if( strncmp(pincode,compare,PINCODE_LENGTH + 1) == 0 )
			return false;
	}
	return true;
}

void pincode_change(int fd, struct char_session_data *sd) {
	char oldpin[PINCODE_LENGTH + 1];
	char newpin[PINCODE_LENGTH + 1];

	memset(oldpin,0,PINCODE_LENGTH + 1);
	memset(newpin,0,PINCODE_LENGTH + 1);

	strncpy(oldpin,(char *)RFIFOP(fd,6),PINCODE_LENGTH);
	pincode_decrypt(sd->pincode_seed,oldpin);

	if( !pincode_compare(fd,sd,oldpin) )
		return;

	strncpy(newpin,(char *)RFIFOP(fd,10),PINCODE_LENGTH);
	pincode_decrypt(sd->pincode_seed,newpin);

	if( pincode_allowed(newpin) ) {
		pincode_notifyLoginPinUpdate(sd->account_id,newpin);
		strncpy(sd->pincode,newpin,sizeof(newpin));
		ShowInfo("Pincode changed for AID: %d\n",sd->account_id);
		pincode_sendstate(fd,sd,PINCODE_PASSED);
	} else
		pincode_sendstate(fd,sd,PINCODE_ILLEGAL);
}

void pincode_setnew(int fd, struct char_session_data *sd) {
	char newpin[PINCODE_LENGTH + 1];

	memset(newpin,0,PINCODE_LENGTH + 1);

	strncpy(newpin,(char *)RFIFOP(fd,6),PINCODE_LENGTH);
	pincode_decrypt(sd->pincode_seed,newpin);

	if( pincode_allowed(newpin) ) {
		pincode_notifyLoginPinUpdate(sd->account_id,newpin);
		strncpy(sd->pincode,newpin,strlen(newpin));
		pincode_sendstate(fd,sd,PINCODE_PASSED);
	} else
		pincode_sendstate(fd,sd,PINCODE_ILLEGAL);
}

// 0 = Disabled / pin is correct
// 1 = Ask for pin - client sends 0x8b8
// 2 = Create new pin - client sends 0x8ba
// 3 = Pin must be changed - client 0x8be
// 4 = Create new pin - client sends 0x8ba
// 5 = Client shows msgstr(1896)
// 6 = Client shows msgstr(1897) Unable to use your KSSN number
// 7 = Char select window shows a button - client sends 0x8c5
// 8 = Pincode was incorrect
void pincode_sendstate(int fd, struct char_session_data *sd, uint16 state) {
	WFIFOHEAD(fd,12);
	WFIFOW(fd,0) = 0x8b9;
	WFIFOL(fd,2) = sd->pincode_seed = rnd()%0xFFFF;
	WFIFOL(fd,6) = sd->account_id;
	WFIFOW(fd,10) = state;
	WFIFOSET(fd,12);
}

void pincode_notifyLoginPinUpdate(int account_id, char *pin) {
	int size = 8 + PINCODE_LENGTH + 1;

	WFIFOHEAD(login_fd,size);
	WFIFOW(login_fd,0) = 0x2738;
	WFIFOW(login_fd,2) = size;
	WFIFOL(login_fd,4) = account_id;
	strncpy((char *)WFIFOP(login_fd,8),pin,PINCODE_LENGTH + 1);
	WFIFOSET(login_fd,size);
}

void pincode_notifyLoginPinError(int account_id) {
	WFIFOHEAD(login_fd,6);
	WFIFOW(login_fd,0) = 0x2739;
	WFIFOL(login_fd,2) = account_id;
	WFIFOSET(login_fd,6);
}

void pincode_decrypt(uint32 userSeed, char *pin) {
	int i;
	char tab[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	char *buf;

	for( i = 1; i < 10; i++ ) {
		int pos;
		uint32 multiplier = 0x3498, baseSeed = 0x881234;

		userSeed = baseSeed + userSeed * multiplier;
		pos = userSeed%(i + 1);
		if( i != pos ) {
			tab[i] ^= tab[pos];
			tab[pos] ^= tab[i];
			tab[i] ^= tab[pos];
		}
	}

	buf = (char *)aMalloc(sizeof(char) * (PINCODE_LENGTH + 1));
	memset(buf,0,PINCODE_LENGTH + 1);
	for( i = 0; i < PINCODE_LENGTH; i++ )
		sprintf(buf + i,"%d",tab[pin[i] - '0']);
	strcpy(pin,buf);
	aFree(buf);
}
#endif

//------------------------------------------------
//Add On system
//------------------------------------------------
void moveCharSlot(int fd, struct char_session_data *sd, unsigned short from, unsigned short to) {
	// Have we changed to often or is it disabled?
	if( !char_move_enabled || ( !char_moves_unlimited && sd->char_moves[from] <= 0 ) ) {
		moveCharSlotReply(fd, sd, from, 1);
		return;
	}

	// We don't even have a character on the chosen slot?
	if( sd->found_char[from] <= 0 || to >= sd->char_slots ) {
		moveCharSlotReply(fd, sd, from, 1);
		return;
	}

	if( sd->found_char[to] > 0 ) {
		// We want to move to a used position
		if( char_movetoused ) { // @TODO: check if the target is in deletion process
			// Admin is friendly and uses triangle exchange
			if(	   SQL_ERROR == Sql_QueryStr(sql_handle, "START TRANSACTION")
				|| SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `char_num`='%d' WHERE `char_id` = '%d'", char_db, to, sd->found_char[from] )
				|| SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `char_num`='%d' WHERE `char_id` = '%d'", char_db, from, sd->found_char[to] )
				|| SQL_ERROR == Sql_QueryStr(sql_handle, "COMMIT")
				){
				moveCharSlotReply(fd, sd, from, 1);
				Sql_ShowDebug(sql_handle);
				Sql_QueryStr(sql_handle,"ROLLBACK");
				return;
			}
		} else {
			// Admin doesn't allow us to
			moveCharSlotReply(fd, sd, from, 1);
			return;
		}
	} else if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `char_num`='%d' WHERE `char_id`='%d'", char_db, to, sd->found_char[from] ) ) {
		Sql_ShowDebug(sql_handle);
		moveCharSlotReply(fd, sd, from, 1);
		return;
	}

	if( !char_moves_unlimited ) {
		sd->char_moves[from]--;
		Sql_Query(sql_handle, "UPDATE `%s` SET `moves`='%d' WHERE `char_id`='%d'", char_db, sd->char_moves[from], sd->found_char[from] );
	}

	// We successfully moved the char - time to notify the client
	moveCharSlotReply(fd, sd, from, 0);
	mmo_char_send(fd, sd);
}

// Reason
// 0: Success
// 1: Failed
void moveCharSlotReply(int fd, struct char_session_data *sd, unsigned short index, short reason) {
	WFIFOHEAD(fd,8);
	WFIFOW(fd,0) = 0x8d5;
	WFIFOW(fd,2) = 8;
	WFIFOW(fd,4) = reason;
	WFIFOW(fd,6) = sd->char_moves[index];
	WFIFOSET(fd,8);
}

// Tells the client if the name was accepted or not
// 028e <result>.W (HC_ACK_IS_VALID_CHARNAME)
// result:
//		0 = name is not OK
//		1 = name is OK
void char_reqrename_response(int fd, struct char_session_data *sd, bool name_valid) {
	WFIFOHEAD(fd,4);
	WFIFOW(fd,0) = 0x28e;
	WFIFOW(fd,2) = name_valid;
	WFIFOSET(fd,4);
}

// Request for checking the new name on character renaming
// 028d <account ID>.l <char ID>.l <new name>.24B (CH_REQ_IS_VALID_CHARNAME)
int char_parse_reqrename(int fd, struct char_session_data *sd) {
	int i, aid, cid;
	char name[NAME_LENGTH], esc_name[NAME_LENGTH * 2 + 1];

	FIFOSD_CHECK(34);
	aid = RFIFOL(fd,2);
	cid = RFIFOL(fd,6);
	safestrncpy(name, (char *)RFIFOP(fd,10), NAME_LENGTH);
	RFIFOSKIP(fd,34);

	if( aid != sd->account_id )
		return 1;

	ARR_FIND(0, MAX_CHARS, i, sd->found_char[i] == cid);
	if( i == MAX_CHARS )
		return 1;

	normalize_name(name, TRIM_CHARS);
	Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));
	if( !check_char_name(name, esc_name) ) {
		i = 1;
		safestrncpy(sd->new_name, name, NAME_LENGTH);
	} else
		i = 0;

	char_reqrename_response(fd, sd, (i > 0));

	return 1;
}

// Sends the response to a rename request to the client.
// 0290 <result>.W (HC_ACK_CHANGE_CHARNAME)
// 08fd <result>.L (HC_ACK_CHANGE_CHARACTERNAME)
// result:
//		0: Successful
//		1: This character's name has already been changed. You cannot change a character's name more than once.
//		2: User information is not correct.
//		3: You have failed to change this character's name.
//		4: Another user is using this character name, so please select another one.
//		5: In order to change the character name, you must leave the guild.
//		6: In order to change the character name, you must leave the party.
//		7: Length exceeds the maximum size of the character name you want to change.
//		8: Name contains invalid characters. Character name change failed.
//		9: The name change is prohibited. Character name change failed.
//		10: Character name change failed, due an unknown error.
void char_rename_response(int fd, struct char_session_data *sd, int16 response) {
#if PACKETVER >= 20111101
	WFIFOHEAD(fd,6);
	WFIFOW(fd,0) = 0x8fd;
	WFIFOL(fd,2) = response;
	WFIFOSET(fd,6);
#else
	WFIFOHEAD(fd,4);
	WFIFOW(fd,0) = 0x290;
	WFIFOW(fd,2) = response;
	WFIFOSET(fd,4);
#endif
}

// Request to change a character name
// 028f <char_id>.L (CH_REQ_CHANGE_CHARNAME)
// 08fc <char_id>.L <new name>.24B (CH_REQ_CHANGE_CHARACTERNAME)
int char_parse_ackrename(int fd, struct char_session_data *sd) {
#if PACKETVER >= 20111101
	FIFOSD_CHECK(30)
	{
		int i, cid;
		char name[NAME_LENGTH], esc_name[NAME_LENGTH * 2 + 1];

		cid = RFIFOL(fd,2);
		safestrncpy(name, (char *)RFIFOP(fd,6), NAME_LENGTH);
		RFIFOSKIP(fd,30);

		ARR_FIND(0, MAX_CHARS, i, sd->found_char[i] == cid);
		if( i == MAX_CHARS )
			return 1;

		normalize_name(name, TRIM_CHARS);
		Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));

		safestrncpy(sd->new_name, name, NAME_LENGTH);

		i = rename_char_sql(sd, cid); // Start the renaming process

		char_rename_response(fd, sd, i);

		if( !i ) // If the renaming was successful, we need to resend the characters
			mmo_char_send(fd, sd);

		return 1;
	}
#else
	FIFOSD_CHECK(6)
	{
		int i;
		int cid = RFIFOL(fd,2);

		RFIFOSKIP(fd,6);
		ARR_FIND(0, MAX_CHARS, i, sd->found_char[i] == cid);
		if( i == MAX_CHARS )
			return 1;
		i = rename_char_sql(sd, cid);

		char_rename_response(fd, sd, i);
	}
	return 1;
#endif
}

/**
 * ZA 0x2b2d
 * <cmd>.W <char_id>.L
 * AZ 0x2b2f
 * <cmd>.W <len>.W <cid>.L <count>.B { <bonus_script_data>.?B }
 * Get bonus_script data(s) from table to load then send to player
 * @param fd
 * @author [Cydh]
 */
void bonus_script_get(int fd) {
	if( RFIFOREST(fd) < 6 )
		return;
	else {
		uint8 num_rows = 0;
		uint32 cid = RFIFOL(fd,2);
		struct bonus_script_data tmp_bsdata;
		SqlStmt *stmt = SqlStmt_Malloc(sql_handle);

		RFIFOSKIP(fd,6);
		if( SQL_ERROR == SqlStmt_Prepare(stmt,
			"SELECT `script`, `tick`, `flag`, `type`, `icon` FROM `%s` WHERE `char_id` = '%d' LIMIT %d",
			bonus_script_db, cid, MAX_PC_BONUS_SCRIPT) ||
			SQL_ERROR == SqlStmt_Execute(stmt) ||
			SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_STRING, &tmp_bsdata.script_str, sizeof(tmp_bsdata.script_str), NULL, NULL) ||
			SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_UINT32, &tmp_bsdata.tick, 0, NULL, NULL) ||
			SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_UINT16, &tmp_bsdata.flag, 0, NULL, NULL) ||
			SQL_ERROR == SqlStmt_BindColumn(stmt, 3, SQLDT_UINT8,  &tmp_bsdata.type, 0, NULL, NULL) ||
			SQL_ERROR == SqlStmt_BindColumn(stmt, 4, SQLDT_INT16,  &tmp_bsdata.icon, 0, NULL, NULL) )
		{
			SqlStmt_ShowDebug(stmt);
			SqlStmt_Free(stmt);
			return;
		}
		if( (num_rows = (uint8)SqlStmt_NumRows(stmt)) > 0 ) {
			uint8 i;
			uint32 size = 9 + num_rows * sizeof(struct bonus_script_data);

			WFIFOHEAD(fd,size);
			WFIFOW(fd,0) = 0x2b2f;
			WFIFOW(fd,2) = size;
			WFIFOL(fd,4) = cid;
			WFIFOB(fd,8) = num_rows;
			for( i = 0; i < num_rows && SQL_SUCCESS == SqlStmt_NextRow(stmt); i++ ) {
				struct bonus_script_data bsdata;

				memset(&bsdata, 0, sizeof(bsdata));
				memset(bsdata.script_str, '\0', sizeof(bsdata.script_str));
				safestrncpy(bsdata.script_str, tmp_bsdata.script_str, strlen(tmp_bsdata.script_str) + 1);
				bsdata.tick = tmp_bsdata.tick;
				bsdata.flag = tmp_bsdata.flag;
				bsdata.type = tmp_bsdata.type;
				bsdata.icon = tmp_bsdata.icon;
				memcpy(WFIFOP(fd, 9 + i * sizeof(struct bonus_script_data)), &bsdata, sizeof(struct bonus_script_data));
			}
			WFIFOSET(fd,size);
			ShowInfo("Bonus Script loaded for CID=%d. Total: %d.\n", cid, i);
			if (SQL_ERROR == SqlStmt_Prepare(stmt,"DELETE FROM `%s` WHERE `char_id`='%d'",bonus_script_db,cid) ||
				SQL_ERROR == SqlStmt_Execute(stmt))
				SqlStmt_ShowDebug(stmt);
		}
		SqlStmt_Free(stmt);
	}
}

/**
 * ZA 0x2b2e
 * <cmd>.W <len>.W <char_id>.L <count>.B { <bonus_script>.?B }
 * Save bonus_script data(s) to the table
 * @param fd
 * @author [Cydh]
 */
void bonus_script_save(int fd) {
	if( RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2) )
		return;
	else {
		uint32 cid = RFIFOL(fd,4);
		uint8 count = RFIFOB(fd,8);

		if( SQL_ERROR == Sql_Query(sql_handle,"DELETE FROM `%s` WHERE `char_id` = '%d'", bonus_script_db, cid) )
			Sql_ShowDebug(sql_handle);
		if( count > 0 ) {
			char esc_script[MAX_BONUS_SCRIPT_LENGTH * 2];
			struct bonus_script_data bsdata;
			StringBuf buf;
			uint8 i;

			StringBuf_Init(&buf);
			StringBuf_Printf(&buf, "INSERT INTO `%s` (`char_id`, `script`, `tick`, `flag`, `type`, `icon`) VALUES ", bonus_script_db);
			for( i = 0; i < count; ++i ) {
				memcpy(&bsdata, RFIFOP(fd, 9 + i * sizeof(struct bonus_script_data)), sizeof(struct bonus_script_data));
				Sql_EscapeString(sql_handle, esc_script, bsdata.script_str);
				if( i > 0 )
					StringBuf_AppendStr(&buf, ", ");
				StringBuf_Printf(&buf, "('%d','%s','%d','%d','%d','%d')", cid, esc_script, bsdata.tick, bsdata.flag, bsdata.type, bsdata.icon);
			}
			if( SQL_ERROR == Sql_QueryStr(sql_handle,StringBuf_Value(&buf)) )
				Sql_ShowDebug(sql_handle);
			StringBuf_Destroy(&buf);
		}
		ShowInfo("Saved %d bonus_script for char_id: %d\n", count, cid);
		RFIFOSKIP(fd,RFIFOW(fd,2));
	}
}

//R 06C <ErrorCode>B HEADER_HC_REFUSE_ENTER
void char_reject(int fd, uint8 errCode) {
    WFIFOHEAD(fd,3);
    WFIFOW(fd,0) = 0x6c;
    WFIFOB(fd,2) = errCode; //Rejected from server
    WFIFOSET(fd,3);
}

/**
 * Inform client that his deletion request was refused
 * 0x70 <ErrorCode>B HC_REFUSE_DELETECHAR
 * @param fd
 * @param ErrorCode
 *   00: Incorrect Email address
 *   01 = Invalid Slot
 *   02 = In a party or guild
 */
void char_refuse_delchar(int fd, uint8 errCode) {
	WFIFOHEAD(fd,3);
	WFIFOW(fd,0) = 0x70;
	WFIFOB(fd,2) = errCode;
	WFIFOSET(fd,3);
}

/**
 * Inform the mapserv wheater his login attemp to us was a success or not
 * @param fd: file descriptor to parse, (link to mapserv)
 * @param errCode 0: Success, 3: Fail
 */
void char_connectack(int fd, uint8 errCode) {
	WFIFOHEAD(fd,3);
	WFIFOW(fd,0) = 0x2af9;
	WFIFOB(fd,2) = errCode;
	WFIFOSET(fd,3);
}

/**
 * Inform mapserv of a new character selection request
 * @param fd: FD link tomapserv
 * @param aid: Player account id
 * @param res: result, 0: Not OK, 1: OK
 */
void char_charselres(int fd, uint32 aid, uint8 res) {
	WFIFOHEAD(fd,7);
	WFIFOW(fd,0) = 0x2b03;
	WFIFOL(fd,2) = aid;
	WFIFOB(fd,6) = res;
	WFIFOSET(fd,7);
}

/**
 * Inform the mapserv, of a change mapserv request
 * @param fd: Link to mapserv
 * @param nok 0: Accepted or 1: No
 */
void char_changemapserv_ack(int fd, bool nok) {
	WFIFOHEAD(fd,30);
	WFIFOW(fd,0) = 0x2b06;
	memcpy(WFIFOP(fd,2), RFIFOP(fd,2), 28);
	if(nok)
		WFIFOL(fd,6) = 0; //Set login1 to 0 (Not OK)
	WFIFOSET(fd,30);
}

void char_send_map_data(int fd, struct mmo_charstatus *cd, uint32 ipl, int map_server_index)
{
#if PACKETVER >= 20170315
	int cmd = 0xac5;
	int size = 156;
#else
	int cmd = 0x71;
	int size = 28;
#endif
	uint32 subnet_map_ip;

	WFIFOHEAD(fd,size);
	WFIFOW(fd,0) = cmd;
	WFIFOL(fd,2) = cd->char_id;
	mapindex_getmapname_ext(mapindex_id2name(cd->last_point.map),(char *)WFIFOP(fd,6));
	subnet_map_ip = lan_subnetcheck(ipl); //Advanced subnet check [LuzZza]
	WFIFOL(fd,22) = htonl((subnet_map_ip ? subnet_map_ip : server[map_server_index].ip));
	WFIFOW(fd,26) = ntows(htons(server[map_server_index].port)); //[!] LE byte order here [!]
#if PACKETVER >= 20170315
	memset(WFIFOP(fd,28),0,128); //Unknown
#endif
	WFIFOSET(fd,size);
}

//------------------------------------------------
//Invoked 15 seconds after mapif_disconnectplayer in case the map server doesn't
//replies/disconnect the player we tried to kick [Skotlex]
//------------------------------------------------
static int chardb_waiting_disconnect(int tid, unsigned int tick, int id, intptr_t data)
{
	struct online_char_data *character;

	//Mark it offline due to timeout
	if((character = (struct online_char_data *)idb_get(online_char_db, id)) != NULL && character->waiting_disconnect == tid) {
		character->waiting_disconnect = INVALID_TIMER;
		set_char_offline(character->char_id, character->account_id);
	}
	return 0;
}

/**
 * @see DBApply
 */
static int online_data_cleanup_sub(DBKey key, DBData *data, va_list ap)
{
	struct online_char_data *character = db_data2ptr(data);

	if(character->fd != -1)
		return 0; //Character still connected
	if(character->server == -2) //Unknown server, set them offline
		set_char_offline(character->char_id, character->account_id);
	if(character->server < 0) //Free data from players that have not been online for a while
		db_remove(online_char_db, key);
	return 0;
}

static int online_data_cleanup(int tid, unsigned int tick, int id, intptr_t data)
{
	online_char_db->foreach(online_char_db, online_data_cleanup_sub);
	return 0;
}

static int clan_member_cleanup(int tid, unsigned int tick, int id, intptr_t data)
{
	if(clan_remove_inactive_days <= 0) //Auto removal is disabled
		return 0;
	if(SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `clan_id`='0' WHERE `online`='0' AND `clan_id`<>'0' AND `last_login` IS NOT NULL AND `last_login` <= NOW() - INTERVAL %d DAY", char_db, clan_remove_inactive_days))
		Sql_ShowDebug(sql_handle);
	return 0;
}

//----------------------------------
// Reading Lan Support configuration
// Rewrote: Anvanced subnet check [LuzZza]
//----------------------------------
int char_lan_config_read(const char *lancfgName)
{
	FILE *fp;
	int line_num = 0;
	char line[1024], w1[64], w2[64], w3[64], w4[64];

	if((fp = fopen(lancfgName, "r")) == NULL) {
		ShowWarning("LAN Support configuration file is not found: %s\n", lancfgName);
		return 1;
	}

	while(fgets(line, sizeof(line), fp)) {
		line_num++;
		if((line[0] == '/' && line[1] == '/') || line[0] == '\n' || line[1] == '\n')
			continue;

		if(sscanf(line,"%63[^:]: %63[^:]:%63[^:]:%63[^\r\n]", w1, w2, w3, w4) != 4) {
			ShowWarning("Error syntax of configuration file %s in line %d.\n", lancfgName, line_num);
			continue;
		}

		remove_control_chars(w1);
		remove_control_chars(w2);
		remove_control_chars(w3);
		remove_control_chars(w4);

		if(strcmpi(w1, "subnet") == 0) {
			subnet[subnet_count].mask = str2ip(w2);
			subnet[subnet_count].char_ip = str2ip(w3);
			subnet[subnet_count].map_ip = str2ip(w4);

			if((subnet[subnet_count].char_ip & subnet[subnet_count].mask) != (subnet[subnet_count].map_ip & subnet[subnet_count].mask)) {
				ShowError("%s: Configuration Error: The char server (%s) and map server (%s) belong to different subnetworks!\n", lancfgName, w3, w4);
				continue;
			}

			subnet_count++;
		}
	}

	if(subnet_count > 1) //Only useful if there is more than 1
		ShowStatus("Read information about %d subnetworks.\n", subnet_count);

	fclose(fp);
	return 0;
}

void sql_config_read(const char *cfgName)
{
	char line[1024], w1[1024], w2[1024];
	FILE *fp;

	if((fp = fopen(cfgName, "r")) == NULL) {
		ShowError("File not found: %s\n", cfgName);
		return;
	}

	while(fgets(line, sizeof(line), fp)) {
		if(line[0] == '/' && line[1] == '/')
			continue;

		if(sscanf(line, "%1023[^:]: %1023[^\r\n]", w1, w2) != 2)
			continue;

		if(!strcmpi(w1, "char_db"))
			safestrncpy(char_db, w2, sizeof(char_db));
		else if(!strcmpi(w1, "scdata_db"))
			safestrncpy(scdata_db, w2, sizeof(scdata_db));
		else if(!strcmpi(w1, "cart_db"))
			safestrncpy(cart_db, w2, sizeof(cart_db));
		else if(!strcmpi(w1, "inventory_db"))
			safestrncpy(inventory_db, w2, sizeof(inventory_db));
		else if(!strcmpi(w1, "charlog_db"))
			safestrncpy(charlog_db, w2, sizeof(charlog_db));
		else if(!strcmpi(w1, "reg_db"))
			safestrncpy(reg_db, w2, sizeof(reg_db));
		else if(!strcmpi(w1, "skill_db"))
			safestrncpy(skill_db, w2, sizeof(skill_db));
		else if(!strcmpi(w1, "interlog_db"))
			safestrncpy(interlog_db, w2, sizeof(interlog_db));
		else if(!strcmpi(w1, "memo_db"))
			safestrncpy(memo_db, w2, sizeof(memo_db));
		else if(!strcmpi(w1, "guild_db"))
			safestrncpy(guild_db, w2, sizeof(guild_db));
		else if(!strcmpi(w1, "guild_alliance_db"))
			safestrncpy(guild_alliance_db, w2, sizeof(guild_alliance_db));
		else if(!strcmpi(w1, "guild_castle_db"))
			safestrncpy(guild_castle_db, w2, sizeof(guild_castle_db));
		else if(!strcmpi(w1, "guild_expulsion_db"))
			safestrncpy(guild_expulsion_db, w2, sizeof(guild_expulsion_db));
		else if(!strcmpi(w1, "guild_member_db"))
			safestrncpy(guild_member_db, w2, sizeof(guild_member_db));
		else if(!strcmpi(w1, "guild_skill_db"))
			safestrncpy(guild_skill_db, w2, sizeof(guild_skill_db));
		else if(!strcmpi(w1, "guild_position_db"))
			safestrncpy(guild_position_db, w2, sizeof(guild_position_db));
		else if(!strcmpi(w1, "guild_storage_db"))
			safestrncpy(guild_storage_db, w2, sizeof(guild_storage_db));
		else if(!strcmpi(w1, "party_db"))
			safestrncpy(party_db, w2, sizeof(party_db));
		else if(!strcmpi(w1, "pet_db"))
			safestrncpy(pet_db, w2, sizeof(pet_db));
		else if(!strcmpi(w1, "mail_db"))
			safestrncpy(mail_db, w2, sizeof(mail_db));
		else if(!strcmpi(w1, "mail_attachment_db"))
			safestrncpy(mail_attachment_db, w2, sizeof(mail_attachment_db));
		else if(!strcmpi(w1, "auction_db"))
			safestrncpy(auction_db, w2, sizeof(auction_db));
		else if(!strcmpi(w1, "friend_db"))
			safestrncpy(friend_db, w2, sizeof(friend_db));
		else if(!strcmpi(w1, "hotkey_db"))
			safestrncpy(hotkey_db, w2, sizeof(hotkey_db));
		else if(!strcmpi(w1, "quest_db"))
			safestrncpy(quest_db, w2, sizeof(quest_db));
		else if(!strcmpi(w1, "homunculus_db"))
			safestrncpy(homunculus_db, w2, sizeof(homunculus_db));
		else if(!strcmpi(w1, "skill_homunculus_db"))
			safestrncpy(skill_homunculus_db, w2, sizeof(skill_homunculus_db));
		else if(!strcmpi(w1, "mercenary_db"))
			safestrncpy(mercenary_db, w2, sizeof(mercenary_db));
		else if(!strcmpi(w1, "mercenary_owner_db"))
			safestrncpy(mercenary_owner_db, w2, sizeof(mercenary_owner_db));
		else if(!strcmpi(w1, "ragsrvinfo_db"))
			safestrncpy(ragsrvinfo_db, w2,sizeof(ragsrvinfo_db));
		else if(!strcmpi(w1, "elemental_db"))
			safestrncpy(elemental_db, w2,sizeof(elemental_db));
		else if(!strcmpi(w1, "interreg_db"))
			safestrncpy(interreg_db, w2, sizeof(interreg_db));
		else if(!strcmpi(w1, "skillcooldown_db"))
			safestrncpy(skillcooldown_db, w2, sizeof(skillcooldown_db));
		else if(!strcmpi(w1, "bonus_script_db"))
			safestrncpy(bonus_script_db, w2, sizeof(bonus_script_db));
		else if(!strcmpi(w1, "clan_db"))
			safestrncpy(clan_db, w2, sizeof(clan_db));
		else if(!strcmpi(w1, "clan_alliance_db"))
			safestrncpy(clan_alliance_db, w2, sizeof(clan_alliance_db));
		else if(!strcmpi(w1, "achievement_db"))
			safestrncpy(achievement_db, w2, sizeof(achievement_db));
		//Support the import command, just like any other config
		else if(!strcmpi(w1, "import"))
			sql_config_read(w2);
	}
	fclose(fp);
	ShowInfo("Done reading %s.\n", cfgName);
}

/**
 * Split start_point configuration values.
 * @param w1_value: Value from w1
 * @param w2_value: Value from w2
 * @param start_point_cur: Start point reference
 * @param count: Start point count reference
 */
static void char_config_split_startpoint(char *w1_value, char *w2_value, struct point start_point_cur[MAX_STARTPOINT], short *count)
{
	char *lineitem, **fields;
	int i = 0, fields_length = 3 + 1;

	(*count) = 0; //Reset to begin reading

	fields = (char **)aMalloc(fields_length * sizeof(char *));
	if(fields == NULL)
		return; //Failed to allocate memory
	lineitem = strtok(w2_value, ":");

	while(lineitem != NULL && (*count) < MAX_STARTPOINT) {
		int n = sv_split(lineitem, strlen(lineitem), 0, ',', fields, fields_length, SV_NOESCAPE_NOTERMINATE);

		if(n + 1 < fields_length) {
			ShowDebug("%s: not enough arguments for %s! Skipping...\n", w1_value, lineitem);
			lineitem = strtok(NULL, ":"); //Next lineitem
			continue;
		}
		start_point_cur[i].map = mapindex_name2id(fields[1]);
		if(!start_point_cur[i].map) {
			ShowError("Start point %s not found in map-index cache. Setting to default location.\n", start_point_cur[i].map);
			start_point_cur[i].map = mapindex_name2id(MAP_DEFAULT_NAME);
			start_point_cur[i].x = MAP_DEFAULT_X;
			start_point_cur[i].y = MAP_DEFAULT_Y;
		} else {
			start_point_cur[i].x = max(0, atoi(fields[2]));
			start_point_cur[i].y = max(0, atoi(fields[3]));
		}
		(*count)++;
		lineitem = strtok(NULL, ":"); //Next lineitem
		i++;
	}
	aFree(fields);
}

/**
 * Split start_items configuration values.
 * @param w1_value: Value from w1
 * @param w2_value: Value from w2
 * @param start_items_cur: Start item reference
 */
static void char_config_split_startitem(char *w1_value, char *w2_value, struct startitem start_items_cur[MAX_STARTITEM])
{
	char *lineitem, **fields;
	int i = 0, fields_length = 3 + 1;

	fields = (char **)aMalloc(fields_length * sizeof(char *));
	if(fields == NULL)
		return; //Failed to allocate memory
	lineitem = strtok(w2_value, ":");

	while(lineitem != NULL && i < MAX_STARTITEM) {
		int n = sv_split(lineitem, strlen(lineitem), 0, ',', fields, fields_length, SV_NOESCAPE_NOTERMINATE);

		if(n + 1 < fields_length) {
			ShowDebug("%s: not enough arguments for %s! Skipping...\n", w1_value, lineitem);
			lineitem = strtok(NULL, ":"); //Next lineitem
			continue;
		}
		//TODO: Item ID verification
		start_items_cur[i].nameid = max(0, atoi(fields[1]));
		start_items_cur[i].amount = max(0, atoi(fields[2]));
		start_items_cur[i].pos = max(0, atoi(fields[3]));
		lineitem = strtok(NULL, ":"); //Next lineitem
		i++;
	}
	aFree(fields);
}

int char_config_read(const char *cfgName)
{
	char line[1024], w1[1024], w2[1024];
	FILE *fp = fopen(cfgName, "r");

	if(fp == NULL) {
		ShowError("Configuration file not found: %s.\n", cfgName);
		return 1;
	}

	while(fgets(line, sizeof(line), fp)) {
		if(line[0] == '/' && line[1] == '/')
			continue;
		if(sscanf(line, "%1023[^:]: %1023[^\r\n]", w1, w2) != 2)
			continue;

		remove_control_chars(w1);
		remove_control_chars(w2);
		if(strcmpi(w1, "timestamp_format") == 0)
			safestrncpy(timestamp_format, w2, sizeof(timestamp_format));
		else if(strcmpi(w1, "console_silent") == 0) {
			msg_silent = atoi(w2);
			if(msg_silent) //Only bother if its actually enabled
				ShowInfo("Console Silent Setting: %d\n", atoi(w2));
		} else if(strcmpi(w1, "console_msg_log") == 0)
			console_msg_log = atoi(w2);
		else if(strcmpi(w1, "console_log_filepath") == 0)
			safestrncpy(console_log_filepath, w2, sizeof(console_log_filepath));
		else if(strcmpi(w1, "stdout_with_ansisequence") == 0)
			stdout_with_ansisequence = config_switch(w2);
		else if(strcmpi(w1, "userid") == 0)
			safestrncpy(userid, w2, sizeof(userid));
		else if(strcmpi(w1, "passwd") == 0)
			safestrncpy(passwd, w2, sizeof(passwd));
		else if(strcmpi(w1, "server_name") == 0)
			safestrncpy(server_name, w2, sizeof(server_name));
		else if(strcmpi(w1, "wisp_server_name") == 0) {
			if(strlen(w2) >= 4)
				safestrncpy(wisp_server_name, w2, sizeof(wisp_server_name));
		} else if(strcmpi(w1, "login_ip") == 0) {
			login_ip = host2ip(w2);
			if(login_ip) {
				char ip_str[16];

				safestrncpy(login_ip_str, w2, sizeof(login_ip_str));
				ShowStatus("Login server IP address : %s -> %s\n", w2, ip2str(login_ip, ip_str));
			}
		} else if(strcmpi(w1, "login_port") == 0)
			login_port = atoi(w2);
		else if(strcmpi(w1, "char_ip") == 0) {
			char_ip = host2ip(w2);
			if(char_ip) {
				char ip_str[16];

				safestrncpy(char_ip_str, w2, sizeof(char_ip_str));
				ShowStatus("Character server IP address : %s -> %s\n", w2, ip2str(char_ip, ip_str));
			}
		} else if(strcmpi(w1, "bind_ip") == 0) {
			bind_ip = host2ip(w2);
			if(bind_ip) {
				char ip_str[16];

				safestrncpy(bind_ip_str, w2, sizeof(bind_ip_str));
				ShowStatus("Character server binding IP address : %s -> %s\n", w2, ip2str(bind_ip, ip_str));
			}
		} else if(strcmpi(w1, "char_port") == 0)
			char_port = atoi(w2);
		else if(strcmpi(w1, "char_server_type") == 0)
			char_server_type = atoi(w2);
		else if(strcmpi(w1, "char_new") == 0)
			char_new = (bool)atoi(w2);
		else if(strcmpi(w1, "char_new_display") == 0)
			char_new_display = atoi(w2);
		else if(strcmpi(w1, "max_connect_user") == 0) {
			max_connect_user = atoi(w2);
			if(max_connect_user < -1)
				max_connect_user = -1;
		} else if(strcmpi(w1, "gm_allow_group") == 0)
			gm_allow_group = atoi(w2);
		else if(strcmpi(w1, "autosave_time") == 0) {
			autosave_interval = atoi(w2) * 1000;
			if(autosave_interval <= 0)
				autosave_interval = DEFAULT_AUTOSAVE_INTERVAL;
		} else if(strcmpi(w1, "save_log") == 0)
			save_log = config_switch(w2);
#ifdef RENEWAL
		else if(strcmpi(w1, "start_point") == 0)
#else
		else if(strcmpi(w1, "start_point_pre") == 0)
#endif
			char_config_split_startpoint(w1, w2, start_point, &start_point_count);
#if PACKETVER >= 20151104
		else if(strcmpi(w1, "start_point_doram") == 0)
			char_config_split_startpoint(w1, w2, start_point_doram, &start_point_count_doram);
#endif
		else if(strcmpi(w1, "start_zeny") == 0) {
			start_zeny = atoi(w2);
			if(start_zeny < 0)
				start_zeny = 0;
		} else if(strcmpi(w1, "start_items") == 0)
			char_config_split_startitem(w1, w2, start_items);
#if PACKETVER >= 20151104
		else if (strcmpi(w1, "start_items_doram") == 0)
			char_config_split_startitem(w1, w2, start_items_doram);
#endif
		else if(strcmpi(w1,"log_char") == 0) //Log char or not [devil]
			log_char = atoi(w2);
		else if(strcmpi(w1, "unknown_char_name") == 0) {
			safestrncpy(unknown_char_name, w2, sizeof(unknown_char_name));
			unknown_char_name[NAME_LENGTH-1] = '\0';
		} else if(strcmpi(w1, "name_ignoring_case") == 0)
			name_ignoring_case = (bool)config_switch(w2);
		else if(strcmpi(w1, "char_name_option") == 0)
			char_name_option = atoi(w2);
		else if(strcmpi(w1, "char_name_letters") == 0)
			safestrncpy(char_name_letters, w2, sizeof(char_name_letters));
		else if(strcmpi(w1, "char_del_level") == 0) //Disable/enable char deletion by its level condition [Lupus]
			char_del_level = atoi(w2);
		else if(strcmpi(w1, "char_del_delay") == 0)
			char_del_delay = atoi(w2);
		else if(strcmpi(w1, "char_del_option") == 0)
			char_del_option = atoi(w2);
		else if(strcmpi(w1, "char_del_restriction") == 0)
			char_del_restriction = atoi(w2);
		else if(strcmpi(w1, "db_path") == 0)
			safestrncpy(db_path, w2, sizeof(db_path));
		else if(strcmpi(w1, "console") == 0)
			console = config_switch(w2);
		else if(strcmpi(w1, "fame_list_alchemist") == 0) {
			fame_list_size_chemist = atoi(w2);
			if(fame_list_size_chemist > MAX_FAME_LIST) {
				ShowWarning("Max fame list size is %d (fame_list_alchemist)\n", MAX_FAME_LIST);
				fame_list_size_chemist = MAX_FAME_LIST;
			}
		} else if(strcmpi(w1, "fame_list_blacksmith") == 0) {
			fame_list_size_smith = atoi(w2);
			if(fame_list_size_smith > MAX_FAME_LIST) {
				ShowWarning("Max fame list size is %d (fame_list_blacksmith)\n", MAX_FAME_LIST);
				fame_list_size_smith = MAX_FAME_LIST;
			}
		} else if(strcmpi(w1, "fame_list_taekwon") == 0) {
			fame_list_size_taekwon = atoi(w2);
			if(fame_list_size_taekwon > MAX_FAME_LIST) {
				ShowWarning("Max fame list size is %d (fame_list_taekwon)\n", MAX_FAME_LIST);
				fame_list_size_taekwon = MAX_FAME_LIST;
			}
		} else if(strcmpi(w1, "guild_exp_rate") == 0)
			guild_exp_rate = atoi(w2);
		else if(strcmpi(w1, "pincode_enabled") == 0)
#if PACKETVER_SUPPORTS_PINCODE
			pincode_enabled = config_switch(w2);
		else if(strcmpi(w1, "pincode_changetime") == 0)
			pincode_changetime = atoi(w2) * 60 * 60 * 24;
		else if(strcmpi(w1, "pincode_maxtry") == 0)
			pincode_maxtry = atoi(w2);
		else if(strcmpi(w1, "pincode_force") == 0)
			pincode_force = config_switch(w2);
		else if(strcmpi(w1, "pincode_allow_repeated") == 0)
			pincode_allow_repeated = config_switch(w2);
		else if(strcmpi(w1, "pincode_allow_sequential") == 0)
			pincode_allow_sequential = config_switch(w2);
#else
		{
			if(config_switch(w2))
				ShowWarning("pincode_enabled requires PACKETVER 20110309 or higher.\n");
		}
#endif
		else if(strcmpi(w1, "char_move_enabled") == 0)
			char_move_enabled = config_switch(w2);
		else if(strcmpi(w1, "char_movetoused") == 0)
			char_movetoused = config_switch(w2);
		else if(strcmpi(w1, "char_moves_unlimited") == 0)
			char_moves_unlimited = config_switch(w2);
		else if(strcmpi(w1, "char_rename_party") == 0)
			char_rename_party = (bool)config_switch(w2);
		else if(strcmpi(w1, "char_rename_guild") == 0)
			char_rename_guild = (bool)config_switch(w2);
		else if(strcmpi(w1, "char_maintenance_min_group_id") == 0)
			char_maintenance_min_group_id = atoi(w2);
		else if(strcmpi(w1, "default_map") == 0)
			safestrncpy(default_map, w2, MAP_NAME_LENGTH);
		else if(strcmpi(w1, "default_map_x") == 0)
			default_map_x = atoi(w2);
		else if(strcmpi(w1, "default_map_y") == 0)
			default_map_y = atoi(w2);
		else if(strcmpi(w1, "clan_remove_inactive_days") == 0)
			clan_remove_inactive_days = atoi(w2);
		else if(strcmpi(w1, "mail_return_days") == 0)
			mail_return_days = atoi(w2);
		else if(strcmpi(w1, "mail_delete_days") == 0)
			mail_delete_days = atoi(w2);
		else if(strcmpi(w1, "import") == 0)
			char_config_read(w2);
	}
	fclose(fp);

	ShowInfo("Done reading %s.\n", cfgName);
	return 0;
}

/**
 * Checks for values out of range.
 */
static void char_config_adjust() {
#if PACKETVER < 20100803
	if(char_del_option&CHAR_DEL_BIRTHDATE) {
		ShowWarning("conf/char_athena.conf:char_del_option birthdate is enabled but it requires PACKETVER 2010-08-03 or newer, defaulting to email...\n");
		char_del_option &= ~CHAR_DEL_BIRTHDATE;
	}
#endif
}

void do_final(void)
{
	ShowStatus("Terminating...\n");

	set_all_offline(-1);
	set_all_offline_sql();

	inter_final();

	flush_fifos();

	do_final_msg();
	do_final_mapif();
	do_final_loginif();

	if(SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s`", ragsrvinfo_db))
		Sql_ShowDebug(sql_handle);

	char_db_->destroy(char_db_, NULL);
	online_char_db->destroy(online_char_db, NULL);
	auth_db->destroy(auth_db, NULL);

	if(char_fd != -1) {
		do_close(char_fd);
		char_fd = -1;
	}

	Sql_Free(sql_handle);
	mapindex_final();

	ShowStatus("Finished.\n");
}

//------------------------------
// Function called when the server
// has received a crash signal.
//------------------------------
void do_abort(void)
{
}

void set_server_type(void)
{
	SERVER_TYPE = ATHENA_SERVER_CHAR;
}

/// Called when a terminate signal is received
void do_shutdown(void)
{
	if(runflag != CHARSERVER_ST_SHUTDOWN) {
		int id;

		runflag = CHARSERVER_ST_SHUTDOWN;
		ShowStatus("Shutting down...\n");
		//TODO: Proper shutdown procedure; wait for acks?, kick all characters, ... [FlavoJS]
		for(id = 0; id < ARRAYLENGTH(server); ++id)
			mapif_server_reset(id);
		loginif_check_shutdown();
		flush_fifos();
		runflag = CORE_ST_STOP;
	}
}

int do_init(int argc, char **argv)
{
	//Read map indexes
	runflag = CHARSERVER_ST_STARTING;
	mapindex_init();

	start_point[0].map = mapindex_name2id(MAP_DEFAULT_NAME);
	start_point[0].x = MAP_DEFAULT_X;
	start_point[0].y = MAP_DEFAULT_Y;

#if PACKETVER >= 20151104
	start_point_doram[0].map = mapindex_name2id(MAP_DEFAULT_NAME);
	start_point_doram[0].x = MAP_DEFAULT_X;
	start_point_doram[0].y = MAP_DEFAULT_Y;
#endif

	start_items[0].nameid = 1201;
	start_items[0].amount = 1;
	start_items[0].pos = 2;
	start_items[1].nameid = 2301;
	start_items[1].amount = 1;
	start_items[1].pos = 16;

#if PACKETVER >= 20151104
	start_items_doram[0].nameid = 1681;
	start_items_doram[0].amount = 1;
	start_items_doram[0].pos = 2;
	start_items_doram[1].nameid = 2301;
	start_items_doram[1].amount = 1;
	start_items_doram[1].pos = 16;
#endif

	safestrncpy(default_map, "prontera", MAP_NAME_LENGTH);

	//Init default value
	CHAR_CONF_NAME = "conf/char_athena.conf";
	LAN_CONF_NAME = "conf/subnet_athena.conf";
	SQL_CONF_NAME = "conf/inter_athena.conf";
	MSG_CONF_NAME = "conf/msg_conf/char_msg.conf";
	safestrncpy(console_log_filepath, "./log/char-msg_log.log", sizeof(console_log_filepath));

	cli_get_options(argc,argv);

	msg_config_read(MSG_CONF_NAME);
	char_config_read(CHAR_CONF_NAME);
	char_config_adjust();
	char_lan_config_read(LAN_CONF_NAME);
	sql_config_read(SQL_CONF_NAME);

	if(strcmp(userid, "s1") == 0 && strcmp(passwd, "p1") == 0) {
		ShowWarning("Using the default user/password s1/p1 is NOT RECOMMENDED.\n");
		ShowNotice("Please edit your 'login' table to create a proper inter-server user/password (gender 'S')\n");
		ShowNotice("And then change the user/password to use in conf/char_athena.conf (or conf/import/char_conf.txt)\n");
	}

	inter_init_sql((argc > 2) ? argv[2] : inter_cfgName); //inter-server configuration

	auth_db = idb_alloc(DB_OPT_RELEASE_DATA);
	online_char_db = idb_alloc(DB_OPT_RELEASE_DATA);
	mmo_char_sql_init();
	char_read_fame_list(); //Read fame lists

	if((naddr_ != 0) && (!login_ip || !char_ip)) {
		char ip_str[16];
		ip2str(addr_[0], ip_str);

		if(naddr_ > 1)
			ShowStatus("Multiple interfaces detected..  using %s as our IP address\n", ip_str);
		else
			ShowStatus("Defaulting to %s as our IP address\n", ip_str);
		if(!login_ip) {
			safestrncpy(login_ip_str, ip_str, sizeof(login_ip_str));
			login_ip = str2ip(login_ip_str);
		}
		if(!char_ip) {
			safestrncpy(char_ip_str, ip_str, sizeof(char_ip_str));
			char_ip = str2ip(char_ip_str);
		}
	}

	do_init_loginif();
	do_init_mapif();

	//Periodically update the overall user count on all mapservers + login server
	add_timer_func_list(broadcast_user_count, "broadcast_user_count");
	add_timer_interval(gettick() + 1000, broadcast_user_count, 0, 0, 5 * 1000);

	//Timer to clear (online_char_db)
	add_timer_func_list(chardb_waiting_disconnect, "chardb_waiting_disconnect");

	//Online Data timers (checking if char still connected)
	add_timer_func_list(online_data_cleanup, "online_data_cleanup");
	add_timer_interval(gettick() + 1000, online_data_cleanup, 0, 0, 600 * 1000);

	//Periodically remove players that have not logged in for a long time from clans
	add_timer_func_list(clan_member_cleanup, "clan_member_cleanup");
	add_timer_interval(gettick() + 1000, clan_member_cleanup, 0, 0, 60 * 60 * 1000); //Every 60 minutes

	//Periodically check if mails need to be returned to their sender
	add_timer_func_list(mail_return_timer, "mail_return_timer");
	add_timer_interval(gettick() + 1000, mail_return_timer, 0, 0, 5 * 60 * 1000); //Every 5 minutes

	//Periodically check if mails need to be deleted completely
	add_timer_func_list(mail_delete_timer, "mail_delete_timer");
	add_timer_interval(gettick() + 1000, mail_delete_timer, 0, 0, 5 * 60 * 1000); //Every 5 minutes

	//Cleaning the tables for NULL entrys @ startup [Sirius]
	//Chardb clean
	if(SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `account_id` = '0'", char_db))
		Sql_ShowDebug(sql_handle);

	//Guilddb clean
	if(SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `guild_lv` = '0' AND `max_member` = '0' AND `exp` = '0' AND `next_exp` = '0' AND `average_lv` = '0'", guild_db))
		Sql_ShowDebug(sql_handle);

	//Guildmemberdb clean
	if(SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `guild_id` = '0' AND `account_id` = '0' AND `char_id` = '0'", guild_member_db))
		Sql_ShowDebug(sql_handle);

	set_defaultparse(parse_char);

	if((char_fd = make_listen_bind(bind_ip,char_port)) == -1) {
		ShowFatalError("Failed to bind to port '"CL_WHITE"%d"CL_RESET"'\n",char_port);
		exit(EXIT_FAILURE);
	}

	Sql_HerculesUpdateCheck(sql_handle);

	mapindex_check_mapdefault(default_map);
	ShowInfo("Default map: '"CL_WHITE"%s %d,%d"CL_RESET"'\n", default_map, default_map_x, default_map_y);

	ShowStatus("The char-server is "CL_GREEN"ready"CL_RESET" (Server is listening on the port %d).\n\n", char_port);

	if(runflag != CORE_ST_STOP) {
		shutdown_callback = do_shutdown;
		runflag = CHARSERVER_ST_RUNNING;
	}

	if(console) { //start listening
		add_timer_func_list(parse_console_timer, "parse_console_timer");
		add_timer_interval(gettick()+1000, parse_console_timer, 0, 0, 1000); //start in 1s each 1sec
	}

	return 0;
}

int char_msg_config_read(char *cfgName) {
	return _msg_config_read(cfgName,CHAR_MAX_MSG,msg_table);
}
const char *char_msg_txt(int msg_number) {
	return _msg_txt(msg_number,CHAR_MAX_MSG,msg_table);
}
void char_do_final_msg(void) {
	_do_final_msg(CHAR_MAX_MSG,msg_table);
}

/*======================================================
 * Login-Server help option info
 *------------------------------------------------------*/
void display_helpscreen(bool do_exit)
{
	ShowInfo("Usage: %s [options]\n", SERVER_NAME);
	ShowInfo("\n");
	ShowInfo("Options:\n");
	ShowInfo("  -?, -h [--help]\t\tDisplays this help screen.\n");
	ShowInfo("  -v [--version]\t\tDisplays the server's version.\n");
	ShowInfo("  --run-once\t\t\tCloses server after loading (testing).\n");
	ShowInfo("  --char-config <file>\t\tAlternative char-server configuration.\n");
	ShowInfo("  --lan-config <file>\t\tAlternative lag configuration.\n");
	ShowInfo("  --inter-config <file>\t\tAlternative inter-server configuration.\n");
	ShowInfo("  --msg-config <file>\t\tAlternative message configuration.\n");
	if(do_exit)
		exit(EXIT_SUCCESS);
}
