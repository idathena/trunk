// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "itemdb.h"
#include "map.h"
#include "battle.h" // struct battle_config
#include "cashshop.h"
#include "script.h" // item script processing
#include "pc.h"     // W_MUSICAL, W_WHIP
#include "intif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DBMap *itemdb; //Item DB
static DBMap *itemdb_combo; //Item Combo DB
static DBMap *itemdb_group; //Item Group DB
static DBMap *itemdb_randomopt; //Random option DB
static DBMap *itemdb_randomopt_group; //Random option group DB

struct item_data *dummy_item; //This is the default dummy item used for non-existant items [Skotlex]

struct s_roulette_db rd;

/**
 * Check if combo exists
 * @param combo_id
 * @return NULL if not exist, or struct item_combo*
 */
struct item_combo *itemdb_combo_exists(unsigned short combo_id) {
	return (struct item_combo *)uidb_get(itemdb_combo, combo_id);
}

/**
 * Check if item group exists
 * @param group_id
 * @return NULL if not exist, or s_item_group_db *
 */
struct s_item_group_db *itemdb_group_exists(unsigned short group_id) {
	return (struct s_item_group_db *)uidb_get(itemdb_group, group_id);
}

/**
 * Search for item name
 * name = item alias, so we should find items aliases first. if not found then look for "jname" (full name)
 * @see DBApply
 */
static int itemdb_searchname_sub(DBKey key, DBData *data, va_list ap)
{
	struct item_data *item = db_data2ptr(data), **dst, **dst2;
	char *str;

	str = va_arg(ap, char *);
	dst = va_arg(ap, struct item_data **);
	dst2 = va_arg(ap, struct item_data **);

	//Absolute priority to Aegis code name
	if (strcmpi(item->name, str) == 0)
		*dst = item;

	//Second priority to Client displayed name
	if (strcmpi(item->jname, str) == 0)
		*dst2 = item;
	return 0;
}

/**
 * Return item data from item name. (lookup)
 * @param str Item Name
 * @return item data
 */
struct item_data *itemdb_searchname(const char *str)
{
	struct item_data *item = NULL, *item2 = NULL;

	itemdb->foreach(itemdb, itemdb_searchname_sub, str, &item, &item2);
	return (item ? item : item2);
}

/**
 * @see DBMatcher
 */
static int itemdb_searchname_array_sub(DBKey key, DBData data, va_list ap)
{
	struct item_data *item = (struct item_data *)db_data2ptr(&data);
	char *str = va_arg(ap, char *);

	if (stristr(item->jname, str))
		return 0;
	if (stristr(item->name, str))
		return 0;
	return strcmpi(item->jname, str);
}

/**
 * Founds up to N matches. Returns number of matches [Skotlex]
 * @param *data
 * @param size
 * @param str
 * @return Number of matches item
 */
int itemdb_searchname_array(struct item_data **data, int size, const char *str)
{
	DBData *db_data[MAX_SEARCH];
	int i, count = 0, db_count;

	db_count = itemdb->getall(itemdb, (DBData **)&db_data, size, itemdb_searchname_array_sub, str);
	for (i = 0; i < db_count && count < size; i++)
		data[count++] = db_data2ptr(db_data[i]);
	return count;
}

/**
 * Return a random group entry from Item Group
 * @param group_id
 * @param sub_group: 0 is 'must' item group, random groups start from 1 to MAX_ITEMGROUP_RANDGROUP + 1
 * @return Item group entry or NULL on fail
 */
struct s_item_group_entry *itemdb_get_randgroupitem(uint16 group_id, uint8 sub_group) {
	struct s_item_group_db *group = (struct s_item_group_db *)uidb_get(itemdb_group, group_id);
	struct s_item_group_entry *list = NULL;
	uint16 qty = 0;

	if (!group) {
		ShowError("itemdb_get_randgroupitem: Invalid group id %d\n", group_id);
		return NULL;
	}
	if (sub_group > MAX_ITEMGROUP_RANDGROUP + 1) {
		ShowError("itemdb_get_randgroupitem: Invalid sub_group %d\n", sub_group);
		return NULL;
	}
	if (sub_group == 0) {
		list = group->must;
		qty = group->must_qty;
	} else {
		list = group->random[sub_group - 1].data;
		qty = group->random[sub_group - 1].data_qty;
	}
	if (!qty) {
		ShowError("itemdb_get_randgroupitem: No item entries for group id %d and sub group %d\n", group_id, sub_group);
		return NULL;
	}
	return &list[rnd()%qty];
}

/**
 * Return a random Item ID from from Item Group
 * @param group_id
 * @param sub_group: 0 is 'must' item group, random groups start from 1 to MAX_ITEMGROUP_RANDGROUP + 1
 * @return Item ID or UNKNOWN_ITEM_ID on fail
 */
unsigned short itemdb_searchrandomid(uint16 group_id, uint8 sub_group) {
	struct s_item_group_entry *entry = itemdb_get_randgroupitem(group_id, sub_group);

	return (entry ? entry->nameid : UNKNOWN_ITEM_ID);
}

/** [Cydh]
 * Gives item(s) to the player based on item group
 * @param sd: Player that obtains item from item group
 * @param group_id: The group ID of item that obtained by player
 * @param *group: struct s_item_group from itemgroup_db[group_id].random[idx] or itemgroup_db[group_id].must[sub_group][idx]
 */
static void itemdb_pc_get_itemgroup_sub(struct map_session_data *sd, struct s_item_group_entry *data) {
	uint16 i, get_amt = 0;
	struct item tmp;

	nullpo_retv(data);

	memset(&tmp, 0, sizeof(tmp));

	tmp.nameid = data->nameid;
	tmp.bound = data->bound;
	tmp.identify = 1;
	tmp.expire_time = (data->duration) ? (unsigned int)(time(NULL) + data->duration * 60) : 0;
	if (data->isNamed) {
		tmp.card[0] = (itemdb_isequip(data->nameid) ? CARD0_FORGE : CARD0_CREATE);
		tmp.card[1] = 0;
		tmp.card[2] = GetWord(sd->status.char_id, 0);
		tmp.card[3] = GetWord(sd->status.char_id, 1);
	}

	if (!itemdb_isstackable(data->nameid))
		get_amt = 1;
	else
		get_amt = data->amount;

	//Do loop for non-stackable item
	for (i = 0; i < data->amount; i += get_amt) {
		char flag = 0;

		tmp.unique_id = (data->GUID ? pc_generate_unique_id(sd) : 0); //Generate GUID
		if ((flag = pc_additem(sd, &tmp, get_amt, LOG_TYPE_SCRIPT))) {
			clif_additem(sd, 0, 0, flag);
			if (pc_candrop(sd, &tmp))
				map_addflooritem(&tmp, tmp.amount, sd->bl.m, sd->bl.x,sd->bl.y, 0, 0, 0, 0, 0);
		} else if (!flag && data->isAnnounced)
			intif_broadcast_obtain_special_item(sd, data->nameid, sd->itemid, ITEMOBTAIN_TYPE_BOXITEM);
	}
}

/** [Cydh]
 * Find item(s) that will be obtained by player based on Item Group
 * @param group_id: The group ID that will be gained by player
 * @param nameid: The item that trigger this item group
 * @return val: 0: success, 1: no sd, 2: invalid item group
 */
char itemdb_pc_get_itemgroup(uint16 group_id, struct map_session_data *sd) {
	uint16 i = 0;
	struct s_item_group_db *group;

	nullpo_retr(1, sd);

	if (!(group = (struct s_item_group_db *)uidb_get(itemdb_group, group_id))) {
		ShowError("itemdb_pc_get_itemgroup: Invalid group id '%d' specified.", group_id);
		return 2;
	}

	// Get the 'must' item(s)
	if (group->must_qty) {
		for (i = 0; i < group->must_qty; i++)
			if (&group->must[i])
				itemdb_pc_get_itemgroup_sub(sd, &group->must[i]);
	}

	// Get the 'random' item each random group
	for (i = 0; i < MAX_ITEMGROUP_RANDGROUP; i++) {
		uint16 rand;

		if (!(&group->random[i]) || !group->random[i].data_qty) //Skip empty random group
			continue;
		rand = rnd()%group->random[i].data_qty;
		if (!(&group->random[i].data[rand]) || !group->random[i].data[rand].nameid)
			continue;
		itemdb_pc_get_itemgroup_sub(sd,&group->random[i].data[rand]);
	}

	return 0;
}

