// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/db.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/ers.h"
#include "../common/conf.h"

#include "pc.h"
#include "status.h"
#include "map.h"
#include "path.h"
#include "intif.h"
#include "clif.h"
#include "chrif.h"
#include "pet.h"
#include "itemdb.h"
#include "battle.h"
#include "mob.h"
#include "npc.h"
#include "script.h"
#include "skill.h"
#include "unit.h"
#include "atcommand.h" // msg_txt()
#include "log.h"
#include "achievement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define MIN_PETTHINKTIME 100

struct config_t pet_db_conf;
struct s_pet_db pet_db[MAX_PET_DB];

static struct eri *item_drop_ers; //For loot drops delay structures.
static struct eri *item_drop_list_ers;

int pet_hungry_val(struct pet_data *pd)
{
	nullpo_ret(pd);

	if (pd->pet.hungry > 90)
		return 4;
	else if (pd->pet.hungry > 75)
		return 3;
	else if (pd->pet.hungry > 25)
		return 2;
	else if (pd->pet.hungry > 10)
		return 1;
	else
		return 0;
}

void pet_set_intimate(struct pet_data *pd, int value)
{
	int intimate;
	struct map_session_data *sd;

	nullpo_retv(pd);

	intimate = pd->pet.intimate;
	sd = pd->master;
	pd->pet.intimate = value;
	if (sd) {
#ifdef RENEWAL
		int bonus = battle_config.pet_bonus_min_friendly_renewal;
#else
		int bonus = battle_config.pet_bonus_min_friendly;
#endif

		if ((intimate < bonus && pd->pet.intimate >= bonus) || (intimate >= bonus && pd->pet.intimate > bonus) ||
			(intimate >= bonus && pd->pet.intimate < bonus))
			status_calc_pc(sd,SCO_NONE);
	}

	if (value <= 0) { //Pet is lost, delete the egg
		int i;

		ARR_FIND(0,MAX_INVENTORY,i,(sd->inventory.u.items_inventory[i].card[0] == CARD0_PET &&
			pd->pet.pet_id == MakeDWord(sd->inventory.u.items_inventory[i].card[1],sd->inventory.u.items_inventory[i].card[2])));
		if (i != MAX_INVENTORY)
			pc_delitem(sd,i,1,0,0,LOG_TYPE_OTHER);
	}
}

int pet_create_egg(struct map_session_data *sd, unsigned short item_id)
{
	int pet_id = search_petDB_index(item_id,PET_EGG);

	if (pet_id < 0)
		return 0; //No pet egg here
	if (!pc_inventoryblank(sd))
		return 0; //Inventory full
	sd->catch_target_class = pet_db[pet_id].class_;
	intif_create_pet(sd->status.account_id,sd->status.char_id,pet_db[pet_id].class_,mob_db(pet_db[pet_id].class_)->lv,pet_db[pet_id].EggID,0,pet_db[pet_id].intimate,100,0,1,pet_db[pet_id].jname);
	return 1;
}

int pet_unlocktarget(struct pet_data *pd)
{
	nullpo_ret(pd);

	pd->target_id = 0;
	pet_stop_attack(pd);
	pet_stop_walking(pd,1);
	return 0;
}

/*==========================================
 * Pet Attack Skill [Skotlex]
 *------------------------------------------*/
int pet_attackskill(struct pet_data *pd, int target_id)
{
	if (!battle_config.pet_status_support || !pd->a_skill ||
		(battle_config.pet_equip_required && !pd->pet.equip))
		return 0;

	if (DIFF_TICK(pd->ud.canact_tick, gettick()) > 0)
		return 0;

	if (rnd()%100 < (pd->a_skill->rate + pd->pet.intimate * pd->a_skill->bonusrate / 1000)) { //Skotlex: Use pet's skill
		int inf;
		struct block_list *bl;

		bl = map_id2bl(target_id);
		if (bl == NULL || pd->bl.m != bl->m || bl->prev == NULL || status_isdead(bl) ||
			!check_distance_bl(&pd->bl,bl,pd->db->range3))
			return 0;

		inf = skill_get_inf(pd->a_skill->id);
		if (inf&INF_GROUND_SKILL)
			unit_skilluse_pos(&pd->bl,bl->x,bl->y,pd->a_skill->id,pd->a_skill->lv);
		else //Offensive self skill? Could be stuff like GX
			unit_skilluse_id(&pd->bl,(inf&INF_SELF_SKILL ? pd->bl.id : bl->id),pd->a_skill->id,pd->a_skill->lv);
		return 1; //Skill invoked
	}
	return 0;
}

/**
 * Make sure pet can attack from given config values.
 * @param pd: pet data
 * @param bl: target
 * @param type: pet's attack rate type
 * @return 0
 */
int pet_target_check(struct pet_data *pd, struct block_list *bl, int type)
{
	int rate;

	nullpo_ret(pd);

	Assert(!pd->master || pd->master->pd == pd);

	if(!bl || bl->type != BL_MOB || !bl->prev ||
		pd->pet.intimate < battle_config.pet_support_min_friendly ||
		pd->pet.hungry <= 0 ||
		pd->pet.class_ == status_get_class(bl))
		return 0;

	if(pd->bl.m != bl->m || !check_distance_bl(&pd->bl, bl, pd->db->range2))
		return 0;

	if(!battle_config.pet_master_dead && pc_isdead(pd->master)) {
		pet_unlocktarget(pd);
		return 0;
	}

	if(!status_check_skilluse(&pd->bl, bl, 0, 0))
		return 0;

	if(!type) {
		rate = pd->petDB->attack_rate;
		rate = rate * pd->rate_fix / 1000;
		if(pd->petDB->attack_rate > 0 && rate <= 0)
			rate = 1;
	} else {
		rate = pd->petDB->defence_attack_rate;
		rate = rate * pd->rate_fix / 1000;
		if(pd->petDB->defence_attack_rate > 0 && rate <= 0)
			rate = 1;
	}

	if(rnd()%10000 < rate && (pd->target_id == 0 || rnd()%10000 < pd->petDB->change_target_rate))
		pd->target_id = bl->id;

	return 0;
}

static int pet_unequipitem(struct map_session_data *sd, struct pet_data *pd);
static int pet_food(struct map_session_data *sd, struct pet_data *pd);
static int pet_ai_sub_hard_lootsearch(struct block_list *bl,va_list ap);

/*==========================================
 * Pet SC Check [Skotlex]
 *------------------------------------------*/
int pet_sc_check(struct map_session_data *sd, int type)
{
	struct pet_data *pd;

	nullpo_ret(sd);
	pd = sd->pd;

	if(pd == NULL || (battle_config.pet_equip_required && pd->pet.equip == 0) ||
		pd->recovery == NULL ||
		pd->recovery->timer != INVALID_TIMER ||
		pd->recovery->type != type)
		return 1;

	pd->recovery->timer = add_timer(gettick() + pd->recovery->delay * 1000,pet_recovery_timer,sd->bl.id,0);

	return 0;
}

int pet_get_walk_speed(struct map_session_data *sd)
{
	struct pet_data *pd;

	nullpo_ret(sd);
	pd = sd->pd;

	switch(battle_config.pet_walk_speed) {
			default:
			case 1: //Master
				return sd->battle_status.speed;
			case 2: //DEFAULT_WALK_SPEED
				return DEFAULT_WALK_SPEED;
			case 3: //Mob database
				return pd->db->status.speed;
		}
}

