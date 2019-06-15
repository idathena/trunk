// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/conf.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/mmo.h"
#include "../common/showmsg.h"
#include "../common/utils.h"
#include "../common/random.h"
#include "../common/strlib.h"

#include "log.h"
#include "clif.h"
#include "chrif.h"
#include "intif.h"
#include "itemdb.h"
#include "map.h"
#include "pc.h"
#include "status.h"
#include "skill.h"
#include "mob.h"
#include "pet.h"
#include "battle.h"
#include "party.h"
#include "guild.h"
#include "atcommand.h"
#include "script.h"
#include "npc.h"
#include "trade.h"
#include "unit.h"
#include "elemental.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct config_t ele_db_conf;
struct elemental_data elemental_db[MAX_ELEMENTAL_CLASS]; //Elemental Database

int elemental_search_index(enum elemental_type kind, int scale, int class_) {
	int i;

	if( !class_ )
		ARR_FIND(0, ARRAYLENGTH(elemental_db), i, elemental_db[i].elemental.scale == scale && elemental_db[i].elemental.kind == kind);
	else
		ARR_FIND(0, ARRAYLENGTH(elemental_db), i, elemental_db[i].db && elemental_db[i].db->vd.class_ == class_);

	return (i == MAX_ELEMENTAL_CLASS) ? -1 : i;
}

bool elemental_class(int class_) {
	return (bool)(elemental_search_index(0, 0, class_) > -1);
}

struct view_data *elemental_get_viewdata(int class_) {
	int i = elemental_search_index(0, 0, class_);

	if( i < 0 )
		return 0;

	return &elemental_db[i].db->vd;
}

int elemental_create(struct map_session_data *sd, enum elemental_type kind, int scale, unsigned int lifetime) {
	struct s_elemental ele;
	int skill_level;

	nullpo_retr(1, sd);

	if( kind < EL_KIND_AGNI || kind > EL_KIND_TERA )
		return 0;

	memset(&ele, 0, sizeof(struct s_elemental));

	//Elemental stat formulas [malufett]
	ele.char_id = sd->status.char_id;
	ele.kind = kind;
	ele.scale = scale;
	ele.amotion = max(0x190, 0x2ee - 0x2d / scale - status_get_lv(&sd->bl) - sd->battle_status.dex);
	ele.def = (short)status_get_def(&sd->bl) + status_get_def2(&sd->bl) + status_get_lv(&sd->bl) / (5 - scale);
	ele.mdef = status_get_mdef(&sd->bl) + status_get_mdef2(&sd->bl) + sd->battle_status.int_ / (5 - scale);
	ele.atk = sd->battle_status.max_sp / (18 / scale);
	ele.matk = scale * (sd->battle_status.int_ / 2 + sd->battle_status.dex / 4);
	ele.flee = sd->battle_status.flee + status_get_lv(&sd->bl) / (5 - scale);
	ele.hit = sd->battle_status.hit + status_get_job_lv(&sd->bl);
	ele.max_hp = ((10 * (sd->battle_status.int_ + 2 * status_get_job_lv(&sd->bl))) * (scale + 2) / 3) + sd->battle_status.max_hp / 3;
	ele.max_sp = sd->battle_status.max_sp / 4;

	switch( kind ) {
		case EL_KIND_AGNI:
			ele.atk += scale * 20;
			ele.hit += scale * 10;
			break;
		case EL_KIND_AQUA:
			ele.matk += scale * 20;
			ele.mdef += scale * 10;
			break;
		case EL_KIND_VENTUS:
			ele.flee += scale * 20;
			ele.matk += scale * 10;
			break;
		case EL_KIND_TERA:
			ele.def += scale * 25;
			ele.atk += scale * 5;
			break;
	}

	if( (skill_level = pc_checkskill(sd, SO_EL_SYMPATHY)) > 0 ) {
		ele.max_hp += ele.max_hp * 5 * skill_level / 100;
		ele.max_sp += ele.max_sp * 5 * skill_level / 100;
		ele.atk += 25 * skill_level;
		ele.matk += 25 * skill_level;
	}

	ele.hp = ele.max_hp;
	ele.sp = ele.max_sp;
	ele.life_time = lifetime;

	intif_elemental_create(&ele); //Request Char Server to create this elemental

	return 1;
}

