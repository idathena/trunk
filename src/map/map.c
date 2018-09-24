// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/core.h"
#include "../common/timer.h"
#include "../common/ers.h"
#include "../common/grfio.h"
#include "../common/malloc.h"
#include "../common/socket.h" // WFIFO*()
#include "../common/showmsg.h"
#include "../common/nullpo.h"
#include "../common/random.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/cli.h"
#include "../common/ers.h"

#include "map.h"
#include "path.h"
#include "chrif.h"
#include "clan.h"
#include "clif.h"
#include "duel.h"
#include "intif.h"
#include "npc.h"
#include "pc.h"
#include "status.h"
#include "mob.h"
#include "npc.h" // npc_setcells(), npc_unsetcells()
#include "chat.h"
#include "itemdb.h"
#include "storage.h"
#include "skill.h"
#include "trade.h"
#include "party.h"
#include "unit.h"
#include "battle.h"
#include "battleground.h"
#include "quest.h"
#include "script.h"
#include "mapreg.h"
#include "guild.h"
#include "pet.h"
#include "homunculus.h"
#include "instance.h"
#include "mercenary.h"
#include "elemental.h"
#include "atcommand.h"
#include "log.h"
#include "mail.h"
#include "cashshop.h"
#include "channel.h"
#include "vending.h"
#include "achievement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#ifndef _WIN32
#include <unistd.h>
#endif

char default_codepage[32] = "";

int map_server_port = 3306;
char map_server_ip[32] = "127.0.0.1";
char map_server_id[32] = "ragnarok";
char map_server_pw[32] = "";
char map_server_db[32] = "ragnarok";
Sql *mmysql_handle;
Sql *qsmysql_handle; // For query_sql

int db_use_sqldbs = 0;
char buyingstores_db[32] = "buyingstores";
char buyingstore_items_db[32] = "buyingstore_items";
char guild_storage_log_db[32] = "guild_storage_log";
char item_db_db[32] = "item_db";
char item_db2_db[32] = "item_db2";
char item_db_re_db[32] = "item_db_re";
char item_cash_db_db[32] = "item_cash_db";
char item_cash_db2_db[32] = "item_cash_db2";
char markets_db[32] = "markets";
char mob_db_db[32] = "mob_db";
char mob_db_re_db[32] = "mob_db_re";
char mob_db2_db[32] = "mob_db2";
char mob_skill_db_db[32] = "mob_skill_db";
char mob_skill_db_re_db[32] = "mob_skill_db_re";
char mob_skill_db2_db[32] = "mob_skill_db2";
char sales_db[32] = "sales";
char vendings_db[32] = "vendings";
char vending_items_db[32] = "vending_items";

// log database
char log_db_ip[32] = "127.0.0.1";
int log_db_port = 3306;
char log_db_id[32] = "ragnarok";
char log_db_pw[32] = "ragnarok";
char log_db_db[32] = "log";
Sql *logmysql_handle;

// DBMap declaration
static DBMap *id_db = NULL; // int id -> struct block_list*
static DBMap *pc_db = NULL; // int id -> struct map_session_data*
static DBMap *mobid_db = NULL; // int id -> struct mob_data*
static DBMap *bossid_db = NULL; // int id -> struct mob_data* (MVP db)
static DBMap *map_db = NULL; // unsigned int mapindex -> struct map_data*
static DBMap *nick_db = NULL; // int char_id -> struct charid2nick* (requested names of offline characters)
static DBMap *charid_db = NULL; // int char_id -> struct map_session_data*
static DBMap *regen_db = NULL; // int id -> struct block_list* (status_natural_heal processing)

static int map_users = 0;

#define BLOCK_SIZE 8
#define block_free_max 1048576
struct block_list *block_free[block_free_max];
static int block_free_count = 0, block_free_lock = 0;

#define BL_LIST_MAX 1048576
static struct block_list *bl_list[BL_LIST_MAX];
static int bl_list_count = 0;

#define MAP_MAX_MSG 1550
static char *msg_table[MAP_MAX_MSG]; // map Server messages

struct map_data map[MAX_MAP_PER_SERVER];
int map_num = 0;
int map_port = 0;

int autosave_interval = DEFAULT_AUTOSAVE_INTERVAL;
int minsave_interval = 100;
int16 save_settings = CHARSAVE_ALL;
bool agit_flag = false;
bool agit2_flag = false;
bool agit3_flag = false;
int night_flag = 0; // 0 = day, 1 = night [Yor]

#ifdef ADJUST_SKILL_DAMAGE
struct eri *map_skill_damage_ers = NULL;
#endif

struct charid_request {
	struct charid_request* next;
	int charid;// who want to be notified of the nick
};
struct charid2nick {
	char nick[NAME_LENGTH];
	struct charid_request* requests;// requests of notification on this nick
};

// This is the main header found at the very beginning of the map cache
struct map_cache_main_header {
	uint32 file_size;
	uint16 map_count;
};

// This is the header appended before every compressed map cells info in the map cache
struct map_cache_map_info {
	char name[MAP_NAME_LENGTH];
	int16 xs;
	int16 ys;
	int32 len;
};

char db_path[256] = "db";
char motd_txt[256] = "conf/motd.txt";
char help_txt[256] = "conf/help.txt";
char help2_txt[256] = "conf/help2.txt";
char charhelp_txt[256] = "conf/charhelp.txt";
char channel_conf[256] = "conf/channels.conf";

char wisp_server_name[NAME_LENGTH] = "Server"; // can be modified in char-server configuration file

struct s_map_default map_default;

int console = 0;
int enable_spy = 0; //To enable/disable @spy commands, which consume too much cpu time when sending packets. [Skotlex]
int enable_grf = 0;	//To enable/disable reading maps from GRF files, bypassing mapcache [blackhole89]

/*==========================================
 * server player count (of all mapservers)
 *------------------------------------------*/
void map_setusers(int users)
{
	map_users = users;
}

int map_getusers(void)
{
	return map_users;
}

/*==========================================
 * server player count (this mapserver only)
 *------------------------------------------*/
int map_usercount(void)
{
	return pc_db->size(pc_db);
}

//
// block�폜�̈��S���m��?��
//

/*==========================================
 * Attempt to free a map blocklist
 *------------------------------------------*/
int map_freeblock (struct block_list *bl)
{
	nullpo_retr(block_free_lock, bl);
	if (block_free_lock == 0 || block_free_count >= block_free_max) {
		aFree(bl);
		bl = NULL;
		if (block_free_count >= block_free_max)
			ShowWarning("map_freeblock: too many free block! %d %d\n", block_free_count, block_free_lock);
	} else
		block_free[block_free_count++] = bl;

	return block_free_lock;
}
/*==========================================
 * Lock blocklist, (prevent map_freeblock usage)
 *------------------------------------------*/
int map_freeblock_lock (void)
{
	return ++block_free_lock;
}

/*==========================================
 * Remove the lock on map_bl
 *------------------------------------------*/
int map_freeblock_unlock (void)
{
	if ((--block_free_lock) == 0) {
		int i;

		for (i = 0; i < block_free_count; i++) {
			aFree(block_free[i]);
			block_free[i] = NULL;
		}
		block_free_count = 0;
	} else if (block_free_lock < 0) {
		ShowError("map_freeblock_unlock: lock count < 0 !\n");
		block_free_lock = 0;
	}

	return block_free_lock;
}

// Timer function to check if there some remaining lock and remove them if so.
// Called each 1s
TIMER_FUNC(map_freeblock_timer)
{
	if (block_free_lock > 0) {
		ShowError("map_freeblock_timer: block_free_lock(%d) is invalid.\n", block_free_lock);
		block_free_lock = 1;
		map_freeblock_unlock();
	}

	return 0;
}

//
// blocklist
//
/*==========================================
 * Handling of map_bl[]
 * The address of bl_heal is set in bl->prev
 *------------------------------------------*/
static struct block_list bl_head;

#ifdef CELL_NOSTACK
/*==========================================
 * These pair of functions update the counter of how many objects
 * lie on a tile.
 *------------------------------------------*/
static void map_addblcell(struct block_list *bl)
{
	if( bl->m < 0 || bl->x < 0 || bl->x >= map[bl->m].xs || bl->y < 0 || bl->y >= map[bl->m].ys || !(bl->type&BL_CHAR) )
		return;
	map[bl->m].cell[bl->x + bl->y * map[bl->m].xs].cell_bl++;
	return;
}

static void map_delblcell(struct block_list *bl)
{
	if( bl->m < 0 || bl->x < 0 || bl->x >= map[bl->m].xs || bl->y < 0 || bl->y >= map[bl->m].ys || !(bl->type&BL_CHAR) )
		return;
	map[bl->m].cell[bl->x + bl->y * map[bl->m].xs].cell_bl--;
}
#endif

/*==========================================
 * Adds a block to the map.
 * Returns 0 on success, 1 on failure (illegal coordinates).
 *------------------------------------------*/
int map_addblock(struct block_list *bl)
{
	int16 m, x, y;
	int pos;

	nullpo_ret(bl);

	if( bl->prev != NULL ) {
		ShowError("map_addblock: bl->prev != NULL\n");
		return 1;
	}

	m = bl->m;
	x = bl->x;
	y = bl->y;
	if( m < 0 || m >= map_num ) {
		ShowError("map_addblock: invalid map id (%d), only %d are loaded.\n", m, map_num);
		return 1;
	}
	if( x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys ) {
		ShowError("map_addblock: out-of-bounds coordinates (\"%s\",%d,%d), map is %dx%d\n", map[m].name, x, y, map[m].xs, map[m].ys);
		return 1;
	}

	pos = x / BLOCK_SIZE + (y / BLOCK_SIZE) * map[m].bxs;

	if( bl->type == BL_MOB ) {
		bl->next = map[m].block_mob[pos];
		bl->prev = &bl_head;
		if( bl->next )
			bl->next->prev = bl;
		map[m].block_mob[pos] = bl;
	} else {
		bl->next = map[m].block[pos];
		bl->prev = &bl_head;
		if( bl->next )
			bl->next->prev = bl;
		map[m].block[pos] = bl;
	}

#ifdef CELL_NOSTACK
	map_addblcell(bl);
#endif

	return 0;
}

/*==========================================
 * Removes a block from the map.
 *------------------------------------------*/
int map_delblock(struct block_list *bl)
{
	int pos;
	nullpo_ret(bl);

	//Blocklist (2ways chainlist)
	if (bl->prev == NULL) {
		if (bl->next != NULL) //Can't delete block (already at the beginning of the chain)
			ShowError("map_delblock error : bl->next!=NULL\n");
		return 0;
	}

#ifdef CELL_NOSTACK
	map_delblcell(bl);
#endif
	
	pos = bl->x / BLOCK_SIZE + (bl->y / BLOCK_SIZE) * map[bl->m].bxs;

	if (bl->next)
		bl->next->prev = bl->prev;
	if (bl->prev == &bl_head) {
		//Since the head of the list, update the block_list map of []
		if (bl->type == BL_MOB)
			map[bl->m].block_mob[pos] = bl->next;
		else
			map[bl->m].block[pos] = bl->next;
	} else
		bl->prev->next = bl->next;
	bl->next = NULL;
	bl->prev = NULL;

	return 0;
}

/**
 * Moves a block a x/y target position. [Skotlex]
 * Pass flag as 1 to prevent doing skill_unit_move checks
 * (which are executed by default on BL_CHAR types)
 * @param bl : block(object) to move
 * @param x1 : new x position
 * @param y1 : new y position
 * @param tick : when this was scheduled
 * @return 0 : success, 1 : fail
 */
int map_moveblock(struct block_list *bl, int x1, int y1, unsigned int tick)
{
	struct status_change *sc = NULL;
	int x0, y0, moveblock;

	nullpo_ret(bl);

	x0 = bl->x;
	y0 = bl->y;
	moveblock = (x0 / BLOCK_SIZE != x1 / BLOCK_SIZE || y0 / BLOCK_SIZE != y1 / BLOCK_SIZE);

	if (!bl->prev) { //Block not in map, just update coordinates, but do naught else
		bl->x = x1;
		bl->y = y1;
		return 0;
	}

	if (bl->type&BL_CHAR) { //@TODO: Perhaps some outs of bounds checking should be placed here?
		skill_unit_move(bl, tick, 2);
		if ((sc = status_get_sc(bl)) && sc->count) { //At least one to cancel
			status_change_end(bl, SC_CLOSECONFINE, INVALID_TIMER);
			status_change_end(bl, SC_CLOSECONFINE2, INVALID_TIMER);
			status_change_end(bl, SC_TINDER_BREAKER, INVALID_TIMER);
			//status_change_end(bl, SC_BLADESTOP, INVALID_TIMER); //Won't stop when you are knocked away, go figure
			status_change_end(bl, SC_MAGICROD, INVALID_TIMER);
			status_change_end(bl, SC_SU_STOOP, INVALID_TIMER);
#ifdef RENEWAL //3x3 AoE ranged damage protection
			if (sc->data[SC_TATAMIGAESHI] && sc->data[SC_TATAMIGAESHI]->val2 > 0)
#endif
				status_change_end(bl, SC_TATAMIGAESHI, INVALID_TIMER);
			if (sc->data[SC_PROPERTYWALK] &&
				sc->data[SC_PROPERTYWALK]->val3 >= skill_get_maxcount(sc->data[SC_PROPERTYWALK]->val1, sc->data[SC_PROPERTYWALK]->val2))
				status_change_end(bl, SC_PROPERTYWALK, INVALID_TIMER);
		}
	} else if (bl->type == BL_NPC)
		npc_unsetcells((TBL_NPC *)bl);

	if (moveblock)
		map_delblock(bl);
#ifdef CELL_NOSTACK
	else
		map_delblcell(bl);
#endif

	bl->x = x1;
	bl->y = y1;

	if (moveblock) {
		if (map_addblock(bl))
			return 1;
	}
#ifdef CELL_NOSTACK
	else
		map_addblcell(bl);
#endif

	if (bl->type&BL_CHAR) {
		skill_unit_move(bl, tick, 3);
		if (bl->type == BL_PC && ((TBL_PC *)bl)->shadowform_id) { //Moving Target Shadow Form
			struct block_list *s_bl = map_id2bl(((TBL_PC *)bl)->shadowform_id);

			if (!check_distance_bl(bl, s_bl, 10))
				status_change_end(s_bl, SC__SHADOWFORM, INVALID_TIMER);
		}
		if (sc && sc->count) {
			if (sc->data[SC_DANCING])
				skill_unit_move_unit_group(skill_id2group(sc->data[SC_DANCING]->val2), bl->m, x1 - x0, y1 - y0);
			else {
				if (sc->data[SC_CLOAKING] && sc->data[SC_CLOAKING]->val1 < 3 && !skill_check_cloaking(bl, NULL))
					status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
				if (sc->data[SC_WARM])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_WARM]->val4), bl->m, x1 - x0, y1 - y0);
#ifdef RENEWAL
				if (sc->data[SC_TATAMIGAESHI])
					sc->data[SC_TATAMIGAESHI]->val2++;
#endif
				if (sc->data[SC_BANDING])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_BANDING]->val4), bl->m, x1 - x0, y1 - y0);
				if (sc->data[SC_NEUTRALBARRIER_MASTER])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_NEUTRALBARRIER_MASTER]->val2), bl->m, x1 - x0, y1 - y0);
				else if (sc->data[SC_STEALTHFIELD_MASTER])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_STEALTHFIELD_MASTER]->val2), bl->m, x1 - x0, y1 - y0);
				if (sc->data[SC__SHADOWFORM]) { //Moving Caster Shadow Form
					struct block_list *s_bl = map_id2bl(sc->data[SC__SHADOWFORM]->val2);

					if (!check_distance_bl(bl, s_bl, 10))
						status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
				}
				if (sc->data[SC_PROPERTYWALK] &&
					sc->data[SC_PROPERTYWALK]->val3 < skill_get_maxcount(sc->data[SC_PROPERTYWALK]->val1, sc->data[SC_PROPERTYWALK]->val2) &&
					map_find_skill_unit_oncell(bl, bl->x, bl->y, SO_ELECTRICWALK, NULL, 0) == NULL &&
					map_find_skill_unit_oncell(bl, bl->x, bl->y, SO_FIREWALK, NULL, 0) == NULL &&
					skill_unitsetting(bl, sc->data[SC_PROPERTYWALK]->val1, sc->data[SC_PROPERTYWALK]->val2, x0,  y0, 0))
					sc->data[SC_PROPERTYWALK]->val3++;
			}
			//Moving Guild Aura
			if (bl->type == BL_PC && ((TBL_PC *)bl)->state.gmaster_flag) {
				if (sc->data[SC_LEADERSHIP])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_LEADERSHIP]->val4), bl->m, x1 - x0, y1 - y0);
				if (sc->data[SC_GLORYWOUNDS])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_GLORYWOUNDS]->val4), bl->m, x1 - x0, y1 - y0);
				if (sc->data[SC_SOULCOLD])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_SOULCOLD]->val4), bl->m, x1 - x0, y1 - y0);
				if (sc->data[SC_HAWKEYES])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_HAWKEYES]->val4), bl->m, x1 - x0, y1 - y0);
			}
		}
	} else if (bl->type == BL_NPC)
		npc_setcells((TBL_NPC *)bl);

	return 0;
}
	
/*==========================================
 * Counts specified number of objects on given cell.
 * flag:
 *  0x1 - only count standing units
 *  0x2 - don't count invinsible units
 *------------------------------------------*/
int map_count_oncell(int16 m, int16 x, int16 y, int type, int flag)
{
	int bx,by;
	struct block_list *bl;
	int count = 0;

	if (x < 0 || y < 0 || (x >= map[m].xs) || (y >= map[m].ys))
		return 0;

	bx = x / BLOCK_SIZE;
	by = y / BLOCK_SIZE;

	if (type&~BL_MOB) {
		for (bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next) {
			if (bl->x == x && bl->y == y && bl->type&type) {
				if (flag&0x2) {
					struct map_session_data *sd = map_id2sd(bl->id);
					struct npc_data *nd = map_id2nd(bl->id);

					if (sd && pc_isinvisible(sd))
						continue;
					if (nd && (nd->class_ == JT_FAKENPC || nd->class_ == JT_HIDDEN_WARP_NPC))
						continue;
				}
				if (flag&0x1) {
					struct unit_data *ud = unit_bl2ud(bl);

					if (ud && ud->walktimer != INVALID_TIMER)
						continue;
				}
				count++;
			}
		}
	}

	if (type&BL_MOB) {
		for (bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next) {
			if (bl->x == x && bl->y == y) {
				if (flag&0x2) {
					struct map_session_data *sd = map_id2sd(bl->id);

					if (sd && pc_isinvisible(sd))
						continue;
				}
				if (flag&0x1) {
					struct unit_data *ud = unit_bl2ud(bl);

					if (ud && ud->walktimer != INVALID_TIMER)
						continue;
				}
				count++;
			}
		}
	}

	return count;
}