static TIMER_FUNC(pet_hungry)
{
	struct map_session_data *sd;
	struct pet_data *pd;
	int interval;

	sd = map_id2sd(id);
	if(!sd)
		return 1;

	if(!sd->status.pet_id || !sd->pd)
		return 1;

	pd = sd->pd;
	if(pd->pet_hungry_timer != tid) {
		ShowError("pet_hungry_timer %d != %d\n",pd->pet_hungry_timer,tid);
		return 0;
	}
	pd->pet_hungry_timer = INVALID_TIMER;

	if(pd->pet.intimate < PETINTIMATE_AWKWARD)
		return 1; //You lost the pet already, the rest is irrelevant

	if(battle_config.pet_hungry_delay_rate != 100)
		interval = pd->petDB->hungry_delay * battle_config.pet_hungry_delay_rate / 100;
	else
		interval = pd->petDB->hungry_delay;

	pd->pet.hungry -= pd->petDB->hunger_pts;

	if(battle_config.feature_pet_autofeed && pd->pet.autofeed && pd->pet.hungry <= 25)
		pet_food(sd, pd);

	if(pd->pet.hungry <= 0) {
		int val = (pd->pet.class_ == MOBID_LITTLE_PORING ? 1 : battle_config.pet_hungry_friendly_decrease);

		pd->pet.hungry = 0;
		pet_set_intimate(pd,pd->pet.intimate - val);
		if(pd->pet.intimate < PETINTIMATE_AWKWARD) {
			pd->pet.intimate = 0;
			pet_stop_attack(pd);
			pd->status.speed = pet_get_walk_speed(pd->master);
		}
		status_calc_pet(pd,SCO_NONE);
		clif_send_petdata(sd,pd,1,pd->pet.intimate);
		interval = 20000;
	}
	clif_send_petdata(sd,pd,2,pd->pet.hungry);

	interval = max(interval,1);
	pd->pet_hungry_timer = add_timer(tick + interval,pet_hungry,sd->bl.id,0);

	return 0;
}

int search_petDB_index(int key, int type)
{
	int i;

	for(i = 0; i < MAX_PET_DB; i++) {
		if(pet_db[i].class_ <= 0)
			continue;
		switch(type) {
			case PET_CLASS: if(pet_db[i].class_ == key) return i; break;
			case PET_CATCH: if(pet_db[i].itemID == key) return i; break;
			case PET_EGG:   if(pet_db[i].EggID  == key) return i; break;
			case PET_EQUIP: if(pet_db[i].AcceID == key) return i; break;
			case PET_FOOD:  if(pet_db[i].FoodID == key) return i; break;
			default:
				return -1;
		}
	}
	return -1;
}

int pet_hungry_timer_delete(struct pet_data *pd)
{
	nullpo_ret(pd);
	if(pd->pet_hungry_timer != INVALID_TIMER) {
		delete_timer(pd->pet_hungry_timer,pet_hungry);
		pd->pet_hungry_timer = INVALID_TIMER;
	}

	return 1;
}

static int pet_performance(struct map_session_data *sd, struct pet_data *pd)
{
	int val;

	if (pd->pet.intimate >= PETINTIMATE_LOYAL)
		val = (pd->petDB->s_perfor > 0) ? 4 : 3;
	else if(pd->pet.intimate >= PETINTIMATE_CORDIAL) //@TODO: This is way too high
		val = 2;
	else
		val = 1;

	pet_stop_walking(pd,2000<<8);
	clif_pet_performance(pd, rnd()%val + 1);
	pet_lootitem_drop(pd,NULL);
	return 1;
}

int pet_return_egg(struct map_session_data *sd, struct pet_data *pd)
{
	int i;

	pet_lootitem_drop(pd,sd);

	ARR_FIND(0,MAX_INVENTORY,i,(sd->inventory.u.items_inventory[i].card[0] == CARD0_PET &&
		pd->pet.pet_id == MakeDWord(sd->inventory.u.items_inventory[i].card[1],sd->inventory.u.items_inventory[i].card[2])));
	if (i != MAX_INVENTORY) {
		sd->inventory.u.items_inventory[i].attribute = 0;
		sd->inventory.u.items_inventory[i].bound = BOUND_NONE;
	} else { //The pet egg wasn't found: it was probably hatched with the old system that deleted the egg
		struct item tmp_item;
		int flag;

		memset(&tmp_item,0,sizeof(tmp_item));
		tmp_item.nameid = pd->petDB->EggID;
		tmp_item.identify = 1;
		tmp_item.card[0] = CARD0_PET;
		tmp_item.card[1] = GetWord(pd->pet.pet_id,0);
		tmp_item.card[2] = GetWord(pd->pet.pet_id,1);
		tmp_item.card[3] = pd->pet.rename_flag;
		if ((flag = pc_additem(sd,&tmp_item,1,LOG_TYPE_OTHER))) {
			clif_additem(sd,0,0,flag);
			map_addflooritem(&tmp_item,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0,0,false);
		}
	}

#if PACKETVER >= 20180704
	clif_inventorylist(sd);
	clif_send_petdata(sd,pd,6,0);
#endif
	pd->pet.incubate = 1;
	unit_free(&pd->bl,CLR_OUTSIGHT);
	status_calc_pc(sd,SCO_NONE);
	sd->status.pet_id = 0;
	return 1;
}

int pet_data_init(struct map_session_data *sd, struct s_pet *pet)
{
	struct pet_data *pd;
	int i = 0, interval = 0;

	nullpo_retr(1,sd);

	Assert(!sd->status.pet_id || !sd->pd || sd->pd->master == sd);

	if(sd->status.account_id != pet->account_id || sd->status.char_id != pet->char_id) {
		sd->status.pet_id = 0;
		return 1;
	}
	if(sd->status.pet_id != pet->pet_id) {
		if(sd->status.pet_id) { //Wrong pet?? Set incubate to no and send it back for saving
			pet->incubate = 1;
			intif_save_petdata(sd->status.account_id,pet);
			sd->status.pet_id = 0;
			return 1;
		}
		//The pet_id value was lost? Odd, restore it
		sd->status.pet_id = pet->pet_id;
	}

	i = search_petDB_index(pet->class_,PET_CLASS);
	if(i < 0) {
		sd->status.pet_id = 0;
		return 1;
	}
	sd->pd = pd = (struct pet_data *)aCalloc(1,sizeof(struct pet_data));
	pd->bl.type = BL_PET;
	pd->bl.id = npc_get_new_npc_id();

	pd->master = sd;
	pd->petDB = &pet_db[i];
	pd->db = mob_db(pet->class_);
	memcpy(&pd->pet,pet,sizeof(struct s_pet));
	status_set_viewdata(&pd->bl,pet->class_);
	unit_dataset(&pd->bl);
	pd->ud.dir = sd->ud.dir;

	pd->bl.m = sd->bl.m;
	pd->bl.x = sd->bl.x;
	pd->bl.y = sd->bl.y;
	unit_calc_pos(&pd->bl,sd->bl.x,sd->bl.y,sd->ud.dir);
	pd->bl.x = pd->ud.to_x;
	pd->bl.y = pd->ud.to_y;

	map_addiddb(&pd->bl);
	status_calc_pet(pd,SCO_FIRST);

	pd->last_thinktime = gettick();
	pd->state.skillbonus = 0;

	if(battle_config.pet_status_support)
		run_script(pet_db[i].pet_script,0,sd->bl.id,0);

	if(pd->petDB) {
		if(pd->petDB->pet_friendly_script)
			status_calc_pc(sd,SCO_NONE);
		if(battle_config.pet_hungry_delay_rate != 100)
			interval = pd->petDB->hungry_delay * battle_config.pet_hungry_delay_rate / 100;
		else
			interval = pd->petDB->hungry_delay;
		if(pd->pet.hungry <= 0)
			interval = 20000; //Check 20 seconds while pet is starving
	}

	interval = max(interval,1);
	pd->pet_hungry_timer = add_timer(gettick() + interval,pet_hungry,sd->bl.id,0);
	pd->masterteleport_timer = INVALID_TIMER;
	return 0;
}

