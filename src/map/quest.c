// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/conf.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/malloc.h"
#include "../common/nullpo.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "map.h"
#include "pc.h"
#include "npc.h"
#include "itemdb.h"
#include "script.h"
#include "intif.h"
#include "battle.h"
#include "mob.h"
#include "party.h"
#include "unit.h"
#include "log.h"
#include "clif.h"
#include "quest.h"
#include "chrif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

struct config_t quest_db_conf;
static DBMap *questdb = NULL; //int quest_id -> struct quest_db*
static void questdb_free_sub(struct quest_db *quest, bool free);

/**
 * Searches a quest by ID.
 *
 * @param quest_id ID to lookup
 * @return Quest entry (equals to &quest_dummy if the ID is invalid)
 */
struct quest_db *quest_search(int quest_id) {
	struct quest_db *quest = (struct quest_db *)idb_get(questdb, quest_id);

	if( !quest )
		return &quest_dummy;
	return quest;
}

/**
 * Sends quest info to the player on login.
 *
 * @param sd Player's data
 * @return 0 in case of success, nonzero otherwise (i.e. the player has no quests)
 */
int quest_pc_login(TBL_PC *sd) {
#if PACKETVER < 20141022
	int i;
#endif

	if( !sd->avail_quests )
		return 1;

	clif_quest_send_list(sd);
#if PACKETVER < 20141022
	clif_quest_send_mission(sd);

	//@TODO[Haru]: Is this necessary? Does quest_send_mission not take care of this?
	for( i = 0; i < sd->avail_quests; i++ )
		clif_quest_update_objective(sd, &sd->quest_log[i]);
#endif

	return 0;
}

/**
 * Adds a quest to the player's list.
 *
 * New quest will be added as Q_ACTIVE.
 *
 * @param sd       Player's data
 * @param quest_id ID of the quest to add.
 * @return 0 in case of success, nonzero otherwise
 */