/**
 * Looks for a skill unit on a given cell
 * flag&1: runs battle_check_target check based on unit->group->target_flag
 */
struct skill_unit *map_find_skill_unit_oncell(struct block_list *target, int16 x, int16 y, uint16 skill_id, struct skill_unit *out_unit, int flag) {
	int16 m, bx, by;
	struct block_list *bl;
	struct skill_unit *unit;
	m = target->m;

	if( x < 0 || y < 0 || (x >= map[m].xs) || (y >= map[m].ys) )
		return NULL;

	bx = x / BLOCK_SIZE;
	by = y / BLOCK_SIZE;

	for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
		if( bl->x != x || bl->y != y || bl->type != BL_SKILL )
			continue;
		unit = (struct skill_unit *) bl;
		if( unit == out_unit || !unit->alive || !unit->group || unit->group->skill_id != skill_id )
			continue;
		if( !(flag&1) || battle_check_target(&unit->bl,target,unit->group->target_flag) > 0 )
			return unit;
	}
	return NULL;
}

/*==========================================
 * Adapted from foreachinarea for an easier invocation. [Skotlex]
 *------------------------------------------*/
int map_foreachinrangeV(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int type, va_list ap, bool wall_check)
{
	int bx, by, m;
	int returnCount = 0; //Total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount = bl_list_count, i;
	int x0, x1, y0, y1;
	va_list ap_copy;

	m = center->m;
	if( m < 0 )
		return 0;

	x0 = i16max(center->x - range, 0);
	y0 = i16max(center->y - range, 0);
	x1 = i16min(center->x + range, map[m].xs - 1);
	y1 = i16min(center->y + range, map[m].ys - 1);

	if( type&~BL_MOB ) {
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ ) {
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->type&type
						&& bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& (!wall_check || path_search_long(NULL, center->m, center->x, center->y, bl->x, bl->y, CELL_CHKWALL))
						&& bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count++] = bl;
				}
			}
		}
	}

	if( type&BL_MOB ) {
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ ) {
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& (!wall_check || path_search_long(NULL, center->m, center->x, center->y, bl->x, bl->y, CELL_CHKWALL))
						&& bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count++] = bl;
				}
			}
		}
	}

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_foreachinrange: block count too many!\n");

	map_freeblock_lock();

	for( i = blockcount; i < bl_list_count; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion
			va_copy(ap_copy, ap);
			returnCount += func(bl_list[i], ap_copy);
			va_end(ap_copy);
		}
	}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]
}

int map_foreachinrange(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int type, ...)
{
	int returnCount = 0;
	va_list ap;

	va_start(ap, type);
	returnCount = map_foreachinrangeV(func, center, range, type, ap, (battle_config.skill_wall_check > 0));
	va_end(ap);
	return returnCount;
}

int map_foreachinallrange(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int type, ...)
{
	int returnCount = 0;
	va_list ap;

	va_start(ap, type);
	returnCount = map_foreachinrangeV(func, center, range, type, ap, false);
	va_end(ap);
	return returnCount;
}

/*==========================================
 * Same as foreachinrange, but there must be a shoot-able range between center and target to be counted in. [Skotlex]
 *------------------------------------------*/
int map_foreachinshootrange(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int type, ...)
{
	int returnCount = 0;
	va_list ap;

	va_start(ap, type);
	returnCount = map_foreachinrangeV(func, center, range, type, ap, true);
	va_end(ap);
	return returnCount;
}

/**
 * range = map m (x0,y0)-(x1,y1)
 * Apply *func with ... arguments for the range.
 * @author [Playtester]
 * @param m: ID of map
 * @param x0: West end of area
 * @param y0: South end of area
 * @param x1: East end of area
 * @param y1: North end of area
 * @param type: Type of bl to search for
 */
int map_foreachinareaV(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, va_list ap, bool wall_check)
{
	int bx, by, cx = 0, cy = 0;
	int returnCount = 0; //Total sum of returned values of func()
	struct block_list *bl;
	int blockcount = bl_list_count, i;
	va_list ap_copy;

	if( m < 0 || m >= map_num )
		return 0;

	if( x1 < x0 )
		swap(x0, x1);

	if( y1 < y0 )
		swap(y0, y1);

	x0 = i16max(x0, 0);
	y0 = i16max(y0, 0);
	x1 = i16min(x1, map[m].xs - 1);
	y1 = i16min(y1, map[m].ys - 1);

	if( wall_check ) {
		cx = x0 + (x1 - x0) / 2;
		cy = y0 + (y1 - y0) / 2;
	}

	if( type&~BL_MOB ) {
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ ) {
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->type&type
						&& bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
						&& (!wall_check || path_search_long(NULL, m, cx, cy, bl->x, bl->y, CELL_CHKWALL))
						&& bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count++] = bl;
				}
			}
		}
	}

	if( type&BL_MOB ) {
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ ) {
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
						&& (!wall_check || path_search_long(NULL, m, cx, cy, bl->x, bl->y, CELL_CHKWALL))
						&& bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count++] = bl;
				}
			}
		}
	}

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_foreachinarea: block count too many!\n");

	map_freeblock_lock();

	for( i = blockcount; i < bl_list_count; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion
			va_copy(ap_copy, ap);
			returnCount += func(bl_list[i], ap_copy);
			va_end(ap_copy);
		}
	}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;
}

int map_foreachinallarea(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, ...)
{
	int returnCount = 0;
	va_list ap;

	va_start(ap, type);
	returnCount = map_foreachinareaV(func, m, x0, y0, x1, y1, type, ap, false);
	va_end(ap);
	return returnCount;
}

int map_foreachinshootarea(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, ...)
{
	int returnCount = 0;
	va_list ap;

	va_start(ap, type);
	returnCount = map_foreachinareaV(func, m, x0, y0, x1, y1, type, ap, true);
	va_end(ap);
	return returnCount;
}

int map_foreachinarea(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, ...)
{
	int returnCount = 0;
	va_list ap;

	va_start(ap, type);
	returnCount = map_foreachinareaV(func, m, x0, y0, x1, y1, type, ap, (battle_config.skill_wall_check > 0));
	va_end(ap);
	return returnCount;
}

/*==========================================
 * Adapted from forcountinarea for an easier invocation. [pakpil]
 *------------------------------------------*/
int map_forcountinrange(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int count, int type, ...)
{
	int bx, by, m;
	int returnCount = 0; //Total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount = bl_list_count, i;
	int x0, x1, y0, y1;
	va_list ap;

	m = center->m;
	x0 = i16max(center->x - range, 0);
	y0 = i16max(center->y - range, 0);
	x1 = i16min(center->x + range, map[m].xs - 1);
	y1 = i16min(center->y + range, map[m].ys - 1);

	if( type&~BL_MOB ) {
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ ) {
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->type&type
						&& bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count++] = bl;
				}
			}
		}
	}

	if( type&BL_MOB ) {
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ ) {
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ ){
				for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count++] = bl;
				}
			}
		}
	}

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_forcountinrange: block count too many!\n");

	map_freeblock_lock();

	for( i = blockcount; i < bl_list_count; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
			if( count && returnCount >= count )
				break;
		}
	}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]
}

int map_forcountinarea(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int count, int type, ...)
{
	int bx, by;
	int returnCount = 0; //Total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount = bl_list_count, i;
	va_list ap;

	if( m < 0 )
		return 0;

	if( x1 < x0 )
		swap(x0, x1);

	if( y1 < y0 )
		swap(y0, y1);

	x0 = i16max(x0, 0);
	y0 = i16max(y0, 0);
	x1 = i16min(x1, map[m].xs - 1);
	y1 = i16min(y1, map[m].ys - 1);

	if( type&~BL_MOB )
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ )
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ )
				for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next )
					if( bl->type&type && bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1 && bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count++] = bl;

	if( type&BL_MOB )
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ )
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ )
				for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next )
					if( bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1 && bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count++] = bl;

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_forcountinarea: block count too many!\n");

	map_freeblock_lock();

	for( i = blockcount; i < bl_list_count; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
			if( count && returnCount >= count )
				break;
		}
	}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]
}

/*==========================================
 * For what I get
 * Move bl and do func* with va_list while moving.
 * Movement is set by dx dy which are distance in x and y
 *------------------------------------------*/
int map_foreachinmovearea(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int16 dx, int16 dy, int type, ...)
{
	int bx, by, m;
	int returnCount = 0; //Total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount = bl_list_count, i;
	int16 x0, x1, y0, y1;
	va_list ap;

	if( !range )
		return 0;

	if( !dx && !dy )
		return 0; //No movement

	m = center->m;
	x0 = center->x - range;
	x1 = center->x + range;
	y0 = center->y - range;
	y1 = center->y + range;

	if( x1 < x0 )
		swap(x0, x1);

	if( y1 < y0 )
		swap(y0, y1);

	if( dx == 0 || dy == 0 ) {
		//Movement along one axis only
		if( dx == 0 ) {
			if( dy < 0 ) //Moving south
				y0 = y1 + dy + 1;
			else //North
				y1 = y0 + dy - 1;
		} else { //dy == 0
			if( dx < 0 ) //West
				x0 = x1 + dx + 1;
			else //East
				x1 = x0 + dx - 1;
		}

		x0 = i16max(x0, 0);
		y0 = i16max(y0, 0);
		x1 = i16min(x1, map[m].xs - 1);
		y1 = i16min(y1, map[m].ys - 1);

		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ ) {
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ ) {
				if( type&~BL_MOB ) {
					for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
						if( bl->type&type &&
							bl->x >= x0 && bl->x <= x1 &&
							bl->y >= y0 && bl->y <= y1 &&
							bl_list_count < BL_LIST_MAX )
							bl_list[bl_list_count++] = bl;
					}
				}
				if( type&BL_MOB ) {
					for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
						if( bl->x >= x0 && bl->x <= x1 &&
							bl->y >= y0 && bl->y <= y1 &&
							bl_list_count < BL_LIST_MAX )
							bl_list[bl_list_count++] = bl;
					}
				}
			}
		}
	} else { // Diagonal movement
		x0 = i16max(x0, 0);
		y0 = i16max(y0, 0);
		x1 = i16min(x1, map[m].xs - 1);
		y1 = i16min(y1, map[m].ys - 1);

		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++ ) {
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++ ) {
				if( type & ~BL_MOB ) {
					for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
						if( bl->type&type &&
							bl->x >= x0 && bl->x <= x1 &&
							bl->y >= y0 && bl->y <= y1 &&
							bl_list_count < BL_LIST_MAX )
						if( (dx > 0 && bl->x < x0 + dx) ||
							(dx < 0 && bl->x > x1 + dx) ||
							(dy > 0 && bl->y < y0 + dy) ||
							(dy < 0 && bl->y > y1 + dy) )
							bl_list[bl_list_count++] = bl;
					}
				}
				if( type&BL_MOB ) {
					for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
						if( bl->x >= x0 && bl->x <= x1 &&
							bl->y >= y0 && bl->y <= y1 &&
							bl_list_count < BL_LIST_MAX)
						if( (dx > 0 && bl->x < x0 + dx) ||
							(dx < 0 && bl->x > x1 + dx) ||
							(dy > 0 && bl->y < y0 + dy) ||
							(dy < 0 && bl->y > y1 + dy) )
							bl_list[bl_list_count++] = bl;
					}
				}
			}
		}
	}

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_foreachinmovearea: block count too many!\n");

	map_freeblock_lock(); // Prohibit the release from memory

	for( i = blockcount; i < bl_list_count; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}
	}

	map_freeblock_unlock();	// Allow Free

	bl_list_count = blockcount;
	return returnCount;
}

// -- moonsoul	(added map_foreachincell which is a rework of map_foreachinallarea but
//			 which only checks the exact single x/y passed to it rather than an
//			 area radius - may be more useful in some instances)
//
int map_foreachincell(int (*func)(struct block_list *, va_list), int16 m, int16 x, int16 y, int type, ...)
{
	int bx, by;
	int returnCount = 0; //Total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount = bl_list_count, i;
	va_list ap;

	if( x < 0 || y < 0 || x >= map[m].xs || y >= map[m].ys )
		return 0;

	by = y / BLOCK_SIZE;
	bx = x / BLOCK_SIZE;

	if( type&~BL_MOB )
		for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next )
			if( bl->type&type && bl->x == x && bl->y == y && bl_list_count < BL_LIST_MAX )
				bl_list[bl_list_count++] = bl;

	if( type&BL_MOB )
		for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next )
			if( bl->x == x && bl->y == y && bl_list_count < BL_LIST_MAX)
				bl_list[bl_list_count++] = bl;

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_foreachincell: block count too many!\n");

	map_freeblock_lock();

	for( i = blockcount; i < bl_list_count; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}
	}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;
}

/*============================================================
* For checking a path between two points (x0, y0) and (x1, y1)
*------------------------------------------------------------*/
int map_foreachinpath(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int16 range, int length, int type, ...)
{
	int returnCount = 0; //Total sum of returned values of func() [Skotlex]
//////////////////////////////////////////////////////////////
//
// Sharp Shooting 3 [Skotlex]
//
//////////////////////////////////////////////////////////////
// Problem:
// Same as Sharp Shooting 1. Hits all targets within range of
// the line.
// (t1,t2 t3 and t4 get hit)
//
//     target 1
//      x t4
//     t2
// t3 x
//   x
//  S
//////////////////////////////////////////////////////////////
// Methodology:
// My trigonometrics and math are a little rusty... so the approach I am writing
// here is basically do a double for to check for all targets in the square that
// contains the initial and final positions (area range increased to match the
// radius given), then for each object to test, calculate the distance to the
// path and include it if the range fits and the target is in the line (0<k<1,
// as they call it).
// The implementation I took as reference is found at
// http://astronomy.swin.edu.au/~pbourke/geometry/pointline/
// (they have a link to a C implementation, too)
// This approach is a lot like #2 commented on this function, which I have no
// idea why it was commented. I won't use doubles/floats, but pure int math for
// speed purposes. The range considered is always the same no matter how
// close/far the target is because that's how SharpShooting works currently in
// kRO

	//Generic map_foreach* variables
	int i, blockcount = bl_list_count;
	struct block_list *bl;
	int bx, by;
	//method specific variables
	int magnitude2, len_limit; //The square of the magnitude
	int k, xi, yi, xu, yu;
	int16 mx0 = x0, mx1 = x1, my0 = y0, my1 = y1;
	va_list ap;

	//Avoid needless calculations by not getting the sqrt right away
	#define MAGNITUDE2(x0, y0, x1, y1) (((x1) - (x0)) * ((x1) - (x0)) + ((y1) - (y0)) * ((y1) - (y0)))

	if( m < 0 )
		return 0;

	len_limit = magnitude2 = MAGNITUDE2(x0, y0, x1, y1);
	if( magnitude2 < 1 ) //Same begin and ending point, can't trace path
		return 0;

	if( length ) { //Adjust final position to fit in the given area
		//@TODO: Find an alternate method which does not requires a square root calculation
		k = (int)sqrt((float)magnitude2);
		mx1 = x0 + (x1 - x0) * length / k;
		my1 = y0 + (y1 - y0) * length / k;
		len_limit = MAGNITUDE2(x0, y0, mx1, my1);
	}

	//Expand target area to cover range
	if( mx0 > mx1 ) {
		mx0 += range;
		mx1 -= range;
	} else {
		mx0 -= range;
		mx1 += range;
	}

	if( my0 > my1 ) {
		my0 += range;
		my1 -= range;
	} else {
		my0 -= range;
		my1 += range;
	}

	//The two fors assume mx0 < mx1 && my0 < my1
	if( mx0 > mx1 )
		swap(mx0, mx1);

	if( my0 > my1 )
		swap(my0, my1);

	mx0 = i16max(mx0, 0);
	my0 = i16max(my0, 0);
	mx1 = i16min(mx1, map[ m ].xs - 1);
	my1 = i16min(my1, map[ m ].ys - 1);

	range *= (range<<8); //Values are shifted later on for higher precision using int math.

	if( type&~BL_MOB ) {
		for( by = my0 / BLOCK_SIZE; by <= my1 / BLOCK_SIZE; by++ ) {
			for( bx = mx0 / BLOCK_SIZE; bx <= mx1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->prev && bl->type&type && bl_list_count < BL_LIST_MAX ) {
						xi = bl->x;
						yi = bl->y;

						k = (xi - x0) * (x1 - x0) + (yi - y0) * (y1 - y0);

						if( k < 0 || k > len_limit ) //Since more skills use this, check for ending point as well
							continue;

						if( k > magnitude2 && !path_search_long(NULL, m, x0, y0, xi, yi, CELL_CHKWALL) )
							continue; //Targets beyond the initial ending point need the wall check

						//All these shifts are to increase the precision of the intersection point and distance considering how it's int math
						k  = (k<<4) / magnitude2; //k will be between 1~16 instead of 0~1
						xi <<= 4;
						yi <<= 4;
						xu = (x0<<4) + k * (x1 - x0);
						yu = (y0<<4) + k * (y1 - y0);
						k  = MAGNITUDE2(xi, yi, xu, yu);

						//If all dot coordinates were <<4 the square of the magnitude is <<8
						if( k > range )
							continue;

						bl_list[bl_list_count++] = bl;
					}
				}
			}
		}
	}

	 if( type&BL_MOB ) {
		for( by = my0 / BLOCK_SIZE; by <= my1 / BLOCK_SIZE; by++ ) {
			for( bx = mx0 / BLOCK_SIZE; bx <= mx1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->prev && bl_list_count < BL_LIST_MAX ) {
						xi = bl->x;
						yi = bl->y;
						k = (xi - x0) * (x1 - x0) + (yi - y0) * (y1 - y0);

						if( k < 0 || k > len_limit )
							continue;

						if( k > magnitude2 && !path_search_long(NULL, m, x0, y0, xi, yi, CELL_CHKWALL) )
							continue; //Targets beyond the initial ending point need the wall check

						k  = (k<<4) / magnitude2; //k will be between 1~16 instead of 0~1
						xi <<= 4;
						yi <<= 4;
						xu = (x0<<4) + k * (x1 - x0);
						yu = (y0<<4) + k * (y1 - y0);
						k  = MAGNITUDE2(xi, yi, xu, yu);

						//If all dot coordinates were <<4 the square of the magnitude is <<8
						if( k > range )
							continue;

						bl_list[bl_list_count++] = bl;
					}
				}
			}
		}
	}

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_foreachinpath: block count too many!\n");

	map_freeblock_lock();

	for( i = blockcount; i < bl_list_count; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}
	}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]

}

