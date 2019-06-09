// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/mmo.h"
#include "../common/malloc.h"
#include "../common/strlib.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/utils.h"
#include "../common/sql.h"
#include "char.h"
#include "inter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool mapif_elemental_save(struct s_elemental *ele) {
	bool flag = true;

	if( ele->elemental_id == 0 ) { // Create new DB entry
		if( SQL_ERROR == Sql_Query(sql_handle,
			"INSERT INTO `%s` (`char_id`,`kind`,`scale`,`hp`,`sp`,`max_hp`,`max_sp`,`atk`,`matk`,`aspd`,`def`,`mdef`,`flee`,`hit`,`life_time`)"
			"VALUES ('%d','%d','%d','%u','%u','%u','%u','%d','%d','%d','%d','%d','%d','%d','%u')",
			elemental_db, ele->char_id, ele->kind, ele->scale, ele->hp, ele->sp, ele->max_hp, ele->max_sp,
			ele->atk, ele->matk, ele->amotion, ele->def, ele->mdef, ele->flee, ele->hit, ele->life_time) )
		{
			Sql_ShowDebug(sql_handle);
			flag = false;
		} else
			ele->elemental_id = (int)Sql_LastInsertId(sql_handle);
	} else if( SQL_ERROR == Sql_Query(sql_handle,
		"UPDATE `%s` SET `char_id` = '%d', `kind` = '%d', `scale` = '%d', `hp` = '%u', `sp` = '%u',"
		"`max_hp` = '%u', `max_sp` = '%u', `atk` = '%d', `matk` = '%d', `aspd` = '%d', `def` = '%d',"
		"`mdef` = '%d', `flee` = '%d', `hit` = '%d', `life_time` = '%u' WHERE `ele_id` = '%d'",
		elemental_db, ele->char_id, ele->kind, ele->scale, ele->hp, ele->sp, ele->max_hp, ele->max_sp,
		ele->atk, ele->matk, ele->amotion, ele->def, ele->mdef, ele->flee, ele->hit, ele->life_time, ele->elemental_id) )
	{ // Update DB entry
		Sql_ShowDebug(sql_handle);
		flag = false;
	}
	return flag;
}

bool mapif_elemental_load(int ele_id, int char_id, struct s_elemental *ele) {
	char *data;

	memset(ele, 0, sizeof(struct s_elemental));

	ele->elemental_id = ele_id;
	ele->char_id = char_id;

	if( SQL_ERROR == Sql_Query(sql_handle,
		"SELECT `kind`, `scale`, `hp`, `sp`, `max_hp`, `max_sp`, `atk`, `matk`, `aspd`,"
		"`def`, `mdef`, `flee`, `hit`, `life_time` FROM `%s` WHERE `ele_id` = '%d' AND `char_id` = '%d'",
		elemental_db, ele_id, char_id) )
	{
		Sql_ShowDebug(sql_handle);
		return false;
	}

	if( SQL_SUCCESS != Sql_NextRow(sql_handle) ) {
		Sql_FreeResult(sql_handle);
		return false;
	}

	Sql_GetData(sql_handle,  0, &data, NULL); ele->kind = (enum elemental_type)atoi(data);
	Sql_GetData(sql_handle,  1, &data, NULL); ele->scale = atoi(data);
	Sql_GetData(sql_handle,  2, &data, NULL); ele->hp = atoi(data);
	Sql_GetData(sql_handle,  3, &data, NULL); ele->sp = atoi(data);
	Sql_GetData(sql_handle,  4, &data, NULL); ele->max_hp = atoi(data);
	Sql_GetData(sql_handle,  5, &data, NULL); ele->max_sp = atoi(data);
	Sql_GetData(sql_handle,  6, &data, NULL); ele->atk = atoi(data);
	Sql_GetData(sql_handle,  7, &data, NULL); ele->matk = atoi(data);
	Sql_GetData(sql_handle,  8, &data, NULL); ele->amotion = atoi(data);
	Sql_GetData(sql_handle,  9, &data, NULL); ele->def = atoi(data);
	Sql_GetData(sql_handle, 10, &data, NULL); ele->mdef = atoi(data);
	Sql_GetData(sql_handle, 11, &data, NULL); ele->flee = atoi(data);
	Sql_GetData(sql_handle, 12, &data, NULL); ele->hit = atoi(data);
	Sql_GetData(sql_handle, 13, &data, NULL); ele->life_time = atoi(data);
	Sql_FreeResult(sql_handle);

	if( save_log )
		ShowInfo("Elemental loaded (ID: %d / CID: %d).\n", ele->elemental_id, ele->char_id);
	return true;
}