int quest_add(struct map_session_data *sd, int quest_id) {
	int n;
	struct quest_db *qi = quest_search(quest_id);

	if( qi == &quest_dummy ) {
		ShowError("quest_add: quest %d not found in DB.\n", quest_id);
		return -1;
	}

	if( quest_check(sd, quest_id, HAVEQUEST) >= 0 ) {
		ShowError("quest_add: Character %d already has quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	n = sd->avail_quests; //Insertion point

	sd->num_quests++;
	sd->avail_quests++;
	RECREATE(sd->quest_log, struct quest, sd->num_quests);

	//The character has some completed quests, make room before them so that they will stay at the end of the array
	if( sd->avail_quests != sd->num_quests )
		memmove(&sd->quest_log[n + 1], &sd->quest_log[n], sizeof(struct quest) * (sd->num_quests - sd->avail_quests));

	memset(&sd->quest_log[n], 0, sizeof(struct quest));

	sd->quest_log[n].quest_id = qi->id;
	if( qi->time ) {
		if( !qi->time_type )
			sd->quest_log[n].time = (unsigned int)(time(NULL) + qi->time);
		else { //Quest time limit at HH:MM
			int time_today;
			time_t t;
			struct tm *lt;

			t = time(NULL);
			lt = localtime(&t);
			time_today = lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec;
			if( time_today < qi->time )
				sd->quest_log[n].time = (unsigned int)(time(NULL) + qi->time - time_today);
			else //Next day
				sd->quest_log[n].time = (unsigned int)(time(NULL) + 86400 + qi->time - time_today);
		}
	}
	sd->quest_log[n].state = Q_ACTIVE;

	sd->save_quest = true;

	clif_quest_add(sd, &sd->quest_log[n]);
	clif_quest_update_objective(sd, &sd->quest_log[n]);

	if( save_settings&CHARSAVE_QUEST )
		chrif_save(sd,CSAVE_NORMAL);

	return 0;
}

/**
 * Replaces a quest in a player's list with another one.
 *
 * @param sd   Player's data
 * @param qid1 Current quest to replace
 * @param qid2 New quest to add
 * @return 0 in case of success, nonzero otherwise
 */
int quest_change(struct map_session_data *sd, int qid1, int qid2) {
	int i;
	struct quest_db *qi = quest_search(qid2);

	if( qi == &quest_dummy ) {
		ShowError("quest_change: quest %d not found in DB.\n", qid2);
		return -1;
	}

	if( quest_check(sd, qid2, HAVEQUEST) >= 0 ) {
		ShowError("quest_change: Character %d already has quest %d.\n", sd->status.char_id, qid2);
		return -1;
	}

	if( quest_check(sd, qid1, HAVEQUEST) < 0 ) {
		ShowError("quest_change: Character %d doesn't have quest %d.\n", sd->status.char_id, qid1);
		return -1;
	}

	ARR_FIND(0, sd->avail_quests, i, sd->quest_log[i].quest_id == qid1);
	if( i == sd->avail_quests ) {
		ShowError("quest_change: Character %d has completed quest %d.\n", sd->status.char_id, qid1);
		return -1;
	}

	memset(&sd->quest_log[i], 0, sizeof(struct quest));
	sd->quest_log[i].quest_id = qi->id;
	if( qi->time ) {
		if( !qi->time_type )
			sd->quest_log[i].time = (unsigned int)(time(NULL) + qi->time);
		else { //Quest time limit at HH:MM
			int time_today;
			time_t t;
			struct tm *lt;

			t = time(NULL);
			lt = localtime(&t);
			time_today = lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec;
			if( time_today < qi->time )
				sd->quest_log[i].time = (unsigned int)(time(NULL) + qi->time - time_today);
			else //Next day
				sd->quest_log[i].time = (unsigned int)(time(NULL) + 86400 + qi->time - time_today);
		}
	}
	sd->quest_log[i].state = Q_ACTIVE;

	sd->save_quest = true;

	clif_quest_delete(sd, qid1);
	clif_quest_add(sd, &sd->quest_log[i]);
	clif_quest_update_objective(sd, &sd->quest_log[i]);

	if( save_settings&CHARSAVE_QUEST )
		chrif_save(sd,CSAVE_NORMAL);

	return 0;
}

/**
 * Removes a quest from a player's list
 *
 * @param sd       Player's data
 * @param quest_id ID of the quest to remove
 * @return 0 in case of success, nonzero otherwise
 */
int quest_delete(struct map_session_data *sd, int quest_id) {
	int i;

	//Search for quest
	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].quest_id == quest_id);
	if( i == sd->num_quests ) {
		ShowError("quest_delete: Character %d doesn't have quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}
	if( sd->quest_log[i].state != Q_COMPLETE )
		sd->avail_quests--;
	if( i < --sd->num_quests ) //Compact the array
		memmove(&sd->quest_log[i], &sd->quest_log[i + 1], sizeof(struct quest) * (sd->num_quests - i));
	if( sd->num_quests == 0 ) {
		aFree(sd->quest_log);
		sd->quest_log = NULL;
	} else
		RECREATE(sd->quest_log, struct quest, sd->num_quests);
	sd->save_quest = true;

	clif_quest_delete(sd, quest_id);

	if( save_settings&CHARSAVE_QUEST )
		chrif_save(sd,CSAVE_NORMAL);

	return 0;
}

/**
 * Map iterator subroutine to update quest objectives for a party after killing a monster.
 *
 * @see map_foreachinallrange
 * @param ap Argument list, expecting:
 *           int Party ID
 *           int Mob ID
 */
int quest_update_objective_sub(struct block_list *bl, va_list ap) {
	struct map_session_data *sd;
	int party_id, mob_id, mob_size, mob_race, mob_element;
	uint16 mob_level;

	nullpo_ret(bl);
	nullpo_ret(sd = (struct map_session_data *)bl);

	party_id = va_arg(ap,int);
	mob_id = va_arg(ap,int);
	mob_size = va_arg(ap,int);
	mob_race = va_arg(ap,int);
	mob_element = va_arg(ap,int);
	mob_level = va_arg(ap,unsigned short);

	if( !sd->avail_quests )
		return 0;

	if( sd->status.party_id != party_id )
		return 0;

	quest_update_objective(sd, mob_id, mob_size, mob_race, mob_element, mob_level);

	return 1;
}

/**
 * Updates the quest objectives for a character after killing a monster, including the handling of quest-granted drops.
 *
 * @param sd     Character's data
 * @param mob_id Monster ID
 */
void quest_update_objective(struct map_session_data *sd, int mob_id, int mob_size, int mob_race, int mob_element, uint16 mob_level) {
	int i, j;

	for( i = 0; i < sd->avail_quests; i++ ) {
		struct quest_db *qi = NULL;

		if( sd->quest_log[i].state == Q_COMPLETE ) //Skip complete quests
			continue;

		qi = quest_search(sd->quest_log[i].quest_id);

		for( j = 0; j < qi->objective_count; j++ ) {
			bool is_valid = false;
			bool is_level = (qi->objectives[j].min_level > 0);

			switch( qi->objectives[j].mobtype ) {
				case MOB_TYPE_SIZE_SMALL:
					if( mob_size == SZ_SMALL )
						is_valid = true;
					break;
				case MOB_TYPE_SIZE_MEDIUM:
					if( mob_size == SZ_MEDIUM )
						is_valid = true;
					break;
				case MOB_TYPE_SIZE_BIG:
					if( mob_size == SZ_BIG )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_DEMIHUMAN:
					if( mob_race == RC_DEMIHUMAN )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_BRUTE:
					if( mob_race == RC_BRUTE )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_INSECT:
					if( mob_race == RC_INSECT )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_FISH:
					if( mob_race == RC_FISH )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_PLANT:
					if( mob_race == RC_PLANT )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_DEMON:
					if( mob_race == RC_DEMON )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_ANGEL:
					if( mob_race == RC_ANGEL )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_UNDEAD:
					if( mob_race == RC_UNDEAD )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_FORMLESS:
					if( mob_race == RC_FORMLESS )
						is_valid = true;
					break;
				case MOB_TYPE_RACE_DRAGON:
					if( mob_race == RC_DRAGON )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_WATER:
					if( mob_element == ELE_WATER )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_WIND:
					if( mob_element == ELE_WIND )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_EARTH:
					if( mob_element == ELE_EARTH )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_FIRE:
					if( mob_element == ELE_FIRE )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_DARK:
					if( mob_element == ELE_DARK )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_HOLY:
					if( mob_element == ELE_HOLY )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_POISON:
					if( mob_element == ELE_POISON )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_GHOST:
					if( mob_element == ELE_GHOST )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_NEUTRAL:
					if( mob_element == ELE_NEUTRAL )
						is_valid = true;
					break;
				case MOB_TYPE_DEF_ELE_UNDEAD:
					if( mob_element == ELE_UNDEAD )
						is_valid = true;
					break;
				default:
					if( qi->objectives[j].mob == mob_id )
						is_valid = true;
					break;
			}
			if( is_valid && is_level && mob_level < qi->objectives[j].min_level )
				is_valid = false;
			if( is_valid && sd->quest_log[i].count[j] < qi->objectives[j].count )  {
				sd->quest_log[i].count[j]++;
				sd->save_quest = true;
				clif_quest_update_objective(sd, &sd->quest_log[i]);
			}
		}

		//Process quest-granted extra drop bonuses
		for( j = 0; j < qi->dropitem_count; j++ ) {
			struct quest_dropitem *entry = &qi->dropitems[j];
			struct item item;
			int temp;

			if( entry->mob_id && entry->mob_id != mob_id )
				continue;
			if( entry->rate < 10000 && rnd()%10000 >= entry->rate )
				continue;
			if( !itemdb_exists(entry->nameid) )
				continue;

			memset(&item, 0, sizeof(item));
			item.nameid = entry->nameid;
			item.identify = itemdb_isidentified(entry->nameid);
			item.amount = 1;
//			item.amount = entry->count;
//#ifdef BOUND_ITEMS
//			item.bound = entry->bound;
//#endif
//			if( entry->isGUID )
//				item.unique_id = pc_generate_unique_id(sd);
			if( (temp = pc_additem(sd, &item, 1, LOG_TYPE_QUEST)) ) //Failed to obtain the item
				clif_additem(sd, 0, 0, temp);
//			else if( entry->isAnnounced || itemdb_exists(entry->nameid)->flag.broadcast )
//				intif_broadcast_obtain_special_item(sd, entry->nameid, entry->mob_id, ITEMOBTAIN_TYPE_MONSTER_ITEM);
		}
	}
}

/**
 * Updates a quest's state.
 *
 * Only status of active and inactive quests can be updated. Completed quests can't (for now). [Inkfish]
 *
 * @param sd       Character's data
 * @param quest_id Quest ID to update
 * @param qs       New quest state
 * @return 0 in case of success, nonzero otherwise
 */
int quest_update_status(struct map_session_data *sd, int quest_id, enum quest_state status) {
	int i;

	ARR_FIND(0, sd->avail_quests, i, sd->quest_log[i].quest_id == quest_id);
	if( i == sd->avail_quests ) {
		ShowError("quest_update_status: Character %d doesn't have quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	sd->quest_log[i].state = status;
	sd->save_quest = true;

	if( status < Q_COMPLETE ) {
		clif_quest_update_status(sd, quest_id, (status == Q_ACTIVE ? true : false));
		return 0;
	}

	//The quest is complete, so it needs to be moved to the completed quests block at the end of the array
	if( i < --(sd->avail_quests) ) {
		struct quest tmp_quest;

		memcpy(&tmp_quest, &sd->quest_log[i], sizeof(struct quest));
		memcpy(&sd->quest_log[i], &sd->quest_log[sd->avail_quests], sizeof(struct quest));
		memcpy(&sd->quest_log[sd->avail_quests], &tmp_quest, sizeof(struct quest));
	}

	clif_quest_delete(sd, quest_id);

	if( save_settings&CHARSAVE_QUEST )
		chrif_save(sd,CSAVE_NORMAL);

	return 0;
}

/**
 * Queries quest information for a character.
 *
 * @param sd       Character's data
 * @param quest_id Quest ID
 * @param type     Check type
 * @return -1 if the quest was not found, otherwise it depends on the type:
 *         HAVEQUEST: The quest's state
 *         PLAYTIME:  2 if the quest's timeout has expired
 *                    1 if the quest was completed
 *                    0 otherwise
 *         HUNTING:   2 if the quest has not been marked as completed yet, and its objectives have been fulfilled
 *                    1 if the quest's timeout has expired
 *                    0 otherwise
 */
int quest_check(struct map_session_data *sd, int quest_id, enum quest_check_type type) {
	int i;

	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].quest_id == quest_id);
	if( i == sd->num_quests )
		return -1;

	switch( type ) {
		case HAVEQUEST:
			return sd->quest_log[i].state;
		case PLAYTIME:
			return (sd->quest_log[i].time < (unsigned int)time(NULL) ? 2 : (sd->quest_log[i].state == Q_COMPLETE ? 1 : 0));
		case HUNTING:
			if( sd->quest_log[i].state == Q_INACTIVE || sd->quest_log[i].state == Q_ACTIVE ) {
				int j;
				struct quest_db *qi = quest_search(sd->quest_log[i].quest_id);

				ARR_FIND(0, qi->objective_count, j, sd->quest_log[i].count[j] < qi->objectives[j].count);
				if( j == qi->objective_count )
					return 2;
				if( sd->quest_log[i].time < (unsigned int)time(NULL) )
					return 1;
			}
			return 0;
		default:
			ShowError("quest_check_quest: Unknown parameter %d", type);
			break;
	}

	return -1;
}



/**
 * Reads and parses an entry from the quest_db.
 *
 * @param cs     The config setting containing the entry.
 * @param n      The sequential index of the current config setting.
 * @param source The source configuration file.
 * @return The parsed quest entry.
 * @retval NULL in case of errors.
 */
struct quest_db *quest_readdb_sub(struct config_setting_t *cs, int n, const char *source)
{
	struct quest_db *entry = NULL;
	struct config_setting_t *t = NULL;
	int quest_id;
	const char *quest_name = NULL, *quest_time = NULL;

	nullpo_retr(NULL, cs);

	if( !config_setting_lookup_int(cs, "Id", &quest_id) ) {
		ShowWarning("quest_readdb_sub: Missing id in \"%s\", entry #%d, skipping.\n", source, n);
		return NULL;
	}

	if( quest_id < 1 || quest_id >= INT_MAX ) {
		ShowWarning("quest_readdb_sub: Invalid quest ID '%d' in \"%s\", entry #%d (min: 1, max: %d), skipping.\n", quest_id, source, n, INT_MAX);
		return NULL;
	}

	if( !config_setting_lookup_string(cs, "Name", &quest_name) || !*quest_name ) {
		ShowWarning("quest_readdb_sub: Missing Name in quest %d of \"%s\", skipping.\n", quest_id, source);
		return NULL;
	}

	CREATE(entry, struct quest_db, 1);
	entry->id = quest_id;
	//StringBuf_Init(&entry->name);
	//StringBuf_Printf(&entry->name, "%s", quest_name);

	if( config_setting_lookup_string(cs, "TimeLimit", &quest_time) ) {
		if( !strchr(quest_time, ':') ) {
			entry->time = atoi(quest_time);
			entry->time_type = 0;
		} else {
			unsigned char hour, min;
			char *qtime;
			bool fail = false;

			hour = (unsigned char)strtol(quest_time, &qtime, 10);
			if( !qtime || *qtime != ':' ) {
				ShowWarning("quest_readdb_sub: Cannot parse hour: '%s' - wrong format\n", quest_time);
				fail = true;
			}
			min = (unsigned char)strtol(qtime + 1, &qtime, 10);
			if( !qtime || *qtime != '\0' ) {
				ShowWarning("quest_readdb_sub: Cannot parse minute: '%s' - wrong format\n", quest_time);
				fail = true;
			}
			if( !fail ) {
				entry->time = hour * 3600 + min * 60;
				entry->time_type = 1;
			} else {
				entry->time = 0;
				entry->time_type = 0;
			}
		}
	}

	if( (t = config_setting_get_member(cs, "Targets")) && config_setting_is_list(t) ) {
		int i, len = config_setting_length(t);

		for( i = 0; i < len && entry->objective_count < MAX_QUEST_OBJECTIVES; i++ ) {
			// Note: We ensure that objective_count < MAX_QUEST_OBJECTIVES because
			//       quest_log (as well as the client) expect this maximum size.
			struct config_setting_t *tt = config_setting_get_elem(t, i);
			int mob_id = 0, mob_type = 0, min_level = 0, max_level = 0, count = 0, i32;
			const char *name;

			if( !tt )
				break;
			if( !config_setting_is_group(tt) )
				continue;
			if( config_setting_lookup_int(tt, "MobId", &i32) )
				mob_id = i32;
			if( mob_id && !mobdb_exists(mob_id) ) {
				ShowWarning("quest_readdb_sub: Invalid value for 'MobId': %d, defaulting to 0\n", mob_id);
				mob_id = 0;
			}
			if( config_setting_lookup_string(tt, "MobType", &name) && !script_get_constant(name, &mob_type) ) {
				ShowWarning("quest_readdb_sub: Invalid value for 'MobType': %d, defaulting to 0\n", mob_type);
				mob_type = 0;
			}
			if( config_setting_lookup_int(tt, "MinLevel", &i32) )
				min_level = max(i32, 0);
			if( config_setting_lookup_int(tt, "MaxLevel", &i32) )
				max_level = max(i32, 0);
			if( !min_level || min_level > max_level )
				max_level = 0;
			if( min_level && !max_level )
				max_level = 999;
			if( config_setting_lookup_int(tt, "Count", &i32) )
				count = i32;
			if( count <= 0 ) {
				ShowWarning("quest_readdb_sub: Invalid value for 'Count': %d, skipping.\n", mob_id);
				continue;
			}
			if( !mob_id && !mob_type )
				continue;
			RECREATE(entry->objectives, struct quest_objective, entry->objective_count + 1);
			entry->objectives[entry->objective_count].mob = mob_id;
			entry->objectives[entry->objective_count].mobtype = mob_type;
			entry->objectives[entry->objective_count].min_level = min_level;
			entry->objectives[entry->objective_count].max_level = max_level;
			entry->objectives[entry->objective_count].count = count;
			entry->objective_count++;
		}
	}

	if( (t = config_setting_get_member(cs, "Drops")) && config_setting_is_list(t) ) {
		int i, len = config_setting_length(t);

		for( i = 0; i < len; i++ ) {
			struct config_setting_t *tt = config_setting_get_elem(t, i);
			int mob_id = 0, nameid = 0, rate = 0;

			if( !tt )
				break;
			if( !config_setting_is_group(tt) )
				continue;
			if( !config_setting_lookup_int(tt, "MobId", &mob_id) )
				mob_id = 0; //Zero = any monster
			if( mob_id && !mobdb_exists(mob_id) )
				continue;
			if( !config_setting_lookup_int(tt, "ItemId", &nameid) || (nameid && !itemdb_exists(nameid)) )
				continue;
			if( !config_setting_lookup_int(tt, "Rate", &rate) || rate <= 0 )
				continue;
			RECREATE(entry->dropitems, struct quest_dropitem, entry->dropitem_count + 1);
			entry->dropitems[entry->dropitem_count].mob_id = mob_id;
			entry->dropitems[entry->dropitem_count].nameid = nameid;
			entry->dropitems[entry->dropitem_count].rate = rate;
			entry->dropitem_count++;
		}
	}
	return entry;
}

/**
 * Loads quests from the quest db.
 */
void quest_readdb(void)
{
	struct config_setting_t *qdb = NULL, *q = NULL;
	char filepath[256];
	int count = 0;

	safesnprintf(filepath, sizeof(filepath), "%s/%s", db_path, "quest_db.conf");
	if( config_read_file(&quest_db_conf, filepath) )
		return;

	if( !(qdb = config_lookup(&quest_db_conf, "quest_db")) )
		return;

	while( (q = config_setting_get_elem(qdb, count)) ) {
		struct quest_db *entry = quest_readdb_sub(q, count, filepath);

		if( !entry ) {
			ShowWarning("quest_readdb: Failed to parse quest entry %d.\n", count);
			continue;
		}
		if( quest_search(entry->id) != &quest_dummy ) {
			ShowWarning("quest_readdb: Duplicate quest %d.\n", entry->id);
			questdb_free_sub(entry, false);
			continue;
		}
		idb_put(questdb, entry->id, entry);
		count++;
	}

	config_destroy(&quest_db_conf);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, filepath);
}

/**
 * Map iterator to ensures a player has no invalid quest log entries.
 *
 * Any entries that are no longer in the db are removed.
 *
 * @see map_foreachpc
 * @param ap Ignored
 */
int quest_reload_check_sub(struct map_session_data *sd, va_list ap) {
	int i, j;

	nullpo_ret(sd);

	j = 0;
	for( i = 0; i < sd->num_quests; i++ ) {
		struct quest_db *qi = quest_search(sd->quest_log[i].quest_id);

		if( qi == &quest_dummy ) { //Remove no longer existing entries
			if( sd->quest_log[i].state != Q_COMPLETE ) //And inform the client if necessary
				clif_quest_delete(sd, sd->quest_log[i].quest_id);
			continue;
		}
		if( i != j ) {
			//Move entries if there's a gap to fill
			memcpy(&sd->quest_log[j], &sd->quest_log[i], sizeof(struct quest));
		}
		j++;
	}
	sd->num_quests = j;
	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].state == Q_COMPLETE);
	sd->avail_quests = i;

	return 1;
}