int elemental_get_lifetime(struct elemental_data *ed) {
	const struct TimerData *td;

	if( !ed || ed->summon_timer == INVALID_TIMER )
		return 0;

	td = get_timer(ed->summon_timer);

	return (td ? DIFF_TICK(td->tick, gettick()) : 0);
}

int elemental_save(struct elemental_data *ed) {
	ed->elemental.hp = ed->battle_status.hp;
	ed->elemental.sp = ed->battle_status.sp;
	ed->elemental.max_hp = ed->battle_status.max_hp;
	ed->elemental.max_sp = ed->battle_status.max_sp;
	ed->elemental.atk = ed->battle_status.rhw.atk;
	ed->elemental.matk = ed->battle_status.matk_min;
	ed->elemental.def = ed->battle_status.def;
	ed->elemental.mdef = ed->battle_status.mdef;
	ed->elemental.flee = ed->battle_status.flee;
	ed->elemental.hit = ed->battle_status.hit;
	ed->elemental.life_time = elemental_get_lifetime(ed);
	intif_elemental_save(&ed->elemental);
	intif_elemental_sc_save(ed);

	return 1;
}

static TIMER_FUNC(elemental_summon_end) {
	struct map_session_data *sd;
	struct elemental_data *ed;

	if( !(sd = map_id2sd(id)) )
		return 1;

	if( !(ed = sd->ed) )
		return 1;

	if( ed->summon_timer != tid ) {
		ShowError("elemental_summon_end %d != %d.\n", ed->summon_timer, tid);
		return 0;
	}

	ed->summon_timer = INVALID_TIMER;
	elemental_delete(ed, 0); //Elemental's summon time is over

	return 0;
}

void elemental_summon_stop(struct elemental_data *ed) {
	nullpo_retv(ed);

	if( ed->summon_timer != INVALID_TIMER )
		delete_timer(ed->summon_timer, elemental_summon_end);
	ed->summon_timer = INVALID_TIMER;
}

int elemental_delete(struct elemental_data *ed, int reply) {
	struct map_session_data *sd;

	nullpo_ret(ed);

	sd = ed->master;
	ed->elemental.life_time = 0;

	elemental_clean_effect(ed);
	elemental_summon_stop(ed);

	if( !sd )
		return unit_free(&ed->bl, 0);

	sd->ed = NULL;
	sd->status.ele_id = 0;

	if( !ed->bl.prev )
		return unit_free(&ed->bl, 0);

	return unit_remove_map(&ed->bl, 0);
}

void elemental_summon_init(struct elemental_data *ed) {
	if( ed->summon_timer == INVALID_TIMER )
		ed->summon_timer = add_timer(gettick() + ed->elemental.life_time, elemental_summon_end, ed->master->bl.id, 0);
	ed->regen.state.block = 0;
}

/**
 * Inter-serv has sent us the elemental data from sql, fill it in map-serv memory
 * @param ele : The elemental data received from char-serv
 * @param flag : 0:not created, 1:was saved/loaded
 * @return 0:failed, 1:sucess
 */