/**
 * Calls the given function for every object of a type that is on a path.
 * The path goes into one of the eight directions and the direction is determined by the given coordinates.
 * The path has a length, a width and an offset.
 * The cost for diagonal movement is the same as for horizontal/vertical movement.
 * @author [Playtester]
 * @param m: ID of map
 * @param x0: Start X
 * @param y0: Start Y
 * @param x1: X to calculate direction against
 * @param y1: Y to calculate direction against
 * @param range: Determines width of the path (width = range*2+1 cells)
 * @param length: Length of the path
 * @param offset: Moves the whole path, half-length for diagonal paths
 * @param type: Type of bl to search for
 */
int map_foreachindir(int(*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int16 range, int length, int offset, int type, ...)
{
	int returnCount = 0; //Total sum of returned values of func()
	int i, blockcount = bl_list_count;
	struct block_list *bl;
	int bx, by, rx, ry;
	int16 mx0, mx1, my0, my1;
	uint8 dir = map_calc_dir_xy(x0, y0, x1, y1, 6);
	short dx = dirx[dir];
	short dy = diry[dir];
	va_list ap;

	if( m < 0 )
		return 0;

	if( range < 0 )
		return 0;

	if( length < 1 )
		return 0;

	if( offset < 0 )
		return 0;

	if( offset && (dir%2) ) { //Special offset handling for diagonal paths
		offset = 2 * offset - 1; //So that diagonal paths can attach to each other, we have to work with half-tile offsets
		length++; //To get the half-tiles we need to increase length by one
	}

	//Get area that needs to be checked
	mx0 = x0 + dx * (offset / ((dir%2) + 1));
	my0 = y0 + dy * (offset / ((dir%2) + 1));
	mx1 = x0 + dx * (offset / ((dir%2) + 1) + length - 1);
	my1 = y0 + dy * (offset / ((dir%2) + 1) + length - 1);

	//The following assumes mx0 < mx1 && my0 < my1
	if( mx0 > mx1 )
		swap(mx0, mx1);

	if( my0 > my1 )
		swap(my0, my1);

	//Apply width to the path by turning 90 degrees
	mx0 -= abs(range * dirx[(dir + 2)%8]);
	my0 -= abs(range * diry[(dir + 2)%8]);
	mx1 += abs(range * dirx[(dir + 2)%8]);
	my1 += abs(range * diry[(dir + 2)%8]);

	mx0 = i16max(mx0, 0);
	my0 = i16max(my0, 0);
	mx1 = i16min(mx1, map[m].xs - 1);
	my1 = i16min(my1, map[m].ys - 1);

	if( type&~BL_MOB ) {
		for( by = my0 / BLOCK_SIZE; by <= my1 / BLOCK_SIZE; by++ ) {
			for( bx = mx0 / BLOCK_SIZE; bx <= mx1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->prev && bl->type&type && bl_list_count < BL_LIST_MAX ) {
						if( bl->x < mx0 || bl->x > mx1 || bl->y < my0 || bl->y > my1 )
							continue; //Check if inside search area
						//What matters now is the relative x and y from the start point
						rx = (bl->x - x0);
						ry = (bl->y - y0);
						if( rx == 0 && ry == 0 )
							continue; //Do not hit source cell
						//This turns it so that the area that is hit is always with positive rx and ry
						rx *= dx;
						ry *= dy;
						if( dir%2 ) { //These checks only need to be done for diagonal paths
							if( (rx + ry < offset) || (rx + ry > 2 * (length + (offset / 2) - 1)) )
								continue; //Check for length
							if( abs(rx - ry) > 2 * range )
								continue; //Check for width
						}
						if( !path_search_long(NULL, m, x0, y0, bl->x, bl->y, CELL_CHKWALL) )
							continue; //Everything else ok, check for line of sight from source
						bl_list[bl_list_count++] = bl; //All checks passed, add to list
					}
				}
			}
		}
	}

	if( type&BL_MOB ) {
		for( by = my0 / BLOCK_SIZE; by <= my1 / BLOCK_SIZE; by++ ) {
			for( bx = mx0 / BLOCK_SIZE; bx <= mx1 / BLOCK_SIZE; bx++ ) {
				for( bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next ) {
					if( bl->prev && bl_list_count < BL_LIST_MAX ) {
						if( bl->x < mx0 || bl->x > mx1 || bl->y < my0 || bl->y > my1 )
							continue; //Check if inside search area
						//What matters now is the relative x and y from the start point
						rx = (bl->x - x0);
						ry = (bl->y - y0);
						if( rx == 0 && ry == 0 )
							continue; //Do not hit source cell
						//This turns it so that the area that is hit is always with positive rx and ry
						rx *= dx;
						ry *= dy;
						if( dir%2 ) { //These checks only need to be done for diagonal paths
							if( (rx + ry < offset) || (rx + ry > 2 * (length + (offset / 2) - 1)) )
								continue; //Check for length
							if( abs(rx - ry) > 2 * range )
								continue; //Check for width
						}
						if( !path_search_long(NULL, m, x0, y0, bl->x, bl->y, CELL_CHKWALL) )
							continue; //Everything else ok, check for line of sight from source
						bl_list[bl_list_count++] = bl; //All checks passed, add to list
					}
				}
			}
		}
	}

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_foreachindir: block count too many!\n");

	map_freeblock_lock();

	for( i = blockcount; i < bl_list_count; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion.
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}
	}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;
}

// Copy of map_foreachincell, but applied to the whole map. [Skotlex]
int map_foreachinmap(int (*func)(struct block_list*, va_list), int16 m, int type,...)
{
	int b, bsize;
	int returnCount = 0; //Total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount = bl_list_count, i;
	va_list ap;

	bsize = map[m].bxs * map[m].bys;

	if( type&~BL_MOB )
		for( b = 0; b < bsize; b++ )
			for( bl = map[m].block[b]; bl != NULL; bl = bl->next )
				if( bl->type&type && bl_list_count < BL_LIST_MAX )
					bl_list[bl_list_count++] = bl;

	if( type&BL_MOB )
		for( b = 0; b < bsize; b++ )
			for( bl = map[m].block_mob[b]; bl != NULL; bl = bl->next )
				if( bl_list_count < BL_LIST_MAX )
					bl_list[bl_list_count++] = bl;

	if( bl_list_count >= BL_LIST_MAX )
		ShowWarning("map_foreachinmap: block count too many!\n");

	map_freeblock_lock();

	for( i = blockcount; i < bl_list_count ; i++ ) {
		if( bl_list[i]->prev ) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}
	}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;
}


/// Generates a new flooritem object id from the interval [MIN_FLOORITEM, MAX_FLOORITEM).
/// Used for floor items, skill units and chatroom objects.
/// @return The new object id
int map_get_new_object_id(void)
{
	static int last_object_id = MIN_FLOORITEM - 1;
	int i;

	// find a free id
	i = last_object_id + 1;
	while( i != last_object_id ) {
		if( i == MAX_FLOORITEM )
			i = MIN_FLOORITEM;

		if( !idb_exists(id_db, i) )
			break;

		++i;
	}

	if( i == last_object_id ) {
		ShowError("map_addobject: no free object id!\n");
		return 0;
	}

	// update cursor
	last_object_id = i;

	return i;
}

/*==========================================
 * Timered fonction to clear the floor (remove remaining item)
 * Called each flooritem_lifetime ms
 *------------------------------------------*/
TIMER_FUNC(map_clearflooritem_timer)
{
	struct flooritem_data *fitem = (struct flooritem_data *)idb_get(id_db, id);
	
	if (fitem == NULL || fitem->bl.type != BL_ITEM || (fitem->cleartimer != tid)) {
		ShowError("map_clearflooritem_timer : error\n");
		return 1;
	}
	
	if (search_petDB_index(fitem->item.nameid, PET_EGG) >= 0)
		intif_delete_petdata(MakeDWord(fitem->item.card[1], fitem->item.card[2]));
		
	clif_clearflooritem(fitem, 0);
	map_deliddb(&fitem->bl);
	map_delblock(&fitem->bl);
	map_freeblock(&fitem->bl);
	return 0;
}

/*
 * Clears a single bl item out of the map.
 */
void map_clearflooritem(struct block_list *bl) {
	struct flooritem_data *fitem = (struct flooritem_data *)bl;

	if( fitem->cleartimer )
		delete_timer(fitem->cleartimer,map_clearflooritem_timer);

	clif_clearflooritem(fitem,0);
	map_deliddb(&fitem->bl);
	map_delblock(&fitem->bl);
	map_freeblock(&fitem->bl);
}

/*==========================================
 * (m,x,y) locates a random available free cell around the given coordinates
 * to place an BL_ITEM object. Scan area is 9x9, returns 1 on success.
 * x and y are modified with the target cell when successful.
 *------------------------------------------*/
int map_searchrandfreecell(int16 m,int16 *x,int16 *y,int stack) {
	int free_cell,i,j;
	int free_cells[9][2];

	for(free_cell = 0, i = -1; i <= 1; i++) {
		if(i + *y < 0 || i + *y >= map[m].ys)
			continue;
		for(j = -1; j <= 1; j++) {
			if(j + *x < 0 || j + *x >= map[m].xs)
				continue;
			if(map_getcell(m,j + *x,i + *y,CELL_CHKNOPASS) && !map_getcell(m,j + *x,i + *y,CELL_CHKICEWALL))
				continue;
			//Avoid item stacking to prevent against exploits [Skotlex]
			if(stack && map_count_oncell(m,j + *x,i + *y,BL_ITEM,0) > stack)
				continue;
			free_cells[free_cell][0] = j + *x;
			free_cells[free_cell++][1] = i + *y;
		}
	}
	if(free_cell == 0)
		return 0;
	free_cell = rnd()%free_cell;
	*x = free_cells[free_cell][0];
	*y = free_cells[free_cell][1];
	return 1;
}


static int map_count_sub(struct block_list *bl,va_list ap)
{
	return 1;
}

/*==========================================
 * Locates a random spare cell around the object given, using range as max
 * distance from that spot. Used for warping functions. Use range < 0 for
 * whole map range.
 * Returns 1 on success. when it fails and src is available, x/y are set to src's
 * src can be null as long as flag&1
 * when ~flag&1, m is not needed.
 * Flag values:
 * &1 = random cell must be around given m,x,y, not around src
 * &2 = the target should be able to walk to the target tile.
 * &4 = there shouldn't be any players around the target tile (use the no_spawn_on_player setting)
 *------------------------------------------*/
int map_search_freecell(struct block_list *src, int16 m, int16 *x, int16 *y, int16 rx, int16 ry, int flag)
{
	int tries, spawn = 0;
	int bx, by;
	int rx2 = 2 * rx + 1;
	int ry2 = 2 * ry + 1;

	if (!src && (!(flag&1) || flag&2)) {
		ShowDebug("map_search_freecell: Incorrect usage! When src is NULL, flag has to be &1 and can't have &2\n");
		return 0;
	}

	if (flag&1) {
		bx = *x;
		by = *y;
	} else {
		bx = src->x;
		by = src->y;
		m = src->m;
	}
	if (!rx && !ry) { //No range? Return the target cell then
		*x = bx;
		*y = by;
		return map_getcell(m,*x,*y,CELL_CHKREACH);
	}
	
	if (rx >= 0 && ry >= 0) {
		tries = rx2 * ry2;
		if (tries > 100)
			tries = 100;
	} else {
		tries = map[m].xs * map[m].ys;
		if (tries > 500)
			tries = 500;
	}
	
	while (tries--) {
		*x = (rx >= 0) ? (rnd()%rx2 - rx + bx) : (rnd()%(map[m].xs - 2) + 1);
		*y = (ry >= 0) ? (rnd()%ry2 - ry + by) : (rnd()%(map[m].ys - 2) + 1);

		if (*x == bx && *y == by)
			continue; //Avoid picking the same target tile

		if (map_getcell(m,*x,*y,CELL_CHKREACH)) {
			if (flag&2 && !unit_can_reach_pos(src,*x,*y,1))
				continue;
			if (flag&4) {
				if (spawn >= 100)
					return 0; //Limit of retries reached
				if (spawn++ < battle_config.no_spawn_on_player &&
					map_foreachinallarea(map_count_sub,m,*x - AREA_SIZE,*y - AREA_SIZE,*x + AREA_SIZE,*y + AREA_SIZE,BL_PC))
				continue;
			}
			return 1;
		}
	}
	*x = bx;
	*y = by;
	return 0;
}

/**
 * Locates the closest, walkable cell with no blocks of a certain type on it
 * Returns true on success and sets x and y to cell found.
 * Otherwise returns false and x and y are not changed.
 * type: Types of block to count
 * flag: 
 *  0x1 - only count standing units
 */
bool map_closest_freecell(int16 m, int16 *x, int16 *y, int type, int flag)
{
	uint8 dir = 6;
	int16 tx = *x;
	int16 ty = *y;
	int costrange = 10;

	if(!map_count_oncell(m,tx,ty,type,flag))
		return true; //Current cell is free

	//Algorithm only works up to costrange of 34
	while(costrange <= 34) {
		short dx = dirx[dir];
		short dy = diry[dir];

		if(dir%2 == 0 && costrange%MOVE_COST == 0) { //Linear search
			tx = *x + dx * (costrange / MOVE_COST);
			ty = *y + dy * (costrange / MOVE_COST);
			if(!map_count_oncell(m,tx,ty,type,flag) && map_getcell(m,tx,ty,CELL_CHKPASS)) {
				*x = tx;
				*y = ty;
				return true;
			}
		} else if(dir%2 == 1 && costrange%MOVE_DIAGONAL_COST == 0) { //Full diagonal search
			tx = *x + dx * (costrange / MOVE_DIAGONAL_COST);
			ty = *y + dy * (costrange / MOVE_DIAGONAL_COST);
			if(!map_count_oncell(m,tx,ty,type,flag) && map_getcell(m,tx,ty,CELL_CHKPASS)) {
				*x = tx;
				*y = ty;
				return true;
			}
		} else if(dir%2 == 1 && costrange%MOVE_COST == 4) { //One cell diagonal, rest linear (@TODO: Find a better algorithm for this)
			tx = *x + dx * ((dir%4 == 3) ? (costrange / MOVE_COST) : 1);
			ty = *y + dy * ((dir%4 == 1) ? (costrange / MOVE_COST) : 1);
			if(!map_count_oncell(m,tx,ty,type,flag) && map_getcell(m,tx,ty,CELL_CHKPASS)) {
				*x = tx;
				*y = ty;
				return true;
			}
			tx = *x + dx * ((dir%4 == 1) ? (costrange / MOVE_COST) : 1);
			ty = *y + dy * ((dir%4 == 3) ? (costrange / MOVE_COST) : 1);
			if(!map_count_oncell(m,tx,ty,type,flag) && map_getcell(m,tx,ty,CELL_CHKPASS)) {
				*x = tx;
				*y = ty;
				return true;
			}
		}

		//Get next direction
		if(dir == 5) { //Diagonal search complete, repeat with higher cost range
			if(costrange == 14)
				costrange += 6;
			else if(costrange == 28 || costrange >= 38)
				costrange += 2;
			else
				costrange += 4;
			dir = 6;
		} else if(dir == 4) //Linear search complete, switch to diagonal directions
			dir = 7;
		else
			dir = (dir + 2)%8;
	}

	return false;
}

/**
 * Add an item in floor to location (m,x,y) and add restriction for those who could pickup later
 * NB : If charids are null their no restriction for pickup
 * @param item : item attributes
 * @param amount : items quantity
 * @param m : mapid
 * @param x : x coordinates
 * @param y : y coordinates
 * @param first_charid : 1st player that could loot the item (only charid that could loot for first_get_tick duration)
 * @param second_charid :  2nd player that could loot the item (2nd charid that could loot for second_get_charid duration)
 * @param third_charid : 3rd player that could loot the item (3rd charid that could loot for third_get_charid duration)
 * @param flag: &1 MVP item. &2 do stacking check. &4 bypass droppable check.
 * @param mob_id: Monster ID if dropped by monster
 * @return 0:failure, x:item_gid [MIN_FLOORITEM;MAX_FLOORITEM]==[2;START_ACCOUNT_NUM]
 */
int map_addflooritem(struct item *item, int amount, int16 m, int16 x, int16 y, int first_charid, int second_charid, int third_charid, int flags, unsigned short mob_id)
{
	int r;
	struct flooritem_data *fitem = NULL;

	nullpo_ret(item);

	if(!(flags&4) && battle_config.item_onfloor && (itemdb_traderight(item->nameid)&1))
		return 0; //Can't be dropped

	if(!map_searchrandfreecell(m, &x, &y, flags&2?1:0))
		return 0;

	r = rnd();

	CREATE(fitem, struct flooritem_data, 1);
	fitem->bl.type = BL_ITEM;
	fitem->bl.prev = fitem->bl.next = NULL;
	fitem->bl.m = m;
	fitem->bl.x = x;
	fitem->bl.y = y;
	fitem->bl.id = map_get_new_object_id();
	if(fitem->bl.id == 0) {
		aFree(fitem);
		return 0;
	}

	fitem->first_get_charid = first_charid;
	fitem->first_get_tick = gettick() + (flags&1 ? battle_config.mvp_item_first_get_time : battle_config.item_first_get_time);
	fitem->second_get_charid = second_charid;
	fitem->second_get_tick = fitem->first_get_tick + (flags&1 ? battle_config.mvp_item_second_get_time : battle_config.item_second_get_time);
	fitem->third_get_charid = third_charid;
	fitem->third_get_tick = fitem->second_get_tick + (flags&1 ? battle_config.mvp_item_third_get_time : battle_config.item_third_get_time);
	fitem->mob_id = mob_id;

	memcpy(&fitem->item, item, sizeof(*item));
	fitem->item.amount = amount;
	fitem->subx =(r&3) * 3 + 3;
	fitem->suby = ((r>>2)&3) * 3 + 3;
	fitem->cleartimer = add_timer(gettick() + battle_config.flooritem_lifetime, map_clearflooritem_timer, fitem->bl.id, 0);

	map_addiddb(&fitem->bl);
	if(map_addblock(&fitem->bl))
		return 0;
	clif_dropflooritem(fitem);

	return fitem->bl.id;
}

/**
 * @see DBCreateData
 */
static DBData create_charid2nick(DBKey key, va_list args)
{
	struct charid2nick *p;

	CREATE(p, struct charid2nick, 1);
	return db_ptr2data(p);
}