static void mapif_parse_elemental_request_sc_save(int fd) {
#ifdef ENABLE_SC_SAVING
	int cid = RFIFOL(fd,4);
	int eid = RFIFOL(fd,8);
	int count = RFIFOW(fd,12);

	if( SQL_ERROR == Sql_Query(sql_handle,
		"DELETE FROM `%s` WHERE `char_id` = '%d' AND `ele_id`='%d'", elemental_scdata_db, cid, eid) )
		Sql_ShowDebug(sql_handle);

	if( count > 0 ) {
		struct status_change_data data;
		StringBuf buf;
		int i;

		StringBuf_Init(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `%s` (`char_id`, `ele_id`, `type`, `tick`, `val1`, `val2`, `val3`, `val4`) VALUES ", elemental_scdata_db);
		for( i = 0; i < count; ++i ) {
			memcpy(&data, RFIFOP(fd, 14 + i * sizeof(struct status_change_data)), sizeof(struct status_change_data));
			if( i > 0 )
				StringBuf_AppendStr(&buf, ", ");
			StringBuf_Printf(&buf, "('%d','%d','%hu','%d','%d','%d','%d','%d')", cid, eid,
				data.type, data.tick, data.val1, data.val2, data.val3, data.val4);
		}
		if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
			Sql_ShowDebug(sql_handle);
		StringBuf_Destroy(&buf);
	}
#endif
}

static void mapif_parse_elemental_request_sc_load(int fd, int char_id, int ele_id) {
#ifdef ENABLE_SC_SAVING
	if( SQL_ERROR == Sql_Query(sql_handle,
		"SELECT `type`, `tick`, `val1`, `val2`, `val3`, `val4` FROM `%s` WHERE `char_id` = '%d' AND `ele_id` = '%d'",
		elemental_scdata_db, char_id, ele_id) )
	{
		Sql_ShowDebug(sql_handle);
		return;
	}
	if( Sql_NumRows(sql_handle) > 0 ) {
		struct status_change_data scdata;
		int count;
		char *data;

		memset(&scdata, 0, sizeof(scdata));

		WFIFOHEAD(fd, 14 + 50 * sizeof(struct status_change_data));
		WFIFOW(fd,0) = 0x3894;
		WFIFOL(fd,4) = char_id;
		WFIFOL(fd,8) = ele_id;
		for( count = 0; count < 50 && SQL_SUCCESS == Sql_NextRow(sql_handle); ++count ) {
			Sql_GetData(sql_handle, 0, &data, NULL); scdata.type = atoi(data);
			Sql_GetData(sql_handle, 1, &data, NULL); scdata.tick = atoi(data);
			Sql_GetData(sql_handle, 2, &data, NULL); scdata.val1 = atoi(data);
			Sql_GetData(sql_handle, 3, &data, NULL); scdata.val2 = atoi(data);
			Sql_GetData(sql_handle, 4, &data, NULL); scdata.val3 = atoi(data);
			Sql_GetData(sql_handle, 5, &data, NULL); scdata.val4 = atoi(data);
			memcpy(WFIFOP(fd, 14 + count * sizeof(struct status_change_data)), &scdata, sizeof(struct status_change_data));
		}
		if( count >= 50 )
			ShowWarning("Too many status changes for %d:%d, some of them were not loaded.\n", char_id, ele_id);
		if( count > 0 ) {
			WFIFOW(fd,2) = 14 + count * sizeof(struct status_change_data);
			WFIFOW(fd,12) = count;
			WFIFOSET(fd,WFIFOW(fd,2));
			//Clear the data once loaded
			if( SQL_ERROR == Sql_Query(sql_handle,
				"DELETE FROM `%s` WHERE `char_id` = '%d' AND `ele_id`='%d'", elemental_scdata_db, char_id, ele_id) )
				Sql_ShowDebug(sql_handle);
		}
	} else { //No SC (needs a response)
		WFIFOHEAD(fd,14);
		WFIFOW(fd,0) = 0x3894;
		WFIFOW(fd,2) = 14;
		WFIFOL(fd,4) = char_id;
		WFIFOL(fd,8) = ele_id;
		WFIFOW(fd,12) = 0;
		WFIFOSET(fd,WFIFOW(fd,2));
	}
	Sql_FreeResult(sql_handle);
#endif
}

bool mapif_elemental_delete(int ele_id) {
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `ele_id` = '%d'", elemental_db, ele_id) ) {
		Sql_ShowDebug(sql_handle);
		return false;
	}
#ifdef ENABLE_SC_SAVING
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `ele_id` = '%d'", elemental_scdata_db, ele_id) ) {
		Sql_ShowDebug(sql_handle);
		return false;
	}