int elemental_data_received(struct s_elemental *ele, bool flag) {
	struct map_session_data *sd;
	struct elemental_data *ed;
	int i;

	nullpo_ret(ele);

	if( !(sd = map_charid2sd(ele->char_id)) )
		return 0;

	i = elemental_search_index(ele->kind, ele->scale, 0);

	if( !flag || i < 0 ) { //Not created - loaded - DB info
		sd->status.ele_id = 0;
		return 0;
	}

	if( !sd->ed ) {	//Initialize it after first summon
		int j;

		CREATE(ed, struct elemental_data, 1);
		ed->bl.type = BL_ELEM;
		ed->bl.id = npc_get_new_npc_id();
		sd->ed = ed;
		ed->master = sd;
		ed->db = elemental_db[i].db;
		for( j = EL_MODE_PASSIVE; j < EL_MODE_WAIT; j++ )
			memcpy(&ed->skill[j], elemental_db[i].skill[j], sizeof(elemental_db[i].skill[j]));
		memcpy(&ed->elemental, ele, sizeof(struct s_elemental));
		memcpy(&ed->base_status, &elemental_db[i].db->status, sizeof(struct status_data));
		status_set_viewdata(&ed->bl, ed->db->vd.class_);
		ed->vd->head_mid = 10; //Why?
		status_change_init(&ed->bl);
		unit_dataset(&ed->bl);
		ed->ud.dir = sd->ud.dir;

		ed->bl.m = sd->bl.m;
		ed->bl.x = sd->bl.x;
		ed->bl.y = sd->bl.y;
		unit_calc_pos(&ed->bl, sd->bl.x, sd->bl.y, sd->ud.dir);
		ed->bl.x = ed->ud.to_x;
		ed->bl.y = ed->ud.to_y;

		map_addiddb(&ed->bl);
		status_calc_elemental(ed,SCO_FIRST);
		ed->last_spdrain_time = ed->last_thinktime = gettick();
		ed->summon_timer = INVALID_TIMER;
		ed->masterteleport_timer = INVALID_TIMER;
		elemental_summon_init(ed);
		sc_start(&ed->bl, &ed->bl, SC_EL_WAIT, 100, 1, -1);
	} else {
		memcpy(&sd->ed->elemental, ele, sizeof(struct s_elemental));
		ed = sd->ed;
	}

	sd->status.ele_id = ele->elemental_id;

	if( !ed->bl.prev && sd->bl.prev ) {
		if( map_addblock(&ed->bl) )
			return 0;
		clif_spawn(&ed->bl);
		clif_elemental_info(sd);
		clif_elemental_updatestatus(sd, SP_HP);
		clif_hpmeter_single(sd->fd, ed->bl.id, ed->battle_status.hp, ed->battle_status.max_hp);
		clif_elemental_updatestatus(sd, SP_SP);
	}

	intif_elemental_sc_load(sd->status.char_id, ele->elemental_id);

	return 1;
}

int elemental_clean_single_effect(struct elemental_data *ed, uint16 skill_id) {
	struct block_list *bl;
	enum sc_type type = status_skill2sc(skill_id);

	nullpo_ret(ed);

	bl = battle_get_master(&ed->bl);

	if( type ) {
		switch( type ) {
			//Just remove status change
			case SC_PYROTECHNIC_OPTION:
			case SC_HEATER_OPTION:
			case SC_TROPIC_OPTION:
			case SC_FIRE_CLOAK_OPTION:
			case SC_AQUAPLAY_OPTION:
			case SC_WATER_SCREEN_OPTION:
			case SC_COOLER_OPTION:
			case SC_CHILLY_AIR_OPTION:
			case SC_GUST_OPTION:
			case SC_WIND_STEP_OPTION:
			case SC_BLAST_OPTION:
			case SC_WATER_DROP_OPTION:
			case SC_WIND_CURTAIN_OPTION:
			case SC_WILD_STORM_OPTION:
			case SC_PETROLOGY_OPTION:
			case SC_SOLID_SKIN_OPTION:
			case SC_CURSED_SOIL_OPTION:
			case SC_STONE_SHIELD_OPTION:
			case SC_UPHEAVAL_OPTION:
			case SC_CIRCLE_OF_FIRE_OPTION:
			case SC_TIDAL_WEAPON_OPTION:
				if( bl )
					status_change_end(bl,type,INVALID_TIMER); //Master
				status_change_end(&ed->bl,type - 1,INVALID_TIMER); //Elemental Spirit
				break;
			case SC_ZEPHYR:
				if( bl )
					status_change_end(bl,type,INVALID_TIMER);
				break;
			default:
				ShowWarning("Invalid SC=%d in elemental_clean_single_effect\n",type);
				break;
		}
	}

	return 1;
}