/**
 * Clear quest single entry
 * @param quest
 * @param free Will free quest from memory
 */
static void questdb_free_sub(struct quest_db *quest, bool free) {
	if( quest->objectives ) {
		aFree(quest->objectives);
		quest->objectives = NULL;
		quest->objective_count = 0;
	}
	if( quest->dropitems ) {
		aFree(quest->dropitems);
		quest->dropitems = NULL;
		quest->dropitem_count = 0;
	}
	if( &quest->name )
		StringBuf_Destroy(&quest->name);
	if( free )
		aFree(quest);
}

/**
 * Clears the quest database for shutdown or reload.
 */
static int questdb_free(DBKey key, DBData *data, va_list ap) {
	struct quest_db *quest = (struct quest_db *)db_data2ptr(data);

	if( !quest )
		return 0;

	questdb_free_sub(quest, true);
	return 1;
}

/**
 * Initializes the quest interface.
 */
void do_init_quest(void) {
	questdb = idb_alloc(DB_OPT_BASE);
	quest_readdb();
}

/**
 * Finalizes the quest interface before shutdown.
 */
void do_final_quest(void) {
	memset(&quest_dummy, 0, sizeof(quest_dummy));
	questdb->destroy(questdb, questdb_free);
}

/**
 * Reloads the quest database.
 */
void do_reload_quest(void) {
	memset(&quest_dummy, 0, sizeof(quest_dummy));
	questdb->clear(questdb, questdb_free);

	quest_readdb();

	//Update quest data for players, to ensure no entries about removed quests are left over.
	map_foreachpc(&quest_reload_check_sub);
}