/** Searches for the item_data. Use this to check if item exists or not.
 * @param nameid
 * @return *item_data if item is exist, or NULL if not
 */
struct item_data *itemdb_exists(unsigned short nameid) {
	return ((struct item_data *)uidb_get(itemdb,nameid));
}

/// Returns human readable name for given item type.
/// @param type Type id to retrieve name for ( IT_* ).
const char *itemdb_typename(enum item_types type)
{
	switch (type) {
		case IT_HEALING:           return "Potion/Food";
		case IT_USABLE:            return "Usable";
		case IT_ETC:               return "Etc.";
		case IT_ARMOR:             return "Armor";
		case IT_WEAPON:            return "Weapon";
		case IT_CARD:              return "Card";
		case IT_PETEGG:            return "Pet Egg";
		case IT_PETARMOR:          return "Pet Accessory";
		case IT_AMMO:              return "Arrow/Ammunition";
		case IT_RESTRICTEDCONSUME: return "Restricted-Consume Usable";
		case IT_SHADOWGEAR:        return "Shadow Equipment";
		case IT_CASH:              return "Cash Usable";
	}
	return "Unknown Type";
}

/// Returns name type of ammunition [Cydh]
const char *itemdb_typename_ammo(enum e_item_ammo ammo) {
	switch (ammo) {
		case AMMO_ARROW:            return "Arrow";
		case AMMO_THROWABLE_DAGGER: return "Throwable Dagger";
		case AMMO_BULLET:           return "Bullet";
		case AMMO_SHELL:            return "Shell";
		case AMMO_GRENADE:          return "Grenade";
		case AMMO_SHURIKEN:         return "Shuriken";
		case AMMO_KUNAI:            return "Kunai";
		case AMMO_CANNONBALL:       return "Cannonball";
		case AMMO_THROWABLE_ITEM:   return "Throwable Item/Sling Item";
	}
	return "Ammunition";
}

/*==========================================
 * Converts the jobid from the format in itemdb
 * to the format used by the map server. [Skotlex]
 *------------------------------------------*/
static void itemdb_jobid2mapid(unsigned int *bclass, unsigned int jobmask)
{
	bclass[0] = bclass[1] = bclass[2] = 0;

	//Novice And 1st Jobs
	if (jobmask & 1<<JOB_NOVICE) { //Novice and Super Novice share the same job equip mask
		bclass[0] |= 1<<MAPID_NOVICE;
		bclass[1] |= 1<<MAPID_NOVICE;
	}
	if (jobmask & 1<<JOB_SWORDMAN)
		bclass[0] |= 1<<MAPID_SWORDMAN;
	if (jobmask & 1<<JOB_MAGE)
		bclass[0] |= 1<<MAPID_MAGE;
	if (jobmask & 1<<JOB_ARCHER)
		bclass[0] |= 1<<MAPID_ARCHER;
	if (jobmask & 1<<JOB_ACOLYTE) { //Acolyte and Gangsi share the same job equip mask
		bclass[0] |= 1<<MAPID_ACOLYTE;
		bclass[0] |= 1<<MAPID_GANGSI;
	}
	if (jobmask & 1<<JOB_MERCHANT)
		bclass[0] |= 1<<MAPID_MERCHANT;
	if (jobmask & 1<<JOB_THIEF)
		bclass[0] |= 1<<MAPID_THIEF;
	//2-1 classes
	if (jobmask & 1<<JOB_KNIGHT) { //Knight and Death Knight share the same job equip mask
		bclass[1] |= 1<<MAPID_SWORDMAN;
		bclass[1] |= 1<<MAPID_GANGSI;
	}
	if (jobmask & 1<<JOB_PRIEST)
		bclass[1] |= 1<<MAPID_ACOLYTE;
	if (jobmask & 1<<JOB_WIZARD)
		bclass[1] |= 1<<MAPID_MAGE;
	if (jobmask & 1<<JOB_BLACKSMITH)
		bclass[1] |= 1<<MAPID_MERCHANT;
	if (jobmask & 1<<JOB_HUNTER)
		bclass[1] |= 1<<MAPID_ARCHER;
	if (jobmask & 1<<JOB_ASSASSIN)
		bclass[1] |= 1<<MAPID_THIEF;
	//2-2 classes
	if (jobmask & 1<<JOB_CRUSADER)
		bclass[2] |= 1<<MAPID_SWORDMAN;
	if (jobmask & 1<<JOB_MONK)
		bclass[2] |= 1<<MAPID_ACOLYTE;
	if (jobmask & 1<<JOB_SAGE) { //Sage and Dark Collector share the same job equip mask
		bclass[2] |= 1<<MAPID_MAGE;
		bclass[2] |= 1<<MAPID_GANGSI;
	}
	if (jobmask & 1<<JOB_ROGUE)
		bclass[2] |= 1<<MAPID_THIEF;
	if (jobmask & 1<<JOB_ALCHEMIST)
		bclass[2] |= 1<<MAPID_MERCHANT;
	if (jobmask & 1<<JOB_BARD)
		bclass[2] |= 1<<MAPID_ARCHER;
	//Bard/Dancer share the same slot now
	//if (jobmask & 1<<JOB_DANCER)
		//bclass[2] |= 1<<MAPID_ARCHER;
	//Expanded Jobs
	if (jobmask & 1<<21) //Taekwon boy
		bclass[0] |= 1<<MAPID_TAEKWON;
	if (jobmask & 1<<22) //Star Gladiator
		bclass[1] |= 1<<MAPID_TAEKWON;
	if (jobmask & 1<<23) //Soul Linker
		bclass[2] |= 1<<MAPID_TAEKWON;
	if (jobmask & 1<<JOB_GUNSLINGER) { //Gunslinger and Rebellion share the same job equip mask
		bclass[0] |= 1<<MAPID_GUNSLINGER;
		bclass[1] |= 1<<MAPID_GUNSLINGER;
	}
	if (jobmask & 1<<JOB_NINJA) { //Ninja and Kagerou/Oboro share the same job equip mask
		bclass[0] |= 1<<MAPID_NINJA;
		bclass[1] |= 1<<MAPID_NINJA;
	}
	if (jobmask & 1<<26) //Summoner
		bclass[0] |= 1<<MAPID_SUMMONER;
}

/**
 * Create dummy item_data as dummy_item and dummy item group entry as dummy_itemgroup
 */
static void itemdb_create_dummy(void) {
	CREATE(dummy_item, struct item_data, 1);

	memset(dummy_item, 0, sizeof(struct item_data));
	dummy_item->nameid = 500;
	dummy_item->weight = 1;
	dummy_item->value_sell = 1;
	dummy_item->type = IT_ETC; //Etc item
	safestrncpy(dummy_item->name, "UNKNOWN_ITEM", sizeof(dummy_item->name));
	safestrncpy(dummy_item->jname, "Unknown Item", sizeof(dummy_item->jname));
	dummy_item->view_id = UNKNOWN_ITEM_ID;
}

/**
 * Create new item data
 * @param nameid
 */
static struct item_data *itemdb_create_item(unsigned short nameid) {
	struct item_data *id;

	CREATE(id, struct item_data, 1);
	memset(id, 0, sizeof(struct item_data));
	id->nameid = nameid;
	id->type = IT_ETC; //Etc item
	uidb_put(itemdb, nameid, id);
	return id;
}

/**
 * Loads an item from the db. If not found, it will return the dummy item.
 * @param nameid
 * @return *item_data or *dummy_item if item not found
 */
struct item_data *itemdb_search(unsigned short nameid) {
	struct item_data *id = NULL;

	if (nameid == dummy_item->nameid)
		id = dummy_item;
	else if (!(id = (struct item_data *)uidb_get(itemdb, nameid))) {
		ShowWarning("itemdb_search: Item ID %hu does not exists in the item_db. Using dummy data.\n", nameid);
		id = dummy_item;
	}
	return id;
}

/** Checks if item is equip type or not
 * @param id Item data
 * @return True if item is equip, false otherwise
 */
bool itemdb_isequip2(struct item_data *id)
{
	nullpo_retr(false,id);

	switch (id->type) {
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_AMMO:
		case IT_SHADOWGEAR:
			return true;
		default:
			return false;
	}
}

/** Checks if item is stackable or not
 * @param id Item data
 * @return True if item is stackable, false otherwise
 */