int pet_birth_process(struct map_session_data *sd, struct s_pet *pet)
{
	nullpo_retr(1, sd);

	Assert(!sd->status.pet_id || !sd->pd || sd->pd->master == sd);

	if(sd->status.pet_id && pet->incubate == 1) {
		sd->status.pet_id = 0;
		return 1;
	}

	pet->incubate = 0;
	pet->account_id = sd->status.account_id;
	pet->char_id = sd->status.char_id;
	sd->status.pet_id = pet->pet_id;
	if(pet_data_init(sd, pet)) {
		sd->status.pet_id = 0;
		return 1;
	}

	intif_save_petdata(sd->status.account_id,pet);
	if(save_settings&CHARSAVE_PET)
		chrif_save(sd, CSAVE_INVENTORY); //Is it REALLY Needed to save the char for hatching a pet? [Skotlex]

	if(sd->bl.prev) {
		if(map_addblock(&sd->pd->bl))
			return 1;
		clif_spawn(&sd->pd->bl);
		clif_send_petdata(sd, sd->pd, 0, 0);
		clif_send_petdata(sd, sd->pd, 5, battle_config.pet_hair_style);
#if PACKETVER >= 20180704
		clif_send_petdata(sd, sd->pd, 6, 1);
#endif
		clif_pet_equip_area(sd->pd);
		clif_send_petstatus(sd);
	}
	Assert(!sd->status.pet_id || !sd->pd || sd->pd->master == sd);

	return 0;
}

int pet_recv_petdata(int account_id,struct s_pet *p, int flag)
{
	struct map_session_data *sd;

	if(!(sd = map_id2sd(account_id)))
		return 1;
	if(flag == 1) {
		sd->status.pet_id = 0;
		return 1;
	}
	if(p->incubate == 1) {
		int i;

		//Get Egg Index
		ARR_FIND(0,MAX_INVENTORY,i,(sd->inventory.u.items_inventory[i].card[0] == CARD0_PET &&
			p->pet_id == MakeDWord(sd->inventory.u.items_inventory[i].card[1],sd->inventory.u.items_inventory[i].card[2])));
		if(i == MAX_INVENTORY) {
			ShowError("pet_recv_petdata: Hatching pet (%d:%s) aborted, couldn't find egg in inventory for removal!\n",p->pet_id,p->name);
			sd->status.pet_id = 0;
			return 1;
		}
		if(!pet_birth_process(sd,p)) {
			sd->inventory.u.items_inventory[i].attribute = 1; //Hide the egg by setting broken attribute [Asheraf]
			sd->inventory.u.items_inventory[i].bound = BOUND_CHAR; //Bind the egg to the character to avoid moving it via forged packets [Asheraf]
		}
	} else {
		pet_data_init(sd,p);
		if(sd->pd && sd->bl.prev) {
			if(map_addblock(&sd->pd->bl))
				return 1;
			clif_spawn(&sd->pd->bl);
			clif_send_petdata(sd,sd->pd,0,0);
			clif_send_petdata(sd,sd->pd,5,battle_config.pet_hair_style);
			clif_pet_equip_area(sd->pd);
			clif_send_petstatus(sd);
		}
	}

	return 0;
}

int pet_select_egg(struct map_session_data *sd, short egg_index)
{
	nullpo_ret(sd);

	if(egg_index < 0 || egg_index >= MAX_INVENTORY)
		return 0; //Forged packet!

	if(sd->inventory.u.items_inventory[egg_index].card[0] == CARD0_PET)
		intif_request_petdata(sd->status.account_id,sd->status.char_id,MakeDWord(sd->inventory.u.items_inventory[egg_index].card[1],sd->inventory.u.items_inventory[egg_index].card[2]));
	else
		ShowError("Wrong egg item inventory %d\n",egg_index);

	return 0;
}

int pet_catch_process1(struct map_session_data *sd, int target_class)
{
	nullpo_ret(sd);

	sd->catch_target_class = target_class;
	clif_catch_process(sd);

	return 0;
}

int pet_catch_process2(struct map_session_data *sd, int target_id)
{
	struct mob_data *md;
	int i = 0, pet_catch_rate = 0;

	nullpo_retr(1, sd);

	md = (struct mob_data *)map_id2bl(target_id);
	if(!md || md->bl.type != BL_MOB || !md->bl.prev) { //Invalid inputs/state, abort capture.
		clif_pet_roulette(sd,0);
		sd->catch_target_class = PET_CATCH_FAIL;
		sd->itemid = sd->itemindex = -1;
		return 1;
	}

	//FIXME: delete taming item here, if this was an item-invoked capture and the item was flagged as delay-consume [ultramage]

	i = search_petDB_index(md->mob_id,PET_CLASS);
	if(sd->catch_target_class == PET_CATCH_UNIVERSAL && !status_has_mode(&md->status,MD_STATUS_IMMUNE))
		sd->catch_target_class = md->mob_id; //catch_target_class == PET_CATCH_UNIVERSAL is used for universal lures (except bosses for now) [Skotlex]
	else if(sd->catch_target_class == PET_CATCH_UNIVERSAL_ITEM && sd->itemid == pet_db[i].itemID)
		sd->catch_target_class = md->mob_id; //catch_target_class == PET_CATCH_UNIVERSAL_ITEM is used for catching any monster required the lure item used
	if(i < 0 || sd->catch_target_class != md->mob_id) {
		clif_emotion(&md->bl,E_AG); //Mob will do /ag if wrong lure is used on them.
		clif_pet_roulette(sd,0);
		sd->catch_target_class = PET_CATCH_FAIL;
		return 1;
	}

	pet_catch_rate = (pet_db[i].capture + (sd->status.base_level - md->level)*30 + sd->battle_status.luk*20)*(200 - get_percentage(md->status.hp,md->status.max_hp))/100;

	if(pet_catch_rate < 1)
		pet_catch_rate = 1;
	if(battle_config.pet_catch_rate != 100)
		pet_catch_rate = (pet_catch_rate*battle_config.pet_catch_rate)/100;

	if(rnd()%10000 < pet_catch_rate) {
		achievement_update_objective(sd,AG_TAMING,1,md->mob_id);
		unit_remove_map(&md->bl,CLR_OUTSIGHT);
		status_kill(&md->bl);
		clif_pet_roulette(sd,1);
		intif_create_pet(sd->status.account_id,sd->status.char_id,pet_db[i].class_,mob_db(pet_db[i].class_)->lv,pet_db[i].EggID,0,pet_db[i].intimate,100,0,1,pet_db[i].jname);
	} else {
		clif_pet_roulette(sd,0);
		sd->catch_target_class = PET_CATCH_FAIL;
	}

	return 0;
}

/**
 * Is invoked _only_ when a new pet has been created is a product of packet 0x3880
 * see mapif_pet_created@int_pet.c for more information
 * Handles new pet data from inter-server and prepares item information
 * to add pet egg
 *
 * pet_id - Should contain pet id otherwise means failure
 * returns true on success
 */
bool pet_get_egg(int account_id, short pet_class, int pet_id)
{
	struct map_session_data *sd;
	struct item tmp_item;
	int i = 0;
	unsigned char ret = 0;

	if(pet_id == 0 || pet_class == 0)
		return false;

	sd = map_id2sd(account_id);
	if(sd == NULL)
		return false;

	//i = search_petDB_index(sd->catch_target_class,PET_CLASS);
	//bugreport:8150
	//Before this change in cases where more than one pet egg were requested in a short
	//period of time it wasn't possible to know which kind of egg was being requested after
	//the first request. [Panikon]
	i = search_petDB_index(pet_class,PET_CLASS);
	sd->catch_target_class = PET_CATCH_FAIL;

	if(i < 0) {
		intif_delete_petdata(pet_id);
		return false;
	}

	memset(&tmp_item,0,sizeof(tmp_item));
	tmp_item.nameid = pet_db[i].EggID;
	tmp_item.identify = 1;
	tmp_item.card[0] = CARD0_PET;
	tmp_item.card[1] = GetWord(pet_id,0);
	tmp_item.card[2] = GetWord(pet_id,1);
	tmp_item.card[3] = 0; //New pets are not named
	if((ret = pc_additem(sd,&tmp_item,1,LOG_TYPE_PICKDROP_PLAYER))) {
		clif_additem(sd,0,0,ret);
		map_addflooritem(&tmp_item,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0,0,false);
	}

	return true;
}