int elemental_clean_effect(struct elemental_data *ed) {
	struct map_session_data *sd;

	nullpo_ret(ed);

	//Elemental side
	status_change_end(&ed->bl, SC_TROPIC, INVALID_TIMER);
	status_change_end(&ed->bl, SC_HEATER, INVALID_TIMER);
	status_change_end(&ed->bl, SC_AQUAPLAY, INVALID_TIMER);
	status_change_end(&ed->bl, SC_COOLER, INVALID_TIMER);
	status_change_end(&ed->bl, SC_CHILLY_AIR, INVALID_TIMER);
	status_change_end(&ed->bl, SC_PYROTECHNIC, INVALID_TIMER);
	status_change_end(&ed->bl, SC_FIRE_CLOAK, INVALID_TIMER);
	status_change_end(&ed->bl, SC_WATER_DROP, INVALID_TIMER);
	status_change_end(&ed->bl, SC_WATER_SCREEN, INVALID_TIMER);
	status_change_end(&ed->bl, SC_GUST, INVALID_TIMER);
	status_change_end(&ed->bl, SC_WIND_STEP, INVALID_TIMER);
	status_change_end(&ed->bl, SC_BLAST, INVALID_TIMER);
	status_change_end(&ed->bl, SC_WIND_CURTAIN, INVALID_TIMER);
	status_change_end(&ed->bl, SC_WILD_STORM, INVALID_TIMER);
	status_change_end(&ed->bl, SC_PETROLOGY, INVALID_TIMER);
	status_change_end(&ed->bl, SC_SOLID_SKIN, INVALID_TIMER);
	status_change_end(&ed->bl, SC_CURSED_SOIL, INVALID_TIMER);
	status_change_end(&ed->bl, SC_STONE_SHIELD, INVALID_TIMER);
	status_change_end(&ed->bl, SC_UPHEAVAL, INVALID_TIMER);
	status_change_end(&ed->bl, SC_CIRCLE_OF_FIRE, INVALID_TIMER);
	status_change_end(&ed->bl, SC_TIDAL_WEAPON, INVALID_TIMER);

	if( !(sd = ed->master) )
		return 0;

	//Master side
	status_change_end(&sd->bl, SC_TROPIC_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_HEATER_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_AQUAPLAY_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_COOLER_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_CHILLY_AIR_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_PYROTECHNIC_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_FIRE_CLOAK_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_WATER_DROP_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_WATER_SCREEN_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_GUST_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_WIND_STEP_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_BLAST_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_WIND_CURTAIN_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_WILD_STORM_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_ZEPHYR, INVALID_TIMER);
	status_change_end(&sd->bl, SC_PETROLOGY_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_SOLID_SKIN_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_CURSED_SOIL_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_STONE_SHIELD_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_UPHEAVAL_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_CIRCLE_OF_FIRE_OPTION, INVALID_TIMER);
	status_change_end(&sd->bl, SC_TIDAL_WEAPON_OPTION, INVALID_TIMER);

	return 1;
}

int elemental_action(struct elemental_data *ed, struct block_list *bl, unsigned int tick) {
	struct skill_condition req;
	uint16 skill_id, skill_lv;
	int i;

	nullpo_ret(ed);
	nullpo_ret(bl);

	if( !ed->master )
		return 0;

	if( ed->target_id )
		elemental_unlocktarget(ed);	//Remove previous target

	ARR_FIND(0, MAX_EL_SKILL, i, ed->skill[EL_MODE_OFFENSIVE][i].id == 0);

	if( (i--) > 0 )
		i = rnd_value(0, i);

	skill_id = ed->skill[EL_MODE_OFFENSIVE][i].id;
	skill_lv = ed->skill[EL_MODE_OFFENSIVE][i].lv;

	if( elemental_skillnotok(skill_id, ed) )
		return 0;

	if( ed->ud.skilltimer != INVALID_TIMER )
		return 0;
	else if( DIFF_TICK(tick, ed->ud.canact_tick) < 0 )
		return 0;

	ed->target_id = ed->ud.skilltarget = bl->id; //Set new target
	ed->last_thinktime = tick;

	if( !battle_check_range(&ed->bl, bl, skill_get_range(skill_id, skill_lv)) ) { //Not in skill range
		if( !unit_walktobl(&ed->bl, bl, skill_get_range(skill_id, skill_lv), 2) )
			elemental_unlocktarget(ed); //Try to walk to the target
		else { //Walking, waiting to be in range. Client don't handle it, then we must handle it here
			int walk_dist = distance_bl(&ed->bl, bl) - skill_get_range(skill_id, skill_lv);

			ed->ud.skill_id = skill_id;
			ed->ud.skill_lv = skill_lv;

			if( skill_get_inf(skill_id)&INF_GROUND_SKILL )
				ed->ud.skilltimer = add_timer(tick + status_get_speed(&ed->bl) * walk_dist, skill_castend_pos, ed->bl.id, 0);
			else
				ed->ud.skilltimer = add_timer(tick + status_get_speed(&ed->bl) * walk_dist, skill_castend_id, ed->bl.id, 0);
		}
		return 0;
	}

	req = elemental_skill_get_requirements(skill_id, skill_lv);

	if( req.hp || req.sp ) {
		struct map_session_data *sd = BL_CAST(BL_PC, battle_get_master(&ed->bl));

		if( sd ) {
			if( sd->skill_id_old != SO_EL_ACTION && //Regardless of remaining HP/SP it can be cast
				(status_get_hp(&ed->bl) < req.hp || status_get_sp(&ed->bl) < req.sp) )
				return 0;
			else
				status_zap(&ed->bl, req.hp, req.sp);
		}
	}

	//Otherwise, just cast the skill
	if( skill_get_inf(skill_id)&INF_GROUND_SKILL )
		unit_skilluse_pos(&ed->bl, bl->x, bl->y, skill_id, skill_lv);
	else
		unit_skilluse_id(&ed->bl, bl->id, skill_id, skill_lv);

	//Reset target
	ed->target_id = 0;

	return 1;
}