bool itemdb_isstackable2(struct item_data *id)
{
	nullpo_retr(false,id);

	switch (id->type) {
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_PETEGG:
		case IT_PETARMOR:
		case IT_SHADOWGEAR:
			return false;
		default:
			return true;
	}
}

/*==========================================
 * Trade Restriction functions [Skotlex]
 *------------------------------------------*/
bool itemdb_isdropable_sub(struct item_data *item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&ITR_NODROP) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_cantrade_sub(struct item_data *item, int gmlv, int gmlv2) {
	return (item && (!(item->flag.trade_restriction&ITR_NOTRADE) || gmlv >= item->gm_lv_trade_override || gmlv2 >= item->gm_lv_trade_override));
}

bool itemdb_canpartnertrade_sub(struct item_data *item, int gmlv, int gmlv2) {
	return (item && (item->flag.trade_restriction&ITR_PARTNEROVERRIDE || gmlv >= item->gm_lv_trade_override || gmlv2 >= item->gm_lv_trade_override));
}

bool itemdb_cansell_sub(struct item_data *item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&ITR_NOSELLTONPC) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_cancartstore_sub(struct item_data *item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&ITR_NOCART) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_canstore_sub(struct item_data *item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&ITR_NOSTORAGE) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_canguildstore_sub(struct item_data *item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&ITR_NOGSTORAGE) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_canmail_sub(struct item_data *item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&ITR_NOMAIL) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_canauction_sub(struct item_data *item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&ITR_NOAUCTION) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_isrestricted(struct item *item, int gmlv, int gmlv2, bool (*func)(struct item_data *, int, int))
{
	struct item_data *item_data = itemdb_search(item->nameid);
	int i;

	if(!func(item_data, gmlv, gmlv2))
		return false;

	if(!item_data->slot || itemdb_isspecial(item->card[0]))
		return true;

	for(i = 0; i < item_data->slot; i++) {
		if(!item->card[i])
			continue;
		if(!func(itemdb_search(item->card[i]), gmlv, gmlv2))
			return false;
	}
	return true;
}

/** Specifies if item-type should drop unidentified.
 * @param nameid ID of item
 */
char itemdb_isidentified(unsigned short nameid)
{
	uint8 type = itemdb_type(nameid);

	switch(type) {
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_PETARMOR:
		case IT_SHADOWGEAR:
			return 0;
		default:
			return 1;
	}
}

/** Search by name for the override flags available items (Give item another sprite)
 * Structure: <nameid>,<sprite>
 */
static bool itemdb_read_itemavail(char *str[], int columns, int current)
{
	unsigned short nameid;
	int sprite;
	struct item_data *id;

	nameid = atoi(str[0]);

	if( (id = itemdb_exists(nameid)) == NULL ) {
		ShowWarning("itemdb_read_itemavail: Invalid item id %hu.\n", nameid);
		return false;
	}

	sprite = atoi(str[1]);

	if( sprite > 0 ) {
		id->flag.available = 1;
		id->view_id = sprite;
	} else
		id->flag.available = 0;

	return true;
}

static int itemdb_group_free(DBKey key, DBData *data, va_list ap);

/** Read item group data
 * Structure: GroupID,ItemID,Rate{,Amount,isMust,isAnnounced,Duration,GUID,isBound,isNamed}
 */
static void itemdb_read_itemgroup_sub(const char *filename)
{
	FILE *fp;
	int ln = 0, entries = 0;
	char line[1024];

	if( (fp = fopen(filename, "r")) == NULL ) {
		ShowError("Can't read %s\n", filename);
		return;
	}

	while( fgets(line, sizeof(line), fp) ) {
		DBData data;
		int group_id = -1;
		unsigned int j, prob = 1;
		int8 rand_group = 1;
		char *str[10], *p;
		struct s_item_group_random *random = NULL;
		struct s_item_group_db *group = NULL;
		struct s_item_group_entry entry;
		bool found = false;

		ln++;
		if( line[0] == '/' && line[1] == '/' )
			continue;
		if( strstr(line, "import") ) {
			char w1[16], w2[64];

			if( sscanf(line, "%15[^:]: %63[^\r\n]", w1, w2) == 2 && strcmpi(w1, "import") == 0 ) {
				itemdb_read_itemgroup_sub(w2);
				continue;
			}
		}
		memset(str, 0, sizeof(str));
		for( j = 0, p = line; j < 9 && p; j++ ) {
			str[j] = p;
			p = strchr(p, ',');
			if( p )
				*p++ = 0;
		}
		if( str[0] == NULL ) //Empty Group ID
			continue;
		if( j < 3 ) {
			if( j > 1 ) //Or else it barks on blank lines
				ShowWarning("itemdb_read_itemgroup: Insufficient fields for entry at %s:%d\n", filename, ln);
			continue;
		}

		memset(&entry, 0, sizeof(entry));
		entry.amount = 1;
		entry.bound = BOUND_NONE;

		//Checking group_id
		trim(str[0]);
		if( ISDIGIT(str[0][0]) )
			group_id = atoi(str[0]);
		else //Try reads group id by const
			script_get_constant(trim(str[0]), &group_id);

		if( group_id < 0 ) {
			ShowWarning("itemdb_read_itemgroup: Invalid Group ID '%s' (%s:%d)\n", str[0], filename, ln);
			continue;
		}

		//Remove from DB
		if( strcmpi(str[1], "clear") == 0 && itemdb_group->remove(itemdb_group, db_ui2key(group_id), &data) ) {
			itemdb_group_free(db_ui2key(group_id), &data, 0);
			ShowNotice("Item Group '%s' has been cleared.\n", str[0]);
			continue;
		}

		//Checking sub group
		prob = atoi(str[2]);
		if( str[4] != NULL )
			rand_group = atoi(str[4]);
		if( rand_group < 0 || rand_group > MAX_ITEMGROUP_RANDGROUP ) {
			ShowWarning("itemdb_read_itemgroup: Invalid sub group '%d' for group '%s' in %s:%d\n", rand_group, str[0], filename, ln);
			continue;
		}
		if( rand_group != 0 && !prob ) {
			ShowWarning("itemdb_read_itemgroup: Random item must has probability. Group '%s' in %s:%d\n", str[0], filename, ln);
			continue;
		}

		//Checking item
		trim(str[1]);
		if( ISDIGIT(str[1][0]) && ISDIGIT(str[1][1]) && itemdb_exists((entry.nameid = atoi(str[1]))) )
			found = true;
		else {
			struct item_data *id = itemdb_searchname(str[1]);

			if( id ) {
				entry.nameid = id->nameid;
				found = true;
			}
		}

		if( !found ) {
			ShowWarning("itemdb_read_itemgroup: Non-existant item '%s' in %s:%d\n", str[1], filename, ln);
			continue;
		}

		if( str[3] != NULL )
			entry.amount = cap_value(atoi(str[3]), 1, MAX_AMOUNT);
		if( str[5] != NULL )
			entry.isAnnounced = atoi(str[5]);
		if( str[6] != NULL )
			entry.duration = cap_value(atoi(str[6]), 0, UINT16_MAX);
		if( str[7] != NULL )
			entry.GUID = atoi(str[7]);
		if( str[8] != NULL )
			entry.bound = cap_value(atoi(str[8]), BOUND_NONE, BOUND_MAX - 1);
		if( str[9] != NULL )
			entry.isNamed = atoi(str[9]);

		if( !(group = (struct s_item_group_db *)uidb_get(itemdb_group, group_id)) ) {
			CREATE(group, struct s_item_group_db, 1);
			group->id = group_id;
			uidb_put(itemdb_group, group->id, group);
		}

		//Must item (rand_group == 0), place it here
		if( !rand_group ) {
			RECREATE(group->must, struct s_item_group_entry, group->must_qty + 1);
			group->must[group->must_qty++] = entry;

			//If 'must' item isn't set as random item, skip the next process
			if( !prob ) {
				entries++;
				continue;
			}
			rand_group = 0;
		} else
			rand_group -= 1;

		random = &group->random[rand_group];

		RECREATE(random->data, struct s_item_group_entry, random->data_qty + prob);

		//Put the entry to its rand_group
		for( j = random->data_qty; j < random->data_qty + prob; j++ )
			random->data[j] = entry;

		random->data_qty += prob;
		entries++;
	}
	fclose(fp);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", entries, filename);
}

