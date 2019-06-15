// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _ELEMENTAL_H_
#define _ELEMENTAL_H_

#include "status.h" // struct status_data, struct status_change
#include "unit.h" // struct unit_data

#define MIN_ELETHINKTIME 100
#define MIN_ELEDISTANCE 2
#define MAX_ELEDISTANCE 5

// Enum of Elemental ID
enum elemental_elemantalid {
	ELEMENTALID_AGNI_S = 2114,
	ELEMENTALID_AGNI_M,
	ELEMENTALID_AGNI_L,
	ELEMENTALID_AQUA_S,
	ELEMENTALID_AQUA_M,
	ELEMENTALID_AQUA_L,
	ELEMENTALID_VENTUS_S,
	ELEMENTALID_VENTUS_M,
	ELEMENTALID_VENTUS_L,
	ELEMENTALID_TERA_S,
	ELEMENTALID_TERA_M,
	ELEMENTALID_TERA_L,
};

struct elemental_skill {
	unsigned short id, lv;
};

enum elemental_mode {
	EL_MODE_PASSIVE,
	EL_MODE_DEFENSIVE,
	EL_MODE_OFFENSIVE,
	EL_MODE_WAIT,
	MAX_EL_MODE
};

struct elemental_data {
	struct block_list bl;
	struct unit_data ud;
	struct view_data *vd;
	struct status_data base_status, battle_status;
	struct status_change sc;
	struct regen_data regen;

	struct mob_db *db;
	struct s_elemental elemental;

	int masterteleport_timer;
	struct map_session_data *master;
	int summon_timer;
	int skill_timer;

	unsigned last_thinktime, last_linktime, last_spdrain_time;
	short min_chase;
	int target_id, attacked_id;

	struct elemental_skill skill[MAX_EL_MODE][MAX_EL_SKILL];
};

extern struct elemental_data elemental_db[MAX_ELEMENTAL_CLASS];

bool elemental_class(int class_);
struct view_data *elemental_get_viewdata(int class_);

int elemental_create(struct map_session_data *sd, enum elemental_type kind, int scale, unsigned int lifetime);
int elemental_data_received(struct s_elemental *ele, bool flag);
int elemental_save(struct elemental_data *ed);

int elemental_change_mode_ack(struct elemental_data *ed, enum elemental_mode mode);

void elemental_heal(struct elemental_data *ed, int hp, int sp);
int elemental_dead(struct elemental_data *ed);

int elemental_delete(struct elemental_data *ed, int reply);
void elemental_summon_stop(struct elemental_data *ed);

int elemental_get_lifetime(struct elemental_data *ed);

int elemental_unlocktarget(struct elemental_data *ed);
bool elemental_skillnotok(uint16 skill_id, struct elemental_data *ed);
int elemental_set_target(struct map_session_data *sd, struct block_list *bl);
int elemental_clean_single_effect(struct elemental_data *ed, uint16 skill_id);
int elemental_clean_effect(struct elemental_data *ed);
int elemental_action(struct elemental_data *ed, struct block_list *bl, unsigned int tick);
struct skill_condition elemental_skill_get_requirements(uint16 skill_id, uint16 skill_lv);

#define elemental_stop_walking(ed, type) unit_stop_walking(&(ed)->bl, type)
#define elemental_stop_attack(ed) unit_stop_attack(&(ed)->bl)

void reload_elementaldb(void);
void do_init_elemental(void);
void do_final_elemental(void);

#endif /* _ELEMENTAL_H_ */