/*===============================================================
 * Action that elemental perform after changing mode.
 * Activates one of the skills of the new mode.
 *-------------------------------------------------------------*/
int elemental_change_mode_ack(struct elemental_data *ed, enum elemental_mode mode) {
	struct block_list *bl = &ed->master->bl;
	uint16 skill_id, skill_lv;
	int i;

	nullpo_ret(ed);

	if( !bl || mode == EL_MODE_WAIT || mode == EL_MODE_OFFENSIVE )
		return 0;

	ARR_FIND(1, MAX_EL_SKILL, i, ed->skill[mode][i].id == 0);

	if( (i--) > 0 )
		i = rnd_value(0, i);

	skill_id = ed->skill[mode][i].id;
	skill_lv = ed->skill[mode][i].lv;

	if( elemental_skillnotok(skill_id, ed) )
		return 0;

	if( ed->ud.skilltimer != INVALID_TIMER )
		return 0;
	else if( DIFF_TICK(gettick(), ed->ud.canact_tick) < 0 )
		return 0;

	ed->target_id = bl->id;	//Set new target
	ed->last_thinktime = gettick();

	if( skill_get_inf(skill_id)&INF_GROUND_SKILL )
		unit_skilluse_pos(&ed->bl, ed->bl.x, ed->bl.y, skill_id, skill_lv);
	else
		unit_skilluse_id(&ed->bl, ed->bl.id, skill_id, skill_lv);

	ed->target_id = 0; //Reset target after casting the skill to avoid continious attack

	return 1;
}

void elemental_heal(struct elemental_data *ed, int hp, int sp) {
	nullpo_retv(ed);

	if( !ed->master )
		return;
	if( hp )
		clif_elemental_updatestatus(ed->master, SP_HP);
	if( sp )
		clif_elemental_updatestatus(ed->master, SP_SP);
}

int elemental_dead(struct elemental_data *ed) {
	elemental_delete(ed, 1);
	return 0;
}

int elemental_unlocktarget(struct elemental_data *ed) {
	nullpo_ret(ed);

	ed->target_id = 0;
	elemental_stop_attack(ed);
	elemental_stop_walking(ed,USW_FIXPOS);
	return 0;
}

bool elemental_skillnotok(uint16 skill_id, struct elemental_data *ed) {
	uint16 idx = skill_get_index(skill_id);

	nullpo_retr(1, ed);

	if( !idx )
		return false; //Invalid skill id

	return skill_isNotOk(skill_id, ed->master);
}

struct skill_condition elemental_skill_get_requirements(uint16 skill_id, uint16 skill_lv) {
	struct skill_condition req;
	uint16 idx = skill_get_index(skill_id);

	memset(&req, 0, sizeof(req));

	if( !idx ) //Invalid skill id
		return req;

	if( skill_lv < 1 || skill_lv > MAX_SKILL_LEVEL )
		return req;

	req.hp = skill_db[idx].require.hp[skill_lv - 1];
	req.sp = skill_db[idx].require.sp[skill_lv - 1];

	return req;
}

int elemental_set_target(struct map_session_data *sd, struct block_list *bl) {
	struct elemental_data *ed;

	nullpo_ret(sd);

	ed = sd->ed;

	nullpo_ret(ed);
	nullpo_ret(bl);

	if( ed->bl.m != bl->m || !check_distance_bl(&ed->bl, bl, ed->db->range2) )
		return 0;

	if( !status_check_skilluse(&ed->bl, bl, 0, 0) )
		return 0;

	if( !ed->target_id )
		ed->target_id = bl->id;

	return 1;
}