static void itemdb_read_itemgroup(void)
{
	char path[256];

	snprintf(path, 255, "%s/"DBPATH"item_group_db.txt", db_path);
	itemdb_read_itemgroup_sub(path);
}

static bool itemdb_noequip_parse_dbrow(char *str[], int columns, int current)
{
	unsigned short nameid;
	struct item_data *id;

	nameid = atoi(str[0]);

	if( !(id = itemdb_exists(nameid)) ) {
		ShowWarning("itemdb_noequip_parse_dbrow: Invalid item id %hu.\n", nameid);
		return false;
	}

	id->flag.no_equip |= atoi(str[1]);

	return true;
}

/** Read item forbidden by mapflag (can't equip item)
 * Structure: <nameid>,<mode>
 */
static void itemdb_read_noequip() {
	const char *filename[] = {
		DBPATH"item_noequip.txt",
		"item_noequip_db2.txt"
	};
	int fi;

	for( fi = 0; fi < ARRAYLENGTH(filename); ++fi ) {
		if( fi > 0 ) {
			char path[256];

			sprintf(path, "%s/%s", db_path, filename[fi]);
			if( !exists(path) )
				continue;
		}
		sv_readdb(db_path, filename[fi], ',', 2, 2, -1, &itemdb_noequip_parse_dbrow);
	}
}

static bool itemdb_itemtrade_parse_dbrow(char *str[], int columns, int current) {
	unsigned short nameid;
	int flag, gmlv;
	struct item_data *id;

	nameid = atoi(str[0]);

	if( !(id = itemdb_exists(nameid)) ) {
		//ShowWarning("itemdb_itemtrade_parse_dbrow: Invalid item id %d.\n", nameid);
		//return false;
		//FIXME: item_trade.txt contains items, which are commented in item database.
		return true;
	}

	flag = atoi(str[1]);
	gmlv = atoi(str[2]);

	if( flag > ITR_ALL ) { //Check range
		ShowWarning("itemdb_itemtrade_parse_dbrow: Invalid trading mask %d for item id %hu.\n", flag, nameid);
		return false;
	}
	if( gmlv < 1 ) {
		ShowWarning("itemdb_itemtrade_parse_dbrow: Invalid override GM level %d for item id %hu.\n", gmlv, nameid);
		return false;
	}

	id->flag.trade_restriction = flag;
	id->gm_lv_trade_override = gmlv;

	return true;
}

/** Reads item trade restrictions [Skotlex]
 * Structure: <nameid>,<mask>,<gm level>
 */
static void itemdb_read_itemtrade() {
	const char *filename[] = {
		DBPATH"item_trade.txt",
		"item_trade_db2.txt"
	};
	int fi;

	for( fi = 0; fi < ARRAYLENGTH(filename); ++fi ) {
		if( fi > 0 ) {
			char path[256];

			sprintf(path, "%s/%s", db_path, filename[fi]);
			if( !exists(path) )
				continue;
		}
		sv_readdb(db_path, filename[fi], ',', 3, 3, -1, &itemdb_itemtrade_parse_dbrow);
	}
}

/** Reads item delay amounts [Paradox924X]
 * Structure: <nameid>,<delay>
 */
static bool itemdb_read_itemdelay(char *str[], int columns, int current)
{
	unsigned short nameid;
	int delay;
	struct item_data *id;

	nameid = atoi(str[0]);

	if( !(id = itemdb_exists(nameid)) ) {
		ShowWarning("itemdb_read_itemdelay: Invalid item id %hu.\n", nameid);
		return false;
	}

	delay = atoi(str[1]);

	if( delay < 0 ) {
		ShowWarning("itemdb_read_itemdelay: Invalid delay %d for item id %hu.\n", delay, nameid);
		return false;
	}

	id->delay = delay;

	if( columns == 2 )
		id->delay_sc = SC_NONE;
	else {
		trim(str[2]);
		if( ISDIGIT(str[2][0]) )
			id->delay_sc = atoi(str[2]);
		else if( !script_get_constant(trim(str[2]), &id->delay_sc) ) {
			ShowWarning("itemdb_read_itemdelay: Unknown SC_GroupID constant \"%s\".\n", str[2]);
			return false;
		}
	}

	return true;
}

/** Reads item stacking restrictions
 * Structure: <item id>,<stack limit amount>,<type>
 */
static bool itemdb_read_stack(char *fields[], int columns, int current)
{
	unsigned short nameid, amount;
	unsigned int type;
	struct item_data *id;

	nameid = (unsigned short)strtoul(fields[0], NULL, 10);

	if( !(id = itemdb_exists(nameid)) ) {
		ShowWarning("itemdb_read_stack: Unknown item id '%hu'.\n", nameid);
		return false;
	}

	if( !itemdb_isstackable2(id) ) {
		ShowWarning("itemdb_read_stack: Item id '%hu' is not stackable.\n", nameid);
		return false;
	}

	amount = (unsigned short)strtoul(fields[1], NULL, 10);
	type = strtoul(fields[2], NULL, 10);

	if( !amount ) //Ignore
		return true;

	id->stack.amount       = amount;
	id->stack.inventory    = (type&1) != 0;
	id->stack.cart         = (type&2) != 0;
	id->stack.storage      = (type&4) != 0;
	id->stack.guildstorage = (type&8) != 0;

	return true;
}

static bool itemdb_buyingstore_parse_dbrow(char *fields[], int columns, int current)
{
	unsigned short nameid;
	struct item_data *id;

	nameid = atoi(fields[0]);

	if( !(id = itemdb_exists(nameid)) ) {
		ShowWarning("itemdb_buyingstore_parse_dbrow: Invalid item id %hu.\n", nameid);
		return false;
	}

	if( !itemdb_isstackable2(id) ) {
		ShowWarning("itemdb_buyingstore_parse_dbrow: Non-stackable item id %hu cannot be enabled for buying store.\n", nameid);
		return false;
	}

	id->flag.buyingstore = 1;

	return true;
}

/** Reads items allowed to be sold in buying stores
 * Structure: <nameid>
 */
static void itemdb_read_buyingstore() {
	const char *filename[] = {
		DBPATH"item_buyingstore.txt",
		"item_buyingstore_db2.txt"
	};
	int fi;

	for( fi = 0; fi < ARRAYLENGTH(filename); ++fi ) {
		if( fi > 0 ) {
			char path[256];

			sprintf(path, "%s/%s", db_path, filename[fi]);
			if( !exists(path) )
				continue;
		}
		sv_readdb(db_path, filename[fi], ',', 1, 1, -1, &itemdb_buyingstore_parse_dbrow);
	}
}

/** Misc Item flags
 * <item_id>,<flag>
 * &1  - As dead branch item
 * &2  - As item container
 * &4  - GUID item, cannot be stacked even same or stackable item
 * &8  - Item will be bound item when equipped
 * &16 - Special Broadcast
 * &32 - Keep after being consumed
 */
static bool itemdb_read_flag(char *fields[], int columns, int current) {
	unsigned short nameid = atoi(fields[0]);
	uint8 flag;
	bool set;
	struct item_data *id;

	if( !(id = itemdb_exists(nameid)) ) {
		ShowError("itemdb_read_flag: Invalid item item with id %hu\n", nameid);
		return true;
	}

	flag = abs(atoi(fields[1]));
	set = (atoi(fields[1]) > 0);

	if( flag&1 )
		id->flag.dead_branch = (set ? 1 : 0);

	if( flag&2 )
		id->flag.group = (set ? 1 : 0);

	if( flag&4 && itemdb_isstackable2(id) )
		id->flag.guid = (set ? 1 : 0);

	if( flag&8 )
		id->flag.bindOnEquip = true;

	if( flag&16 )
		id->flag.broadcast = 1;

	if( flag&32 )
		id->flag.keepAfterUse = true;

	return true;
}

/**
 * @return: amount of retrieved entries.
 */
static int itemdb_combo_split_atoi(char *str, int *val) {
	int i;

	for (i = 0; i < MAX_ITEMS_PER_COMBO; i++) {
		if (!str)
			break;
		val[i] = atoi(str);
		str = strchr(str,':');
		if (str)
			*str++ = 0;
	}

	if (i == 0) //No data found
		return 0;

	return i;
}

/**
 * <combo{:combo{:combo:{..}}}>,<{ script }>
 */