int pet_menu(struct map_session_data *sd, int menunum)
{
	struct item_data *egg_id;

	nullpo_ret(sd);

	if(!sd->pd)
		return 1;

	if(!sd->status.pet_id || sd->pd->pet.intimate < PETINTIMATE_AWKWARD || sd->pd->pet.incubate)
		return 1; //You lost the pet already

	egg_id = itemdb_exists(sd->pd->petDB->EggID);
	if(egg_id) {
		if((egg_id->flag.trade_restriction&ITR_NODROP) && !pc_inventoryblank(sd)) {
			clif_displaymessage(sd->fd, msg_txt(451)); // You can't return your pet because your inventory is full.
			return 1;
		}
	}

	switch(menunum) {
		case 0:
			clif_send_petstatus(sd);
			break;
		case 1:
			pet_food(sd, sd->pd);
			break;
		case 2:
			pet_performance(sd, sd->pd);
			break;
		case 3:
			pet_return_egg(sd, sd->pd);
			break;
		case 4:
			pet_unequipitem(sd, sd->pd);
			break;
	}
	return 0;
}

int pet_change_name(struct map_session_data *sd,char *name)
{
	int i;
	struct pet_data *pd;

	nullpo_retr(1, sd);

	pd = sd->pd;
	if(!pd || (pd->pet.rename_flag == 1 && !battle_config.pet_rename))
		return 1;

	for(i = 0; i < NAME_LENGTH && name[i]; i++) {
		if(!(name[i]&0xe0) || name[i] == 0x7f)
			return 1;
	}

	return intif_rename_pet(sd, name);
}

int pet_change_name_ack(struct map_session_data *sd, char *name, int flag)
{
	struct pet_data *pd = sd->pd;

	if (!pd)
		return 0;

	normalize_name(name," "); //bugreport:3032

	if (!flag || !strlen(name)) {
		clif_displaymessage(sd->fd, msg_txt(280)); // You cannot use this name for your pet.
		clif_send_petstatus(sd); //Send status so client knows oet name change got rejected.
		return 0;
	}

	safestrncpy(pd->pet.name, name, NAME_LENGTH);
	clif_name_area(&pd->bl);
	pd->pet.rename_flag = 1;
	clif_pet_equip_area(pd);
	clif_send_petstatus(sd);
	return 1;
}

int pet_equipitem(struct map_session_data *sd,int index)
{
	struct pet_data *pd;
	unsigned short nameid;

	nullpo_retr(1, sd);

	if (!(pd = sd->pd))
		return 1;

	nameid = sd->inventory.u.items_inventory[index].nameid;

	if (!pd->petDB->AcceID || nameid != pd->petDB->AcceID || pd->pet.equip) {
		clif_equipitemack(sd,0,0,ITEM_EQUIP_ACK_FAIL);
		return 1;
	}

	pc_delitem(sd,index,1,0,0,LOG_TYPE_OTHER);
	pd->pet.equip = nameid;
	status_set_viewdata(&pd->bl, pd->pet.class_); //Updates view_data
	clif_pet_equip_area(pd);
	if (battle_config.pet_equip_required) { //Skotlex: start support timers if need
		unsigned int tick = gettick();

		if (pd->s_skill && pd->s_skill->timer == INVALID_TIMER) {
			if (pd->s_skill->id)
				pd->s_skill->timer = add_timer(tick + pd->s_skill->delay * 1000, pet_skill_support_timer, sd->bl.id, 0);
			else
				pd->s_skill->timer = add_timer(tick + pd->s_skill->delay * 1000, pet_heal_timer, sd->bl.id, 0);
		}
		if (pd->bonus && pd->bonus->timer == INVALID_TIMER)
			pd->bonus->timer = add_timer(tick + pd->bonus->delay * 1000, pet_skill_bonus_timer, sd->bl.id, 0);
	}
	return 0;
}

static int pet_unequipitem(struct map_session_data *sd, struct pet_data *pd)
{
	struct item tmp_item;
	unsigned short nameid;
	unsigned char flag = 0;

	if (!pd->pet.equip)
		return 1;

	nameid = pd->pet.equip;
	pd->pet.equip = 0;
	status_set_viewdata(&pd->bl, pd->pet.class_);
	clif_pet_equip_area(pd);
	memset(&tmp_item,0,sizeof(tmp_item));
	tmp_item.nameid = nameid;
	tmp_item.identify = 1;
	if ((flag = pc_additem(sd,&tmp_item,1,LOG_TYPE_OTHER))) {
		clif_additem(sd,0,0,flag);
		map_addflooritem(&tmp_item,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0,0,false);
	}
	if (battle_config.pet_equip_required) { //Skotlex: halt support timers if needed
		if (pd->state.skillbonus) {
			pd->state.skillbonus = 0;
			status_calc_pc(sd,SCO_NONE);
		}
		if (pd->s_skill && pd->s_skill->timer != INVALID_TIMER) {
			if (pd->s_skill->id)
				delete_timer(pd->s_skill->timer, pet_skill_support_timer);
			else
				delete_timer(pd->s_skill->timer, pet_heal_timer);
			pd->s_skill->timer = INVALID_TIMER;
		}
		if (pd->bonus && pd->bonus->timer != INVALID_TIMER) {
			delete_timer(pd->bonus->timer, pet_skill_bonus_timer);
			pd->bonus->timer = INVALID_TIMER;
		}
	}

	return 0;
}

static int pet_food(struct map_session_data *sd, struct pet_data *pd)
{
	int i, food_id;

	food_id = pd->petDB->FoodID;
	i = pc_search_inventory(sd,food_id);

	if( i == INDEX_NOT_FOUND ) {
		clif_pet_food(sd,food_id,0);
		return 1;
	}

	pc_delitem(sd,i,1,0,0,LOG_TYPE_CONSUME);

	if( pd->pet.hungry > 90 ) {
		pet_set_intimate(pd,pd->pet.intimate - pd->petDB->r_full);
		if( pd->pet.intimate < PETINTIMATE_AWKWARD ) {
			pd->pet.intimate = 0;
			pet_stop_attack(pd);
			pd->status.speed = pet_get_walk_speed(pd->master);
		}
	} else {
		int add_intimate = 0;

		if( battle_config.pet_friendly_rate != 100 )
			add_intimate = pd->petDB->r_hungry * battle_config.pet_friendly_rate / 100;
		else
			add_intimate = pd->petDB->r_hungry;
		if( pd->pet.hungry > 75 ) {
			add_intimate = add_intimate>>1;
			add_intimate = max(add_intimate,1);
		}
		pet_set_intimate(pd,pd->pet.intimate + add_intimate);
		pd->pet.intimate = min(pd->pet.intimate,1000);
	}

	status_calc_pet(pd,SCO_NONE);
	pd->pet.hungry += battle_config.pet_hungry_feeding_increase;
	pd->pet.hungry = min(pd->pet.hungry,100);
	log_feeding(sd,LOG_FEED_PET,pd->petDB->FoodID);
	clif_send_petdata(sd,pd,2,pd->pet.hungry);
	clif_send_petdata(sd,pd,1,pd->pet.intimate);
	clif_pet_food(sd,pd->petDB->FoodID,1);

	return 0;
}

static int pet_randomwalk(struct pet_data *pd,unsigned int tick)
{
	nullpo_ret(pd);

	Assert(!pd->master || pd->master->pd == pd);

	if(DIFF_TICK(pd->next_walktime,tick) < 0 && unit_can_move(&pd->bl)) {
		const int retrycount = 20;
		int i, c, d = 12 - pd->move_fail_count;

		if(d < 5)
			d = 5;
		for(i = 0; i < retrycount; i++) {
			int r = rnd(), x, y;

			x = pd->bl.x + r%(d * 2 + 1) - d;
			y = pd->bl.y + r / (d * 2 + 1)%(d * 2 + 1) - d;
			if(map_getcell(pd->bl.m,x,y,CELL_CHKPASS) && unit_walktoxy(&pd->bl,x,y,0)) {
				pd->move_fail_count = 0;
				break;
			}
			if(i + 1 >= retrycount) {
				pd->move_fail_count++;
				if(pd->move_fail_count > 1000) {
					ShowWarning("Pet can't move. hold position %d, class = %d\n",pd->bl.id,pd->pet.class_);
					pd->move_fail_count = 0;
					pd->ud.canmove_tick = tick + 60000;
					return 0;
				}
			}
		}
		for(i = c = 0; i < pd->ud.walkpath.path_len; i++) {
			if(direction_diagonal(pd->ud.walkpath.path[i]))
				c += pd->status.speed * MOVE_DIAGONAL_COST / MOVE_COST;
			else
				c += pd->status.speed;
		}
		pd->next_walktime = tick + rnd()%1000 + MIN_RANDOMWALKTIME + c;

		return 1;
	}
	return 0;
}

