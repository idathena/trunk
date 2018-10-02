// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _QUEST_H_
#define _QUEST_H_

struct quest_dropitem {
	uint16 nameid;
	uint16 rate;
	uint16 mob_id;
	//uint16 count;
	//uint8 bound;
	//bool isAnnounced;
	//bool isGUID;
};

struct quest_objective {
	uint16 mob;
	int mobtype;
	uint16 min_level;
	uint16 max_level;
	uint16 count;
};

struct quest_db {
	int id; //@TODO: find out if signed or unsigned in client
	unsigned int time;
	bool time_type;
	uint8 objective_count;
	struct quest_objective *objectives;
	uint8 dropitem_count;
	struct quest_dropitem *dropitems;
	StringBuf name;
};

struct quest_db quest_dummy; //Dummy entry for invalid quest lookups

//Questlog check types
enum quest_check_type {
	HAVEQUEST, //Query the state of the given quest
	PLAYTIME, //Check if the given quest has been completed or has yet to expire
	HUNTING, //Check if the given hunting quest's requirements have been met
};

int quest_pc_login(TBL_PC *sd);

int quest_add(struct map_session_data *sd, int quest_id);
int quest_delete(struct map_session_data *sd, int quest_id);
int quest_change(struct map_session_data *sd, int qid1, int qid2);
int quest_update_objective_sub(struct block_list *bl, va_list ap);
void quest_update_objective(struct map_session_data *sd, int mob_id, int mob_size, int mob_race, int mob_element, uint16 mob_level);
int quest_update_status(struct map_session_data *sd, int quest_id, enum quest_state status);
int quest_check(struct map_session_data *sd, int quest_id, enum quest_check_type type);
void quest_clear(void);

struct quest_db *quest_search(int quest_id);

void do_init_quest(void);
void do_final_quest(void);
void do_reload_quest(void);

#endif