static void itemdb_read_combos() {
	const char *filename[] = {
		DBPATH"item_combo_db.txt",
		"item_combo_db2.txt"
	};
	int fi;

	for (fi = 0; fi < ARRAYLENGTH(filename); ++fi) {
		uint32 lines = 0, count = 0;
		char line[1024];
		char path[256];
		FILE *fp;

		sprintf(path, "%s/%s", db_path, filename[fi]);

		if ((fp = fopen(path, "r")) == NULL) {
			ShowError("itemdb_read_combos: File not found \"%s\".\n", path);
			return;
		}

		// Process rows one by one
		while (fgets(line, sizeof(line), fp)) {
			char *str[2], *p;

			lines++;

			if (line[0] == '/' && line[1] == '/')
				continue;

			memset(str, 0, sizeof(str));
			p = line;
			p = trim(p);

			if (*p == '\0')
				continue; // Empty line

			if (!strchr(p,',')) {
				// Is there even a single column?
				ShowError("itemdb_read_combos: Insufficient columns in line %d of \"%s\", skipping.\n", lines, path);
				continue;
			}

			str[0] = p;
			p = strchr(p,',');
			*p = '\0';
			p++;

			str[1] = p;
			p = strchr(p,',');
			p++;

			if (str[1][0] != '{') {
				ShowError("itemdb_read_combos(#1): Invalid format (Script column) in line %d of \"%s\", skipping.\n", lines, path);
				continue;
			}

			// No ending key anywhere (missing \}\)
			if (str[1][strlen(str[1]) - 1] != '}') {
				ShowError("itemdb_read_combos(#2): Invalid format (Script column) in line %d of \"%s\", skipping.\n", lines, path);
				continue;
			} else {
				int items[MAX_ITEMS_PER_COMBO];
				int v = 0, retcount = 0;
				struct item_data *id = NULL;
				int idx = 0;

				if ((retcount = itemdb_combo_split_atoi(str[0], items)) < 2) {
					ShowError("itemdb_read_combos: line %d of \"%s\" doesn't have enough items to make for a combo (min:2), skipping.\n", lines, path);
					continue;
				}

				// Validate
				for (v = 0; v < retcount; v++) {
					if (!itemdb_exists(items[v])) {
						ShowError("itemdb_read_combos: line %d of \"%s\" contains unknown item ID %d, skipping.\n", lines, path, items[v]);
						break;
					}
				}

				// Failed at some item
				if (v < retcount)
					continue;

				id = itemdb_exists(items[0]);
				idx = id->combos_count;

				// First entry, create
				if (id->combos == NULL) {
					CREATE(id->combos, struct item_combo *, 1);
					id->combos_count = 1;
				} else
					RECREATE(id->combos, struct item_combo *, ++id->combos_count);

				CREATE(id->combos[idx], struct item_combo, 1);
				id->combos[idx]->nameid = aMalloc(retcount * sizeof(unsigned short));
				id->combos[idx]->count = retcount;
				id->combos[idx]->script = parse_script(str[1], path, lines, 0);
				id->combos[idx]->id = count;
				id->combos[idx]->isRef = false;

				// Populate ->nameid field
				for (v = 0; v < retcount; v++)
					id->combos[idx]->nameid[v] = items[v];

				// Populate the children to refer to this combo
				for (v = 1; v < retcount; v++) {
					struct item_data *it;
					int index;

					it = itemdb_exists(items[v]);
					index = it->combos_count;

					if (it->combos == NULL) {
						CREATE(it->combos, struct item_combo *, 1);
						it->combos_count = 1;
					} else
						RECREATE(it->combos, struct item_combo *, ++it->combos_count);

					CREATE(it->combos[index], struct item_combo, 1);
					// We copy previously alloc'd pointers and just set it to reference
					memcpy(it->combos[index], id->combos[idx], sizeof(struct item_combo));
					// We flag this way to ensure we don't double-dealloc same data
					it->combos[index]->isRef = true;
				}
				uidb_put(itemdb_combo, id->combos[idx]->id, id->combos[idx]);
			}
			count++;
		}

		fclose(fp);
		ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, path);
	}
}

/**
 * Reads Roulette database
 * Structure: Level,ItemID,Amount{,Flag,Chance}
 */
static bool itemdb_roulette_parse_dbrow(char *str[], int columns, int current)
{
	unsigned short item_id = 0, amount;
	int level, column, flag = 0, chance = 0;

	level = atoi(str[0]);
	trim(str[1]);
	if (ISDIGIT(str[1][0]) && ISDIGIT(str[1][1]))
		item_id = atoi(str[1]);
	else {
		struct item_data *id = itemdb_searchname(str[1]);

		if (id)
			item_id = id->nameid;
	}
	if (!itemdb_exists(item_id)) {
		ShowWarning("itemdb_parse_roulette_db: Unknown item ID '%hu' in level '%d'\n", item_id, level);
		return false;
	}
	amount = atoi(str[2]);
	if (amount < 1 || amount > MAX_AMOUNT) {
		ShowWarning("itemdb_parse_roulette_db: Unsupported amount '%hu' for item ID '%hu' in level '%d'\n", amount, item_id, level);
		return false;
	}
	if (columns > 3) {
		flag = atoi(str[3]);
		if (flag < 0 || flag > 1) {
			ShowWarning("itemdb_parse_roulette_db: Unsupported flag '%d' for item ID '%hu' in level '%d'\n", flag, item_id, level);
			return false;
		}
		chance = min(atoi(str[4]), 100);
		chance = max(0, chance);
	}
	level--;
	column = rd.items[level];
	RECREATE(rd.nameid[level], unsigned short, ++rd.items[level]);
	RECREATE(rd.qty[level], unsigned short, rd.items[level]);
	RECREATE(rd.flag[level], int, rd.items[level]);
	RECREATE(rd.chance[level], int, rd.items[level]);
	rd.nameid[level][column] = item_id;
	rd.qty[level][column] = amount;
	rd.flag[level][column] = flag;
	rd.chance[level][column] = chance;
	return true;
}

/**
 * Process Roulette items
 */
static void itemdb_roulette_read(void) {
	int i, j;

	for (i = 0; i < MAX_ROULETTE_LEVEL; i++)
		rd.items[i] = 0;

	sv_readdb(db_path, "roulette_db.txt",    ',', 3, 5, -1, &itemdb_roulette_parse_dbrow);

	for (i = 0; i < MAX_ROULETTE_LEVEL; i++) {
		int limit = MAX_ROULETTE_COLUMNS - i;

		if (rd.items[i] == limit)
			continue;
		if (rd.items[i] > limit) {
			ShowWarning("itemdb_roulette_read: Level %d has %d items, only %d supported, capping...\n", i + 1, rd.items[i], limit);
			rd.items[i] = limit;
			continue;
		}
		// This scenario = rd.items[i] < limit
		ShowWarning("itemdb_roulette_read: Level %d has %d items, %d are required. Filling with Apples...\n", i + 1, rd.items[i], limit);
		rd.items[i] = limit;
		RECREATE(rd.nameid[i], unsigned short, rd.items[i]);
		RECREATE(rd.qty[i], unsigned short, rd.items[i]);
		RECREATE(rd.flag[i], int, rd.items[i]);
		for (j = 0; j < MAX_ROULETTE_COLUMNS - i; j++) {
			if (rd.qty[i][j])
				continue;
			rd.nameid[i][j] = ITEMID_APPLE;
			rd.qty[i][j] = 1;
			rd.flag[i][j] = 0;
		}
	}
}

/**
 * Free Roulette items
 */
static void itemdb_roulette_free(void) {
	int i;

	for (i = 0; i < MAX_ROULETTE_LEVEL; i++) {
		if (rd.nameid[i])
			aFree(rd.nameid[i]);
		if (rd.qty[i])
			aFree(rd.qty[i]);
		if (rd.flag[i])
			aFree(rd.flag[i]);
		if (rd.chance[i])
			aFree(rd.chance[i]);
		rd.nameid[i] = NULL;
		rd.qty[i] = NULL;
		rd.flag[i] = NULL;
		rd.chance[i] = NULL;
		rd.items[i] = 0;
	}
}

/*======================================
 * Applies gender restrictions according to settings. [Skotlex]
 *======================================*/
static char itemdb_gendercheck(struct item_data *id)
{
	if (id->nameid == WEDDING_RING_M) //Grom Ring
		return 1;
	if (id->nameid == WEDDING_RING_F) //Bride Ring
		return 0;
	if (id->look == W_MUSICAL && id->type == IT_WEAPON) //Musical instruments are always male-only
		return 1;
	if (id->look == W_WHIP && id->type == IT_WEAPON) //Whips are always female-only
		return 0;

	return (battle_config.ignore_items_gender) ? 2 : id->sex;
}