/// Adds(or replaces) the nick of charid to nick_db and fulfils pending requests.
/// Does nothing if the character is online.
void map_addnickdb(int charid, const char *nick)
{
	struct charid2nick *p;

	if( map_charid2sd(charid) )
		return; //Already online

	p = idb_ensure(nick_db, charid, create_charid2nick);
	safestrncpy(p->nick, nick, sizeof(p->nick));

	while( p->requests ) {
		struct map_session_data *sd;
		struct charid_request* req;

		req = p->requests;
		p->requests = req->next;
		sd = map_charid2sd(req->charid);
		if( sd )
			clif_solved_charname(sd->fd, charid, p->nick);
		aFree(req);
	}
}

/// Removes the nick of charid from nick_db.
/// Sends name to all pending requests on charid.
void map_delnickdb(int charid, const char *name)
{
	struct charid2nick *p;
	DBData data;

	if( !nick_db->remove(nick_db, db_i2key(charid), &data) || !(p = db_data2ptr(&data)) )
		return;

	while( p->requests ) {
		struct map_session_data *sd;
		struct charid_request* req;

		req = p->requests;
		p->requests = req->next;
		sd = map_charid2sd(req->charid);
		if( sd )
			clif_solved_charname(sd->fd, charid, name);
		aFree(req);
	}
	aFree(p);
}

/// Notifies sd of the nick of charid.
/// Uses the name in the character if online.
/// Uses the name in nick_db if offline.
void map_reqnickdb(struct map_session_data *sd, int charid)
{
	struct charid2nick *p;
	struct charid_request* req;
	struct map_session_data *tsd;

	nullpo_retv(sd);

	tsd = map_charid2sd(charid);
	if( tsd ) {
		clif_solved_charname(sd->fd, charid, tsd->status.name);
		return;
	}

	p = idb_ensure(nick_db, charid, create_charid2nick);
	if( *p->nick ) {
		clif_solved_charname(sd->fd, charid, p->nick);
		return;
	}
	//Not in cache, request it
	CREATE(req, struct charid_request, 1);
	req->next = p->requests;
	p->requests = req;
	chrif_searchcharid(charid);
}

/*==========================================
 * add bl to id_db
 *------------------------------------------*/
void map_addiddb(struct block_list *bl)
{
	nullpo_retv(bl);

	if( bl->type == BL_PC ) {
		TBL_PC *sd = (TBL_PC *)bl;

		idb_put(pc_db, sd->bl.id, sd);
		idb_put(charid_db, sd->status.char_id, sd);
	} else if( bl->type == BL_MOB ) {
		TBL_MOB *md = (TBL_MOB *)bl;

		idb_put(mobid_db, bl->id, bl);
		if( md->state.boss == BTYPE_MVP )
			idb_put(bossid_db, bl->id, bl);
	}

	if( bl->type&BL_REGEN )
		idb_put(regen_db, bl->id, bl);

	idb_put(id_db, bl->id, bl);
}

/*==========================================
 * remove bl from id_db
 *------------------------------------------*/
void map_deliddb(struct block_list *bl)
{
	nullpo_retv(bl);

	if( bl->type == BL_PC ) {
		TBL_PC *sd = (TBL_PC *)bl;

		idb_remove(pc_db, sd->bl.id);
		idb_remove(charid_db, sd->status.char_id);
	} else if( bl->type == BL_MOB ) {
		idb_remove(mobid_db, bl->id);
		idb_remove(bossid_db, bl->id);
	}

	if( bl->type&BL_REGEN )
		idb_remove(regen_db, bl->id);

	idb_remove(id_db, bl->id);
}

/*==========================================
 * Standard call when a player connection is closed.
 *------------------------------------------*/
int map_quit(struct map_session_data *sd) {
	int i;

	if (!sd->state.keepshop) { //Close vending/buyingstore
		if (sd->state.vending)
			vending_closevending(sd);
		else if (sd->state.buyingstore)
			buyingstore_close(sd);
	}

	if (!sd->state.active) { //Removing a player that is not active
		struct auth_node *node = chrif_search(sd->status.account_id);

		//Except when logging out, clear the auth-connect data immediately
		if (node && node->char_id == sd->status.char_id && node->state != ST_LOGOUT)
			chrif_auth_delete(node->account_id,node->char_id,node->state);
		//Non-active players should not have loaded any data yet (or it was cleared already) so no additional cleanups are needed
		return 0;
	}

	if (sd->expiration_tid != INVALID_TIMER)
		delete_timer(sd->expiration_tid,pc_expiration_timer);

	if (sd->npc_timer_id != INVALID_TIMER) //Cancel the event timer
		npc_timerevent_quit(sd);

	if (sd->autotrade_tid != INVALID_TIMER)
		delete_timer(sd->autotrade_tid,pc_autotrade_timer);

	if (sd->npc_id)
		npc_event_dequeue(sd);

	if (sd->bg_id)
		bg_team_leave(sd,1);

	if (sd->status.clan_id)
		clan_member_left(sd);

	pc_itemcd_do(sd,false);

	npc_script_event(sd,NPCE_LOGOUT);

	//'unit_free' handles clearing the player related data,
	//'map_quit' handles extra specific data which is related to quitting normally
	//(changing map-servers invokes unit_free but bypasses map_quit)
	if (sd->sc.count) {
		for (i = 0; i < SC_MAX; i++) { //Statuses that are removed on logout
			if (status_get_sc_type(i)&SC_REM_ON_LOGOUT) {
				if (!sd->sc.data[i])
					continue;
				switch (i) {
					case SC_REGENERATION:
						if (!sd->sc.data[i]->val4)
							break;
					//Fall through
					case SC_PROVOKE:
					case SC_EXTREMITYFIST:
					case SC_PRESERVE: //Infinite provoke ends on logout
						if (i == SC_PROVOKE && sd->sc.data[i]->timer != INVALID_TIMER)
							break;
#ifdef RENEWAL
						if (i == SC_EXTREMITYFIST || i == SC_PRESERVE)
							break;
#endif
					//Fall through
					default:
						status_change_end(&sd->bl,(sc_type)i,INVALID_TIMER);
						break;
				}
			}
		}
	}

	for (i = 0; i < EQI_MAX; i++) {
		if (sd->equip_index[i] >= 0 && pc_isequip(sd, sd->equip_index[i]))
			pc_unequipitem(sd, sd->equip_index[i], 2);
	}

	if (sd->pd) //Return loot to owner
		pet_lootitem_drop(sd->pd,sd);

	if (sd->ed) //Remove effects here rather than unit_remove_map_pc so we don't clear on Teleport/map change
		elemental_clean_effect(sd->ed);

	if (map[sd->bl.m].instance_id)
		instance_delusers(map[sd->bl.m].instance_id);

	unit_remove_map_pc(sd,CLR_RESPAWN);

	if (map[sd->bl.m].instance_id) { //Avoid map conflicts and warnings on next login
		int16 m;
		struct point *pt;

		if (map[sd->bl.m].save.map)
			pt = &map[sd->bl.m].save;
		else
			pt = &sd->status.save_point;
		if ((m = map_mapindex2mapid(pt->map)) >= 0) {
			sd->bl.m = m;
			sd->bl.x = pt->x;
			sd->bl.y = pt->y;
			sd->mapindex = pt->map;
		}
	}

	if (sd->state.vending)
		idb_remove(vending_db,sd->status.char_id);

	if (sd->state.buyingstore)
		idb_remove(buyingstore_db,sd->status.char_id);

	pc_damage_log_clear(sd,0);
	party_booking_delete(sd); //Party Booking [Spiria]
	pc_makesavestatus(sd);
	pc_clean_skilltree(sd);
	pc_crimson_marker_clear(sd);
	chrif_save(sd,CSAVE_QUIT|CSAVE_INVENTORY|CSAVE_CART);
	unit_free_pc(sd);
	return 0;
}

/*==========================================
 * Lookup, id to session (player,mob,npc,homon,merc..)
 *------------------------------------------*/
struct map_session_data *map_id2sd(int id) {
	if (id <= 0) return NULL;
	return (struct map_session_data *)idb_get(pc_db,id);
}

struct mob_data *map_id2md(int id) {
	if (id <= 0) return NULL;
	return (struct mob_data *)idb_get(mobid_db,id);
}

struct npc_data *map_id2nd(int id) {
	struct block_list *bl = map_id2bl(id);
	return BL_CAST(BL_NPC, bl);
}

struct homun_data *map_id2hd(int id) {
	struct block_list *bl = map_id2bl(id);
	return BL_CAST(BL_HOM, bl);
}

struct mercenary_data *map_id2mc(int id) {
	struct block_list *bl = map_id2bl(id);
	return BL_CAST(BL_MER, bl);
}

struct pet_data *map_id2pd(int id) {
	struct block_list *bl = map_id2bl(id);
	return BL_CAST(BL_PET, bl);
}

struct elemental_data *map_id2ed(int id) {
	struct block_list *bl = map_id2bl(id);
	return BL_CAST(BL_ELEM, bl);
}

struct chat_data *map_id2cd(int id) {
	struct block_list *bl = map_id2bl(id);
	return BL_CAST(BL_CHAT, bl);
}

/// Returns the nick of the target charid or NULL if unknown (requests the nick to the char server).
const char *map_charid2nick(int charid)
{
	struct charid2nick *p;
	struct map_session_data *sd;

	sd = map_charid2sd(charid);
	if( sd )
		return sd->status.name; //Character is online, return it's name

	p = idb_ensure(nick_db, charid, create_charid2nick);
	if( *p->nick )
		return p->nick; //Name in nick_db

	chrif_searchcharid(charid); //Request the name
	return NULL;
}

/// Returns the struct map_session_data of the charid or NULL if the char is not online.
struct map_session_data *map_charid2sd(int charid)
{
	return (struct map_session_data *)idb_get(charid_db, charid);
}

/*==========================================
 * Search session data from a nick name
 * (without sensitive case if necessary)
 * return map_session_data pointer or NULL
 *------------------------------------------*/
struct map_session_data *map_nick2sd(const char *nick)
{
	struct map_session_data *sd;
	struct map_session_data *found_sd;
	struct s_mapiterator *iter;
	size_t nicklen;
	int qty = 0;

	if( nick == NULL )
		return NULL;

	nicklen = strlen(nick);
	iter = mapit_getallusers();

	found_sd = NULL;
	for( sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC *)mapit_next(iter) )
	{
		if( battle_config.partial_name_scan )
		{// partial name search
			if( strnicmp(sd->status.name, nick, nicklen) == 0 )
			{
				found_sd = sd;

				if( strcmp(sd->status.name, nick) == 0 )
				{// Perfect Match
					qty = 1;
					break;
				}

				qty++;
			}
		}
		else if( strcasecmp(sd->status.name, nick) == 0 )
		{// exact search only
			found_sd = sd;
			break;
		}
	}
	mapit_free(iter);

	if( battle_config.partial_name_scan && qty != 1 )
		found_sd = NULL;

	return found_sd;
}

/*==========================================
 * Looksup id_db DBMap and returns BL pointer of 'id' or NULL if not found
 *------------------------------------------*/
struct block_list *map_id2bl(int id) {
	return (struct block_list *)idb_get(id_db,id);
}

/**
 * Same as map_id2bl except it only checks for its existence
 */
bool map_blid_exists( int id ) {
	return (idb_exists(id_db,id));
}

/*==========================================
 * Convex Mirror
 *------------------------------------------*/
struct mob_data *map_getmob_boss(int16 m)
{
	DBIterator *iter;
	struct mob_data *md = NULL;
	bool found = false;

	iter = db_iterator(bossid_db);
	for( md = (struct mob_data *)dbi_first(iter); dbi_exists(iter); md = (struct mob_data *)dbi_next(iter) ) {
		if( md->bl.m == m ) {
			found = true;
			break;
		}
	}
	dbi_destroy(iter);

	return (found) ? md : NULL;
}

struct mob_data *map_id2boss(int id)
{
	if (id <= 0) return NULL;
	return (struct mob_data *)idb_get(bossid_db,id);
}

/// Applies func to all the players in the db.
/// Stops iterating if func returns -1.
void map_foreachpc(int (*func)(struct map_session_data *sd, va_list args), ...)
{
	DBIterator *iter;
	struct map_session_data *sd;

	iter = db_iterator(pc_db);
	for( sd = dbi_first(iter); dbi_exists(iter); sd = dbi_next(iter) )
	{
		va_list args;
		int ret;

		va_start(args, func);
		ret = func(sd, args);
		va_end(args);
		if( ret == -1 )
			break;// stop iterating
	}
	dbi_destroy(iter);
}

/// Applies func to all the mobs in the db.
/// Stops iterating if func returns -1.
void map_foreachmob(int (*func)(struct mob_data *md, va_list args), ...)
{
	DBIterator *iter;
	struct mob_data *md;

	iter = db_iterator(mobid_db);
	for( md = (struct mob_data *)dbi_first(iter); dbi_exists(iter); md = (struct mob_data *)dbi_next(iter) )
	{
		va_list args;
		int ret;

		va_start(args, func);
		ret = func(md, args);
		va_end(args);
		if( ret == -1 )
			break;// stop iterating
	}
	dbi_destroy(iter);
}

/// Applies func to all the npcs in the db.
/// Stops iterating if func returns -1.
void map_foreachnpc(int (*func)(struct npc_data *nd, va_list args), ...)
{
	DBIterator *iter;
	struct block_list *bl;

	iter = db_iterator(id_db);
	for( bl = (struct block_list *)dbi_first(iter); dbi_exists(iter); bl = (struct block_list *)dbi_next(iter) )
	{
		if( bl->type == BL_NPC )
		{
			struct npc_data *nd = (struct npc_data *)bl;
			va_list args;
			int ret;

			va_start(args, func);
			ret = func(nd, args);
			va_end(args);
			if( ret == -1 )
				break;// stop iterating
		}
	}
	dbi_destroy(iter);
}

/// Applies func to everything in the db.
/// Stops iterating if func returns -1.
void map_foreachregen(int (*func)(struct block_list *bl, va_list args), ...)
{
	DBIterator *iter;
	struct block_list *bl;

	iter = db_iterator(regen_db);
	for( bl = (struct block_list *)dbi_first(iter); dbi_exists(iter); bl = (struct block_list *)dbi_next(iter) )
	{
		va_list args;
		int ret;

		va_start(args, func);
		ret = func(bl, args);
		va_end(args);
		if( ret == -1 )
			break;// stop iterating
	}
	dbi_destroy(iter);
}

/// Applies func to everything in the db.
/// Stops iterating if func returns -1.
void map_foreachiddb(int (*func)(struct block_list *bl, va_list args), ...)
{
	DBIterator *iter;
	struct block_list *bl;

	iter = db_iterator(id_db);
	for( bl = (struct block_list *)dbi_first(iter); dbi_exists(iter); bl = (struct block_list *)dbi_next(iter) )
	{
		va_list args;
		int ret;

		va_start(args, func);
		ret = func(bl, args);
		va_end(args);
		if( ret == -1 )
			break;// stop iterating
	}
	dbi_destroy(iter);
}

/// Iterator.
/// Can filter by bl type.
struct s_mapiterator
{
	enum e_mapitflags flags;// flags for special behaviour
	enum bl_type types;// what bl types to return
	DBIterator *dbi;// database iterator
};

/// Returns true if the block_list matches the description in the iterator.
///
/// @param _mapit_ Iterator
/// @param _bl_ block_list
/// @return true if it matches
#define MAPIT_MATCHES(_mapit_,_bl_) \
	( \
		( (_bl_)->type & (_mapit_)->types /* type matches */ ) \
	)

/// Allocates a new iterator.
/// Returns the new iterator.
/// types can represent several BL's as a bit field.
/// @TODO: Should this be expanded to allow filtering of map/guild/party/chat/cell/area/...?
///
/// @param flags Flags of the iterator
/// @param type Target types
/// @return Iterator
struct s_mapiterator *mapit_alloc(enum e_mapitflags flags, enum bl_type types)
{
	struct s_mapiterator *mapit;

	CREATE(mapit, struct s_mapiterator, 1);
	mapit->flags = flags;
	mapit->types = types;
	if( types == BL_PC )       mapit->dbi = db_iterator(pc_db);
	else if( types == BL_MOB ) mapit->dbi = db_iterator(mobid_db);
	else                       mapit->dbi = db_iterator(id_db);
	return mapit;
}

/// Frees the iterator.
///
/// @param mapit Iterator
void mapit_free(struct s_mapiterator *mapit)
{
	nullpo_retv(mapit);

	dbi_destroy(mapit->dbi);
	aFree(mapit);
}

/// Returns the first block_list that matches the description.
/// Returns NULL if not found.
///
/// @param mapit Iterator
/// @return first block_list or NULL
struct block_list *mapit_first(struct s_mapiterator *mapit)
{
	struct block_list *bl;

	nullpo_retr(NULL,mapit);

	for( bl = (struct block_list *)dbi_first(mapit->dbi); bl != NULL; bl = (struct block_list *)dbi_next(mapit->dbi) )
		if( MAPIT_MATCHES(mapit,bl) )
			break; // Found match

	return bl;
}

/// Returns the last block_list that matches the description.
/// Returns NULL if not found.
///
/// @param mapit Iterator
/// @return last block_list or NULL
struct block_list *mapit_last(struct s_mapiterator *mapit)
{
	struct block_list *bl;

	nullpo_retr(NULL,mapit);

	for( bl = (struct block_list *)dbi_last(mapit->dbi); bl != NULL; bl = (struct block_list *)dbi_prev(mapit->dbi) )
		if( MAPIT_MATCHES(mapit,bl) )
			break; // Found match

	return bl;
}

/// Returns the next block_list that matches the description.
/// Returns NULL if not found.
///
/// @param mapit Iterator
/// @return next block_list or NULL
struct block_list *mapit_next(struct s_mapiterator *mapit)
{
	struct block_list *bl;

	nullpo_retr(NULL,mapit);

	for( ; ; )
	{
		bl = (struct block_list *)dbi_next(mapit->dbi);
		if( bl == NULL )
			break;// end
		if( MAPIT_MATCHES(mapit,bl) )
			break;// found a match
		// try next
	}
	return bl;
}

/// Returns the previous block_list that matches the description.
/// Returns NULL if not found.
///
/// @param mapit Iterator
/// @return previous block_list or NULL
struct block_list *mapit_prev(struct s_mapiterator *mapit)
{
	struct block_list *bl;

	nullpo_retr(NULL,mapit);

	for( ; ; )
	{
		bl = (struct block_list *)dbi_prev(mapit->dbi);
		if( bl == NULL )
			break;// end
		if( MAPIT_MATCHES(mapit,bl) )
			break;// found a match
		// try prev
	}
	return bl;
}