static int pet_ai_sub_hard(struct pet_data *pd, struct map_session_data *sd, unsigned int tick)
{
	struct block_list *target = NULL;

	if(pd->bl.prev == NULL || sd == NULL || sd->bl.prev == NULL)
		return 0;

	if(DIFF_TICK(tick,pd->last_thinktime) < MIN_PETTHINKTIME)
		return 0;

	pd->last_thinktime = tick;

	if(pd->ud.attacktimer != INVALID_TIMER || pd->ud.skilltimer != INVALID_TIMER || pd->bl.m != sd->bl.m)
		return 0;

	if(pd->ud.walktimer != INVALID_TIMER && pd->ud.walkpath.path_pos <= 2)
		return 0; //No thinking when you just started to walk.

	if(pd->pet.intimate < PETINTIMATE_AWKWARD) { //Pet should just... well, random walk.
		pet_randomwalk(pd,tick);
		return 0;
	}

	if(!check_distance_bl(&sd->bl,&pd->bl,pd->db->range3)) { //Master too far, chase.
		if(pd->target_id)
			pet_unlocktarget(pd);
		if(pd->ud.walktimer != INVALID_TIMER && pd->ud.target == sd->bl.id)
			return 0; //Already walking to him
		if(DIFF_TICK(tick,pd->ud.canmove_tick) < 0)
			return 0; //Can't move yet.
		pd->status.speed = (sd->battle_status.speed>>1);
		if(!pd->status.speed)
			pd->status.speed = 1;
		if(!unit_walktobl(&pd->bl,&sd->bl,3,0))
			pet_randomwalk(pd,tick);
		return 0;
	}

	//Return speed to normal.
	if(pd->status.speed != pet_get_walk_speed(pd->master)) {
		if(pd->ud.walktimer != INVALID_TIMER)
			return 0; //Wait until the pet finishes walking back to master.
		pd->status.speed = pet_get_walk_speed(pd->master);
		pd->ud.state.change_walk_target = pd->ud.state.speed_changed = 1;
	}

	if(pd->target_id) {
		target = map_id2bl(pd->target_id);
		if(!target || pd->bl.m != target->m || status_isdead(target) ||
			!check_distance_bl(&pd->bl,target,pd->db->range3)) {
			target = NULL;
			pet_unlocktarget(pd);
		}
	}

	//Use half the pet's range of sight.
	if(!target && pd->loot && pd->loot->count < pd->loot->max && DIFF_TICK(tick,pd->ud.canact_tick) > 0)
		map_foreachinallrange(pet_ai_sub_hard_lootsearch,&pd->bl,pd->db->range2 / 2,BL_ITEM,pd,&target);

	if (!target) { //Just walk around.
		if(check_distance_bl(&sd->bl,&pd->bl,3))
			return 0; //Already next to master.

		if(pd->ud.walktimer != INVALID_TIMER && check_distance_blxy(&sd->bl,pd->ud.to_x,pd->ud.to_y,3))
			return 0; //Already walking to him

		unit_calc_pos(&pd->bl,sd->bl.x,sd->bl.y,sd->ud.dir);
		if(!unit_walktoxy(&pd->bl,pd->ud.to_x,pd->ud.to_y,0))
			pet_randomwalk(pd,tick);

		return 0;
	}

	if(pd->ud.target == target->id &&
		(pd->ud.attacktimer != INVALID_TIMER || pd->ud.walktimer != INVALID_TIMER))
		return 0; //Target already locked

	if(target->type != BL_ITEM) { //enemy targeted
		if(!battle_check_range(&pd->bl,target,pd->status.rhw.range)) { //Chase
			if(!unit_walktobl(&pd->bl,target,pd->status.rhw.range,2))
				pet_unlocktarget(pd); //Unreachable target.
			return 0;
		}
		unit_attack(&pd->bl,pd->target_id,1); //Continuous attack
	} else { //Item Targeted, attempt loot
		if(!check_distance_bl(&pd->bl,target,1)) { //Out of range
			if(!unit_walktobl(&pd->bl,target,1,1)) //Unreachable target
				pet_unlocktarget(pd);
			return 0;
		} else {
			struct flooritem_data *fitem = (struct flooritem_data *)target;

			if(pd->loot->count < pd->loot->max) {
				memcpy(&pd->loot->item[pd->loot->count++],&fitem->item,sizeof(pd->loot->item[0]));
				pd->loot->weight += itemdb_weight(fitem->item.nameid) * fitem->item.amount;
				map_clearflooritem(target);
			}
			pet_unlocktarget(pd); //Target is unlocked regardless of whether it was picked or not
		}
	}
	return 0;
}

static int pet_ai_sub_foreachclient(struct map_session_data *sd,va_list ap)
{
	unsigned int tick = va_arg(ap,unsigned int);
	if(sd->status.pet_id && sd->pd)
		pet_ai_sub_hard(sd->pd,sd,tick);

	return 0;
}

static TIMER_FUNC(pet_ai_hard)
{
	map_foreachpc(pet_ai_sub_foreachclient,tick);

	return 0;
}

static int pet_ai_sub_hard_lootsearch(struct block_list *bl,va_list ap)
{
	struct pet_data *pd;
	struct flooritem_data *fitem = (struct flooritem_data *)bl;
	struct block_list **target;
	int sd_charid = 0;

	pd = va_arg(ap,struct pet_data *);
	target = va_arg(ap,struct block_list**);

	sd_charid = fitem->first_get_charid;

	if(sd_charid && sd_charid != pd->master->status.char_id)
		return 0;

	if(unit_can_reach_bl(&pd->bl,bl, pd->db->range2, 1, NULL, NULL) && (*target) == NULL) {
		(*target) = bl;
		pd->target_id = bl->id;
		return 1;
	}

	return 0;
}

static TIMER_FUNC(pet_delay_item_drop)
{
	struct item_drop_list *list = (struct item_drop_list *)data;
	struct item_drop *ditem = list->item;

	while (ditem) {
		struct item_drop *ditem_prev;

		map_addflooritem(&ditem->item_data,ditem->item_data.amount,
			list->m,list->x,list->y,
			list->first_charid,list->second_charid,list->third_charid,4,0,false);
		ditem_prev = ditem;
		ditem = ditem->next;
		ers_free(item_drop_ers, ditem_prev);
	}
	ers_free(item_drop_list_ers, list);
	return 0;
}

int pet_lootitem_drop(struct pet_data *pd, struct map_session_data *sd)
{
	int i;
	struct item_drop_list *dlist;
	struct item_drop *ditem;

	if(!pd || !pd->loot || !pd->loot->count)
		return 0;
	dlist = ers_alloc(item_drop_list_ers, struct item_drop_list);
	dlist->m = pd->bl.m;
	dlist->x = pd->bl.x;
	dlist->y = pd->bl.y;
	dlist->first_charid = 0;
	dlist->second_charid = 0;
	dlist->third_charid = 0;
	dlist->item = NULL;

	for(i = 0; i < pd->loot->count; i++) {
		struct item *it = &pd->loot->item[i];

		if(sd) {
			unsigned char flag = 0;

			if((flag = pc_additem(sd,it,it->amount,LOG_TYPE_PICKDROP_PLAYER))){
				clif_additem(sd,0,0,flag);
				ditem = ers_alloc(item_drop_ers, struct item_drop);
				memcpy(&ditem->item_data, it, sizeof(struct item));
				ditem->next = dlist->item;
				dlist->item = ditem;
			}
		} else {
			ditem = ers_alloc(item_drop_ers, struct item_drop);
			memcpy(&ditem->item_data, it, sizeof(struct item));
			ditem->next = dlist->item;
			dlist->item = ditem;
		}
	}
	//The smart thing to do is use pd->loot->max (thanks for pointing it out, Shinomori)
	memset(pd->loot->item,0,pd->loot->max * sizeof(struct item));
	pd->loot->count = 0;
	pd->loot->weight = 0;
	pd->ud.canact_tick = gettick() + 10000; //Prevent picked up during 10 * 1000ms

	if (dlist->item)
		add_timer(gettick() + 540,pet_delay_item_drop,0,(intptr_t)dlist);
	else
		ers_free(item_drop_list_ers, dlist);
	return 1;
}