/**
 * [Ind]
 * For backwards compatibility, in renewal, MATK from weapons comes from the atk slot
 * We use a ':' delimiter which, if not found, assumes the weapon does not provide any MATK.
 */
static void itemdb_re_split_atoi(char *str, int *val1, int *val2) {
	int i, val[2];

	for (i = 0; i < 2; i++) {
		if (!str)
			break;
		val[i] = atoi(str);
		str = strchr(str,':');
		if (str)
			*str++ = 0;
	}
	if (i == 0) {
		*val1 = *val2 = 0;
		return; //No data found
	}
	if (i == 1) { //Single value
		*val1 = val[0];
		*val2 = 0;
		return;
	}
	//We assume we have 2 values
	*val1 = val[0];
	*val2 = val[1];
}

/**
 * Processes one itemdb entry
 */
static bool itemdb_parse_dbrow(char **str, const char *source, int line, int scriptopt) {
	/*
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-------------+------------+------+--------+--------------+----------------+
		| 00 |      01      |       02      |  03  |     04    |     05     |   06   |   07   |    08   |   09  |   10  |     11     |      12     |       13      |        14       |      15      |      16     |     17     |  18  |   19   |      20      |        21      |
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-------------+------------+------+--------+--------------+----------------+
		| id | name_english | name_japanese | type | price_buy | price_sell | weight | attack | defence | range | slots | equip_jobs | equip_upper | equip_genders | equip_locations | weapon_level | equip_level | refineable | view | script | equip_script | unequip_script |
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-------------+------------+------+--------+--------------+----------------+
	*/
	unsigned short nameid;
	struct item_data *id;

	if (atoi(str[0]) <= 0 || atoi(str[0]) >= MAX_ITEMID || atoi(str[0]) == dummy_item->nameid) {
		ShowWarning("itemdb_parse_dbrow: Invalid id %d in line %d of \"%s\", skipping.\n", atoi(str[0]), line, source);
		return false;
	}

	nameid = atoi(str[0]);

	//ID,Name,Jname,Type,Price,Sell,Weight,ATK,DEF,Range,Slot,Job,Job Upper,Gender,Loc,wLV,eLV,refineable,View
	if (!(id = itemdb_exists(nameid)))
		id = itemdb_create_item(nameid);

	safestrncpy(id->name, str[1], sizeof(id->name));
	safestrncpy(id->jname, str[2], sizeof(id->jname));

	id->type = atoi(str[3]);

	if (id->type < 0 || id->type == IT_UNKNOWN || id->type == IT_UNKNOWN2 ||
		(id->type > IT_SHADOWGEAR && id->type < IT_CASH) || id->type >= IT_MAX) { //Catch invalid item types
		ShowWarning("itemdb_parse_dbrow: Invalid item type %d for item %hu. IT_ETC will be used.\n", id->type, nameid);
		id->type = IT_ETC;
	}

	if (id->type == IT_RESTRICTEDCONSUME) { //Items that are consumed only after target confirmation
		id->type = IT_USABLE;
		id->flag.restricted_consume = 1;
	} else //In case of an itemdb reload and the item type changed
		id->flag.restricted_consume = 0;

	//When a particular price is not given, we should base it off the other one
	//(it is important to make a distinction between 'no price' and 0z)
	if (str[4][0])
		id->value_buy = atoi(str[4]);
	else
		id->value_buy = atoi(str[5]) * 2;

	if (str[5][0])
		id->value_sell = atoi(str[5]);
	else
		id->value_sell = id->value_buy / 2;
	/*
	if (!str[4][0] && !str[5][0]) { 
		ShowWarning("itemdb_parse_dbrow: No buying/selling price defined for item %hu (%s), using 20/10z\n", nameid, id->jname);
		id->value_buy = 20;
		id->value_sell = 10;
	} else
	*/
	if (id->value_buy / 124. < id->value_sell / 75.)
		ShowWarning("itemdb_parse_dbrow: Buying/Selling [%d/%d] price of item %hu (%s) allows Zeny making exploit  through buying/selling at discounted/overcharged prices!\n",
			id->value_buy, id->value_sell, nameid, id->jname);

	id->weight = atoi(str[6]);
#ifdef RENEWAL
	itemdb_re_split_atoi(str[7], &id->atk, &id->matk);
#else
	id->atk = atoi(str[7]);
#endif
	id->def = atoi(str[8]);
	id->range = atoi(str[9]);
	id->slot = atoi(str[10]);

	if (id->slot > MAX_SLOTS) {
		ShowWarning("itemdb_parse_dbrow: Item %hu (%s) specifies %d slots, but the server only supports up to %d. Using %d slots.\n", nameid, id->jname, id->slot, MAX_SLOTS, MAX_SLOTS);
		id->slot = MAX_SLOTS;
	}

	itemdb_jobid2mapid(id->class_base, (unsigned int)strtoul(str[11], NULL, 0));
	id->class_upper = atoi(str[12]);
	id->sex	= atoi(str[13]);
	id->equip = atoi(str[14]);

	if (!id->equip && itemdb_isequip2(id)) {
		ShowWarning("Item %hu (%s) is an equipment with no equip-field! Making it an etc item.\n", nameid, id->jname);
		id->type = IT_ETC;
	}

	if (id->type != IT_SHADOWGEAR && (id->equip&EQP_SHADOW_GEAR)) {
		ShowWarning("Item %hu (%s) have invalid equipment slot! Making it an etc item.\n", nameid, id->jname);
		id->type = IT_ETC;
	}

	id->wlv = cap_value(atoi(str[15]), REFINE_TYPE_ARMOR, REFINE_TYPE_MAX);
	itemdb_re_split_atoi(str[16], &id->elv, &id->elvmax);
	id->flag.no_refine = (atoi(str[17]) ? 0 : 1); //FIXME: verify this
	id->look = atoi(str[18]);

	id->flag.available = 1;
	id->view_id = 0;
	id->sex = itemdb_gendercheck(id); //Apply gender filtering

	if (id->script) {
		script_free_code(id->script);
		id->script = NULL;
	}
	if (id->equip_script) {
		script_free_code(id->equip_script);
		id->equip_script = NULL;
	}
	if (id->unequip_script) {
		script_free_code(id->unequip_script);
		id->unequip_script = NULL;
	}

	if (*str[19])
		id->script = parse_script(str[19], source, line, scriptopt);
	if (*str[20])
		id->equip_script = parse_script(str[20], source, line, scriptopt);
	if (*str[21])
		id->unequip_script = parse_script(str[21], source, line, scriptopt);

	if (!id->nameid) {
		id->nameid = nameid;
		uidb_put(itemdb, nameid, id);
	}

	return true;
}

/**
 * Read item from item db
 * item_db2 overwriting item_db
 */