/// Returns true if the current block_list exists in the database.
///
/// @param mapit Iterator
/// @return true if it exists
bool mapit_exists(struct s_mapiterator *mapit)
{
	nullpo_retr(false,mapit);

	return dbi_exists(mapit->dbi);
}

/*==========================================
 * Add npc-bl to id_db, basically register npc to map
 *------------------------------------------*/
bool map_addnpc(int16 m,struct npc_data *nd)
{
	nullpo_ret(nd);

	if( m < 0 || m >= map_num )
		return false;

	if( map[m].npc_num == MAX_NPC_PER_MAP ) {
		ShowWarning("too many NPCs in one map %s\n",map[m].name);
		return false;
	}

	map[m].npc[map[m].npc_num] = nd;
	map[m].npc_num++;
	idb_put(id_db,nd->bl.id,nd);
	return true;
}

/*==========================================
 * Add an instance map
 *------------------------------------------*/
int map_addinstancemap(const char *name, int id)
{
	int src_m = map_mapname2mapid(name);
	int dst_m = -1, i;
	char iname[MAP_NAME_LENGTH];
	size_t num_cell, size;

	if(src_m < 0)
		return -1;

	if(strlen(name) > 20) {
		// against buffer overflow
		ShowError("map_addisntancemap: can't add long map name \"%s\"\n", name);
		return -2;
	}

	for(i = instance_start; i < MAX_MAP_PER_SERVER; i++) {
		if(!map[i].name[0])
			break;
	}
	if(i < map_num) // Destination map value overwrites another
		dst_m = i;
	else if(i < MAX_MAP_PER_SERVER) // Destination map value increments to new map
		dst_m = map_num++;
	else {
		// Out of bounds
		ShowError("map_addinstancemap failed. map_num(%d) > map_max(%d)\n",map_num, MAX_MAP_PER_SERVER);
		return -3;
	}

	// Copy the map
	memcpy(&map[dst_m], &map[src_m], sizeof(struct map_data));

	strcpy(iname,name);

	// Alter the name
	// Due to this being custom we only worry about preserving as many characters as necessary for accurate map distinguishing
	// This also allows us to maintain complete independence with main map functions
	if((strchr(iname,'@') == NULL) && strlen(iname) > 8) {
		memmove(iname, iname+(strlen(iname)-9), strlen(iname));
		snprintf(map[dst_m].name, sizeof(map[dst_m].name),"%d#%s", id, iname);
	} else
		snprintf(map[dst_m].name, sizeof(map[dst_m].name),"%.3d%s", id, iname);
	map[dst_m].name[MAP_NAME_LENGTH-1] = '\0';

	map[dst_m].m = dst_m;
	map[dst_m].instance_id = id;
	map[dst_m].instance_src_map = src_m;
	map[dst_m].users = 0;

	memset(map[dst_m].npc, 0, sizeof(map[dst_m].npc));
	map[dst_m].npc_num = 0;

	// Reallocate cells
	num_cell = map[dst_m].xs * map[dst_m].ys;
	CREATE(map[dst_m].cell, struct mapcell, num_cell);
	memcpy(map[dst_m].cell, map[src_m].cell, num_cell * sizeof(struct mapcell));

	size = map[dst_m].bxs * map[dst_m].bys * sizeof(struct block_list *);
	map[dst_m].block = (struct block_list **)aCalloc(1,size);
	map[dst_m].block_mob = (struct block_list **)aCalloc(1,size);

	map[dst_m].index = mapindex_addmap(-1, map[dst_m].name);
	map[dst_m].channel = NULL;
	map[dst_m].mob_delete_timer = INVALID_TIMER;

	map_addmap2db(&map[dst_m]);

	return dst_m;
}

/*==========================================
 * Set player to save point when they leave
 *------------------------------------------*/
static int map_instancemap_leave(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd;

	nullpo_retr(0, bl);
	nullpo_retr(0, sd = (struct map_session_data *)bl);

	pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, 3);

	return 1;
}

/*==========================================
 * Remove all units from instance
 *------------------------------------------*/
static int map_instancemap_clean(struct block_list *bl, va_list ap)
{
	nullpo_retr(0, bl);

	switch(bl->type) { //BL_PET, BL_HOM, BL_MER, and BL_ELEM are moved when BL_PC warped out in 'map_instancemap_leave'
		case BL_NPC:
			npc_unload((struct npc_data *)bl, true);
			break;
		case BL_MOB:
			unit_free(bl, CLR_OUTSIGHT);
			break;
		case BL_ITEM:
			map_clearflooritem(bl);
			break;
		case BL_SKILL:
			skill_delunit((struct skill_unit *)bl);
			break;
	}

	return 1;
}

static void map_free_questinfo(int m);

/*==========================================
 * Deleting an instance map
 *------------------------------------------*/
int map_delinstancemap(int m)
{
	if(m < 0 || !map[m].instance_id)
		return 0;

	// Kick everyone out
	map_foreachinmap(map_instancemap_leave, m, BL_PC);

	// Do the unit cleanup
	map_foreachinmap(map_instancemap_clean, m, BL_ALL);

	if(map[m].mob_delete_timer != INVALID_TIMER)
		delete_timer(map[m].mob_delete_timer, map_removemobs_timer);

	// Free memory
	aFree(map[m].cell);
	aFree(map[m].block);
	aFree(map[m].block_mob);
	map_free_questinfo(m);

	mapindex_removemap(map[m].index);
	map_removemapdb(&map[m]);
	memset(&map[m], 0x00, sizeof(map[0]));
	map[m].mob_delete_timer = INVALID_TIMER;
	return 1;
}

/*=========================================
 * Dynamic Mobs [Wizputer]
 *-----------------------------------------*/
// Stores the spawn data entry in the mob list.
// Returns the index of successful, or -1 if the list was full.
int map_addmobtolist(unsigned short m, struct spawn_data *spawn)
{
	size_t i;

	ARR_FIND(0, MAX_MOB_LIST_PER_MAP, i, map[m].moblist[i] == NULL);
	if( i < MAX_MOB_LIST_PER_MAP ) {
		map[m].moblist[i] = spawn;
		return i;
	}
	return -1;
}

void map_spawnmobs(int16 m)
{
	int i, k = 0;

	if (map[m].mob_delete_timer != INVALID_TIMER) { //Mobs have not been removed yet [Skotlex]
		delete_timer(map[m].mob_delete_timer, map_removemobs_timer);
		map[m].mob_delete_timer = INVALID_TIMER;
		return;
	}

	for (i = 0; i < MAX_MOB_LIST_PER_MAP; i++) {
		if (map[m].moblist[i] != NULL) {
			k += map[m].moblist[i]->num;
			npc_parse_mob2(map[m].moblist[i]);
		}
	}

	if (battle_config.etc_log && k > 0)
		ShowStatus("Map %s: Spawned '"CL_WHITE"%d"CL_RESET"' mobs.\n",map[m].name, k);
}

int map_removemobs_sub(struct block_list *bl, va_list ap)
{
	struct mob_data *md = (struct mob_data *)bl;
	nullpo_ret(md);

	//When not to remove mob:
	// doesn't respawn and is not a slave
	if( !md->spawn && !md->master_id )
		return 0;
	// respawn data is not in cache
	if( md->spawn && !md->spawn->state.dynamic )
		return 0;
	// hasn't spawned yet
	if( md->spawn_timer != INVALID_TIMER )
		return 0;
	// is damaged and mob_remove_damaged is off
	if( !battle_config.mob_remove_damaged && md->status.hp < md->status.max_hp )
		return 0;
	// is a mvp
	if( md->db->mexp > 0 )
		return 0;
	
	unit_free(&md->bl,CLR_OUTSIGHT);

	return 1;
}

TIMER_FUNC(map_removemobs_timer)
{
	int count;
	const int16 m = id;

	if (m < 0 || m >= MAX_MAP_PER_SERVER) { //Incorrect map id!
		ShowError("map_removemobs_timer error: timer %d points to invalid map %d\n",tid, m);
		return 0;
	}
	if (map[m].mob_delete_timer != tid) { //Incorrect timer call!
		ShowError("map_removemobs_timer mismatch: %d != %d (map %s)\n",map[m].mob_delete_timer, tid, map[m].name);
		return 0;
	}
	map[m].mob_delete_timer = INVALID_TIMER;
	if (map[m].users > 0) //Map not empty!
		return 1;

	count = map_foreachinmap(map_removemobs_sub, m, BL_MOB);

	if (battle_config.etc_log && count > 0)
		ShowStatus("Map %s: Removed '"CL_WHITE"%d"CL_RESET"' mobs.\n",map[m].name, count);

	return 1;
}

void map_removemobs(int16 m)
{
	if (map[m].mob_delete_timer != INVALID_TIMER) //Should never happen
		return; //Mobs are already scheduled for removal

	if (map[m].instance_id)
		return; //Don't remove mobs on instance map

	map[m].mob_delete_timer = add_timer(gettick() + battle_config.mob_remove_delay, map_removemobs_timer, m, 0);
}

/*==========================================
 * Check for map_name from map_id
 *------------------------------------------*/
const char *map_mapid2mapname(int m)
{
	if (map[m].instance_id) { //Instance map check
		struct instance_data *im = &instance_data[map[m].instance_id];

		if (!im) //This shouldn't happen but if it does give them the map we intended to give
			return map[m].name;
		else {
			int i;

			for (i = 0; i < MAX_MAP_PER_INSTANCE; i++) { //Loop to find the src map we want
				if (im->map[i].m == m)
					return map[im->map[i].src_m].name;
			}
		}
	}

	return map[m].name;
}

/*==========================================
 * Hookup, get map_id from map_name
 *------------------------------------------*/
int16 map_mapname2mapid(const char *name)
{
	unsigned short map_index;

	map_index = mapindex_name2id(name);
	if (!map_index)
		return -1;
	return map_mapindex2mapid(map_index);
}

/*==========================================
 * Returns the map of the given mapindex. [Skotlex]
 *------------------------------------------*/
int16 map_mapindex2mapid(unsigned short mapindex)
{
	struct map_data *md = NULL;
	
	if (!mapindex)
		return -1;
	
	md = (struct map_data*)uidb_get(map_db,(unsigned int)mapindex);
	if (md == NULL || md->cell == NULL)
		return -1;
	return md->m;
}

/*==========================================
 * Switching Ip, port ? (like changing map_server) get ip/port from map_name
 *------------------------------------------*/
int map_mapname2ipport(unsigned short name, uint32 *ip, uint16* port)
{
	struct map_data_other_server *mdos;

	mdos = (struct map_data_other_server*)uidb_get(map_db,(unsigned int)name);
	if (mdos == NULL || mdos->cell) //If gat isn't null, this is a local map
		return -1;
	*ip = mdos->ip;
	*port = mdos->port;
	return 0;
}

/*==========================================
 * Checks if both dirs point in the same direction.
 *------------------------------------------*/
int map_check_dir(int s_dir,int t_dir)
{
	if (s_dir == t_dir)
		return 0;
	switch (s_dir) {
		case 0: if(t_dir == 7 || t_dir == 1 || t_dir == 0) return 0; break;
		case 1: if(t_dir == 0 || t_dir == 2 || t_dir == 1) return 0; break;
		case 2: if(t_dir == 1 || t_dir == 3 || t_dir == 2) return 0; break;
		case 3: if(t_dir == 2 || t_dir == 4 || t_dir == 3) return 0; break;
		case 4: if(t_dir == 3 || t_dir == 5 || t_dir == 4) return 0; break;
		case 5: if(t_dir == 4 || t_dir == 6 || t_dir == 5) return 0; break;
		case 6: if(t_dir == 5 || t_dir == 7 || t_dir == 6) return 0; break;
		case 7: if(t_dir == 6 || t_dir == 0 || t_dir == 7) return 0; break;
	}
	return 1;
}

/*==========================================
 * Returns the direction of the given cell, relative to 'src'
 *------------------------------------------*/
uint8 map_calc_dir(struct block_list *src, int16 x, int16 y)
{
	uint8 dir = 0;

	nullpo_ret(src);

	dir = map_calc_dir_xy(src->x, src->y, x, y, unit_getdir(src));
	return dir;
}

/*==========================================
 * Returns the direction of the given cell, relative to source cell
 * Use this if you don't have a block list available to check against
 *------------------------------------------*/
uint8 map_calc_dir_xy(int16 srcx, int16 srcy, int16 x, int16 y, uint8 srcdir)
{
	uint8 dir = 0;
	int dx, dy;

	dx = x - srcx;
	dy = y - srcy;

	//Both are standing on the same spot
	//Aegis-style, makes knockback default to the left
	//Athena-style, makes knockback default to behind 'src'
	if(dx == 0 && dy == 0)
		dir = (battle_config.knockback_left ? 6 : srcdir);
	else if(dx >= 0 && dy >= 0) { //Upper-right
		if(dx >= dy * 3)
			dir = 6; //Right
		else if(dx * 3 < dy)
			dir = 0; //Up
		else
			dir = 7; //Up-right
	} else if(dx >= 0 && dy <= 0) { //Lower-right
		if(dx >= -dy * 3)
			dir = 6; //Right
		else if(dx * 3 < -dy)
			dir = 4; //Down
		else
			dir = 5; //Down-right
	} else if(dx <= 0 && dy <= 0) { //Lower-left
		if(dx * 3 >= dy)
			dir = 4; //Down
		else if(dx < dy * 3)
			dir = 2; //Left
		else
			dir = 3; //Down-left
	} else { //Upper-left
		if(-dx * 3 <= dy)
			dir = 0; //Up
		else if(-dx > dy * 3)
			dir = 2; //Left
		else
			dir = 1; //Up-left
	}
	return dir;
}

/*==========================================
 * Randomizes target cell x,y to a random walkable cell that
 * has the same distance from object as given coordinates do. [Skotlex]
 *------------------------------------------*/
int map_random_dir(struct block_list *bl, int16 *x, int16 *y)
{
	short xi = *x - bl->x;
	short yi = *y - bl->y;
	short i = 0;
	int dist2 = xi * xi + yi * yi;
	short dist = (short)sqrt((float)dist2);

	if(dist < 1) dist = 1;

	do {
		short j = 1 + 2 * (rnd()%4); //Pick a random diagonal direction
		short segment = 1 + (rnd()%dist); //Pick a random interval from the whole vector in that direction

		xi = bl->x + segment * dirx[j];
		segment = (short)sqrt((float)(dist2 - segment * segment)); //The complement of the previously picked segment
		yi = bl->y + segment * diry[j];
	} while(
		(map_getcell(bl->m,xi,yi,CELL_CHKNOPASS) || !path_search(NULL,bl->m,bl->x,bl->y,xi,yi,1,CELL_CHKNOREACH)) &&
		(++i) < 100);

	if(i < 100) {
		*x = xi;
		*y = yi;
		return 1;
	}
	return 0;
}

// gat system
static inline struct mapcell map_gat2cell(int gat) {
	struct mapcell cell;

	memset(&cell,0,sizeof(struct mapcell));

	switch(gat) {
		case 0: cell.walkable = 1; cell.shootable = 1; cell.water = 0; break; //Walkable ground
		case 1: cell.walkable = 0; cell.shootable = 0; cell.water = 0; break; //Non-walkable ground
		case 2: cell.walkable = 1; cell.shootable = 1; cell.water = 0; break; //???
		case 3: cell.walkable = 1; cell.shootable = 1; cell.water = 1; break; //Walkable water
		case 4: cell.walkable = 1; cell.shootable = 1; cell.water = 0; break; //???
		case 5: cell.walkable = 0; cell.shootable = 1; cell.water = 0; break; //Gap (snipable)
		case 6: cell.walkable = 1; cell.shootable = 1; cell.water = 0; break; //???
		default:
			ShowWarning("map_gat2cell: unrecognized gat type '%d'\n", gat);
			break;
	}

	return cell;
}

static int map_cell2gat(struct mapcell cell)
{
	if(cell.walkable == 1 && cell.shootable == 1 && cell.water == 0) return 0;
	if(cell.walkable == 0 && cell.shootable == 0 && cell.water == 0) return 1;
	if(cell.walkable == 1 && cell.shootable == 1 && cell.water == 1) return 3;
	if(cell.walkable == 0 && cell.shootable == 1 && cell.water == 0) return 5;

	ShowWarning("map_cell2gat: cell has no matching gat type\n");
	return 1; //Default to 'wall'
}

/*==========================================
 * Confirm if celltype in (m,x,y) match the one given in cellchk
 *------------------------------------------*/
int map_getcell(int16 m,int16 x,int16 y,cell_chk cellchk)
{
	return (m < 0 || m >= MAX_MAP_PER_SERVER) ? 0 : map_getcellp(&map[m],x,y,cellchk);
}

int map_getcellp(struct map_data *m,int16 x,int16 y,cell_chk cellchk)
{
	struct mapcell cell;

	nullpo_ret(m);

	//NOTE: this intentionally overrides the last row and column
	if(x < 0 || x >= m->xs - 1 || y < 0 || y >= m->ys - 1)
		return(cellchk == CELL_CHKNOPASS);

	cell = m->cell[x + y * m->xs];

	switch(cellchk) {
		//Gat type retrieval
		case CELL_GETTYPE:
			return map_cell2gat(cell);

		//Base gat type checks
		case CELL_CHKWALL:
			return (!cell.walkable && !cell.shootable);

		case CELL_CHKWATER:
			return (cell.water);

		case CELL_CHKCLIFF:
			return (!cell.walkable && cell.shootable);

		//Base cell type checks
		case CELL_CHKNPC:
			return (cell.npc);
		case CELL_CHKBASILICA:
			return (cell.basilica);
		case CELL_CHKLANDPROTECTOR:
			return (cell.landprotector);
		case CELL_CHKNOVENDING:
			return (cell.novending);
		case CELL_CHKNOCHAT:
			return (cell.nochat);
		case CELL_CHKMAELSTROM:
			return (cell.maelstrom);
		case CELL_CHKICEWALL:
			return (cell.icewall);
		case CELL_CHKNOICEWALL:
			return (cell.noicewall);

		//Special checks
		case CELL_CHKPASS:
#ifdef CELL_NOSTACK
			if (cell.cell_bl >= battle_config.custom_cell_stack_limit)
				return 0;
#endif
		case CELL_CHKREACH:
			return (cell.walkable);

		case CELL_CHKNOPASS:
#ifdef CELL_NOSTACK
			if (cell.cell_bl >= battle_config.custom_cell_stack_limit)
				return 1;
#endif
		case CELL_CHKNOREACH:
			return (!cell.walkable);

		case CELL_CHKSTACK:
#ifdef CELL_NOSTACK
			return (cell.cell_bl >= battle_config.custom_cell_stack_limit);
#else
			return 0;
#endif

		default:
			return 0;
	}
}