void pet_evolution(struct map_session_data *sd, unsigned short pet_idx)
{
	int i = 0, idx, petIndex;
	bool checked = false;
	int class_ = pet_db[pet_idx].class_;
	int egg_id = pet_db[pet_idx].EggID;

	nullpo_retv(sd);

	if (!battle_config.feature_pet_evolution || !sd->pd) {
		clif_pet_evolution_result(sd, PET_EVOL_UNKNOWN);
		return;
	}

	if (!sd->pd->pet.pet_id) {
		clif_pet_evolution_result(sd, PET_EVOL_NO_CALLPET);
		return;
	}

	ARR_FIND(0, MAX_INVENTORY, idx, (sd->inventory.u.items_inventory[idx].card[0] == CARD0_PET &&
		sd->pd->pet.pet_id == MakeDWord(sd->inventory.u.items_inventory[idx].card[1], sd->inventory.u.items_inventory[idx].card[2])));
	if (idx == MAX_INVENTORY) {
		clif_pet_evolution_result(sd, PET_EVOL_NO_PETEGG);
		return;
	}

	if (sd->pd->pet.intimate < PETINTIMATE_LOYAL) {
		clif_pet_evolution_result(sd, PET_EVOL_RG_FAMILIAR);
		return;
	}

	ARR_FIND(0, MAX_PET_DB, petIndex, pet_db[petIndex].class_ == sd->pd->pet.class_);
	if (petIndex == MAX_PET_DB) {
		clif_pet_evolution_result(sd, PET_EVOL_UNKNOWN);
		return;
	}

	//Client side validation is not done as it is insecure
	for (i = 0; i < pet_db[petIndex].ev_data_count; i++) {
		struct s_pet_evo_datalist *ped = &pet_db[petIndex].ev_datas[i];

		if (ped->class_ == class_) {
			int j;

			if (!ped->ev_item_count) {
				clif_pet_evolution_result(sd, PET_EVOL_NO_RECIPE);
				return;
			}
			for (j = 0; j < ped->ev_item_count; j++) {
				struct s_pet_evo_itemlist *list = &ped->ev_items[j];
				short n = pc_search_inventory(sd, list->nameid);

				if (n == INDEX_NOT_FOUND) {
					clif_pet_evolution_result(sd, PET_EVOL_NO_MATERIAL);
					return;
				}
				if (pc_delitem(sd, n, list->quantity, 0, 0, LOG_TYPE_OTHER)) {
					clif_pet_evolution_result(sd, PET_EVOL_NO_MATERIAL);
					return;
				}
			}
			checked = true;
		}
	}

	if (checked) {
		//Virtually delete the old egg
		log_pick_pc(sd, LOG_TYPE_OTHER, -1, &sd->inventory.u.items_inventory[idx]);
		clif_delitem(sd, idx, 1, 0);

		//Change the old egg to the new one
		sd->inventory.u.items_inventory[idx].nameid = egg_id;
		sd->inventory_data[idx] = itemdb_search(egg_id);

		//Virtually add it to the inventory
		log_pick_pc(sd, LOG_TYPE_OTHER, 1, &sd->inventory.u.items_inventory[idx]);
		clif_additem(sd, idx, 1, 0);

		//Remove the old pet from sight
		unit_remove_map(&sd->pd->bl, CLR_OUTSIGHT);

		//Prepare the new pet
		sd->catch_target_class = class_;
		sd->pd->pet.class_ = class_;
		sd->pd->pet.egg_id = egg_id;
		sd->pd->pet.intimate = pet_db[pet_idx].intimate;
		if (!sd->pd->pet.rename_flag)
			safestrncpy(sd->pd->pet.name, pet_db[pet_idx].jname, NAME_LENGTH);
		status_set_viewdata(&sd->pd->bl, class_);

		//Save the pet and inventory data
		intif_save_petdata(sd->status.account_id, &sd->pd->pet);
		if (save_settings&CHARSAVE_PET)
			chrif_save(sd, CSAVE_INVENTORY);

		//Spawn it
		if (map_addblock(&sd->pd->bl))
			return;

		clif_spawn(&sd->pd->bl);
		clif_send_petdata(sd, sd->pd, 0, 0);
		clif_send_petdata(sd, sd->pd, 5, battle_config.pet_hair_style);
		clif_pet_equip_area(sd->pd);
		clif_send_petstatus(sd);
		clif_emotion(&sd->bl, E_NO1);
		clif_specialeffect(&sd->pd->bl, EF_HO_UP, AREA);

		clif_pet_evolution_result(sd, PET_EVOL_SUCCESS);
		clif_inventorylist(sd);
	} else
		clif_pet_evolution_result(sd, PET_EVOL_UNKNOWN);
}

/*==========================================
 * pet bonus giving skills [Valaris] / Rewritten by [Skotlex]
 *------------------------------------------*/
TIMER_FUNC(pet_skill_bonus_timer)
{
	struct map_session_data *sd = map_id2sd(id);
	struct pet_data *pd;
	int bonus;
	int timer = 0;

	if (!sd || !sd->pd || !sd->pd->bonus)
		return 1;

	pd = sd->pd;

	if (pd->bonus->timer != tid) {
		ShowError("pet_skill_bonus_timer %d != %d\n",pd->bonus->timer,tid);
		pd->bonus->timer = INVALID_TIMER;
		return 0;
	}

	//Determine the time for the next timer
	if (pd->state.skillbonus && pd->bonus->delay > 0) {
		bonus = 0;
		timer = pd->bonus->delay * 1000; //The duration until pet bonuses will be reactivated again
	} else if (pd->pet.intimate) {
		bonus = 1;
		timer = pd->bonus->duration * 1000; //The duration for pet bonuses to be in effect
	} else { //Lost pet
		pd->bonus->timer = INVALID_TIMER;
		return 0;
	}

	if (pd->state.skillbonus != bonus) {
		pd->state.skillbonus = bonus;
		status_calc_pc(sd,SCO_NONE);
	}
	//Wait for the next timer
	pd->bonus->timer = add_timer(tick + timer,pet_skill_bonus_timer,sd->bl.id,0);
	return 0;
}

/*==========================================
 * pet recovery skills [Valaris] / Rewritten by [Skotlex]
 *------------------------------------------*/
TIMER_FUNC(pet_recovery_timer)
{
	struct map_session_data *sd = map_id2sd(id);
	struct pet_data *pd;

	if (!sd || !sd->pd || !sd->pd->recovery)
		return 1;

	pd = sd->pd;

	if (pd->recovery->timer != tid) {
		ShowError("pet_recovery_timer %d != %d\n",pd->recovery->timer,tid);
		return 0;
	}

	if (sd->sc.data[pd->recovery->type]) { //Display a heal animation?
		clif_skill_nodamage(&pd->bl,&sd->bl,TF_DETOXIFY,1,1); //Detoxify is chosen for now
		status_change_end(&sd->bl,pd->recovery->type,INVALID_TIMER);
		clif_emotion(&pd->bl,E_OK);
	}

	pd->recovery->timer = INVALID_TIMER;
	return 0;
}