static int elemental_ai_sub_timer_activesearch(struct block_list *bl, va_list ap) {
	struct elemental_data *ed;
	struct block_list **target;
	int dist;

	nullpo_ret(bl);

	ed = va_arg(ap,struct elemental_data *);

	nullpo_ret(ed);

	target = va_arg(ap,struct block_list **);

	nullpo_ret(target);

	//If can't seek yet, not an enemy, or you can't attack it, skip
	if( (*target) == bl || !status_check_skilluse(&ed->bl, bl, 0, 0) )
		return 0;

	if( battle_check_target(&ed->bl,bl,BCT_ENEMY) <= 0 )
		return 0;

	switch( bl->type ) {
		case BL_PC:
			if( !map_flag_vs(ed->bl.m) )
				return 0;
		default:
			dist = distance_bl(&ed->bl, bl);
			if( (!(*target) || !check_distance_bl(&ed->bl, *target, dist)) && battle_check_range(&ed->bl, bl, ed->db->range2) ) { //Pick closest target?
				(*target) = bl;
				ed->target_id = bl->id;
				ed->min_chase = dist + ed->db->range3;
				if( ed->min_chase > AREA_SIZE )
					ed->min_chase = AREA_SIZE;
				return 1;
			}
			break;
	}
	return 0;
}

static int elemental_ai_sub_timer(struct elemental_data *ed, struct map_session_data *sd, unsigned int tick) {
	struct block_list *target = NULL;
	int master_dist, view_range;

	nullpo_ret(ed);
	nullpo_ret(sd);

	if( !ed->bl.prev || !sd || !sd->bl.prev )
		return 0;

	if( DIFF_TICK(tick, ed->last_thinktime) < MIN_ELETHINKTIME )
		return 0;

	ed->last_thinktime = tick;

	if( ed->ud.skilltimer != INVALID_TIMER )
		return 0;

	if( ed->ud.walktimer != INVALID_TIMER && ed->ud.walkpath.path_pos <= 2 )
		return 0; //No thinking when you just started to walk

	if( ed->ud.walkpath.path_pos < ed->ud.walkpath.path_len && ed->ud.target == sd->bl.id )
		return 0; //No thinking until be near the master

	if( ed->sc.count && ed->sc.data[SC_BLIND] && ed->sc.data[SC_PYREXIA] )
		view_range = 3;
	else
		view_range = ed->db->range2;

	master_dist = distance_bl(&sd->bl, &ed->bl);
	if( master_dist > AREA_SIZE ) {	//Master out of vision range
		elemental_unlocktarget(ed);
		unit_warp(&ed->bl, sd->bl.m, sd->bl.x, sd->bl.y, CLR_TELEPORT);
		clif_elemental_updatestatus(sd, SP_HP);
		clif_elemental_updatestatus(sd, SP_SP);
		return 0;
	} else if( master_dist > MAX_ELEDISTANCE ) { //Master too far, chase
		short x = sd->bl.x, y = sd->bl.y;

		if( ed->target_id )
			elemental_unlocktarget(ed);
		if( ed->ud.walktimer != INVALID_TIMER && ed->ud.target == sd->bl.id )
			return 0; //Already walking to him
		if( DIFF_TICK(tick, ed->ud.canmove_tick) < 0 )
			return 0; //Can't move yet
		if( map_search_freecell(&ed->bl, sd->bl.m, &x, &y, MIN_ELEDISTANCE, MIN_ELEDISTANCE, 1) && unit_walktoxy(&ed->bl, x, y, 0) )
			return 0;
	}

	if( ed->sc.data[SC_EL_OFFENSIVE] ) {
		target = map_id2bl(ed->ud.target);

		if( !target )
			map_foreachinallrange(elemental_ai_sub_timer_activesearch, &ed->bl, view_range, BL_CHAR, ed, &target, status_get_mode(&ed->bl));

		if( !target ) { //No targets available
			elemental_unlocktarget(ed);
			return 1;
		}

		//Attempt to attack
		//At this point we know the target is attackable, we just gotta check if the range matches
		if( ed->ud.target == target->id && ed->ud.attacktimer != INVALID_TIMER ) //Already locked
			return 1;

		if( battle_check_range(&ed->bl, target, ed->base_status.rhw.range) ) { //Target within range, engage
			unit_attack(&ed->bl, target->id, 1);
			return 1;
		}

		//Follow up if possible
		if( !unit_walktobl(&ed->bl, target, ed->base_status.rhw.range, 2) )
			elemental_unlocktarget(ed);
	}

	return 0;
}