/*==========================================
 * Change the type/flags of a map cell
 * 'cell' - which flag to modify
 * 'flag' - true = on, false = off
 *------------------------------------------*/
void map_setcell(int16 m, int16 x, int16 y, cell_t cell, bool flag)
{
	int j;

	if( m < 0 || m >= map_num || x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys )
		return;

	j = x + y*map[m].xs;

	switch( cell ) {
		case CELL_WALKABLE:      map[m].cell[j].walkable = flag;      break;
		case CELL_SHOOTABLE:     map[m].cell[j].shootable = flag;     break;
		case CELL_WATER:         map[m].cell[j].water = flag;         break;

		case CELL_NPC:           map[m].cell[j].npc = flag;           break;
		case CELL_BASILICA:      map[m].cell[j].basilica = flag;      break;
		case CELL_LANDPROTECTOR: map[m].cell[j].landprotector = flag; break;
		case CELL_NOVENDING:     map[m].cell[j].novending = flag;     break;
		case CELL_NOCHAT:        map[m].cell[j].nochat = flag;        break;
		case CELL_MAELSTROM:     map[m].cell[j].maelstrom = flag;     break;
		case CELL_ICEWALL:       map[m].cell[j].icewall = flag;       break;
		case CELL_NOICEWALL:     map[m].cell[j].noicewall = flag;     break;

		default:
			ShowWarning("map_setcell: invalid cell type '%d'\n", (int)cell);
			break;
	}
}

void map_setgatcell(int16 m, int16 x, int16 y, int gat)
{
	int j;
	struct mapcell cell;

	if( m < 0 || m >= map_num || x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys )
		return;

	j = x + y*map[m].xs;

	cell = map_gat2cell(gat);
	map[m].cell[j].walkable = cell.walkable;
	map[m].cell[j].shootable = cell.shootable;
	map[m].cell[j].water = cell.water;
}

/*==========================================
 * Invisible Walls
 *------------------------------------------*/
static DBMap *iwall_db;

bool map_iwall_exist(const char *wall_name)
{
	return strdb_exists(iwall_db, wall_name);
}

void map_iwall_nextxy(int16 x, int16 y, int8 dir, int pos, int16 *x1, int16 *y1)
{
	if( dir == 0 || dir == 4 )
		*x1 = x; // Keep X
	else if( dir > 0 && dir < 4 )
		*x1 = x - pos; // Going left
	else
		*x1 = x + pos; // Going right

	if( dir == 2 || dir == 6 )
		*y1 = y;
	else if( dir > 2 && dir < 6 )
		*y1 = y - pos;
	else
		*y1 = y + pos;
}

bool map_iwall_set(int16 m, int16 x, int16 y, int size, int8 dir, bool shootable, const char *wall_name)
{
	struct iwall_data *iwall;
	int i;
	int16 x1 = 0, y1 = 0;

	if( size < 1 || !wall_name )
		return false;

	if( (iwall = (struct iwall_data *)strdb_get(iwall_db, wall_name)) != NULL )
		return false; // Already Exists

	if( map_getcell(m, x, y, CELL_CHKNOREACH) )
		return false; // Starting cell problem

	CREATE(iwall, struct iwall_data, 1);
	iwall->m = m;
	iwall->x = x;
	iwall->y = y;
	iwall->size = size;
	iwall->dir = dir;
	iwall->shootable = shootable;
	safestrncpy(iwall->wall_name, wall_name, sizeof(iwall->wall_name));

	for( i = 0; i < size; i++ ) {
		map_iwall_nextxy(x, y, dir, i, &x1, &y1);

		if( map_getcell(m, x1, y1, CELL_CHKNOREACH) )
			break; // Collision

		map_setcell(m, x1, y1, CELL_WALKABLE, false);
		map_setcell(m, x1, y1, CELL_SHOOTABLE, shootable);

		clif_changemapcell(0, m, x1, y1, map_getcell(m, x1, y1, CELL_GETTYPE), ALL_SAMEMAP);
	}

	iwall->size = i;

	strdb_put(iwall_db, iwall->wall_name, iwall);
	map[m].iwall_num++;

	return true;
}

void map_iwall_get(struct map_session_data *sd)
{
	struct iwall_data *iwall;
	DBIterator *iter;
	int16 x1, y1;
	int i;

	if( map[sd->bl.m].iwall_num < 1 )
		return;

	iter = db_iterator(iwall_db);
	for( iwall = dbi_first(iter); dbi_exists(iter); iwall = dbi_next(iter) ) {
		if( iwall->m != sd->bl.m )
			continue;

		for( i = 0; i < iwall->size; i++ ) {
			map_iwall_nextxy(iwall->x, iwall->y, iwall->dir, i, &x1, &y1);
			clif_changemapcell(sd->fd, iwall->m, x1, y1, map_getcell(iwall->m, x1, y1, CELL_GETTYPE), SELF);
		}
	}
	dbi_destroy(iter);
}

bool map_iwall_remove(const char *wall_name)
{
	struct iwall_data *iwall;
	int16 i, x1, y1;

	if( (iwall = (struct iwall_data *)strdb_get(iwall_db, wall_name)) == NULL )
		return false; // Nothing to do

	for( i = 0; i < iwall->size; i++ ) {
		map_iwall_nextxy(iwall->x, iwall->y, iwall->dir, i, &x1, &y1);

		map_setcell(iwall->m, x1, y1, CELL_SHOOTABLE, true);
		map_setcell(iwall->m, x1, y1, CELL_WALKABLE, true);

		clif_changemapcell(0, iwall->m, x1, y1, map_getcell(iwall->m, x1, y1, CELL_GETTYPE), ALL_SAMEMAP);
	}

	map[iwall->m].iwall_num--;
	strdb_remove(iwall_db, iwall->wall_name);
	return true;
}

/**
 * @see DBCreateData
 */
static DBData create_map_data_other_server(DBKey key, va_list args)
{
	struct map_data_other_server *mdos;
	unsigned short mapindex = (unsigned short)key.ui;
	mdos=(struct map_data_other_server *)aCalloc(1,sizeof(struct map_data_other_server));
	mdos->index = mapindex;
	memcpy(mdos->name, mapindex_id2name(mapindex), MAP_NAME_LENGTH);
	return db_ptr2data(mdos);
}

/*==========================================
 * Add mapindex to db of another map server
 *------------------------------------------*/
int map_setipport(unsigned short mapindex, uint32 ip, uint16 port)
{
	struct map_data_other_server *mdos;

	mdos= uidb_ensure(map_db,(unsigned int)mapindex, create_map_data_other_server);
	
	if(mdos->cell) //Local map,Do nothing. Give priority to our own local maps over ones from another server. [Skotlex]
		return 0;
	if(ip == clif_getip() && port == clif_getport()) {
		//That's odd, we received info that we are the ones with this map, but... we don't have it.
		ShowFatalError("map_setipport : received info that this map-server SHOULD have map '%s', but it is not loaded.\n",mapindex_id2name(mapindex));
		exit(EXIT_FAILURE);
	}
	mdos->ip   = ip;
	mdos->port = port;
	return 1;
}

/**
 * Delete all the other maps server management
 * @see DBApply
 */
int map_eraseallipport_sub(DBKey key, DBData *data, va_list va)
{
	struct map_data_other_server *mdos = db_data2ptr(data);
	if(mdos->cell == NULL) {
		db_remove(map_db,key);
		aFree(mdos);
	}
	return 0;
}

int map_eraseallipport(void)
{
	map_db->foreach(map_db,map_eraseallipport_sub);
	return 1;
}

/*==========================================
 * Delete mapindex from db of another map server
 *------------------------------------------*/
int map_eraseipport(unsigned short mapindex, uint32 ip, uint16 port)
{
	struct map_data_other_server *mdos;

	mdos = (struct map_data_other_server*)uidb_get(map_db,(unsigned int)mapindex);
	if(!mdos || mdos->cell) //Map either does not exists or is a local map.
		return 0;

	if(mdos->ip == ip && mdos->port == port) {
		uidb_remove(map_db,(unsigned int)mapindex);
		aFree(mdos);
		return 1;
	}
	return 0;
}

/*==========================================
 * [Shinryo]: Init the mapcache
 *------------------------------------------*/
static char *map_init_mapcache(FILE *fp)
{
	struct map_cache_main_header header;
	size_t size = 0;
	char *buffer;

	// No file open? Return..
	nullpo_ret(fp);

	// Get file size
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// Allocate enough space
	CREATE(buffer, char, size);

	// No memory? Return..
	nullpo_ret(buffer);

	// Read file into buffer..
	if(fread(buffer, 1, size, fp) != size) {
		ShowError("map_init_mapcache: Could not read entire mapcache file\n");
		aFree(buffer);
		return NULL;
	}

	rewind(fp);

	// Get main header to verify if data is corrupted
	if( fread(&header, sizeof(header), 1, fp) != 1 ) {
		ShowError("map_init_mapcache: Error obtaining main header!\n");
		aFree(buffer);
		return NULL;
	}

	// If the file is totally corrupted this will allow us to warn the user
	if( GetULong((unsigned char *)&(header.file_size)) != size ) {
		ShowError("map_init_mapcache: Map cache is corrupted!\n");
		aFree(buffer);
		return NULL;
	}

	return buffer;
}

/*==========================================
 * Map cache reading
 * [Shinryo]: Optimized some behaviour to speed this up
 *==========================================*/
int map_readfromcache(struct map_data *m, char *buffer, char *decode_buffer)
{
	int i;
	struct map_cache_main_header *header = (struct map_cache_main_header *)buffer;
	struct map_cache_map_info *info = NULL;
	char *p = buffer + sizeof(struct map_cache_main_header);

	for(i = 0; i < header->map_count; i++) {
		info = (struct map_cache_map_info *)p;

		if( strcmp(m->name, info->name) == 0 )
			break; // Map found

		// Jump to next entry..
		p += sizeof(struct map_cache_map_info) + info->len;
	}

	if( info && i < header->map_count ) {
		unsigned long size, xy;

		if( info->xs <= 0 || info->ys <= 0 )
			return 0;// Invalid

		m->xs = info->xs;
		m->ys = info->ys;
		size = (unsigned long)info->xs * (unsigned long)info->ys;

		if(size > MAX_MAP_SIZE) {
			ShowWarning("map_readfromcache: %s exceeded MAX_MAP_SIZE of %d\n", info->name, MAX_MAP_SIZE);
			return 0; // Say not found to remove it from list.. [Shinryo]
		}

		// TO-DO: Maybe handle the scenario, if the decoded buffer isn't the same size as expected? [Shinryo]
		decode_zip(decode_buffer, &size, p + sizeof(struct map_cache_map_info), info->len);

		CREATE(m->cell, struct mapcell, size);


		for( xy = 0; xy < size; ++xy )
			m->cell[xy] = map_gat2cell(decode_buffer[xy]);

		return 1;
	}

	return 0; // Not found
}

int map_addmap(char *mapname)
{
	if( strcmpi(mapname,"clear") == 0 ) {
		map_num = 0;
		instance_start = 0;
		return 0;
	}

	if( map_num >= MAX_MAP_PER_SERVER - 1 ) {
		ShowError("Could not add map '"CL_WHITE"%s"CL_RESET"', the limit of maps has been reached.\n",mapname);
		return 1;
	}

	mapindex_getmapname(mapname, map[map_num].name);
	map_num++;
	return 0;
}

static void map_delmapid(int id)
{
	ShowNotice("Removing map [ %s ] from maplist"CL_CLL"\n",map[id].name);
	memmove(map+id, map+id+1, sizeof(map[0])*(map_num-id-1));
	map_num--;
}

int map_delmap(char *mapname)
{
	int i;
	char map_name[MAP_NAME_LENGTH];

	if( strcmpi(mapname, "all") == 0 ) {
		map_num = 0;
		return 0;
	}

	mapindex_getmapname(mapname, map_name);
	for( i = 0; i < map_num; i++ ) {
		if( strcmp(map[i].name, map_name) == 0 ) {
			map_delmapid(i);
			return 1;
		}
	}
	return 0;
}

void do_final_maps(void) {
	int i, v = 0;

	for( i = 0; i < map_num; i++ ) {
		if( map[i].cell )
			aFree(map[i].cell);

		if( map[i].block )
			aFree(map[i].block);

		if( map[i].block_mob )
			aFree(map[i].block_mob);

		if( battle_config.dynamic_mobs ) { //Dynamic mobs flag by [random]
			int j;

			if( map[i].mob_delete_timer != INVALID_TIMER )
				delete_timer(map[i].mob_delete_timer, map_removemobs_timer);
			for( j = 0; j < MAX_MOB_LIST_PER_MAP; j++ )
				if( map[i].moblist[j] )
					aFree(map[i].moblist[j]);
		}

#ifdef ADJUST_SKILL_DAMAGE
		if( map[i].skill_damage.count )
			map_skill_damage_free(&map[i]);
#endif

		if( map[i].unit_count ) {
			for( v = 0; v < map[i].unit_count; v++ )
				aFree(map[i].units[v]);
			if( map[i].units ) {
				aFree(map[i].units);
				map[i].units = NULL;
			}
			map[i].unit_count = 0;
		}

		if( map[i].skill_count ) {
			for( v = 0; v < map[i].skill_count; v++ )
				aFree(map[i].skills[v]);
			if( map[i].skills ) {
				aFree(map[i].skills);
				map[i].skills = NULL;
			}
			map[i].skill_count = 0;
		}

		map_free_questinfo(i);
	}
}

/// Initializes map flags and adjusts them depending on configuration.
void map_flags_init(void) {
	int i, v = 0;

	for( i = 0; i < map_num; i++ ) {
		// Mapflags
		memset(&map[i].flag, 0, sizeof(map[i].flag));

		// Additional mapflag data
		map[i].zone        = 0; // Restricted mapflag zone
		map[i].nocommand   = 0; // Nocommand mapflag level
		map[i].adjust.bexp = 100; // Per map base exp multiplicator
		map[i].adjust.jexp = 100; // Per map job exp multiplicator
		memset(map[i].drop_list, 0, sizeof(map[i].drop_list)); // PVP nightmare drop list

		// Skill damage
#ifdef ADJUST_SKILL_DAMAGE
		memset(&map[i].adjust.damage, 0, sizeof(map[i].adjust.damage));
		if( map[i].skill_damage.count )
			map_skill_damage_free(&map[i]);
#endif

		if( map[i].unit_count ) {
			for( v = 0; v < map[i].unit_count; v++ )
				aFree(map[i].units[v]);
			aFree(map[i].units);
		}
		map[i].units = NULL;
		map[i].unit_count = 0;

		if( map[i].skill_count ) {
			for( v = 0; v < map[i].skill_count; v++ )
				aFree(map[i].skills[v]);
			aFree(map[i].skills);
		}
		map[i].skills = NULL;
		map[i].skill_count = 0;

		// Adjustments
		if( battle_config.pk_mode )
			map[i].flag.pvp = 1; // Make all maps pvp for pk_mode [Valaris]

		map_free_questinfo(i);
	}
}

#define NO_WATER 1000000

/*
 * Reads from the .rsw for each map
 * Returns water height (or NO_WATER if file doesn't exist) or other error is encountered.
 * Assumed path for file is data/mapname.rsw
 * Credits to LittleWolf
 */
int map_waterheight(char *mapname)
{
	char fn[256];
	char *rsw, *found;

	// Look up for the rsw
	sprintf(fn, "data\\%s.rsw", mapname);

	found = grfio_find_file(fn);
	if (found)
		strcpy(fn, found); // Replace with real name

	// Read & convert fn
	rsw = (char *)grfio_read (fn);
	if (rsw) { // Load water height from file
		//FIXME: Casting between integer* and float* which have an incompatible binary data representation.
		int wh = (int)*(float *)(rsw + 166);

		aFree(rsw);
		return wh;
	}
	ShowWarning("Failed to find water level for (%s)\n", mapname, fn);
	return NO_WATER;
}

/*==================================
 * .GAT format
 *----------------------------------*/
int map_readgat (struct map_data *m)
{
	char filename[256];
	uint8 *gat;
	int water_height;
	size_t xy, off, num_cells;

	sprintf(filename, "data\\%s.gat", m->name);

	gat = (uint8 *) grfio_read(filename);
	if (gat == NULL)
		return 0;

	m->xs = *(int32*)(gat+6);
	m->ys = *(int32*)(gat+10);
	num_cells = m->xs * m->ys;
	CREATE(m->cell, struct mapcell, num_cells);

	water_height = map_waterheight(m->name);

	// Set cell properties
	off = 14;
	for( xy = 0; xy < num_cells; ++xy )
	{
		// read cell data
		float height = *(float *)( gat + off      );
		uint32 type = *(uint32 *)( gat + off + 16 );
		off += 20;

		if( type == 0 && water_height != NO_WATER && height > water_height )
			type = 3; // Cell is 0 (walkable) but under water level, set to 3 (walkable water)

		m->cell[xy] = map_gat2cell(type);
	}
	
	aFree(gat);

	return 1;
}

/*======================================
 * Add/Remove map to the map_db
 *--------------------------------------*/
void map_addmap2db(struct map_data *m)
{
	uidb_put(map_db, (unsigned int)m->index, m);
}

void map_removemapdb(struct map_data *m)
{
	uidb_remove(map_db, (unsigned int)m->index);
}

/*======================================
 * Initiate maps loading stage
 *--------------------------------------*/