TIMER_FUNC(pet_heal_timer)
{
	struct map_session_data *sd = map_id2sd(id);
	struct status_data *status;
	struct pet_data *pd;
	unsigned int rate = 100;

	if (sd == NULL || sd->pd == NULL || sd->pd->s_skill == NULL)
		return 1;

	pd = sd->pd;

	if (pd->s_skill->timer != tid) {
		ShowError("pet_heal_timer %d != %d\n",pd->s_skill->timer,tid);
		return 0;
	}

	status = status_get_status_data(&sd->bl);

	if (pc_isdead(sd) ||
		(rate = get_percentage(status->sp,status->max_sp)) > pd->s_skill->sp ||
		(rate = get_percentage(status->hp,status->max_hp)) > pd->s_skill->hp ||
		(rate = (pd->ud.skilltimer != INVALID_TIMER))) //Another skill is in effect
	{ //Wait (how long? 1 sec for every 10% of remaining)
		pd->s_skill->timer = add_timer(gettick() + (rate > 10 ? rate : 10) * 100,pet_heal_timer,sd->bl.id,0);
		return 0;
	}
	pet_stop_attack(pd);
	pet_stop_walking(pd,1);
	clif_skill_nodamage(&pd->bl,&sd->bl,AL_HEAL,pd->s_skill->lv,1);
	status_heal(&sd->bl,pd->s_skill->lv,0,0);
	pd->s_skill->timer = add_timer(tick + pd->s_skill->delay * 1000,pet_heal_timer,sd->bl.id,0);
	return 0;
}

/*==========================================
 * pet support skills [Skotlex]
 *------------------------------------------*/
TIMER_FUNC(pet_skill_support_timer)
{
	struct map_session_data *sd = map_id2sd(id);
	struct pet_data *pd;
	struct status_data *status;
	short rate = 100;

	if (!sd || !sd->pd || !sd->pd->s_skill)
		return 1;

	pd = sd->pd;

	if (pd->s_skill->timer != tid) {
		ShowError("pet_skill_support_timer %d != %d\n",pd->s_skill->timer,tid);
		return 0;
	}

	status = status_get_status_data(&sd->bl);

	if (DIFF_TICK(pd->ud.canact_tick, tick) > 0) { //Wait until the pet can act again
		pd->s_skill->timer = add_timer(pd->ud.canact_tick,pet_skill_support_timer,sd->bl.id,0);
		return 0;
	}

	if (pc_isdead(sd) ||
		(rate = get_percentage(status->sp,status->max_sp)) > pd->s_skill->sp ||
		(rate = get_percentage(status->hp,status->max_hp)) > pd->s_skill->hp ||
		(rate = (pd->ud.skilltimer != INVALID_TIMER))) //Another skill is in effect
	{ //Wait (how long? 1 sec for every 10% of remaining)
		pd->s_skill->timer = add_timer(tick + (rate > 10 ? rate : 10) * 100,pet_skill_support_timer,sd->bl.id,0);
		return 0;
	}

	pet_stop_attack(pd);
	pet_stop_walking(pd,1);
	pd->s_skill->timer = add_timer(tick + pd->s_skill->delay * 1000,pet_skill_support_timer,sd->bl.id,0);
	if (skill_get_inf(pd->s_skill->id)&INF_GROUND_SKILL)
		unit_skilluse_pos(&pd->bl,sd->bl.x,sd->bl.y,pd->s_skill->id,pd->s_skill->lv);
	else
		unit_skilluse_id(&pd->bl,sd->bl.id,pd->s_skill->id,pd->s_skill->lv);
	return 0;
}

static void pet_readdb_clear(bool destroy)
{
	int i;

	for (i = 0; i < MAX_PET_DB; i++) { //Remove any previous scripts in case reloaddb was invoked
		int j;

		if (pet_db[i].pet_script) {
			script_free_code(pet_db[i].pet_script);
			pet_db[i].pet_script = NULL;
		}
		if (pet_db[i].pet_friendly_script) {
			script_free_code(pet_db[i].pet_friendly_script);
			pet_db[i].pet_friendly_script = NULL;
		}
		if (pet_db[i].ev_datas) {
			for (j = 0; j < pet_db[i].ev_data_count; j++) {
				if (pet_db[i].ev_datas[j].ev_items) {
					aFree(pet_db[i].ev_datas[j].ev_items);
					pet_db[i].ev_datas[j].ev_items = NULL;
					pet_db[i].ev_datas->ev_item_count = 0;
				}
			}
			aFree(pet_db[i].ev_datas);
			pet_db[i].ev_datas = NULL;
			pet_db[i].ev_data_count = 0;
		}
	}

	if (!destroy) //Clear database
		memset(pet_db,0,sizeof(pet_db));
}

static void pet_readdb_libconfig_sub_intimacy(struct config_setting_t *t, int idx)
{
	int i32 = 0;

	nullpo_retv(t);

	Assert(idx >= 0 && idx < MAX_PET_DB);

	if (config_setting_lookup_int(t, "Initial", &i32))
		pet_db[idx].intimate = i32;

	if (config_setting_lookup_int(t, "FeedIncrement", &i32))
		pet_db[idx].r_hungry = max(i32, 1);

	if (config_setting_lookup_int(t, "OverFeedDecrement", &i32))
		pet_db[idx].r_full = i32;

	if (config_setting_lookup_int(t, "OwnerDeathDecrement", &i32))
		pet_db[idx].die = i32;
}

static void pet_readdb_libconfig_sub_evolution(struct config_setting_t *t, int idx)
{
	int i, len = config_setting_length(t);

	nullpo_retv(t);

	Assert(idx >= 0 && idx < MAX_PET_DB);

	CREATE(pet_db[idx].ev_datas, struct s_pet_evo_datalist, 1);

	for (i = 0; i < len; i++) {
		struct config_setting_t *tt = config_setting_get_elem(t, i);
		struct config_setting_t *itemt = NULL;
		int mob_id = 0;

		if (!tt)
			break;
		if (!config_setting_is_group(tt))
			continue;
		if (!config_setting_lookup_int(tt, "Id", &mob_id)) {
			ShowWarning("pet_readdb_libconfig_sub_evolution: Missing Evolved Id in Pet #%d.\n", pet_db[idx].class_);
			continue;
		}
		if (mob_id && !mobdb_exists(mob_id)) {
			ShowWarning("pet_readdb_libconfig_sub_evolution: Invalid Evolved Id %d in Pet #%d.\n", mob_id, pet_db[idx].class_);
			continue;
		}
		RECREATE(pet_db[idx].ev_datas, struct s_pet_evo_datalist, pet_db[idx].ev_data_count + 1);
		pet_db[idx].ev_datas[i].class_ = mob_id;
		if (!(itemt = config_setting_get_member(tt, "Requirements"))) {
			ShowWarning("pet_readdb_libconfig_sub_evolution: Missing Evolved Requirements in Pet #%d.\n", pet_db[idx].class_);
			continue;
		}
		if (config_setting_is_list(itemt)) {
			int j, len2 = config_setting_length(itemt);

			CREATE(pet_db[idx].ev_datas[i].ev_items, struct s_pet_evo_itemlist, 1);
			for (j = 0; j < len2; j++) {
				struct config_setting_t *itemtt = config_setting_get_elem(itemt, j);
				int nameid = 0, quantity = 0;

				if (!itemtt)
					break;
				if (!config_setting_is_group(itemtt))
					continue;
				if (!config_setting_lookup_int(itemtt, "ItemID", &nameid)) {
					ShowWarning("pet_readdb_libconfig_sub_evolution: Missing Required ItemID in Evolved Pet #%d.\n", mob_id);
					continue;
				}
				if (nameid && !itemdb_exists(nameid)) {
					ShowWarning("pet_readdb_libconfig_sub_evolution: Invalid Required ItemID %d in Evolved Pet #%d\n", nameid, mob_id);
					continue;
				}
				if (!config_setting_lookup_int(itemtt, "Amount", &quantity)) {
					ShowWarning("pet_readdb_libconfig_sub_evolution: Missing Required Amount in Evolved Pet #%d.\n", mob_id);
					continue;
				}
				if (quantity <= 0) {
					ShowWarning("pet_readdb_libconfig_sub_evolution: Invalid Required Amount %d in Evolved Pet #%d\n", quantity, mob_id);
					continue;
				}
				RECREATE(pet_db[idx].ev_datas[i].ev_items, struct s_pet_evo_itemlist, pet_db[idx].ev_datas[i].ev_item_count + 1);
				pet_db[idx].ev_datas[i].ev_items[j].nameid = nameid;
				pet_db[idx].ev_datas[i].ev_items[j].quantity = quantity;
				pet_db[idx].ev_datas[i].ev_item_count++;
			}
		}
		pet_db[idx].ev_data_count++;
	}
}