static int itemdb_readdb(void)
{
	const char *filename[] = {
		DBPATH"item_db.txt",
		"item_db2.txt"
	};
	bool duplicate[MAX_ITEMID];
	int fi;

	for (fi = 0; fi < ARRAYLENGTH(filename); ++fi) {
		uint32 lines = 0, count = 0;
		char line[1024];
		char path[256];
		FILE *fp;

		sprintf(path, "%s/%s", db_path, filename[fi]);
		fp = fopen(path, "r");
		if (fp == NULL) {
			ShowWarning("itemdb_readdb: File not found \"%s\", skipping.\n", path);
			continue;
		}

		memset(&duplicate, 0, sizeof(duplicate));

		//Process rows one by one
		while (fgets(line, sizeof(line), fp)) {
			struct item_data *itd;
			char *str[32], *p;
			int i, nameid;

			lines++;
			if (line[0] == '/' && line[1] == '/')
				continue;
			memset(str, 0, sizeof(str));

			p = line;
			while (ISSPACE(*p))
				++p;
			if (*p == '\0')
				continue; //Empty line
			for (i = 0; i < 19; ++i) {
				str[i] = p;
				p = strchr(p, ',');
				if (p == NULL)
					break; //Comma not found
				*p = '\0';
				++p;
			}

			if (p == NULL) {
				ShowError("itemdb_readdb: Insufficient columns in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}

			//Script
			if (*p != '{') {
				ShowError("itemdb_readdb: Invalid format (Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[19] = p;
			p = strstr(p + 1,"},");
			if (p == NULL) {
				ShowError("itemdb_readdb: Invalid format (Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			p[1] = '\0';
			p += 2;

			//OnEquip_Script
			if (*p != '{') {
				ShowError("itemdb_readdb: Invalid format (OnEquip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[20] = p;
			p = strstr(p + 1, "},");
			if (p == NULL) {
				ShowError("itemdb_readdb: Invalid format (OnEquip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			p[1] = '\0';
			p += 2;

			//OnUnequip_Script (last column)
			if (*p != '{') {
				ShowError("itemdb_readdb: Invalid format (OnUnequip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[21] = p;

			if (str[21][strlen(str[21]) - 2] != '}') {
				//Lets count to ensure it's not something silly e.g. a extra space at line ending
				int v, lcurly = 0, rcurly = 0;

				for (v = 0; v < strlen(str[21]); v++) {
					if (str[21][v] == '{')
						lcurly++;
					else if (str[21][v] == '}')
						rcurly++;
				}

				if (lcurly != rcurly) {
					ShowError("itemdb_readdb: Mismatching curly braces in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
					continue;
				}
			}

			nameid = atoi(str[0]);
			if (!(itd = itemdb_exists(nameid)))
				CREATE(itd, struct item_data, 1);

			if (!itd->nameid) {
				itd->nameid = nameid;
				uidb_put(itemdb, nameid, itd);
			}

			if (!itemdb_parse_dbrow(str, path, lines, 0))
				continue;

			if (duplicate[nameid])
				ShowWarning("itemdb_readdb: %s: duplicate entry of ID #%d (%s/%s)\n", path, nameid, itemdb_name(nameid), itemdb_jname(nameid));
			else
				duplicate[nameid] = true;

			count++;
		}

		fclose(fp);

		ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, path);
	}

	return 0;
}

/**
 * Read item_db table
 */
static int itemdb_read_sqldb(void) {

	const char *item_db_name[] = {
#ifdef RENEWAL
		item_db_re_db,
#else
		item_db_db,
#endif
		item_db2_db
	};
	int fi;

	for (fi = 0; fi < ARRAYLENGTH(item_db_name); ++fi) {
		uint32 lines = 0, count = 0;

		//Retrieve all rows from the item database
		if (SQL_ERROR == Sql_Query(mmysql_handle, "SELECT * FROM `%s`", item_db_name[fi])) {
			Sql_ShowDebug(mmysql_handle);
			continue;
		}

		//Process rows one by one
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) { //Wrap the result into a TXT-compatible format
			char *str[22];
			char *dummy = "";
			int i;
			++lines;
			for (i = 0; i < 22; ++i) {
				Sql_GetData(mmysql_handle, i, &str[i], NULL);
				if (str[i] == NULL)
					str[i] = dummy; //Get rid of NULL columns
			}

			if (!itemdb_parse_dbrow(str, item_db_name[fi], lines, SCRIPT_IGNORE_EXTERNAL_BRACKETS))
				continue;
			++count;
		}

		//Free the query result
		Sql_FreeResult(mmysql_handle);

		ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, item_db_name[fi]);
	}

	return 0;
}

/** Check if the item is restricted by item_noequip.txt
 * @param id Item that will be checked
 * @param m Map ID
 * @return true: can't be used; false: can be used
 */
bool itemdb_isNoEquip(struct item_data *id, uint16 m) {
	if (!id->flag.no_equip)
		return false;
	if ((!map_flag_vs2(m) && id->flag.no_equip&1) || //Normal
		(map[m].flag.pvp && id->flag.no_equip&2) || //PVP
		(map_flag_gvg2_no_te(m) && id->flag.no_equip&4) || //GVG
		(map[m].flag.battleground && id->flag.no_equip&8) || //Battleground
		(map_flag_gvg2_te(m) && id->flag.no_equip&16) || //WOE:TE
		(map[m].flag.restricted && id->flag.no_equip&(8 * map[m].zone))) //Zone restriction
		return true;
	return false;
}

/**
 * Check if item is available in spellbook_db or not
 * @param nameid
 * @return True if item is spellbook; False if not
 */
bool itemdb_is_spellbook2(unsigned short nameid) {
	unsigned char i;

	if (!nameid || !itemdb_exists(nameid))
		return false;
	ARR_FIND(0, MAX_SKILL_SPELLBOOK_DB, i, skill_spellbook_db[i].nameid == nameid);
	if (i == MAX_SKILL_SPELLBOOK_DB)
		return false;
	return true;
}

/**
 * Check is item have usable type
 */
bool itemdb_is_item_usable(struct item_data *item) {
	return (item->type == IT_HEALING || item->type == IT_USABLE || item->type == IT_CASH);
}

/**
 * Retrieves random option data
 */
struct s_random_opt_data *itemdb_randomopt_exists(short id) {
	return ((struct s_random_opt_data *)uidb_get(itemdb_randomopt, id));
}

/** Random option
 * <ID>,<{Script}>
 */
static void itemdb_read_randomopt() {
	uint32 lines = 0, count = 0;
	char line[1024];

	char path[256];
	FILE *fp;

	sprintf(path, "%s/%s", db_path, DBPATH"item_randomopt_db.txt");

	if ((fp = fopen(path, "r")) == NULL) {
		ShowError("itemdb_read_randomopt: File not found \"%s\".\n", path);
		return;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *str[2], *p;

		lines++;

		if (line[0] == '/' && line[1] == '/') // Ignore comments
			continue;

		memset(str, 0, sizeof(str));
		p = line;
		p = trim(p);

		if (*p == '\0')
			continue; // Empty line

		if (!strchr(p, ',')) {
			ShowError("itemdb_read_randomopt: Insufficient columns in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		}

		str[0] = p;
		p = strchr(p, ',');
		*p = '\0';
		p++;

		str[1] = p;

		if (str[1][0] != '{') {
			ShowError("itemdb_read_randomopt(#1): Invalid format (Script column) in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		}

		//No ending key anywhere (missing \}\)
		if (str[1][strlen(str[1]) - 1] != '}') {
			ShowError("itemdb_read_randomopt(#2): Invalid format (Script column) in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		} else {
			int id = -1;
			struct s_random_opt_data *data;
			struct script_code *code;

			str[0] = trim(str[0]);

			if (ISDIGIT(str[0][0]))
				id = atoi(str[0]);
			else
				script_get_constant(str[0], &id);

			if (id < 0) {
				ShowError("itemdb_read_randomopt: Invalid Random Option ID '%s' in line %d of \"%s\", skipping.\n", str[0], lines, path);
				continue;
			}

			if (!(data = itemdb_randomopt_exists(id))) {
				CREATE(data, struct s_random_opt_data, 1);
				uidb_put(itemdb_randomopt, id, data);
			}

			data->id = id;

			if (!(code = parse_script(str[1], path, lines, 0))) {
				ShowWarning("itemdb_read_randomopt: Invalid script on option ID #%d.\n", id);
				continue;
			}

			if (data->script) {
				script_free_code(data->script);
				data->script = NULL;
			}

			data->script = code;
		}
		count++;
	}

	fclose(fp);
	ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, path);
}

/**
 * Clear Item Random Option Group from memory
 * @author [Cydh]
 */
static int itemdb_randomopt_group_free(DBKey key, DBData *data, va_list ap) {
	struct s_random_opt_group *g = (struct s_random_opt_group *)db_data2ptr(data);

	if (!g)
		return 0;
	if (g->entries)
		aFree(g->entries);
	g->entries = NULL;
	aFree(g);
	return 1;
}

/**
 * Get Item Random Option Group from itemdb_randomopt_group MapDB
 * @param id Random Option Group
 * @return Random Option Group data or NULL if not found
 * @author [Cydh]
 */
struct s_random_opt_group *itemdb_randomopt_group_exists(int id) {
	return (struct s_random_opt_group *)uidb_get(itemdb_randomopt_group, id);
}

/**
 * Read Item Random Option Group from db file
 * @author [Cydh]
 */
static bool itemdb_read_randomopt_group(char *str[], int columns, int current) {
	int id = 0, i;
	unsigned short rate = (unsigned short)strtoul(str[1], NULL, 10);
	struct s_random_opt_group *g = NULL;

	if (!script_get_constant(str[0], &id)) {
		ShowError("itemdb_read_randomopt_group: Invalid ID for Random Option Group '%s'.\n", str[0]);
		return false;
	}

	if ((columns - 2)%3) {
		ShowError("itemdb_read_randomopt_group: Invalid column entries '%d'.\n", columns);
		return false;
	}

	if (!(g = (struct s_random_opt_group *)uidb_get(itemdb_randomopt_group, id))) {
		CREATE(g, struct s_random_opt_group, 1);
		g->id = id;
		g->total = 0;
		g->entries = NULL;
		uidb_put(itemdb_randomopt_group, g->id, g);
	}

	RECREATE(g->entries, struct s_random_opt_group_entry, g->total + rate);

	for (i = g->total; i < (g->total + rate); i++) {
		int j, k;

		memset(&g->entries[i].option, 0, sizeof(g->entries[i].option));
		for (j = 0, k = 2; k < columns && j < MAX_ITEM_RDM_OPT; k += 3) {
			int randid = 0;

			if (!script_get_constant(str[k], &randid) || !itemdb_randomopt_exists(randid)) {
				ShowError("itemdb_read_randomopt_group: Invalid random group id '%s' in column %d!\n", str[k], k + 1);
				continue;
			}
			g->entries[i].option[j].id = randid;
			g->entries[i].option[j].value = (short)strtoul(str[k + 1], NULL, 10);
			g->entries[i].option[j].param = (char)strtoul(str[k + 2], NULL, 10);
			j++;
		}
	}
	g->total += rate;
	return true;
}

/**
 * Read all item-related databases
 */
static void itemdb_read(void) {
	if (db_use_sqldbs)
		itemdb_read_sqldb();
	else
		itemdb_readdb();

	itemdb_read_combos();
	itemdb_read_itemgroup();
	itemdb_read_buyingstore();
	itemdb_read_noequip();
	itemdb_read_itemtrade();
	itemdb_read_randomopt();
	sv_readdb(db_path, "item_avail.txt",         ',', 2, 2, -1, &itemdb_read_itemavail);
	sv_readdb(db_path, DBPATH"item_delay.txt",   ',', 2, 3, -1, &itemdb_read_itemdelay);
	sv_readdb(db_path, DBPATH"item_flag.txt",    ',', 2, 2, -1, &itemdb_read_flag);
	sv_readdb(db_path, "item_stack.txt",         ',', 3, 3, -1, &itemdb_read_stack);
	sv_readdb(db_path, DBPATH"item_randomopt_group.txt", ',', 5, 2 + 5 * MAX_ITEM_RDM_OPT, -1, &itemdb_read_randomopt_group);
}

/*==========================================
 * Initialize/Finalize
 *------------------------------------------*/

/**
 * Destroys the item_data.
 */
static void destroy_item_data(struct item_data *self)
{
	if( self == NULL )
		return;
	// Free scripts
	if( self->script )
		script_free_code(self->script);
	if( self->equip_script )
		script_free_code(self->equip_script);
	if( self->unequip_script )
		script_free_code(self->unequip_script);
	if( self->combos_count ) {
		int i;

		for( i = 0; i < self->combos_count; i++ ) {
			if( !self->combos[i]->isRef ) {
				aFree(self->combos[i]->nameid);
				if( self->combos[i]->script )
					script_free_code(self->combos[i]->script);
			}
			aFree(self->combos[i]);
		}
		aFree(self->combos);
	}
#if defined(DEBUG)
	memset(self, 0xDD, sizeof(struct item_data)); // Trash item
#endif
	aFree(self); // Free self
}

/**
 * @see DBApply
 */
static int itemdb_final_sub(DBKey key, DBData *data, va_list ap)
{
	struct item_data *id = db_data2ptr(data);

	destroy_item_data(id);

	return 0;
}

static int itemdb_group_free(DBKey key, DBData *data, va_list ap) {
	struct s_item_group_db *group = db_data2ptr(data);
	uint8 j;

	if( !group )
		return 0;
	if( group->must_qty )
		aFree(group->must);
	group->must_qty = 0;
	for( j = 0; j < MAX_ITEMGROUP_RANDGROUP; j++ ) {
		if( !group->random[j].data_qty || !(&group->random[j]) )
			continue;
		aFree(group->random[j].data);
		group->random[j].data_qty = 0;
	}
	aFree(group);
	return 0;
}

static int itemdb_randomopt_free(DBKey key, DBData *data, va_list ap) {
	struct s_random_opt_data *opt = (struct s_random_opt_data *)db_data2ptr(data);

	if( !opt )
		return 0;
	if( opt->script )
		script_free_code(opt->script);
	opt->script = NULL;
	aFree(opt);
	return 1;
}

/**
 * Re-link monster drop data with item data
 * Fixes the need of a @reloadmobdb after a @reloaditemdb
 * @author Epoque
 */
void itemdb_reload_itemmob_data(void) {
	int i;

	for( i = 0; i < MAX_MOB_DROP_TOTAL; i++ ) {
		struct mob_db *entry = mob_db(i);
		int d, k;

		for( d = 0; d < MAX_MOB_DROP; d++ ) {
			struct item_data *id;

			if( !entry->dropitem[d].nameid )
				continue;
			id = itemdb_search(entry->dropitem[d].nameid);
			for( k = 0; k < MAX_SEARCH; k++ ) {
				if( id->mob[k].chance <= entry->dropitem[d].p )
					break;
			}
			if( k == MAX_SEARCH )
				continue;
			if( id->mob[k].id != i )
				memmove(&id->mob[k + 1], &id->mob[k], (MAX_SEARCH - k - 1) * sizeof(id->mob[0]));
			id->mob[k].chance = entry->dropitem[d].p;
			id->mob[k].id = i;
		}
	}
}

/**
 * Reload Item DB
 */
void itemdb_reload(void) {
	struct s_mapiterator *iter;
	struct map_session_data *sd;

	itemdb_group->clear(itemdb_group, itemdb_group_free);
	itemdb_randomopt->clear(itemdb_randomopt, itemdb_randomopt_free);
	itemdb_randomopt_group->clear(itemdb_randomopt_group, itemdb_randomopt_group_free);
	itemdb->clear(itemdb, itemdb_final_sub);
	db_clear(itemdb_combo);

	if( battle_config.feature_roulette )
		itemdb_roulette_free();

	//Read new data
	itemdb_read();
	cashshop_reloaddb();

	if( battle_config.feature_roulette )
		itemdb_roulette_read();

	itemdb_reload_itemmob_data();

	//Read just itemdb pointer cache for each player
	iter = mapit_geteachpc();
	for( sd = (struct map_session_data *)mapit_first(iter); mapit_exists(iter); sd = (struct map_session_data *)mapit_next(iter) ) {
		memset(sd->item_delay, 0, sizeof(sd->item_delay)); //Reset item delays
		pc_setinventorydata(sd);
		pc_check_available_item(sd, ITMCHK_ALL); //Check for invalid(ated) items
		//Clear combo bonuses
		if( sd->combos.count ) {
			aFree(sd->combos.bonus);
			aFree(sd->combos.id);
			aFree(sd->combos.pos);
			sd->combos.bonus = NULL;
			sd->combos.id = NULL;
			sd->combos.pos = NULL;
			sd->combos.count = 0;
			if( pc_load_combo(sd) > 0 )
				status_calc_pc(sd, SCO_FORCE);
		}

	}
	mapit_free(iter);
}

/**
 * Finalizing Item DB
 */
void do_final_itemdb(void) {
	db_destroy(itemdb_combo);
	itemdb_group->destroy(itemdb_group, itemdb_group_free);
	itemdb_randomopt->destroy(itemdb_randomopt, itemdb_randomopt_free);
	itemdb_randomopt_group->destroy(itemdb_randomopt_group, itemdb_randomopt_group_free);
	itemdb->destroy(itemdb, itemdb_final_sub);
	destroy_item_data(dummy_item);
	if( battle_config.feature_roulette )
		itemdb_roulette_free();
}

/**
 * Initializing Item DB
 */
void do_init_itemdb(void) {
	itemdb = uidb_alloc(DB_OPT_BASE);
	itemdb_combo = uidb_alloc(DB_OPT_BASE);
	itemdb_group = uidb_alloc(DB_OPT_BASE);
	itemdb_randomopt = uidb_alloc(DB_OPT_BASE);
	itemdb_randomopt_group = uidb_alloc(DB_OPT_BASE);
	itemdb_create_dummy();
	itemdb_read();
	if( battle_config.feature_roulette )
		itemdb_roulette_read();
}