int map_readallmaps (void)
{
	int i;
	FILE *fp = NULL;
	int maps_removed = 0;
	char *map_cache_buffer = NULL; //Has the uncompressed gat data of all maps, so just one allocation has to be made
	char map_cache_decode_buffer[MAX_MAP_SIZE];

	if( enable_grf )
		ShowStatus("Loading maps (using GRF files)...\n");
	else {
		char mapcachefilepath[254];
		sprintf(mapcachefilepath,"%s/%s%s",db_path,DBPATH,"map_cache.dat");
		ShowStatus("Loading maps (using %s as map cache)...\n", mapcachefilepath);
		if( (fp = fopen(mapcachefilepath, "rb")) == NULL ) {
			ShowFatalError("Unable to open map cache file "CL_WHITE"%s"CL_RESET"\n", mapcachefilepath);
			exit(EXIT_FAILURE); //No use launching server if maps can't be read.
		}

		//Init mapcache data. [Shinryo]
		map_cache_buffer = map_init_mapcache(fp);
		if( !map_cache_buffer ) {
			ShowFatalError("Failed to initialize mapcache data (%s)..\n", mapcachefilepath);
			exit(EXIT_FAILURE);
		}
	}

	for( i = 0; i < map_num; i++ ) {
		size_t size;
		unsigned short idx = 0;

		//Show progress
		if( enable_grf )
			ShowStatus("Loading maps [%i/%i]: %s"CL_CLL"\r", i, map_num, map[i].name);

		//Try to load the map
		if( !(idx = mapindex_name2id(map[i].name)) ||
			!(enable_grf ? map_readgat(&map[i]) : map_readfromcache(&map[i], map_cache_buffer, map_cache_decode_buffer)) ) {
			map_delmapid(i);
			maps_removed++;
			i--;
			continue;
		}

		map[i].index = idx;

		if( uidb_get(map_db,(unsigned int)map_id2index(i)) != NULL ) {
			ShowWarning("Map %s already loaded!"CL_CLL"\n", map[i].name);
			if( map[i].cell ) {
				aFree(map[i].cell);
				map[i].cell = NULL;
			}
			map_delmapid(i);
			maps_removed++;
			i--;
			continue;
		}

		map_addmap2db(&map[i]);

		map[i].m = i;
		memset(map[i].moblist, 0, sizeof(map[i].moblist)); //Initialize moblist [Skotlex]
		map[i].mob_delete_timer = INVALID_TIMER; //Initialize timer [Skotlex]

		map[i].bxs = (map[i].xs + BLOCK_SIZE - 1) / BLOCK_SIZE;
		map[i].bys = (map[i].ys + BLOCK_SIZE - 1) / BLOCK_SIZE;

		size = map[i].bxs * map[i].bys * sizeof(struct block_list *);
		map[i].block = (struct block_list**)aCalloc(size, 1);
		map[i].block_mob = (struct block_list**)aCalloc(size, 1);
	}

	//Intialization and configuration-dependent adjustments of mapflags
	map_flags_init();

	if( !enable_grf ) {
		fclose(fp);

		//The cache isn't needed anymore, so free it. [Shinryo]
		aFree(map_cache_buffer);
	}

	//Finished map loading
	ShowInfo("Successfully loaded '"CL_WHITE"%d"CL_RESET"' maps."CL_CLL"\n",map_num);
	instance_start = map_num; //Next Map Index will be instances

	if( maps_removed )
		ShowNotice("Maps removed: '"CL_WHITE"%d"CL_RESET"'\n",maps_removed);

	return 0;
}

////////////////////////////////////////////////////////////////////////
static int map_ip_set = 0;
static int char_ip_set = 0;

/*==========================================
 * Console Command Parser [Wizputer]
 *------------------------------------------*/
int parse_console(const char *buf) {
	char type[64];
	char command[64];
	char mapname[MAP_NAME_LENGTH];
	int16 x = 0;
	int16 y = 0;
	int16 m;
	int n;
	struct map_session_data sd;

	memset(&sd, 0, sizeof(struct map_session_data));
	strcpy(sd.status.name, "console");

	if( (n = sscanf(buf, "%63[^:]:%63[^:]:%11s %6hd %6hd[^\n]", type, command, mapname, &x, &y)) < 5 ) {
		if( (n = sscanf(buf, "%63[^:]:%63[^\n]", type, command)) < 2 ) {
			if( (n = sscanf(buf, "%63[^\n]", type)) < 1 ) return -1; //nothing to do no arg
		}
	}

	if( n != 5 ) { //end string and display
		if( n < 2 ) {
			ShowNotice("Type of command: '%s'\n", type);
			command[0] = '\0';
			mapname[0] = '\0';
		} else {
			ShowNotice("Type of command: '%s' || Command: '%s'\n", type, command);
			mapname[0] = '\0';
		}
	} else
		ShowNotice("Type of command: '%s' || Command: '%s' || Map: '%s' Coords: %d %d\n", type, command, mapname, x, y);

	if( strcmpi("admin",type) == 0 ) {
		if( strcmpi("map",command) == 0 ) {
			m = map_mapname2mapid(mapname);
			if( m < 0 ) {
				ShowWarning("Console: Unknown map.\n");
				return 0;
			}
			sd.bl.m = m;
			map_search_freecell(&sd.bl, m, &sd.bl.x, &sd.bl.y, -1, -1, 0);
			if( x > 0 )
				sd.bl.x = x;
			if( y > 0 )
				sd.bl.y = y;
			ShowNotice("Now at: '%s' Coords: %d %d\n", mapname, x, y);
		} else if( !is_atcommand(sd.fd, &sd, command, 2) )
			ShowInfo("Console: Invalid atcommand.\n");
	} else if( n == 2 && strcmpi("server", type) == 0 ) {
		if( strcmpi("shutdown", command) == 0 || strcmpi("exit", command) == 0 || strcmpi("quit", command) == 0 ) {
			runflag = 0;
		}
	} else if( strcmpi("ers_report", type) == 0 ) {
		ers_report();
	} else if( strcmpi("help", type) == 0 ) {
		ShowInfo("Available commands:\n");
		ShowInfo("\t admin:@<atcommand> => Uses an atcommand. Do NOT use commands requiring an attached player.\n");
		ShowInfo("\t admin:map:<map> <x> <y> => Changes the map from which console commands are executed.\n");
		ShowInfo("\t server:shutdown => Stops the server.\n");
		ShowInfo("\t ers_report => Displays database usage.\n");
	}

	return 0;
}

/*==========================================
 * Read map server configuration files (conf/map_athena.conf...)
 *------------------------------------------*/
int map_config_read(char *cfgName)
{
	char line[1024], w1[32], w2[1024];
	FILE *fp;

	fp = fopen(cfgName,"r");
	if (fp == NULL) {
		ShowError("Map configuration file not found at: %s\n", cfgName);
		return 1;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *ptr;

		if (line[0] == '/' && line[1] == '/')
			continue;
		if ((ptr = strstr(line, "//")) != NULL)
			*ptr = '\n'; //Strip comments
		if (sscanf(line, "%31[^:]: %1023[^\t\r\n]", w1, w2) < 2)
			continue;

		//Strip trailing spaces
		ptr = w2 + strlen(w2);
		while (--ptr >= w2 && *ptr == ' ');
		ptr++;
		*ptr = '\0';

		if (strcmpi(w1, "timestamp_format") == 0)
			safestrncpy(timestamp_format, w2, 20);
		else if (strcmpi(w1, "stdout_with_ansisequence") == 0)
			stdout_with_ansisequence = config_switch(w2);
		else if (strcmpi(w1, "console_silent") == 0) {
			msg_silent = atoi(w2);
			if (msg_silent) //Only bother if its actually enabled
				ShowInfo("Console Silent Setting: %d\n", atoi(w2));
		} else if (strcmpi(w1, "userid") == 0)
			chrif_setuserid(w2);
		else if (strcmpi(w1, "passwd") == 0)
			chrif_setpasswd(w2);
		else if (strcmpi(w1, "char_ip") == 0)
			char_ip_set = chrif_setip(w2);
		else if (strcmpi(w1, "char_port") == 0)
			chrif_setport(atoi(w2));
		else if (strcmpi(w1, "map_ip") == 0)
			map_ip_set = clif_setip(w2);
		else if (strcmpi(w1, "bind_ip") == 0)
			clif_setbindip(w2);
		else if (strcmpi(w1, "map_port") == 0) {
			clif_setport(atoi(w2));
			map_port = (atoi(w2));
		} else if (strcmpi(w1, "map") == 0)
			map_addmap(w2);
		else if (strcmpi(w1, "delmap") == 0)
			map_delmap(w2);
		else if (strcmpi(w1, "npc") == 0)
			npc_addsrcfile(w2, false);
		else if (strcmpi(w1, "delnpc") == 0)
			npc_delsrcfile(w2);
		else if (strcmpi(w1, "autosave_time") == 0) {
			autosave_interval = atoi(w2);
			if (autosave_interval < 1) //Revert to default saving
				autosave_interval = DEFAULT_AUTOSAVE_INTERVAL;
			else
				autosave_interval *= 1000; //Pass from sec to ms
		} else if (strcmpi(w1, "minsave_time") == 0) {
			minsave_interval= atoi(w2);
			if (minsave_interval < 1)
				minsave_interval = 1;
		} else if (strcmpi(w1, "save_settings") == 0)
			save_settings = atoi(w2);
		else if (strcmpi(w1, "motd_txt") == 0)
			strcpy(motd_txt, w2);
		else if (strcmpi(w1, "help_txt") == 0)
			strcpy(help_txt, w2);
		else if (strcmpi(w1, "help2_txt") == 0)
			strcpy(help2_txt, w2);
		else if (strcmpi(w1, "charhelp_txt") == 0)
			strcpy(charhelp_txt, w2);
		else if (strcmpi(w1, "channel_conf") == 0)
			safestrncpy(channel_conf, w2, sizeof(channel_conf));
		else if(strcmpi(w1,"db_path") == 0)
			safestrncpy(db_path, w2, 255);
		else if (strcmpi(w1, "console") == 0) {
			console = config_switch(w2);
			if (console)
				ShowNotice("Console Commands are enabled.\n");
		} else if (strcmpi(w1, "enable_spy") == 0)
			enable_spy = config_switch(w2);
		else if (strcmpi(w1, "use_grf") == 0)
			enable_grf = config_switch(w2);
		else if (strcmpi(w1, "console_msg_log") == 0)
			console_msg_log = atoi(w2); //[Ind]
		else if (strcmpi(w1, "console_log_filepath") == 0)
			safestrncpy(console_log_filepath, w2, sizeof(console_log_filepath));
		else if (strcmpi(w1, "import") == 0)
			map_config_read(w2);
		else
			ShowWarning("Unknown setting '%s' in file %s\n", w1, cfgName);
	}

	fclose(fp);
	return 0;
}

void map_reloadnpc_sub(char *cfgName)
{
	char line[1024], w1[1024], w2[1024];
	FILE *fp;

	fp = fopen(cfgName,"r");
	if (fp == NULL) {
		ShowError("Map configuration file not found at: %s\n", cfgName);
		return;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *ptr;

		if (line[0] == '/' && line[1] == '/')
			continue;
		if ((ptr = strstr(line, "//")) != NULL)
			*ptr = '\n'; //Strip comments
		if (sscanf(line, "%1023[^:]: %1023[^\t\r\n]", w1, w2) < 2)
			continue;

		//Strip trailing spaces
		ptr = w2 + strlen(w2);
		while (--ptr >= w2 && *ptr == ' ');
		ptr++;
		*ptr = '\0';
			
		if (strcmpi(w1, "npc") == 0)
			npc_addsrcfile(w2, false);
		else if (strcmpi(w1, "delnpc") == 0)
			npc_delsrcfile(w2);
		else if (strcmpi(w1, "import") == 0)
			map_reloadnpc_sub(w2);
		else
			ShowWarning("Unknown setting '%s' in file %s\n", w1, cfgName);
	}

	fclose(fp);
}

void map_reloadnpc(bool clear)
{
	if (clear)
		npc_addsrcfile("clear", false); //This will clear the current script list

#ifdef RENEWAL
	map_reloadnpc_sub("npc/re/scripts_main.conf");
#else
	map_reloadnpc_sub("npc/pre-re/scripts_main.conf");
#endif
}

int inter_config_read(char *cfgName)
{
	char line[1024], w1[1024], w2[1024];
	FILE *fp;

	fp = fopen(cfgName, "r");
	if( fp == NULL ) {
		ShowError("File not found: %s\n", cfgName);
		return 1;
	}
	while( fgets(line, sizeof(line), fp) ) {
		if( line[0] == '/' && line[1] == '/' )
			continue;
		if( sscanf(line, "%1023[^:]: %1023[^\r\n]", w1, w2) < 2 )
			continue;

		if( strcmpi(w1, "buyingstores_db") == 0 )
			strcpy(buyingstores_db, w2);
		else if( strcmpi(w1, "buyingstore_items_db") == 0 )
			strcpy(buyingstore_items_db, w2);
		else if( strcmpi(w1, "guild_storage_log") == 0 )
			strcpy(guild_storage_log_db, w2);
		else if( strcmpi(w1, "item_cash_db_db") == 0 )
			strcpy(item_cash_db_db, w2);
		else if( strcmpi(w1, "item_cash_db2_db") == 0 )
			strcpy(item_cash_db2_db, w2);
		else if( strcmpi(w1, "item_db_db") == 0 )
			strcpy(item_db_db, w2);
		else if( strcmpi(w1, "item_db2_db") == 0 )
			strcpy(item_db2_db, w2);
		else if( strcmpi(w1, "item_db_re_db") == 0 )
			strcpy(item_db_re_db, w2);
		else if( strcmpi(w1, "markets_db") == 0 )
			strcpy(markets_db, w2);
		else if( strcmpi(w1, "mob_db_db") == 0 )
			strcpy(mob_db_db, w2);
		else if( strcmpi(w1, "mob_db_re_db") == 0 )
			strcpy(mob_db_re_db, w2);
		else if( strcmpi(w1, "mob_db2_db") == 0 )
			strcpy(mob_db2_db, w2);
		else if( strcmpi(w1, "mob_skill_db_db") == 0 )
			strcpy(mob_skill_db_db, w2);
		else if( strcmpi(w1, "mob_skill_db_re_db") == 0 )
			strcpy(mob_skill_db_re_db, w2);
		else if( strcmpi(w1, "mob_skill_db2_db") == 0 )
			strcpy(mob_skill_db2_db, w2);
		else if( strcmpi(w1, "sales_db") == 0 )
			strcpy(sales_db, w2);
		else if( strcmpi(w1, "vendings_db") == 0 )
			strcpy(vendings_db, w2);
		else if( strcmpi(w1, "vending_items_db") == 0 )
			strcpy(vending_items_db, w2);
		//Map Server SQL DB
		else if( strcmpi(w1, "map_server_ip") == 0 )
			strcpy(map_server_ip, w2);
		else if( strcmpi(w1, "map_server_port") == 0 )
			map_server_port = atoi(w2);
		else if( strcmpi(w1, "map_server_id") == 0 )
			strcpy(map_server_id, w2);
		else if( strcmpi(w1, "map_server_pw") == 0 )
			strcpy(map_server_pw, w2);
		else if( strcmpi(w1, "map_server_db") == 0 )
			strcpy(map_server_db, w2);
		else if( strcmpi(w1, "default_codepage") == 0 )
			strcpy(default_codepage, w2);
		else if( strcmpi(w1, "use_sql_db") == 0 ) {
			db_use_sqldbs = config_switch(w2);
			ShowStatus ("Using SQL dbs: %s\n", w2);
		} else if( strcmpi(w1, "log_db_ip") == 0 )
			strcpy(log_db_ip, w2);
		else if( strcmpi(w1, "log_db_id") == 0 )
			strcpy(log_db_id, w2);
		else if( strcmpi(w1, "log_db_pw") == 0 )
			strcpy(log_db_pw, w2);
		else if( strcmpi(w1, "log_db_port") == 0 )
			log_db_port = atoi(w2);
		else if( strcmpi(w1, "log_db_db") == 0 )
			strcpy(log_db_db, w2);
		else if( mapreg_config_read(w1, w2) )
			continue;
		//Support the import command, just like any other config
		else if( strcmpi(w1, "import") == 0 )
			inter_config_read(w2);
	}
	fclose(fp);

	return 0;
}

/*=======================================
 *  MySQL Init
 *---------------------------------------*/
int map_sql_init(void)
{
	// main db connection
	mmysql_handle = Sql_Malloc();
	qsmysql_handle = Sql_Malloc();

	ShowInfo("Connecting to the Map DB Server....\n");
	if( SQL_ERROR == Sql_Connect(mmysql_handle, map_server_id, map_server_pw, map_server_ip, map_server_port, map_server_db) ||
		SQL_ERROR == Sql_Connect(qsmysql_handle, map_server_id, map_server_pw, map_server_ip, map_server_port, map_server_db) ) {
		ShowError("Couldn't connect with uname='%s',passwd='%s',host='%s',port='%d',database='%s'\n",
			map_server_id, map_server_pw, map_server_ip, map_server_port, map_server_db);
		Sql_ShowDebug(mmysql_handle);
		Sql_Free(mmysql_handle);
		Sql_ShowDebug(qsmysql_handle);
		Sql_Free(qsmysql_handle);
		exit(EXIT_FAILURE);
	}
	ShowStatus("Connect success! (Map Server Connection)\n");

	if( strlen(default_codepage) > 0 ) {
		if( SQL_ERROR == Sql_SetEncoding(mmysql_handle, default_codepage) )
			Sql_ShowDebug(mmysql_handle);
		if( SQL_ERROR == Sql_SetEncoding(qsmysql_handle, default_codepage) )
			Sql_ShowDebug(qsmysql_handle);
	}

	return 0;
}

int map_sql_close(void)
{
	ShowStatus("Close Map DB Connection....\n");
	Sql_Free(mmysql_handle);
	Sql_Free(qsmysql_handle);
	mmysql_handle = NULL;
	qsmysql_handle = NULL;
#ifndef BETA_THREAD_TEST
	if( log_config.sql_logs ) {
		ShowStatus("Close Log DB Connection....\n");
		Sql_Free(logmysql_handle);
		logmysql_handle = NULL;
	}
#endif
	return 0;
}

int log_sql_init(void)
{
#ifndef BETA_THREAD_TEST
	// log db connection
	logmysql_handle = Sql_Malloc();

	ShowInfo(""CL_WHITE"[SQL]"CL_RESET": Connecting to the Log Database "CL_WHITE"%s"CL_RESET" At "CL_WHITE"%s"CL_RESET"...\n",log_db_db,log_db_ip);
	if( SQL_ERROR == Sql_Connect(logmysql_handle, log_db_id, log_db_pw, log_db_ip, log_db_port, log_db_db) )
		exit(EXIT_FAILURE);
	ShowStatus(""CL_WHITE"[SQL]"CL_RESET": Successfully '"CL_GREEN"connected"CL_RESET"' to Database '"CL_WHITE"%s"CL_RESET"'.\n", log_db_db);

	if( strlen(default_codepage) > 0 )
		if ( SQL_ERROR == Sql_SetEncoding(logmysql_handle, default_codepage) )
			Sql_ShowDebug(logmysql_handle);
#endif
	return 0;
}

struct questinfo *map_add_questinfo(int m, struct questinfo *qi) {
	unsigned short i;

	for( i = 0; i < map[m].qi_count; i++ )
		if( &map[m].qi_data[i] && map[m].qi_data[i].nd == qi->nd && map[m].qi_data[i].quest_id == qi->quest_id )
			break; //Duplicate, override