static int pet_readdb_libconfig_sub(struct config_setting_t *it, int n, const char *source)
{
	struct config_setting_t *t = NULL;
	struct item_data *id = NULL;
	const char *str = NULL;
	int i32 = 0;

	nullpo_ret(it);
	nullpo_ret(source);

	Assert(n >= 0 && n < MAX_PET_DB);

	if (!config_setting_lookup_int(it, "Id", &i32)) {
		ShowWarning("pet_readdb_libconfig_sub: Missing Id in \"%s\", entry #%d, skipping.\n", source, n);
		return 0;
	}
	pet_db[n].class_ = i32;

	if (!config_setting_lookup_string(it, "SpriteName", &str) || !*str ) {
		ShowWarning("pet_readdb_libconfig_sub: Missing SpriteName in pet %d of \"%s\", skipping.\n", pet_db[n].class_, source);
		return 0;
	}
	safestrncpy(pet_db[n].name, str, sizeof(pet_db[n].name));

	if (!config_setting_lookup_string(it, "Name", &str) || !*str) {
		ShowWarning("pet_readdb_libconfig_sub: Missing Name in pet %d of \"%s\", skipping.\n", pet_db[n].class_, source);
		return 0;
	}
	safestrncpy(pet_db[n].jname, str, sizeof(pet_db[n].jname));

	if (config_setting_lookup_int(it, "TamingItem", &i32)) {
		if (!(id = itemdb_exists(i32)))
			ShowWarning("pet_readdb_libconfig_sub: Invalid item ID %d in pet %d of \"%s\", defaulting to 0.\n", i32, pet_db[n].class_, source);
		else
			pet_db[n].itemID = id->nameid;
	}

	if (config_setting_lookup_int(it, "EggItem", &i32)) {
		if (!(id = itemdb_exists(i32)))
			ShowWarning("pet_readdb_libconfig_sub: Invalid item ID %d in pet %d of \"%s\", defaulting to 0.\n", i32, pet_db[n].class_, source);
		else
			pet_db[n].EggID = id->nameid;
	}

	if (config_setting_lookup_int(it, "AccessoryItem", &i32)) {
		if (!(id = itemdb_exists(i32)))
			ShowWarning("pet_readdb_libconfig_sub: Invalid item ID %d in pet %d of \"%s\", defaulting to 0.\n", i32, pet_db[n].class_, source);
		else
			pet_db[n].AcceID = id->nameid;
	}

	if (config_setting_lookup_int(it, "FoodItem", &i32)) {
		if (!(id = itemdb_exists(i32)))
			ShowWarning("pet_readdb_libconfig_sub: Invalid item ID %d in pet %d of \"%s\", defaulting to 0.\n", i32, pet_db[n].class_, source);
		else
			pet_db[n].FoodID = id->nameid;
	}

	if (config_setting_lookup_int(it, "HungerPoints", &i32))
		pet_db[n].hunger_pts = i32;

	if (config_setting_lookup_int(it, "HungerDelay", &i32))
		pet_db[n].hungry_delay = i32 * 1000;

	if ((t = config_setting_get_member(it, "Intimacy")) && config_setting_is_group(t))
		pet_readdb_libconfig_sub_intimacy(t, n);

	if (config_setting_lookup_int(it, "CaptureRate", &i32))
		pet_db[n].capture = i32;

	if (config_setting_lookup_int(it, "Speed", &i32))
		pet_db[n].speed = i32;

	if ((t = config_setting_get_member(it, "SpecialPerformance")) && (i32 = config_setting_get_bool(t)))
		pet_db[n].s_perfor = (char)i32;

	if ((t = config_setting_get_member(it, "TalkWithEmotes")) && (i32 = config_setting_get_bool(t)))
		pet_db[n].talk_convert_class = i32;

	if (config_setting_lookup_int(it, "AttackRate", &i32))
		pet_db[n].attack_rate = i32;

	if (config_setting_lookup_int(it, "DefendRate", &i32))
		pet_db[n].defence_attack_rate = i32;

	if (config_setting_lookup_int(it, "ChangeTargetRate", &i32))
		pet_db[n].change_target_rate = i32;

	if ((t = config_setting_get_member(it, "Evolve")) && config_setting_is_list(t))
		pet_readdb_libconfig_sub_evolution(t, n);

	if (config_setting_lookup_string(it, "PetScript", &str))
		pet_db[n].pet_script = (*str ? parse_script(str, source, pet_db[n].class_, SCRIPT_IGNORE_EXTERNAL_BRACKETS) : NULL);

	if (config_setting_lookup_string(it, "PetFriendlyScript", &str))
		pet_db[n].pet_friendly_script = (*str ? parse_script(str, source, pet_db[n].class_, SCRIPT_IGNORE_EXTERNAL_BRACKETS) : NULL);

	return pet_db[n].class_;
}

static void pet_readdb_libconfig(const char *filename, bool ignore_missing)
{
	struct config_setting_t *pdb = NULL, *t = NULL;
	bool duplicate[MAX_MOB_DB];
	char filepath[256];
	int count = 0;

	memset(&duplicate, 0, sizeof(duplicate));

	safesnprintf(filepath, sizeof(filepath), "%s/%s", db_path, filename);

	if (!exists(filepath)) {
		if (!ignore_missing)
			ShowError("pet_readdb_libconfig: Can't find file %s\n", filepath);
		return;
	}

	if (config_read_file(&pet_db_conf, filepath))
		return;

	if (!(pdb = config_lookup(&pet_db_conf, "pet_db")))
		return;

	while ((t = config_setting_get_elem(pdb, count))) {
		int pet_id = pet_readdb_libconfig_sub(t, count, filename);

		if (pet_id <= 0 || pet_id >= MAX_MOB_DB)
			continue;
		if (duplicate[pet_id])
			ShowWarning("pet_readdb_libconfig:%s: Duplicate entry of ID #%d\n", filename, pet_id);
		else
			duplicate[pet_id] = true;
		count++;
	}

	config_destroy(&pet_db_conf);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, filepath);
}

void pet_readdb(void)
{
	const char *filename[] = {
		DBPATH"pet_db.conf",
		"pet_db2.conf"
	};
	int i;

	pet_readdb_clear(false);
	for (i = 0; i < ARRAYLENGTH(filename); ++i)
		pet_readdb_libconfig(filename[i], (i > 0 ? true : false));
}

/*==========================================
 * Initialization process relationship skills
 *------------------------------------------*/
void do_init_pet(void)
{
	pet_readdb();

	item_drop_ers = ers_new(sizeof(struct item_drop),"pet.c::item_drop_ers",ERS_OPT_NONE);
	item_drop_list_ers = ers_new(sizeof(struct item_drop_list),"pet.c::item_drop_list_ers",ERS_OPT_NONE);

	add_timer_func_list(pet_hungry,"pet_hungry");
	add_timer_func_list(pet_ai_hard,"pet_ai_hard");
	add_timer_func_list(pet_skill_bonus_timer,"pet_skill_bonus_timer"); // [Valaris]
	add_timer_func_list(pet_delay_item_drop,"pet_delay_item_drop");
	add_timer_func_list(pet_skill_support_timer, "pet_skill_support_timer"); // [Skotlex]
	add_timer_func_list(pet_recovery_timer,"pet_recovery_timer"); // [Valaris]
	add_timer_func_list(pet_heal_timer,"pet_heal_timer"); // [Valaris]
	add_timer_interval(gettick() + MIN_PETTHINKTIME,pet_ai_hard,0,0,MIN_PETTHINKTIME);
}

void do_final_pet(void)
{
	pet_readdb_clear(true);
	ers_destroy(item_drop_ers);
	ers_destroy(item_drop_list_ers);
}