static int elemental_ai_sub_foreachclient(struct map_session_data *sd, va_list ap) {
	unsigned int tick = va_arg(ap,unsigned int);

	if(sd->status.ele_id && sd->ed)
		elemental_ai_sub_timer(sd->ed,sd,tick);

	return 0;
}

static TIMER_FUNC(elemental_ai_timer) {
	map_foreachpc(elemental_ai_sub_foreachclient,tick);
	return 0;
}

void read_elementaldb(void) {
	struct config_setting_t *eledb = NULL;
	const char *filepath = "db/elemental_db.conf";
	int i, count = 0;

	if( config_read_file(&ele_db_conf, filepath) )
		return;

	eledb = config_lookup(&ele_db_conf, "elemental_db");

	if( eledb ) {
		struct config_setting_t *kinds = config_setting_get_elem(eledb, 0);

		for( i = EL_KIND_AGNI; i < MAX_EL_KIND; i++ ) {
			int j = 0, len = 0;
			struct elemental_data db;
			const char *modes[] = { "Passive", "Defensive", "Offensive" };
			struct config_setting_t *mobs = config_setting_get_elem(kinds, i);

			memset(&db, 0, sizeof(db));

			if( !mobs )
				continue;
			if( count == MAX_ELEMENTAL_CLASS ) {
				ShowWarning("read_elementaldb: MAX_ELEMENTAL_CLASS(%d) has been reached, skipping ...\n", MAX_ELEMENTAL_CLASS);
				continue;
			}
			len = config_setting_length(mobs);
			for( j = 0; j < len; j++ ) {
				struct config_setting_t *temp = NULL;
				struct config_setting_t *skills = NULL;
				struct config_setting_t *monster = config_setting_get_elem(mobs, j);
				const char *sprite_name;
				int i32;

				sprite_name = config_setting_name(monster);

				if( !(i32 = mobdb_searchname(sprite_name)) ) {
					ShowWarning("read_elementaldb: Invalid mob name specified '%s'!\n", sprite_name);
					continue;
				}

				db.db = mob_db(i32);
				db.elemental.kind = (enum elemental_type)i;

				if( config_setting_lookup_int(monster, "Scale", &i32) )
					db.elemental.scale = i32;
				else
					db.elemental.scale = 1;

				for( i32 = 0; i32 < ARRAYLENGTH(modes); i32++ ) {
					if( (temp = config_setting_get_member(monster, modes[i32])) ) {
						int idx = 0, skill_id;

						while( (skills = config_setting_get_elem(temp, idx++)) ) {
							const char *skill_name = config_setting_name(skills);
							int skill_lv = config_setting_get_int(skills);

							if( idx > MAX_EL_SKILL ) {
								ShowWarning("read_elementaldb: MAX_EL_SKILL(%d) has been reached, skipping ...\n", MAX_EL_SKILL);
								break;
							}
							if( (skill_id = skill_name2id(skill_name)) ) {
								if( !skill_get_inf(skill_id) ) {
									ShowWarning("read_elementaldb: Passive skill '%s' is not supported\n", skill_name);
									continue;
								}
								db.skill[i32][idx - 1].id = skill_id;
								db.skill[i32][idx - 1].lv = max(skill_lv, 1);
							} else
								ShowWarning("read_elementaldb: Unknown skill '%s' of '%s'(%s)\n", skill_name, sprite_name, modes[i32]);
						}
					}
				}
				memcpy(&elemental_db[count++], &db, sizeof(struct elemental_data));
			}
		}
		ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' elementals in '"CL_WHITE"%s"CL_RESET"'.\n", count, filepath);
		config_destroy(&ele_db_conf);
	}
}

void reload_elementaldb(void) {
	read_elementaldb();
}

void do_init_elemental(void) {
	read_elementaldb();

	add_timer_func_list(elemental_ai_timer, "elemental_ai_timer");
	add_timer_interval(gettick() + MIN_ELETHINKTIME, elemental_ai_timer, 0, 0, MIN_ELETHINKTIME);
}

void do_final_elemental(void) {
	return;
}