	if( i == map[m].qi_count )
		RECREATE(map[m].qi_data, struct questinfo, ++map[m].qi_count);
	else { //Clear previous criteria on override
		if( map[m].qi_data[i].jobid )
			aFree(map[m].qi_data[i].jobid);
		map[m].qi_data[i].jobid = NULL;
		map[m].qi_data[i].jobid_count = 0;
		if( map[m].qi_data[i].req )
			aFree(map[m].qi_data[i].req);
		map[m].qi_data[i].req = NULL;
		map[m].qi_data[i].req_count = 0;
	}

	memcpy(&map[m].qi_data[i], qi, sizeof(struct questinfo));
	return &map[m].qi_data[i];
}

bool map_remove_questinfo(int m, struct npc_data *nd) {
	unsigned short i, c;

	for( i = 0; i < map[m].qi_count; i++ ) {
		struct questinfo *qi = &map[m].qi_data[i];

		if( qi->nd == nd ) {
			if( qi->jobid )
				aFree(qi->jobid);
			qi->jobid = NULL;
			qi->jobid_count = 0;
			if( qi->req )
				aFree(qi->req);
			qi->req = NULL;
			qi->req_count = 0;
			memset(&map[m].qi_data[i], 0, sizeof(map[m].qi_data[i]));
		}
	}

	for( i = 0, c = 0; i < map[m].qi_count; i++ ) { //Move next data to empty slot
		struct questinfo *qi = &map[m].qi_data[i];

		if( !qi || !qi->nd )
			continue;
		if( i != c ) {
			map[m].qi_data[c] = map[m].qi_data[i];
			memset(&map[m].qi_data[i], 0, sizeof(map[m].qi_data[i]));
		}
		c++;
	}

	if( !(map[m].qi_count = c) ) {
		aFree(map[m].qi_data);
		map[m].qi_data = NULL;
	} else
		RECREATE(map[m].qi_data, struct questinfo, map[m].qi_count);
	return true;
}

static void map_free_questinfo(int m) {
	unsigned short i;

	for( i = 0; i < map[m].qi_count; i++ ) {
		if( map[m].qi_data[i].jobid )
			aFree(map[m].qi_data[i].jobid);
		map[m].qi_data[i].jobid = NULL;
		map[m].qi_data[i].jobid_count = 0;
		if( map[m].qi_data[i].req )
			aFree(map[m].qi_data[i].req);
		map[m].qi_data[i].req = NULL;
		map[m].qi_data[i].req_count = 0;
	}
	aFree(map[m].qi_data);
	map[m].qi_data = NULL;
	map[m].qi_count = 0;
}

struct questinfo *map_has_questinfo(int m, struct npc_data *nd, int quest_id) {
	unsigned short i;

	for( i = 0; i < map[m].qi_count; i++ ) {
		struct questinfo *qi = &map[m].qi_data[i];

		if( qi->nd == nd && qi->quest_id == quest_id )
			return qi;
	}
	return NULL;
}

/**
 * @see DBApply
 */
int map_db_final(DBKey key, DBData *data, va_list ap)
{
	struct map_data_other_server *mdos = db_data2ptr(data);

	if( mdos && mdos->cell == NULL )
		aFree(mdos);
	return 0;
}

/**
 * @see DBApply
 */
int nick_db_final(DBKey key, DBData *data, va_list args)
{
	struct charid2nick *p = db_data2ptr(data);
	struct charid_request* req;

	if( p == NULL )
		return 0;
	while( p->requests ) {
		req = p->requests;
		p->requests = req->next;
		aFree(req);
	}
	aFree(p);
	return 0;
}

int cleanup_sub(struct block_list *bl, va_list ap)
{
	nullpo_ret(bl);

	switch( bl->type ) {
		case BL_PC:
			map_quit((struct map_session_data *) bl);
			break;
		case BL_NPC:
			npc_unload((struct npc_data *)bl,false);
			break;
		case BL_MOB:
			unit_free(bl,CLR_OUTSIGHT);
			break;
		case BL_PET:
		//There is no need for this, the pet is removed together with the player. [Skotlex]
			break;
		case BL_ITEM:
			map_clearflooritem(bl);
			break;
		case BL_SKILL:
			skill_delunit((struct skill_unit *)bl);
			break;
	}

	return 1;
}

#ifdef ADJUST_SKILL_DAMAGE
/**
 * Free all skill damage entries for a map
 * @param m Map data
 */
void map_skill_damage_free(struct map_data *m) {
	uint8 i;

	for( i = 0; i < m->skill_damage.count; i++ ) {
		ers_free(map_skill_damage_ers, m->skill_damage.entries[i]);
		m->skill_damage.entries[i] = NULL;
	}

	aFree(m->skill_damage.entries);
	m->skill_damage.entries = NULL;
	m->skill_damage.count = 0;
}

/**
 * Add new skill damage adjustment entry for a map
 * @param m Map data
 * @param skill_id Skill
 * @param pc Rate to PC
 * @param mobs Rate to Monster
 * @param boss Rate to Boss-monster
 * @param other Rate to Other target
 * @param caster Caster type
 */
void map_skill_damage_add(struct map_data *m, uint16 skill_id, int pc, int mob, int boss, int other, uint8 caster) {
	struct s_skill_damage *entry;
	int i = 0;

	if( m->skill_damage.count >= UINT8_MAX )
		return;

	for( i = 0; i < m->skill_damage.count; i++ ) {
		if( m->skill_damage.entries[i]->skill_id == skill_id ) {
			m->skill_damage.entries[i]->pc = pc;
			m->skill_damage.entries[i]->mob = mob;
			m->skill_damage.entries[i]->boss = boss;
			m->skill_damage.entries[i]->other = other;
			m->skill_damage.entries[i]->caster = caster;
			return;
		}
	}

	entry = ers_alloc(map_skill_damage_ers, struct s_skill_damage);
	entry->skill_id = skill_id;
	entry->pc = pc;
	entry->mob = mob;
	entry->boss = boss;
	entry->other = other;
	entry->caster = caster;

	RECREATE(m->skill_damage.entries, struct s_skill_damage *, m->skill_damage.count + 1);
	m->skill_damage.entries[m->skill_damage.count++] = entry;
}
#endif

/**
 * PvP timer handling (starting)
 * @param bl: Player block object
 * @param ap: func* with va_list values
 * @return 0
 */
int map_mapflag_pvp_start(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd = map_id2sd(bl->id);

	nullpo_retr(0, sd);

	if( sd->pvp_timer == INVALID_TIMER ) {
		sd->pvp_timer = add_timer(gettick() + 200, pc_calc_pvprank_timer, sd->bl.id, 0);
		sd->pvp_rank = 0;
		sd->pvp_lastusers = 0;
		sd->pvp_point = 5;
		sd->pvp_won = 0;
		sd->pvp_lost = 0;
	}
	clif_map_property(&sd->bl, MAPPROPERTY_FREEPVPZONE, SELF);
	return 0;
}

/**
 * PvP timer handling (stopping)
 * @param bl: Player block object
 * @param ap: func* with va_list values
 * @return 0
 */
int map_mapflag_pvp_stop(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd = map_id2sd(bl->id);

	nullpo_retr(0, sd);

	clif_pvpset(sd, 0, 0, 2);
	if( sd->pvp_timer != INVALID_TIMER ) {
		delete_timer(sd->pvp_timer, pc_calc_pvprank_timer);
		sd->pvp_timer = INVALID_TIMER;
	}
	return 0;
}

/**
 * @see DBApply
 */
static int cleanup_db_sub(DBKey key, DBData *data, va_list va)
{
	return cleanup_sub(db_data2ptr(data), va);
}

/*==========================================
 * map destructor
 *------------------------------------------*/
void do_final(void)
{
	int i;
	struct map_session_data *sd;
	struct s_mapiterator *iter;

	ShowStatus("Terminating...\n");
	channel_config.closing = true;

	//Ladies and babies first.
	iter = mapit_getallusers();
	for( sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC *)mapit_next(iter) )
		map_quit(sd);
	mapit_free(iter);

	//Prepares npcs for a faster shutdown process
	do_clear_npc();

	//Remove all objects on maps
	for( i = 0; i < map_num; i++ ) {
		ShowStatus("Cleaning up maps [%d/%d]: %s..."CL_CLL"\r", i + 1, map_num, map[i].name);
		if( map[i].m >= 0 ) {
			map_foreachinmap(cleanup_sub, i, BL_ALL);
			channel_delete(map[i].channel, false);
		}
	}
	ShowStatus("Cleaned up %d maps."CL_CLL"\n", map_num);

	id_db->foreach(id_db,cleanup_db_sub);
	chrif_char_reset_offline();
	chrif_flush_fifo();

	do_final_atcommand();
	do_final_battle();
	do_final_chrif();
	do_final_clan();;
	do_final_clif();
	do_final_npc();
	do_final_quest();
	do_final_achievement();
	do_final_script();
	do_final_instance();
	do_final_itemdb();
	do_final_storage();
	do_final_guild();
	do_final_party();
	do_final_pc();
	do_final_pet();
	do_final_homunculus();
	do_final_mercenary();
	do_final_mob();
	do_final_msg();
	do_final_skill();
	do_final_status();
	do_final_unit();
	do_final_battleground();
	do_final_duel();
	do_final_elemental();
	do_final_cashshop();
	do_final_channel(); // Should be called after final guild
	do_final_vending();
	do_final_buyingstore();
	do_final_maps();
	do_final_path();

	map_db->destroy(map_db, map_db_final);

	mapindex_final();
	if( enable_grf )
		grfio_final();

	id_db->destroy(id_db, NULL);
	pc_db->destroy(pc_db, NULL);
	mobid_db->destroy(mobid_db, NULL);
	bossid_db->destroy(bossid_db, NULL);
	nick_db->destroy(nick_db, nick_db_final);
	charid_db->destroy(charid_db, NULL);
	iwall_db->destroy(iwall_db, NULL);
	regen_db->destroy(regen_db, NULL);

#ifdef ADJUST_SKILL_DAMAGE
	ers_destroy(map_skill_damage_ers);
#endif

	map_sql_close();

	ShowStatus("Finished.\n");
}

static int map_abort_sub(struct map_session_data *sd, va_list ap)
{
	chrif_save(sd,CSAVE_QUIT|CSAVE_INVENTORY|CSAVE_CART);
	return 1;
}


//------------------------------
// Function called when the server
// has received a crash signal.
//------------------------------
void do_abort(void)
{
	static int run = 0;
	//Save all characters and then flush the inter-connection.
	if (run) {
		ShowFatalError("Server has crashed while trying to save characters. Character data can't be saved!\n");
		return;
	}
	run = 1;
	if (!chrif_isconnected()) {
		if (pc_db->size(pc_db))
			ShowFatalError("Server has crashed without a connection to the char-server, %u characters can't be saved!\n", pc_db->size(pc_db));
		return;
	}
	ShowError("Server received crash signal! Attempting to save all online characters!\n");
	map_foreachpc(map_abort_sub);
	chrif_flush_fifo();
}

/*======================================================
 * Map-Server help options screen
 *------------------------------------------------------*/
void display_helpscreen(bool do_exit)
{
	ShowInfo("Usage: %s [options]\n", SERVER_NAME);
	ShowInfo("\n");
	ShowInfo("Options:\n");
	ShowInfo("  -?, -h [--help]\t\tDisplays this help screen.\n");
	ShowInfo("  -v [--version]\t\tDisplays the server's version.\n");
	ShowInfo("  --run-once\t\t\tCloses server after loading (testing).\n");
	ShowInfo("  --map-config <file>\t\tAlternative map-server configuration.\n");
	ShowInfo("  --battle-config <file>\tAlternative battle configuration.\n");
	ShowInfo("  --atcommand-config <file>\tAlternative atcommand configuration.\n");
	ShowInfo("  --script-config <file>\tAlternative script configuration.\n");
	ShowInfo("  --msg-config <file>\t\tAlternative message configuration.\n");
	ShowInfo("  --grf-path <file>\t\tAlternative GRF path configuration.\n");
	ShowInfo("  --inter-config <file>\t\tAlternative inter-server configuration.\n");
	ShowInfo("  --log-config <file>\t\tAlternative logging configuration.\n");
	if( do_exit )
		exit(EXIT_SUCCESS);
}

/*======================================================
 * Map-Server Init and Command-line Arguments [Valaris]
 *------------------------------------------------------*/
void set_server_type(void)
{
	SERVER_TYPE = ATHENA_SERVER_MAP;
}


/// Called when a terminate signal is received.
void do_shutdown(void)
{
	if( runflag != MAPSERVER_ST_SHUTDOWN ) {
		runflag = MAPSERVER_ST_SHUTDOWN;
		ShowStatus("Shutting down...\n"); {
			struct map_session_data *sd;
			struct s_mapiterator *iter = mapit_getallusers();

			for( sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC *)mapit_next(iter) )
				clif_GM_kick(NULL, sd);
			mapit_free(iter);
			flush_fifos();
		}
		chrif_check_shutdown();
	}
}

int do_init(int argc, char *argv[])
{
	runflag = MAPSERVER_ST_STARTING;
#ifdef GCOLLECT
	GC_enable_incremental();
#endif

	INTER_CONF_NAME="conf/inter_athena.conf";
	LOG_CONF_NAME="conf/log_athena.conf";
	MAP_CONF_NAME = "conf/map_athena.conf";
	BATTLE_CONF_FILENAME = "conf/battle_athena.conf";
	ATCOMMAND_CONF_FILENAME = "conf/atcommand_athena.conf";
	SCRIPT_CONF_NAME = "conf/script_athena.conf";
	MSG_CONF_NAME = "conf/msg_conf/map_msg.conf";
	GRF_PATH_FILENAME = "conf/grf-files.txt";
	safestrncpy(console_log_filepath, "./log/map-msg_log.log", sizeof(console_log_filepath));

	// Default map
	safestrncpy(map_default.mapname, "prontera", MAP_NAME_LENGTH);
	map_default.x = 156;
	map_default.y = 191;

	cli_get_options(argc,argv);

	rnd_init();
	map_config_read(MAP_CONF_NAME);

	if (save_settings&CHARSAVE_NONE)
		ShowWarning("Value of 'save_settings' is not set, player's data only will be saved every 'autosave_time' (%d seconds).\n", autosave_interval / 1000);

	// Loads npcs
	map_reloadnpc(false);

	chrif_checkdefaultlogin();

	if (!map_ip_set || !char_ip_set) {
		char ip_str[16];

		ip2str(addr_[0], ip_str);

		if (runflag != CORE_ST_STOP) // Skip this warning if the server is run with run-once flag
			ShowWarning("Not all IP addresses in map_athena.conf configured, autodetecting...\n");

		if (!naddr_)
			ShowError("Unable to determine your IP address...\n");
		else if (naddr_ > 1)
			ShowNotice("Multiple interfaces detected...\n");

		ShowInfo("Defaulting to %s as our IP address\n", ip_str);

		if (!map_ip_set)
			clif_setip(ip_str);
		if (!char_ip_set)
			chrif_setip(ip_str);
	}

	battle_config_read(BATTLE_CONF_FILENAME);
	msg_config_read(MSG_CONF_NAME);
	script_config_read(SCRIPT_CONF_NAME);
	inter_config_read(INTER_CONF_NAME);
	log_config_read(LOG_CONF_NAME);

	id_db = idb_alloc(DB_OPT_BASE);
	pc_db = idb_alloc(DB_OPT_BASE);	//Added for reliable map_id2sd() use. [Skotlex]
	mobid_db = idb_alloc(DB_OPT_BASE);	//Added to lower the load of the lazy mob ai. [Skotlex]
	bossid_db = idb_alloc(DB_OPT_BASE); // Used for Convex Mirror quick MVP search
	map_db = uidb_alloc(DB_OPT_BASE);
	nick_db = idb_alloc(DB_OPT_BASE);
	charid_db = idb_alloc(DB_OPT_BASE);
	regen_db = idb_alloc(DB_OPT_BASE); // efficient status_natural_heal processing

	iwall_db = strdb_alloc(DB_OPT_RELEASE_DATA, 2 * NAME_LENGTH + 2 + 1); // [Zephyrus] Invisible Walls

#ifdef ADJUST_SKILL_DAMAGE
	map_skill_damage_ers = ers_new(sizeof(struct s_skill_damage), "map.c::map_skill_damage_ers", ERS_OPT_NONE);
#endif

	map_sql_init();
	if (log_config.sql_logs)
		log_sql_init();

	mapindex_init();
	if (enable_grf)
		grfio_init(GRF_PATH_FILENAME);

	map_readallmaps();

	add_timer_func_list(map_freeblock_timer, "map_freeblock_timer");
	add_timer_func_list(map_clearflooritem_timer, "map_clearflooritem_timer");
	add_timer_func_list(map_removemobs_timer, "map_removemobs_timer");
	add_timer_interval(gettick() + 1000, map_freeblock_timer, 0, 0, 60 * 1000);

	do_init_path();
	do_init_atcommand();
	do_init_battle();
	do_init_instance();
	do_init_chrif();
	do_init_clan();
	do_init_clif();
	do_init_script();
	do_init_itemdb();
	do_init_channel();
	do_init_cashshop();
	do_init_skill();
	do_init_mob();
	do_init_pc();
	do_init_status();
	do_init_party();
	do_init_guild();
	do_init_storage();
	do_init_pet();
	do_init_homunculus();
	do_init_mercenary();
	do_init_elemental();
	do_init_quest();
	do_init_achievement();
	do_init_npc();
	do_init_unit();
	do_init_battleground();
	do_init_duel();
	do_init_vending();
	do_init_buyingstore();

	npc_event_do_oninit(false);	// Init npcs (OnInit)

	if (battle_config.pk_mode)
		ShowNotice("Server is running on '"CL_WHITE"PK Mode"CL_RESET"'.\n");

	Sql_HerculesUpdateCheck(mmysql_handle);

	ShowStatus("Server is '"CL_GREEN"ready"CL_RESET"' and listening on port '"CL_WHITE"%d"CL_RESET"'.\n\n", map_port);

	if (runflag != CORE_ST_STOP) {
		shutdown_callback = do_shutdown;
		runflag = MAPSERVER_ST_RUNNING;
	}
#if defined(BUILDBOT)
	if (buildbotflag)
		exit(EXIT_FAILURE);
#endif

	if (console) { // Start listening
		add_timer_func_list(parse_console_timer, "parse_console_timer");
		add_timer_interval(gettick() + 1000, parse_console_timer, 0, 0, 1000); // Start in 1s each 1sec
	}

	return 0;
}

int map_msg_config_read(char *cfgName) {
	return _msg_config_read(cfgName,MAP_MAX_MSG,msg_table);
}
const char *map_msg_txt(int msg_number) {
	return _msg_txt(msg_number,MAP_MAX_MSG,msg_table);
}
void map_do_final_msg(void) {
	_do_final_msg(MAP_MAX_MSG,msg_table);
}