#endif
	return true;
}

static void mapif_elemental_send(int fd, struct s_elemental *ele, unsigned char flag) {
	int size = sizeof(struct s_elemental) + 5;

	WFIFOHEAD(fd,size);
	WFIFOW(fd,0) = 0x387c;
	WFIFOW(fd,2) = size;
	WFIFOB(fd,4) = flag;
	memcpy(WFIFOP(fd,5),ele,sizeof(struct s_elemental));
	WFIFOSET(fd,size);
}

static void mapif_parse_elemental_create(int fd, struct s_elemental *ele) {
	bool result = mapif_elemental_save(ele);

	mapif_elemental_send(fd, ele, result);
}

static void mapif_parse_elemental_load(int fd, int ele_id, int char_id) {
	struct s_elemental ele;
	bool result = mapif_elemental_load(ele_id, char_id, &ele);

	mapif_elemental_send(fd, &ele, result);
}

static void mapif_elemental_deleted(int fd, unsigned char flag) {
	WFIFOHEAD(fd,3);
	WFIFOW(fd,0) = 0x387d;
	WFIFOB(fd,2) = flag;
	WFIFOSET(fd,3);
}

static void mapif_parse_elemental_delete(int fd, int ele_id) {
	bool result = mapif_elemental_delete(ele_id);

	mapif_elemental_deleted(fd, result);
}

static void mapif_elemental_saved(int fd, unsigned char flag) {
	WFIFOHEAD(fd,3);
	WFIFOW(fd,0) = 0x387e;
	WFIFOB(fd,2) = flag;
	WFIFOSET(fd,3);
}

static void mapif_parse_elemental_save(int fd, struct s_elemental *ele) {
	bool result = mapif_elemental_save(ele);

	mapif_elemental_saved(fd, result);
}

void inter_elemental_sql_init(void) {
	return;
}
void inter_elemental_sql_final(void) {
	return;
}

/*==========================================
 * Inter Packets
 *------------------------------------------*/
int inter_elemental_parse_frommap(int fd) {
	unsigned short cmd = RFIFOW(fd,0);

	switch( cmd ) {
		case 0x307a: mapif_parse_elemental_request_sc_save(fd); break;
		case 0x307b: mapif_parse_elemental_request_sc_load(fd, (int)RFIFOL(fd,4), (int)RFIFOL(fd,8)); break;
		case 0x307c: mapif_parse_elemental_create(fd, (struct s_elemental *)RFIFOP(fd,4)); break;
		case 0x307d: mapif_parse_elemental_load(fd, (int)RFIFOL(fd,2), (int)RFIFOL(fd,6)); break;
		case 0x307e: mapif_parse_elemental_delete(fd, (int)RFIFOL(fd,2)); break;
		case 0x307f: mapif_parse_elemental_save(fd, (struct s_elemental *)RFIFOP(fd,4)); break;
		default:
			return 0;
	}
	return 1;
}
