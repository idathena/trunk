// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"
#include "../common/ers.h"
#include "../common/random.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "map.h"
#include "path.h"
#include "pc.h"
#include "status.h"
#include "skill.h"
#include "homunculus.h"
#include "mercenary.h"
#include "elemental.h"
#include "mob.h"
#include "itemdb.h"
#include "clif.h"
#include "pet.h"
#include "guild.h"
#include "party.h"
#include "battle.h"
#include "battleground.h"
#include "chrif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int attr_fix_table[4][ELE_MAX][ELE_MAX];
int64 battle_damage_temp[2];

struct Battle_Config battle_config;
static struct eri *delay_damage_ers; //For battle delay damage structures

uint16 battle_getcurrentskill(struct block_list *bl) { //Returns the current/last skill in use by this bl
	struct unit_data *ud;

	if( bl->type == BL_SKILL ) {
		struct skill_unit *su = (struct skill_unit *)bl;

		return (su && su->group ? su->group->skill_id : 0);
	}

	ud = unit_bl2ud(bl);

	return (ud ? ud->skill_id : 0);
}

/*==========================================
 * Get random targetting enemy
 *------------------------------------------*/
static int battle_gettargeted_sub(struct block_list *bl, va_list ap) {
	struct block_list **bl_list;
	struct unit_data *ud;
	int target_id;
	int *c;

	bl_list = va_arg(ap, struct block_list **);
	c = va_arg(ap, int *);
	target_id = va_arg(ap, int);

	if (bl->id == target_id)
		return 0;

	if (*c >= 24)
		return 0;

	if (!(ud = unit_bl2ud(bl)))
		return 0;

	if (ud->target == target_id || ud->skilltarget == target_id) {
		bl_list[(*c)++] = bl;
		return 1;
	}

	return 0;
}

struct block_list *battle_gettargeted(struct block_list *target) {
	struct block_list *bl_list[24];
	int c = 0;

	nullpo_retr(NULL, target);

	memset(bl_list, 0, sizeof(bl_list));
	map_foreachinallrange(battle_gettargeted_sub, target, AREA_SIZE, BL_CHAR, bl_list, &c, target->id);
	if (c == 0)
		return NULL;
	if (c > 24)
		c = 24;
	return bl_list[rnd()%c];
}

//Returns the id of the current targeted character of the passed bl. [Skotlex]
int battle_gettarget(struct block_list *bl) {

	switch (bl->type) {
		case BL_PC:  return ((struct map_session_data *)bl)->ud.target;
		case BL_MOB: return ((struct mob_data *)bl)->target_id;
		case BL_PET: return ((struct pet_data *)bl)->target_id;
		case BL_HOM: return ((struct homun_data *)bl)->ud.target;
		case BL_MER: return ((struct mercenary_data *)bl)->ud.target;
		case BL_ELEM: return ((struct elemental_data *)bl)->ud.target;
	}

	return 0;
}

static int battle_getenemy_sub(struct block_list *bl, va_list ap) {
	struct block_list **bl_list;
	struct block_list *target;
	int *c;

	bl_list = va_arg(ap, struct block_list **);
	c = va_arg(ap, int *);
	target = va_arg(ap, struct block_list *);

	if (bl->id == target->id)
		return 0;

	if (*c >= 24)
		return 0;

	if (status_isdead(bl))
		return 0;

	if (battle_check_target(target, bl, BCT_ENEMY) > 0) {
		bl_list[(*c)++] = bl;
		return 1;
	}

	return 0;
}

//Picks a random enemy of the given type (BL_PC, BL_CHAR, etc) within the range given. [Skotlex]
struct block_list *battle_getenemy(struct block_list *target, int type, int range) {
	struct block_list *bl_list[24];
	int c = 0;

	memset(bl_list, 0, sizeof(bl_list));
	map_foreachinallrange(battle_getenemy_sub, target, range, type, bl_list, &c, target);

	if (c == 0)
		return NULL;

	if (c > 24)
		c = 24;

	return bl_list[rnd()%c];
}

static int battle_getenemyarea_sub(struct block_list *bl, va_list ap) {
	struct block_list **bl_list, *src;
	int *c, ignore_id;
	uint16 skill_id;

	bl_list = va_arg(ap, struct block_list **);
	c = va_arg(ap, int *);
	src = va_arg(ap, struct block_list *);
	ignore_id = va_arg(ap, int);
	skill_id = va_arg(ap, unsigned int);

	if( bl->id == src->id || bl->id == ignore_id )
		return 0; //Ignores Caster and a possible pre-target

	if( *c >= 23 )
		return 0;

	if( status_isdead(bl) || !status_check_skilluse(src, bl, skill_id, 2) )
		return 0;

	if( battle_check_target(src, bl, BCT_ENEMY) > 0 ) { //Is Enemy!
		bl_list[(*c)++] = bl;
		return 1;
	}

	return 0;
}

//Pick a random enemy
struct block_list *battle_getenemyarea(struct block_list *src, int x, int y, int range, int type, int ignore_id, uint16 skill_id) {
	struct block_list *bl_list[24];
	int c = 0;

	memset(bl_list, 0, sizeof(bl_list));
	map_foreachinallarea(battle_getenemyarea_sub, src->m, x - range, y - range, x + range, y + range, type, bl_list, &c, src, ignore_id);

	if( !c )
		return NULL;

	if( c >= 24 )
		c = 23;

	return bl_list[rnd()%c];
}

/**
 * Deals damage without delay, applies additional effects and triggers monster events
 * This function is called from battle_delay_damage or battle_delay_damage_sub
 * @author [Playtester]
 * @param src: Source of damage
 * @param target: Target of damage
 * @param damage: Damage to be dealt
 * @param delay: Damage delay
 * @param skill_lv: Level of skill used
 * @param skill_id: ID o skill used
 * @param dmg_lv: State of the attack (miss, etc.)
 * @param attack_type: Type of the attack (BF_NORMAL|BF_SKILL|BF_SHORT|BF_LONG|BF_WEAPON|BF_MAGIC|BF_MISC)
 * @param additional_effects: Whether additional effect should be applied
 * @param isspdamage: If the damage is done to SP
 * @param tick: Current tick
 */
void battle_damage(struct block_list *src, struct block_list *target, int64 damage, int delay, uint16 skill_lv, uint16 skill_id, enum damage_lv dmg_lv, unsigned short attack_type, bool additional_effects, unsigned int tick, bool isspdamage) {
	if( isspdamage )
		status_fix_spdamage(src, target, damage, delay);
	else
		status_fix_damage(src, target, damage, delay); //We have to separate here between reflect damage and others [icescope]
	if( attack_type ) {
		if( !status_isdead(target) && additional_effects )
			skill_additional_effect(src, target, skill_id, skill_lv, attack_type, dmg_lv, tick);
		if( dmg_lv > ATK_BLOCK )
			skill_counter_additional_effect(src, target, skill_id, skill_lv, attack_type, tick);
	}
	//This is the last place where we have access to the actual damage type, so any monster events depending on type must be placed here
	if( target->type == BL_MOB && damage > 0 && (attack_type&BF_NORMAL) ) {
		//Monsters differentiate whether they have been attacked by a skill or a normal attack
		struct mob_data *md = BL_CAST(BL_MOB, target);

		md->norm_attacked_id = md->attacked_id;
	}
}

//Damage delayed info
struct delay_damage {
	int src_id;
	int target_id;
	int64 damage;
	int delay;
	unsigned short distance;
	uint16 skill_lv;
	uint16 skill_id;
	enum damage_lv dmg_lv;
	unsigned short attack_type;
	bool additional_effects;
	enum bl_type src_type;
	bool isspdamage;
};

TIMER_FUNC(battle_delay_damage_sub) {
	struct delay_damage *dat = (struct delay_damage *)data;

	if( dat ) {
		struct block_list *src = map_id2bl(dat->src_id);
		struct block_list *target = map_id2bl(dat->target_id);

		if( !target || status_isdead(target) ) { //Nothing we can do
			if( src && src->type == BL_PC && --((TBL_PC *)src)->delayed_damage == 0 && ((TBL_PC *)src)->state.hold_recalc ) {
				((TBL_PC *)src)->state.hold_recalc = 0;
				status_calc_pc(((TBL_PC *)src), SCO_FORCE);
			}
			ers_free(delay_damage_ers, dat);
			return 0;
		}
		if( src &&
			(target->type != BL_PC || ((TBL_PC *)target)->invincible_timer == INVALID_TIMER) &&
			(dat->skill_id == MO_EXTREMITYFIST || (target->m == src->m && check_distance_bl(src, target, dat->distance))) )
		{ //Check to see if you haven't teleported [Skotlex]
			map_freeblock_lock();
			//Deal damage
			battle_damage(src, target, dat->damage, dat->delay, dat->skill_lv, dat->skill_id, dat->dmg_lv, dat->attack_type, dat->additional_effects, tick, dat->isspdamage);
			map_freeblock_unlock();
		} else if( !src && dat->skill_id == CR_REFLECTSHIELD ) {
			//It was monster reflected damage, and the monster died, we pass the damage to the character as expected
			map_freeblock_lock();
			status_fix_damage(target, target, dat->damage, dat->delay);
			map_freeblock_unlock();
		}
		if( src && src->type == BL_PC && --((TBL_PC *)src)->delayed_damage == 0 && ((TBL_PC *)src)->state.hold_recalc ) {
			((TBL_PC *)src)->state.hold_recalc = 0;
			status_calc_pc(((TBL_PC *)src), SCO_FORCE);
		}
	}
	ers_free(delay_damage_ers, dat);
	return 0;
}

int battle_delay_damage(unsigned int tick, int amotion, struct block_list *src, struct block_list *target, int attack_type, uint16 skill_id, uint16 skill_lv, int64 damage, enum damage_lv dmg_lv, int ddelay, bool additional_effects, bool isvanishdamage, bool isspdamage)
{
	struct delay_damage *dat;
	struct status_change *sc;
	struct block_list *d_tbl = NULL, *e_tbl = NULL;
	struct map_session_data *s_tsd = NULL;

	nullpo_ret(src);
	nullpo_ret(target);

	if( (sc = status_get_sc(target)) ) {
		if( sc->data[SC_DEVOTION] && sc->data[SC_DEVOTION]->val1 )
			d_tbl = map_id2bl(sc->data[SC_DEVOTION]->val1);
		if( sc->data[SC__SHADOWFORM] && sc->data[SC__SHADOWFORM]->val2 )
			s_tsd = map_id2sd(sc->data[SC__SHADOWFORM]->val2);
		if( sc->data[SC_WATER_SCREEN_OPTION] && sc->data[SC_WATER_SCREEN_OPTION]->val1 )
			e_tbl = map_id2bl(sc->data[SC_WATER_SCREEN_OPTION]->val1);
	}

	if( ((d_tbl && check_distance_bl(target, d_tbl, sc->data[SC_DEVOTION]->val3)) ||
		(s_tsd && s_tsd->shadowform_id == target->id) || e_tbl) &&
		damage > 0 && skill_id != CR_REFLECTSHIELD && skill_id != PA_PRESSURE && !isvanishdamage )
		damage = 0;

	if( !battle_config.delay_battle_damage || amotion <= 1 ) { //Deal damage
		map_freeblock_lock();
		battle_damage(src, target, damage, ddelay, skill_lv, skill_id, dmg_lv, attack_type, additional_effects, gettick(), isspdamage);
		map_freeblock_unlock();
		return 0;
	}

	dat = ers_alloc(delay_damage_ers, struct delay_damage);
	dat->src_id = src->id;
	dat->target_id = target->id;
	dat->skill_id = skill_id;
	dat->skill_lv = skill_lv;
	dat->attack_type = attack_type;
	dat->damage = damage;
	dat->dmg_lv = dmg_lv;
	dat->delay = ddelay;
	dat->distance = distance_bl(src, target) + (battle_config.snap_dodge ? 10 : AREA_SIZE);
	dat->additional_effects = additional_effects;
	dat->src_type = src->type;
	dat->isspdamage = isspdamage;

	if( src->type != BL_PC && amotion > 1000 )
		amotion = 1000; //Aegis places a damage-delay cap of 1 sec to non player attacks [Skotlex]

	if( src->type == BL_PC )
		((TBL_PC *)src)->delayed_damage++;

	add_timer(tick + amotion, battle_delay_damage_sub, 0, (intptr_t)dat);

	return 0;
}

/**
 * Get attribute ratio
 * @param atk_elem Attack element enum e_element
 * @param def_type Defense element enum e_element
 * @param def_lv Element level 1 ~ MAX_ELE_LEVEL
 */
int battle_attr_ratio(int atk_elem, int def_type, int def_lv)
{
	if( !CHK_ELEMENT(atk_elem) || !CHK_ELEMENT(def_type) || !CHK_ELEMENT_LEVEL(def_lv) )
		return 100;

	return attr_fix_table[def_lv - 1][atk_elem][def_type];
}

/*==========================================
 * Does attribute fix modifiers.
 * Added passing of the chars so that the status changes can affect it. [Skotlex]
 * NOTE: Passing src/target == NULL is perfectly valid, it skips SC_ checks.
 *------------------------------------------*/
int64 battle_attr_fix(struct block_list *src, struct block_list *target, int64 damage, int atk_elem, int def_type, int def_lv)
{
	struct status_change *sc = NULL, *tsc = NULL;
	int ratio;

	if( src )
		sc = status_get_sc(src);

	if( target )
		tsc = status_get_sc(target);

	if( !CHK_ELEMENT(atk_elem) )
		atk_elem = rnd()%ELE_ALL;

	if( !CHK_ELEMENT(def_type) || !CHK_ELEMENT_LEVEL(def_lv) ) {
		ShowError("battle_attr_fix: unknown attr type: atk=%d def_type=%d def_lv=%d\n",atk_elem,def_type,def_lv);
		return damage;
	}

	ratio = attr_fix_table[def_lv - 1][atk_elem][def_type];

	if( sc && sc->count ) { //Increase damage by src status
		switch( atk_elem ) {
			case ELE_FIRE:
				if( sc->data[SC_VOLCANO] ) {
#ifdef RENEWAL
					ratio += sc->data[SC_VOLCANO]->val3;
#else
					damage += (int64)(damage * sc->data[SC_VOLCANO]->val3 / 100);
#endif
				}
				break;
			case ELE_WIND:
				if( sc->data[SC_VIOLENTGALE] ) {
#ifdef RENEWAL
					ratio += sc->data[SC_VIOLENTGALE]->val3;
#else
					damage += (int64)(damage * sc->data[SC_VIOLENTGALE]->val3 / 100);
#endif
				}
				break;
			case ELE_WATER:
				if( sc->data[SC_DELUGE] ) {
#ifdef RENEWAL
					ratio += sc->data[SC_DELUGE]->val3;
#else
					damage += (int64)(damage * sc->data[SC_DELUGE]->val3 / 100);
#endif
				}
				break;
		}
	}

	if( tsc && tsc->count ) { //Since an atk can only have one type let's optimise this a bit
		switch( atk_elem ) {
			case ELE_FIRE:
				if( tsc->data[SC_SPIDERWEB] ) { //Double damage
#ifdef RENEWAL
					ratio += 100;
#else
					damage *= 2;
#endif
					status_change_end(target,SC_SPIDERWEB,INVALID_TIMER);
				}
				if( tsc->data[SC_WIDEWEB] ) {
#ifdef RENEWAL
					ratio += 100;
#else
					damage *= 2;
#endif
					status_change_end(target,SC_WIDEWEB,INVALID_TIMER);
				}
				if( tsc->data[SC_THORNSTRAP] && battle_getcurrentskill(src) != GN_CARTCANNON )
					status_change_end(target,SC_THORNSTRAP,INVALID_TIMER);
				if( tsc->data[SC_CRYSTALIZE] )
					status_change_end(target,SC_CRYSTALIZE,INVALID_TIMER);
				if( tsc->data[SC_EARTH_INSIGNIA] ) {
#ifdef RENEWAL
					ratio += 50;
#else
					damage += (int64)(damage * 50 / 100);
#endif
				}
				if( tsc->data[SC_BURNT] ) {
#ifdef RENEWAL
					ratio += 400;
#else
					damage += (int64)(damage * 400 / 100);
#endif
				}
				break;
			case ELE_HOLY:
				if( tsc->data[SC_ORATIO] ) {
#ifdef RENEWAL
					ratio += tsc->data[SC_ORATIO]->val2;
#else
					damage += (int64)(damage * tsc->data[SC_ORATIO]->val2 / 100);
#endif
				}
				break;
			case ELE_POISON:
				if( tsc->data[SC_VENOMIMPRESS] ) {
#ifdef RENEWAL
					ratio += tsc->data[SC_VENOMIMPRESS]->val2;
#else
					damage += (int64)(damage * tsc->data[SC_VENOMIMPRESS]->val2 / 100);
#endif
				}
				break;
			case ELE_WIND:
				if( tsc->data[SC_CRYSTALIZE] ) {
					uint16 skill_id = battle_getcurrentskill(src);

					if( skill_get_type(skill_id) == BF_MAGIC ) {
#ifdef RENEWAL
						ratio += 50;
#else
						damage += (int64)(damage * 50 / 100);
#endif
					}
				}
				if( tsc->data[SC_WATER_INSIGNIA] ) {
#ifdef RENEWAL
					ratio += 50;
#else
					damage += (int64)(damage * 50 / 100);
#endif
				}
				break;
			case ELE_WATER:
				if( tsc->data[SC_FIRE_INSIGNIA] ) {
#ifdef RENEWAL
					ratio += 50;
#else
					damage += (int64)(damage * 50 / 100);
#endif
				}
				break;
			case ELE_EARTH:
				if( tsc->data[SC_WIND_INSIGNIA] ) {
#ifdef RENEWAL
					ratio += 50;
#else
					damage += (int64)(damage * 50 / 100);
#endif
				}
				if( tsc->data[SC_MAGNETICFIELD] ) //Freed if received earth damage
					status_change_end(target,SC_MAGNETICFIELD,INVALID_TIMER);
				break;
		}
	}

	if( !battle_config.attr_recover && ratio < 0 )
		ratio = 0;

#ifdef RENEWAL //In renewal, reductions are always rounded down so damage can never reach 0 unless ratio is 0
	damage = damage - (int64)(damage * (100 - ratio) / 100);
#else
	damage = (int64)(damage * ratio / 100);
#endif

	//Damage can be negative, see battle_config.attr_recover
	return damage;
}

/**
 * Calculates card bonuses damage adjustments.
 * @param attack_type @see enum e_battle_flag
 * @param src Attacker
 * @param target Target
 * @param nk Skill's nk @see enum e_skill_nk [NK_NO_CARDFIX_ATK|NK_NO_ELEFIX|NK_NO_CARDFIX_DEF]
 * @param rh_ele Right-hand weapon element
 * @param lh_ele Left-hand weapon element (BF_MAGIC and BF_MISC ignore this value)
 * @param damage Original damage
 * @param left Left hand flag (BF_MISC ignore flag value)
 *    For BF_MAGIC
 *         1: Calculates attacker bonuses.
 *         0: Calculates target bonuses
 *    For BF_WEAPON
 *         3: Calculates attacker bonuses in both hands.
 *         2: Calculates attacker bonuses in right-hand only.
 *         0 or 1: Only calculates target bonuses.
 * @param flag Misc value of skill & damage flags
 * @return damage Damage diff between original damage and after calculation
 */
int battle_calc_cardfix(int attack_type, struct block_list *src, struct block_list *target, int nk, int rh_ele, int lh_ele, int64 damage, int left, int flag) {
	struct map_session_data *sd, //Attacker session data if BL_PC
		*tsd; //Target session data if BL_PC
	short cardfix = 1000;
	int s_class, //Attacker class
		t_class; //Target class
	enum e_race2 s_race2, //Attacker Race2
		t_race2; //Target Race2
	enum e_element s_defele; //Attacker Element (not a weapon or skill element!)
	struct status_data *sstatus, //Attacker status data
		*tstatus; //Target status data
	int64 original_damage;
	uint16 skill_id;
	int i;

	if( !damage )
		return 0;

	original_damage = damage;

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);
	s_class = status_get_class(src);
	t_class = status_get_class(target);
	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(target);
	s_race2 = status_get_race2(src);
	t_race2 = status_get_race2(target);
	s_defele = (tsd ? (enum e_element)status_get_element(src) : ELE_NONE);
	skill_id = battle_getcurrentskill(src);

//Official servers apply the cardfix value on a base of 1000 and round down the reduction/increase
#define APPLY_CARDFIX(damage, fix) { (damage) = (damage) - (int64)((damage) * (1000 - (fix)) / 1000); }

	switch( attack_type ) {
		case BF_MAGIC:
			if( sd && !(nk&NK_NO_CARDFIX_ATK) && (left&1) ) { //Affected by attacker ATK bonuses
				cardfix = cardfix * (100 + sd->magic_addrace[tstatus->race] + sd->magic_addrace[RC_ALL]) / 100;
				if( !(nk&NK_NO_ELEFIX) ) { //Affected by element modifier bonuses
					cardfix = cardfix * (100 + sd->magic_adddefele[tstatus->def_ele] + sd->magic_adddefele[ELE_ALL]) / 100;
					cardfix = cardfix * (100 + sd->magic_atkele[rh_ele] + sd->magic_atkele[ELE_ALL]) / 100;
				}
				cardfix = cardfix * (100 + sd->magic_addsize[tstatus->size] + sd->magic_addsize[SZ_ALL]) / 100;
				cardfix = cardfix * (100 + sd->magic_addrace2[t_race2]) / 100;
				cardfix = cardfix * (100 + sd->magic_addclass[tstatus->class_] + sd->magic_addclass[CLASS_ALL]) / 100;
				for( i = 0; i < ARRAYLENGTH(sd->add_mdmg) && sd->add_mdmg[i].rate; i++ ) {
					if( sd->add_mdmg[i].class_ != t_class )
						continue;
					cardfix = cardfix * (100 + sd->add_mdmg[i].rate) / 100;
				}
				APPLY_CARDFIX(damage,cardfix);
			} else if( tsd && !(nk&NK_NO_CARDFIX_DEF) && !(left&1) ) { //Affected by target DEF bonuses
				cardfix = 1000; //Reset var for target
				if( !(nk&NK_NO_ELEFIX) ) { //Affected by element modifier bonuses
					int ele_fix = tsd->subele[rh_ele] + tsd->subele[ELE_ALL];

					for( i = 0; ARRAYLENGTH(tsd->subele2) > i && tsd->subele2[i].rate; i++ ) {
						if( tsd->subele2[i].ele != rh_ele )
							continue;
						if( !(((tsd->subele2[i].flag)&flag)&BF_WEAPONMASK &&
							((tsd->subele2[i].flag)&flag)&BF_RANGEMASK &&
							((tsd->subele2[i].flag)&flag)&BF_SKILLMASK) )
							continue;
						ele_fix += tsd->subele2[i].rate;
					}
					if( s_defele != ELE_NONE )
						ele_fix += tsd->subdefele[s_defele] + tsd->subdefele[ELE_ALL];
					cardfix = cardfix * (100 - ele_fix) / 100;
				}
				cardfix = cardfix * (100 - (tsd->subsize[sstatus->size] + tsd->subsize[SZ_ALL])) / 100;
				cardfix = cardfix * (100 - tsd->subrace2[s_race2]) / 100;
				cardfix = cardfix * (100 - (tsd->subrace[sstatus->race] + tsd->subrace[RC_ALL])) / 100;
				cardfix = cardfix * (100 - (tsd->subclass[sstatus->class_] + tsd->subclass[CLASS_ALL])) / 100;
				for( i = 0; i < ARRAYLENGTH(tsd->add_mdef) && tsd->add_mdef[i].rate; i++ ) {
					if( tsd->add_mdef[i].class_ != s_class )
						continue;
					cardfix = cardfix * (100 - tsd->add_mdef[i].rate) / 100;
				}
#ifndef RENEWAL //It was discovered that ranged defense also counts vs magic! [Skotlex]
				if( flag&BF_SHORT )
					cardfix = cardfix * (100 - tsd->bonus.near_attack_def_rate) / 100;
				else
					cardfix = cardfix * (100 - tsd->bonus.long_attack_def_rate) / 100;
#endif
				cardfix = cardfix * (100 - tsd->bonus.magic_def_rate) / 100;
				if( tsd->sc.data[SC_MDEF_RATE] )
					cardfix = cardfix * (100 - tsd->sc.data[SC_MDEF_RATE]->val1) / 100;
				APPLY_CARDFIX(damage,cardfix);
			}
			break;
		case BF_WEAPON:
			if( sd && !(nk&NK_NO_CARDFIX_ATK) && (left&2) ) { //Affected by attacker ATK bonuses
				short cardfix_ = 1000;

				if( sd->state.arrow_atk ) { //Ranged attack
					cardfix = cardfix * (100 + sd->right_weapon.addrace[tstatus->race] + sd->arrow_addrace[tstatus->race] +
						sd->right_weapon.addrace[RC_ALL] + sd->arrow_addrace[RC_ALL]) / 100;
					if( !(nk&NK_NO_ELEFIX) ) { //Affected by element modifier bonuses
						int ele_fix = sd->right_weapon.adddefele[tstatus->def_ele] + sd->arrow_adddefele[tstatus->def_ele] +
							sd->right_weapon.adddefele[ELE_ALL] + sd->arrow_adddefele[ELE_ALL];

						for( i = 0; ARRAYLENGTH(sd->right_weapon.adddefele2) > i && sd->right_weapon.adddefele2[i].rate; i++ ) {
							if( sd->right_weapon.adddefele2[i].ele != tstatus->def_ele )
								continue;
							if( !(((sd->right_weapon.adddefele2[i].flag)&flag)&BF_WEAPONMASK &&
								((sd->right_weapon.adddefele2[i].flag)&flag)&BF_RANGEMASK &&
								((sd->right_weapon.adddefele2[i].flag)&flag)&BF_SKILLMASK) )
								continue;
							ele_fix += sd->right_weapon.adddefele2[i].rate;
						}
						cardfix = cardfix * (100 + ele_fix) / 100;
					}
					cardfix = cardfix * (100 + sd->right_weapon.addsize[tstatus->size] + sd->arrow_addsize[tstatus->size] +
						sd->right_weapon.addsize[SZ_ALL] + sd->arrow_addsize[SZ_ALL]) / 100;
					cardfix = cardfix * (100 + sd->right_weapon.addrace2[t_race2]) / 100;
					cardfix = cardfix * (100 + sd->right_weapon.addclass[tstatus->class_] + sd->arrow_addclass[tstatus->class_] +
						sd->right_weapon.addclass[CLASS_ALL] + sd->arrow_addclass[CLASS_ALL]) / 100;
					for( i = 0; i < ARRAYLENGTH(sd->right_weapon.add_dmg) && sd->right_weapon.add_dmg[i].rate; i++ ) {
						if( sd->right_weapon.add_dmg[i].class_ != t_class )
							continue;
						cardfix = cardfix * (100 + sd->right_weapon.add_dmg[i].rate) / 100;
					}
				} else { //Melee attack
					uint16 lv;

					if( !battle_config.left_cardfix_to_right ) { //Calculates each right & left hand weapon bonuses separatedly
						//Right-handed weapon
						cardfix = cardfix * (100 + sd->right_weapon.addrace[tstatus->race] + sd->shield_addrace[tstatus->race] +
							sd->right_weapon.addrace[RC_ALL] + sd->shield_addrace[RC_ALL]) / 100;
						if( !(nk&NK_NO_ELEFIX) ) { //Affected by element modifier bonuses
							int ele_fix = sd->right_weapon.adddefele[tstatus->def_ele] + sd->shield_adddefele[tstatus->def_ele] +
								sd->right_weapon.adddefele[ELE_ALL] + sd->shield_adddefele[ELE_ALL];

							for( i = 0; ARRAYLENGTH(sd->right_weapon.adddefele2) > i && sd->right_weapon.adddefele2[i].rate; i++ ) {
								if( sd->right_weapon.adddefele2[i].ele != tstatus->def_ele )
									continue;
								if( !(((sd->right_weapon.adddefele2[i].flag)&flag)&BF_WEAPONMASK &&
									((sd->right_weapon.adddefele2[i].flag)&flag)&BF_RANGEMASK &&
									((sd->right_weapon.adddefele2[i].flag)&flag)&BF_SKILLMASK) )
									continue;
								ele_fix += sd->right_weapon.adddefele2[i].rate;
							}
							cardfix = cardfix * (100 + ele_fix) / 100;
						}
						cardfix = cardfix * (100 + sd->right_weapon.addsize[tstatus->size] + sd->shield_addsize[tstatus->size] +
							sd->right_weapon.addsize[SZ_ALL] + sd->shield_addsize[SZ_ALL]) / 100;
						cardfix = cardfix * (100 + sd->right_weapon.addrace2[t_race2]) / 100;
						cardfix = cardfix * (100 + sd->right_weapon.addclass[tstatus->class_] + sd->shield_addclass[tstatus->class_] +
							sd->right_weapon.addclass[CLASS_ALL] + sd->shield_addclass[CLASS_ALL]) / 100;
						for( i = 0; i < ARRAYLENGTH(sd->right_weapon.add_dmg) && sd->right_weapon.add_dmg[i].rate; i++ ) {
							if( sd->right_weapon.add_dmg[i].class_ != t_class )
								continue;
							cardfix = cardfix * (100 + sd->right_weapon.add_dmg[i].rate) / 100;
						}
						if( left&1 ) { //Left-handed weapon
							cardfix_ = cardfix_ * (100 + sd->left_weapon.addrace[tstatus->race] + sd->left_weapon.addrace[RC_ALL]) / 100;
							if( !(nk&NK_NO_ELEFIX) ) { //Affected by Element modifier bonuses
								int ele_fix_lh = sd->left_weapon.adddefele[tstatus->def_ele] + sd->left_weapon.adddefele[ELE_ALL];

								for( i = 0; ARRAYLENGTH(sd->left_weapon.adddefele2) > i && sd->left_weapon.adddefele2[i].rate; i++ ) {
									if( sd->left_weapon.adddefele2[i].ele != tstatus->def_ele )
										continue;
									if( !(((sd->left_weapon.adddefele2[i].flag)&flag)&BF_WEAPONMASK &&
										((sd->left_weapon.adddefele2[i].flag)&flag)&BF_RANGEMASK &&
										((sd->left_weapon.adddefele2[i].flag)&flag)&BF_SKILLMASK) )
										continue;
									ele_fix_lh += sd->left_weapon.adddefele2[i].rate;
								}
								cardfix_ = cardfix_ * (100 + ele_fix_lh) / 100;
							}
							cardfix_ = cardfix_ * (100 + sd->left_weapon.addsize[tstatus->size] + sd->left_weapon.addsize[SZ_ALL]) / 100;
							cardfix_ = cardfix_ * (100 + sd->left_weapon.addrace2[t_race2]) / 100;
							cardfix_ = cardfix_ * (100 + sd->left_weapon.addclass[tstatus->class_] + sd->left_weapon.addclass[CLASS_ALL]) / 100;
							for( i = 0; i < ARRAYLENGTH(sd->left_weapon.add_dmg) && sd->left_weapon.add_dmg[i].rate; i++ ) {
								if( sd->left_weapon.add_dmg[i].class_ != t_class )
									continue;
								cardfix_ = cardfix_ * (100 + sd->left_weapon.add_dmg[i].rate) / 100;
							}
						}
					} else { //Calculates right & left hand weapon as unity
						int add_dmg = 0;

						cardfix = cardfix * (100 + sd->right_weapon.addrace[tstatus->race] + sd->shield_addrace[tstatus->race] + sd->left_weapon.addrace[tstatus->race] +
							sd->right_weapon.addrace[RC_ALL] + sd->shield_addrace[RC_ALL] + sd->left_weapon.addrace[RC_ALL]) / 100;
						if( !(nk&NK_NO_ELEFIX) ) {
							int ele_fix = sd->right_weapon.adddefele[tstatus->def_ele] + sd->shield_adddefele[tstatus->def_ele] + sd->left_weapon.adddefele[tstatus->def_ele] +
								sd->right_weapon.adddefele[ELE_ALL] + sd->shield_adddefele[ELE_ALL] + sd->left_weapon.adddefele[ELE_ALL];

							for( i = 0; ARRAYLENGTH(sd->right_weapon.adddefele2) > i && sd->right_weapon.adddefele2[i].rate; i++ ) {
								if( sd->right_weapon.adddefele2[i].ele != tstatus->def_ele )
									continue;
								if( !(((sd->right_weapon.adddefele2[i].flag)&flag)&BF_WEAPONMASK &&
									((sd->right_weapon.adddefele2[i].flag)&flag)&BF_RANGEMASK &&
									((sd->right_weapon.adddefele2[i].flag)&flag)&BF_SKILLMASK) )
									continue;
								ele_fix += sd->right_weapon.adddefele2[i].rate;
							}
							for( i = 0; ARRAYLENGTH(sd->left_weapon.adddefele2) > i && sd->left_weapon.adddefele2[i].rate; i++ ) {
								if( sd->left_weapon.adddefele2[i].ele != tstatus->def_ele )
									continue;
								if( !(((sd->left_weapon.adddefele2[i].flag)&flag)&BF_WEAPONMASK &&
									((sd->left_weapon.adddefele2[i].flag)&flag)&BF_RANGEMASK &&
									((sd->left_weapon.adddefele2[i].flag)&flag)&BF_SKILLMASK) )
									continue;
								ele_fix += sd->left_weapon.adddefele2[i].rate;
							}
							cardfix = cardfix * (100 + ele_fix) / 100;
						}
						cardfix = cardfix * (100 + sd->right_weapon.addsize[tstatus->size] + sd->shield_addsize[tstatus->size] + sd->left_weapon.addsize[tstatus->size] +
							sd->right_weapon.addsize[SZ_ALL] + sd->shield_addsize[SZ_ALL] + sd->left_weapon.addsize[SZ_ALL]) / 100;
						cardfix = cardfix * (100 + sd->right_weapon.addrace2[t_race2] + sd->left_weapon.addrace2[t_race2]) / 100;
						cardfix = cardfix * (100 + sd->right_weapon.addclass[tstatus->class_] + sd->shield_addclass[tstatus->class_] + sd->left_weapon.addclass[tstatus->class_] +
							sd->right_weapon.addclass[CLASS_ALL] + sd->shield_addclass[CLASS_ALL] + sd->left_weapon.addclass[CLASS_ALL]) / 100;
						for( i = 0; i < ARRAYLENGTH(sd->right_weapon.add_dmg) && sd->right_weapon.add_dmg[i].rate; i++ ) {
							if( sd->right_weapon.add_dmg[i].class_ != t_class )
								continue;
							add_dmg += sd->right_weapon.add_dmg[i].rate;
						}
						for( i = 0; i < ARRAYLENGTH(sd->left_weapon.add_dmg) && sd->left_weapon.add_dmg[i].rate; i++ ) {
							if( sd->left_weapon.add_dmg[i].class_ != t_class )
								continue;
							add_dmg += sd->left_weapon.add_dmg[i].rate;
						}
						cardfix = cardfix * (100 + add_dmg) / 100;
					}
					//Adv. Katar Mastery functions similar to a +%ATK card on official [helvetica]
					if( (lv = pc_checkskill(sd,ASC_KATAR)) > 0 && sd->status.weapon == W_KATAR )
						cardfix = cardfix * (100 + (10 + 2 * lv)) / 100;
				}
#ifndef RENEWAL
				if( flag&BF_LONG && skill_id != SR_GATEOFHELL )
					cardfix = cardfix * (100 + sd->bonus.long_attack_atk_rate) / 100;
#endif
				if( left&1 ) {
					APPLY_CARDFIX(damage,cardfix_);
				} else {
					APPLY_CARDFIX(damage,cardfix);
				}
			} else if( tsd && !(nk&NK_NO_CARDFIX_DEF) && !(left&2) ) { //Affected by target DEF bonuses
				if( !(nk&NK_NO_ELEFIX) ) { //Affected by Element modifier bonuses
					int ele_fix = tsd->subele[rh_ele] + tsd->subele[ELE_ALL];

					for( i = 0; ARRAYLENGTH(tsd->subele2) > i && tsd->subele2[i].rate; i++ ) {
						if( tsd->subele2[i].ele != rh_ele )
							continue;
						if( !(((tsd->subele2[i].flag)&flag)&BF_WEAPONMASK &&
							((tsd->subele2[i].flag)&flag)&BF_RANGEMASK &&
							((tsd->subele2[i].flag)&flag)&BF_SKILLMASK) )
							continue;
						ele_fix += tsd->subele2[i].rate;
					}
					if( s_defele != ELE_NONE )
						ele_fix += tsd->subdefele[s_defele] + tsd->subdefele[ELE_ALL];
					cardfix = cardfix * (100 - ele_fix) / 100;
					if( left&1 && lh_ele != rh_ele ) {
						int ele_fix_lh = tsd->subele[lh_ele] + tsd->subele[ELE_ALL];

						for( i = 0; ARRAYLENGTH(tsd->subele2) > i && tsd->subele2[i].rate; i++ ) {
							if( tsd->subele2[i].ele != lh_ele )
								continue;
							if( !(((tsd->subele2[i].flag)&flag)&BF_WEAPONMASK &&
								((tsd->subele2[i].flag)&flag)&BF_RANGEMASK &&
								((tsd->subele2[i].flag)&flag)&BF_SKILLMASK) )
								continue;
							ele_fix_lh += tsd->subele2[i].rate;
						}
						cardfix = cardfix * (100 - ele_fix_lh) / 100;
					}
				}
				cardfix = cardfix * (100 - (tsd->subsize[sstatus->size] + tsd->subsize[SZ_ALL])) / 100;
				cardfix = cardfix * (100 - tsd->subrace2[s_race2]) / 100;
				cardfix = cardfix * (100 - (tsd->subrace[sstatus->race] + tsd->subrace[RC_ALL])) / 100;
				cardfix = cardfix * (100 - (tsd->subclass[sstatus->class_] + tsd->subclass[CLASS_ALL])) / 100;
				for( i = 0; i < ARRAYLENGTH(tsd->add_def) && tsd->add_def[i].rate;i++ ) {
					if( tsd->add_def[i].class_ != s_class )
						continue;
					cardfix = cardfix * (100 - tsd->add_def[i].rate) / 100;
				}
				if( flag&BF_SHORT )
					cardfix = cardfix * (100 - tsd->bonus.near_attack_def_rate) / 100;
				else if( skill_id != SR_GATEOFHELL )
					cardfix = cardfix * (100 - tsd->bonus.long_attack_def_rate) / 100;
				if( tsd->sc.data[SC_DEF_RATE] )
					cardfix = cardfix * (100 - tsd->sc.data[SC_DEF_RATE]->val1) / 100;
				APPLY_CARDFIX(damage,cardfix);
			}
			break;
		case BF_MISC:
			if( tsd && !(nk&NK_NO_CARDFIX_DEF) ) { //Affected by target DEF bonuses
				if( !(nk&NK_NO_ELEFIX) ) { //Affected by Element modifier bonuses
					int ele_fix = tsd->subele[rh_ele] + tsd->subele[ELE_ALL];

					for( i = 0; ARRAYLENGTH(tsd->subele2) > i && tsd->subele2[i].rate; i++ ) {
						if( tsd->subele2[i].ele != rh_ele )
							continue;
						if( !(((tsd->subele2[i].flag)&flag)&BF_WEAPONMASK &&
							((tsd->subele2[i].flag)&flag)&BF_RANGEMASK &&
							((tsd->subele2[i].flag)&flag)&BF_SKILLMASK))
							continue;
						ele_fix += tsd->subele2[i].rate;
					}
					if( s_defele != ELE_NONE )
						ele_fix += tsd->subdefele[s_defele] + tsd->subdefele[ELE_ALL];
					cardfix = cardfix * (100 - ele_fix) / 100;
				}
				cardfix = cardfix * (100 - (tsd->subsize[sstatus->size] + tsd->subsize[SZ_ALL])) / 100;
				cardfix = cardfix * (100 - tsd->subrace2[s_race2]) / 100;
				cardfix = cardfix * (100 - (tsd->subrace[sstatus->race] + tsd->subrace[RC_ALL])) / 100;
				cardfix = cardfix * (100 - (tsd->subclass[sstatus->class_] + tsd->subclass[CLASS_ALL])) / 100;
				cardfix = cardfix * (100 - tsd->bonus.misc_def_rate) / 100;
				if( flag&BF_SHORT )
					cardfix = cardfix * (100 - tsd->bonus.near_attack_def_rate) / 100;
				else
					cardfix = cardfix * (100 - tsd->bonus.long_attack_def_rate) / 100;
				APPLY_CARDFIX(damage,cardfix);
			}
			break;
	}

#undef APPLY_CARDFIX

	return (int)cap_value(damage - original_damage,INT_MIN,INT_MAX);
}

/**
 * Absorb damage based on criteria
 * @param bl
 * @param d Damage
 */
static void battle_absorb_damage(struct block_list *bl, struct Damage *d)
{
	int64 dmg_ori = 0, dmg_new = 0;

	nullpo_retv(bl);
	nullpo_retv(d);

	if( !d->damage && !d->damage2 )
		return;

	switch( bl->type ) {
		case BL_PC: {
				struct map_session_data *sd = BL_CAST(BL_PC,bl);

				if( !sd )
					return;
				if( sd->bonus.absorb_dmg_maxhp ) {
					int hp = sd->bonus.absorb_dmg_maxhp * status_get_max_hp(bl) / 100;

					dmg_ori = dmg_new = d->damage + d->damage2;
					if( dmg_ori > hp )
						dmg_new = dmg_ori - hp;
				}
			}
			break;
	}

	if( dmg_ori == dmg_new )
		return;

	if( !d->damage2 )
		d->damage = dmg_new;
	else if( !d->damage )
		d->damage2 = dmg_new;
	else {
		d->damage = dmg_new;
		d->damage2 = i64max(dmg_new * d->damage2 / dmg_ori / 100,1);
		d->damage = d->damage - d->damage2;
	}
}

/**
 * Check if bl is shadow forming someone
 * And shadow target have the specific status type
 * @param bl
 * @param type
 */
struct status_change_entry *battle_check_shadowform(struct block_list *bl, enum sc_type type) {
	struct status_change *sc = status_get_sc(bl);
	struct map_session_data *s_sd = NULL; //Shadow target

	//Check if shadow target have the status type [exneval]
	if( sc && sc->data[SC__SHADOWFORM] && (s_sd = map_id2sd(sc->data[SC__SHADOWFORM]->val2)) && s_sd->shadowform_id == bl->id ) {
		struct status_change *s_sc = NULL;

		if( (s_sc = &s_sd->sc) && s_sc->data[type] )
			return s_sc->data[type];
	}
	return NULL;
}

/**
 * Check if skill has NK_NO_CARDFIX_ATK property
 * @param skill_id
 */
bool battle_skill_check_no_cardfix_atk(uint16 skill_id) {
	switch( skill_id ) {
#ifdef RENEWAL
		case AS_SPLASHER:
		case ASC_BREAKER:
		case AM_DEMONSTRATION:
		case AM_ACIDTERROR:
		case WS_CARTTERMINATION:
		case CR_ACIDDEMONSTRATION:
		case GN_FIRE_EXPANSION_ACID:
		case GN_SPORE_EXPLOSION:
#endif
		case RA_CLUSTERBOMB:
			return true;
		case RK_DRAGONBREATH:
		case RK_DRAGONBREATH_WATER:
		case NC_SELFDESTRUCTION:
			return false;
	}
	return (skill_get_nk(skill_id)&NK_NO_CARDFIX_ATK);
}

/**
 * Check if skill damage reduced by SC_DEFENDER
 * @param skill_id
 */
static bool battle_skill_check_defender(uint16 skill_id) {
	switch( skill_id ) {
		case HT_BLITZBEAT:
		case SN_FALCONASSAULT:
		case AM_DEMONSTRATION:
		case AM_ACIDTERROR:
		case ASC_BREAKER:
#ifndef RENEWAL
		case CR_ACIDDEMONSTRATION:
		case GN_FIRE_EXPANSION_ACID:
#endif
		case NJ_ZENYNAGE:
		case KO_MUCHANAGE:
			return false;
	}
	return true;
}

static bool battle_skill_check_bypass_modifiers(struct block_list *src, struct block_list *bl, struct Damage *d, uint16 skill_id) {
	switch( skill_id ) {
		case NPC_MAXPAIN_ATK:
		case PA_PRESSURE:
		case HW_GRAVITATION:
			return true;
		case NPC_GRANDDARKNESS:
		case CR_GRANDCROSS:
			if( bl->id == src->id )
				return true;
		//Fall through
		default:
			if( d->isvanishdamage )
				return true;
			break;
	}
	return false;
}

int64 battle_calc_damage_sub(struct block_list *src, struct block_list *bl, struct Damage *d, int64 damage, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *tsd = NULL;
	struct status_change *sc = NULL;
	struct status_change_entry *sce = NULL;
	int flag = d->flag;

	nullpo_ret(bl);

	if( !damage )
		return 0;

	sc = status_get_sc(bl);
	tsd = BL_CAST(BL_PC,src);

	if( sc && sc->count ) {
		if( flag&BF_WEAPON ) {
			if( sc->data[SC_CRYSTALIZE] && tsd ) {
				switch( tsd->status.weapon ) {
					case W_MACE: case W_2HMACE: case W_1HAXE: case W_2HAXE:
						damage += damage * 150 / 100;
						break;
					case W_MUSICAL: case W_WHIP:
						if( !tsd->state.arrow_atk )
							break;
					//Fall through
					case W_BOW:     case W_REVOLVER: case W_RIFLE:  case W_GATLING:
					case W_SHOTGUN: case W_GRENADE:  case W_DAGGER: case W_1HSWORD:
					case W_2HSWORD:
						damage -= damage * 150 / 100;
						break;
				}
			}
#ifdef RENEWAL
			if( (sce = sc->data[SC_ARMORCHANGE]) )
				damage -= damage * sce->val2 / 100;
#else
			if( sc->data[SC_ENERGYCOAT] && !battle_skill_check_no_cardfix_atk(skill_id) ) {
				int per = 100 * status_get_sp(bl) / status_get_max_sp(bl) - 1;

				per /= 20;
				if( !status_charge(bl,0,(10 + 5 * per) * status_get_max_sp(bl) / 1000) )
					status_change_end(bl,SC_ENERGYCOAT,INVALID_TIMER);
				damage -= damage * 6 * (1 + per) / 100;
			}
#endif
		}

		if( (flag&(BF_SHORT|BF_WEAPON)) == (BF_SHORT|BF_WEAPON) ) {
			if( (sce = sc->data[SC_DARKCROW]) )
				damage += damage * sce->val2 / 100;
			if( sc->data[SC_SMOKEPOWDER] )
				damage -= damage * 15 / 100; //15% reduction to physical melee attacks
		}

		if( (flag&(BF_LONG|BF_WEAPON)) == (BF_LONG|BF_WEAPON) ) {
#ifndef RENEWAL
			if( sc->data[SC_ADJUSTMENT] )
				damage -= damage * 20 / 100;
#endif
			if( battle_skill_check_defender(skill_id) ) {
				if( ((sce = sc->data[SC_DEFENDER]) || (sce = battle_check_shadowform(bl,SC_DEFENDER))) )
					damage -= damage * sce->val2 / 100;
				if( sc->data[SC_SMOKEPOWDER] )
					damage -= damage * 50 / 100; //50% reduction to physical ranged attacks
			}
			if( sc->data[SC_FOGWALL] && !skill_id )
				damage -= damage * 75 / 100; //75% reduction
		}

#ifdef RENEWAL
		if( flag&BF_MAGIC && (sce = sc->data[SC_ARMORCHANGE]) )
			damage -= damage * sce->val3 / 100;
#endif
	}

	return damage;
}

int64 battle_calc_damage(struct block_list *src, struct block_list *bl, struct Damage *d, int64 damage, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct status_change *sc = NULL, *tsc = NULL;
	struct status_data *tstatus = NULL;
	struct status_change_entry *sce = NULL;
	int div_ = d->div_, flag = d->flag;

	nullpo_ret(bl);

	if( !damage )
		return 0;

	if( battle_config.ksprotection && mob_ksprotected(src,bl) )
		return 0;

	sd = BL_CAST(BL_PC,bl);
	sc = status_get_sc(bl);
	tsd = BL_CAST(BL_PC,src);
	tsc = status_get_sc(src);
	tstatus = status_get_status_data(src);

	if( sd ) {
		if( flag&BF_WEAPON && rnd()%100 < sd->special_state.no_weapon_damage )
			damage = 0;

		if( flag&BF_MAGIC && rnd()%100 < sd->special_state.no_magic_damage )
			damage = 0;

		if( flag&BF_MISC && rnd()%100 < sd->special_state.no_misc_damage )
			damage = 0;

		if( !damage )
			return 0;
	}

	if( sc && sc->data[SC_INVINCIBLE] && !sc->data[SC_INVINCIBLEOFF] )
		return 1;

	if( battle_skill_check_bypass_modifiers(src,bl,d,skill_id) )
		return damage;

	if( sc && sc->count ) {
		if( (sce = sc->data[SC_KYRIE]) ) {
			if( --(sce->val3) <= 0 )
				status_change_end(bl,SC_KYRIE,INVALID_TIMER);
			if( flag&BF_WEAPON ) {
				sce->val2 -= (int)cap_value(damage,INT_MIN,INT_MAX);
				if( sce->val2 <= 0 )
					damage = -sce->val2;
			}
			if( sce->val2 <= 0 )
				status_change_end(bl,SC_KYRIE,INVALID_TIMER);
			else
				return 0;
		}

		if( (sce = sc->data[SC_TUNAPARTY]) && flag&BF_WEAPON ) {
			sce->val2 -= (int)cap_value(damage,INT_MIN,INT_MAX);
			if( sce->val2 <= 0 ) {
				damage = -sce->val2;
				status_change_end(bl,SC_TUNAPARTY,INVALID_TIMER);
			} else
				return 0;
		}

		if( sc->data[SC_BASILICA] && !status_bl_has_mode(src,MD_STATUS_IMMUNE) ) {
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		//Gravitation and Pressure do damage without removing the effect
		if( sc->data[SC_WHITEIMPRISON] ) {
			if( (skill_id && skill_get_ele(skill_id,skill_lv) == ELE_GHOST) || (!skill_id && tstatus->rhw.ele == ELE_GHOST) )
				status_change_end(bl,SC_WHITEIMPRISON,INVALID_TIMER); //Those skills do damage and removes effect
			else {
				d->dmg_lv = ATK_BLOCK;
				return 0;
			}
		}

		if( (sce = sc->data[SC_SAFETYWALL]) && (flag&(BF_SHORT|BF_MAGIC)) == BF_SHORT ) {
			struct skill_unit_group *group = skill_id2group(sce->val3);

			if( group ) {
				d->dmg_lv = ATK_BLOCK;
				switch( sce->val2 ) {
					case MG_SAFETYWALL:
						if( --(group->val2) <= 0 ) {
							skill_delunitgroup(group);
							break;
						}
#ifdef RENEWAL
						group->val3 -= (int)cap_value(damage,INT_MIN,INT_MAX);
						if( group->val3 <= 0 )
							skill_delunitgroup(group);
#endif
						break;
					case MH_STEINWAND:
						group->val3 -= (int)cap_value(damage,INT_MIN,INT_MAX);
						if( --(group->val2) <= 0 || group->val3 <= 0 )
							skill_delunitgroup(group);
						break;
				}
				skill_unit_move(bl,gettick(),1); //For stacked units [exneval]
				return 0;
			}
			status_change_end(bl,SC_SAFETYWALL,INVALID_TIMER);
		}

		if( (sc->data[SC_PNEUMA] && (flag&(BF_LONG|BF_MAGIC)) == BF_LONG) || sc->data[SC__MANHOLE] || sc->data[SC_KINGS_GRACE] ) {
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		if( ((sce = sc->data[SC_AUTOGUARD]) || (sce = battle_check_shadowform(bl,SC_AUTOGUARD))) &&
			flag&BF_WEAPON && !battle_skill_check_no_cardfix_atk(skill_id) && rnd()%100 < sce->val2 ) {
			int delay;
			struct status_change_entry *sce_d = sc->data[SC_DEVOTION];
			struct status_change_entry *sce_s = sc->data[SC__SHADOWFORM];
			struct map_session_data *s_sd = NULL;
			struct block_list *d_bl = NULL;

			//Different delay depending on skill level [celest]
			if( sce->val1 <= 5 )
				delay = 300;
			else if( sce->val1 > 5 && sce->val1 <= 9 )
				delay = 200;
			else
				delay = 100;
			if( sd && pc_issit(sd) )
				pc_setstand(sd);
			if( sce_d && (d_bl = map_id2bl(sce_d->val1)) &&
				((d_bl->type == BL_MER && ((TBL_MER *)d_bl)->master && ((TBL_MER *)d_bl)->master->bl.id == bl->id) ||
				(d_bl->type == BL_PC && ((TBL_PC *)d_bl)->devotion[sce_d->val2] == bl->id)) &&
				check_distance_bl(bl,d_bl,sce_d->val3) )
			{ //If player is target of devotion, show guard effect on the devotion caster rather than the target
				clif_skill_nodamage(d_bl,d_bl,CR_AUTOGUARD,sce->val1,1);
				unit_set_walkdelay(d_bl,gettick(),delay,1);
				d->dmg_lv = ATK_MISS;
				return 0;
			} else if( sce_s && (s_sd = map_id2sd(sce_s->val2)) && s_sd->shadowform_id == bl->id ) {
				clif_skill_nodamage(&s_sd->bl,&s_sd->bl,CR_AUTOGUARD,sce->val1,1);
				unit_set_walkdelay(&s_sd->bl,gettick(),delay,1);
				d->dmg_lv = ATK_MISS;
				return 0;
			} else {
				clif_skill_nodamage(bl,bl,CR_AUTOGUARD,sce->val1,1);
				unit_set_walkdelay(bl,gettick(),delay,1);
				if( sc->data[SC_SHRINK] && rnd()%100 < 5 * sce->val1 )
					skill_blown(bl,src,skill_get_blewcount(CR_SHRINK,1),-1,0);
				d->dmg_lv = ATK_MISS;
				return 0;
			}
		}

		if( (sce = sc->data[SC_PARRYING]) && flag&BF_WEAPON && !battle_skill_check_no_cardfix_atk(skill_id) && rnd()%100 < sce->val2 ) {
			clif_skill_nodamage(bl,bl,LK_PARRYING,sce->val1,1);
			return 0; //Attack blocked by Parrying
		}

		if( (sce = sc->data[SC_WEAPONBLOCKING]) && (flag&(BF_SHORT|BF_WEAPON)) == (BF_SHORT|BF_WEAPON) &&
			!battle_skill_check_no_cardfix_atk(skill_id) && rnd()%100 < sce->val2 ) {
			clif_skill_nodamage(bl,bl,GC_WEAPONBLOCKING,sce->val1,
				sc_start2(src,bl,SC_COMBO,100,GC_WEAPONBLOCKING,src->id,2000));
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		if( (sce = sc->data[SC_DODGE]) && ((flag&BF_LONG) || sc->data[SC_STRUP]) && rnd()%100 < 20 ) {
			if( sd && pc_issit(sd) )
				pc_setstand(sd); //Stand it to dodge
			clif_skill_nodamage(bl,bl,TK_DODGE,sce->val1,
				sc_start4(src,bl,SC_COMBO,100,TK_JUMPKICK,src->id,1,0,2000));
			return 0;
		}

		if( flag&BF_MAGIC ) {
			if( sc->data[SC_HERMODE] )
				return 0;
			if( (sce = sc->data[SC_HALLUCINATIONWALK]) && rnd()%100 < sce->val3 )
				return 0;
			if( (sce = sc->data[SC_PRESTIGE]) && rnd()%100 < sce->val3 )
				return 0;
		}

		if( (sc->data[SC_TATAMIGAESHI] || sc->data[SC_NEUTRALBARRIER]) && (flag&(BF_LONG|BF_MAGIC)) == BF_LONG )
			return 0;

		//Kaupe blocks damage (skill or otherwise) from players, mobs, homuns, mercenaries
		if( (sce = sc->data[SC_KAUPE]) && rnd()%100 < sce->val2 ) {
			clif_specialeffect(bl,EF_STORMKICK4,AREA);
			if( --(sce->val3) <= 0 ) //We make it work like Safety Wall, even though it only blocks 1 time
				status_change_end(bl,SC_KAUPE,INVALID_TIMER);
			return 0;
		}

		if( ((sce = sc->data[SC_UTSUSEMI]) || sc->data[SC_BUNSINJYUTSU]) && flag&BF_WEAPON &&
			!battle_skill_check_no_cardfix_atk(skill_id) ) {
			skill_additional_effect(src,bl,skill_id,skill_lv,flag,ATK_BLOCK,gettick());
			if( !status_isdead(src) )
				skill_counter_additional_effect(src,bl,skill_id,skill_lv,flag,gettick());
			if( sce ) {
				clif_specialeffect(bl,EF_STORMKICK4,AREA);
				skill_blown(src,bl,sce->val3,-1,0);
				if( --(sce->val2) <= 0 )
					status_change_end(bl,SC_UTSUSEMI,INVALID_TIMER);
			}
			if( (sce = sc->data[SC_BUNSINJYUTSU]) && --(sce->val2) <= 0 )
				status_change_end(bl,SC_BUNSINJYUTSU,INVALID_TIMER);
			return 0;
		}

		if( sd && sd->shieldball > 0 ) {
			sd->shieldball_health -= (int)cap_value(damage,INT_MIN,INT_MAX);
			if( sd->shieldball_health <= 0 ) {
				pc_delshieldball(sd,1,0);
				status_change_start(src,bl,SC_STUN,10000,0,0,0,0,1000,SCFLAG_FIXEDTICK);
			}
			return 0;
		}

		if( (sce = sc->data[SC_LIGHTNINGWALK]) && (flag&(BF_LONG|BF_MAGIC)) == BF_LONG && rnd()%100 < sce->val2 ) {
			int dx[8] = { 0,-1,-1,-1,0,1,1,1 };
			int dy[8] = { 1,1,0,-1,-1,-1,0,1 };
			uint8 dir = map_calc_dir(bl,src->x,src->y);

			if( !map_flag_gvg2(src->m) && !map[src->m].flag.battleground && unit_movepos(bl,src->x - dx[dir],src->y - dy[dir],1,true) ) {
				clif_blown(bl,src);
				unit_setdir(bl,dir);
			}
			status_change_end(bl,SC_LIGHTNINGWALK,INVALID_TIMER);
			d->dmg_lv = ATK_DEF;
			return 0;
		}

		if( (sce = sc->data[SC_MEIKYOUSISUI]) && rnd()%100 < sce->val2 )
			return 0;

		if( sc->data[SC_ZEPHYR] && (((flag&(BF_SHORT|BF_MAGIC)) == BF_SHORT && skill_id) ||
			(flag&(BF_LONG|BF_MAGIC)) == BF_LONG || (flag&BF_MAGIC &&
			!(skill_get_inf(skill_id)&(INF_GROUND_SKILL|INF_SELF_SKILL)) && rnd()%100 < 70)) )
		{
			//Block all ranged attacks, all short-ranged skills
			//Block targeted magic skills with 70% success chance
			//Normal melee attacks and ground magic skills can still hit the player inside Zephyr
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		if( (sce = sc->data[SC_MAXPAIN]) ) {
			clif_skill_nodamage(bl,bl,NPC_MAXPAIN_ATK,sce->val1,1);
			battle_damage_temp[0] = damage;
			skill_castend_damage_id(bl,src,NPC_MAXPAIN_ATK,sce->val1,gettick(),0);
			d->dmg_lv = ATK_MISS;
			return 0;
		}

		if( (sce = sc->data[SC_ARMOR]) && sce->val3&flag && sce->val4&flag ) //NPC_DEFENDER
			damage -= damage * sce->val2 / 100;

#ifdef RENEWAL
		if( sc->data[SC_ENERGYCOAT] ) {
			int per = 100 * status_get_sp(bl) / status_get_max_sp(bl) - 1; //100% should be counted as the 80~99% interval

			per /= 20; //Uses 20% SP intervals
			//SP Cost: 1% + 0.5% per every 20% SP
			if( !status_charge(bl,0,(10 + 5 * per) * status_get_max_sp(bl) / 1000) )
				status_change_end(bl,SC_ENERGYCOAT,INVALID_TIMER);
			damage -= damage * 6 * (1 + per) / 100; //Reduction: 6% + 6% every 20%
		}

		if( sc->data[SC_STEELBODY] )
			damage -= damage * 90 / 100; //Renewal: Steel Body reduces all incoming damage to 1/10 [helvetica]
#else
		if( sc->data[SC_ASSUMPTIO] ) {
			if( map_flag_vs(bl->m) )
				damage -= damage * 34 / 100; //Receive 66% damage
			else
				damage -= damage * 50 / 100; //Receive 50% damage
		}
#endif

		if( sc->data[SC_FOGWALL] && skill_id && !(skill_get_inf(skill_id)&INF_GROUND_SKILL) &&
			!(skill_get_nk(skill_id)&NK_SPLASH) )
			damage -= damage * 25 / 100; //25% reduction

		if( (sce = sc->data[SC_GRANITIC_ARMOR]) )
			damage -= damage * sce->val2 / 100;

		if( (sce = sc->data[SC_PAIN_KILLER]) ) {
			int tmp_div = (skill_id ? skill_get_num(skill_id,skill_lv) : div_);

			damage -= (tmp_div < 0 ? sce->val3 : tmp_div * sce->val3);
			damage = i64max(damage,1);
		}

		if( sc->data[SC_WATER_BARRIER] )
			damage -= damage * 20 / 100; //20% reduction to all type attacks

		if( sc->data[SC_SU_STOOP] )
			damage -= damage * 90 / 100;

#ifdef RENEWAL
		if( (sce = sc->data[SC_RAID]) ) {
			damage += damage * 20 / 100;
			if( --(sce->val1) <= 0 )
				status_change_end(bl,SC_RAID,INVALID_TIMER);
		}
#endif

		if( sc->data[SC_DEEPSLEEP] )
			damage += damage * 150 / 100; //150% more damage while in Deep Sleep

		if( sc->data[SC_BURNT] && status_get_element(src) == ELE_FIRE )
			damage += damage * 666 / 100; //Custom value

		if( (sce = sc->data[SC_ANTI_M_BLAST]) && src->type == BL_PC )
			damage += damage * sce->val2 / 100;

		if( sc->data[SC_AETERNA] && skill_id != PF_SOULBURN ) {
			if( src->type != BL_MER || !skill_id )
				damage *= 2; //Lex Aeterna only doubles damage of basic attack from mercenaries
			status_change_end(bl,SC_AETERNA,INVALID_TIMER);
		}

		if( (sce = sc->data[SC_STONEHARDSKIN]) && (flag&(BF_SHORT|BF_WEAPON)) == (BF_SHORT|BF_WEAPON) ) {
			if( src->type == BL_MOB ) //Using explicit call instead break_equip for duration
				sc_start(src,src,SC_STRIPWEAPON,30,0,skill_get_time2(RK_STONEHARDSKIN,sce->val1));
			else
				skill_break_equip(src,src,EQP_WEAPON,3000,BCT_SELF);
		}

		if( sd ) {
			if( pc_ismadogear(sd) ) {
				int element = battle_get_weapon_element(d,src,bl,skill_id,skill_lv,EQI_HAND_R);

				pc_overheat(sd,(element == ELE_FIRE ? 3 : 1));
			}
			if( (sce = sc->data[SC_FORCEOFVANGUARD]) && rnd()%100 < sce->val2 )
				pc_addrageball(sd,skill_get_time(LG_FORCEOFVANGUARD,sce->val1),sce->val3);
			if( (sce = sc->data[SC_GT_ENERGYGAIN]) && rnd()%100 < sce->val2 )
				pc_addspiritball(sd,skill_get_time2(SR_GENTLETOUCH_ENERGYGAIN,sce->val1),pc_getmaxspiritball(sd,5));
		}

		if( (sce = sc->data[SC__DEADLYINFECT]) && (flag&(BF_SHORT|BF_MAGIC)) == BF_SHORT && rnd()%100 < 30 + 10 * sce->val1 )
			status_change_spread(bl,src,true); //Deadly infect attacked side

		if( (sce = sc->data[SC_STYLE_CHANGE]) && sce->val1 == MH_MD_GRAPPLING ) { //When being hit
			TBL_HOM *hd = BL_CAST(BL_HOM,bl);

			if( hd && rnd()%100 < status_get_lv(&hd->bl) / 2 )
				hom_addspiritball(hd,10);
		}

		if( (sce = sc->data[SC_MAGMA_FLOW]) && rnd()%100 < sce->val2 )
			skill_castend_nodamage_id(bl,bl,MH_MAGMA_FLOW,sce->val1,gettick(),flag|2);
	}

	//SC effects from caster's side
	if( tsc && tsc->count ) {
		if( tsc->data[SC_INVINCIBLE] && !tsc->data[SC_INVINCIBLEOFF] )
			damage += damage * 75 / 100;

		if( flag&BF_WEAPON ) {
			if( (sce = tsc->data[SC_POISONINGWEAPON]) && skill_id != GC_VENOMPRESSURE && rnd()%100 < sce->val3 )
				sc_start(src,bl,(sc_type)sce->val2,100,sce->val1,skill_get_time2(GC_POISONINGWEAPON,1));
			if( (sce = tsc->data[SC_SHIELDSPELL_REF]) && sce->val1 == 1 ) {
				skill_break_equip(src,bl,EQP_ARMOR,10000,BCT_ENEMY);
				status_change_end(src,SC_SHIELDSPELL_REF,INVALID_TIMER);
			}
			if( (sce = tsc->data[SC_GT_ENERGYGAIN]) && rnd()%100 < sce->val2 && tsd )
				pc_addspiritball(tsd,skill_get_time2(SR_GENTLETOUCH_ENERGYGAIN,sce->val1),pc_getmaxspiritball(tsd,5));
			if( (sce = tsc->data[SC_BLOODLUST]) && rnd()%100 < sce->val3 )
				status_heal(src,damage * sce->val4 / 100,0,3);
		}

		if( (sce = tsc->data[SC__DEADLYINFECT]) && (flag&(BF_SHORT|BF_MAGIC)) == BF_SHORT && rnd()%100 < 30 + 10 * sce->val1 )
			status_change_spread(src,bl,false);

		if( (sce = tsc->data[SC_STYLE_CHANGE]) && sce->val1 == MH_MD_FIGHTING ) { //When attacking
			TBL_HOM *hd = BL_CAST(BL_HOM,src);

			if( hd && rnd()%100 < 20 + status_get_lv(&hd->bl) / 5 )
				hom_addspiritball(hd,10);
		}
	}

	if( damage > 0 ) {
		if( battle_config.pk_mode && bl->type == BL_PC && src->type == BL_PC && map[bl->m].flag.pvp ) { //PK damage rates
			if( flag&BF_SKILL ) { //Skills get a different reduction than non-skills [Skotlex]
				if( flag&BF_WEAPON )
					damage = damage * battle_config.pk_weapon_damage_rate / 100;
				if( flag&BF_MAGIC )
					damage = damage * battle_config.pk_magic_damage_rate / 100;
				if( flag&BF_MISC )
					damage = damage * battle_config.pk_misc_damage_rate / 100;
			} else { //Normal attacks get reductions based on range
				if( flag&BF_SHORT )
					damage = damage * battle_config.pk_short_damage_rate / 100;
				if( flag&BF_LONG )
					damage = damage * battle_config.pk_long_damage_rate / 100;
			}
		}

		if( damage < div_ && battle_config.skill_min_damage &&
			((flag&BF_WEAPON && battle_config.skill_min_damage&1) ||
			(flag&BF_MAGIC && battle_config.skill_min_damage&2) ||
			(flag&BF_MISC && battle_config.skill_min_damage&4)) )
			damage = div_;
	}

	if( bl->type == BL_MOB && !status_isdead(bl) && bl->id != src->id ) {
		if( damage > 0 )
			mobskill_event((TBL_MOB *)bl,src,gettick(),flag);

		if( skill_id )
			mobskill_event((TBL_MOB *)bl,src,gettick(),MSC_SKILLUSED|(skill_id<<16));
	}

	return damage;
}

/*==========================================
 * Calculates BG related damage adjustments.
 *------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
int64 battle_calc_bg_damage(struct block_list *src, struct block_list *bl, int64 damage, uint16 skill_id, int flag)
{
	if( !damage )
		return 0;

	if( skill_get_inf2(skill_id)&INF2_NO_BG_GVG_DMG )
		return damage; //Skill that ignore bg map reduction

	if( flag&BF_SKILL ) { //Skills get a different reduction than non-skills [Skotlex]
		if( flag&BF_WEAPON )
			damage = damage * battle_config.bg_weapon_damage_rate / 100;
		if( flag&BF_MAGIC )
			damage = damage * battle_config.bg_magic_damage_rate / 100;
		if(	flag&BF_MISC )
			damage = damage * battle_config.bg_misc_damage_rate / 100;
	} else { //Normal attacks get reductions based on range.
		if( flag&BF_SHORT )
			damage = damage * battle_config.bg_short_damage_rate / 100;
		if( flag&BF_LONG )
			damage = damage * battle_config.bg_long_damage_rate / 100;
	}

	return damage;
}

bool battle_can_hit_gvg_target(struct block_list *src, struct block_list *bl, uint16 skill_id, int flag)
{
	struct mob_data *md = BL_CAST(BL_MOB, bl);
	struct unit_data *ud = unit_bl2ud(bl);
	short mob_id = ((TBL_MOB *)bl)->mob_id;

	if( ud && ud->immune_attack )
		return false;

	if( md && md->guardian_data ) {
		if( (status_bl_has_mode(bl, MD_SKILL_IMMUNE) || (mob_id == MOBID_EMPERIUM && !(skill_get_inf3(skill_id)&INF3_HIT_EMP))) && flag&BF_SKILL )
			return false; //Skill immunity
		if( src->type != BL_MOB || mob_is_clone(((struct mob_data *)src)->mob_id) ) {
			struct guild *g = (src->type == BL_PC ? ((TBL_PC *)src)->guild : guild_search(status_get_guild_id(src)));

			if( mob_id == MOBID_EMPERIUM && (!g || guild_checkskill(g,GD_APPROVAL) <= 0) )
				return false;
			if( g && battle_config.guild_max_castles && guild_checkcastles(g) >= battle_config.guild_max_castles )
				return false; //[MouseJstr]
		}
	}

	return true;
}

/*==========================================
 * Calculates GVG related damage adjustments.
 *------------------------------------------*/
int64 battle_calc_gvg_damage(struct block_list *src, struct block_list *bl, int64 damage, uint16 skill_id, int flag)
{
	if( !damage ) //No reductions to make
		return 0;

	if( !battle_can_hit_gvg_target(src,bl,skill_id,flag) )
		return 0;

	if( skill_get_inf2(skill_id)&INF2_NO_BG_GVG_DMG )
		return damage;

#ifndef RENEWAL
	//if( md && md->guardian_data ) //Uncomment if you want god-mode Emperiums at 100 defense [Kisuka]
		//damage -= damage * md->guardian_data->castle->defense / 100 * battle_config.castle_defense_rate / 100;
#endif

	if( flag&BF_SKILL ) { //Skills get a different reduction than non-skills [Skotlex]
		if( flag&BF_WEAPON )
			damage = damage * battle_config.gvg_weapon_damage_rate / 100;
		if( flag&BF_MAGIC )
			damage = damage * battle_config.gvg_magic_damage_rate / 100;
		if( flag&BF_MISC )
			damage = damage * battle_config.gvg_misc_damage_rate / 100;
	} else { //Normal attacks get reductions based on range
		if( flag&BF_SHORT )
			damage = damage * battle_config.gvg_short_damage_rate / 100;
		if( flag&BF_LONG )
			damage = damage * battle_config.gvg_long_damage_rate / 100;
	}

	return damage;
}

/*==========================================
 * HP/SP drain calculation
 *------------------------------------------*/
static int battle_calc_drain(int64 damage, int rate, int per)
{
	int64 diff = 0;

	if(per && (rate > 1000 || rnd()%1000 < rate)) {
		diff = damage * per / 100;
		if(!diff) {
			if(per > 0)
				diff = 1;
			else
				diff = -1;
		}
	}
	return (int)cap_value(diff,INT_MIN,INT_MAX);
}

/*==========================================
 * Passive skill damage increases
 *------------------------------------------*/
int64 battle_addmastery(struct map_session_data *sd, struct block_list *target, int64 dmg, int type)
{
	int64 damage;
	struct status_data *status = status_get_status_data(target);
	int weapon;
	uint16 lv;

#ifdef RENEWAL
	damage = 0;
#else
	damage = dmg;
#endif

	nullpo_ret(sd);

	if((lv = pc_checkskill(sd,AL_DEMONBANE)) > 0 &&
		target->type == BL_MOB && //This bonus doesn't work against players
		(battle_check_undead(status->race,status->def_ele) || status->race == RC_DEMON))
		damage += lv * (int)(3 + (sd->status.base_level + 1) * 0.05); //[orn]

	if((lv = pc_checkskill(sd,HT_BEASTBANE)) > 0 && (status->race == RC_BRUTE || status->race == RC_INSECT) ) {
		damage += lv * 4;
		if(sd->sc.data[SC_SPIRIT] && sd->sc.data[SC_SPIRIT]->val2 == SL_HUNTER)
			damage += sd->status.str;
	}

#ifdef RENEWAL
	if((lv = pc_checkskill(sd,BS_WEAPONRESEARCH)) > 0) //Weapon Research bonus applies to all weapons
		damage += lv * 2;
#endif

	if((lv = pc_checkskill(sd,RA_RANGERMAIN)) > 0 && (status->race == RC_BRUTE || status->race == RC_PLANT || status->race == RC_FISH))
		damage += lv * 5;

	if((lv = pc_checkskill(sd,NC_RESEARCHFE)) > 0 && (status->def_ele == ELE_FIRE || status->def_ele == ELE_EARTH))
		damage += lv * 10;

	damage += pc_checkskill(sd,NC_MADOLICENCE) * 15; //Attack bonus is granted even without the Madogear
	weapon = (!type ? sd->weapontype1 : sd->weapontype2);

	switch(weapon) {
		case W_1HSWORD:
#ifdef RENEWAL
			if((lv = pc_checkskill(sd,AM_AXEMASTERY)) > 0)
				damage += lv * 3;
#endif
		//Fall through
		case W_DAGGER:
			if((lv = pc_checkskill(sd,SM_SWORD)) > 0)
				damage += lv * 4;
			if((lv = pc_checkskill(sd,GN_TRAINING_SWORD)) > 0)
				damage += lv * 10;
			break;
		case W_2HSWORD:
			if((lv = pc_checkskill(sd,SM_TWOHAND)) > 0)
				damage += lv * 4;
			break;
		case W_1HSPEAR:
		case W_2HSPEAR:
			if((lv = pc_checkskill(sd,KN_SPEARMASTERY)) > 0) {
				if(!pc_isriding(sd) && !pc_isridingdragon(sd))
					damage += lv * 4;
				else
					damage += lv * 5;
				if(pc_checkskill(sd,RK_DRAGONTRAINING) > 0)
					damage += lv * 10; //Increase damage by level of KN_SPEARMASTERY * 10
			}
			break;
		case W_1HAXE:
		case W_2HAXE:
			if((lv = pc_checkskill(sd,AM_AXEMASTERY)) > 0)
				damage += lv * 3;
			if((lv = pc_checkskill(sd,NC_TRAININGAXE)) > 0)
				damage += lv * 5;
			break;
		case W_MACE:
		case W_2HMACE:
			if((lv = pc_checkskill(sd,PR_MACEMASTERY)) > 0)
				damage += lv * 3;
			if((lv = pc_checkskill(sd,NC_TRAININGAXE)) > 0)
				damage += lv * 4;
			break;
		case W_FIST:
			if((lv = pc_checkskill(sd,TK_RUN)) > 0)
				damage += lv * 10; //+ATK (Bare Handed)
		//Fall through
		case W_KNUCKLE:
			if((lv = pc_checkskill(sd,MO_IRONHAND)) > 0)
				damage += lv * 3;
			break;
		case W_MUSICAL:
			if((lv = pc_checkskill(sd,BA_MUSICALLESSON)) > 0)
				damage += lv * 3;
			break;
		case W_WHIP:
			if((lv = pc_checkskill(sd,DC_DANCINGLESSON)) > 0)
				damage += lv * 3;
			break;
		case W_BOOK:
			if((lv = pc_checkskill(sd,SA_ADVANCEDBOOK)) > 0)
				damage += lv * 3;
			break;
		case W_KATAR:
			if((lv = pc_checkskill(sd,AS_KATAR)) > 0)
				damage += lv * 3;
			break;
	}

	return damage;
}

/**
 * Calculates over refine damage bonus
 * @param sd Player
 * @param damage Current damage
 * @param type EQI_HAND_L:left-hand weapon, EQI_HAND_R:right-hand weapon
 */
static void battle_add_weapon_damage(struct map_session_data *sd, int64 *damage, uint8 type) {
	int overrefine = 0;

	if(!sd)
		return;
	switch(type) { //Rodatazone says that Over refine bonus is part of base weapon attack
		case EQI_HAND_L:
			overrefine = sd->left_weapon.overrefine + 1;
			break;
		case EQI_HAND_R:
			overrefine = sd->right_weapon.overrefine + 1;
			break;
	}
	(*damage) += rnd()%overrefine;
}

#ifdef RENEWAL
static int battle_calc_sizefix(int64 damage, struct map_session_data *sd, short t_size, unsigned char weapon_type, bool weapon_perfection)
{
	if(sd && !sd->special_state.no_sizefix && !weapon_perfection) //Size fix only for player
		damage = damage * (weapon_type == EQI_HAND_L ? sd->left_weapon.atkmods[t_size] : sd->right_weapon.atkmods[t_size]) / 100;
	return (int)cap_value(damage,INT_MIN,INT_MAX);
}

static int battle_calc_status_attack(struct status_data *status, short hand)
{
	if(hand == EQI_HAND_L)
		return status->batk; //Left-hand penalty on sATK is always 50% [Baalberith]
	else
		return status->batk * 2;
}

/**
 * Calculates renewal Variance, OverRefineBonus, and SizePenaltyMultiplier of weapon damage parts for player
 * @param src Block list of attacker
 * @param tstatus Target's status data
 * @param watk Weapon attack data
 * @param sd Player
 * @param skill_id Id of skill
 * @return Base weapon damage
 */
static int battle_calc_base_weapon_attack(struct block_list *src, struct status_data *tstatus, struct weapon_atk *watk, struct map_session_data *sd, uint16 skill_id)
{
	struct status_data *status = status_get_status_data(src);
	struct status_change *sc = status_get_sc(src);
	uint8 type = (watk == &status->lhw ? EQI_HAND_L : EQI_HAND_R);
	uint16 atkmin = (type == EQI_HAND_L ? status->watk2 : status->watk);
	uint16 atkmax = atkmin;
	int64 damage;
	bool weapon_perfection = false;
	short index = sd->equip_index[type];

	if(index >= 0 && sd->inventory_data[index]) {
		float strdex_bonus, variance;
		short flag = 0, dstr;

		switch(sd->status.weapon) {
			case W_BOW:	case W_MUSICAL:
			case W_WHIP:	case W_REVOLVER:
			case W_RIFLE:	case W_GATLING:
			case W_SHOTGUN:	case W_GRENADE:
				flag = 1;
				break;
		}
		if(flag)
			dstr = status->dex;
		else
			dstr = status->str;
		variance = 5.0f * watk->atk * watk->wlv / 100.0f;
		strdex_bonus = watk->atk * dstr / 200.0f;
		atkmin = u16max((uint16)(atkmin - variance + strdex_bonus),0);
		atkmax = u16min((uint16)(atkmax + variance + strdex_bonus),UINT16_MAX);
	}

	if(!(sc && sc->data[SC_MAXIMIZEPOWER])) {
		if(atkmax > atkmin)
			atkmax = atkmin + rnd()%(atkmax - atkmin + 1);
		else
			atkmax = atkmin;
	}

	damage = atkmax;

	if(sd->charmball_type == CHARM_TYPE_LAND && sd->charmball > 0)
		damage += damage * sd->charmball / 10;

	battle_add_weapon_damage(sd, &damage, type); //Over refine atk bonus is not affected by Maximize Power status

	switch(skill_id) { //Ignore size fix
		case MO_EXTREMITYFIST:
		case NJ_ISSEN:
		case CR_SHIELDBOOMERANG:
		case PA_SHIELDCHAIN:
			weapon_perfection = true;
			break;
		default:
			if(sc && sc->data[SC_WEAPONPERFECTION])
				weapon_perfection = true;
			break;
	}

	damage = battle_calc_sizefix(damage,sd,tstatus->size,type,weapon_perfection);
	return (int)cap_value(damage,INT_MIN,INT_MAX);
}
#endif

/*==========================================
 * Calculates the standard damage of a normal attack assuming it hits,
 * it calculates nothing extra fancy, is needed for magnum break's WATK_ELEMENT bonus. [Skotlex]
 * This applies to pre-renewal and non-sd in renewal
 *------------------------------------------
 * Pass damage2 as NULL to not calc it.
 * Flag values:
 * &1 : Critical hit
 * &2 : Arrow attack
 * &4 : Skill is Magic Crasher / Lif Change
 * &8 : Skip target size adjustment (Extremity Fist)
 * &16: Arrow attack but BOW, REVOLVER, RIFLE, SHOTGUN, GATLING or GRENADE type weapon not equipped (i.e. shuriken, kunai and venom knives not affected by DEX)
 *
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static int64 battle_calc_base_damage(struct block_list *src, struct status_data *status, struct weapon_atk *watk, struct status_change *sc, unsigned short t_size, struct map_session_data *sd, int flag)
{
	unsigned int atkmin = 0, atkmax = 0;
	uint8 type = 0;
	int64 damage = 0;

	if(!sd) { //Mobs/Pets
		if(flag&4) {
			atkmin = status->matk_min;
			atkmax = status->matk_max;
		} else {
			atkmin = watk->atk;
			atkmax = watk->atk2;
#ifdef RENEWAL
			if(src->type == BL_MOB) {
				atkmin = watk->atk * 80 / 100;
				atkmax = watk->atk * 120 / 100;
			}
#endif
		}
		if(atkmin > atkmax)
			atkmin = atkmax;
	} else { //PCs
		atkmax = watk->atk;
		type = (watk == &status->lhw ? EQI_HAND_L : EQI_HAND_R);
		if(!(flag&1) || (flag&2)) { //Normal attacks
			atkmin = status->dex;
			if(sd->equip_index[type] >= 0 && sd->inventory_data[sd->equip_index[type]])
				atkmin = atkmin * (80 + sd->inventory_data[sd->equip_index[type]]->wlv * 20) / 100;
			if(atkmin > atkmax)
				atkmin = atkmax;
			if(flag&2 && !(flag&16)) { //Bows
				atkmin = atkmin * atkmax / 100;
				if (atkmin > atkmax)
					atkmax = atkmin;
			}
		}
	}

	if(sc && sc->data[SC_MAXIMIZEPOWER])
		atkmin = atkmax;

	//Weapon Damage calculation
	if(!(flag&1))
		damage = (atkmax > atkmin ? rnd()%(atkmax - atkmin) : 0) + atkmin;
	else
		damage = atkmax;

	if(sd) {
		//Rodatazone says the range is 0~arrow_atk-1 for non crit
		if(flag&2 && sd->bonus.arrow_atk)
			damage += ((flag&1) ? sd->bonus.arrow_atk : rnd()%sd->bonus.arrow_atk);
		//Size fix only for player
		if(!(sd->special_state.no_sizefix || (flag&8)))
			damage = damage * (type == EQI_HAND_L ? sd->left_weapon.atkmods[t_size] : sd->right_weapon.atkmods[t_size]) / 100;
	} else {
		if(src->type == BL_ELEM) {
			struct status_change *e_sc = status_get_sc(src);
			int e_class = status_get_class(src);

			if(e_sc) {
				switch(e_class) {
					case ELEMENTALID_AGNI_S:
					case ELEMENTALID_AGNI_M:
					case ELEMENTALID_AGNI_L:
						if(e_sc->data[SC_FIRE_INSIGNIA] && e_sc->data[SC_FIRE_INSIGNIA]->val1 == 1)
							damage += damage * 20 / 100;
						break;
					case ELEMENTALID_AQUA_S:
					case ELEMENTALID_AQUA_M:
					case ELEMENTALID_AQUA_L:
						if(e_sc->data[SC_WATER_INSIGNIA] && e_sc->data[SC_WATER_INSIGNIA]->val1 == 1)
							damage += damage * 20 / 100;
						break;
					case ELEMENTALID_VENTUS_S:
					case ELEMENTALID_VENTUS_M:
					case ELEMENTALID_VENTUS_L:
						if(e_sc->data[SC_WIND_INSIGNIA] && e_sc->data[SC_WIND_INSIGNIA]->val1 == 1)
							damage += damage * 20 / 100;
						break;
					case ELEMENTALID_TERA_S:
					case ELEMENTALID_TERA_M:
					case ELEMENTALID_TERA_L:
						if(e_sc->data[SC_EARTH_INSIGNIA] && e_sc->data[SC_EARTH_INSIGNIA]->val1 == 1)
							damage += damage * 20 / 100;
						break;
				}
			}
		}
	}

	//Finally, add base attack
	if(flag&4)
		damage += status->matk_min;
	else
		damage += status->batk;

	battle_add_weapon_damage(sd, &damage, type);

	return damage;
}

/*==========================================
 * Consumes ammo for the given skill.
 *------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
void battle_consume_ammo(TBL_PC *sd, uint16 skill_id, uint16 skill_lv)
{
	int qty = 1;

	if(!battle_config.ammo_decrement)
		return;

	if(skill_id) {
		qty = skill_get_ammo_qty(skill_id,skill_lv);
		if(!qty)
			qty = 1;
	}

	if(sd->equip_index[EQI_AMMO] >= 0) //Qty check should have been done in skill_check_condition
		pc_delitem(sd,sd->equip_index[EQI_AMMO],qty,0,1,LOG_TYPE_CONSUME);

	sd->state.arrow_atk = 0;
}

static int battle_range_type(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	if((skill_get_inf2(skill_id)&INF2_TRAP) //Traps are always short range [Akinari],[Xynvaroth]
#ifdef RENEWAL
		|| (skill_id == CR_GRANDCROSS && target->id == src->id)
		|| skill_id == PA_SHIELDCHAIN
#endif
		|| skill_id == NJ_KIRIKAGE)
		return BF_SHORT;

	if(skill_id == RL_FIRE_RAIN)
		return BF_LONG;

	//When monsters use Arrow Shower or Bomb, it is always short range
	if(src->type == BL_MOB && (skill_id == AC_SHOWER || skill_id == AM_DEMONSTRATION))
		return BF_SHORT;

	//Skill range criteria
	if(battle_config.skillrange_by_distance && (src->type&battle_config.skillrange_by_distance)) {
		if(check_distance_bl(src, target, 3)) //Based on distance between src/target [Skotlex]
			return BF_SHORT;
		return BF_LONG;
	}

	if(skill_get_range2(src, skill_id, skill_lv, true) <= 3) //Based on used skill's range
		return BF_SHORT;
	return BF_LONG;
}

static inline int battle_adjust_skill_damage(int m, unsigned short skill_id)
{
	if(map[m].skill_count) {
		int i;

		skill_id = skill_dummy2skill_id(skill_id);
		ARR_FIND(0, map[m].skill_count, i, map[m].skills[i]->skill_id == skill_id);
		if(i < map[m].skill_count)
			return map[m].skills[i]->modifier;
	}
	return 0;
}

static int battle_blewcount_bonus(struct map_session_data *sd, uint16 skill_id)
{
	uint8 i;

	if(!sd->skillblown[0].id)
		return 0;

	//Apply the bonus blewcount [Skotlex]
	for(i = 0; i < ARRAYLENGTH(sd->skillblown) && sd->skillblown[i].id; i++)
		if(sd->skillblown[i].id == skill_id)
			return sd->skillblown[i].val;
	return 0;
}

#ifdef ADJUST_SKILL_DAMAGE
/**
 * Damage calculation for adjusting skill damage
 * @param caster Applied caster type for damage skill
 * @param type BL_Type of attacker
 */
static bool battle_skill_damage_iscaster(uint8 caster, enum bl_type src_type)
{
	if(!caster)
		return false;

	switch(src_type) {
		case BL_PC: if(caster&SDC_PC) return true; break;
		case BL_MOB: if(caster&SDC_MOB) return true; break;
		case BL_PET: if(caster&SDC_PET) return true; break;
		case BL_HOM: if(caster&SDC_HOM) return true; break;
		case BL_MER: if(caster&SDC_MER) return true; break;
		case BL_ELEM: if(caster&SDC_ELEM) return true; break;
	}
	return false;
}

/**
 * Gets skill damage rate from a skill (based on skill_damage_db.txt)
 * @param src
 * @param target
 * @param skill_id
 * @return Skill damage rate
 */
static int battle_skill_damage_skill(struct block_list *src, struct block_list *target, uint16 skill_id)
{
	uint16 idx = skill_get_index(skill_id), m = src->m;
	struct s_skill_damage *damage = NULL;
	struct map_data *mapd = &map[m];

	if(!idx || !skill_db[idx].damage.map)
		return 0;

	damage = &skill_db[idx].damage;

	//Check the adjustment works for specified type
	if(!battle_skill_damage_iscaster(damage->caster, src->type))
		return 0;

	if((damage->map&1 && (!mapd->flag.pvp && !map_flag_gvg2(m) && !mapd->flag.battleground && !mapd->flag.skill_damage && !mapd->flag.restricted)) ||
		(damage->map&2 && mapd->flag.pvp) ||
		(damage->map&4 && map_flag_gvg2(m)) ||
		(damage->map&8 && mapd->flag.battleground) ||
		(damage->map&16 && mapd->flag.skill_damage) ||
		(mapd->flag.restricted && damage->map&(8 * mapd->zone)))
	{
		switch(target->type) {
			case BL_PC:
				return damage->pc;
			case BL_MOB:
				if(status_get_class_(target) == CLASS_BOSS)
					return damage->boss;
				else
					return damage->mob;
			default:
				return damage->other;
		}
	}
	return 0;
}

/**
 * Gets skill damage rate from a skill (based on 'skill_damage' mapflag)
 * @param src
 * @param target
 * @param skill_id
 * @return Skill damage rate
 */
static int battle_skill_damage_map(struct block_list *src, struct block_list *target, uint16 skill_id)
{
	int rate = 0;
	uint8 i = 0;
	struct map_data *mapd = &map[src->m];

	if(!mapd || !mapd->flag.skill_damage)
		return 0;

	//Damage rate for all skills at this map
	if(battle_skill_damage_iscaster(mapd->adjust.damage.caster, src->type)) {
		switch(target->type) {
			case BL_PC:
				rate = mapd->adjust.damage.pc;
				break;
			case BL_MOB:
				if(status_get_class_(target) == CLASS_BOSS)
					rate = mapd->adjust.damage.boss;
				else
					rate = mapd->adjust.damage.mob;
				break;
			default:
				rate = mapd->adjust.damage.other;
				break;
		}
	}

	if(!mapd->skill_damage.count)
		return rate;

	//Damage rate for specified skill at this map
	for(i = 0; i < mapd->skill_damage.count; i++) {
		if(mapd->skill_damage.entries[i]->skill_id == skill_id &&
			battle_skill_damage_iscaster(mapd->skill_damage.entries[i]->caster, src->type)) {
			switch(target->type) {
				case BL_PC:
					rate += mapd->skill_damage.entries[i]->pc;
					break;
				case BL_MOB:
					if(status_get_class_(target) == CLASS_BOSS)
						rate += mapd->skill_damage.entries[i]->boss;
					else
						rate += mapd->skill_damage.entries[i]->mob;
					break;
				default:
					rate += mapd->skill_damage.entries[i]->other;
					break;
			}
		}
	}
	return rate;
}

/**
 * Check skill damage adjustment based on mapflags and skill_damage_db.txt for specified skill
 * @param src
 * @param target
 * @param skill_id
 * @return Total damage rate
 */
static int battle_skill_damage(struct block_list *src, struct block_list *target, uint16 skill_id)
{
	nullpo_ret(src);

	if(!target || !skill_id)
		return 0;
	skill_id = skill_dummy2skill_id(skill_id);
	return battle_skill_damage_skill(src, target, skill_id) + battle_skill_damage_map(src, target, skill_id);
}
#endif

struct Damage battle_calc_magic_attack(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int mflag);
struct Damage battle_calc_misc_attack(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int mflag);

/*=======================================================
 * Should infinite defense be applied on target? (plant)
 *-------------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 *	flag - see e_battle_flag
 */
bool is_infinite_defense(struct block_list *target, int flag)
{
	struct status_data *tstatus = status_get_status_data(target);

	if(target->type == BL_SKILL) {
		TBL_SKILL *su = ((TBL_SKILL *)target);

		if(su && su->group && su->group->unit_id == UNT_REVERBERATION)
			return true;
	}
	if(status_has_mode(tstatus, MD_IGNOREMELEE) && (flag&(BF_SHORT|BF_WEAPON)) == (BF_SHORT|BF_WEAPON))
		return true;
	if(status_has_mode(tstatus, MD_IGNORERANGED) && (flag&(BF_LONG|BF_MAGIC)) == BF_LONG)
		return true;
	if(status_has_mode(tstatus, MD_IGNOREMAGIC) && (flag&BF_MAGIC))
		return true;
	if(status_has_mode(tstatus, MD_IGNOREMISC) && (flag&BF_MISC))
		return true;
	return false;
}

/*========================
 * Is attack arrow based?
 *------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static bool is_skill_using_arrow(struct block_list *src, uint16 skill_id)
{
	if(src) {
		struct status_data *sstatus = status_get_status_data(src);
		struct map_session_data *sd = BL_CAST(BL_PC, src);

		return ((sd && sd->state.arrow_atk) ||
			(!sd && ((skill_id && skill_get_ammotype(skill_id)) || sstatus->rhw.range > 3)) ||
			skill_id == HT_PHANTASMIC || skill_id == GS_GROUNDDRIFT);
	} else
		return false;
}

/*=========================================
 * Is attack right handed? Default: Yes
 *-----------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static bool is_attack_right_handed(struct block_list *src, uint16 skill_id)
{
	if(src) {
		struct map_session_data *sd = BL_CAST(BL_PC, src);

		//Skills ALWAYS use ONLY your right-hand weapon (tested on Aegis 10.2)
		if(!skill_id && sd && !sd->weapontype1 && sd->weapontype2 > 0)
			return false;
	}
	return true;
}

/*=======================================
 * Is attack left handed? Default: No
 *---------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static bool is_attack_left_handed(struct block_list *src, uint16 skill_id)
{
	if(src) {
		struct status_data *sstatus = status_get_status_data(src);
		struct map_session_data *sd = BL_CAST(BL_PC, src);

		if(!skill_id) { //Skills ALWAYS use ONLY your right-hand weapon (tested on Aegis 10.2)
			if(sd) {
				if(!sd->weapontype1 && sd->weapontype2 > 0)
					return true;
				if(sd->status.weapon == W_KATAR)
					return true;
			}
			if(sstatus->lhw.atk)
				return true;
		}
	}
	return false;
}

/*=============================
 * Do we score a critical hit?
 *-----------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static bool is_attack_critical(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, bool first_call)
{
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, target);
	bool allow_cri = false;

	if(!first_call)
		return (wd.type == DMG_CRITICAL || wd.type == DMG_MULTI_HIT_CRITICAL);

	switch(skill_id) {
		case 0:
			if(sc && sc->data[SC_SPELLFIST] && !(wd.miscflag&16))
				return false;
			if(battle_config.enable_critical_multihit || !(wd.type&DMG_MULTI_HIT))
				allow_cri = true;
			break;
		case NPC_CRITICALSLASH:
		case LG_PINPOINTATTACK:
			return true; //Always critical
		case KN_AUTOCOUNTER:
		case SN_SHARPSHOOTING:
		case MA_SHARPSHOOTING:
		case NJ_KIRIKAGE:
			allow_cri = true;
			break;
		case MO_TRIPLEATTACK:
			if(battle_config.enable_critical_multihit)
				allow_cri = true;
			break;
	}

	if(allow_cri && sstatus->cri) {
		short cri = sstatus->cri;

		if(sd) {
			if(!battle_config.show_status_katar_crit && sd->status.weapon == W_KATAR)
				cri *= 2; //On official double critical bonus from katar won't be showed in status display
			cri += sd->critaddrace[tstatus->race] + sd->critaddrace[RC_ALL];
			if(is_skill_using_arrow(src, skill_id))
				cri += sd->bonus.arrow_cri;
			if(!skill_id && (wd.flag&BF_LONG))
				cri += sd->bonus.critical_long;
		}

		if(sc && sc->data[SC_CAMOUFLAGE])
			cri += 100 * min(10, sc->data[SC_CAMOUFLAGE]->val3); //Max 100% (1K)

		//The official equation is * 2, but that only applies when sd's do critical
		//Therefore, we use the old value 3 on cases when an sd gets attacked by a mob
		cri -= tstatus->luk * (!sd && tsd ? 3 : 2);

		if(tsc && tsc->data[SC_SLEEP])
			cri *= 2;

		switch(skill_id) {
			case 0:
				if(!(sc && sc->data[SC_AUTOCOUNTER]))
					break;
				clif_specialeffect(src, EF_AUTOCOUNTER, AREA);
				status_change_end(src, SC_AUTOCOUNTER, INVALID_TIMER);
			//Fall through
			case KN_AUTOCOUNTER:
				if(battle_config.auto_counter_type && (battle_config.auto_counter_type&src->type))
					return true;
				else
					cri *= 2;
				break;
			case SN_SHARPSHOOTING:
			case MA_SHARPSHOOTING:
				cri += 200;
				break;
			case NJ_KIRIKAGE:
				cri += 250 + 50 * skill_lv;
				break;
		}

		if(tsd && tsd->bonus.critical_def)
			cri = cri * (100 - tsd->bonus.critical_def) / 100;
		return (rnd()%1000 < cri);
	}
	return false;
}

/*==========================================================
 * Is the attack piercing? (Investigate/Ice Pick in pre-re)
 *----------------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static bool is_attack_piercing(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, short weapon_position)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_data *tstatus = status_get_status_data(target);

	switch(skill_id) {
		case NPC_GRANDDARKNESS:
		case CR_GRANDCROSS:
		case CR_SHIELDBOOMERANG:
#ifdef RENEWAL //Renewal Soul Breaker no longer gains ice pick effect [helvetica]
		case ASC_BREAKER:
#endif
		case PA_SACRIFICE:
		case PA_SHIELDCHAIN:
		case RK_DRAGONBREATH:
		case RK_DRAGONBREATH_WATER:
		case NC_SELFDESTRUCTION:
			return false;
		case MO_INVESTIGATE:
		case RL_MASS_SPIRAL:
			return true;
	}

#ifndef RENEWAL //Renewal critical gains ice pick effect [helvetica]
	if(is_attack_critical(wd, src, target, skill_id, skill_lv, false))
		return false;
#endif

	if(sd) { //Elemental/Racial adjustments
		if((sd->right_weapon.def_ratio_atk_ele&(1<<tstatus->def_ele)) || (sd->right_weapon.def_ratio_atk_ele&(1<<ELE_ALL)) ||
			(sd->right_weapon.def_ratio_atk_race&(1<<tstatus->race)) || (sd->right_weapon.def_ratio_atk_race&(1<<RC_ALL)) ||
			(sd->right_weapon.def_ratio_atk_class&(1<<tstatus->class_)) || (sd->right_weapon.def_ratio_atk_class&(1<<CLASS_ALL)))
			if(weapon_position == EQI_HAND_R)
				return true;
		if((sd->left_weapon.def_ratio_atk_ele&(1<<tstatus->def_ele)) || (sd->left_weapon.def_ratio_atk_ele&(1<<ELE_ALL)) ||
			(sd->left_weapon.def_ratio_atk_race&(1<<tstatus->race)) || (sd->left_weapon.def_ratio_atk_race&(1<<RC_ALL)) ||
			(sd->left_weapon.def_ratio_atk_class&(1<<tstatus->class_)) || (sd->left_weapon.def_ratio_atk_class&(1<<CLASS_ALL)))
		{ //Pass effect onto right hand if configured so [Skotlex]
			if(battle_config.left_cardfix_to_right && is_attack_right_handed(src, skill_id)) {
				if(weapon_position == EQI_HAND_R)
					return true;
			} else if(weapon_position == EQI_HAND_L)
				return true;
		}
	}
	return false;
}

static int battle_skill_get_damage_properties(uint16 skill_id, int is_splash)
{
	int nk = skill_get_nk(skill_id);

	if(!skill_id && is_splash) //If flag, this is splash damage from Baphomet Card and it always hits
		nk |= NK_NO_CARDFIX_ATK|NK_IGNORE_FLEE;
	return nk;
}

/*=============================
 * Checks if attack is hitting
 *-----------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static bool is_attack_hitting(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, bool first_call)
{
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	int nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);
	short flee, hitrate;
	uint8 lv = 0;

	if(!first_call)
		return (wd.dmg_lv != ATK_FLEE);

	if(is_attack_critical(wd, src, target, skill_id, skill_lv, false))
		return true;
	else if(sd && sd->bonus.perfect_hit > 0 && rnd()%100 < sd->bonus.perfect_hit)
		return true;
	else if(sc && (sc->data[SC_FUSION] || (sc->data[SC_SPELLFIST] && !skill_id && !(wd.miscflag&16))))
		return true;
	else if((skill_id == AS_SPLASHER || skill_id == GN_SPORE_EXPLOSION) && !(wd.miscflag&16))
		return true; //Always hits the one exploding
	else if(skill_id == CR_SHIELDBOOMERANG && sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_CRUSADER)
		return true;
	else if(tsc && tsc->opt1 && tsc->opt1 != OPT1_STONEWAIT && tsc->opt1 != OPT1_BURNING)
		return true;
	else if(nk&NK_IGNORE_FLEE)
		return true;

	if(sc && sc->data[SC_NEUTRALBARRIER] && skill_id && (wd.flag&(BF_LONG|BF_MAGIC)) == BF_LONG)
		return false;

	flee = tstatus->flee;
#ifdef RENEWAL
	hitrate = 0; //Default hitrate
#else
	hitrate = 80; //Default hitrate
#endif

	if(battle_config.agi_penalty_type && battle_config.agi_penalty_target&target->type) {
		unsigned char attacker_count = unit_counttargeted(target); //256 max targets should be a sane max

		if(attacker_count >= battle_config.agi_penalty_count) {
			if(battle_config.agi_penalty_type == 1)
				flee = (flee * (100 - (attacker_count - (battle_config.agi_penalty_count - 1)) * battle_config.agi_penalty_num)) / 100;
			else //Assume type 2 : absolute reduction
				flee -= (attacker_count - (battle_config.agi_penalty_count - 1)) * battle_config.agi_penalty_num;
			flee = max(flee, 1);
		}
	}

	hitrate += sstatus->hit - flee;

	if(tsc && tsc->data[SC_FOGWALL] && !skill_id && (wd.flag&BF_LONG))
		hitrate -= 50; //Fogwall's hit penalty is only for normal ranged attacks

	if(sd) {
#ifdef RENEWAL
		if((lv = pc_checkskill(sd, AC_VULTURE)) > 0)
			hitrate += lv;
#endif
		if((lv = pc_checkskill(sd, TF_DOUBLE)) > 0 && !skill_id && (wd.type&DMG_MULTI_HIT) && wd.div_ == 2)
			hitrate += lv; //+1 hit per level of Double Attack on a successful normal double attack [helvetica]
		if((lv = pc_checkskill(sd, GN_TRAINING_SWORD)) > 0 && (sd->weapontype1 == W_1HSWORD || sd->weapontype1 == W_DAGGER))
			hitrate += 3 * lv;
		if(is_skill_using_arrow(src, skill_id))
			hitrate += sd->bonus.arrow_hit;
	}

	switch(skill_id) { //Skills hit modifier
		case SM_BASH:
		case MS_BASH:
			hitrate += hitrate * 5 * skill_lv / 100;
			break;
		case MS_MAGNUM:
		case SM_MAGNUM:
			hitrate += hitrate * 10 * skill_lv / 100;
			break;
		case MC_CARTREVOLUTION:
		case GN_CART_TORNADO:
		case GN_CARTCANNON:
			if(sd && (lv = pc_checkskill(sd, GN_REMODELING_CART)) > 0)
				hitrate += 4 * lv;
			break;
		case KN_AUTOCOUNTER:
		case PA_SHIELDCHAIN:
		case NPC_WATERATTACK:
		case NPC_GROUNDATTACK:
		case NPC_FIREATTACK:
		case NPC_WINDATTACK:
		case NPC_POISONATTACK:
		case NPC_HOLYATTACK:
		case NPC_DARKNESSATTACK:
		case NPC_UNDEADATTACK:
		case NPC_TELEKINESISATTACK:
		case NPC_BLEEDING:
			hitrate += hitrate * 20 / 100;
			break;
		case NPC_EARTHQUAKE:
		case NPC_FIREBREATH:
		case NPC_ICEBREATH:
		case NPC_THUNDERBREATH:
		case NPC_ACIDBREATH:
		case NPC_DARKNESSBREATH:
			hitrate *= 2;
			break;
		case KN_PIERCE:
		case ML_PIERCE:
			hitrate += hitrate * 5 * skill_lv / 100;
			break;
		case AS_SONICBLOW:
			if(sd && pc_checkskill(sd, AS_SONICACCEL) > 0)
				hitrate += hitrate * 50 / 100;
			break;
		case GC_VENOMPRESSURE:
			hitrate += 10 + 4 * skill_lv;
			break;
		case SC_FATALMENACE:
			hitrate -= 35 - 5 * skill_lv;
			break;
		case LG_BANISHINGPOINT:
			hitrate += 3 * skill_lv;
			break;
		case RL_SLUGSHOT: {
				int dist = distance_bl(src, target);

				if(dist > 3)
					hitrate -= (dist - 3) * (hitrate * (11 - skill_lv) / 100);
			}
			break;
	}

#ifdef RENEWAL
	if(sd && (lv = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)
		hitrate += hitrate * 2 * lv / 100;
#endif

	if(sc && sc->data[SC_INCHITRATE])
		hitrate += hitrate * sc->data[SC_INCHITRATE]->val1 / 100;

	hitrate = cap_value(hitrate, battle_config.min_hitrate, battle_config.max_hitrate);
	return (rnd()%100 < hitrate);
}

/*==========================================
 * If attack ignores def.
 *------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static bool attack_ignores_def(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, short weapon_position)
{
	struct status_data *tstatus = status_get_status_data(target);
	struct status_change *sc = status_get_sc(src);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	int nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);

	switch(skill_id) {
		case NPC_GRANDDARKNESS:
		case CR_GRANDCROSS:
			return (nk&NK_IGNORE_DEF);
	}

#ifndef RENEWAL //Renewal critical doesn't ignore defense reduction
	if(is_attack_critical(wd, src, target, skill_id, skill_lv, false))
		return true;
#endif

	if(sc && sc->data[SC_FUSION])
		return true;

	if(sd) { //Ignore Defense
		if((sd->right_weapon.ignore_def_ele&(1<<tstatus->def_ele)) || (sd->right_weapon.ignore_def_ele&(1<<ELE_ALL)) ||
			(sd->right_weapon.ignore_def_race&(1<<tstatus->race)) || (sd->right_weapon.ignore_def_race&(1<<RC_ALL)) ||
			(sd->right_weapon.ignore_def_class&(1<<tstatus->class_)) || (sd->right_weapon.ignore_def_class&(1<<CLASS_ALL)))
			if(weapon_position == EQI_HAND_R)
				return true;
		if((sd->left_weapon.ignore_def_ele&(1<<tstatus->def_ele)) || (sd->left_weapon.ignore_def_ele&(1<<ELE_ALL)) ||
			(sd->left_weapon.ignore_def_race&(1<<tstatus->race)) || (sd->left_weapon.ignore_def_race&(1<<RC_ALL)) ||
			(sd->left_weapon.ignore_def_class&(1<<tstatus->class_)) || (sd->left_weapon.ignore_def_class&(1<<CLASS_ALL)))
		{
			if(battle_config.left_cardfix_to_right && is_attack_right_handed(src, skill_id)) {
				if(weapon_position == EQI_HAND_R)
					return true; //Move effect to right hand [Skotlex]
			} else if(weapon_position == EQI_HAND_L)
				return true;
		}
	}
	return (nk&NK_IGNORE_DEF);
}

/*================================================
 * Should skill attack consider VVS and masteries?
 *------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static bool battle_skill_stacks_masteries_vvs(uint16 skill_id)
{
	switch(skill_id) {
		case MO_INVESTIGATE:
		case MO_EXTREMITYFIST:
		case CR_GRANDCROSS:
		case PA_SACRIFICE:
#ifndef RENEWAL
		case NJ_ISSEN:
		case LK_SPIRALPIERCE:
		case ML_SPIRALPIERCE:
		case CR_SHIELDBOOMERANG:
		case PA_SHIELDCHAIN:
#else
		case AM_ACIDTERROR:
		case CR_ACIDDEMONSTRATION:
		case GN_FIRE_EXPANSION_ACID:
#endif
		case RK_DRAGONBREATH:
		case RK_DRAGONBREATH_WATER:
		case NC_SELFDESTRUCTION:
		case RL_MASS_SPIRAL:
			return false;
	}
	return true;
}

#ifdef RENEWAL
/*========================================
 * Calculate equipment ATK for renewal ATK
 *----------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static int battle_calc_equip_attack(struct block_list *src, uint16 skill_id)
{
	int eatk = 0;
	struct status_data *status = status_get_status_data(src);
	struct map_session_data *sd = BL_CAST(BL_PC, src);

	if(sd) //Add arrow ATK if using an applicable skill
		eatk += (is_skill_using_arrow(src, skill_id) ? sd->bonus.arrow_atk : 0);
	return eatk + status->eatk;
}
#endif

/*========================================
 * Returns the element type of attack
 *----------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
int battle_get_weapon_element(struct Damage *d, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, short weapon_position)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);
	int element = skill_get_ele(skill_id, skill_lv);

	if(!skill_id || element == -1) { //Take weapon's element
		if(sd && sd->charmball_type != CHARM_TYPE_NONE && sd->charmball >= MAX_CHARMBALL)
			element = sd->charmball_type; //Summoning 10 charmball will endow your weapon
		else
			element = (weapon_position == EQI_HAND_L ? sstatus->lhw.ele : sstatus->rhw.ele);
		if(is_skill_using_arrow(src, skill_id) && weapon_position == EQI_HAND_R && sd) {
			if(sd->bonus.arrow_ele)
				element = sd->bonus.arrow_ele;
			else {
				switch(sd->status.weapon) {
					case W_BOW:	case W_REVOLVER:
					case W_RIFLE:	case W_GATLING:
					case W_SHOTGUN:	case W_GRENADE:
						break; //Used weapon element
					default:
						element = ELE_NEUTRAL;
						break;
				}
			}
		}
	} else if(element == -2) //Use enchantment's element
		element = status_get_attack_sc_element(src, sc);
	else if(element == -3) //Use random element
		element = rnd()%ELE_ALL;

	switch(skill_id) {
		case LK_SPIRALPIERCE:
			if(!sd) //Forced neutral for monsters
		//Fall through
		case SO_VARETYR_SPEAR:
			element = ELE_NEUTRAL;
			break;
		case LG_HESPERUSLIT:
			if(sc && sc->data[SC_BANDING] && ((battle_config.hesperuslit_bonus_stack && sc->data[SC_BANDING]->val2 >= 5) || sc->data[SC_BANDING]->val2 == 5))
				element = ELE_HOLY;
			break;
		case RL_H_MINE:
			if(d->miscflag&16)
				element = ELE_FIRE; //Force RL_H_MINE deals fire damage if activated by RL_FLICKER
			break;
	}

	if(sc && sc->data[SC_GOLDENE_FERSE] && ((!skill_id && (rnd()%100 < sc->data[SC_GOLDENE_FERSE]->val4)) || skill_id == MH_STAHL_HORN) )
		element = ELE_HOLY;
	return element;
}

#define ATK_RATE(damage, damage2, a) { damage = damage * (a) / 100; if(is_attack_left_handed(src, skill_id)) damage2 = damage2 * (a) / 100; }
#define ATK_RATE2(damage, damage2, a , b) { damage = damage *(a) / 100; if(is_attack_left_handed(src, skill_id)) damage2 = damage2 * (b) / 100; }
#define ATK_RATER(damage, a) { damage = damage * (a) / 100; }
#define ATK_RATEL(damage2, a) { damage2 = damage2 * (a) / 100; }
//Adds dmg%. 100 = +100% (double) damage. 10 = +10% damage
#define ATK_ADDRATE(damage, damage2, a) { damage += damage * (a) / 100; if(is_attack_left_handed(src, skill_id)) damage2 += damage2 *(a) / 100; }
#define ATK_ADDRATE2(damage, damage2, a , b) { damage += damage * (a) / 100; if(is_attack_left_handed(src, skill_id)) damage2 += damage2 * (b) / 100; }
//Adds an absolute value to damage. 100 = +100 damage
#define ATK_ADD(damage, damage2, a) { damage += a; if(is_attack_left_handed(src, skill_id)) damage2 += a; }
#define ATK_ADD2(damage, damage2, a , b) { damage += a; if(is_attack_left_handed(src, skill_id)) damage2 += b; }

#ifdef RENEWAL
	#define RE_ALLATK_ADD(wd, a) { ATK_ADD(wd.statusAtk, wd.statusAtk2, a); ATK_ADD(wd.weaponAtk, wd.weaponAtk2, a); ATK_ADD(wd.equipAtk, wd.equipAtk2, a); ATK_ADD(wd.masteryAtk, wd.masteryAtk2, a); }
	#define RE_ALLATK_RATE(wd, a) { ATK_RATE(wd.statusAtk, wd.statusAtk2, a); ATK_RATE(wd.weaponAtk, wd.weaponAtk2, a); ATK_RATE(wd.equipAtk, wd.equipAtk2, a); ATK_RATE(wd.masteryAtk, wd.masteryAtk2, a); }
	#define RE_ALLATK_ADDRATE(wd, a) { ATK_ADDRATE(wd.statusAtk, wd.statusAtk2, a); ATK_ADDRATE(wd.weaponAtk, wd.weaponAtk2, a); ATK_ADDRATE(wd.equipAtk, wd.equipAtk2, a); ATK_ADDRATE(wd.masteryAtk, wd.masteryAtk2, a); }
#else
	#define RE_ALLATK_ADD(wd, a) {;}
	#define RE_ALLATK_RATE(wd, a) {;}
	#define RE_ALLATK_ADDRATE(wd, a) {;}
#endif

/*========================================
 * Do element damage modifier calculation
 *----------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static struct Damage battle_calc_element_damage(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	int element = skill_get_ele(skill_id, skill_lv);
	int left_element = battle_get_weapon_element(&wd, src, target, skill_id, skill_lv, EQI_HAND_L);
	int right_element = battle_get_weapon_element(&wd, src, target, skill_id, skill_lv, EQI_HAND_R);
	int nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);

	if(!(nk&NK_NO_ELEFIX) && (wd.damage > 0 || wd.damage2 > 0)) { //Elemental attribute fix
		//Non-pc (mob, pet, homun) physical melee attacks are "no elemental", they deal 100% to all target elements
		//However the "no elemental" attacks still get reduced by "Neutral resistance"
		//Also non-pc units have only a defending element, but can inflict elemental attacks using skills [exneval]
		if((battle_config.attack_attr_none&src->type) && ((!skill_id && right_element == ELE_NEUTRAL) ||
			(skill_id && (element == -1 || right_element == ELE_NEUTRAL))) && (wd.flag&(BF_SHORT|BF_WEAPON)) == (BF_SHORT|BF_WEAPON))
			return wd;
		switch(skill_id) {
#ifdef RENEWAL
			case PA_SACRIFICE:
			case RK_DRAGONBREATH:
			case RK_DRAGONBREATH_WATER:
			case NC_SELFDESTRUCTION:
			case HFLI_SBR44:
#else
			default:
#endif
				wd.damage = battle_attr_fix(src, target, wd.damage, right_element, tstatus->def_ele, tstatus->ele_lv);
				if(is_attack_left_handed(src, skill_id))
					wd.damage2 = battle_attr_fix(src, target, wd.damage2, left_element, tstatus->def_ele, tstatus->ele_lv);
				break;
		}
		switch(skill_id) { //Skills force to neutral element
#ifdef RENEWAL
			case MO_INVESTIGATE:
			case CR_SHIELDBOOMERANG:
			case PA_SHIELDCHAIN:
#endif
			case MC_CARTREVOLUTION:
			case HW_MAGICCRASHER:
			case SR_FALLENEMPIRE:
			case SR_TIGERCANNON:
			case SR_CRESCENTELBOW_AUTOSPELL:
			case SR_GATEOFHELL:
			case NC_MAGMA_ERUPTION:
				wd.damage = battle_attr_fix(src, target, wd.damage, ELE_NEUTRAL, tstatus->def_ele, tstatus->ele_lv);
				if(is_attack_left_handed(src, skill_id))
					wd.damage2 = battle_attr_fix(src, target, wd.damage2, ELE_NEUTRAL, tstatus->def_ele, tstatus->ele_lv);
				break;
		}
#ifdef RENEWAL
		if(!sd) { //Only monsters have a single ATK for element, in pre-renewal we also apply element to entire ATK on players [helvetica]
#endif
			if(sc && sc->data[SC_WATK_ELEMENT]) { //Descriptions indicate this means adding a percent of a normal attack in another element [Skotlex]
				int64 damage = battle_calc_base_damage(src, sstatus, &sstatus->rhw, sc, tstatus->size, sd, (is_skill_using_arrow(src, skill_id) ? 2 : 0)) * sc->data[SC_WATK_ELEMENT]->val2 / 100;

				wd.damage += battle_attr_fix(src, target, damage, sc->data[SC_WATK_ELEMENT]->val1, tstatus->def_ele, tstatus->ele_lv);
				if(is_attack_left_handed(src, skill_id)) {
					damage = battle_calc_base_damage(src, sstatus, &sstatus->lhw, sc, tstatus->size, sd, (is_skill_using_arrow(src, skill_id) ? 2 : 0)) * sc->data[SC_WATK_ELEMENT]->val2 / 100;
					wd.damage2 += battle_attr_fix(src, target, damage, sc->data[SC_WATK_ELEMENT]->val1, tstatus->def_ele, tstatus->ele_lv);
				}
			}
#ifdef RENEWAL
		}
#endif
	}
	return wd;
}

/*==================================
 * Calculate weapon mastery damages
 *----------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static struct Damage battle_calc_attack_masteries(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);
	int t_class = status_get_class(target);
	uint8 lv = 0;

	//Add mastery damage
	if(battle_skill_stacks_masteries_vvs(skill_id)) {
		wd.damage = battle_addmastery(sd, target, wd.damage, 0);
#ifdef RENEWAL
		wd.masteryAtk = battle_addmastery(sd, target, wd.weaponAtk, 0);
#endif
		if(is_attack_left_handed(src, skill_id)) {
			wd.damage2 = battle_addmastery(sd, target, wd.damage2, 1);
#ifdef RENEWAL
			wd.masteryAtk2 = battle_addmastery(sd, target, wd.weaponAtk2, 1);
#endif
		}
		//General skill masteries
#ifdef RENEWAL
		if(sd) {
			short div_ = max(wd.div_, 1);

			if(skill_id != MC_CARTREVOLUTION && pc_checkskill(sd, BS_HILTBINDING) > 0)
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, 4);
			if(skill_id != CR_SHIELDBOOMERANG)
				ATK_ADD2(wd.masteryAtk, wd.masteryAtk2, div_ * sd->right_weapon.star, div_ * sd->left_weapon.star);
			if(skill_id == MO_FINGEROFFENSIVE) {
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, div_ * sd->spiritball_old * 3);
			} else
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, div_ * sd->spiritball * 3);
			if(!skill_id && sd->status.party_id && (lv = pc_checkskill(sd, TK_POWER)) > 0) { //Doesn't increase skill damage in renewal [exneval]
				int members = party_foreachsamemap(party_sub_count, sd, 0);

				if(members > 1)
					ATK_ADDRATE(wd.masteryAtk, wd.masteryAtk2, 2 * lv * (members - 1));
			}
		}
#endif
		switch(skill_id) {
			case TF_POISON:
				ATK_ADD(wd.damage, wd.damage2, 15 * skill_lv);
#ifdef RENEWAL //Additional ATK from Envenom is treated as mastery type damage [helvetica]
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, 15 * skill_lv);
#endif
				break;
			case AS_SONICBLOW:
				if(sd && pc_checkskill(sd, AS_SONICACCEL) > 0) {
					ATK_ADDRATE(wd.damage, wd.damage2, 10);
#ifdef RENEWAL
					ATK_ADDRATE(wd.masteryAtk, wd.masteryAtk2, 10);
#endif
				}
				break;
			case TK_DOWNKICK:
			case TK_STORMKICK:
			case TK_TURNKICK:
			case TK_COUNTER:
				if((lv = (sd && sd->status.weapon == W_FIST ? pc_checkskill(sd, TK_RUN) : 10)) > 0) {
					ATK_ADD(wd.damage, wd.damage2, 10 * lv);
#ifdef RENEWAL
					ATK_ADD(wd.masteryAtk, wd.masteryAtk2, 10 * lv);
#endif
				}
				break;
			case GS_GROUNDDRIFT:
				ATK_ADD(wd.damage, wd.damage2, 50 * skill_lv);
				break;
			case NJ_SYURIKEN:
				if((lv = (sd ? pc_checkskill(sd, NJ_TOBIDOUGU) : 10)) > 0) {
					ATK_ADD(wd.damage, wd.damage2, 3 * lv);
#ifdef RENEWAL
					ATK_ADD(wd.masteryAtk, wd.masteryAtk2, 3 * lv);
#endif
				}
				break;
			case RA_WUGDASH:
			case RA_WUGSTRIKE:
			case RA_WUGBITE:
				if(sd && (lv = pc_checkskill(sd, RA_TOOTHOFWUG)) > 0) {
					ATK_ADD(wd.damage, wd.damage2, 30 * lv);
#ifdef RENEWAL
					ATK_ADD(wd.masteryAtk, wd.masteryAtk2, 30 * lv);
#endif
				}
				break;
		}
		if(sc) { //Status change considered as masteries
			uint8 i = 0;

#ifdef RENEWAL //The level 4 weapon limitation has been removed
			if(sc->data[SC_NIBELUNGEN])
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, sc->data[SC_NIBELUNGEN]->val2);
#endif
			if(sc->data[SC_AURABLADE]) {
				ATK_ADD(wd.damage, wd.damage2, 20 * sc->data[SC_AURABLADE]->val1);
#ifdef RENEWAL
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, 20 * sc->data[SC_AURABLADE]->val1);
#endif
			}
			if(sc->data[SC_CAMOUFLAGE]) {
				ATK_ADD(wd.damage, wd.damage2, 30 * min(10, sc->data[SC_CAMOUFLAGE]->val3));
#ifdef RENEWAL
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, 30 * min(10, sc->data[SC_CAMOUFLAGE]->val3));
#endif
			}
			if(sc->data[SC_GN_CARTBOOST]) {
				ATK_ADD(wd.damage, wd.damage2, 10 * sc->data[SC_GN_CARTBOOST]->val1);
#ifdef RENEWAL
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, 10 * sc->data[SC_GN_CARTBOOST]->val1);
#endif
			}
			if(sc->data[SC_RUSHWINDMILL]) {
				ATK_ADD(wd.damage, wd.damage2, sc->data[SC_RUSHWINDMILL]->val4);
#ifdef RENEWAL
				ATK_ADD(wd.masteryAtk, wd.masteryAtk2, sc->data[SC_RUSHWINDMILL]->val4);
#endif
			}
#ifdef RENEWAL
			if(sc->data[SC_PROVOKE])
				ATK_ADDRATE(wd.masteryAtk, wd.masteryAtk2, sc->data[SC_PROVOKE]->val3);
#endif
			if(sd) {
				if(sc->data[SC_MIRACLE])
					i = 2; //Star anger
				else
					ARR_FIND(0, MAX_PC_FEELHATE, i, t_class == sd->hate_mob[i]);
				if(i < MAX_PC_FEELHATE && (lv = pc_checkskill(sd,sg_info[i].anger_id)) > 0) {
					int skillratio = sd->status.base_level + sstatus->dex + sstatus->luk;

					if(i == 2)
						skillratio += sstatus->str; //Star Anger
					if(lv < 4)
						skillratio /= 12 - 3 * lv;
					ATK_ADDRATE(wd.damage, wd.damage2, skillratio);
#ifdef RENEWAL
					ATK_ADDRATE(wd.masteryAtk, wd.masteryAtk2, skillratio);
#endif
				}
			}
		}
	}

	return wd;
}

#ifdef RENEWAL
/*=========================================
 * Calculate the various Renewal ATK parts
 *-----------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_damage_parts(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	int right_element = battle_get_weapon_element(&wd, src, target, skill_id, skill_lv, EQI_HAND_R);
	int left_element = battle_get_weapon_element(&wd, src, target, skill_id, skill_lv, EQI_HAND_L);

	wd.statusAtk += battle_calc_status_attack(sstatus, EQI_HAND_R);
	wd.statusAtk2 += battle_calc_status_attack(sstatus, EQI_HAND_L);

	if(sd->sc.data[SC_SEVENWIND]) { //Status ATK can only be endowed by Mild Wind
		wd.statusAtk = battle_attr_fix(src, target, wd.statusAtk, right_element, tstatus->def_ele, tstatus->ele_lv);
		wd.statusAtk2 = battle_attr_fix(src, target, wd.statusAtk, left_element, tstatus->def_ele, tstatus->ele_lv);
	} else { //Without Mild Wind status, status ATK should always be neutral
		wd.statusAtk = battle_attr_fix(src, target, wd.statusAtk, ELE_NEUTRAL, tstatus->def_ele, tstatus->ele_lv);
		wd.statusAtk2 = battle_attr_fix(src, target, wd.statusAtk, ELE_NEUTRAL, tstatus->def_ele, tstatus->ele_lv);
	}

	wd.weaponAtk += battle_calc_base_weapon_attack(src, tstatus, &sstatus->rhw, sd, skill_id);
	wd.weaponAtk = battle_attr_fix(src, target, wd.weaponAtk, right_element, tstatus->def_ele, tstatus->ele_lv);

	wd.weaponAtk2 += battle_calc_base_weapon_attack(src, tstatus, &sstatus->lhw, sd, skill_id);
	wd.weaponAtk2 = battle_attr_fix(src, target, wd.weaponAtk2, left_element, tstatus->def_ele, tstatus->ele_lv);

	wd.equipAtk += battle_calc_equip_attack(src, skill_id);
	if(is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_R))
		wd.equipAtk += battle_get_defense(src, target, skill_id, 0) / 2;
	wd.equipAtk = battle_attr_fix(src, target, wd.equipAtk, right_element, tstatus->def_ele, tstatus->ele_lv);

	wd.equipAtk2 += battle_calc_equip_attack(src, skill_id);
	if(is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_L))
		wd.equipAtk2 += battle_get_defense(src, target, skill_id, 0) / 2;
	wd.equipAtk2 = battle_attr_fix(src, target, wd.equipAtk2, left_element, tstatus->def_ele, tstatus->ele_lv);

	//Mastery ATK is a special kind of ATK that has no elemental properties
	//Because masteries are not elemental, they are unaffected by Ghost armors or Raydric Card
	wd = battle_calc_attack_masteries(wd, src, target, skill_id, skill_lv);

	wd.damage = 0;
	wd.damage2 = 0;

	return wd;
}
#endif

/*==========================================================
 * Calculate basic ATK that goes into the skill ATK formula
 *----------------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_skill_base_damage(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	int nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);
	uint16 i;

	switch(skill_id) { //Calc base damage according to skill
		case PA_SACRIFICE: {
				int64 damagevalue = sstatus->max_hp * 9 / 100;

				ATK_ADD(wd.damage, wd.damage2, damagevalue);
#ifdef RENEWAL
				ATK_ADD(wd.weaponAtk, wd.weaponAtk2, damagevalue);
#endif
			}
			break;
#ifdef RENEWAL
		case LK_SPIRALPIERCE:
		case ML_SPIRALPIERCE:
			if(sd) {
				short index = sd->equip_index[EQI_HAND_R];

				wd = battle_calc_damage_parts(wd, src, target, skill_id, skill_lv);
				//Weight from spear is treated as equipment ATK on official [helvetica]
				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
					ATK_ADD(wd.equipAtk, wd.equipAtk2, sd->inventory_data[index]->weight * 5 / 100); //50% of weight
			} else //Monsters have no weight and use ATK instead
				wd.damage = battle_calc_base_damage(src, sstatus, &sstatus->rhw, sc, tstatus->size, sd, 0);
			switch(tstatus->size) { //Size-fix
				case SZ_SMALL: //125%
					RE_ALLATK_RATE(wd, 125);
					break;
				case SZ_BIG: //75%
					RE_ALLATK_RATE(wd, 75);
					break;
			}
			break;
		case CR_SHIELDBOOMERANG:
		case PA_SHIELDCHAIN:
			if(sd) {
				short index = sd->equip_index[EQI_HAND_L];

				wd = battle_calc_damage_parts(wd, src, target, skill_id, skill_lv);
				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR) {
					ATK_ADD(wd.equipAtk, wd.equipAtk2, sd->inventory_data[index]->weight / 10);
					if(skill_id == CR_SHIELDBOOMERANG)
						ATK_ADD(wd.equipAtk, wd.equipAtk2, 4 * sd->inventory.u.items_inventory[index].refine);
				}
			} else
				wd.damage = battle_calc_base_damage(src, sstatus, &sstatus->rhw, sc, tstatus->size, sd, 0);
			break;
#else
		case NJ_ISSEN:
			ATK_ADD(wd.damage, wd.damage2, 40 * sstatus->str + sstatus->hp * 8 * skill_lv / 100);
			break;
		case LK_SPIRALPIERCE:
		case ML_SPIRALPIERCE:
			if(sd) {
				short index = sd->equip_index[EQI_HAND_R];

				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
					wd.damage = sd->inventory_data[index]->weight * 8 / 100; //80% of weight
				ATK_ADDRATE(wd.damage, wd.damage2, 50 * skill_lv); //Skill modifier applies to weight only
			} else
				wd.damage = battle_calc_base_damage(src, sstatus, &sstatus->rhw, sc, tstatus->size, sd, 0);
			if((i = sstatus->str / 10)) { //Add STR bonus
				i = (i + (i - 1)) * 5;
				ATK_ADD(wd.damage, wd.damage2, i);
			}
			switch(tstatus->size) {
				case SZ_SMALL:
					ATK_RATE(wd.damage, wd.damage2, 125);
					break;
				case SZ_BIG:
					ATK_RATE(wd.damage, wd.damage2, 75);
					break;
			}
			break;
		case CR_SHIELDBOOMERANG:
		case PA_SHIELDCHAIN:
			if(sd) {
				short index = sd->equip_index[EQI_HAND_L];

				ATK_ADD(wd.damage, wd.damage2, sstatus->batk);
				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR) {
					ATK_ADD(wd.damage, wd.damage2, sd->inventory_data[index]->weight / 10);
					if(skill_id == CR_SHIELDBOOMERANG)
						ATK_ADD(wd.damage, wd.damage2, 4 * sd->inventory.u.items_inventory[index].refine);
				}
			} else
				wd.damage = battle_calc_base_damage(src, sstatus, &sstatus->rhw, sc, tstatus->size, sd, 0);
			break;
#endif
		case RK_DRAGONBREATH:
		case RK_DRAGONBREATH_WATER:
			{
				int64 damagevalue = (sstatus->hp / 50 + status_get_max_sp(src) / 4) * skill_lv;

				if(status_get_lv(src) > 100)
					damagevalue = damagevalue * status_get_lv(src) / 150;
				damagevalue = damagevalue * (100 + 5 * ((sd ? pc_checkskill(sd,RK_DRAGONTRAINING) : 5) - 1)) / 100;
				ATK_ADD(wd.damage, wd.damage2, damagevalue);
#ifdef RENEWAL
				ATK_ADD(wd.weaponAtk, wd.weaponAtk2, damagevalue);
#endif
			}
			break;
		case NC_SELFDESTRUCTION: {
				int64 damagevalue = (skill_lv + 1) * ((sd ? pc_checkskill(sd,NC_MAINFRAME) : 4) + 8) * (status_get_sp(src) + sstatus->vit);

				if(status_get_lv(src) > 100)
					damagevalue = damagevalue * status_get_lv(src) / 100;
				damagevalue = damagevalue + sstatus->hp;
				ATK_ADD(wd.damage, wd.damage2, damagevalue);
#ifdef RENEWAL
				ATK_ADD(wd.weaponAtk, wd.weaponAtk2, damagevalue);
#endif
			}
			break;
		case HFLI_SBR44: //[orn]
			if(src->type == BL_HOM)
				wd.damage = ((TBL_HOM *)src)->homunculus.intimacy;
			break;
		default:
#ifdef RENEWAL
			if(sd)
				wd = battle_calc_damage_parts(wd, src, target, skill_id, skill_lv);
			else {
				i = (!skill_id && sc && sc->data[SC_CHANGE] ? 4 : 0);
				wd.damage = battle_calc_base_damage(src, sstatus, &sstatus->rhw, sc, tstatus->size, sd, i);
				if(is_attack_left_handed(src, skill_id))
					wd.damage2 = battle_calc_base_damage(src, sstatus, &sstatus->lhw, sc, tstatus->size, sd, i);
			}
#else
			i = (is_attack_critical(wd, src, target, skill_id, skill_lv, false) ? 1 : 0)|
				(is_skill_using_arrow(src, skill_id) ? 2 : 0)|
				(skill_id == HW_MAGICCRASHER ? 4 : 0)|
				(!skill_id && sc && sc->data[SC_CHANGE] ? 4 : 0)|
				(skill_id == MO_EXTREMITYFIST ? 8 : 0)|
				(sc && sc->data[SC_WEAPONPERFECTION] ? 8 : 0);
			if(is_skill_using_arrow(src, skill_id) && sd) {
				switch(sd->status.weapon) {
					case W_BOW:	case W_REVOLVER:
					case W_RIFLE:	case W_GATLING:
					case W_SHOTGUN:	case W_GRENADE:
						break;
					default:
						i |= 16; //For ex. shuriken must not be influenced by DEX
						break;
				}
			}
			wd.damage = battle_calc_base_damage(src, sstatus, &sstatus->rhw, sc, tstatus->size, sd, i);
			if(is_attack_left_handed(src, skill_id))
				wd.damage2 = battle_calc_base_damage(src, sstatus, &sstatus->lhw, sc, tstatus->size, sd, i);
#endif
			if(nk&NK_SPLASHSPLIT) { //Divide ATK among targets
				if(wd.miscflag > 0) {
					wd.damage /= wd.miscflag;
#ifdef RENEWAL
					wd.statusAtk /= wd.miscflag;
					wd.weaponAtk /= wd.miscflag;
					wd.equipAtk /= wd.miscflag;
					wd.masteryAtk /= wd.miscflag;
#endif
				} else
					ShowError("0 enemies targeted by %d:%s, divide per 0 avoided!\n", skill_id, skill_get_name(skill_id));
			}
			if(sd) { //Add any bonuses that modify the base damage
				if(sd->bonus.atk_rate)
					ATK_ADDRATE(wd.damage, wd.damage2, sd->bonus.atk_rate);
#ifndef RENEWAL
				//Add +crit damage bonuses here in pre-renewal mode [helvetica]
				if(!skill_id && is_attack_critical(wd, src, target, skill_id, skill_lv, false))
					ATK_ADDRATE(wd.damage, wd.damage2, sd->bonus.crit_atk_rate);
				if(sd->status.party_id && pc_checkskill(sd, TK_POWER) > 0 &&
					(i = party_foreachsamemap(party_sub_count, sd, 0)) > 1) //Exclude the player himself [Inkfish]
					ATK_ADDRATE(wd.damage, wd.damage2, 2 * pc_checkskill(sd, TK_POWER) * (i - 1));
				if(sd->charmball_type == CHARM_TYPE_LAND && sd->charmball > 0)
					ATK_ADDRATE(wd.damage, wd.damage2, 10 * sd->charmball);
#endif
			}
			break;
	} //End switch(skill_id)
	return wd;
}

//For quick div adjustment
#define DAMAGE_DIV_FIX(dmg, div) {if (div > 1) (dmg) *= div; else if (div < 0) (div) *= -1; }
#define DAMAGE_DIV_FIX2(dmg, div) { if(div > 1) (dmg) *= div; }

/**
 * Applies DAMAGE_DIV_FIX and checks for min damage
 * @author [Playtester]
 * @param d: Damage struct to apply DAMAGE_DIV_FIX to
 * @param skill_id: ID of the skill that deals damage
 * @return Modified damage struct
 */
static struct Damage battle_apply_div_fix(struct Damage d, uint16 skill_id)
{
	if(d.damage > 0) {
		DAMAGE_DIV_FIX(d.damage, d.div_);
		//Min damage
		if(d.damage < d.div_) {
			switch(skill_id) {
				case SU_CN_METEOR:
				case SU_CN_METEOR2:
				case SU_LUNATICCARROTBEAT:
				case SU_LUNATICCARROTBEAT2:
					d.damage = d.div_;
					break;
				default:
					if(battle_config.skill_min_damage&d.flag)
						d.damage = d.div_;
					break;
			}
		}
	} else if(d.div_ < 0)
		d.div_ *= -1;

	return d;
}

/*=======================================
 * Check for and calculate multi attacks
 *---------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static struct Damage battle_calc_multi_attack(struct Damage wd, struct block_list *src,struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	short i;

	//If no skill_id passed, check for double attack [helvetica]
	if(sd && !skill_id) {
		short dachance = 0; //Success chance of double attacking. If player is in fear breeze status and generated number is within fear breeze's range, this will be ignored
		short hitnumber = 0; //Used for setting how many hits will hit
		short gendetect[] = { 12,12,21,27,30 }; //If generated number is outside this value while in fear breeze status, it will check if their's a chance for double attacking
		short generate = rnd()%100 + 1; //Generates a random number between 1 - 100 which is then used to determine if fear breeze or double attacking will happen
		bool is_qds_ready = false;

		//First we go through a number of checks to see if their's any chance of double attacking a target. Only the highest success chance is taken
		if(sd->weapontype1 != W_FIST) {
			if(sd->bonus.double_rate > 0)
				dachance = sd->bonus.double_rate;
			if(sc && sc->data[SC_KAGEMUSYA] && 10 * sc->data[SC_KAGEMUSYA]->val1 > dachance)
				dachance = 10 * sc->data[SC_KAGEMUSYA]->val1;
			if(sc && sc->data[SC_E_CHAIN] && 5 * sc->data[SC_E_CHAIN]->val1 > dachance) {
				dachance = 5 * sc->data[SC_E_CHAIN]->val1;
				is_qds_ready = true;
			}
		}

		if(5 * pc_checkskill(sd,TF_DOUBLE) > dachance && sd->weapontype1 == W_DAGGER)
			dachance = 5 * pc_checkskill(sd,TF_DOUBLE);

		if(5 * pc_checkskill(sd,GS_CHAINACTION) > dachance && sd->status.weapon == W_REVOLVER) {
			dachance = 5 * pc_checkskill(sd,GS_CHAINACTION);
			is_qds_ready = true;
		}

		//This checks if the generated value is within fear breeze's success chance range for the level used as set by gendetect
		if(sc && sc->data[SC_FEARBREEZE] && generate <= gendetect[sc->data[SC_FEARBREEZE]->val1 - 1] &&
			sd->status.weapon == W_BOW && (i = sd->equip_index[EQI_AMMO]) >= 0 && sd->inventory_data[i] &&
			sd->inventory.u.items_inventory[i].amount > 1)
		{
				if(generate >= 1 && generate <= 12) //12% chance to deal 2 hits
					hitnumber = 2;
				else if(generate >= 13 && generate <= 21) //9% chance to deal 3 hits
					hitnumber = 3;
				else if(generate >= 22 && generate <= 27) //6% chance to deal 4 hits
					hitnumber = 4;
				else if(generate >= 28 && generate <= 30) //3% chance to deal 5 hits
					hitnumber = 5;
				hitnumber = min(hitnumber,sd->inventory.u.items_inventory[i].amount);
				sc->data[SC_FEARBREEZE]->val4 = hitnumber - 1;
		}
		//If the generated value is higher then Fear Breeze's success chance range,
		//but not higher then the player's double attack success chance, then allow a double attack to happen
		else if(generate - 1 < dachance)
			hitnumber = 2;

		if(hitnumber > 1) { //Needed to allow critical attacks to hit when not hitting more then once
			if(is_qds_ready && !(sc && sc->data[SC_QD_SHOT_READY]) && pc_checkskill(sd,RL_QD_SHOT) && sstatus->amotion != pc_maxaspd(sd))
				sc_start(src,src,SC_QD_SHOT_READY,100,target->id,skill_get_time(RL_QD_SHOT,1));
			wd.div_ = hitnumber;
			wd.type = DMG_MULTI_HIT;
		}
	}

	switch(skill_id) {
		case NJ_ISSEN:
			if(sc && sc->data[SC_BUNSINJYUTSU] && (i = sc->data[SC_BUNSINJYUTSU]->val2) > 0)
				wd.div_ = -(i + 2); //Mirror image count + 2
			break;
		case RA_AIMEDBOLT:
			if(tsc && (tsc->data[SC_ANKLE] || tsc->data[SC_ELECTRICSHOCKER] || tsc->data[SC_BITE]))
				wd.div_ = tstatus->size + 2 + (rnd()%100 < 50 - tstatus->size * 10 ? 1 : 0);
			break;
		case RL_QD_SHOT:
			if((i = sd->equip_index[EQI_AMMO]) >= 0 && sd->inventory_data[i] && sd->inventory.u.items_inventory[i].amount > 0) {
				wd.div_ += status_get_job_lv(src) / 20;
				wd.div_ = min(wd.div_,sd->inventory.u.items_inventory[i].amount);
				if(battle_config.ammo_decrement && wd.div_ > 1)
					pc_delitem(sd,i,wd.div_ - 1,0,1,LOG_TYPE_CONSUME);
			}
			break;
	}

	return wd;
}

/*======================================================
 * Calculate skill level ratios for weapon-based skills
 *------------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static int battle_calc_attack_skill_ratio(struct Damage wd,struct block_list *src,struct block_list *target,uint16 skill_id,uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC,src);
	struct map_session_data *tsd = BL_CAST(BL_PC,target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	int skillratio = 100;
	int i;

	if(battle_skill_stacks_masteries_vvs(skill_id) && sc) { //Skill damage modifiers that stack linearly
		if(sc->data[SC_OVERTHRUST])
			skillratio += sc->data[SC_OVERTHRUST]->val3;
		if(sc->data[SC_MAXOVERTHRUST])
			skillratio += sc->data[SC_MAXOVERTHRUST]->val2;
#ifdef RENEWAL
		if(sc->data[SC_TRUESIGHT])
			skillratio += 2 * sc->data[SC_TRUESIGHT]->val1;
		if(sc->data[SC_CONCENTRATION])
			skillratio += sc->data[SC_CONCENTRATION]->val2;
#endif
		if(!skill_id || skill_id == KN_AUTOCOUNTER) {
			if(sc->data[SC_CRUSHSTRIKE]) {
				skillratio += -100 + sc->data[SC_CRUSHSTRIKE]->val2;
				skill_break_equip(src,src,EQP_WEAPON,2000,BCT_SELF);
				status_change_end(src,SC_CRUSHSTRIKE,INVALID_TIMER);
			} else {
				if(sc->data[SC_BERSERK])
#ifdef RENEWAL
					skillratio += 200;
#else
					skillratio += 100;
#endif
				//Increase is 125% in PvP/GvG, 250% otherwise, and only applies to Rune Knights
				if(sc->data[SC_GIANTGROWTH] && sd && (sd->class_&MAPID_THIRDMASK) == MAPID_RUNE_KNIGHT)
					skillratio += (map_flag_vs(src->m) ? 125 : 250);
			}
		}
		if(sc->data[SC_HEAT_BARREL])
			skillratio += sc->data[SC_HEAT_BARREL]->val2;
	}

	if(tsc && tsc->data[SC_KAITE] && tsc->data[SC_KAITE]->val3 && wd.flag == BF_SHORT)
		skillratio += 400; //Renewal: Increases melee physical damage the target takes additively by 400% [exneval])

	switch(skill_id) {
		case SM_BASH:
		case MS_BASH:
			skillratio += 30 * skill_lv;
			break;
		case SM_MAGNUM:
		case MS_MAGNUM:
			if(wd.miscflag == 1)
				skillratio += 20 * skill_lv; //Inner 3x3 circle takes 100%+20%*level damage [Playtester]
			else
				skillratio += 10 * skill_lv; //Outer 5x5 circle takes 100%+10%*level damage [Playtester]
			break;
		case MC_MAMMONITE:
			skillratio += 50 * skill_lv;
			break;
		case HT_POWER:
			skillratio += -50 + 8 * sstatus->str;
			break;
		case AC_DOUBLE:
		case MA_DOUBLE:
			skillratio += 10 * (skill_lv - 1);
			break;
		case AC_SHOWER:
		case MA_SHOWER:
#ifdef RENEWAL
			skillratio += 50 + 10 * skill_lv;
#else
			skillratio += -25 + 5 * skill_lv;
#endif
			break;
		case AC_CHARGEARROW:
		case MA_CHARGEARROW:
			skillratio += 50;
			break;
#ifndef RENEWAL
		case HT_FREEZINGTRAP:
		case MA_FREEZINGTRAP:
			skillratio += -50 + 10 * skill_lv;
			break;
#endif
		case KN_PIERCE:
		case ML_PIERCE:
			skillratio += 10 * skill_lv;
			break;
		case MER_CRASH:
			skillratio += 10 * skill_lv;
			break;
		case KN_SPEARSTAB:
			skillratio += 20 * skill_lv;
			break;
		case KN_SPEARBOOMERANG:
			skillratio += 50 * skill_lv;
			break;
		case KN_BRANDISHSPEAR:
		case ML_BRANDISH:
			{
				int ratio = 100 + 20 * skill_lv;

				skillratio += -100 + ratio;
				if(skill_lv > 3 && !wd.miscflag)
					skillratio += ratio / 2;
				if(skill_lv > 6 && !wd.miscflag)
					skillratio += ratio / 4;
				if(skill_lv > 9 && !wd.miscflag)
					skillratio += ratio / 8;
				if(skill_lv > 6 && wd.miscflag == 1)
					skillratio += ratio / 2;
				if(skill_lv > 9 && wd.miscflag == 1)
					skillratio += ratio / 4;
				if(skill_lv > 9 && wd.miscflag == 2)
					skillratio += ratio / 2;
			}
			break;
		case KN_BOWLINGBASH:
		case MS_BOWLINGBASH:
			skillratio+= 40 * skill_lv;
			break;
		case AS_GRIMTOOTH:
			skillratio += 20 * skill_lv;
			break;
		case AS_POISONREACT:
			skillratio += 30 * skill_lv;
			break;
		case AS_SONICBLOW:
			skillratio += 300 + 40 * skill_lv;
			break;
		case TF_SPRINKLESAND:
			skillratio += 30;
			break;
		case MC_CARTREVOLUTION:
			skillratio += 50;
			if(sd && sd->cart_weight)
				skillratio += 100 * sd->cart_weight / sd->cart_weight_max; //+1% every 1% weight
			else
				skillratio += 100; //Max damage for non players
			break;
		case NPC_PIERCINGATT:
			skillratio += -25; //75% base damage
			break;
		case NPC_COMBOATTACK:
			skillratio += 25 * skill_lv;
			break;
		case NPC_RANDOMATTACK:
		case NPC_WATERATTACK:
		case NPC_GROUNDATTACK:
		case NPC_FIREATTACK:
		case NPC_WINDATTACK:
		case NPC_POISONATTACK:
		case NPC_HOLYATTACK:
		case NPC_DARKNESSATTACK:
		case NPC_UNDEADATTACK:
		case NPC_TELEKINESISATTACK:
		case NPC_BLOODDRAIN:
		case NPC_ACIDBREATH:
		case NPC_DARKNESSBREATH:
		case NPC_FIREBREATH:
		case NPC_ICEBREATH:
		case NPC_THUNDERBREATH:
		case NPC_HELLJUDGEMENT:
		case NPC_PULSESTRIKE:
			skillratio += 100 * (skill_lv - 1);
			break;
		case NPC_EARTHQUAKE:
			skillratio += 100 + 100 * skill_lv + 100 * skill_lv / 2 + ((skill_lv > 4) ? 100 : 0);
			break;
		case NPC_REVERBERATION_ATK:
			skillratio += -100 + 200 * (skill_lv + 2);
			break;
		case RG_BACKSTAP:
			if(sd && sd->status.weapon == W_BOW && battle_config.backstab_bow_penalty)
				skillratio += (200 + 40 * skill_lv) / 2;
			else
				skillratio += 200 + 40 * skill_lv;
			break;
		case RG_RAID:
			skillratio += 40 * skill_lv;
			break;
		case RG_INTIMIDATE:
			skillratio += 30 * skill_lv;
			break;
		case CR_SHIELDCHARGE:
			skillratio += 20 * skill_lv;
			break;
		case CR_SHIELDBOOMERANG:
			skillratio += 30 * skill_lv;
			break;
		case NPC_DARKCROSS:
		case CR_HOLYCROSS:
#ifdef RENEWAL
			if(sd && sd->status.weapon == W_2HSPEAR)
				skillratio += 70 * skill_lv;
			else
#endif
				skillratio += 35 * skill_lv;
			break;
		case AM_DEMONSTRATION:
			skillratio += 20 * skill_lv;
			break;
		case AM_ACIDTERROR:
#ifdef RENEWAL
			skillratio += 100 + 80 * skill_lv;
#else
			skillratio += 40 * skill_lv;
#endif
			break;
		case MO_FINGEROFFENSIVE:
			skillratio += 50 * skill_lv;
			break;
		case MO_INVESTIGATE:
			skillratio += 100 + 150 * skill_lv;
			break;
		case MO_TRIPLEATTACK:
			skillratio += 20 * skill_lv;
			break;
		case MO_CHAINCOMBO:
			skillratio += 50 + 50 * skill_lv;
			break;
		case MO_COMBOFINISH:
			skillratio += 140 + 60 * skill_lv;
			break;
		case BA_MUSICALSTRIKE:
		case DC_THROWARROW:
			skillratio += 25 + 25 * skill_lv;
			break;
		case CH_TIGERFIST:
			skillratio += -60 + 100 * skill_lv;
			break;
		case CH_CHAINCRUSH:
			skillratio += 300 + 100 * skill_lv;
			break;
		case CH_PALMSTRIKE:
			skillratio += 100 + 100 * skill_lv;
			break;
		case LK_HEADCRUSH:
			skillratio += 40 * skill_lv;
			break;
		case LK_JOINTBEAT:
			i = -50 + 10 * skill_lv;
			if(wd.miscflag&BREAK_NECK)
				i *= 2; //Although not clear, it's being assumed that the 2x damage is only for the break neck ailment
			skillratio += i;
			break;
#ifdef RENEWAL //Renewal: Skill ratio applies to entire damage [helvetica]
		case LK_SPIRALPIERCE:
		case ML_SPIRALPIERCE:
			skillratio += 50 * skill_lv;
			break;
#endif
		case ASC_METEORASSAULT:
			skillratio += -60 + 40 * skill_lv;
			break;
		case SN_SHARPSHOOTING:
		case MA_SHARPSHOOTING:
			skillratio += 100 + 50 * skill_lv;
			break;
		case CG_ARROWVULCAN:
			skillratio += 100 + 100 * skill_lv;
			break;
		case AS_SPLASHER:
			skillratio += 400 + 50 * skill_lv + 20 * (sd ? pc_checkskill(sd,AS_POISONREACT) : 10);
			break;
#ifndef RENEWAL //Pre-Renewal: Skill ratio for weapon part of damage [helvetica]
		case ASC_BREAKER:
			skillratio += -100 + 100 * skill_lv;
			break;
#endif
		case PA_SACRIFICE:
			skillratio += -10 + 10 * skill_lv;
			break;
		case PA_SHIELDCHAIN:
			skillratio += 30 * skill_lv;
			break;
		case WS_CARTTERMINATION:
			i = max(1,10 * (16 - skill_lv));
			if(sd && sd->cart_weight) //Preserve damage ratio when max cart weight is changed
				skillratio += sd->cart_weight / i * 80000 / battle_config.max_cart_weight - 100;
			else
				skillratio += 80000 / i - 100;
			break;
		case TK_DOWNKICK:
		case TK_STORMKICK:
			skillratio += 60 + 20 * skill_lv;
			break;
		case TK_TURNKICK:
		case TK_COUNTER:
			skillratio += 90 + 30 * skill_lv;
			break;
		case TK_JUMPKICK: //Different damage formulas depending on damage trigger
			if(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == skill_id)
				skillratio += -100 + 4 * status_get_lv(src); //Tumble formula [4%*baselevel]
			else if(wd.miscflag) {
				skillratio += -100 + 4 * status_get_lv(src); //Running formula [4%*baselevel]
				if(sc && sc->data[SC_STRUP])
					skillratio *= 2; //Strup formula [8%*baselevel]
			} else
				skillratio += -70 + 10 * skill_lv;
			break;
		case GS_TRIPLEACTION:
			skillratio += 50 * skill_lv;
			break;
		case GS_BULLSEYE: //Only works well against non boss brute/demihuman monster
			if((tstatus->race == RC_BRUTE || tstatus->race == RC_DEMIHUMAN) && !status_has_mode(tstatus,MD_STATUS_IMMUNE))
				skillratio += 400;
			break;
		case GS_TRACKING:
			skillratio += 100 * (skill_lv + 1);
			break;
		case GS_PIERCINGSHOT:
#ifdef RENEWAL
			if(sd && sd->status.weapon == W_RIFLE)
				skillratio += 150 + 30 * skill_lv;
			else
				skillratio += 100 + 20 * skill_lv;
#else
			skillratio += 20 * skill_lv;
#endif
			break;
		case GS_RAPIDSHOWER:
			skillratio += 400 + 50 * skill_lv;
			break;
		case GS_DESPERADO:
			skillratio += 50 * (skill_lv - 1);
			if(sc && sc->data[SC_FALLEN_ANGEL])
				skillratio *= 2;
			break;
		case GS_DUST:
			skillratio += 50 * skill_lv;
			break;
		case GS_FULLBUSTER:
			skillratio += 100 * (skill_lv + 2);
			break;
		case GS_SPREADATTACK:
#ifdef RENEWAL
			skillratio += 30 * skill_lv;
#else
			skillratio += 20 * (skill_lv - 1);
#endif
			break;
#ifdef RENEWAL
		case GS_GROUNDDRIFT:
			skillratio += 100 + 20 * skill_lv;
			break;
#endif
		case NJ_HUUMA:
			skillratio += 50 + 150 * skill_lv;
			break;
		case NJ_TATAMIGAESHI:
			skillratio += 10 * skill_lv;
#ifdef RENEWAL
			skillratio *= 2;
#endif
			break;
		case NJ_KASUMIKIRI:
			skillratio += 10 * skill_lv;
			break;
		case NJ_KIRIKAGE:
			skillratio += 100 * (skill_lv - 1);
			break;
#ifdef RENEWAL
		case NJ_KUNAI:
			skillratio += 200;
			break;
#endif
		case KN_CHARGEATK: { //+100% every 3 cells of distance but hard-limited to 500%
				int k = (wd.miscflag - 1) / 3;

				if(k < 0)
					k = 0;
				else if(k > 4)
					k = 4;
				skillratio += 100 * k;
			}
			break;
		case HT_PHANTASMIC:
			skillratio += 50;
			break;
		case MO_BALKYOUNG:
			skillratio += 200;
			break;
		case HFLI_MOON: //[orn]
			skillratio += 10 + 110 * skill_lv;
			break;
		case HFLI_SBR44: //[orn]
			skillratio += 100 * (skill_lv - 1);
			break;
		case NPC_VAMPIRE_GIFT:
			skillratio += ((skill_lv - 1)%5 + 1) * 100;
			break;
		case RK_SONICWAVE:
			//ATK = {((Skill Level + 5) x 100) x (1 + [(Caster's Base Level - 100) / 200])} %
			skillratio += -100 + (skill_lv + 5) * 100;
			skillratio = skillratio * (100 + (status_get_lv(src) - 100) / 2) / 100;
			break;
		case RK_HUNDREDSPEAR:
			skillratio += 500 + 80 * skill_lv + 50 * (sd ? pc_checkskill(sd,LK_SPIRALPIERCE) : 5);
			if(sd) {
				short index = sd->equip_index[EQI_HAND_R];

				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
					skillratio += max(10000 - sd->inventory_data[index]->weight,0) / 10;
			} else
				skillratio += 1000;
			//(1 + [(Caster's Base Level - 100) / 200])
			skillratio = skillratio * (100 + (status_get_lv(src) - 100) / 2) / 100;
			break;
		case RK_WINDCUTTER:
			skillratio += -100 + (skill_lv + 2) * 50;
			RE_LVL_DMOD(100);
			break;
		case RK_IGNITIONBREAK:
			//3x3 cell Damage = ATK [{(Skill Level x 300) x (1 + [(Caster's Base Level - 100) / 100])}] %
			//7x7 cell Damage = ATK [{(Skill Level x 250) x (1 + [(Caster's Base Level - 100) / 100])}] %
			//11x11 cell Damage = ATK [{(Skill Level x 200) x (1 + [(Caster's Base Level - 100) / 100])}] %
			i = distance_bl(src,target);
			if(i < 2)
				skillratio += -100 + 300 * skill_lv;
			else if(i < 4)
				skillratio += -100 + 250 * skill_lv;
			else
				skillratio += -100 + 200 * skill_lv;
			skillratio = skillratio * status_get_lv(src) / 100;
			//Additional (Skill Level x 100) damage if your weapon element is fire
			if(sstatus->rhw.ele == ELE_FIRE)
				skillratio += 100 * skill_lv;
			break;
		case RK_STORMBLAST:
			//ATK = [{Rune Mastery Skill Level + (Caster's STR / 8)} x 100] %
			skillratio += -100 + 100 * ((sd ? pc_checkskill(sd,RK_RUNEMASTERY) : 10) + sstatus->str / 8);
			break;
		case RK_PHANTOMTHRUST:
			//ATK = [{(Skill Level x 50) + (Spear Master Level x 10)} x Caster's Base Level / 150] %
			skillratio += -100 + 50 * skill_lv + 10 * (sd ? pc_checkskill(sd,KN_SPEARMASTERY) : 10);
			RE_LVL_DMOD(150); //Base level bonus
			break;
		case GC_CROSSIMPACT:
			skillratio += 900 + 100 * skill_lv;
			RE_LVL_DMOD(120);
			break;
		case GC_COUNTERSLASH:
			//ATK [{(Skill Level x 100) + 300} x Caster's Base Level / 120]% + ATK [(AGI x 2) + (Caster's Job Level x 4)]%
			skillratio += 200 + (100 * skill_lv);
			RE_LVL_DMOD(120);
			break;
		case GC_VENOMPRESSURE:
			skillratio += 900;
			break;
		case GC_PHANTOMMENACE:
			skillratio += 200;
			break;
		case GC_ROLLINGCUTTER:
			skillratio += -50 + 50 * skill_lv;
			RE_LVL_DMOD(100);
			break;
		case GC_CROSSRIPPERSLASHER:
			skillratio += 300 + 80 * skill_lv;
			RE_LVL_DMOD(100);
			if(sc && sc->data[SC_ROLLINGCUTTER])
				skillratio += sc->data[SC_ROLLINGCUTTER]->val1 * sstatus->agi;
			break;
		case GC_DARKCROW:
			skillratio += 100 * (skill_lv - 1);
			break;
		case AB_DUPLELIGHT_MELEE:
			skillratio += 50 + 15 * skill_lv;
			break;
		case RA_ARROWSTORM:
			skillratio += 900 + 80 * skill_lv;
			RE_LVL_DMOD(100);
			break;
		case RA_AIMEDBOLT:
			skillratio += 400 + 50 * skill_lv;
			RE_LVL_DMOD(100);
			break;
		case RA_CLUSTERBOMB:
			skillratio += 100 + 100 * skill_lv;
			break;
		case RA_WUGDASH: //ATK 300%
			skillratio += 200;
			break;
		case RA_WUGSTRIKE:
			skillratio += -100 + 200 * skill_lv;
			break;
		case RA_WUGBITE:
			skillratio += 300 + 200 * skill_lv;
			if(skill_lv == 5)
				skillratio += 100;
			break;
		case RA_SENSITIVEKEEN:
			skillratio += 50 * skill_lv;
			break;
		case NC_BOOSTKNUCKLE:
			skillratio += 100 + 100 * skill_lv + sstatus->dex;
			RE_LVL_DMOD(120);
			break;
		case NC_PILEBUNKER:
			skillratio += 200 + 100 * skill_lv + sstatus->str;
			RE_LVL_DMOD(100);
			break;
		case NC_VULCANARM:
			skillratio += -100 + 70 * skill_lv + sstatus->dex;
			RE_LVL_DMOD(120);
			break;
		case NC_FLAMELAUNCHER:
		case NC_COLDSLOWER:
			skillratio += 200 + 300 * skill_lv;
			RE_LVL_DMOD(150);
			break;
		case NC_ARMSCANNON:
			switch(tstatus->size) {
				case SZ_SMALL: skillratio += 200 + 400 * skill_lv; break; //Small
				case SZ_MEDIUM: skillratio += 200 + 350 * skill_lv; break; //Medium
				case SZ_BIG: skillratio += 200 + 300 * skill_lv; break; //Large
			}
			RE_LVL_DMOD(120);
			break;
		case NC_AXEBOOMERANG:
			skillratio += 150 + 50 * skill_lv;
			if(sd) {
				short index = sd->equip_index[EQI_HAND_R];

				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
					skillratio += sd->inventory_data[index]->weight / 10;
			} else
				skillratio += 500;
			RE_LVL_DMOD(100);
			break;
		case NC_POWERSWING:
			skillratio += -100 + sstatus->str + sstatus->dex;
			RE_LVL_DMOD(100);
			skillratio += 300 + 100 * skill_lv;
			break;
		case NC_AXETORNADO:
			skillratio += 100 + 100 * skill_lv + sstatus->vit;
			RE_LVL_DMOD(100);
			i = distance_bl(src,target);
			if(i > 2)
				skillratio = skillratio * 75 / 100;
			break;
		case NC_MAGMA_ERUPTION:
			skillratio += 350 + 50 * skill_lv;
			break;
		case SC_FATALMENACE:
			skillratio += 100 * skill_lv;
			RE_LVL_DMOD(100);
			break;
		case SC_TRIANGLESHOT:
			skillratio += 200 + (skill_lv - 1) * sstatus->agi / 2;
			RE_LVL_DMOD(120);
			break;
		case SC_FEINTBOMB:
			skillratio += -100 + (1 + skill_lv) * sstatus->dex / 2 * status_get_job_lv(src) / 10;
			RE_LVL_DMOD(120);
			break;
		case LG_CANNONSPEAR:
			skillratio += -100 + skill_lv * (50 + sstatus->str);
			RE_LVL_DMOD(100);
			break;
		case LG_BANISHINGPOINT:
			skillratio += -100 + 50 * skill_lv + 30 * (sd ? pc_checkskill(sd,SM_BASH) : 10);
			RE_LVL_DMOD(100);
			break;
		case LG_SHIELDPRESS:
			skillratio += -100 + 150 * skill_lv + sstatus->str;
			if(sd) {
				short index = sd->equip_index[EQI_HAND_L];

				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
					skillratio += sd->inventory_data[index]->weight / 10;
			} else
				skillratio += 250;
			RE_LVL_DMOD(100);
			break;
		case LG_PINPOINTATTACK:
			skillratio += -100 + 100 * skill_lv + 5 * sstatus->agi;
			RE_LVL_DMOD(120);
			break;
		case LG_RAGEBURST:
			skillratio += -100 + 200 * (sd ? sd->rageball_old : MAX_RAGEBALL) + (sstatus->max_hp - sstatus->hp) / 100;
			RE_LVL_DMOD(100);
			break;
		case LG_SHIELDSPELL: //[(Caster's Base Level x 4) + (Shield DEF x 10) + (Caster's VIT x 2)] %
			skillratio += -100 + 4 * status_get_lv(src) + 2 * sstatus->vit;
			if(sd) {
				short index = sd->equip_index[EQI_HAND_L];

				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
					skillratio += sd->inventory_data[index]->def * 10;
			}
			break;
		case LG_MOONSLASHER:
			skillratio += -100 + 120 * skill_lv + 80 * (sd ? pc_checkskill(sd,LG_OVERBRAND) : 5);
			RE_LVL_DMOD(100);
			break;
		case LG_OVERBRAND:
			skillratio += -100 + 400 * skill_lv + 50 * (sd ? pc_checkskill(sd,CR_SPEARQUICKEN) : 10);
			RE_LVL_DMOD(150);
			break;
		case LG_OVERBRAND_BRANDISH:
			skillratio += -100 + 300 * skill_lv + sstatus->str + sstatus->dex;
			RE_LVL_DMOD(150);
			break;
		case LG_OVERBRAND_PLUSATK:
			skillratio += -100 + 200 * skill_lv;
			break;
		case LG_RAYOFGENESIS:
			skillratio += 200 + 300 * skill_lv;
			RE_LVL_DMOD(100);
			break;
		case LG_EARTHDRIVE:
			if(sd) {
				short index = sd->equip_index[EQI_HAND_L];

				if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
					skillratio += -100 + (skill_lv + 1) * sd->inventory_data[index]->weight / 10;
			} else
				skillratio += -100 + (skill_lv + 1) * 250;
			RE_LVL_DMOD(100);
			break;
		case LG_HESPERUSLIT:
			skillratio += -100 + 120 * skill_lv;
			if(sc) {
				if(sc->data[SC_BANDING]) {
					skillratio += 200 * sc->data[SC_BANDING]->val2;
					if((battle_config.hesperuslit_bonus_stack && sc->data[SC_BANDING]->val2 >= 6) || sc->data[SC_BANDING]->val2 == 6)
						skillratio = skillratio * 150 / 100;
				}
				if(sc->data[SC_INSPIRATION])
					skillratio += 600;
			}
			RE_LVL_DMOD(100);
			break;
		case SR_DRAGONCOMBO:
			skillratio += 40 * skill_lv;
			RE_LVL_DMOD(100);
			break;
		case SR_SKYNETBLOW:
			//ATK [{(Skill Level x 100) + (Caster's AGI) + 150} x Caster's Base Level / 100] %
			if(wd.miscflag&8)
				skillratio += 100 * skill_lv + sstatus->agi + 50;
			else //ATK [{(Skill Level x 80) + (Caster's AGI)} x Caster's Base Level / 100] %
				skillratio += -100 + 80 * skill_lv + sstatus->agi;
			RE_LVL_DMOD(100);
			break;
		case SR_EARTHSHAKER:
			//[(Skill Level x 150) x (Caster's Base Level / 100) + (Caster's INT x 3)] %
			if(tsc && ((tsc->option&(OPTION_HIDE|OPTION_CLOAK)) || tsc->data[SC_CAMOUFLAGE])) {
				skillratio += -100 + 150 * skill_lv;
				RE_LVL_DMOD(100);
				skillratio += sstatus->int_ * 3;
			} else { //[(Skill Level x 50) x (Caster's Base Level / 100) + (Caster's INT x 2)] %
				skillratio += -100 + 50 * skill_lv;
				RE_LVL_DMOD(100);
				skillratio += sstatus->int_ * 2;
			}
			break;
		case SR_FALLENEMPIRE: //ATK [(Skill Level x 150 + 100) x Caster's Base Level / 150] %
			skillratio += 150 * skill_lv;
			RE_LVL_DMOD(150);
			break;
		case SR_TIGERCANNON: {
				int hp = sstatus->max_hp * (10 + 2 * skill_lv) / 100,
					sp = sstatus->max_sp * (5 + skill_lv) / 100;

				//ATK [((Caster's consumed HP + SP) / 2) x Caster's Base Level / 100] %
				if(wd.miscflag&8)
					skillratio += -100 + (hp + sp) / 2;
				else //ATK [((Caster's consumed HP + SP) / 4) x Caster's Base Level / 100] %
					skillratio += -100 + (hp + sp) / 4;
				RE_LVL_DMOD(100);
			}
			break;
		case SR_RAMPAGEBLASTER:
			if(sc && sc->data[SC_EXPLOSIONSPIRITS]) {
				skillratio += -100 + 20 * (sc->data[SC_EXPLOSIONSPIRITS]->val1 + skill_lv) * (sd ? sd->spiritball_old : MAX_SPIRITBALL);
				RE_LVL_DMOD(120);
			} else {
				skillratio += -100 + 20 * skill_lv * (sd ? sd->spiritball_old : MAX_SPIRITBALL);
				RE_LVL_DMOD(150);
			}
			break;
		case SR_KNUCKLEARROW:
			if(wd.miscflag&4) {
				//ATK [(Skill Level x 150) + (1000 x Target's current weight / Maximum weight) + (Target's Base Level x 5) x (Caster's Base Level / 150)] %
				skillratio += -100 + 150 * skill_lv + status_get_lv(target) * 5;
				if(tsd && tsd->weight)
					skillratio += 1000 * (tsd->weight / 10) / (tsd->max_weight / 10);
				RE_LVL_DMOD(150);
			} else { //ATK [(Skill Level x 100 + 500) x Caster's Base Level / 100] %
				skillratio += 400 + 100 * skill_lv;
				RE_LVL_DMOD(100);
			}
			break;
		case SR_WINDMILL:
			//ATK [(Caster's Base Level + Caster's DEX) x Caster's Base Level / 100] %
			skillratio += -100 + status_get_lv(src) + sstatus->dex;
			RE_LVL_DMOD(100);
			break;
		case SR_CRESCENTELBOW_AUTOSPELL:
			//ATK [{(Target's HP / 100) x Skill Level} x Caster's Base Level / 125] %
			skillratio += -100 + tstatus->hp / 100 * skill_lv;
			RE_LVL_DMOD(125);
			skillratio = min(5000,skillratio); //Maximum of 5000% ATK
			break;
		case SR_GATEOFHELL:
			if(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == SR_FALLENEMPIRE)
				skillratio += -100 + 800 * skill_lv;
			else
				skillratio += -100 + 500 * skill_lv;
			RE_LVL_DMOD(100);
			break;
		case SR_GENTLETOUCH_QUIET:
			skillratio += -100 + 100 * skill_lv + sstatus->dex;
			RE_LVL_DMOD(100);
			break;
		case SR_HOWLINGOFLION:
			skillratio += -100 + 300 * skill_lv;
			RE_LVL_DMOD(150);
			break;
		case SR_RIDEINLIGHTNING:
			//ATK [{(Skill Level x 200) + Additional Damage} x Caster's Base Level / 100] %
			skillratio += -100 + 200 * skill_lv;
			if((sstatus->rhw.ele) == ELE_WIND || (sstatus->lhw.ele) == ELE_WIND)
				skillratio += skill_lv * 50;
			RE_LVL_DMOD(100);
			break;
		case WM_REVERBERATION_MELEE:
			//ATK [{(Skill Level x 100) + 300} x Caster's Base Level / 100]
			skillratio += 200 + 100 * (sd ? pc_checkskill(sd,WM_REVERBERATION) : 5);
			RE_LVL_DMOD(100);
			break;
		case WM_SEVERE_RAINSTORM_MELEE:
			//ATK [{(Caster's DEX + AGI) x (Skill Level / 5)} x Caster's Base Level / 100] %
			skillratio += -100 + (sstatus->dex + sstatus->agi) * skill_lv / 5;
			RE_LVL_DMOD(100);
			break;
		case WM_GREAT_ECHO: {
				skillratio += 300 + 200 * skill_lv;

				if(skill_chorus_count(sd,0) == 1)
					skillratio += 100;
				else if(skill_chorus_count(sd,0) == 2)
					skillratio += 200;
				else if(skill_chorus_count(sd,0) == 3)
					skillratio += 400;
				else if(skill_chorus_count(sd,0) == 4)
					skillratio += 800;
				else if(skill_chorus_count(sd,0) == 5)
					skillratio += 1600;
			}
			RE_LVL_DMOD(100);
			break;
		case GN_CART_TORNADO:
			//ATK [( Skill Level x 50 ) + ( Cart Weight / ( 150 - Caster's Base STR ))] + ( Cart Remodeling Skill Level x 50 )] %
			skillratio += -100 + 50 * skill_lv;
			if(sd && sd->cart_weight) {
				short strbonus = sd->status.str; //Only using base STR

				skillratio += sd->cart_weight / 10 / (150 - min(strbonus,130));
			} else
				skillratio += 525;
			skillratio += 50 * (sd ? pc_checkskill(sd,GN_REMODELING_CART) : 5);
			break;
		case GN_CARTCANNON: //ATK [{( Cart Remodeling Skill Level x 50 ) x ( INT / 40 )} + ( Cart Cannon Skill Level x 60 )] %
			skillratio += -100 + 60 * skill_lv + 50 * (sd ? pc_checkskill(sd,GN_REMODELING_CART) : 5) * sstatus->int_ / 40;
			break;
		case GN_SPORE_EXPLOSION:
			skillratio += 100 + sstatus->int_ + 100 * skill_lv;
			RE_LVL_DMOD(100);
			if(wd.miscflag&16)
				skillratio = skillratio * 75 / 100;
			break;
		case GN_WALLOFTHORN:
			skillratio += 10 * skill_lv;
			break;
		case GN_CRAZYWEED_ATK:
			skillratio += 400 + 100 * skill_lv;
			break;
		case GN_SLINGITEM_RANGEMELEEATK:
			if(sd) {
				switch(sd->itemid) {
					case ITEMID_APPLE_BOMB:
						skillratio += 200 + sstatus->str + sstatus->dex;
						break;
					case ITEMID_COCONUT_BOMB:
					case ITEMID_PINEAPPLE_BOMB:
						skillratio += 700 + sstatus->str + sstatus->dex;
						break;
					case ITEMID_MELON_BOMB:
						skillratio += 400 + sstatus->str + sstatus->dex;
						break;
					case ITEMID_BANANA_BOMB:
						skillratio += 777 + sstatus->str + sstatus->dex;
						break;
					case ITEMID_BLACK_LUMP:
						skillratio += -100 + (sstatus->str + sstatus->agi + sstatus->dex) / 3;
						break;
					case ITEMID_BLACK_HARD_LUMP:
						skillratio += -100 + (sstatus->str + sstatus->agi + sstatus->dex) / 2;
						break;
					case ITEMID_VERY_HARD_LUMP:
						skillratio += -100 + sstatus->str + sstatus->agi + sstatus->dex;
						break;
				}
				RE_LVL_DMOD(100);
			}
			break;
		case SO_VARETYR_SPEAR:
			//ATK [{( Striking Level x 50 ) + ( Varetyr Spear Skill Level x 50 )} x Caster's Base Level / 100 ] %
			skillratio += -100 + 50 * skill_lv + 50 * (sd ? pc_checkskill(sd,SO_STRIKING) : 5);
			RE_LVL_DMOD(100);
			if(sc && sc->data[SC_BLAST_OPTION])
				skillratio += 5 * status_get_job_lv(src);
			break;
		//Physical Elemantal Spirits Attack Skills
		case EL_CIRCLE_OF_FIRE:
		case EL_FIRE_BOMB_ATK:
		case EL_STONE_RAIN:
			skillratio += 200;
			break;
		case EL_FIRE_WAVE_ATK:
			skillratio += 500;
			break;
		case EL_TIDAL_WEAPON:
			skillratio += 1400;
			break;
		case EL_WIND_SLASH:
			skillratio += 100;
			break;
		case EL_HURRICANE:
			skillratio += 600;
			break;
		case EL_TYPOON_MIS:
			skillratio += 900;
			break;
		case EL_STONE_HAMMER:
			skillratio += 400;
			break;
		case EL_ROCK_CRUSHER:
			skillratio += 700;
			break;
		case KO_JYUMONJIKIRI:
			skillratio += -100 + 150 * skill_lv;
			RE_LVL_DMOD(120);
			if(tsc && tsc->data[SC_JYUMONJIKIRI])
				skillratio += skill_lv * status_get_lv(src);
			break;
		case KO_HAPPOKUNAI:
			skillratio += -100 + 60 * (skill_lv + 5);
			break;
		case KO_HUUMARANKA:
			skillratio += -100 + 150 * skill_lv + sstatus->agi + sstatus->dex + 100 * (sd ? pc_checkskill(sd,NJ_HUUMA) : 5);
			break;
		case KO_SETSUDAN:
			skillratio += 100 * (skill_lv - 1);
			RE_LVL_DMOD(100);
			if(tsc && tsc->data[SC_SPIRIT])
				skillratio += 200 * tsc->data[SC_SPIRIT]->val1;
			break;
		case KO_BAKURETSU:
			skillratio += -100 + (50 + sstatus->dex / 4) * (sd ? pc_checkskill(sd,NJ_TOBIDOUGU) : 10) * 4 * skill_lv / 10;
			RE_LVL_DMOD(120);
			skillratio += 10 * status_get_job_lv(src);
			break;
		case KO_MAKIBISHI:
			skillratio += -100 + 20 * skill_lv;
			break;
		case MH_NEEDLE_OF_PARALYZE:
			skillratio += 600 + 100 * skill_lv;
			break;
		case MH_STAHL_HORN:
			skillratio += 400 + 100 * skill_lv * status_get_lv(src) / 150;
			break;
		case MH_LAVA_SLIDE:
			skillratio += -100 + (10 * skill_lv + status_get_lv(src)) * 2 * status_get_lv(src) / 100;
			break;
		case MH_SONIC_CRAW:
			skillratio += -100 + 40 * skill_lv * status_get_lv(src) / 150;
			break;
		case MH_SILVERVEIN_RUSH:
			skillratio += -100 + 150 * skill_lv * status_get_lv(src) / 100;
			break;
		case MH_MIDNIGHT_FRENZY:
			skillratio += -100 + 300 * skill_lv * status_get_lv(src) / 150;
			break;
		case MH_TINDER_BREAKER:
			skillratio += -100 + (100 * skill_lv + 3 * sstatus->str) * status_get_lv(src) / 120;
			break;
		case MH_CBC:
			skillratio += 300 * skill_lv + 4 * status_get_lv(src);
			break;
		case MH_MAGMA_FLOW:
			skillratio += -100 + (100 * skill_lv + 3 * status_get_lv(src)) * status_get_lv(src) / 120;
			break;
		case RL_MASS_SPIRAL:
			skillratio += -100 + 200 * skill_lv;
			break;
		case RL_FIREDANCE:
			skillratio += -100 + 200 * skill_lv + 50 * (sd ? pc_checkskill(sd,GS_DESPERADO) : 10);
			break;
		case RL_BANISHING_BUSTER:
			skillratio += 1900 + 300 * skill_lv;
			break;
		case RL_S_STORM:
			skillratio += 1600 + 200 * skill_lv;
			break;
		case RL_SLUGSHOT:
			if(target->type == BL_PC)
				skillratio += -100 + 2000 * skill_lv;
			else
				skillratio += -100 + 1200 * skill_lv;
			skillratio *= 2 + tstatus->size;
			break;
		case RL_D_TAIL:
			skillratio += 3900 + 1000 * skill_lv;
			break;
		case RL_R_TRIP:
			skillratio += 900 + 300 * skill_lv;
			break;
		case RL_R_TRIP_PLUSATK:
			skillratio += 400 + 100 * skill_lv;
			break;
		case RL_H_MINE:
			skillratio += 100 + 200 * skill_lv;
			if(wd.miscflag&16) //Explode bonus damage
				skillratio += 500 + 300 * skill_lv;
			break;
		case RL_HAMMER_OF_GOD:
			skillratio += 2700 + 1400 * skill_lv;
			if(tsc && tsc->data[SC_C_MARKER])
				skillratio += 100 * (sd ? sd->spiritball_old : 10);
			else
				skillratio += 10 * (sd ? sd->spiritball_old : 10);
			break;
		case RL_FIRE_RAIN:
		case RL_AM_BLAST:
			skillratio += 3400 + 300 * skill_lv;
			break;
		case SU_BITE:
			skillratio += 100;
			if(status_get_hp(target) <= 70 * status_get_max_hp(target) / 100)
				skillratio += 100;
			break;
		case SU_SCRATCH:
			skillratio += -50 + 50 * skill_lv;
			break;
		case SU_SCAROFTAROU:
			skillratio += -100 + 100 * skill_lv;
			if(sd && pc_checkskill(sd,SU_SPIRITOFLIFE) > 0)
				skillratio += (skillratio + skillratio * 20 / 100) * status_get_hp(src) / status_get_max_hp(src);
			if(status_get_class_(target) == CLASS_BOSS)
				skillratio *= 2;
			break;
		case SU_PICKYPECK:
		case SU_PICKYPECK_DOUBLE_ATK:
			skillratio += 100 + 100 * skill_lv;
			if(skill_id == SU_PICKYPECK_DOUBLE_ATK)
				skillratio *= 2;
			if(sd && pc_checkskill(sd,SU_SPIRITOFLIFE) > 0)
				skillratio += (skillratio + skillratio * 20 / 100) * status_get_hp(src) / status_get_max_hp(src);
			break;
		case SU_LUNATICCARROTBEAT:
		case SU_LUNATICCARROTBEAT2:
			skillratio += 100 + 100 * skill_lv;
			if(sd && pc_checkskill(sd,SU_SPIRITOFLIFE) > 0)
				skillratio += (skillratio + skillratio * 20 / 100) * status_get_hp(src) / status_get_max_hp(src);
			break;
		case SU_SVG_SPIRIT:
			skillratio += 150 + 150 * skill_lv;
			if(sd && pc_checkskill(sd,SU_SPIRITOFLIFE) > 0)
				skillratio += (skillratio + skillratio * 20 / 100) * status_get_hp(src) / status_get_max_hp(src);
			break;
	}
	return skillratio;
}

/*==============================================================================
 * Constant skill damage additions are added after skill level ratio calculation
 *------------------------------------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static int64 battle_calc_skill_constant_addition(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_data *sstatus = status_get_status_data(src);
	int64 atk = 0;

	switch(skill_id) {
		case MO_EXTREMITYFIST: //[malufett]
			atk = 250 * (skill_lv + 1) + 10 * (sstatus->sp + 1) * wd.damage / 100 + 8 * wd.damage;
			break;
#ifndef RENEWAL
		case GS_MAGICALBULLET:
			atk = status_get_matk(src, 2);
			break;
#endif
		case PA_SHIELDCHAIN: {
				short index = -1;

				if(sd && (index = sd->equip_index[EQI_HAND_L]) >= 0 &&
					sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
					atk = (375 + skill_lv * 125) / 100 * sd->inventory.u.items_inventory[index].refine;
			}
			break;
		case NJ_SYURIKEN:
			atk = 4 * skill_lv;
			break;
#ifdef RENEWAL
		case NJ_ISSEN: {
				//Official renewal formula [helvetica]
				//Base damage = CurrentHP + ((ATK * CurrentHP * Skill level) / MaxHP)
				//Final damage = Base damage + ((Mirror Image count + 1) / 5 * Base damage) - (eDEF + sDEF)
				struct status_change *sc = status_get_sc(src);
				int i;

				atk = sstatus->hp + wd.damage * sstatus->hp * skill_lv / sstatus->max_hp;
				//Mirror Image bonus only occurs if active
				if(sc && sc->data[SC_BUNSINJYUTSU] && (i = sc->data[SC_BUNSINJYUTSU]->val2) > 0)
					atk += (atk * (((i + 1) * 10) / 5)) / 10;
			}
			break;
		case HT_FREEZINGTRAP:
			atk = 40 * (sd ? pc_checkskill(sd, RA_RESEARCHTRAP) : 5);
			break;
		case HW_MAGICCRASHER: {
				//Official renewal formula [exneval]
				//Magical Damage = (MATK - (eMDEF + sMDEF)) / 5
				//Damage = [(Physical Damage + Magical Damage + Refinement Damage) - (eDEF + sDEF)] * Attribute modifiers
				struct status_data *tstatus = status_get_status_data(target);
				short totalmdef = tstatus->mdef + tstatus->mdef2;

				atk += (status_get_matk(src, 2) - totalmdef) / 5;
			}
			break;
#endif
		case GC_COUNTERSLASH:
			atk = 2 * sstatus->agi + 4 * status_get_job_lv(src);
			break;
	}
	return atk;
}

/**
 * Cannon ball attack calculation
 */
struct Damage battle_attack_cannonball(struct Damage wd, struct block_list *src, uint16 skill_id)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	short index = -1;

	if(sd && (index = sd->equip_index[EQI_AMMO]) >= 0 && sd->inventory_data[index] &&
		sd->inventory_data[index]->look == AMMO_CANNONBALL && is_skill_using_arrow(src, skill_id)) {
		ATK_ADD(wd.damage, wd.damage2, sd->inventory_data[index]->atk);
#ifdef RENEWAL
		ATK_ADD(wd.equipAtk, wd.equipAtk2, sd->inventory_data[index]->atk);
#endif
	}
	return wd;
}

/*==============================================================
 * Stackable SC bonuses added on top of calculated skill damage
 *--------------------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_attack_sc_bonus(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct status_change_entry *sce = NULL;
	int inf3 = skill_get_inf3(skill_id);

	if(sc && sc->count) { //The following are applied on top of current damage and are stackable
		if((sce = sc->data[SC_ATKPOTION])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val1);
#ifdef RENEWAL
			ATK_ADD(wd.statusAtk, wd.statusAtk2, sce->val1);
#endif
		}
		if((sce = sc->data[SC_QUEST_BUFF1])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val1);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val1);
#endif
		}
		if((sce = sc->data[SC_QUEST_BUFF2])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val1);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val1);
#endif
		}
		if((sce = sc->data[SC_QUEST_BUFF3])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val1);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val1);
#endif
		}
		if(sc->data[SC_ALMIGHTY]) {
			ATK_ADD(wd.damage, wd.damage2, 30);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, 30);
#endif
		}
		if((sce = sc->data[SC_FIGHTINGSPIRIT])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val1);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val1);
#endif
		}
		if((sce = sc->data[SC_GT_CHANGE])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val2);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val2);
#endif
		}
		if((sce = sc->data[SC_WATER_BARRIER])) {
			ATK_ADD(wd.damage, wd.damage2, -sce->val2);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, -sce->val2);
#endif
		}
		if((sce = sc->data[SC_PYROTECHNIC_OPTION])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val2);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val2);
#endif
		}
		if((sce = sc->data[SC_HEATER_OPTION])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val2);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val2);
#endif
		}
		if((sce = sc->data[SC_TROPIC_OPTION])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val2);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val2);
#endif
		}
		if((sce = sc->data[SC_DANCEWITHWUG]) && sd &&
			((sd->class_&MAPID_THIRDMASK) == MAPID_RANGER ||
			(sd->class_&MAPID_THIRDMASK) == MAPID_MINSTRELWANDERER))
		{
			ATK_ADD(wd.damage, wd.damage2, 2 * sce->val1 * skill_chorus_count(sd,1));
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, 2 * sce->val1 * skill_chorus_count(sd,1));
#endif
		}
		if((sce = sc->data[SC_SATURDAYNIGHTFEVER])) {
			ATK_ADD(wd.damage, wd.damage2, 100 * sce->val1);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, 100 * sce->val1);
#endif
		}
		if((sce = sc->data[SC_ZENKAI])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val3);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val3);
#endif
		}
		if((sce = sc->data[SC_ZANGETSU])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val2);
#ifdef RENEWAL
			ATK_ADD(wd.statusAtk, wd.statusAtk2, sce->val2);
#endif
		}
		if((sce = sc->data[SC_P_ALTER])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val2);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val2);
#endif
		}
		if(sc->data[SC_STYLE_CHANGE]) {
			TBL_HOM *hd = BL_CAST(BL_HOM, src);

			if(hd)
				ATK_ADD(wd.damage, wd.damage2, hd->homunculus.spiritball * 3);
		}
		if((sce = sc->data[SC_FLASHCOMBO])) {
			ATK_ADD(wd.damage, wd.damage2, sce->val2);
#ifdef RENEWAL
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val2);
#endif
		}
#ifdef RENEWAL
		if((sce = sc->data[SC_VOLCANO]))
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val2);
		if((sce = sc->data[SC_DRUMBATTLE]))
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val2);
		if(sc->data[SC_MADNESSCANCEL])
			ATK_ADD(wd.equipAtk, wd.equipAtk2, 100);
		if((sce = sc->data[SC_GATLINGFEVER]))
			ATK_ADD(wd.equipAtk, wd.equipAtk2, sce->val3);
		if((sce = sc->data[SC_WATK_ELEMENT]) && skill_id != ASC_METEORASSAULT)
			ATK_ADDRATE(wd.weaponAtk, wd.weaponAtk2, sce->val2);
#else
		if((sce = sc->data[SC_TRUESIGHT]))
			ATK_ADDRATE(wd.damage, wd.damage2, 2 * sce->val1);
#endif
		if((sce = sc->data[SC_EDP])) {
			switch(skill_id) {
				case AS_SPLASHER:
				case ASC_METEORASSAULT:
				//Pre-Renewal only: Soul Breaker ignores EDP 
				//Renewal only: Grimtooth and Venom Knife ignore EDP
				//Both: Venom Splasher and Meteor Assault ignore EDP [helvetica]
#ifndef RENEWAL
				case ASC_BREAKER:
#else
				case AS_GRIMTOOTH:
				case AS_VENOMKNIFE:
#endif
					break; //Skills above have no effect with EDP
#ifdef RENEWAL
				//Renewal EDP mode requires renewal enabled as well
				//Renewal EDP: damage gets a half modifier on top of EDP bonus for skills [helvetica]
				//* Sonic Blow
				//* Soul Breaker
				//* Counter Slash
				//* Cross Impact
				case AS_SONICBLOW:
				case ASC_BREAKER:
				case GC_COUNTERSLASH:
				case GC_CROSSIMPACT:
					//Only modifier is halved but still benefit with the damage bonus
					ATK_RATE(wd.weaponAtk, wd.weaponAtk2, 50);
					ATK_RATE(wd.equipAtk, wd.equipAtk2, 50);
				//Fall through to apply EDP bonuses
				default:
					//Renewal EDP formula [helvetica]
					//Weapon atk * (1 + (edp level * .8))
					//Equip atk * (1 + (edp level * .6))
					ATK_RATE(wd.weaponAtk, wd.weaponAtk2, 100 + sce->val1 * 80);
					ATK_RATE(wd.equipAtk, wd.equipAtk2, 100 + sce->val1 * 60);
#else
				default:
					ATK_ADDRATE(wd.damage, wd.damage2, sce->val3);
#endif
					break;
			}
		}
		if(sd) {
			if((sce = sc->data[SC_INCATKRATE])) {
				ATK_ADDRATE(wd.damage, wd.damage2, sce->val1);
				RE_ALLATK_ADDRATE(wd, sce->val1);
			}
			if((sce = sc->data[SC_CATNIPPOWDER])) {
				ATK_ADDRATE(wd.damage, wd.damage2, -sce->val2);
				RE_ALLATK_ADDRATE(wd, -sce->val2);
			}
		}
		if(skill_id == AS_SONICBLOW && (sce = sc->data[SC_SPIRIT]) && sce->val2 == SL_ASSASIN) {
			ATK_ADDRATE(wd.damage, wd.damage2, (map_flag_gvg2(src->m) ? 25 : 100));
			RE_ALLATK_ADDRATE(wd, (map_flag_gvg2(src->m) ? 25 : 100));
		}
		if(skill_id == CR_SHIELDBOOMERANG && (sce = sc->data[SC_SPIRIT]) && sce->val2 == SL_CRUSADER) {
			ATK_ADDRATE(wd.damage, wd.damage2, 100);
			RE_ALLATK_ADDRATE(wd, 100);
		}
		if((sce = sc->data[SC_GLOOMYDAY]) && (inf3&INF3_SC_GLOOMYDAY)) {
			ATK_ADDRATE(wd.damage, wd.damage2, sce->val4);
			RE_ALLATK_ADDRATE(wd, sce->val4);
		}
		if((sce = sc->data[SC_DANCEWITHWUG]) && (inf3&INF3_SC_DANCEWITHWUG)) {
			ATK_ADDRATE(wd.damage, wd.damage2, 10 * sce->val1 * skill_chorus_count(sd,1));
			RE_ALLATK_ADDRATE(wd, 10 * sce->val1 * skill_chorus_count(sd,1));
		}
		if((sce = sc->data[SC_EQC])) {
			ATK_ADDRATE(wd.damage, wd.damage2, -sce->val2);
#ifdef RENEWAL
			ATK_ADDRATE(wd.equipAtk, wd.equipAtk2, -sce->val2);
#endif
		}
		if((sce = sc->data[SC_SHRIMP])) {
			ATK_ADDRATE(wd.damage, wd.damage2, sce->val2);
#ifdef RENEWAL
			ATK_ADDRATE(wd.equipAtk, wd.equipAtk2, sce->val2);
#endif
		}
		if((sce = sc->data[SC_UNLIMIT]) && wd.flag&BF_LONG && (!skill_id || (inf3&INF3_SC_UNLIMIT))) {
			ATK_ADDRATE(wd.damage, wd.damage2, sce->val2);
			RE_ALLATK_ADDRATE(wd, sce->val2);
		}
		if(!skill_id) {
#ifdef RENEWAL
			if((sce = sc->data[SC_MAGICALBULLET])) {
				int64 dmg = status_get_matk(src, 2);
				short totalmdef = tstatus->mdef + tstatus->mdef2;

				if((dmg = dmg - totalmdef) > 0)
					ATK_ADD(wd.weaponAtk, wd.weaponAtk2, dmg);
			}
#endif
			if((sce = sc->data[SC_ENCHANTBLADE])) { //[((Skill Level x 20) + 100) x (Caster's Base Level / 150)] + Caster's INT + MATK - MDEF - MDEF2
				int64 dmg = sce->val2 + status_get_matk(src, 2);
				short totalmdef = tstatus->mdef + tstatus->mdef2;

				if((dmg = dmg - totalmdef) > 0) {
					ATK_ADD(wd.damage, wd.damage2, dmg);
#ifdef RENEWAL
					ATK_ADD(wd.weaponAtk, wd.weaponAtk2, dmg);
#endif
				}
			}
			if((sce = sc->data[SC_GIANTGROWTH]) && rnd()%100 < sce->val2) {
				ATK_ADDRATE(wd.damage, wd.damage2, 100);
				RE_ALLATK_ADDRATE(wd, 100);
				skill_break_equip(src, src, EQP_WEAPON, 10, BCT_SELF); //Break chance happens on successful damage increase
			}
			if((sce = sc->data[SC_EXEEDBREAK])) {
				ATK_ADDRATE(wd.damage, wd.damage2, sce->val2);
				RE_ALLATK_ADDRATE(wd, sce->val2);
			}
		}
	}
	return wd;
}

/**
 * Calculates defense based on src and target
 * @param src: Source object
 * @param target: Target object
 * @param skill_id: Skill used
 * @param flag: 0 - Return armor defense, 1 - Return status defense
 * @return defense
 */
short battle_get_defense(struct block_list *src, struct block_list *target, uint16 skill_id, uint8 flag)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);

	//Don't use tstatus->def1 due to skill timer reductions
	defType def1 = status_get_def(target); //eDEF
	short def2 = tstatus->def2, vit_def; //sDEF

	def1 = status_calc_def(target, tsc, def1, false);
	def2 = status_calc_def2(target, tsc, def2, false);

	if(tsd && tsd->charmball_type == CHARM_TYPE_LAND && tsd->charmball > 0) {
		uint8 i = 10 * tsd->charmball; //KO Earth Charm effect +10% eDEF

#ifdef RENEWAL
		def1 = def1 * (100 + i) / 100;
#else
		def1 = def1 * (100 + i / 2) / 100;
#endif
	}
	if(sd) {
		int val = sd->ignore_def_by_race[tstatus->race] + sd->ignore_def_by_race[RC_ALL];

		val += sd->ignore_def_by_class[tstatus->class_] + sd->ignore_def_by_class[CLASS_ALL];
		if(val) {
			val = min(val, 100); //Cap it to 100 for 0 DEF min
			def1 = def1 * (100 - val) / 100;
#ifndef RENEWAL
			def2 = def2 * (100 - val) / 100;
#endif
		}
	}
	if(sc && sc->data[SC_EXPIATIO]) {
		uint8 i = 5 * sc->data[SC_EXPIATIO]->val1; //5% per level

		i = u8min(i, 100);
		def1 = def1 * (100 - i) / 100;
#ifndef RENEWAL
		def2 = def2 * (100 - i) / 100;
#endif
	}
	if(battle_config.vit_penalty_type && battle_config.vit_penalty_target&target->type) {
		unsigned char target_count; //256 max targets should be a sane max

		//Official servers limit the count to 22 targets
		target_count = min(unit_counttargeted(target), (100 / battle_config.vit_penalty_num) + (battle_config.vit_penalty_count - 1));
		if(target_count >= battle_config.vit_penalty_count) {
			if(battle_config.vit_penalty_type == 1) {
				if(!tsc || !tsc->data[SC_STEELBODY])
					def1 = def1 * (100 - (target_count - (battle_config.vit_penalty_count - 1)) * battle_config.vit_penalty_num) / 100;
				def2 = def2 * (100 - (target_count - (battle_config.vit_penalty_count - 1)) * battle_config.vit_penalty_num) / 100;
			} else { //Assume type 2
				if(!tsc || !tsc->data[SC_STEELBODY])
					def1 -= (target_count - (battle_config.vit_penalty_count - 1)) * battle_config.vit_penalty_num;
				def2 -= (target_count - (battle_config.vit_penalty_count - 1)) * battle_config.vit_penalty_num;
			}
		}
#ifndef RENEWAL
		if(skill_id == AM_ACIDTERROR)
			def1 = 0; //Ignores eDEF [Skotlex]
#endif
		def2 = max(def2,1);
	}
	//Vitality reduction
	//[rodatazone: http://rodatazone.simgaming.net/mechanics/substats.php#def]
	if(tsd) { //sd vit-eq
		uint16 lv;

#ifndef RENEWAL
		//[VIT*0.5] + rnd([VIT*0.3], max([VIT*0.3], [VIT^2/150]-1))
		vit_def = def2 * (def2 - 15) / 150;
		vit_def = def2 / 2 + (vit_def > 0 ? rnd()%vit_def : 0);
#else
		vit_def = def2;
#endif
		//This bonus already doesn't work vs players
		if(src->type == BL_MOB && (battle_check_undead(sstatus->race, sstatus->def_ele) || sstatus->race == RC_DEMON) &&
			(lv = pc_checkskill(tsd, AL_DP)) > 0)
			vit_def += lv * (int)(3 + ((float)((tsd->status.base_level + 1) * 4) / 100)); //[orn]
		if(src->type == BL_MOB && (lv = pc_checkskill(tsd, RA_RANGERMAIN)) > 0 &&
			(sstatus->race == RC_BRUTE || sstatus->race == RC_FISH || sstatus->race == RC_PLANT))
			vit_def += lv * 5;
		if(src->type == BL_MOB && (lv = pc_checkskill(tsd, NC_RESEARCHFE)) > 0 &&
			(sstatus->def_ele == ELE_FIRE || sstatus->def_ele == ELE_EARTH))
			vit_def += lv * 10;
	} else { //Mob-Pet vit-eq
#ifndef RENEWAL
		//VIT + rnd(0, [VIT / 20] ^ 2 - 1)
		vit_def = (def2 / 20) * (def2 / 20);
		if(tsc && tsc->data[SC_SKA])
			vit_def += 100; //Eska increases the random part of the formula by 100
		vit_def = def2 + (vit_def > 0 ? rnd()%vit_def : 0);
#else
		//SoftDEF of monsters is floor((BaseLevel+Vit)/2)
		vit_def = def2;
#endif
	}
	if(battle_config.weapon_defense_type) {
		vit_def += def1 * battle_config.weapon_defense_type;
		def1 = 0;
	}

	return (flag ? vit_def : (short)def1);
}

/**
 * Calculate defense damage reduction
 * @param wd: Weapon data
 * @param src: Source object
 * @param target: Target object
 * @param skill_id: Skill used
 * @param skill_lv: Skill level used
 * @return weapon data
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_defense_reduction(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	short def1 = battle_get_defense(src, target, skill_id, 0);
	short vit_def = battle_get_defense(src, target, skill_id, 1);

	if(attack_ignores_def(wd, src, target, skill_id, skill_lv, EQI_HAND_R) || attack_ignores_def(wd, src, target, skill_id, skill_lv, EQI_HAND_L))
		return wd;
#ifdef RENEWAL
	if(is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_R) || is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_L))
		return wd;
	switch(skill_id) {
		case MO_EXTREMITYFIST:
		case CR_SHIELDBOOMERANG:
		case PA_SHIELDCHAIN:
		case HW_MAGICCRASHER:
		case NJ_KUNAI:
		case NJ_ISSEN:
		case RK_DRAGONBREATH:
		case RK_DRAGONBREATH_WATER:
		case NC_SELFDESTRUCTION:
		case GN_CARTCANNON:
		case KO_HAPPOKUNAI:
			//Total defense reduction
			wd.damage -= (def1 + vit_def);
			if(is_attack_left_handed(src, skill_id))
				wd.damage2 -= (def1 + vit_def);
			break;
		default:
			/**
			 * RE DEF Reduction
			 * Damage = Attack * (4000 + eDEF) / (4000 + eDEF * 10) - sDEF
			 */
			if(def1 < -399) //It stops at -399
				def1 = 399; //In Aegis it set to 1 but in our case it may lead to exploitation so limit it to 399
			wd.damage = wd.damage * (4000 + def1) / (4000 + 10 * def1) - vit_def;
			if(is_attack_left_handed(src, skill_id))
				wd.damage2 = wd.damage2 * (4000 + def1) / (4000 + 10 * def1) - vit_def;
			break;
	}
#else
	if(def1 > 100)
		def1 = 100;
	ATK_RATE2(wd.damage, wd.damage2,
		(is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_R) ? (def1 + vit_def) : (100 - def1)),
		(is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_L) ? (def1 + vit_def) : (100 - def1))
	);
	ATK_ADD2(wd.damage, wd.damage2,
		(is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_R) ? 0 : -vit_def),
		(is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_L) ? 0 : -vit_def)
	);
#endif

	return wd;
}

/*====================================
 * Modifiers ignoring DEF
 *------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_attack_post_defense(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
#ifndef RENEWAL
	struct status_data *sstatus = status_get_status_data(src);

	wd = battle_calc_attack_masteries(wd, src, target, skill_id, skill_lv);
	if(battle_skill_stacks_masteries_vvs(skill_id)) { //Refine bonus
		if(skill_id == MO_FINGEROFFENSIVE) { //Counts refine bonus multiple times
			ATK_ADD2(wd.damage, wd.damage2, wd.div_ * sstatus->rhw.atk2, wd.div_ * sstatus->lhw.atk2);
		} else
			ATK_ADD2(wd.damage, wd.damage2, sstatus->rhw.atk2, sstatus->lhw.atk2);
	}
#endif
	//Set to min of 1
	if(is_attack_right_handed(src, skill_id) && wd.damage < 1)
		wd.damage = 1;
	if(is_attack_left_handed(src, skill_id) && wd.damage2 < 1)
		wd.damage2 = 1;

	return wd;
}

/**
 * Calculate weapon damage from status and mapflag modifiers
 * @param wd: Weapon data
 * @param src: Source object
 * @param target: Target object
 * @param skill_id: Skill used
 * @param skill_lv: Skill level used
 * @return weapon data
 */
struct Damage battle_calc_damage_modifiers(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	if( !wd.damage2 ) {
		wd.damage = battle_calc_damage(src, target, &wd, wd.damage, skill_id, skill_lv);
		if( map_flag_gvg2(target->m) )
			wd.damage = battle_calc_gvg_damage(src, target, wd.damage, skill_id, wd.flag);
		else if( map[target->m].flag.battleground )
			wd.damage = battle_calc_bg_damage(src, target, wd.damage, skill_id, wd.flag);
	} else if( !wd.damage ) {
		wd.damage2 = battle_calc_damage(src, target, &wd, wd.damage2, skill_id, skill_lv);
		if( map_flag_gvg2(target->m) )
			wd.damage2 = battle_calc_gvg_damage(src, target, wd.damage2, skill_id, wd.flag);
		else if( map[target->m].flag.battleground )
			wd.damage2 = battle_calc_bg_damage(src, target, wd.damage2, skill_id, wd.flag);
	} else {
		int64 d1 = wd.damage + wd.damage2, d2 = wd.damage2;

		wd.damage = battle_calc_damage(src, target, &wd, d1, skill_id, skill_lv);
		if( map_flag_gvg2(target->m) )
			wd.damage = battle_calc_gvg_damage(src, target, wd.damage, skill_id, wd.flag);
		else if( map[target->m].flag.battleground )
			wd.damage = battle_calc_bg_damage(src, target, wd.damage, skill_id, wd.flag);
		wd.damage2 = d2 * 100 / d1 * wd.damage / 100;
		if( wd.damage > 1 && wd.damage2 < 1 )
			wd.damage2 = 1;
		wd.damage -= wd.damage2;
	}

	return wd;
}

/*=================================================================================
 * "Plant"-type (mobs that only take 1 damage from all sources) damage calculation
 *---------------------------------------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_attack_plant(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);

	if(sc && sc->data[SC_CAMOUFLAGE] && skill_id != SN_SHARPSHOOTING && skill_id != RA_ARROWSTORM)
		status_change_end(src, SC_CAMOUFLAGE, INVALID_TIMER);
	//Plants receive 1 damage when hit
	if(wd.damage > 0)
		wd.damage = 1;
	if(is_attack_left_handed(src, skill_id) && wd.damage2 > 0) {
		if(sd->status.weapon == W_KATAR)
			wd.damage2 = 0; //No backhand damage against plants
		else
			wd.damage2 = 1; //Deal 1 HP damage as long as there is a weapon in the left hand
	}
	//For plants we don't continue with the weapon attack code, so we have to apply DAMAGE_DIV_FIX here
	wd = battle_apply_div_fix(wd, skill_id);
	//If there is left hand damage, total damage can never exceed 2, even on multiple hits
	if(wd.damage > 1 && wd.damage2 > 0) {
		wd.damage = 1;
		wd.damage2 = 1;
	}
	wd = battle_calc_damage_modifiers(wd, src, target, skill_id, skill_lv);

	return wd;
}

/*========================================================================================
 * Perform left/right hand weapon damage calculation based on previously calculated damage
 *----------------------------------------------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_attack_left_right_hands(struct Damage wd, struct block_list *src,struct block_list *target,uint16 skill_id,uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);

	if(sd) {
		uint16 lv;

		if(!is_attack_right_handed(src, skill_id) && is_attack_left_handed(src, skill_id)) {
			wd.damage = wd.damage2;
			wd.damage2 = 0;
		} else if(sd->status.weapon == W_KATAR && !skill_id) { //Katar (off hand damage only applies to normal attacks, tested on Aegis 10.2)
			lv = pc_checkskill(sd, TF_DOUBLE);
			wd.damage2 = wd.damage * (1 + lv * 2) / 100;
		} else if(is_attack_right_handed(src, skill_id) && is_attack_left_handed(src, skill_id)) { //Dual-wield
			if(wd.damage > 0) {
				if((sd->class_&MAPID_BASEMASK) == MAPID_THIEF) {
					lv = pc_checkskill(sd,AS_RIGHT);
					ATK_RATER(wd.damage, 50 + lv * 10);
				} else if((sd->class_&MAPID_UPPERMASK) == MAPID_KAGEROUOBORO) {
					lv = pc_checkskill(sd,KO_RIGHT);
					ATK_RATER(wd.damage, 70 + lv * 10);
				}
				wd.damage = i64max(wd.damage, 1);
			}
			if(wd.damage2 > 0) {
				if((sd->class_&MAPID_BASEMASK) == MAPID_THIEF) {
					lv = pc_checkskill(sd,AS_LEFT);
					ATK_RATEL(wd.damage2, 30 + lv * 10);
				} else if((sd->class_&MAPID_UPPERMASK) == MAPID_KAGEROUOBORO) {
					lv = pc_checkskill(sd,KO_LEFT);
					ATK_RATEL(wd.damage2, 50 + lv * 10);
				}
				wd.damage2 = i64max(wd.damage2, 1);
			}
		}
	}

	if(!is_attack_right_handed(src, skill_id) && !is_attack_left_handed(src, skill_id) && wd.damage > 0)
		wd.damage = 0;

	if(!is_attack_left_handed(src, skill_id) && wd.damage2 > 0)
		wd.damage2 = 0;

	return wd;
}

/**
 * Check if bl is devoted by someone
 * @param bl
 * @return 'd_bl' if devoted or NULL if not devoted
 */
struct block_list *battle_check_devotion(struct block_list *bl) {
	struct block_list *d_bl = NULL;

	if(battle_config.devotion_rdamage && rnd()%100 < battle_config.devotion_rdamage) {
		struct status_change *sc = NULL;

		if((sc = status_get_sc(bl)) && sc->data[SC_DEVOTION])
			d_bl = map_id2bl(sc->data[SC_DEVOTION]->val1);
	}
	return d_bl;
}

/*==========================================
 * BG/GvG attack modifiers
 *------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_attack_gvg_bg(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	if( wd.damage + wd.damage2 > 0 ) { //There is a total damage value
		if( src && target && target->id != src->id && //Don't reflect your own damage
			(src->type != BL_SKILL || skill_id == SG_SUN_WARM || skill_id == SG_MOON_WARM || skill_id == SG_STAR_WARM) ) {
			int64 damage = wd.damage + wd.damage2, rdamage = 0;
			struct map_session_data *tsd = BL_CAST(BL_PC, target);
			struct status_data *sstatus = status_get_status_data(src);
			int tick = gettick(), rdelay = 0;

			rdamage = battle_calc_return_damage(target, src, &damage, wd.flag, skill_id, false);
			if( rdamage > 0 ) { //Item reflect gets calculated before any mapflag reducing is applicated
				struct block_list *d_bl = battle_check_devotion(src);

				rdelay = clif_damage(src, (!d_bl ? src : d_bl), tick, wd.amotion, sstatus->dmotion, rdamage, 1, DMG_ENDURE, 0, false);
				if( tsd )
					battle_drain(tsd, src, rdamage, rdamage, sstatus->race, sstatus->class_, false);
				//Use Reflect Shield to signal this kind of skill trigger [Skotlex]
				battle_delay_damage(tick, wd.amotion, target, (!d_bl ? src : d_bl), 0, CR_REFLECTSHIELD, 0, rdamage, ATK_DEF, rdelay, true, false, false);
				skill_additional_effect(target, (!d_bl ? src : d_bl), CR_REFLECTSHIELD, 1, BF_WEAPON|BF_SHORT|BF_NORMAL, ATK_DEF, tick);
			}
		}
		wd = battle_calc_damage_modifiers(wd, src, target, skill_id, skill_lv);
	}
	return wd;
}

/*==========================================
 * Final ATK modifiers - After BG/GvG calc
 *------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_weapon_final_atk_modifiers(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_change_entry *sce = NULL;
#ifdef ADJUST_SKILL_DAMAGE
	int skill_damage = 0;
#endif
	int bonus_damage = 0;

	if(sc) {
		if(sc->data[SC_FUSION]) { //SC_FUSION HP penalty [Komurka]
			unsigned int hp = sstatus->max_hp;

			if(sd && tsd) {
				hp /= 13;
				if(sstatus->hp * 100 <= sstatus->max_hp * 20)
					hp = sstatus->hp;
			} else
				hp = hp * 2 / 100; //2% HP loss per hit
			status_zap(src, hp, 0);
		}
		if(sc->data[SC_CAMOUFLAGE] && skill_id != SN_SHARPSHOOTING && skill_id != RA_ARROWSTORM)
			status_change_end(src, SC_CAMOUFLAGE, INVALID_TIMER);
	}

	if(wd.damage > 0 && tsc) {
		if((sce = tsc->data[SC_REJECTSWORD]) && (!sd ||
			sd->weapontype1 == W_DAGGER ||
			sd->weapontype1 == W_1HSWORD ||
			sd->status.weapon == W_2HSWORD) && rnd()%100 < sce->val2)
		{ //Reject Sword bugreport:4493 by Daegaladh
			ATK_RATER(wd.damage, 50);
			status_fix_damage(target, src, wd.damage, clif_damage(target, src, gettick(), 0, 0, wd.damage, 0, DMG_NORMAL, 0, false));
			if(--(sce->val3) <= 0)
				status_change_end(target, SC_REJECTSWORD, INVALID_TIMER);
		}
		if((sce = tsc->data[SC_CRESCENTELBOW]) && wd.flag&BF_SHORT &&
			!status_bl_has_mode(src, MD_STATUS_IMMUNE) && rnd()%100 < sce->val2) {
			battle_damage_temp[0] = wd.damage; //Will be used for bonus part formula [exneval]
			clif_skill_nodamage(target, src, SR_CRESCENTELBOW_AUTOSPELL, sce->val1, 1);
			skill_attack(BF_WEAPON, target, target, src, SR_CRESCENTELBOW_AUTOSPELL, sce->val1, gettick(), 0);
			ATK_ADD(wd.damage, wd.damage2, battle_damage_temp[1] / 10);
			status_change_end(target, SC_CRESCENTELBOW, INVALID_TIMER);
		}
	}

	switch(skill_id) {
		case NC_AXETORNADO:
			if(sstatus->rhw.ele == ELE_WIND)
				ATK_ADDRATE(wd.damage, wd.damage2, 25);
			break;
#ifndef RENEWAL //Add check here, because we want to apply the same behavior in pre-renewal [exneval]
		case RK_DRAGONBREATH:
		case RK_DRAGONBREATH_WATER:
#else
		case RA_WUGSTRIKE:
		case RA_WUGBITE:
		case SR_GATEOFHELL:
			break; //Ignore ranged attack modifier
		default:
#endif
			if(wd.flag&BF_LONG && sd)
				ATK_ADDRATE(wd.damage, wd.damage2, sd->bonus.long_attack_atk_rate);
			break;
	}

	if(sd) {
#ifdef RENEWAL
		if(!skill_id && is_attack_critical(wd, src, target, skill_id, skill_lv, false))
			ATK_ADDRATE(wd.damage, wd.damage2, 40 + sd->bonus.crit_atk_rate);
#endif
		if((bonus_damage = pc_skillatk_bonus(sd, skill_id)))
			ATK_ADDRATE(wd.damage, wd.damage2, bonus_damage);
		if((bonus_damage = battle_adjust_skill_damage(sd->bl.m, skill_id)))
			ATK_RATE(wd.damage, wd.damage2, bonus_damage);
	}

	if(tsd && (bonus_damage = pc_sub_skillatk_bonus(tsd, skill_id)))
		ATK_ADDRATE(wd.damage, wd.damage2, -bonus_damage);

#ifdef ADJUST_SKILL_DAMAGE
	if((skill_damage = battle_skill_damage(src, target, skill_id)))
		ATK_ADDRATE(wd.damage, wd.damage2, skill_damage);
#endif
	return wd;
}

/*====================================================
 * Basic wd init - not influenced by HIT/MISS/DEF/etc.
 *----------------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
static struct Damage initialize_weapon_data(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int wflag)
{
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct Damage wd;

	wd.type = DMG_NORMAL; //Normal attack
	wd.div_ = (skill_id ? skill_get_num(skill_id, skill_lv) : 1);
	wd.amotion = (skill_id && skill_get_inf(skill_id)&INF_GROUND_SKILL) ? 0 : sstatus->amotion; //Amotion should be 0 for ground skills
	//if(skill_id == KN_AUTOCOUNTER)
		//wd.amotion /= 2; //Counter attack DOES obey ASPD delay on official, uncomment if you want the old (bad) behavior [helvetica]
	wd.dmotion = tstatus->dmotion;
	wd.blewcount = skill_get_blewcount(skill_id, skill_lv);
	wd.miscflag = wflag;
	wd.flag = BF_WEAPON; //Initial Flag
	wd.flag |= (skill_id || wd.miscflag) ? BF_SKILL : BF_NORMAL; //Baphomet card's splash damage is counted as a skill [Inkfish]
	wd.isvanishdamage = false;
	wd.isspdamage = false;
	wd.damage = wd.damage2 = 
#ifdef RENEWAL	
	wd.statusAtk = wd.statusAtk2 = wd.equipAtk = wd.equipAtk2 = wd.weaponAtk = wd.weaponAtk2 = wd.masteryAtk = wd.masteryAtk2 =
#endif
	0;
	wd.dmg_lv = ATK_DEF; //This assumption simplifies the assignation later

	if(sd)
		wd.blewcount += battle_blewcount_bonus(sd, skill_id);

	if(skill_id) {
		wd.flag |= battle_range_type(src, target, skill_id, skill_lv);
		switch(skill_id) {
			case TF_DOUBLE: //For NPC used skill
			case GS_CHAINACTION:
				wd.type = DMG_MULTI_HIT;
				break;
			case GS_GROUNDDRIFT:
				wd.amotion = sstatus->amotion;
			//Fall through
			case KN_SPEARSTAB:
			case KN_BOWLINGBASH:
			case MS_BOWLINGBASH:
			case MO_BALKYOUNG:
			case TK_TURNKICK:
				wd.blewcount = 0;
				break;
			case KN_PIERCE:
			case ML_PIERCE:
				wd.div_ = (wd.div_ > 0 ? tstatus->size + 1 : -(tstatus->size + 1));
				break;
			case KN_AUTOCOUNTER:
				wd.flag = (wd.flag&~BF_SKILLMASK)|BF_NORMAL;
				break;
			case MO_FINGEROFFENSIVE:
				if(sd)
					wd.div_ = (battle_config.finger_offensive_type ? 1 : max(sd->spiritball_old,1));
				break;
			case LK_SPIRALPIERCE:
				if(!sd)
					wd.flag = ((wd.flag&~(BF_RANGEMASK|BF_WEAPONMASK))|BF_LONG|BF_MISC);
				break;
			case LG_HESPERUSLIT: {
					struct status_change *sc = NULL;

					//The number of hits is set to 3 by default for use in Inspiration status
					//When in banding, the number of hits is equal to the number of Royal Guards in banding
					if((sc = status_get_sc(src)) && sc->data[SC_BANDING])
						wd.div_ = sc->data[SC_BANDING]->val2;
				}
				break;
			case RL_R_TRIP: //Knock's back target out of skill range
				wd.blewcount -= distance_bl(src, target);
				break;
			case MH_SONIC_CRAW: {
					TBL_HOM *hd = BL_CAST(BL_HOM, src);

					wd.div_ = (hd ? hd->homunculus.spiritball : skill_get_maxcount(skill_id, skill_lv));
				}
				break;
			case EL_STONE_RAIN:
				if(!(wd.miscflag&1))
					wd.div_ = 1;
				break;
		}
	} else
		wd.flag |= (is_skill_using_arrow(src, skill_id) ? BF_LONG : BF_SHORT);
	return wd;
}

/**
 * Check if we should reflect the damage and calculate it if so
 * @param d : damage
 * @param src : bl who did the attack
 * @param target : target of the attack
 * @param skill_id : id of casted skill, 0 = basic atk
 * @param skill_lv : lvl of skill casted
 */
void battle_do_reflect(struct Damage *d, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	//Don't reflect your own damage
	if(d->damage + d->damage2 > 0 && src && target && target->id != src->id &&
		(src->type != BL_SKILL || skill_id == SG_SUN_WARM || skill_id == SG_MOON_WARM || skill_id == SG_STAR_WARM)) {
		int64 damage = d->damage + d->damage2, rdamage = 0;
		struct map_session_data *tsd = BL_CAST(BL_PC, target);
		struct status_change *tsc = status_get_sc(target);
		struct status_data *sstatus = status_get_status_data(src);
		struct status_data *tstatus = status_get_status_data(target);
		int tick = gettick(), rdelay = 0;
		bool reflectdamage = false;

		if(!tsc)
			return;
		if(tsc->data[SC_REFLECTDAMAGE])
			reflectdamage = true;
		if(tsc->data[SC_DEVOTION]) {
			struct status_change_entry *tsce_d = tsc->data[SC_DEVOTION];
			struct block_list *d_tbl = map_id2bl(tsce_d->val1);
			struct status_change *tsc_d = NULL;

			if(d_tbl &&
				((d_tbl->type == BL_MER && ((TBL_MER *)d_tbl)->master && ((TBL_MER *)d_tbl)->master->bl.id == target->id) ||
				(d_tbl->type == BL_PC && ((TBL_PC *)d_tbl)->devotion[tsce_d->val2] == target->id)) &&
				check_distance_bl(target, d_tbl, tsce_d->val3) && (tsc_d = status_get_sc(d_tbl)) && tsc_d->data[SC_REFLECTDAMAGE])
			{
				target = d_tbl;
				reflectdamage = true;
			}
		} else if(tsc->data[SC__SHADOWFORM]) {
			struct status_change_entry *tsce_s = tsc->data[SC__SHADOWFORM];
			struct map_session_data *s_tsd = map_id2sd(tsce_s->val2);

			if(s_tsd && s_tsd->shadowform_id == target->id && battle_check_shadowform(target, SC_REFLECTDAMAGE)) {
				target = &s_tsd->bl;
				reflectdamage = true;
			}
		}
		rdamage = battle_calc_return_damage(target, src, &damage, d->flag, skill_id, true);
		if(rdamage > 0) {
			if(reflectdamage)
				map_foreachinshootrange(battle_damage_area, target, skill_get_splash(LG_REFLECTDAMAGE, 1), BL_CHAR, tick, target, d->amotion, sstatus->dmotion, rdamage, tstatus->race);
			else {
				struct block_list *d_bl = battle_check_devotion(src);

				rdelay = clif_damage(src, (!d_bl ? src : d_bl), tick, d->amotion, sstatus->dmotion, rdamage, 1, DMG_ENDURE, 0, false);
				if(tsd)
					battle_drain(tsd, src, rdamage, rdamage, sstatus->race, sstatus->class_, false);
				//It appears that official servers give skill reflect damage a longer delay
				battle_delay_damage(tick, d->amotion, target, (!d_bl ? src : d_bl), 0, CR_REFLECTSHIELD, 0, rdamage, ATK_DEF, rdelay, true, false, false);
				skill_additional_effect(target, (!d_bl ? src : d_bl), CR_REFLECTSHIELD, 1, BF_WEAPON|BF_SHORT|BF_NORMAL, ATK_DEF, tick);
			}
		}
	}
}

/*============================================
 * Calculate "weapon"-type attacks and skills
 *--------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_weapon_attack(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int wflag)
{
	struct map_session_data *sd, *tsd;
	struct Damage wd;
	struct status_change *sc, *tsc;
	struct status_data *sstatus, *tstatus;
	int right_element, left_element, nk;

	memset(&wd, 0, sizeof(wd));

	if(!src || !target) {
		nullpo_info(NLP_MARK);
		return wd;
	}

	sc = status_get_sc(src);
	tsc = status_get_sc(target);
	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(target);

	wd = initialize_weapon_data(src, target, skill_id, skill_lv, wflag);

	right_element = battle_get_weapon_element(&wd, src, target, skill_id, skill_lv, EQI_HAND_R);
	left_element = battle_get_weapon_element(&wd, src, target, skill_id, skill_lv, EQI_HAND_L);

	nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);

	//Skip checking as there are no status changes active
	if(sc && !sc->count)
		sc = NULL;

	//Skip checking as there are no status changes active
	if(tsc && !tsc->count)
		tsc = NULL;

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);

	//Check for Lucky Dodge
	if((!skill_id || skill_id == PA_SACRIFICE) && tstatus->flee2 && rnd()%1000 < tstatus->flee2) {
		wd.type = DMG_LUCY_DODGE;
		wd.dmg_lv = ATK_LUCKY;
		if(wd.div_ < 0)
			wd.div_ *= -1;
		return wd;
	}

	//On official check for multi hit first so we can override crit on double attack [helvetica]
	wd = battle_calc_multi_attack(wd, src, target, skill_id, skill_lv);

	//Crit check is next since crits always hit on official [helvetica]
	if(is_attack_critical(wd, src, target, skill_id, skill_lv, true)) {
		if(battle_config.enable_critical_multihit) //kRO new critical behavior update [exneval]
			wd.type = ((wd.type&DMG_MULTI_HIT) ? DMG_MULTI_HIT_CRITICAL : DMG_CRITICAL);
		else
			wd.type = DMG_CRITICAL;
	}

	//Check if we're landing a hit
	if(!is_attack_hitting(wd, src, target, skill_id, skill_lv, true))
		wd.dmg_lv = ATK_FLEE;
	else {
		int ratio = 0;
		int64 const_val = 0;

		//Base skill damage
		wd = battle_calc_skill_base_damage(wd, src, target, skill_id, skill_lv);

#ifdef RENEWAL
		//Card Fix for attacker (sd), 2 is added to the "left" flag meaning "attacker cards only"
		if(sd) {
			wd.weaponAtk += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.weaponAtk, 2, wd.flag);
			wd.equipAtk += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.equipAtk, 2, wd.flag);
			if(is_attack_left_handed(src, skill_id)) {
				wd.weaponAtk2 += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.weaponAtk2, 3, wd.flag);
				wd.equipAtk2 += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.equipAtk2, 3, wd.flag);
			}
		}

		//Card Fix for target (tsd), 2 is not added to the "left" flag meaning "target cards only"
		if(tsd && sd) {
			if(skill_id == SO_VARETYR_SPEAR)
				nk |= NK_NO_CARDFIX_DEF; //Varetyr Spear physical part ignores card reduction [exneval]
			wd.statusAtk += battle_calc_cardfix(BF_WEAPON, src, target, nk|NK_NO_ELEFIX, right_element, left_element, wd.statusAtk, 0, wd.flag);
			wd.weaponAtk += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.weaponAtk, 0, wd.flag);
			wd.equipAtk += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.equipAtk, 0, wd.flag);
			wd.masteryAtk += battle_calc_cardfix(BF_WEAPON, src, target, nk|NK_NO_ELEFIX, right_element, left_element, wd.masteryAtk, 0, wd.flag);
			if(is_attack_left_handed(src, skill_id)) {
				wd.statusAtk2 += battle_calc_cardfix(BF_WEAPON, src, target, nk|NK_NO_ELEFIX, right_element, left_element, wd.statusAtk2, 1, wd.flag);
				wd.weaponAtk2 += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.weaponAtk2, 1, wd.flag);
				wd.equipAtk2 += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.equipAtk2, 1, wd.flag);
				wd.masteryAtk2 += battle_calc_cardfix(BF_WEAPON, src, target, nk|NK_NO_ELEFIX, right_element, left_element, wd.masteryAtk2, 1, wd.flag);
			}
		}

		//Final attack bonuses that aren't affected by cards
		wd = battle_attack_cannonball(wd, src, skill_id);
		wd = battle_attack_sc_bonus(wd, src, target, skill_id, skill_lv);

		//Monsters, homuns and pets have their damage computed directly
		if(sd) {
			if(skill_id == RA_WUGSTRIKE || skill_id == RA_WUGBITE)
				wd.equipAtk = wd.equipAtk2 = 0;
			wd.damage = wd.statusAtk + wd.weaponAtk + wd.equipAtk + wd.masteryAtk;
			if(is_attack_left_handed(src, skill_id))
				wd.damage2 = wd.statusAtk2 + wd.weaponAtk2 + wd.equipAtk2 + wd.masteryAtk2;
		}
#else
		wd = battle_attack_cannonball(wd, src, skill_id);
		wd = battle_attack_sc_bonus(wd, src, target, skill_id, skill_lv);
#endif

		//Skill level ratio
		ratio = battle_calc_attack_skill_ratio(wd, src, target, skill_id, skill_lv);
		ATK_RATE(wd.damage, wd.damage2, ratio);

		//Other skill bonuses
		const_val = battle_calc_skill_constant_addition(wd, src, target, skill_id, skill_lv);
		ATK_ADD(wd.damage, wd.damage2, const_val);

		//Check for defense reduction
		if(wd.damage + wd.damage2 > 0) {
			wd = battle_calc_defense_reduction(wd, src, target, skill_id, skill_lv);
			wd = battle_calc_attack_post_defense(wd, src, target, skill_id, skill_lv);
		}
	}

#ifndef RENEWAL
	if(sd) {
		wd.damage += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.damage, 2, wd.flag);
		if(is_attack_left_handed(src, skill_id))
			wd.damage2 += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.damage2, 3, wd.flag);
	}
#endif

	if(tsd
#ifdef RENEWAL
		&& !sd
#endif
	) {
		if(skill_id == SO_VARETYR_SPEAR)
			nk |= NK_NO_CARDFIX_DEF;
		wd.damage += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.damage, 0, wd.flag);
		if(is_attack_left_handed(src, skill_id))
			wd.damage2 += battle_calc_cardfix(BF_WEAPON, src, target, nk, right_element, left_element, wd.damage2, 1, wd.flag);
	}

	//Fixed damage but reduced by elemental attribute defense [exneval]
	switch(skill_id) {
		case HW_MAGICCRASHER: {
				short index = -1;

				if(sd && (index = sd->equip_index[EQI_HAND_R]) >= 0 &&
					sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
					ATK_ADD(wd.damage, wd.damage2, 2 * sd->inventory.u.items_inventory[index].refine);
			}
			break;
		case LG_SHIELDPRESS:  {
				short index = -1;

				if(sd && (index = sd->equip_index[EQI_HAND_L]) >= 0 &&
					sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
					ATK_ADD(wd.damage, wd.damage2, sstatus->vit * sd->inventory.u.items_inventory[index].refine);
			}
			break;
		case SR_FALLENEMPIRE:
			ATK_ADD(wd.damage, wd.damage2, ((tstatus->size + 1) * 2 + (skill_lv - 1)) * sstatus->str);
			if(tsd && tsd->weight) {
				ATK_ADD(wd.damage, wd.damage2, tsd->weight / 10 * sstatus->dex / 120);
			} else
				ATK_ADD(wd.damage, wd.damage2, status_get_lv(target) * 50 * sstatus->dex / 120);
			break;
		case SR_TIGERCANNON:
			if(wd.miscflag&8) {
				ATK_ADD(wd.damage, wd.damage2, skill_lv * 500 + status_get_lv(target) * 40);
			} else
				ATK_ADD(wd.damage, wd.damage2, skill_lv * 240 + status_get_lv(target) * 40);
			if(wd.miscflag&16)
				wd.damage = battle_damage_temp[0];
			break;
		case SR_GATEOFHELL:
			ATK_ADD(wd.damage, wd.damage2, status_get_max_hp(src) - status_get_hp(src));
			if(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == SR_FALLENEMPIRE) {
				ATK_ADD(wd.damage, wd.damage2, status_get_max_sp(src) * (100 + skill_lv * 20) / 100 + 40 * status_get_lv(src));
			} else
				ATK_ADD(wd.damage, wd.damage2, status_get_sp(src) * (100 + skill_lv * 20) / 100 + 10 * status_get_lv(src));
			break;
	}

#ifndef RENEWAL
	if(sd) {
		short div_ = max(wd.div_, 1);
		uint16 lv;

		if((lv = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)
			ATK_ADD(wd.damage, wd.damage2, lv * 2);
		if(skill_id != CR_SHIELDBOOMERANG) //Only Shield Boomerang doesn't takes the star crumb bonus
			ATK_ADD2(wd.damage, wd.damage2, div_ * sd->right_weapon.star, div_ * sd->left_weapon.star);
		if(skill_id == MO_FINGEROFFENSIVE) { //The finger offensive spheres on moment of attack do count [Skotlex]
			ATK_ADD(wd.damage, wd.damage2, div_ * sd->spiritball_old * 3);
		} else
			ATK_ADD(wd.damage, wd.damage2, div_ * sd->spiritball * 3);
	}
#endif

	//Check for element attribute modifiers
	wd = battle_calc_element_damage(wd, src, target, skill_id, skill_lv);

	//Fixed damage and no elemental [exneval]
	switch(skill_id) {
		case 0:
			if(sc && sc->data[SC_SPELLFIST] && !(wd.miscflag&16)) {
				struct Damage ad = battle_calc_magic_attack(src, target, sc->data[SC_SPELLFIST]->val3, sc->data[SC_SPELLFIST]->val4, BF_SHORT);

				ATK_ADD(wd.damage, wd.damage2, ad.damage);
			}
			break;
#ifndef RENEWAL
		case NJ_KUNAI:
			ATK_ADD(wd.damage, wd.damage2, 90);
			break;
#endif
		case SR_CRESCENTELBOW_AUTOSPELL: //[Received damage x {1 + (Skill Level x 0.2)}]
			ATK_ADD(wd.damage, wd.damage2, battle_damage_temp[0] * (100 + skill_lv * 20) / 100);
			break;
	}

	//Only do 1 dmg to plant, no need to calculate rest
	if(is_infinite_defense(target, wd.flag))
		return battle_calc_attack_plant(wd, src, target, skill_id, skill_lv);

	//Apply DAMAGE_DIV_FIX and check for min damage
	wd = battle_apply_div_fix(wd, skill_id);

	wd = battle_calc_attack_left_right_hands(wd, src, target, skill_id, skill_lv);

	wd.damage = battle_calc_damage_sub(src, target, &wd, wd.damage, skill_id, skill_lv);
	if(is_attack_left_handed(src, skill_id))
		wd.damage2 = battle_calc_damage_sub(src, target, &wd, wd.damage2, skill_id, skill_lv);

	switch(skill_id) {
#ifdef RENEWAL
		case AM_DEMONSTRATION:
		case AM_ACIDTERROR:
		case CR_ACIDDEMONSTRATION:
		case GN_FIRE_EXPANSION_ACID:
#endif
		case NPC_EARTHQUAKE:
		case NPC_GRANDDARKNESS:
		case CR_GRANDCROSS:
		case ASC_BREAKER:
		case RA_CLUSTERBOMB:
		case RA_FIRINGTRAP:
		case RA_ICEBOUNDTRAP:
		case LG_RAYOFGENESIS:
		case SO_VARETYR_SPEAR:
			return wd; //Do GVG fix later
		case 0:
			if(sd)
				battle_vanish(sd, target, &wd); //Check for vanish HP/SP
		//Fall through
		default:
			wd = battle_calc_attack_gvg_bg(wd, src, target, skill_id, skill_lv);
			break;
	}

	if(skill_id == SR_CRESCENTELBOW_AUTOSPELL)
		battle_damage_temp[1] = wd.damage; //Will be used for additional damage to the caster [exneval]

	wd = battle_calc_weapon_final_atk_modifiers(wd, src, target, skill_id, skill_lv);

	battle_absorb_damage(target, &wd);

	//Skill reflect gets calculated after all attack modifier
	battle_do_reflect(&wd, src, target, skill_id, skill_lv);

	return wd;
}

/*==========================================
 * Calculate "magic"-type attacks and skills
 *------------------------------------------
 * Credits:
 *	Original coder DracoRPG
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_magic_attack(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int mflag)
{
	int i, nk;
#ifdef ADJUST_SKILL_DAMAGE
	int skill_damage = 0;
#endif
	short s_ele = 0;
	TBL_PC *sd;
	TBL_PC *tsd;
	struct status_change *sc, *tsc;
	struct Damage ad;
	struct status_data *sstatus, *tstatus;
	struct {
		unsigned imdef : 1;
		unsigned infdef : 1;
	} flag;

	memset(&ad, 0, sizeof(ad));
	memset(&flag, 0, sizeof(flag));

	if(!src || !target) {
		nullpo_info(NLP_MARK);
		return ad;
	}

	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(target);

	//Initial Values
	ad.damage = 1;
	ad.div_ = skill_get_num(skill_id, skill_lv);
	//Amotion should be 0 for ground skills
	ad.amotion = (skill_get_inf(skill_id)&INF_GROUND_SKILL ? 0 : sstatus->amotion);
	ad.dmotion = tstatus->dmotion;
	ad.blewcount = skill_get_blewcount(skill_id, skill_lv);
	ad.miscflag = mflag;
	ad.flag = BF_MAGIC|BF_SKILL;
	ad.dmg_lv = ATK_DEF;
	nk = skill_get_nk(skill_id);
	flag.imdef = (nk&NK_IGNORE_DEF ? 1 : 0);

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);
	sc = status_get_sc(src);
	tsc = status_get_sc(target);

	//Initialize variables that will be used afterwards
	s_ele = skill_get_ele(skill_id, skill_lv);

	if(s_ele == -1) { //Skill takes the weapon's element
		s_ele = sstatus->rhw.ele;
		if(sd && sd->charmball_type != CHARM_TYPE_NONE && sd->charmball >= MAX_CHARMBALL)
			s_ele = sd->charmball_type; //Summoning 10 charmball will endow your weapon
	} else if(s_ele == -2) //Use status element
		s_ele = status_get_attack_sc_element(src, status_get_sc(src));
	else if(s_ele == -3) //Use random element
		s_ele = rnd()%ELE_ALL;

	switch(skill_id) {
#ifdef RENEWAL
		case NPC_EARTHQUAKE:
#endif
		case ASC_BREAKER:
		case LG_RAYOFGENESIS:
		case SO_VARETYR_SPEAR:
			s_ele = ELE_NEUTRAL;
			break;
#ifdef RENEWAL
		case CR_GRANDCROSS:
#endif
		case LG_SHIELDSPELL:
			s_ele = ELE_HOLY;
			break;
		case WL_HELLINFERNO:
			if(ad.miscflag&ELE_DARK)
				s_ele = ELE_DARK;
			break;
		case SO_PSYCHIC_WAVE:
			if(sc && sc->count) {
				if (sc->data[SC_HEATER_OPTION])
					s_ele = sc->data[SC_HEATER_OPTION]->val4;
				else if (sc->data[SC_COOLER_OPTION])
					s_ele = sc->data[SC_COOLER_OPTION]->val4;
				else if (sc->data[SC_BLAST_OPTION])
					s_ele = sc->data[SC_BLAST_OPTION]->val3;
				else if (sc->data[SC_CURSED_SOIL_OPTION])
					s_ele = sc->data[SC_CURSED_SOIL_OPTION]->val4;
			}
			break;
		case KO_KAIHOU:
			if(sd && sd->charmball_type != CHARM_TYPE_NONE && sd->charmball > 0)
				s_ele = sd->charmball_type;
			break;
	}

	//Set miscellaneous data that needs be filled
	if(sd) {
		sd->state.arrow_atk = 0;
		ad.blewcount += battle_blewcount_bonus(sd, skill_id);
	}

	//Skill Range Criteria
	ad.flag |= battle_range_type(src, target, skill_id, skill_lv);

	//Infinite defense (plant mode)
	flag.infdef = (is_infinite_defense(target, ad.flag) ? 1 : 0);

	switch(skill_id) {
		case MG_FIREWALL:
		case EL_FIRE_MANTLE:
			if(tstatus->def_ele == ELE_FIRE || battle_check_undead(tstatus->race, tstatus->def_ele))
				ad.blewcount = 0; //No knockback
		//Fall through
		case NJ_KAENSIN:
		case PR_SANCTUARY:
			ad.dmotion = 1; //No flinch animation
			break;
	}

	if(!flag.infdef) { //No need to do the math for plants
		unsigned int skillratio = 100; //Skill dmg modifiers

#ifdef RENEWAL
		ad.damage = 0; //Reinitialize
#endif
//MATK_RATE scales the damage. 100 = no change. 50 is halved, 200 is doubled, etc
#define MATK_RATE(a) { ad.damage = ad.damage * (a) / 100; }
//Adds dmg%. 100 = +100% (double) damage. 10 = +10% damage
#define MATK_ADDRATE(a) { ad.damage += ad.damage * (a) / 100; }
//Adds an absolute value to damage. 100 = +100 damage
#define MATK_ADD(a) { ad.damage += a; }

		switch(skill_id) { //Calc base damage according to skill
			case AL_HEAL:
			case PR_BENEDICTIO:
			case PR_SANCTUARY:
			case AB_HIGHNESSHEAL:
				ad.damage = skill_calc_heal(src, target, skill_id, skill_lv, false);
				break;
			case PR_ASPERSIO:
				ad.damage = 40;
				break;
			case ALL_RESURRECTION:
			case PR_TURNUNDEAD:
				//Undead check is on skill_castend_damage_id code
#ifdef RENEWAL
				i = 10 * skill_lv + sstatus->luk + sstatus->int_ + status_get_lv(src) +
					300 - 300 * tstatus->hp / tstatus->max_hp;
#else
				i = 20 * skill_lv + sstatus->luk + sstatus->int_ + status_get_lv(src) +
					200 - 200 * tstatus->hp / tstatus->max_hp;
#endif
				i = min(i, 700);
				if(rnd()%1000 < i && !status_has_mode(tstatus, MD_STATUS_IMMUNE))
					ad.damage = tstatus->hp;
				else {
#ifdef RENEWAL
					MATK_ADD(status_get_matk(src, 2));
#else
					ad.damage = status_get_lv(src) + sstatus->int_ + skill_lv * 10;
#endif
				}
				break;
#ifndef RENEWAL
			case ASC_BREAKER:
				ad.damage = rnd_value(500, 1000) + 5 * skill_lv * sstatus->int_;
				flag.imdef = 1;
				break;
#endif
			case PF_SOULBURN:
				ad.damage = tstatus->sp * 2;
				break;
			case NPC_EARTHQUAKE: //Earthquake calculates damage to self first then spread it to others [exneval]
				ad.damage = battle_calc_weapon_attack(src, src, skill_id, skill_lv, ad.miscflag).damage;
				break;
			case NPC_ICEMINE:
			case NPC_FLAMECROSS:
				ad.damage = sstatus->rhw.atk * 20 * skill_lv;
				break;
			default:
				MATK_ADD(status_get_matk(src, 2));

#ifdef RENEWAL
				if(sd) //Card Fix for attacker (sd), 1 is added to the "left" flag meaning "attacker cards only"
					ad.damage += battle_calc_cardfix(BF_MAGIC, src, target, nk, s_ele, 0, ad.damage, 1, ad.flag);
				if(tsd) //Card Fix for target (tsd), 1 is not added to the "left" flag meaning "target cards only"
					ad.damage += battle_calc_cardfix(BF_MAGIC, src, target, nk, s_ele, 0, ad.damage, 0, ad.flag);
#endif

				if(nk&NK_SPLASHSPLIT) { //Divide MATK in case of multiple targets skill
					if(ad.miscflag > 0)
						ad.damage /= ad.miscflag;
					else
						ShowError("0 enemies targeted by %d:%s, divide per 0 avoided!\n", skill_id, skill_get_name(skill_id));
				}

				switch(skill_id) {
					case MG_NAPALMBEAT:
						skillratio += -30 + 10 * skill_lv;
						if(tsc && tsc->data[SC_WHITEIMPRISON])
							skillratio *= 2;
						if(sc && sc->data[SC_TELEKINESIS_INTENSE])
							skillratio = skillratio * sc->data[SC_TELEKINESIS_INTENSE]->val3 / 100;
						break;
					case MG_FIREBALL:
#ifdef RENEWAL
						skillratio += 40 + 20 * skill_lv;
						if(ad.miscflag == 2) //Enemies at the edge of the area will take 75% of the damage
							skillratio = skillratio * 3 / 4;
#else
						skillratio += -30 + 10 * skill_lv;
#endif
						break;
					case MG_SOULSTRIKE:
						if(battle_check_undead(tstatus->race,tstatus->def_ele))
							skillratio += 5 * skill_lv;
						if(tsc && tsc->data[SC_WHITEIMPRISON])
							skillratio *= 2;
						if(sc && sc->data[SC_TELEKINESIS_INTENSE])
							skillratio = skillratio * sc->data[SC_TELEKINESIS_INTENSE]->val3 / 100;
						break;
					case MG_FIREWALL:
						skillratio -= 50;
						break;
#ifndef RENEWAL //In renewal, Thunder Storm boost is 100% (in pre-re, 80%)
					case MG_THUNDERSTORM:
						skillratio -= 20;
						break;
#endif
					case MG_FROSTDIVER:
						skillratio += 10 * skill_lv;
						break;
					case AL_HOLYLIGHT:
						skillratio += 25;
						if(sd && sd->sc.data[SC_SPIRIT] && sd->sc.data[SC_SPIRIT]->val2 == SL_PRIEST)
							skillratio *= 5; //Does 5x damage include bonuses from other skills?
						break;
					case AL_RUWACH:
						skillratio += 45;
						break;
					case WZ_FROSTNOVA:
						skillratio += -100 + (100 + 10 * skill_lv) * 2 / 3;
						break;
					case WZ_FIREPILLAR:
						if(sd && ad.div_ > 0)
							ad.div_ *= -1; //For players, damage is divided by number of hits
						skillratio += -60 + 20 * skill_lv; //20% MATK each hit
						break;
					case WZ_SIGHTRASHER:
						skillratio += 20 * skill_lv;
						break;
					case WZ_WATERBALL:
						skillratio += 30 * skill_lv;
						break;
					case WZ_STORMGUST:
						skillratio += 40 * skill_lv;
						break;
					case HW_NAPALMVULCAN:
						skillratio += 25;
						if(tsc && tsc->data[SC_WHITEIMPRISON])
							skillratio *= 2;
						if(sc && sc->data[SC_TELEKINESIS_INTENSE])
							skillratio = skillratio * sc->data[SC_TELEKINESIS_INTENSE]->val3 / 100;
						break;
					case SL_STIN: //Target size must be small (0) for full damage
						skillratio += (tstatus->size != SZ_SMALL ? -99 : 10 * skill_lv);
						break;
					case SL_STUN: //Full damage is dealt on small/medium targets
						skillratio += (tstatus->size != SZ_BIG ? 5 * skill_lv : -99);
						break;
					case SL_SMA: //Base damage is 40% + lv%
						skillratio += -60 + status_get_lv(src);
						break;
					case NJ_KOUENKA:
						skillratio -= 10;
						if(sd && sd->charmball_type == CHARM_TYPE_FIRE && sd->charmball > 0)
							skillratio += 20 * sd->charmball;
						break;
					case NJ_KAENSIN:
						skillratio -= 50;
						if(sd && sd->charmball_type == CHARM_TYPE_FIRE && sd->charmball > 0)
							skillratio += 10 * sd->charmball;
						break;
					case NJ_BAKUENRYU:
						skillratio += 50 + 150 * skill_lv;
						if(sd && sd->charmball_type == CHARM_TYPE_FIRE && sd->charmball > 0)
							skillratio += 45 * sd->charmball;
						break;
					case NJ_HYOUSENSOU:
#ifdef RENEWAL
						skillratio -= 30;
#endif
						if(sd && sd->charmball_type == CHARM_TYPE_WATER && sd->charmball > 0)
							skillratio += 5 * sd->charmball;
						break;
					case NJ_HYOUSYOURAKU:
						skillratio += 50 * skill_lv;
						if(sd && sd->charmball_type == CHARM_TYPE_WATER && sd->charmball > 0)
							skillratio += 25 * sd->charmball;
						break;
					case NJ_HUUJIN:
#ifdef RENEWAL
						skillratio += 50;
#endif
						if(sd && sd->charmball_type == CHARM_TYPE_WIND && sd->charmball > 0)
							skillratio += 20 * sd->charmball;
						break;
					case NJ_RAIGEKISAI:
						skillratio += 60 + 40 * skill_lv;
						if(sd && sd->charmball_type == CHARM_TYPE_WIND && sd->charmball > 0)
							skillratio += 15 * sd->charmball;
						break;
					case NJ_KAMAITACHI:
						skillratio += 100 * skill_lv;
						if(sd && sd->charmball_type == CHARM_TYPE_WIND && sd->charmball > 0)
							skillratio += 10 * sd->charmball;
						break;
					case NPC_ENERGYDRAIN:
						skillratio += 100 * skill_lv;
						break;
					case NPC_COMET:
						i = (sc ? distance_xy(target->x, target->y, sc->pos_x, sc->pos_y) : 8);
						if(i <= 3)
							skillratio += 2400 + 500 * skill_lv;
						else if(i <= 5)
							skillratio += 1900 + 500 * skill_lv;
						else if(i <= 7)
							skillratio += 1400 + 500 * skill_lv;
						else
							skillratio += 900 + 500 * skill_lv; 
						break;
					case NPC_VENOMFOG:
						skillratio += 600 + 100 * skill_lv;
						break;
					case NPC_PULSESTRIKE2:
						skillratio += 100;
						break;
					case NPC_HELLBURNING:
						skillratio += 900;
						break;
					case NPC_FIRESTORM:
						skillratio += 200;
						break;
#ifdef RENEWAL
					case WZ_HEAVENDRIVE:
					case WZ_METEOR:
						skillratio += 25;
						break;
					case WZ_VERMILION:
						if(sd) {
							int per = 0;

							while((++per) < skill_lv)
								skillratio += per * 5; //100% 105% 115% 130% 150% 175% 205% 240% 280% 325%
						} else
							skillratio += 20 * skill_lv - 20; //Monsters use old formula
						break;
					case AM_DEMONSTRATION:
						skillratio += 20 * skill_lv;
						break;
					case AM_ACIDTERROR:
						skillratio += 100 + 80 * skill_lv;
						break;
#else
					case WZ_VERMILION:
						skillratio += 20 * skill_lv - 20;
						break;
#endif
					case AB_JUDEX:
						skillratio += 200 + 20 * skill_lv;
						if(skill_lv == 5)
							skillratio += 170;
						RE_LVL_DMOD(100);
						break;
					case AB_ADORAMUS:
						skillratio += 230 + 70 * skill_lv;
						RE_LVL_DMOD(100);
						break;
					case AB_DUPLELIGHT_MAGIC:
						skillratio += 300 + 40 * skill_lv;
						break;
					case WL_SOULEXPANSION:
						skillratio += -100 + (skill_lv + 4) * 100 + sstatus->int_;
						RE_LVL_DMOD(100);
						if(tsc && tsc->data[SC_WHITEIMPRISON])
							skillratio *= 2;
						if(sc && sc->data[SC_TELEKINESIS_INTENSE])
							skillratio = skillratio * sc->data[SC_TELEKINESIS_INTENSE]->val3 / 100;
						break;
					case WL_FROSTMISTY:
						skillratio += 100 + 100 * skill_lv;
						RE_LVL_DMOD(100);
						break;
					case WL_JACKFROST:
					case NPC_JACKFROST:
						if(tsc && tsc->data[SC_FREEZING]) {
							skillratio += 900 + 300 * skill_lv;
							RE_LVL_DMOD(100);
						} else {
							skillratio += 400 + 100 * skill_lv;
							RE_LVL_DMOD(150);
						}
						break;
					case WL_DRAINLIFE:
						skillratio += -100 + 200 * skill_lv + sstatus->int_;
						RE_LVL_DMOD(100);
						break;
					case WL_CRIMSONROCK:
						skillratio += 1200 + 300 * skill_lv;
						RE_LVL_DMOD(100);
						break;
					case WL_HELLINFERNO:
						skillratio += -100 + 300 * skill_lv;
						RE_LVL_DMOD(100);
						//Shadow: MATK [{( Skill Level x 300 ) x ( Caster's Base Level / 100 ) x 4/5 }] %
						//Fire : MATK [{( Skill Level x 300 ) x ( Caster's Base Level / 100 ) / 5 }] %
						if(ad.miscflag&ELE_DARK)
							skillratio *= 4;
						skillratio /= 5;
						break;
					case WL_COMET:
						i = (sc ? distance_xy(target->x, target->y, sc->pos_x, sc->pos_y) : 8);
						if(i <= 3)
							skillratio += 2400 + 500 * skill_lv; //7 x 7 cell
						else if(i <= 5)
							skillratio += 1900 + 500 * skill_lv; //11 x 11 cell
						else if(i <= 7)
							skillratio += 1400 + 500 * skill_lv; //15 x 15 cell
						else
							skillratio += 900 + 500 * skill_lv; //19 x 19 cell
						//MATK [{( Skill Level x 400 ) x ( Caster's Base Level / 120 )} + 2500 ] %
						if(skill_check_pc_partner(sd, skill_id, &skill_lv, 1, 0)) {
							skillratio = skill_lv * 400;
							RE_LVL_DMOD(120);
							skillratio += 2500;
						}
						break;
					case WL_CHAINLIGHTNING_ATK:
						skillratio += 400 + 100 * skill_lv;
						RE_LVL_DMOD(100);
						if(ad.miscflag > 0)
							skillratio += 100 * ad.miscflag;
						break;
					case WL_EARTHSTRAIN:
						skillratio += 1900 + 100 * skill_lv;
						RE_LVL_DMOD(100);
						break;
					case WL_TETRAVORTEX_FIRE:
					case WL_TETRAVORTEX_WATER:
					case WL_TETRAVORTEX_WIND:
					case WL_TETRAVORTEX_GROUND:
						skillratio += 400 + 500 * skill_lv;
						break;
					case WL_SUMMON_ATK_FIRE:
					case WL_SUMMON_ATK_WATER:
					case WL_SUMMON_ATK_WIND:
					case WL_SUMMON_ATK_GROUND:
						skillratio += -100 + (1 + skill_lv) / 2 * (status_get_lv(src) + status_get_job_lv(src));
						RE_LVL_DMOD(100);
						break;
					case LG_RAYOFGENESIS:
						skillratio += -100 + 300 * skill_lv;
						if(sc) {
							if(sc->data[SC_BANDING])
								skillratio += 200 * sc->data[SC_BANDING]->val2;
							if(sc->data[SC_INSPIRATION])
								skillratio += 400;
						}
						skillratio = skillratio * status_get_job_lv(src) / 25;
						break;
					case LG_SHIELDSPELL: //[(Caster's Base Level x 4) + (Shield MDEF x 100) + (Caster's INT x 2)] %
						skillratio += -100 + 4 * status_get_lv(src) + 100 * (sd ? sd->bonus.shieldmdef : 1) + 2 * status_get_int(src);
						break;
					case WM_METALICSOUND:
						skillratio += -100 + 120 * skill_lv + 60 * (sd ? pc_checkskill(sd, WM_LESSON) : 10);
						RE_LVL_DMOD(100);
						if(tsc && (tsc->data[SC_SLEEP] || tsc->data[SC_DEEPSLEEP]))
							skillratio += skillratio / 2;
						break;
					case WM_REVERBERATION_MAGIC:
						//MATK [{(Skill Level x 100) + 100} x Caster's Base Level / 100] %
						skillratio += 100 * skill_lv;
						RE_LVL_DMOD(100);
						break;
					case SO_FIREWALK:
						skillratio += -100 + 60 * skill_lv;
						RE_LVL_DMOD(100);
						if(sc && sc->data[SC_HEATER_OPTION])
							skillratio += sc->data[SC_HEATER_OPTION]->val3 / 2;
						break;
					case SO_ELECTRICWALK:
						skillratio += -100 + 60 * skill_lv;
						RE_LVL_DMOD(100);
						if(sc && sc->data[SC_BLAST_OPTION])
							skillratio += sc->data[SC_BLAST_OPTION]->val2 / 2;
						break;
					case SO_EARTHGRAVE:
						skillratio += -100 + sstatus->int_ * skill_lv + 200 * (sd ? pc_checkskill(sd, SA_SEISMICWEAPON) : 5);
						RE_LVL_DMOD(100);
						if(sc && sc->data[SC_CURSED_SOIL_OPTION])
							skillratio += 5 * sc->data[SC_CURSED_SOIL_OPTION]->val3;
						break;
					case SO_DIAMONDDUST:
						skillratio += -100 + sstatus->int_ * skill_lv + 200 * (sd ? pc_checkskill(sd, SA_FROSTWEAPON) : 5);
						RE_LVL_DMOD(100);
						if(sc && sc->data[SC_COOLER_OPTION])
							skillratio += 5 * sc->data[SC_COOLER_OPTION]->val3;
						break;
					case SO_POISON_BUSTER:
						skillratio += 900 + 300 * skill_lv;
						RE_LVL_DMOD(120);
						if(sc && sc->data[SC_CURSED_SOIL_OPTION])
							skillratio += 5 * sc->data[SC_CURSED_SOIL_OPTION]->val3;
						break;
					case SO_PSYCHIC_WAVE:
						skillratio += -100 + 70 * skill_lv + 3 * sstatus->int_;
						RE_LVL_DMOD(100);
						if(sc && (sc->data[SC_HEATER_OPTION] || sc->data[SC_COOLER_OPTION] ||
							sc->data[SC_BLAST_OPTION] || sc->data[SC_CURSED_SOIL_OPTION]))
							skillratio += 20;
						break;
					case SO_CLOUD_KILL:
						skillratio += -100 + 40 * skill_lv;
						RE_LVL_DMOD(100);
						if(sc && sc->data[SC_CURSED_SOIL_OPTION])
							skillratio += sc->data[SC_CURSED_SOIL_OPTION]->val3;
						break;
					case SO_VARETYR_SPEAR:
						//MATK [{( Endow Tornado skill level x 50 ) + ( Caster's INT x Varetyr Spear Skill level )} x Caster's Base Level / 100 ] %
						skillratio += -100 + status_get_int(src) * skill_lv + 50 * (sd ? pc_checkskill(sd, SA_LIGHTNINGLOADER) : 5);
						RE_LVL_DMOD(100);
						if(sc && sc->data[SC_BLAST_OPTION])
							skillratio += sc->data[SC_BLAST_OPTION]->val2 * 5;
						break;
					case GN_DEMONIC_FIRE:
						if(skill_lv > 20) //Fire Expansion Level 2
							skillratio += 10 + 20 * (skill_lv - 20) + 10 * sstatus->int_;
						else if(skill_lv > 10) { //Fire Expansion Level 1
							skillratio += 10 + 20 * (skill_lv - 10) + status_get_job_lv(src) + sstatus->int_;
							RE_LVL_DMOD(100);
						} else //Normal Demonic Fire Damage
							skillratio += 10 + 20 * skill_lv;
						break;
					case KO_KAIHOU:
						skillratio += -100 + 200 * (sd ? sd->charmball_old : MAX_CHARMBALL);
						RE_LVL_DMOD(100);
						break;
					//Magical Elemental Spirits Attack Skills
					case EL_FIRE_MANTLE:
					case EL_WATER_SCREW:
					case EL_WATER_SCREW_ATK:
						skillratio += 900;
						break;
					case EL_FIRE_ARROW:
					case EL_ROCK_CRUSHER_ATK:
						skillratio += 200;
						break;
					case EL_FIRE_BOMB:
					case EL_ICE_NEEDLE:
					case EL_HURRICANE_ATK:
						skillratio += 400;
						break;
					case EL_FIRE_WAVE:
					case EL_TYPOON_MIS_ATK:
						skillratio += 1100;
						break;
					case MH_ERASER_CUTTER:
						skillratio += 400 + 100 * skill_lv + (skill_lv%2 > 0 ? 0 : 300);
						break;
					case MH_XENO_SLASHER:
						if(skill_lv%2)
							skillratio += 350 + 50 * skill_lv; //500:600:700
						else
							skillratio += 400 + 100 * skill_lv; //700:900
						break;
					case MH_HEILIGE_STANGE:
						skillratio += 400 + 250 * skill_lv * status_get_lv(src) / 150;
						break;
					case MH_POISON_MIST:
						skillratio += -100 + 40 * skill_lv * status_get_lv(src) / 100;
						break;
					case SU_SV_STEMSPEAR:
						skillratio += 600;
						break;
					case SU_CN_METEOR:
					case SU_CN_METEOR2:
						skillratio += 100 + 100 * skill_lv;
						break;
				}

				if(sc && ((sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 3 && s_ele == ELE_FIRE) ||
					(sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 3 && s_ele == ELE_WATER) ||
					(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 3 && s_ele == ELE_WIND) ||
					(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 3 && s_ele == ELE_EARTH)))
					skillratio += 25;

				MATK_RATE(skillratio);

				if(skill_id == WZ_FIREPILLAR) //Constant/misc additions from skills
					MATK_ADD(100 + 50 * skill_lv);
				break;
		}

		if(!flag.imdef && sd &&
			((sd->bonus.ignore_mdef_ele&(1<<tstatus->def_ele)) || (sd->bonus.ignore_mdef_ele&(1<<ELE_ALL)) ||
			(sd->bonus.ignore_mdef_race&(1<<tstatus->race)) || (sd->bonus.ignore_mdef_race&(1<<RC_ALL)) ||
			(sd->bonus.ignore_mdef_class&(1<<tstatus->class_)) || (sd->bonus.ignore_mdef_class&(1<<CLASS_ALL))))
			flag.imdef = 1; //Ignore MDEF

		if(!flag.imdef) {
			defType mdef = tstatus->mdef; //eMDEF
			short mdef2 = tstatus->mdef2; //sMDEF

			mdef = status_calc_mdef(target, tsc, mdef, false);
			mdef2 = status_calc_mdef2(target, tsc, mdef2, false);

			if(sc && sc->data[SC_EXPIATIO]) {
				i = 5 * sc->data[SC_EXPIATIO]->val1; //5% per level
 				i = min(i, 100); //Cap it to 100 for 5 mdef min
				mdef -= mdef * i / 100;
				//mdef2 -= mdef2 * i / 100;
			}
			if(sd) {
				i = sd->ignore_mdef_by_race[tstatus->race] + sd->ignore_mdef_by_race[RC_ALL];
				i += sd->ignore_mdef_by_class[tstatus->class_] + sd->ignore_mdef_by_class[CLASS_ALL];
				if(i) {
					i = min(i, 100);
					mdef -= mdef * i / 100;
					//mdef2 -= mdef2 * i / 100;
				}
			}
#ifdef RENEWAL
			/**
			 * RE MDEF Reduction
			 * Damage = Magic Attack * (1000 + eMDEF) / (1000 + eMDEF * 10) - sMDEF
			 */
			if(tsc && (tsc->data[SC_FREEZE] || (tsc->data[SC_STONE] && tsc->opt1 == OPT1_STONE))) {
				if(mdef < -99) //It stops at -99
					mdef = 99; //In Aegis it set to 1 but in our case it may lead to exploitation so limit it to 99
			} else //If you have negative eMDEF in normal state, you will take damage as same as 0 eMDEF [exneval]
				mdef = max(mdef, 0);
			ad.damage = ad.damage * (1000 + mdef) / (1000 + mdef * 10) - mdef2;
#else
			if(battle_config.magic_defense_type)
				ad.damage = ad.damage - mdef * battle_config.magic_defense_type - mdef2;
			else
				ad.damage = ad.damage * (100 - mdef) / 100 - mdef2;
#endif
		}

		if(ad.damage < 1)
			ad.damage = 1;
		else if(sc) { //Only applies when hit
			switch(skill_id) { //@TODO: There is another factor that contribute with the damage and need to be formulated [malufett]
				case MG_LIGHTNINGBOLT:
				case MG_THUNDERSTORM:
					if(sc->data[SC_GUST_OPTION])
						ad.damage += (6 + sstatus->int_ / 4) + max(sstatus->dex - 10, 0) / 30;
					break;
				case MG_FIREBOLT:
				case MG_FIREWALL:
					if(sc->data[SC_PYROTECHNIC_OPTION])
						ad.damage += (6 + sstatus->int_ / 4) + max(sstatus->dex - 10, 0) / 30;
					break;
				case MG_COLDBOLT:
				case MG_FROSTDIVER:
					if(sc->data[SC_AQUAPLAY_OPTION])
						ad.damage += (6 + sstatus->int_ / 4) + max(sstatus->dex - 10, 0) / 30;
					break;
				case WZ_EARTHSPIKE:
				case WZ_HEAVENDRIVE:
					if(sc->data[SC_PETROLOGY_OPTION])
						ad.damage += (6 + sstatus->int_ / 4) + max(sstatus->dex - 10, 0) / 30;
					break;
			}
		}

#ifndef RENEWAL
		if(sd)
			ad.damage += battle_calc_cardfix(BF_MAGIC, src, target, nk, s_ele, 0, ad.damage, 1, ad.flag);
		if(tsd)
			ad.damage += battle_calc_cardfix(BF_MAGIC, src, target, nk, s_ele, 0, ad.damage, 0, ad.flag);
#endif

		if(!(nk&NK_NO_ELEFIX) && ad.damage > 0) {
			switch(skill_id) {
				case ASC_BREAKER: //Soul Breaker's magic damage is treated as no elemental
#ifdef RENEWAL
				case CR_GRANDCROSS:
				case CR_ACIDDEMONSTRATION:
				case GN_FIRE_EXPANSION_ACID:
#endif
					break; //Do attr. fix later
				default:
					ad.damage = battle_attr_fix(src, target, ad.damage, s_ele, tstatus->def_ele, tstatus->ele_lv);
					break;
			}
		}

		switch(skill_id) {
			case MG_FIREBOLT:
			case MG_COLDBOLT:
			case MG_LIGHTNINGBOLT:
				if(sc && sc->data[SC_SPELLFIST] && ad.miscflag&BF_SHORT) { //val1 = used spellfist level, val4 = used bolt level [Rytech]
					ad.damage = ad.damage * (50 * sc->data[SC_SPELLFIST]->val1 + sc->data[SC_SPELLFIST]->val4 * 100) / 100;
					return ad; //The rest will be calculated later
				}
				break;
		}
	} //Hint: Against plants damage will still be 1 at this point

	//Apply DAMAGE_DIV_FIX and check for min damage
	ad = battle_apply_div_fix(ad, skill_id);

	ad.damage = battle_calc_damage_sub(src, target, &ad, ad.damage, skill_id, skill_lv);

	switch(skill_id) {
#ifdef RENEWAL
		case AM_DEMONSTRATION:
		case AM_ACIDTERROR:
		case CR_ACIDDEMONSTRATION:
		case GN_FIRE_EXPANSION_ACID:
#endif
		case NPC_GRANDDARKNESS:
		case CR_GRANDCROSS:
		case ASC_BREAKER:
		case LG_RAYOFGENESIS:
		case SO_VARETYR_SPEAR:
			return ad; //Do GVG fix later
		default:
			ad.damage = battle_calc_damage(src, target, &ad, ad.damage, skill_id, skill_lv);
			if(map_flag_gvg2(target->m))
				ad.damage = battle_calc_gvg_damage(src, target, ad.damage, skill_id, ad.flag);
			else if(map[target->m].flag.battleground)
				ad.damage = battle_calc_bg_damage(src, target, ad.damage, skill_id, ad.flag);
			break;
	}

	if(sd) {
		if((i = pc_skillatk_bonus(sd, skill_id)))
			MATK_ADDRATE(i); //Damage rate bonuses
		if((i = battle_adjust_skill_damage(src->m, skill_id)))
			MATK_RATE(i);
	}

	if(tsd && (i = pc_sub_skillatk_bonus(tsd, skill_id)))
		MATK_ADDRATE(-i);

#ifdef ADJUST_SKILL_DAMAGE
	if((skill_damage = battle_skill_damage(src, target, skill_id)))
		MATK_ADDRATE(skill_damage);
#endif

	battle_absorb_damage(target, &ad);

	return ad;
}

/*==========================================
 * Calculate "misc"-type attacks and skills
 *------------------------------------------
 * Credits:
 *	Original coder Skotlex
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_misc_attack(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int mflag)
{
#ifdef ADJUST_SKILL_DAMAGE
	int skill_damage = 0;
#endif
	short i, nk;
	short s_ele;
	bool is_hybrid_dmg = false;
	struct map_session_data *sd, *tsd;
	struct Damage md; //DO NOT CONFUSE with md of mob_data!
	struct status_data *sstatus, *tstatus;

	memset(&md, 0, sizeof(md));

	if(!src || !target) {
		nullpo_info(NLP_MARK);
		return md;
	}

	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(target);

	//Some initial values
	md.amotion = (skill_get_inf(skill_id)&INF_GROUND_SKILL ? 0 : sstatus->amotion);
	md.dmotion = tstatus->dmotion;
	md.div_ = skill_get_num(skill_id, skill_lv);
	md.blewcount = skill_get_blewcount(skill_id, skill_lv);
	md.miscflag = mflag;
	md.flag = BF_MISC|BF_SKILL;
	md.dmg_lv = ATK_DEF;
	nk = skill_get_nk(skill_id);

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);

	if(sd) {
		sd->state.arrow_atk = 0;
		md.blewcount += battle_blewcount_bonus(sd, skill_id);
	}

	s_ele = skill_get_ele(skill_id, skill_lv);
	if(s_ele < 0 && s_ele != -3) //Attack that takes weapon's element for misc attacks? Make it neutral [Skotlex]
		s_ele = ELE_NEUTRAL;
	else if(s_ele == -3) //Use random element
		s_ele = rnd()%ELE_ALL;

	switch(skill_id) {
#ifdef RENEWAL
		case CR_GRANDCROSS:
			s_ele = ELE_HOLY;
			break;
#endif
		case RA_FIRINGTRAP:
			s_ele = ELE_FIRE;
			break;
		case RA_ICEBOUNDTRAP:
			s_ele = ELE_WATER;
			break;
	}

	//Skill Range Criteria
	md.flag |= battle_range_type(src, target, skill_id, skill_lv);

	switch(skill_id) {
		case TF_THROWSTONE:
			md.damage = (sd ? 50 : 30);
			break;
#ifdef RENEWAL
		case HT_LANDMINE:
		case MA_LANDMINE:
		case HT_BLASTMINE:
		case HT_CLAYMORETRAP:
			md.damage = (int64)(skill_lv * sstatus->dex * (3.0 + (float)status_get_lv(src) / 100.0) * (1.0 + (float)sstatus->int_ / 35.0));
			md.damage += md.damage * (rnd()%20 - 10) / 100;
			md.damage += 40 * (sd ? pc_checkskill(sd, RA_RESEARCHTRAP) : 5);
			break;
#else
		case HT_LANDMINE:
		case MA_LANDMINE:
			md.damage = skill_lv * (sstatus->dex + 75) * (100 + sstatus->int_) / 100;
			break;
		case HT_BLASTMINE:
			md.damage = skill_lv * (sstatus->dex / 2 + 50) * (100 + sstatus->int_) / 100;
			break;
		case HT_CLAYMORETRAP:
			md.damage = skill_lv * (sstatus->dex / 2 + 75) * (100 + sstatus->int_) / 100;
			break;
#endif
		case HT_BLITZBEAT:
		case SN_FALCONASSAULT:
			{
				uint8 lv = 0;

				//Blitz-beat Damage
				if(!sd || !(lv = pc_checkskill(sd, HT_STEELCROW)))
					lv = 0;
				md.damage = (sstatus->dex / 10 + sstatus->int_ / 2 + lv * 3 + 40) * 2;
				if(md.miscflag > 1) //Autocasted Blitz
					nk |= NK_SPLASHSPLIT;
				if(skill_id == SN_FALCONASSAULT) {
					DAMAGE_DIV_FIX2(md.damage, skill_get_num(HT_BLITZBEAT, 5)); //Div fix of Blitzbeat
					md.damage = md.damage * (150 + 70 * skill_lv) / 100; //Falcon Assault Modifier
				}
			}
			break;
#ifdef RENEWAL
		case AM_DEMONSTRATION:
		case AM_ACIDTERROR:
			{
				//Official renewal formula [exneval]
				//Damage = (Final ATK + Final MATK) * Skill modifiers - (eDEF + sDEF + eMDEF + sMDEF)
				struct Damage atk, matk;
				short totaldef, totalmdef;

				totaldef = (short)status_get_def(target) + tstatus->def2;
				totalmdef = tstatus->mdef + tstatus->mdef2;
				atk = battle_calc_weapon_attack(src, target, skill_id, skill_lv, md.miscflag);
				matk = battle_calc_magic_attack(src, target, skill_id, skill_lv, md.miscflag);
				md.damage = atk.damage + matk.damage;
				md.damage -= totaldef + totalmdef;
				md.damage += md.damage * (sd ? sd->bonus.long_attack_atk_rate : 0) / 100;
				if(skill_id == AM_ACIDTERROR && status_has_mode(tstatus, MD_STATUS_IMMUNE))
					md.damage /= 2;
				nk |= NK_NO_ELEFIX|NK_NO_CARDFIX_DEF;
				is_hybrid_dmg = true;
			}
			break;
#endif
		case BA_DISSONANCE:
			md.damage = 30 + 10 * skill_lv + 3 * (sd ? pc_checkskill(sd, BA_MUSICALLESSON) : 10);
			break;
		case NPC_SELFDESTRUCTION:
			md.damage = sstatus->hp;
			break;
		case NPC_SMOKING:
			md.damage = 3;
			break;
		case NPC_DARKBREATH:
			md.damage = tstatus->max_hp * skill_lv / 10;
			break;
		case NPC_EVILLAND:
			md.damage = skill_calc_heal(src, target, skill_id, skill_lv, false);
			break;
		case NPC_MAXPAIN_ATK:
		case OB_OBOROGENSOU_TRANSITION_ATK:
			md.damage = battle_damage_temp[0] * skill_lv / 10;
			break;
		case NPC_WIDESUCK:
			md.damage = tstatus->max_hp * 15 / 100;
			break;
		case NPC_GRANDDARKNESS:
		case CR_GRANDCROSS:
			{
				//Official renewal formula [exneval]
				//Base Damage = [((ATK + MATK) / 2 * skillratio) - (eDEF + sDEF + eMDEF + sMDEF)]
				//Final Damage = Base Damage * Holy attr modifier * Long range atk modifier * Holy attr modifier
				int skillratio = (100 + 40 * skill_lv) / 100;
				struct Damage atk, matk;
#ifdef RENEWAL
				short totaldef, totalmdef;

				totaldef = (short)status_get_def(target) + tstatus->def2;
				totalmdef = tstatus->mdef + tstatus->mdef2;
#endif
				atk = battle_calc_weapon_attack(src, target, skill_id, skill_lv, md.miscflag);
				matk = battle_calc_magic_attack(src, target, skill_id, skill_lv, md.miscflag);
#ifdef RENEWAL
				md.damage = (atk.damage + matk.damage) / 2 * skillratio;
				md.damage -= totaldef + totalmdef;
				if(target->id != src->id) {
					md.damage = battle_attr_fix(src, target, md.damage, s_ele, tstatus->def_ele, tstatus->ele_lv);
					md.damage += md.damage * (sd ? sd->bonus.long_attack_atk_rate : 0) / 100;
				}
#else
				md.damage = (atk.damage + matk.damage) * skillratio;
#endif
				if(target->id == src->id)
					md.damage /= 2;
				nk |= NK_NO_CARDFIX_DEF;
				is_hybrid_dmg = true;
			}
			break;
		case ASC_BREAKER: {
				//Official renewal formula [helvetica]
				//Damage = ((ATK + MATK) * (3 + (.5 * Skill level))) - (eDEF + sDEF + eMDEF + sMDEF)
				struct Damage atk, matk;
#ifdef RENEWAL
				short totaldef, totalmdef;

				totaldef = (short)status_get_def(target) + tstatus->def2;
				totalmdef = tstatus->mdef + tstatus->mdef2;
#endif
				atk = battle_calc_weapon_attack(src, target, skill_id, skill_lv, md.miscflag);
				matk = battle_calc_magic_attack(src, target, skill_id, skill_lv, md.miscflag);
#ifdef RENEWAL
				md.damage = (atk.damage + matk.damage) * (30 + 5 * skill_lv) / 10;
				md.damage -= totaldef + totalmdef;
				md.damage += md.damage * (sd ? sd->bonus.long_attack_atk_rate : 0) / 100;
#else
				md.damage = atk.damage + matk.damage;
				nk |= NK_IGNORE_FLEE;
#endif
				nk |= NK_NO_ELEFIX|NK_NO_CARDFIX_DEF;
				is_hybrid_dmg = true;
			}
			break;
		case HW_GRAVITATION:
#ifdef RENEWAL
			md.damage = 500 + 100 * skill_lv;
#else
			md.damage = 200 + 200 * skill_lv;
#endif
			md.dmotion = 0; //No flinch animation
			break;
		case PA_PRESSURE:
			md.damage = 500 + 300 * skill_lv;
			break;
		case PA_GOSPEL:
			if(mflag)
				md.damage = rnd()%4000 + 1500;
			else {
				md.damage = rnd()%5000 + 3000;
#ifdef RENEWAL
				md.damage -= (int64)status_get_def(target);
#else
				md.damage -= md.damage * (int64)status_get_def(target) / 100;
#endif
				md.damage -= tstatus->def2;
				md.damage = i64max(md.damage, 0);
			}
			break;
		case CR_ACIDDEMONSTRATION:
		case GN_FIRE_EXPANSION_ACID:
#ifdef RENEWAL
			{ //[malufett]
				struct Damage atk, matk;
				float vitfactor = 0.0f, damagevalue;
				short targetVit = min(tstatus->vit, 120);
				short totaldef, totalmdef;

				totaldef = (short)status_get_def(target) + tstatus->def2;
				totalmdef = tstatus->mdef + tstatus->mdef2;
				atk = battle_calc_weapon_attack(src, target, skill_id, skill_lv, md.miscflag);
				matk = battle_calc_magic_attack(src, target, skill_id, skill_lv, md.miscflag);
				if((vitfactor = (tstatus->vit - 120.0f)) > 0 && sd)
					vitfactor = (vitfactor * (atk.damage + matk.damage) / 10) / tstatus->vit;
				damagevalue = max(vitfactor, 0) + (targetVit * (atk.damage + matk.damage)) / 10;
				md.damage = (int64)(damagevalue * 70 * skill_lv / 100);
				if(tsd)
					md.damage /= 2;
				md.damage -= (totaldef + totalmdef) / 2;
				md.damage += md.damage * (sd ? sd->bonus.long_attack_atk_rate : 0) / 100;
				nk |= NK_NO_CARDFIX_DEF;
				is_hybrid_dmg = true;
			}
#else
			if(tstatus->vit + sstatus->int_) //Crash fix
				md.damage = (int64)(7 * tstatus->vit * sstatus->int_ * sstatus->int_ / (10 * (tstatus->vit + sstatus->int_)));
			if(tsd)
				md.damage /= 2;
#endif
			break;
		case NJ_ZENYNAGE:
		case KO_MUCHANAGE:
			{
				uint8 lv = 0;

				md.damage = skill_get_zeny(skill_id, skill_lv);
				if(!md.damage)
					md.damage = (skill_id == NJ_ZENYNAGE ? 2 : 10);
				md.damage = (skill_id == NJ_ZENYNAGE ? rnd()%md.damage + md.damage : md.damage * battle_damage_temp[0]) /
					(skill_id == NJ_ZENYNAGE ? 1 : 100);
				if(skill_id == KO_MUCHANAGE && (lv = (sd ? pc_checkskill(sd, NJ_TOBIDOUGU) : 10)) < 10)
					md.damage = md.damage / 2;
				if(status_get_class_(target) == CLASS_BOSS) //Specific to Boss Class
					md.damage = md.damage / (skill_id == NJ_ZENYNAGE ? 3 : 2);
				else if(skill_id == NJ_ZENYNAGE && tsd)
					md.damage = md.damage / 2;
			}
			break;
		case GS_FLING:
			md.damage = status_get_job_lv(src);
			break;
		case HVAN_EXPLOSION: //[orn]
			md.damage = sstatus->max_hp * (50 + 50 * skill_lv) / 100;
			break;
		case RA_CLUSTERBOMB:
		case RA_FIRINGTRAP:
		case RA_ICEBOUNDTRAP:
			{
				struct Damage atk = battle_calc_weapon_attack(src, target, skill_id, skill_lv, md.miscflag);

				md.damage = skill_lv * sstatus->dex + sstatus->int_ * 5;
				RE_LVL_TMDMOD();
				md.damage = md.damage * 20 * (sd ? pc_checkskill(sd, RA_RESEARCHTRAP) : 5) / (skill_id == RA_CLUSTERBOMB ? 50 : 100);
				md.damage += atk.damage;
				nk |= NK_IGNORE_FLEE|NK_NO_CARDFIX_DEF;
				is_hybrid_dmg = true;
			}
			break;
		case NC_MAGMA_ERUPTION_DOTDAMAGE:
			md.damage = 800 + 200 * skill_lv;
			break;
		case LG_RAYOFGENESIS: {
				struct Damage atk, matk;

				atk = battle_calc_weapon_attack(src, target, skill_id, skill_lv, md.miscflag);
				matk = battle_calc_magic_attack(src, target, skill_id, skill_lv, md.miscflag);
				md.damage = (atk.damage + matk.damage) * 7;
				nk |= NK_NO_CARDFIX_DEF;
				is_hybrid_dmg = true;
			}
			break;
		case WM_SOUND_OF_DESTRUCTION:
			md.damage = 1000 * skill_lv + sstatus->int_ * (sd ? pc_checkskill(sd, WM_LESSON) : 10);
			md.damage += md.damage * 10 * skill_chorus_count(sd, 0) / 100;
			break;
		case SO_VARETYR_SPEAR: {
				struct Damage atk, matk;
				short totaldef, totalmdef;

				totaldef = (short)status_get_def(target) + tstatus->def2;
				totalmdef = tstatus->mdef + tstatus->mdef2;
				atk = battle_calc_weapon_attack(src, target, skill_id, skill_lv, md.miscflag);
				matk = battle_calc_magic_attack(src, target, skill_id, skill_lv, md.miscflag);
				md.damage = atk.damage + matk.damage;
				md.damage -= totaldef + totalmdef;
				nk |= NK_NO_CARDFIX_DEF;
				is_hybrid_dmg = true;
			}
			break;
		case GN_THORNS_TRAP:
			md.damage = 100 + 200 * skill_lv + sstatus->int_;
			break;
		case GN_BLOOD_SUCKER:
			md.damage = 200 + 100 * skill_lv + sstatus->int_;
			break;
		case GN_HELLS_PLANT_ATK:
			md.damage = 10 * skill_lv * status_get_lv(target) + 7 * sstatus->int_ / 2 * (18 + status_get_job_lv(src) / 4) * 5 / (10 - (sd ? pc_checkskill(sd, AM_CANNIBALIZE) : 5));
			break;
		case RL_B_TRAP:
			md.damage = 3 * skill_lv * tstatus->hp / 100;
			if(status_has_mode(tstatus, MD_STATUS_IMMUNE)) //HP damage dealt is 1/10 the amount on boss monsters
				md.damage = md.damage / 10;
			md.damage += 10 * sstatus->dex;
			break;
		case SU_SV_ROOTTWIST_ATK:
			md.damage = 100;
			break;
		case MH_EQC:
			md.damage = max((int)(tstatus->hp - sstatus->hp), 0);
			break;
	}

	if(nk&NK_SPLASHSPLIT) { //Divide ATK among targets
		if(md.miscflag > 0)
			md.damage /= md.miscflag;
		else
			ShowError("0 enemies targeted by %d:%s, divide per 0 avoided!\n", skill_id, skill_get_name(skill_id));
	}

	if(!(nk&NK_IGNORE_FLEE)) {
		struct status_change *sc = status_get_sc(target);

		i = 0; //Temp for "hit or no hit"
		if(sc && sc->opt1 && sc->opt1 != OPT1_STONEWAIT && sc->opt1 != OPT1_BURNING)
			i = 1;
		else {
			short flee = tstatus->flee,
#ifdef RENEWAL
				hitrate = 0; //Default hitrate
#else
				hitrate = 80; //Default hitrate
#endif

			if(battle_config.agi_penalty_type && battle_config.agi_penalty_target&target->type) {
				unsigned char attacker_count = unit_counttargeted(target); //256 max targets should be a sane max

				if(attacker_count >= battle_config.agi_penalty_count) {
					if(battle_config.agi_penalty_type == 1)
						flee = (flee * (100 - (attacker_count - (battle_config.agi_penalty_count - 1)) * battle_config.agi_penalty_num)) / 100;
					else //Assume type 2: absolute reduction
						flee -= (attacker_count - (battle_config.agi_penalty_count - 1)) * battle_config.agi_penalty_num;
					if(flee < 1)
						flee = 1;
				}
			}
			hitrate += sstatus->hit - flee;
#ifdef RENEWAL //In renewal, hit bonus from Vultures Eye is not shown anymore in status window
			if(sd)
				hitrate += pc_checkskill(sd, AC_VULTURE);
#endif
			hitrate = cap_value(hitrate, battle_config.min_hitrate, battle_config.max_hitrate);
			if(rnd()%100 < hitrate)
				i = 1;
		}
		if(!i) {
			md.damage = 0;
			md.dmg_lv = ATK_FLEE;
		}
	}

	md.damage += battle_calc_cardfix(BF_MISC, src, target, nk, s_ele, 0, md.damage, 0, md.flag);

	if(!(nk&NK_NO_ELEFIX) && md.damage > 0)
		md.damage = battle_attr_fix(src, target, md.damage, s_ele, tstatus->def_ele, tstatus->ele_lv);

	//Plant damage
	if(md.damage < 0)
		md.damage = 0;
	else if(md.damage && is_infinite_defense(target, md.flag))
		md.damage = 1;

	//Apply DAMAGE_DIV_FIX and check for min damage
	md = battle_apply_div_fix(md, skill_id);

	if(!is_hybrid_dmg) //Modifiers for hybrid damage already done
		md.damage = battle_calc_damage_sub(src, target, &md, md.damage, skill_id, skill_lv);

	switch(skill_id) {
		case TF_THROWSTONE:
		case HT_LANDMINE:
		case MA_LANDMINE:
		case HT_BLASTMINE:
		case HT_CLAYMORETRAP:
		case HT_BLITZBEAT:
		case SN_FALCONASSAULT:
#ifdef RENEWAL
		case AM_DEMONSTRATION:
		case AM_ACIDTERROR:
#endif
		case BA_DISSONANCE:
		case ASC_BREAKER:
		case GS_FLING:
		case CR_ACIDDEMONSTRATION:
		case GN_FIRE_EXPANSION_ACID:
		case RA_CLUSTERBOMB:
		case RA_FIRINGTRAP:
		case RA_ICEBOUNDTRAP:
		case NC_MAGMA_ERUPTION_DOTDAMAGE:
		case LG_RAYOFGENESIS:
		case RL_B_TRAP:
			md.flag |= BF_WEAPON;
			break;
		case NPC_GRANDDARKNESS:
		case CR_GRANDCROSS:
			if(target->id != src->id)
				md.flag |= BF_MAGIC;
			else if(!sd)
				md.damage = 0;
			break;
		case WM_SOUND_OF_DESTRUCTION:
		case SO_VARETYR_SPEAR:
		case GN_THORNS_TRAP:
		case GN_BLOOD_SUCKER:
		case GN_HELLS_PLANT_ATK:
			md.flag |= BF_MAGIC;
			break;
		case NJ_ZENYNAGE:
			if(sd) {
				if(md.damage > sd->status.zeny)
					md.damage = sd->status.zeny;
				pc_payzeny(sd, (int)cap_value(md.damage, INT_MIN, INT_MAX), LOG_TYPE_STEAL, NULL);
			}
		//Fall through
		case KO_MUCHANAGE:
			md.flag |= BF_WEAPON;
			break;
	}

	if(md.flag&BF_WEAPON && target->id != src->id) {
		int64 damage = md.damage, rdamage = 0;
		int tick = gettick(), rdelay = 0;

		rdamage = battle_calc_return_damage(target, src, &damage, md.flag, skill_id, false);
		if(rdamage > 0) {
			struct block_list *d_bl = battle_check_devotion(src);

			rdelay = clif_damage(src, (!d_bl ? src : d_bl), tick, md.amotion, sstatus->dmotion, rdamage, 1, DMG_ENDURE, 0, false);
			if(tsd)
				battle_drain(tsd, src, rdamage, rdamage, sstatus->race, sstatus->class_, false);
			battle_delay_damage(tick, md.amotion, target, (!d_bl ? src : d_bl), 0, CR_REFLECTSHIELD, 0, rdamage, ATK_DEF, rdelay, true, false, false);
			skill_additional_effect(target, (!d_bl ? src : d_bl), CR_REFLECTSHIELD, 1, BF_WEAPON|BF_SHORT|BF_NORMAL, ATK_DEF, tick);
		}
	}

	md.damage = battle_calc_damage(src, target, &md, md.damage, skill_id, skill_lv);
	if(map_flag_gvg2(target->m))
		md.damage = battle_calc_gvg_damage(src, target, md.damage, skill_id, md.flag);
	else if(map[target->m].flag.battleground)
		md.damage = battle_calc_bg_damage(src, target, md.damage, skill_id, md.flag);

	if(sd) {
		if((i = pc_skillatk_bonus(sd, skill_id)))
			md.damage += md.damage * i / 100;
		if((i = battle_adjust_skill_damage(src->m, skill_id)))
			md.damage = md.damage * i / 100;
	}

	if(tsd && (i = pc_sub_skillatk_bonus(tsd, skill_id)))
		md.damage -= md.damage * i / 100;

#ifdef ADJUST_SKILL_DAMAGE
	if((skill_damage = battle_skill_damage(src, target, skill_id)))
		md.damage += (int64)md.damage * skill_damage / 100;
#endif

	battle_absorb_damage(target, &md);

	if(md.flag&BF_WEAPON)
		battle_do_reflect(&md, src, target, skill_id, skill_lv);

	return md;
}

/*==========================================
 * Battle main entry, from skill_attack
 *------------------------------------------
 * Credits:
 *	Original coder unknown
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
struct Damage battle_calc_attack(int attack_type, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int flag)
{
	struct Damage d;

	switch(attack_type) {
		case BF_WEAPON: d = battle_calc_weapon_attack(src,target,skill_id,skill_lv,flag); break;
		case BF_MAGIC:  d = battle_calc_magic_attack(src,target,skill_id,skill_lv,flag);  break;
		case BF_MISC:   d = battle_calc_misc_attack(src,target,skill_id,skill_lv,flag);   break;
		default:
			ShowError("battle_calc_attack: unknown attack: attack_type=%d skill_id=%d\n",attack_type,skill_id);
			memset(&d,0,sizeof(d));
			break;
	}

	if(d.damage + d.damage2 < 1) { //Miss/Absorbed
		//Weapon attacks should go through to cause additional effects
		if(d.dmg_lv == ATK_DEF /*&& attack_type&(BF_MAGIC|BF_MISC)*/) //Isn't it that additional effects don't apply if miss?
			d.dmg_lv = ATK_MISS;
		d.dmotion = 0;
	} else //Some skills like Weaponry Research will cause damage even if attack is dodged
		d.dmg_lv = ATK_DEF;

	return d;
}

/*==========================================
 * Final damage return function
 *------------------------------------------
 * Credits:
 *	Original coder unknown
 *	Initial refactoring by Baalberith
 *	Refined and optimized by helvetica
 */
int64 battle_calc_return_damage(struct block_list *bl, struct block_list *src, int64 *dmg, int flag, uint16 skill_id, bool status_reflect) {
	struct map_session_data *sd = BL_CAST(BL_PC,bl);
	struct status_change *sc = status_get_sc(bl);
	struct status_change *ssc = status_get_sc(src);
	int64 rdamage = 0, damage = *dmg;
#ifdef RENEWAL
	int max_rdamage = status_get_max_hp(bl);
#endif

#ifdef RENEWAL
	#define CAP_RDAMAGE(d) ( (d) = cap_value((d),1,max_rdamage) )
#else
	#define CAP_RDAMAGE(d) ( (d) = i64max((d),1) )
#endif

	if( flag&BF_SHORT ) { //Bounces back part of the damage
		if( !status_reflect && sd && sd->bonus.short_weapon_damage_return ) {
			rdamage += damage * sd->bonus.short_weapon_damage_return / 100;
			CAP_RDAMAGE(rdamage);
		} else if( status_reflect && sc && sc->count ) {
			struct status_change_entry *sce = NULL;

			if( (sce = sc->data[SC_REFLECTSHIELD]) || (sce = battle_check_shadowform(bl,SC_REFLECTSHIELD)) ) {
				struct status_change_entry *sce_d = sc->data[SC_DEVOTION];
				struct status_change_entry *sce_s = sc->data[SC__SHADOWFORM];
				struct map_session_data *s_sd = NULL;
				struct block_list *d_bl = NULL;

				if( sce_d && (d_bl = map_id2bl(sce_d->val1)) &&
					((d_bl->type == BL_MER && ((TBL_MER *)d_bl)->master && ((TBL_MER *)d_bl)->master->bl.id == bl->id) ||
					(d_bl->type == BL_PC && ((TBL_PC *)d_bl)->devotion[sce_d->val2] == bl->id)) )
				{ //Don't reflect non-skill attack if has SC_REFLECTSHIELD from Devotion bonus inheritance
					if( (!skill_id && battle_config.devotion_rdamage_skill_only && sce->val4) ||
						!check_distance_bl(bl,d_bl,sce_d->val3) )
						return 0;
				} else if( sce_s && (s_sd = map_id2sd(sce_s->val2)) && s_sd->shadowform_id == bl->id && !skill_id )
					return 0;
#ifndef RENEWAL
				if( !battle_skill_check_no_cardfix_atk(skill_id) )
					return 0;
				switch( skill_id ) {
					case KN_PIERCE:
					case ML_PIERCE:
						{
							struct status_data *sstatus = status_get_status_data(bl);
							short count = sstatus->size + 1;

							damage = damage / count;
						}
						break;
				}
#endif
				rdamage += damage * sce->val2 / 100;
				CAP_RDAMAGE(rdamage);
			}
			if( (sce = sc->data[SC_REFLECTDAMAGE]) && !battle_skill_check_no_cardfix_atk(skill_id) &&
				!(skill_get_inf2(skill_id)&INF2_TRAP) && rnd()%100 < 30 + 10 * sce->val1 ) {
				rdamage += damage * sce->val2 / 100;
#ifdef RENEWAL
				max_rdamage = max_rdamage * status_get_lv(bl) / 100;
#endif
				CAP_RDAMAGE(rdamage);
				if( --(sce->val3) <= 0 )
					status_change_end(bl,SC_REFLECTDAMAGE,INVALID_TIMER);
			}
			if( !status_bl_has_mode(src,MD_STATUS_IMMUNE) ) {
				if( (sce = sc->data[SC_DEATHBOUND]) && !battle_skill_check_no_cardfix_atk(skill_id) ) {
					uint8 dir = map_calc_dir(bl,src->x,src->y), t_dir = unit_getdir(bl);
					int64 rd1 = 0;

					if( distance_bl(bl,src) <= 0 || !map_check_dir(dir,t_dir) ) {
						rd1 = i64min(damage,status_get_max_hp(bl)) * sce->val2 / 100; //Amplify damage
						*dmg = rd1 * 30 / 100; //Player receives 30% of the amplified damage
						clif_skill_damage(bl,src,gettick(),status_get_amotion(bl),0,-30000,1,RK_DEATHBOUND,-1,DMG_SKILL);
						skill_blown(bl,src,skill_get_blewcount(RK_DEATHBOUND,sce->val1),unit_getdir(src),0);
						status_change_end(bl,SC_DEATHBOUND,INVALID_TIMER);
						status_change_end(bl,SC_TELEPORT_FIXEDCASTINGDELAY,INVALID_TIMER);
						rdamage += rd1 * 70 / 100; //bl receives 70% of the amplified damage [Rytech]
					}
				}
				if( ((sce = sc->data[SC_SHIELDSPELL_DEF]) || (sce = battle_check_shadowform(bl,SC_SHIELDSPELL_DEF))) && sce->val1 == 2 ) {
					rdamage += damage * sce->val2 / 100;
					CAP_RDAMAGE(rdamage);
				}
			}
		}
	} else if( !status_reflect && sd && sd->bonus.long_weapon_damage_return ) {
		rdamage += damage * sd->bonus.long_weapon_damage_return / 100;
		CAP_RDAMAGE(rdamage);
	}

	if( status_reflect ) {
		if( ssc && ssc->data[SC_INSPIRATION] ) {
			rdamage += damage / 100;
			CAP_RDAMAGE(rdamage);
		}
		if( sc && sc->data[SC_KYOMU] && !sc->data[SC_SHIELDSPELL_DEF] )
			return 0; //Nullify reflecting ability except for Shield Spell DEF
	}

	return rdamage;
#undef CAP_RDAMAGE
}

/**
 * Calculate vanish from target
 * @param sd: Player with vanish item
 * @param target: Target to vanish HP/SP
 * @param wd: Reference to Damage struct
 */
void battle_vanish(struct map_session_data *sd, struct block_list *target, struct Damage *wd)
{
	struct status_data *tstatus;
	int race;

	nullpo_retv(sd);
	nullpo_retv(target);
	nullpo_retv(wd);

	tstatus = status_get_status_data(target);
	race = status_get_race(target);
	wd->isvanishdamage = false;
	wd->isspdamage = false;

	if( wd->flag ) {
		short vellum_rate_hp = cap_value(sd->hp_vanish_race[race].rate + sd->hp_vanish_race[RC_ALL].rate, 0, SHRT_MAX);
		short vellum_hp = cap_value(sd->hp_vanish_race[race].per + sd->hp_vanish_race[RC_ALL].per, SHRT_MIN, SHRT_MAX);
		short vellum_rate_sp = cap_value(sd->sp_vanish_race[race].rate + sd->sp_vanish_race[RC_ALL].rate, 0, SHRT_MAX);
		short vellum_sp = cap_value(sd->sp_vanish_race[race].per + sd->sp_vanish_race[RC_ALL].per, SHRT_MIN, SHRT_MAX);

		//The HP and SP vanish bonus from these items can't stack because of the special damage display
		if( vellum_hp && vellum_rate_hp && (vellum_rate_hp >= 1000 || rnd()%1000 < vellum_rate_hp) ) {
			wd->damage = apply_rate(tstatus->max_hp, vellum_hp);
			wd->damage2 = 0;
			wd->isvanishdamage = true;
		} else if( vellum_sp && vellum_rate_sp && (vellum_rate_sp >= 1000 || rnd()%1000 < vellum_rate_sp) ) {
			wd->damage = apply_rate(tstatus->max_sp, vellum_sp);
			wd->damage2 = 0;
			wd->isvanishdamage = true;
			wd->isspdamage = true;
		}
		if( (wd->type == DMG_CRITICAL || wd->type == DMG_MULTI_HIT_CRITICAL) && wd->isvanishdamage )
			wd->type = DMG_NORMAL;
	} else {
		short vrate_hp = cap_value(sd->bonus.hp_vanish_rate, 0, SHRT_MAX);
		short v_hp = cap_value(sd->bonus.hp_vanish_per, SHRT_MIN, SHRT_MAX);
		short vrate_sp = cap_value(sd->bonus.sp_vanish_rate, 0, SHRT_MAX);
		short v_sp = cap_value(sd->bonus.sp_vanish_per, SHRT_MIN, SHRT_MAX);

		if( v_hp && vrate_hp && (vrate_hp >= 1000 || rnd()%1000 < vrate_hp) )
			v_hp = -v_hp;
		else
			v_hp = 0;
		if( v_sp && vrate_sp && (vrate_sp >= 1000 || rnd()%1000 < vrate_sp) )
			v_sp = -v_sp;
		else
			v_sp = 0;
		if( v_hp < 0 || v_sp < 0 )
			status_percent_damage(&sd->bl, target, (int8)v_hp, (int8)v_sp, false);
	}
}

/*===========================================
 * Perform battle drain effects (HP/SP loss)
 *-------------------------------------------*/
void battle_drain(struct map_session_data *sd, struct block_list *tbl, int64 rdamage, int64 ldamage, int race, int class_, bool isdraindamage)
{
	struct weapon_data *wd;
	int64 *damage;
	struct Damage d;
	int thp = 0, //HP gained by attacked
		tsp = 0, //SP gained by attacked
		hp = 0, sp = 0;
	uint8 i = 0;

	if( !CHK_RACE(race) || !CHK_CLASS(class_) )
		return;

	memset(&d, 0, sizeof(d));

	//Check for vanish HP/SP
	battle_vanish(sd, tbl, &d);

	//Check for drain HP/SP
	hp = sp = i = 0;
	for( i = 0; i < 4; i++ ) {
		if( i < 2 ) { //First two iterations: Right hand
			wd = &sd->right_weapon;
			damage = &rdamage;
		} else {
			wd = &sd->left_weapon;
			damage = &ldamage;
		}
		if( *damage <= 0 )
			continue;
		if( i == 1 || i == 3 ) { //First and Third iterations: class state, other race state
			hp = wd->hp_drain_class[class_] + wd->hp_drain_class[CLASS_ALL];
			sp = wd->sp_drain_class[class_] + wd->sp_drain_class[CLASS_ALL];
			if( isdraindamage ) {
				hp += battle_calc_drain(*damage, wd->hp_drain_rate.rate, wd->hp_drain_rate.per);
				sp += battle_calc_drain(*damage, wd->sp_drain_rate.rate, wd->sp_drain_rate.per);
			}
			if( hp )
				thp += hp;
			if( sp )
				tsp += sp;
		} else {
			hp = wd->hp_drain_race[race] + wd->hp_drain_race[RC_ALL];
			sp = wd->sp_drain_race[race] + wd->sp_drain_race[RC_ALL];
			if( hp )
				thp += hp;
			if( sp )
				tsp += sp;
		}
	}

	if( !thp && !tsp )
		return;

	status_heal(&sd->bl, thp, tsp, (battle_config.show_hp_sp_drain ? 2 : 0));
}

/*===========================================
 * Deals the same damage to targets in area.
 *-------------------------------------------
 * Credits:
 *	Original coder pakpil
 */
int battle_damage_area(struct block_list *bl, va_list ap) {
	unsigned int tick;
	int64 damage;
	int amotion, dmotion;
	struct block_list *src = NULL;
	struct unit_data *ud = NULL;

	nullpo_ret(bl);

	tick = va_arg(ap, unsigned int);
	src = va_arg(ap, struct block_list *);
	amotion = va_arg(ap, int);
	dmotion = va_arg(ap, int);
	damage = va_arg(ap, int);

	if( ((ud = unit_bl2ud(bl)) && ud->immune_attack) || status_bl_has_mode(bl, MD_SKILL_IMMUNE) || status_get_class(bl) == MOBID_EMPERIUM )
		return 0;
	if( bl->id != src->id && battle_check_target(src, bl, BCT_ENEMY) > 0 ) {
		map_freeblock_lock();
		if( src->type == BL_PC )
			battle_drain((TBL_PC *)src, bl, damage, damage, status_get_race(bl), status_get_class_(bl), false);
		if( amotion )
			battle_delay_damage(tick, amotion, src, bl, 0, CR_REFLECTSHIELD, 0, damage, ATK_DEF, 0, true, false, false);
		else
			status_fix_damage(src, bl, damage, 0);
		clif_damage(bl, bl, tick, amotion, dmotion, damage, 1, DMG_ENDURE, 0, false);
		if( !(src->type == BL_PC && ((TBL_PC *)src)->state.autocast) )
			skill_additional_effect(src, bl, CR_REFLECTSHIELD, 1, BF_WEAPON|BF_SHORT|BF_NORMAL, ATK_DEF, tick);
		map_freeblock_unlock();
	}

	return 0;
}

/*==========================================
 * Do a basic physical attack (call through unit_attack_timer)
 *------------------------------------------*/
enum damage_lv battle_weapon_attack(struct block_list *src, struct block_list *target, unsigned int tick, int flag) {
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct status_data *sstatus, *tstatus;
	struct status_change *sc, *tsc;
	int64 damage;
	uint8 lv;
	struct Damage wd;

	nullpo_retr(ATK_NONE,src);
	nullpo_retr(ATK_NONE,target);

	if (!src->prev || !target->prev)
		return ATK_NONE;

	sd = BL_CAST(BL_PC,src);
	tsd = BL_CAST(BL_PC,target);

	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(target);

	sc = status_get_sc(src);
	tsc = status_get_sc(target);

	if (sc && !sc->count) //Avoid sc checks when there's none to check for [Skotlex]
		sc = NULL;

	if (tsc && !tsc->count)
		tsc = NULL;

	if (sd) {
		sd->state.arrow_atk = (sd->status.weapon == W_BOW || (sd->status.weapon >= W_REVOLVER && sd->status.weapon <= W_GRENADE));
		if (sd->state.arrow_atk) {
			short index = sd->equip_index[EQI_AMMO];

			if (index < 0 || !sd->inventory_data[index])
				return ATK_NONE;
		}
	}

	if (sc && sc->count) {
		if (sc->data[SC_CLOAKING] && !(sc->data[SC_CLOAKING]->val4&2))
			status_change_end(src,SC_CLOAKING,INVALID_TIMER);
		else if (sc->data[SC_CLOAKINGEXCEED] && !(sc->data[SC_CLOAKINGEXCEED]->val4&2))
			status_change_end(src,SC_CLOAKINGEXCEED,INVALID_TIMER);
	}

	if (tsc) {
		if (tsc->data[SC_AUTOCOUNTER] && status_check_skilluse(target,src,KN_AUTOCOUNTER,1)) {
			uint8 dir = map_calc_dir(target,src->x,src->y);
			int t_dir = unit_getdir(target);
			int dist = distance_bl(src,target);

			if (dist <= 0 || (!map_check_dir(dir,t_dir) && dist <= tstatus->rhw.range + 1)) {
				uint16 skill_lv = tsc->data[SC_AUTOCOUNTER]->val1;

				clif_skillcastcancel(target); //Remove the casting bar [Skotlex]
				clif_damage(src,target,tick,sstatus->amotion,1,0,1,DMG_NORMAL,0,false); //Display miss
				status_change_end(target,SC_AUTOCOUNTER,INVALID_TIMER);
				skill_attack(BF_WEAPON,target,target,src,KN_AUTOCOUNTER,skill_lv,tick,0);
				return ATK_BLOCK;
			}
		}
		if (tsc->data[SC_BLADESTOP_WAIT] && !status_bl_has_mode(src, MD_STATUS_IMMUNE) && (src->type == BL_PC || !tsd ||
			distance_bl(src,target) <= (tsd->status.weapon == W_FIST ? 1 : 2))) {
			uint16 skill_lv = tsc->data[SC_BLADESTOP_WAIT]->val1;
			int duration = skill_get_time2(MO_BLADESTOP,skill_lv);

			status_change_end(target,SC_BLADESTOP_WAIT,INVALID_TIMER);
			//Target locked
			if (sc_start4(src,src,SC_BLADESTOP,100,(sd ? pc_checkskill(sd,MO_BLADESTOP) : 5),0,0,target->id,duration)) {
				clif_damage(src,target,tick,sstatus->amotion,1,0,1,DMG_NORMAL,0,false);
				clif_bladestop(target,src->id,1);
				sc_start4(src,target,SC_BLADESTOP,100,skill_lv,0,0,src->id,duration);
				return ATK_BLOCK;
			}
		}
	}

	if (sd && (lv = pc_checkskill(sd,MO_TRIPLEATTACK)) > 0) {
		int rate = 30 - lv; //Base rate

		if (sc && sc->data[SC_SKILLRATE_UP] && sc->data[SC_SKILLRATE_UP]->val1 == MO_TRIPLEATTACK) {
			rate += rate * sc->data[SC_SKILLRATE_UP]->val2 / 100;
			status_change_end(src,SC_SKILLRATE_UP,INVALID_TIMER);
		}
		if (rnd()%100 < rate) { //Need to apply canact_tick here because it doesn't go through skill_castend_id
			sd->ud.canact_tick = max(tick + skill_delayfix(src,MO_TRIPLEATTACK,lv),sd->ud.canact_tick);
			if (skill_attack(BF_WEAPON,src,src,target,MO_TRIPLEATTACK,lv,tick,0))
				return ATK_DEF;
			return ATK_MISS;
		}
	}

	if (sc) {
		if (sc->data[SC_SACRIFICE]) {
			uint16 skill_lv = sc->data[SC_SACRIFICE]->val1;
			damage_lv ret_val;

			if (--(sc->data[SC_SACRIFICE]->val2) <= 0)
				status_change_end(src,SC_SACRIFICE,INVALID_TIMER);
			//We need to calculate the DMG before the hp reduction,because it can kill the source
			//For further information: bugreport:4950
			ret_val = (damage_lv)skill_attack(BF_WEAPON,src,src,target,PA_SACRIFICE,skill_lv,tick,0);
			status_zap(src,sstatus->max_hp * 9 / 100,0); //Damage to self is always 9%
			if (ret_val == ATK_NONE)
				return ATK_MISS;
			return ret_val;
		}
		if (sc->data[SC_MAGICALATTACK]) {
			if (skill_attack(BF_MAGIC,src,src,target,NPC_MAGICALATTACK,sc->data[SC_MAGICALATTACK]->val1,tick,0))
				return ATK_DEF;
			return ATK_MISS;
		}
	}

	if (tsc) {
		struct status_change_entry *sce = NULL;

		if ((sce = tsc->data[SC_MTF_MLEATKED]) && rnd()%100 < sce->val3)
			clif_skill_nodamage(target,target,SM_ENDURE,sce->val2,sc_start(target,target,SC_ENDURE,100,sce->val2,skill_get_time(SM_ENDURE,sce->val2)));
		if ((sce = tsc->data[SC_KAAHI]) && tstatus->hp < tstatus->max_hp && status_charge(target,0,sce->val3)) {
			int hp_heal = tstatus->max_hp - tstatus->hp;

			if (hp_heal > sce->val2)
				hp_heal = sce->val2;
			if (hp_heal)
				status_heal(target,hp_heal,0,2);
		}
	}

	wd = battle_calc_attack(BF_WEAPON,src,target,0,0,flag);

	if (sc && sc->count) { //Consume the status even if missed
		if (sc->data[SC_EXEEDBREAK])
			status_change_end(src,SC_EXEEDBREAK,INVALID_TIMER);
		if (sc->data[SC_SPELLFIST] && --(sc->data[SC_SPELLFIST]->val2) <= 0)
			status_change_end(src,SC_SPELLFIST,INVALID_TIMER);
	}

	if (sd) {
		if (battle_config.ammo_decrement && sc && sc->data[SC_FEARBREEZE] && sc->data[SC_FEARBREEZE]->val4 > 0) {
			short idx = sd->equip_index[EQI_AMMO];

			if (idx >= 0 && sd->inventory_data[idx] && sd->inventory.u.items_inventory[idx].amount >= sc->data[SC_FEARBREEZE]->val4) {
				pc_delitem(sd,idx,sc->data[SC_FEARBREEZE]->val4,0,1,LOG_TYPE_CONSUME);
				sc->data[SC_FEARBREEZE]->val4 = 0;
			}
		}
		if (sd->state.arrow_atk) //Consume arrow
			battle_consume_ammo(sd,0,0);
	}

	damage = wd.damage + wd.damage2;

	if (damage > 0 && target->id != src->id) {
		if (sc && sc->data[SC_DUPLELIGHT] && wd.flag&BF_SHORT &&
			rnd()%100 <= 10 + 2 * sc->data[SC_DUPLELIGHT]->val1) { //Activates it only from melee damage
			uint16 skill_id;

			if (rnd()%2 == 1)
				skill_id = AB_DUPLELIGHT_MELEE;
			else
				skill_id = AB_DUPLELIGHT_MAGIC;
			skill_attack(skill_get_type(skill_id),src,src,target,skill_id,sc->data[SC_DUPLELIGHT]->val1,tick,flag);
		}
	}

	wd.dmotion = clif_damage(src,target,tick,wd.amotion,wd.dmotion,wd.damage,wd.div_,(enum e_damage_type)wd.type,wd.damage2,wd.isspdamage);

	if (damage > 0) {
		if (sd && sd->bonus.splash_range)
			skill_castend_damage_id(src,target,0,1,tick,0);
		if (target->type == BL_SKILL) {
			struct skill_unit *unit = (struct skill_unit *)target;
			struct skill_unit_group *group = NULL;

			if (unit && (group = unit->group)) {
				if (group->skill_id == HT_BLASTMINE)
					skill_blown(src,target,3,-1,0);
				if (group->skill_id == GN_WALLOFTHORN) {
					int element = battle_get_weapon_element(&wd,src,target,0,0,EQI_HAND_R);

					if (--(unit->val2) <= 0)
						skill_delunit(unit); //Max hits reached
					if (element == ELE_FIRE) {
						struct block_list *src2 = map_id2bl(group->src_id);

						if(src2) {
							group->unit_id = UNT_USED_TRAPS;
							group->limit = 0;
							src2->val1 = skill_get_time(group->skill_id,group->skill_lv) - DIFF_TICK(tick,group->tick); //Fire Wall duration [exneval]
							skill_unitsetting(src2,group->skill_id,group->skill_lv,group->val3>>16,group->val3&0xffff,1);
						}
					}
				}
			}
		}
	}

	map_freeblock_lock();

	battle_delay_damage(tick,wd.amotion,src,target,wd.flag,0,0,damage,wd.dmg_lv,wd.dmotion,true,wd.isvanishdamage,wd.isspdamage);

	if (tsc) {
		if (!wd.isvanishdamage) {
			if (tsc->data[SC_DEVOTION]) {
				struct status_change_entry *sce_d = tsc->data[SC_DEVOTION];
				struct block_list *d_bl = map_id2bl(sce_d->val1);

				if (d_bl &&
					((d_bl->type == BL_MER && ((TBL_MER *)d_bl)->master && ((TBL_MER *)d_bl)->master->bl.id == target->id) ||
					(d_bl->type == BL_PC && ((TBL_PC *)d_bl)->devotion[sce_d->val2] == target->id)) &&
					check_distance_bl(target,d_bl,sce_d->val3))
				{
					clif_damage(d_bl,d_bl,tick,0,wd.dmotion,damage,0,DMG_NORMAL,0,false);
					status_fix_damage(NULL,d_bl,damage,0);
					if (damage > 0)
						skill_counter_additional_effect(src,d_bl,0,0,wd.flag,tick);
				} else
					status_change_end(target,SC_DEVOTION,INVALID_TIMER);
			} else {
				if (tsc->data[SC__SHADOWFORM]) {
					struct status_change_entry *sce_s = tsc->data[SC__SHADOWFORM];
					struct map_session_data *s_sd = map_id2sd(sce_s->val2);

					if (s_sd && s_sd->shadowform_id == target->id) {
						clif_damage(&s_sd->bl,&s_sd->bl,tick,0,wd.dmotion,damage,0,DMG_NORMAL,0,false);
						status_fix_damage(NULL,&s_sd->bl,damage,0);
						if (damage > 0) {
							skill_counter_additional_effect(src,&s_sd->bl,0,0,wd.flag,tick);
							if (--(sce_s->val3) <= 0)
								status_change_end(target,SC__SHADOWFORM,INVALID_TIMER);
						}
					}
				}
				if (tsc->data[SC_WATER_SCREEN_OPTION]) {
					struct status_change_entry *sce_e = tsc->data[SC_WATER_SCREEN_OPTION];
					struct block_list *e_bl = map_id2bl(sce_e->val1);

					if (e_bl) {
						clif_damage(e_bl,e_bl,tick,0,wd.dmotion,damage,0,DMG_NORMAL,0,false);
						status_fix_damage(NULL,e_bl,damage,0);
					}
				}
			}
		}
		if (tsc->data[SC_CIRCLE_OF_FIRE_OPTION] && wd.flag&BF_SHORT && target->type == BL_PC) {
			struct status_change_entry *sce_e = tsc->data[SC_CIRCLE_OF_FIRE_OPTION];
			struct elemental_data *ed = ((TBL_PC *)target)->ed;

			if (ed) {
				clif_skill_damage(&ed->bl,target,tick,status_get_amotion(src),0,-30000,1,EL_CIRCLE_OF_FIRE,sce_e->val1,DMG_SKILL);
				skill_attack(BF_WEAPON,&ed->bl,&ed->bl,src,EL_CIRCLE_OF_FIRE,sce_e->val1,tick,flag);
			}
		}
	}

	if (sc && sc->data[SC_AUTOSPELL] && rnd()%100 < sc->data[SC_AUTOSPELL]->val4) {
		int sp = 0, i = rnd()%100;
		uint16 skill_id = sc->data[SC_AUTOSPELL]->val2,
			skill_lv = sc->data[SC_AUTOSPELL]->val3;

		if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_SAGE)
			i = 0; //Max chance, no skill_lv reduction [Skotlex]
		//Reduction only for skill_lv > 1
		if (skill_lv > 1) {
			if (i >= 50)
				skill_lv /= 2;
			else if (i >= 15)
				skill_lv--;
		}
		sp = skill_get_sp(skill_id,skill_lv) * 2 / 3;
		if (status_charge(src,0,sp)) {
			struct unit_data *ud = unit_bl2ud(src);

			switch (skill_get_casttype(skill_id)) {
				case CAST_GROUND:
					skill_castend_pos2(src,target->x,target->y,skill_id,skill_lv,tick,flag);
					break;
				case CAST_NODAMAGE:
					skill_castend_nodamage_id(src,target,skill_id,skill_lv,tick,flag);
					break;
				case CAST_DAMAGE:
					skill_castend_damage_id(src,target,skill_id,skill_lv,tick,flag);
					break;
			}
			if (ud) {
				int autospell_tick = skill_delayfix(src,skill_id,skill_lv);

				if (DIFF_TICK(ud->canact_tick,tick + autospell_tick) < 0) {
					ud->canact_tick = max(tick + autospell_tick,ud->canact_tick);
					if (battle_config.display_status_timers && sd)
						clif_status_change(src,SI_POSTDELAY,1,autospell_tick,0,0,0);
				}
			}
		}
	}

	if (sd && wd.flag&BF_WEAPON) {
		if (sc && sc->data[SC__AUTOSHADOWSPELL] && rnd()%100 < sc->data[SC__AUTOSHADOWSPELL]->val4) {
			uint16 skill_id = sc->data[SC__AUTOSHADOWSPELL]->val2,
				skill_lv = sc->data[SC__AUTOSHADOWSPELL]->val3;
			int type = skill_get_casttype(skill_id);

			if (type == CAST_GROUND) {
				int maxcount = 0;

				if (!(BL_PC&battle_config.skill_reiteration) &&
					skill_get_unit_flag(skill_id)&UF_NOREITERATION)
					type = -1;
				if (BL_PC&battle_config.skill_nofootset &&
					skill_get_unit_flag(skill_id)&UF_NOFOOTSET)
					type = -1;
				if (BL_PC&battle_config.land_skill_limit &&
					(maxcount = skill_get_maxcount(skill_id,skill_lv)) > 0) {
					int v;

					for (v = 0; v < MAX_SKILLUNITGROUP && sd->ud.skillunit[v] && maxcount; v++) {
						if (sd->ud.skillunit[v]->skill_id == skill_id)
							maxcount--;
					}
					if (!maxcount)
						type = -1;
				}
				if (type != CAST_GROUND) {
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
					map_freeblock_unlock();
					return wd.dmg_lv;
				}
			}
			if (status_charge(src,0,skill_get_sp(skill_id,skill_lv))) {
				struct unit_data *ud = unit_bl2ud(src);

				sd->state.autocast = 1;
				switch (type) {
					case CAST_GROUND:
						skill_castend_pos2(src,target->x,target->y,skill_id,skill_lv,tick,flag);
						break;
					case CAST_DAMAGE:
						skill_castend_damage_id(src,target,skill_id,skill_lv,tick,flag);
						break;
					case CAST_NODAMAGE:
						skill_castend_nodamage_id(src,target,skill_id,skill_lv,tick,flag);
						break;
				}
				sd->state.autocast = 0;
				if (ud) {
					int delay = skill_delayfix(src,skill_id,skill_lv);

					if (DIFF_TICK(ud->canact_tick,tick + delay) < 0) {
						ud->canact_tick = max(tick + delay,ud->canact_tick);
						if (battle_config.display_status_timers)
							clif_status_change(src,SI_POSTDELAY,1,delay,0,0,0);
					}
				}
			}
		}
		if (damage > 0) {
			if (battle_config.left_cardfix_to_right)
				battle_drain(sd,target,wd.damage,wd.damage,tstatus->race,tstatus->class_,true);
			else
				battle_drain(sd,target,wd.damage,wd.damage2,tstatus->race,tstatus->class_,true);
		}
	}

	if (damage > 0 && tsc) {
		if (tsc->data[SC_POISONREACT] &&
			(rnd()%100 < tsc->data[SC_POISONREACT]->val3 ||
			sstatus->def_ele == ELE_POISON) &&
			//check_distance_bl(src,target,tstatus->rhw.range + 1) && //Doesn't checks range!
			status_check_skilluse(target,src,TF_POISON,0))
		{ //Poison React
			struct status_change_entry *sce = tsc->data[SC_POISONREACT];

			if (sstatus->def_ele == ELE_POISON) {
				sce->val2 = 0;
				skill_attack(BF_WEAPON,target,target,src,AS_POISONREACT,sce->val1,tick,0);
			} else {
				skill_attack(BF_WEAPON,target,target,src,TF_POISON,5,tick,0);
				--sce->val2;
			}
			if (sce->val2 <= 0)
				status_change_end(target,SC_POISONREACT,INVALID_TIMER);
		}
	}

	map_freeblock_unlock();
	return wd.dmg_lv;
}

/*=========================
 * Check for undead status
 *-------------------------
 * Credits:
 *	Original coder Skotlex
 *  Refactored by Baalberith
 */
int battle_check_undead(int race,int element)
{
	if(!battle_config.undead_detect_type) {
		if(element == ELE_UNDEAD)
			return 1;
	} else if(battle_config.undead_detect_type == 1) {
		if(race == RC_UNDEAD)
			return 1;
	} else {
		if(element == ELE_UNDEAD || race == RC_UNDEAD)
			return 1;
	}
	return 0;
}

/*================================================================
 * Returns the upmost level master starting with the given object
 *----------------------------------------------------------------*/
struct block_list *battle_get_master(struct block_list *src)
{
	struct block_list *prev; //Used for infinite loop check (master of yourself?)

	do {
		prev = src;
		switch (src->type) {
			case BL_PET:
				if (((TBL_PET *)src)->master)
					src = (struct block_list *)((TBL_PET *)src)->master;
				break;
			case BL_MOB:
				if (((TBL_MOB *)src)->master_id)
					src = map_id2bl(((TBL_MOB *)src)->master_id);
				break;
			case BL_HOM:
				if (((TBL_HOM *)src)->master)
					src = (struct block_list *)((TBL_HOM *)src)->master;
				break;
			case BL_MER:
				if (((TBL_MER *)src)->master)
					src = (struct block_list *)((TBL_MER *)src)->master;
				break;
			case BL_ELEM:
				if (((TBL_ELEM *)src)->master)
					src = (struct block_list *)((TBL_ELEM *)src)->master;
				break;
			case BL_SKILL:
				if (((TBL_SKILL *)src)->group && ((TBL_SKILL *)src)->group->src_id)
					src = map_id2bl(((TBL_SKILL *)src)->group->src_id);
				break;
		}
	} while (src && src != prev);
	return prev;
}

/*==========================================
 * Checks the state between two targets
 * (enemy, friend, party, guild, etc)
 *------------------------------------------
 * Usage:
 * See battle.h for possible values/combinations
 * to be used here (BCT_* constants)
 * Return value is:
 * 1: flag holds true (is enemy, party, etc)
 * -1: flag fails
 * 0: Invalid target (non-targetable ever)
 *
 * Credits:
 *	Original coder unknown
 *	Rewritten by Skotlex
 */
int battle_check_target(struct block_list *src, struct block_list *target, int flag)
{
	int16 m; //Map
	int state = 0; //Initial state none
	int strip_enemy = 1; //Flag which marks whether to remove the BCT_ENEMY status if it's also friend/ally
	struct block_list *s_bl = NULL;
	struct block_list *t_bl = NULL;
	struct unit_data *ud = NULL;

	nullpo_ret(src);
	nullpo_ret(target);

	s_bl = src;
	t_bl = target;

	ud = unit_bl2ud(target);
	m = target->m;

	//s_bl/t_bl hold the 'master' of the attack, while src/target are the actual objects involved
	if( !(s_bl = battle_get_master(src)) )
		s_bl = src;

	if( !(t_bl = battle_get_master(target)) )
		t_bl = target;

	if( s_bl->type == BL_PC ) {
		switch( t_bl->type ) {
			case BL_MOB: //Source => PC, Target => MOB
				if( pc_has_permission((TBL_PC *)s_bl, PC_PERM_DISABLE_PVM) )
					return 0;
				break;
			case BL_PC:
				if( pc_has_permission((TBL_PC *)s_bl, PC_PERM_DISABLE_PVP) )
					return 0;
				break;
			default: //Anything else goes
				break;
		}
	}

	switch( target->type ) { //Checks on actual target
		case BL_PC: {
				struct status_change *sc = status_get_sc(src);

				if( (((TBL_PC *)target)->invincible_timer != INVALID_TIMER || pc_isinvisible((TBL_PC *)target)) && 
					!(flag&BCT_NOENEMY) )
					return -1; //Cannot be targeted yet
				if( sc && sc->count && sc->data[SC_VOICEOFSIREN] && sc->data[SC_VOICEOFSIREN]->val2 == target->id )
					return -1;
			}
			break;
		case BL_MOB: {
				struct mob_data *md = ((TBL_MOB *)target);

				if( ud && ud->immune_attack )
					return 0;
				if( ((md->special_state.ai == AI_SPHERE || //Marine Sphere
					(md->special_state.ai == AI_FLORA && battle_config.summon_flora&1) || //Flora
					(md->special_state.ai == AI_ZANZOU && map_flag_vs(md->bl.m)) || //Zanzou
					md->special_state.ai == AI_FAW) && //FAW
					s_bl->type == BL_PC && src->type != BL_MOB) )
				{ //Targettable by players
					state |= BCT_ENEMY;
					strip_enemy = 0;
				}
			}
			break;
		case BL_SKILL: {
				TBL_SKILL *su = ((TBL_SKILL *)target);
				uint16 skill_id = battle_getcurrentskill(src);

				if( !su || !su->group )
					return 0;
				if( (skill_get_inf2(su->group->skill_id)&INF2_TRAP) && su->group->unit_id != UNT_USED_TRAPS ) {
					if( !skill_id ) {
						;
					} else if( skill_get_inf2(skill_id)&INF2_HIT_TRAP ) { //Only a few skills can target traps
						switch( skill_id ) {
							case RK_DRAGONBREATH:
							case RK_DRAGONBREATH_WATER:
							case NC_SELFDESTRUCTION:
							case NC_AXETORNADO:
							case SR_SKYNETBLOW:
								if( !map[m].flag.pvp && !map[m].flag.gvg )
									return 0; //Can only hit traps in PVP/GVG maps
								break;
						}
					} else
						return 0;
					state |= BCT_ENEMY;
					strip_enemy = 0;
				} else if( su->group->skill_id == WZ_ICEWALL || (su->group->skill_id == GN_WALLOFTHORN && su->group->unit_id != UNT_FIREWALL) ) {
					switch( skill_id ) {
						case RK_DRAGONBREATH:
						case RK_DRAGONBREATH_WATER:
						case NC_SELFDESTRUCTION:
						case NC_AXETORNADO:
						case SR_SKYNETBLOW:
							if( !map[m].flag.pvp && !map[m].flag.gvg )
								return 0;
							break;
						case HT_CLAYMORETRAP:
							return 0; //Can't hit icewall
						default:
							if( (flag&BCT_ALL) == BCT_ALL && !(skill_get_inf2(skill_id)&INF2_HIT_TRAP) )
								return -1; //Usually BCT_ALL stands for only hitting chars, but skills specifically set to hit traps also hit icewall
							break;
					}
					state |= BCT_ENEMY;
					strip_enemy = 0;
				} else //Excepting traps, icewall, wall of thornes, you should not be able to target skills
					return 0;
			}
			break;
		case BL_MER:
		case BL_HOM:
		case BL_ELEM:
			if( ud && ud->immune_attack )
				return 0;
			break;
		default: //All else not specified is an invalid target
			return 0;
	}

	switch( t_bl->type ) { //Checks on target master
		case BL_PC: {
				struct map_session_data *sd = NULL;
				struct status_change *sc = NULL;

				if( t_bl == s_bl )
					break;
				sd = BL_CAST(BL_PC, t_bl);
				sc = status_get_sc(t_bl);
				if( (sd->state.monster_ignore || (sc->data[SC_KINGS_GRACE] && (src->type != BL_PC || !battle_getcurrentskill(s_bl)))) && (flag&BCT_ENEMY) )
					return 0; //Global immunity only to attacks
				if( sd->status.karma && s_bl->type == BL_PC && ((TBL_PC *)s_bl)->status.karma )
					state |= BCT_ENEMY; //Characters with bad karma may fight amongst them
				if( sd->state.killable ) {
					state |= BCT_ENEMY; //Everything can kill it
					strip_enemy = 0;
				}
			}
			break;
		case BL_MOB: {
				struct mob_data *md = BL_CAST(BL_MOB, t_bl);

				if( !map_flag_gvg(m) && md->guardian_data && (md->guardian_data->g || md->guardian_data->castle->guild_id) )
					return 0; //Disable guardians/emperium owned by Guilds on non-woe times
			}
			break;
	}

	switch( src->type ) { //Checks on actual src type
		case BL_PET:
			if( flag&BCT_ENEMY ) {
				if( t_bl->type != BL_MOB )
					return 0; //Pet may not attack non-mobs
				if( t_bl->type == BL_MOB && ((TBL_MOB *)t_bl)->guardian_data )
					return 0; //Pet may not attack Guardians/Emperium
			}
			break;
		case BL_SKILL: {
				struct skill_unit *su = (struct skill_unit *)src;
				struct status_change *tsc = status_get_sc(target);
				int inf2;

				if( !su || !su->group )
					return 0;
				inf2 = skill_get_inf2(su->group->skill_id);
				if( su->group->src_id == target->id ) {
					if( inf2&INF2_NO_TARGET_SELF )
						return -1;
					if( inf2&INF2_TARGET_SELF )
						return 1;
				}
				//Status changes that prevent traps from triggering
				if( (inf2&INF2_TRAP) && tsc && tsc->count && tsc->data[SC_SIGHTBLASTER] &&
					tsc->data[SC_SIGHTBLASTER]->val2 > 0 && !(tsc->data[SC_SIGHTBLASTER]->val4%2) )
					return -1;
			}
			break;
		case BL_MER:
			if( t_bl->type == BL_MOB && ((TBL_MOB *)t_bl)->mob_id == MOBID_EMPERIUM && (flag&BCT_ENEMY) )
				return 0; //Mercenary may not attack Emperium
			break;
	}

	switch( s_bl->type ) { //Checks on source master
		case BL_PC: {
				struct map_session_data *sd = BL_CAST(BL_PC, s_bl);

				if( t_bl->id != s_bl->id ) {
					if( sd->state.killer ) {
						state |= BCT_ENEMY; //Can kill anything
						strip_enemy = 0;
					} else if( sd->duel_group && !((!battle_config.duel_allow_pvp && map[m].flag.pvp) ||
						(!battle_config.duel_allow_gvg && map_flag_gvg2(m))) ) {
						if( t_bl->type == BL_PC && (sd->duel_group == ((TBL_PC *)t_bl)->duel_group) )
							return (flag&BCT_ENEMY) ? 1 : -1; //Duel targets can ONLY be your enemy, nothing else
						else if( src->type != BL_SKILL || (flag&BCT_ALL) != BCT_ALL )
							return 0;
					}
				}
				if( map_flag_gvg2(m) && !sd->status.guild_id && t_bl->type == BL_MOB && ((TBL_MOB *)t_bl)->mob_id == MOBID_EMPERIUM )
					return 0; //If you don't belong to a guild, can't target emperium
				if( t_bl->type != BL_PC )
					state |= BCT_ENEMY; //Natural enemy
			}
			break;
		case BL_MOB: {
				struct mob_data *md = BL_CAST(BL_MOB, s_bl);

				if( !map_flag_gvg(m) && md->guardian_data && (md->guardian_data->g || md->guardian_data->castle->guild_id) )
					return 0; //Disable guardians/emperium owned by Guilds on non-woe times
				if( !md->special_state.ai ) { //Normal mobs
					if( (target->type == BL_MOB && t_bl->type == BL_PC &&
						(((TBL_MOB *)target)->special_state.ai != AI_ATTACK && //Clone
						((TBL_MOB *)target)->special_state.ai != AI_ZANZOU && //Zanzou
						((TBL_MOB *)target)->special_state.ai != AI_FAW)) || //FAW
						(t_bl->type == BL_MOB && !((TBL_MOB *)t_bl)->special_state.ai) )
						state |= BCT_PARTY; //Normal mobs with no ai are friends
					else
						state |= BCT_ENEMY; //However, all else are enemies
				} else {
					if( t_bl->type == BL_MOB && !((TBL_MOB *)t_bl)->special_state.ai )
						state |= BCT_ENEMY; //Natural enemy for AI mobs are normal mobs
				}
			}
			break;
		default: //Need some sort of default behaviour for unhandled types
			if( t_bl->type != s_bl->type )
				state |= BCT_ENEMY;
			break;
	}

	if( (flag&BCT_ALL) == BCT_ALL ) { //All actually stands for all attackable chars, icewall and traps
		if( target->type&(BL_CHAR|BL_SKILL) )
			return 1;
		else
			return -1;
	}

	if( flag == BCT_NOONE ) //Why would someone use this? no clue
		return -1;

	if( t_bl == s_bl ) { //No need for further testing
		state |= (BCT_SELF|BCT_PARTY|BCT_GUILD);
		if( (state&BCT_ENEMY) && strip_enemy )
			state &= ~BCT_ENEMY;
		return (flag&state) ? 1 : -1;
	}

	if( map_flag_vs(m) ) { //Check rivalry settings
		int sbg_id = 0, tbg_id = 0;

		if( map[m].flag.battleground ) {
			sbg_id = bg_team_get_id(s_bl);
			tbg_id = bg_team_get_id(t_bl);
		}

		if( (flag&(BCT_PARTY|BCT_ENEMY)) ) {
			int s_party = status_get_party_id(s_bl);
			int s_guild = status_get_guild_id(s_bl);
			int t_guild = status_get_guild_id(t_bl);

			if( s_party && s_party == status_get_party_id(t_bl) ) {
				if( map_flag_gvg2(m) && map[m].flag.gvg_noparty ) {
					if( s_guild && t_guild && (s_guild == t_guild || guild_isallied(s_guild, t_guild)) )
						state |= BCT_PARTY;
					else
						state |= (flag&BCT_ENEMY) ? BCT_ENEMY : BCT_PARTY;
				} else if( !(map[m].flag.pvp && map[m].flag.pvp_noparty) && (!map[m].flag.battleground || sbg_id == tbg_id) )
					state |= BCT_PARTY;
				else
					state |= BCT_ENEMY;
			} else
				state |= BCT_ENEMY;
		}

		if( (flag&(BCT_GUILD|BCT_ENEMY)) ) {
			int s_guild = status_get_guild_id(s_bl);
			int t_guild = status_get_guild_id(t_bl);

			if( !(map[m].flag.pvp && map[m].flag.pvp_noguild) && s_guild &&
				t_guild && (s_guild == t_guild || (!(flag&BCT_SAMEGUILD) && guild_isallied(s_guild, t_guild))) &&
				(!map[m].flag.battleground || sbg_id == tbg_id) )
				state |= BCT_GUILD;
			else
				state |= BCT_ENEMY;
		}

		if( state&BCT_ENEMY && map[m].flag.battleground && sbg_id && sbg_id == tbg_id )
			state &= ~BCT_ENEMY;

		if( state&BCT_ENEMY && battle_config.pk_mode && !map_flag_gvg2(m) && s_bl->type == BL_PC && t_bl->type == BL_PC ) {
			TBL_PC *sd = (TBL_PC *)s_bl, *tsd = (TBL_PC *)t_bl;

			//Prevent novice engagement on pk_mode (feature by Valaris)
			if( (sd->class_&MAPID_UPPERMASK) == MAPID_NOVICE ||
				(tsd->class_&MAPID_UPPERMASK) == MAPID_NOVICE ||
				(int)sd->status.base_level < battle_config.pk_min_level ||
				(int)tsd->status.base_level < battle_config.pk_min_level ||
				(battle_config.pk_level_range &&
				abs((int)sd->status.base_level - (int)tsd->status.base_level) > battle_config.pk_level_range) )
				state &= ~BCT_ENEMY;
		}
	} else { //Non pvp/gvg, check party/guild settings
		if( (flag&BCT_PARTY) || (state&BCT_ENEMY) ) {
			int s_party = status_get_party_id(s_bl);

			if( s_party && s_party == status_get_party_id(t_bl) )
				state |= BCT_PARTY;
		}

		if( (flag&BCT_GUILD) || (state&BCT_ENEMY) ) {
			int s_guild = status_get_guild_id(s_bl);
			int t_guild = status_get_guild_id(t_bl);

			if( s_guild && t_guild && (s_guild == t_guild || (!(flag&BCT_SAMEGUILD) && guild_isallied(s_guild, t_guild))) )
				state |= BCT_GUILD;
		}
	}

	if( !state ) //If not an enemy, nor a guild, nor party, nor yourself, it's neutral
		state = BCT_NEUTRAL;
	else if( (state&BCT_ENEMY) && strip_enemy && (state&(BCT_SELF|BCT_PARTY|BCT_GUILD)) )
		state &= ~BCT_ENEMY; //Alliance state takes precedence over enemy one

	return (flag&state) ? 1 : -1;
}

/*==========================================
 * Check if can attack from this range
 * Basic check then calling path_search for obstacle etc
 *------------------------------------------
 */
bool battle_check_range(struct block_list *src, struct block_list *bl, int range)
{
	int d;

	nullpo_retr(false, src);
	nullpo_retr(false, bl);

	if( src->m != bl->m )
		return false;

#ifndef CIRCULAR_AREA
	if( src->type == BL_PC ) { //Range for players' attacks and skills should always have a circular check [Angezerus]
		if( !check_distance_client_bl(src, bl, range) )
			return false;
	} else
#endif
	if( !check_distance_bl(src, bl, range) )
		return false;

	if( (d = distance_bl(src, bl)) < 2 )
		return true; //No need for path checking

	if( d > AREA_SIZE )
		return false; //Avoid targetting objects beyond your range of sight

	return path_search_long(NULL,src->m,src->x,src->y,bl->x,bl->y,CELL_CHKWALL);
}

/*=============================================
 * Battle.conf settings and default/max values
 *---------------------------------------------
 */
static const struct _battle_data {
	const char *str;
	int *val;
	int defval;
	int min;
	int max;
} battle_data[] = {
	{ "warp_point_debug",                   &battle_config.warp_point_debug,                0,      0,      1,              },
	{ "enable_critical",                    &battle_config.enable_critical,                 BL_PC,  BL_NUL, BL_ALL,         },
	{ "mob_critical_rate",                  &battle_config.mob_critical_rate,               100,    0,      INT_MAX,        },
	{ "critical_rate",                      &battle_config.critical_rate,                   100,    0,      INT_MAX,        },
	{ "enable_baseatk",                     &battle_config.enable_baseatk,                  BL_PC|BL_HOM, BL_NUL, BL_ALL,   },
	{ "enable_perfect_flee",                &battle_config.enable_perfect_flee,             BL_PC|BL_PET, BL_NUL, BL_ALL,   },
	{ "casting_rate",                       &battle_config.cast_rate,                       100,    0,      INT_MAX,        },
	{ "delay_rate",                         &battle_config.delay_rate,                      100,    0,      INT_MAX,        },
	{ "delay_dependon_dex",                 &battle_config.delay_dependon_dex,              0,      0,      1,              },
	{ "delay_dependon_agi",                 &battle_config.delay_dependon_agi,              0,      0,      1,              },
	{ "skill_delay_attack_enable",          &battle_config.sdelay_attack_enable,            0,      0,      1,              },
	{ "left_cardfix_to_right",              &battle_config.left_cardfix_to_right,           0,      0,      1,              },
	{ "skill_add_range",                    &battle_config.skill_add_range,                 0,      0,      INT_MAX,        },
	{ "skill_out_range_consume",            &battle_config.skill_out_range_consume,         1,      0,      1,              },
	{ "skillrange_by_distance",             &battle_config.skillrange_by_distance,          ~BL_PC, BL_NUL, BL_ALL,         },
	{ "skillrange_from_weapon",             &battle_config.use_weapon_skill_range,          BL_NUL, BL_NUL, BL_ALL,         },
	{ "player_damage_delay_rate",           &battle_config.pc_damage_delay_rate,            100,    0,      INT_MAX,        },
	{ "defunit_not_enemy",                  &battle_config.defnotenemy,                     0,      0,      1,              },
	{ "gvg_traps_target_all",               &battle_config.vs_traps_bctall,                 BL_PC,  BL_NUL, BL_ALL,         },
	{ "traps_setting",                      &battle_config.traps_setting,                   0,      0,      2,              },
	{ "summon_flora_setting",               &battle_config.summon_flora,                    1|2,    0,      1|2,            },
	{ "clear_skills_on_death",              &battle_config.clear_unit_ondeath,              BL_NUL, BL_NUL, BL_ALL,         },
	{ "clear_skills_on_warp",               &battle_config.clear_unit_onwarp,               BL_ALL, BL_NUL, BL_ALL,         },
	{ "random_monster_checklv",             &battle_config.random_monster_checklv,          0,      0,      1,              },
	{ "attribute_recover",                  &battle_config.attr_recover,                    1,      0,      1,              },
	{ "flooritem_lifetime",                 &battle_config.flooritem_lifetime,              60000,  1000,   INT_MAX,        },
	{ "item_auto_get",                      &battle_config.item_auto_get,                   0,      0,      1,              },
	{ "item_first_get_time",                &battle_config.item_first_get_time,             3000,   0,      INT_MAX,        },
	{ "item_second_get_time",               &battle_config.item_second_get_time,            1000,   0,      INT_MAX,        },
	{ "item_third_get_time",                &battle_config.item_third_get_time,             1000,   0,      INT_MAX,        },
	{ "mvp_item_first_get_time",            &battle_config.mvp_item_first_get_time,         10000,  0,      INT_MAX,        },
	{ "mvp_item_second_get_time",           &battle_config.mvp_item_second_get_time,        10000,  0,      INT_MAX,        },
	{ "mvp_item_third_get_time",            &battle_config.mvp_item_third_get_time,         2000,   0,      INT_MAX,        },
	{ "drop_rate0item",                     &battle_config.drop_rate0item,                  0,      0,      1,              },
	{ "base_exp_rate",                      &battle_config.base_exp_rate,                   100,    0,      INT_MAX,        },
	{ "job_exp_rate",                       &battle_config.job_exp_rate,                    100,    0,      INT_MAX,        },
	{ "pvp_exp",                            &battle_config.pvp_exp,                         1,      0,      1,              },
	{ "death_penalty_type",                 &battle_config.death_penalty_type,              0,      0,      2,              },
	{ "death_penalty_base",                 &battle_config.death_penalty_base,              0,      0,      INT_MAX,        },
	{ "death_penalty_job",                  &battle_config.death_penalty_job,               0,      0,      INT_MAX,        },
	{ "zeny_penalty",                       &battle_config.zeny_penalty,                    0,      0,      INT_MAX,        },
	{ "hp_rate",                            &battle_config.hp_rate,                         100,    1,      INT_MAX,        },
	{ "sp_rate",                            &battle_config.sp_rate,                         100,    1,      INT_MAX,        },
	{ "restart_hp_rate",                    &battle_config.restart_hp_rate,                 0,      0,      100,            },
	{ "restart_sp_rate",                    &battle_config.restart_sp_rate,                 0,      0,      100,            },
	{ "guild_aura",                         &battle_config.guild_aura,                      31,     0,      31,             },
	{ "mvp_hp_rate",                        &battle_config.mvp_hp_rate,                     100,    1,      INT_MAX,        },
	{ "mvp_exp_rate",                       &battle_config.mvp_exp_rate,                    100,    0,      INT_MAX,        },
	{ "monster_hp_rate",                    &battle_config.monster_hp_rate,                 100,    1,      INT_MAX,        },
	{ "monster_max_aspd",                   &battle_config.monster_max_aspd,                199,    100,    199,            },
	{ "view_range_rate",                    &battle_config.view_range_rate,                 100,    0,      INT_MAX,        },
	{ "chase_range_rate",                   &battle_config.chase_range_rate,                100,    0,      INT_MAX,        },
	{ "guild_max_castles",                  &battle_config.guild_max_castles,               0,      0,      INT_MAX,        },
	{ "guild_skill_relog_delay",            &battle_config.guild_skill_relog_delay,         300000, 0,      INT_MAX,        },
	{ "emergency_call",                     &battle_config.emergency_call,                  11,     0,      31,             },
	{ "atcommand_spawn_quantity_limit",     &battle_config.atc_spawn_quantity_limit,        100,    0,      INT_MAX,        },
	{ "atcommand_slave_clone_limit",        &battle_config.atc_slave_clone_limit,           25,     0,      INT_MAX,        },
	{ "partial_name_scan",                  &battle_config.partial_name_scan,               0,      0,      1,              },
	{ "player_skillfree",                   &battle_config.skillfree,                       0,      0,      1,              },
	{ "player_skillup_limit",               &battle_config.skillup_limit,                   1,      0,      1,              },
	{ "weapon_produce_rate",                &battle_config.wp_rate,                         100,    0,      INT_MAX,        },
	{ "potion_produce_rate",                &battle_config.pp_rate,                         100,    0,      INT_MAX,        },
	{ "monster_active_enable",              &battle_config.monster_active_enable,           1,      0,      1,              },
	{ "monster_damage_delay_rate",          &battle_config.monster_damage_delay_rate,       100,    0,      INT_MAX,        },
	{ "monster_loot_type",                  &battle_config.monster_loot_type,               0,      0,      1,              },
//	{ "mob_skill_use",                      &battle_config.mob_skill_use,                   1,      0,      1,              }, //Deprecated
	{ "mob_skill_rate",                     &battle_config.mob_skill_rate,                  100,    0,      INT_MAX,        },
	{ "mob_skill_delay",                    &battle_config.mob_skill_delay,                 100,    0,      INT_MAX,        },
	{ "mob_count_rate",                     &battle_config.mob_count_rate,                  100,    0,      INT_MAX,        },
	{ "mob_spawn_delay",                    &battle_config.mob_spawn_delay,                 100,    0,      INT_MAX,        },
	{ "plant_spawn_delay",                  &battle_config.plant_spawn_delay,               100,    0,      INT_MAX,        },
	{ "boss_spawn_delay",                   &battle_config.boss_spawn_delay,                100,    0,      INT_MAX,        },
	{ "no_spawn_on_player",                 &battle_config.no_spawn_on_player,              0,      0,      100,            },
	{ "force_random_spawn",                 &battle_config.force_random_spawn,              0,      0,      1,              },
	{ "slaves_inherit_mode",                &battle_config.slaves_inherit_mode,             4,      0,      4,              },
	{ "slaves_inherit_speed",               &battle_config.slaves_inherit_speed,            3,      0,      3,              },
	{ "summons_trigger_autospells",         &battle_config.summons_trigger_autospells,      1,      0,      1,              },
	{ "pc_damage_walk_delay_rate",          &battle_config.pc_walk_delay_rate,              20,     0,      INT_MAX,        },
	{ "damage_walk_delay_rate",             &battle_config.walk_delay_rate,                 100,    0,      INT_MAX,        },
	{ "multihit_delay",                     &battle_config.multihit_delay,                  80,     0,      INT_MAX,        },
	{ "quest_skill_learn",                  &battle_config.quest_skill_learn,               0,      0,      1,              },
	{ "quest_skill_reset",                  &battle_config.quest_skill_reset,               0,      0,      1,              },
	{ "basic_skill_check",                  &battle_config.basic_skill_check,               1,      0,      1,              },
	{ "guild_emperium_check",               &battle_config.guild_emperium_check,            1,      0,      1,              },
	{ "guild_exp_limit",                    &battle_config.guild_exp_limit,                 50,     0,      99,             },
	{ "player_invincible_time",             &battle_config.pc_invincible_time,              5000,   0,      INT_MAX,        },
	{ "pet_catch_rate",                     &battle_config.pet_catch_rate,                  100,    0,      INT_MAX,        },
	{ "pet_rename",                         &battle_config.pet_rename,                      0,      0,      1,              },
	{ "pet_friendly_rate",                  &battle_config.pet_friendly_rate,               100,    0,      INT_MAX,        },
	{ "pet_hungry_delay_rate",              &battle_config.pet_hungry_delay_rate,           100,    10,     INT_MAX,        },
	{ "pet_hungry_feeding_increase",        &battle_config.pet_hungry_feeding_increase,     20,     0,      INT_MAX,        },
	{ "pet_hungry_friendly_decrease",       &battle_config.pet_hungry_friendly_decrease,    20,     0,      INT_MAX,        },
	{ "pet_status_support",                 &battle_config.pet_status_support,              0,      0,      1,              },
	{ "pet_attack_support",                 &battle_config.pet_attack_support,              0,      0,      1,              },
	{ "pet_damage_support",                 &battle_config.pet_damage_support,              0,      0,      1,              },
	{ "pet_support_min_friendly",           &battle_config.pet_support_min_friendly,        901,    0,      1000,           },
#ifdef RENEWAL
	{ "pet_bonus_min_friendly_re",          &battle_config.pet_bonus_min_friendly,          0,      0,      1000,           },
#else
	{ "pet_bonus_min_friendly",             &battle_config.pet_bonus_min_friendly,          751,    0,      1000,           },
#endif
	{ "pet_support_rate",                   &battle_config.pet_support_rate,                100,    0,      INT_MAX,        },
	{ "pet_attack_exp_to_master",           &battle_config.pet_attack_exp_to_master,        0,      0,      1,              },
	{ "pet_attack_exp_rate",                &battle_config.pet_attack_exp_rate,             100,    0,      INT_MAX,        },
	{ "pet_lv_rate",                        &battle_config.pet_lv_rate,                     0,      0,      INT_MAX,        },
	{ "pet_max_stats",                      &battle_config.pet_max_stats,                   99,     0,      INT_MAX,        },
	{ "pet_max_atk1",                       &battle_config.pet_max_atk1,                    750,    0,      INT_MAX,        },
	{ "pet_max_atk2",                       &battle_config.pet_max_atk2,                    1000,   0,      INT_MAX,        },
	{ "pet_disable_in_gvg",                 &battle_config.pet_no_gvg,                      0,      0,      1,              },
	{ "pet_ignore_infinite_def",            &battle_config.pet_ignore_infinite_def,         0,      0,      1,              },
	{ "pet_equip_required",                 &battle_config.pet_equip_required,              0,      0,      1,              },
	{ "pet_master_dead",                    &battle_config.pet_master_dead,                 0,      0,      1,              },
	{ "skill_min_damage",                   &battle_config.skill_min_damage,                2|4,    0,      1|2|4,          },
	{ "finger_offensive_type",              &battle_config.finger_offensive_type,           0,      0,      1,              },
	{ "heal_exp",                           &battle_config.heal_exp,                        0,      0,      INT_MAX,        },
	{ "resurrection_exp",                   &battle_config.resurrection_exp,                0,      0,      INT_MAX,        },
	{ "shop_exp",                           &battle_config.shop_exp,                        0,      0,      INT_MAX,        },
	{ "max_heal_lv",                        &battle_config.max_heal_lv,                     11,     1,      INT_MAX,        },
	{ "max_heal",                           &battle_config.max_heal,                        9999,   0,      INT_MAX,        },
	{ "item_check",                         &battle_config.item_check,                      0x0,    0x0,    0x7,            },
	{ "item_use_interval",                  &battle_config.item_use_interval,               100,    0,      INT_MAX,        },
	{ "wedding_modifydisplay",              &battle_config.wedding_modifydisplay,           0,      0,      1,              },
	{ "wedding_ignorepalette",              &battle_config.wedding_ignorepalette,           0,      0,      1,              },
	{ "xmas_ignorepalette",                 &battle_config.xmas_ignorepalette,              0,      0,      1,              },
	{ "summer_ignorepalette",               &battle_config.summer_ignorepalette,            0,      0,      1,              },
	{ "hanbok_ignorepalette",               &battle_config.hanbok_ignorepalette,            0,      0,      1,              },
	{ "natural_healhp_interval",            &battle_config.natural_healhp_interval,         6000,   NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "natural_healsp_interval",            &battle_config.natural_healsp_interval,         8000,   NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "natural_heal_skill_interval",        &battle_config.natural_heal_skill_interval,     10000,  NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "natural_heal_weight_rate",           &battle_config.natural_heal_weight_rate,        50,     50,     100             },
	{ "natural_heal_weight_rate_renewal",   &battle_config.natural_heal_weight_rate_renewal,70,     0,      100             },
	{ "ammo_decrement",                     &battle_config.ammo_decrement,                  1,      0,      2,              },
	{ "ammo_unequip",                       &battle_config.ammo_unequip,                    1,      0,      1,              },
	{ "ammo_check_weapon",                  &battle_config.ammo_check_weapon,               1,      0,      1,              },
	{ "max_aspd",                           &battle_config.max_aspd,                        190,    100,    199,            },
	{ "max_third_aspd",                     &battle_config.max_third_aspd,                  193,    100,    199,            },
	{ "max_walk_speed",                     &battle_config.max_walk_speed,                  300,    100,    100*DEFAULT_WALK_SPEED, },
	{ "max_lv",                             &battle_config.max_lv,                          99,     0,      MAX_LEVEL,      },
	{ "aura_lv",                            &battle_config.aura_lv,                         99,     0,      INT_MAX,        },
	{ "max_hp",                             &battle_config.max_hp,                          32500,  100,    1000000000,     },
	{ "max_sp",                             &battle_config.max_sp,                          32500,  100,    1000000000,     },
	{ "max_cart_weight",                    &battle_config.max_cart_weight,                 8000,   100,    1000000,        },
	{ "max_parameter",                      &battle_config.max_parameter,                   99,     10,     SHRT_MAX,       },
	{ "max_baby_parameter",                 &battle_config.max_baby_parameter,              80,     10,     SHRT_MAX,       },
	{ "max_def",                            &battle_config.max_def,                         99,     0,      INT_MAX,        },
	{ "over_def_bonus",                     &battle_config.over_def_bonus,                  0,      0,      1000,           },
	{ "skill_log",                          &battle_config.skill_log,                       BL_NUL, BL_NUL, BL_ALL,         },
	{ "battle_log",                         &battle_config.battle_log,                      0,      0,      1,              },
	{ "etc_log",                            &battle_config.etc_log,                         1,      0,      1,              },
	{ "save_clothcolor",                    &battle_config.save_clothcolor,                 1,      0,      1,              },
	{ "undead_detect_type",                 &battle_config.undead_detect_type,              0,      0,      2,              },
	{ "auto_counter_type",                  &battle_config.auto_counter_type,               BL_ALL, BL_NUL, BL_ALL,         },
	{ "min_hitrate",                        &battle_config.min_hitrate,                     5,      0,      100,            },
	{ "max_hitrate",                        &battle_config.max_hitrate,                     100,    0,      100,            },
	{ "agi_penalty_target",                 &battle_config.agi_penalty_target,              BL_PC,  BL_NUL, BL_ALL,         },
	{ "agi_penalty_type",                   &battle_config.agi_penalty_type,                1,      0,      2,              },
	{ "agi_penalty_count",                  &battle_config.agi_penalty_count,               3,      2,      INT_MAX,        },
	{ "agi_penalty_num",                    &battle_config.agi_penalty_num,                 10,     0,      INT_MAX,        },
	{ "vit_penalty_target",                 &battle_config.vit_penalty_target,              BL_PC,  BL_NUL, BL_ALL,         },
	{ "vit_penalty_type",                   &battle_config.vit_penalty_type,                1,      0,      2,              },
	{ "vit_penalty_count",                  &battle_config.vit_penalty_count,               3,      2,      INT_MAX,        },
	{ "vit_penalty_num",                    &battle_config.vit_penalty_num,                 5,      1,      INT_MAX,        },
	{ "weapon_defense_type",                &battle_config.weapon_defense_type,             0,      0,      INT_MAX,        },
	{ "magic_defense_type",                 &battle_config.magic_defense_type,              0,      0,      INT_MAX,        },
	{ "skill_reiteration",                  &battle_config.skill_reiteration,               BL_NUL, BL_NUL, BL_ALL,         },
	{ "skill_nofootset",                    &battle_config.skill_nofootset,                 BL_PC,  BL_NUL, BL_ALL,         },
	{ "player_cloak_check_type",            &battle_config.pc_cloak_check_type,             1,      0,      1|2|4,          },
	{ "monster_cloak_check_type",           &battle_config.monster_cloak_check_type,        4,      0,      1|2|4,          },
	{ "sense_type",                         &battle_config.estimation_type,                 1|2,    0,      1|2,            },
	{ "gvg_short_attack_damage_rate",       &battle_config.gvg_short_damage_rate,           80,     0,      INT_MAX,        },
	{ "gvg_long_attack_damage_rate",        &battle_config.gvg_long_damage_rate,            80,     0,      INT_MAX,        },
	{ "gvg_weapon_attack_damage_rate",      &battle_config.gvg_weapon_damage_rate,          60,     0,      INT_MAX,        },
	{ "gvg_magic_attack_damage_rate",       &battle_config.gvg_magic_damage_rate,           60,     0,      INT_MAX,        },
	{ "gvg_misc_attack_damage_rate",        &battle_config.gvg_misc_damage_rate,            60,     0,      INT_MAX,        },
	{ "gvg_flee_penalty",                   &battle_config.gvg_flee_penalty,                20,     0,      INT_MAX,        },
	{ "pk_short_attack_damage_rate",        &battle_config.pk_short_damage_rate,            80,     0,      INT_MAX,        },
	{ "pk_long_attack_damage_rate",         &battle_config.pk_long_damage_rate,             70,     0,      INT_MAX,        },
	{ "pk_weapon_attack_damage_rate",       &battle_config.pk_weapon_damage_rate,           60,     0,      INT_MAX,        },
	{ "pk_magic_attack_damage_rate",        &battle_config.pk_magic_damage_rate,            60,     0,      INT_MAX,        },
	{ "pk_misc_attack_damage_rate",         &battle_config.pk_misc_damage_rate,             60,     0,      INT_MAX,        },
	{ "mob_changetarget_byskill",           &battle_config.mob_changetarget_byskill,        0,      0,      1,              },
	{ "attack_direction_change",            &battle_config.attack_direction_change,         BL_ALL, BL_NUL, BL_ALL,         },
	{ "land_skill_limit",                   &battle_config.land_skill_limit,                BL_ALL, BL_NUL, BL_ALL,         },
	{ "monster_class_change_full_recover",  &battle_config.monster_class_change_recover,    1,      0,      1,              },
	{ "produce_item_name_input",            &battle_config.produce_item_name_input,         0x1|0x2, 0,     0x9F,           },
	{ "display_skill_fail",                 &battle_config.display_skill_fail,              2,      0,      1|2|4|8,        },
	{ "chat_warpportal",                    &battle_config.chat_warpportal,                 0,      0,      1,              },
	{ "mob_warp",                           &battle_config.mob_warp,                        0,      0,      1|2|4,          },
	{ "dead_branch_active",                 &battle_config.dead_branch_active,              1,      0,      1,              },
	{ "vending_max_value",                  &battle_config.vending_max_value,               10000000, 1,    MAX_ZENY,       },
	{ "vending_over_max",                   &battle_config.vending_over_max,                1,      0,      1,              },
	{ "show_steal_in_same_party",           &battle_config.show_steal_in_same_party,        0,      0,      1,              },
	{ "party_hp_mode",                      &battle_config.party_hp_mode,                   0,      0,      1,              },
	{ "show_party_share_picker",            &battle_config.party_show_share_picker,         1,      0,      1,              },
	{ "show_picker.item_type",              &battle_config.show_picker_item_type,           112,    0,      INT_MAX,        },
	{ "party_update_interval",              &battle_config.party_update_interval,           1000,   100,    INT_MAX,        },
	{ "party_item_share_type",              &battle_config.party_share_type,                0,      0,      1|2|3,          },
	{ "attack_attr_none",                   &battle_config.attack_attr_none,                ~BL_PC, BL_NUL, BL_ALL,         },
	{ "gx_allhit",                          &battle_config.gx_allhit,                       0,      0,      1,              },
	{ "gx_disptype",                        &battle_config.gx_disptype,                     1,      0,      1,              },
	{ "devotion_level_difference",          &battle_config.devotion_level_difference,       10,     0,      INT_MAX,        },
	{ "player_skill_partner_check",         &battle_config.player_skill_partner_check,      1,      0,      1,              },
	{ "invite_request_check",               &battle_config.invite_request_check,            1,      0,      1,              },
	{ "skill_removetrap_type",              &battle_config.skill_removetrap_type,           0,      0,      1,              },
	{ "disp_experience",                    &battle_config.disp_experience,                 0,      0,      1,              },
	{ "disp_zeny",                          &battle_config.disp_zeny,                       0,      0,      1,              },
	{ "castle_defense_rate",                &battle_config.castle_defense_rate,             100,    0,      100,            },
	{ "bone_drop",                          &battle_config.bone_drop,                       0,      0,      2,              },
	{ "buyer_name",                         &battle_config.buyer_name,                      1,      0,      1,              },
	{ "skill_wall_check",                   &battle_config.skill_wall_check,                1,      0,      1,              },
	{ "official_cell_stack_limit",          &battle_config.official_cell_stack_limit,       1,      0,      255,            },
	{ "custom_cell_stack_limit",            &battle_config.custom_cell_stack_limit,         1,      1,      255,            },
	{ "dancing_weaponswitch_fix",           &battle_config.dancing_weaponswitch_fix,        1,      0,      1,              },
	{ "check_occupied_cells",               &battle_config.check_occupied_cells,            1,      0,      1,              },
	
	//eAthena additions
	{ "item_logarithmic_drops",             &battle_config.logarithmic_drops,               0,      0,      1,              },
	{ "item_drop_common_min",               &battle_config.item_drop_common_min,            1,      0,      10000,          },
	{ "item_drop_common_max",               &battle_config.item_drop_common_max,            10000,  1,      10000,          },
	{ "item_drop_equip_min",                &battle_config.item_drop_equip_min,             1,      0,      10000,          },
	{ "item_drop_equip_max",                &battle_config.item_drop_equip_max,             10000,  1,      10000,          },
	{ "item_drop_card_min",                 &battle_config.item_drop_card_min,              1,      0,      10000,          },
	{ "item_drop_card_max",                 &battle_config.item_drop_card_max,              10000,  1,      10000,          },
	{ "item_drop_mvp_min",                  &battle_config.item_drop_mvp_min,               1,      0,      10000,          },
	{ "item_drop_mvp_max",                  &battle_config.item_drop_mvp_max,               10000,  1,      10000,          },
	{ "item_drop_mvp_mode",                 &battle_config.item_drop_mvp_mode,              0,      0,      2,              },
	{ "item_drop_heal_min",                 &battle_config.item_drop_heal_min,              1,      0,      10000,          },
	{ "item_drop_heal_max",                 &battle_config.item_drop_heal_max,              10000,  1,      10000,          },
	{ "item_drop_use_min",                  &battle_config.item_drop_use_min,               1,      0,      10000,          },
	{ "item_drop_use_max",                  &battle_config.item_drop_use_max,               10000,  1,      10000,          },
	{ "item_drop_add_min",                  &battle_config.item_drop_adddrop_min,           1,      0,      10000,          },
	{ "item_drop_add_max",                  &battle_config.item_drop_adddrop_max,           10000,  1,      10000,          },
	{ "item_drop_treasure_min",             &battle_config.item_drop_treasure_min,          1,      0,      10000,          },
	{ "item_drop_treasure_max",             &battle_config.item_drop_treasure_max,          10000,  1,      10000,          },
	{ "item_rate_mvp",                      &battle_config.item_rate_mvp,                   100,    0,      1000000,        },
	{ "item_rate_common",                   &battle_config.item_rate_common,                100,    0,      1000000,        },
	{ "item_rate_common_boss",              &battle_config.item_rate_common_boss,           100,    0,      1000000,        },
	{ "item_rate_common_mvp",               &battle_config.item_rate_common_mvp,            100,    0,      1000000,        },
	{ "item_rate_equip",                    &battle_config.item_rate_equip,                 100,    0,      1000000,        },
	{ "item_rate_equip_boss",               &battle_config.item_rate_equip_boss,            100,    0,      1000000,        },
	{ "item_rate_equip_mvp",                &battle_config.item_rate_equip_mvp,             100,    0,      1000000,        },
	{ "item_rate_card",                     &battle_config.item_rate_card,                  100,    0,      1000000,        },
	{ "item_rate_card_boss",                &battle_config.item_rate_card_boss,             100,    0,      1000000,        },
	{ "item_rate_card_mvp",                 &battle_config.item_rate_card_mvp,              100,    0,      1000000,        },
	{ "item_rate_heal",                     &battle_config.item_rate_heal,                  100,    0,      1000000,        },
	{ "item_rate_heal_boss",                &battle_config.item_rate_heal_boss,             100,    0,      1000000,        },
	{ "item_rate_heal_mvp",                 &battle_config.item_rate_heal_mvp,              100,    0,      1000000,        },
	{ "item_rate_use",                      &battle_config.item_rate_use,                   100,    0,      1000000,        },
	{ "item_rate_use_boss",                 &battle_config.item_rate_use_boss,              100,    0,      1000000,        },
	{ "item_rate_use_mvp",                  &battle_config.item_rate_use_mvp,               100,    0,      1000000,        },
	{ "item_rate_adddrop",                  &battle_config.item_rate_adddrop,               100,    0,      1000000,        },
	{ "item_rate_treasure",                 &battle_config.item_rate_treasure,              100,    0,      1000000,        },
	{ "prevent_logout",                     &battle_config.prevent_logout,                  10000,  0,      60000,          },
	{ "prevent_logout_trigger",             &battle_config.prevent_logout_trigger,          0xE,    0,      0xF,            },
	{ "alchemist_summon_reward",            &battle_config.alchemist_summon_reward,         1,      0,      2,              },
	{ "drops_by_luk",                       &battle_config.drops_by_luk,                    0,      0,      INT_MAX,        },
	{ "drops_by_luk2",                      &battle_config.drops_by_luk2,                   0,      0,      INT_MAX,        },
	{ "equip_natural_break_rate",           &battle_config.equip_natural_break_rate,        0,      0,      INT_MAX,        },
	{ "equip_self_break_rate",              &battle_config.equip_self_break_rate,           100,    0,      INT_MAX,        },
	{ "equip_skill_break_rate",             &battle_config.equip_skill_break_rate,          100,    0,      INT_MAX,        },
	{ "pk_mode",                            &battle_config.pk_mode,                         0,      0,      2,              },
	{ "pk_mode_mes",                        &battle_config.pk_mode_mes,                     1,      0,      1,              },
	{ "pk_level_range",                     &battle_config.pk_level_range,                  0,      0,      INT_MAX,        },
	{ "manner_system",                      &battle_config.manner_system,                   0xFFF,  0,      0xFFF,          },
	{ "multi_level_up",                     &battle_config.multi_level_up,                  0,      0,      1,              },
	{ "max_exp_gain_rate",                  &battle_config.max_exp_gain_rate,               0,      0,      INT_MAX,        },
	{ "backstab_bow_penalty",               &battle_config.backstab_bow_penalty,            0,      0,      1,              },
	{ "night_at_start",                     &battle_config.night_at_start,                  0,      0,      1,              },
	{ "show_mob_info",                      &battle_config.show_mob_info,                   0,      0,      1|2|4,          },
	{ "ban_hack_trade",                     &battle_config.ban_hack_trade,                  0,      0,      INT_MAX,        },
	{ "min_hair_style",                     &battle_config.min_hair_style,                  0,      0,      INT_MAX,        },
	{ "max_hair_style",                     &battle_config.max_hair_style,                  29,     0,      INT_MAX,        },
	{ "min_hair_color",                     &battle_config.min_hair_color,                  0,      0,      INT_MAX,        },
	{ "max_hair_color",                     &battle_config.max_hair_color,                  8,      0,      INT_MAX,        },
	{ "min_cloth_color",                    &battle_config.min_cloth_color,                 0,      0,      INT_MAX,        },
	{ "max_cloth_color",                    &battle_config.max_cloth_color,                 4,      0,      INT_MAX,        },
	{ "min_body_style",                     &battle_config.min_body_style,                  0,      0,      SHRT_MAX,       },
	{ "max_body_style",                     &battle_config.max_body_style,                  1,      0,      SHRT_MAX,       },
	{ "min_doram_hair_style",               &battle_config.min_doram_hair_style,            0,      0,      SHRT_MAX,       },
	{ "max_doram_hair_style",               &battle_config.max_doram_hair_style,            6,      0,      SHRT_MAX,       },
	{ "min_doram_hair_color",               &battle_config.min_doram_hair_color,            0,      0,      SHRT_MAX,       },
	{ "max_doram_hair_color",               &battle_config.max_doram_hair_color,            8,      0,      SHRT_MAX,       },
	{ "min_doram_cloth_color",              &battle_config.min_doram_cloth_color,           0,      0,      SHRT_MAX,       },
	{ "max_doram_cloth_color",              &battle_config.max_doram_cloth_color,           0,      0,      SHRT_MAX,       },
	{ "pet_hair_style",                     &battle_config.pet_hair_style,                  100,    0,      INT_MAX,        },
	{ "castrate_dex_scale",                 &battle_config.castrate_dex_scale,              150,    1,      INT_MAX,        },
	{ "vcast_stat_scale",                   &battle_config.vcast_stat_scale,                530,    1,      INT_MAX,        },
	{ "area_size",                          &battle_config.area_size,                       14,     0,      INT_MAX,        },
	{ "chat_area_size",                     &battle_config.chat_area_size,                  9,      0,      INT_MAX,        },
	{ "zeny_from_mobs",                     &battle_config.zeny_from_mobs,                  0,      0,      1,              },
	{ "mobs_level_up",                      &battle_config.mobs_level_up,                   0,      0,      1,              },
	{ "mobs_level_up_exp_rate",             &battle_config.mobs_level_up_exp_rate,          1,      1,      INT_MAX,        },
	{ "pk_min_level",                       &battle_config.pk_min_level,                    55,     1,      INT_MAX,        },
	{ "skill_steal_max_tries",              &battle_config.skill_steal_max_tries,           0,      0,      UCHAR_MAX,      },
	{ "motd_type",                          &battle_config.motd_type,                       0,      0,      1,              },
	{ "finding_ore_rate",                   &battle_config.finding_ore_rate,                100,    0,      INT_MAX,        },
	{ "exp_calc_type",                      &battle_config.exp_calc_type,                   0,      0,      1,              },
	{ "exp_bonus_attacker",                 &battle_config.exp_bonus_attacker,              25,     0,      INT_MAX,        },
	{ "exp_bonus_max_attacker",             &battle_config.exp_bonus_max_attacker,          12,     2,      INT_MAX,        },
	{ "min_skill_delay_limit",              &battle_config.min_skill_delay_limit,           100,    10,     INT_MAX,        },
	{ "default_walk_delay",                 &battle_config.default_walk_delay,              300,    0,      INT_MAX,        },
	{ "no_skill_delay",                     &battle_config.no_skill_delay,                  BL_MOB, BL_NUL, BL_ALL,         },
	{ "attack_walk_delay",                  &battle_config.attack_walk_delay,               BL_ALL, BL_NUL, BL_ALL,         },
	{ "require_glory_guild",                &battle_config.require_glory_guild,             0,      0,      1,              },
	{ "idle_no_share",                      &battle_config.idle_no_share,                   0,      0,      INT_MAX,        },
	{ "party_even_share_bonus",             &battle_config.party_even_share_bonus,          0,      0,      INT_MAX,        },
	{ "delay_battle_damage",                &battle_config.delay_battle_damage,             1,      0,      1,              },
	{ "hide_woe_damage",                    &battle_config.hide_woe_damage,                 0,      0,      1,              },
	{ "display_version",                    &battle_config.display_version,                 1,      0,      1,              },
	{ "display_hallucination",              &battle_config.display_hallucination,           1,      0,      1,              },
	{ "use_statpoint_table",                &battle_config.use_statpoint_table,             1,      0,      1,              },
	{ "ignore_items_gender",                &battle_config.ignore_items_gender,             1,      0,      1,              },
	{ "berserk_cancels_buffs",              &battle_config.berserk_cancels_buffs,           0,      0,      1,              },
	{ "monster_ai",                         &battle_config.mob_ai,                          0x000,  0x000,  0xFFF,          },
	{ "hom_setting",                        &battle_config.hom_setting,                     0xFFFF, 0x0000, 0xFFFF,         },
	{ "dynamic_mobs",                       &battle_config.dynamic_mobs,                    1,      0,      1,              },
	{ "mob_remove_damaged",                 &battle_config.mob_remove_damaged,              1,      0,      1,              },
	{ "show_hp_sp_drain",                   &battle_config.show_hp_sp_drain,                0,      0,      1,              },
	{ "show_hp_sp_gain",                    &battle_config.show_hp_sp_gain,                 1,      0,      1,              },
	{ "mob_npc_event_type",                 &battle_config.mob_npc_event_type,              1,      0,      1,              },
	{ "character_size",                     &battle_config.character_size,                  1|2,    0,      1|2,            },
	{ "mob_max_skilllvl",                   &battle_config.mob_max_skilllvl,                MAX_SKILL_LEVEL, 1, MAX_SKILL_LEVEL, },
	{ "retaliate_to_master",                &battle_config.retaliate_to_master,             1,      0,      1,              },
	{ "rare_drop_announce",                 &battle_config.rare_drop_announce,              0,      0,      10000,          },
	{ "duel_allow_pvp",                     &battle_config.duel_allow_pvp,                  0,      0,      1,              },
	{ "duel_allow_gvg",                     &battle_config.duel_allow_gvg,                  0,      0,      1,              },
	{ "duel_allow_teleport",                &battle_config.duel_allow_teleport,             0,      0,      1,              },
	{ "duel_autoleave_when_die",            &battle_config.duel_autoleave_when_die,         1,      0,      1,              },
	{ "duel_time_interval",                 &battle_config.duel_time_interval,              60,     0,      INT_MAX,        },
	{ "duel_only_on_same_map",              &battle_config.duel_only_on_same_map,           0,      0,      1,              },
	{ "skip_teleport_lv1_menu",             &battle_config.skip_teleport_lv1_menu,          0,      0,      1,              },
	{ "allow_skill_without_day",            &battle_config.allow_skill_without_day,         0,      0,      1,              },
	{ "allow_es_magic_player",              &battle_config.allow_es_magic_pc,               0,      0,      1,              },
	{ "skill_caster_check",                 &battle_config.skill_caster_check,              1,      0,      1,              },
	{ "status_cast_cancel",                 &battle_config.sc_castcancel,                   BL_NUL, BL_NUL, BL_ALL,         },
	{ "pc_status_def_rate",                 &battle_config.pc_sc_def_rate,                  100,    0,      INT_MAX,        },
	{ "mob_status_def_rate",                &battle_config.mob_sc_def_rate,                 100,    0,      INT_MAX,        },
	{ "pc_max_status_def",                  &battle_config.pc_max_sc_def,                   100,    0,      INT_MAX,        },
	{ "mob_max_status_def",                 &battle_config.mob_max_sc_def,                  100,    0,      INT_MAX,        },
	{ "sg_miracle_skill_ratio",             &battle_config.sg_miracle_skill_ratio,          1,      0,      10000,          },
	{ "sg_angel_skill_ratio",               &battle_config.sg_angel_skill_ratio,            10,     0,      10000,          },
	{ "autospell_stacking",                 &battle_config.autospell_stacking,              0,      0,      1,              },
	{ "override_mob_names",                 &battle_config.override_mob_names,              0,      0,      2,              },
	{ "min_chat_delay",                     &battle_config.min_chat_delay,                  0,      0,      INT_MAX,        },
	{ "friend_auto_add",                    &battle_config.friend_auto_add,                 1,      0,      1,              },
	{ "hom_rename",                         &battle_config.hom_rename,                      0,      0,      1,              },
	{ "homunculus_show_growth",             &battle_config.homunculus_show_growth,          0,      0,      1,              },
	{ "homunculus_friendly_rate",           &battle_config.homunculus_friendly_rate,        100,    0,      INT_MAX,        },
	{ "vending_tax",                        &battle_config.vending_tax,                     0,      0,      10000,          },
	{ "vending_tax_min",                    &battle_config.vending_tax_min,                 0,      0,      MAX_ZENY,       },
	{ "day_duration",                       &battle_config.day_duration,                    0,      0,      INT_MAX,        },
	{ "night_duration",                     &battle_config.night_duration,                  0,      0,      INT_MAX,        },
	{ "mob_remove_delay",                   &battle_config.mob_remove_delay,                60000,  1000,   INT_MAX,        },
	{ "mob_active_time",                    &battle_config.mob_active_time,                 0,      0,      INT_MAX,        },
	{ "boss_active_time",                   &battle_config.boss_active_time,                0,      0,      INT_MAX,        },
	{ "sg_miracle_skill_duration",          &battle_config.sg_miracle_skill_duration,       3600000, 0,     INT_MAX,        },
	{ "hvan_explosion_intimate",            &battle_config.hvan_explosion_intimate,         45000,  0,      100000,         },
	{ "quest_exp_rate",                     &battle_config.quest_exp_rate,                  100,    0,      INT_MAX,        },
	{ "at_mapflag",                         &battle_config.autotrade_mapflag,               0,      0,      1,              },
	{ "at_timeout",                         &battle_config.at_timeout,                      0,      0,      INT_MAX,        },
	{ "homunculus_autoloot",                &battle_config.homunculus_autoloot,             0,      0,      1,              },
	{ "idle_no_autoloot",                   &battle_config.idle_no_autoloot,                0,      0,      INT_MAX,        },
	{ "max_guild_alliance",                 &battle_config.max_guild_alliance,              3,      0,      3,              },
	{ "ksprotection",                       &battle_config.ksprotection,                    5000,   0,      INT_MAX,        },
	{ "auction_feeperhour",                 &battle_config.auction_feeperhour,              12000,  0,      INT_MAX,        },
	{ "auction_maximumprice",               &battle_config.auction_maximumprice,            500000000, 0,   MAX_ZENY,       },
	{ "homunculus_auto_vapor",              &battle_config.homunculus_auto_vapor,           1,      0,      1,              },
	{ "display_status_timers",              &battle_config.display_status_timers,           1,      0,      1,              },
	{ "skill_add_heal_rate",                &battle_config.skill_add_heal_rate,             39,     0,      INT_MAX,        },
	{ "eq_single_target_reflectable",       &battle_config.eq_single_target_reflectable,    1,      0,      1,              },
	{ "invincible.nodamage",                &battle_config.invincible_nodamage,             0,      0,      1,              },
	{ "mob_slave_keep_target",              &battle_config.mob_slave_keep_target,           0,      0,      1,              },
	{ "autospell_check_range",              &battle_config.autospell_check_range,           0,      0,      1,              },
	{ "client_reshuffle_dice",              &battle_config.client_reshuffle_dice,           0,      0,      1,              },
	{ "client_sort_storage",                &battle_config.client_sort_storage,             0,      0,      1,              },
	{ "feature.buying_store",               &battle_config.feature_buying_store,            1,      0,      1,              },
	{ "feature.search_stores",              &battle_config.feature_search_stores,           1,      0,      1,              },
	{ "searchstore_querydelay",             &battle_config.searchstore_querydelay,         10,      0,      INT_MAX,        },
	{ "searchstore_maxresults",             &battle_config.searchstore_maxresults,         30,      1,      INT_MAX,        },
	{ "display_party_name",                 &battle_config.display_party_name,              0,      0,      1,              },
	{ "cashshop_show_points",               &battle_config.cashshop_show_points,            0,      0,      1,              },
	{ "mail_show_status",                   &battle_config.mail_show_status,                0,      0,      2,              },
	{ "client_limit_unit_lv",               &battle_config.client_limit_unit_lv,            0,      0,      BL_ALL,         },
	//BattleGround Settings
	{ "bg_update_interval",                 &battle_config.bg_update_interval,              1000,   100,    INT_MAX,        },
	{ "bg_short_attack_damage_rate",        &battle_config.bg_short_damage_rate,            80,     0,      INT_MAX,        },
	{ "bg_long_attack_damage_rate",         &battle_config.bg_long_damage_rate,             80,     0,      INT_MAX,        },
	{ "bg_weapon_attack_damage_rate",       &battle_config.bg_weapon_damage_rate,           60,     0,      INT_MAX,        },
	{ "bg_magic_attack_damage_rate",        &battle_config.bg_magic_damage_rate,            60,     0,      INT_MAX,        },
	{ "bg_misc_attack_damage_rate",         &battle_config.bg_misc_damage_rate,             60,     0,      INT_MAX,        },
	{ "bg_flee_penalty",                    &battle_config.bg_flee_penalty,                 20,     0,      INT_MAX,        },
	{ "max_third_parameter",                &battle_config.max_third_parameter,             130,    10,     SHRT_MAX,       },
	{ "max_baby_third_parameter",           &battle_config.max_baby_third_parameter,        117,    10,     SHRT_MAX,       },
	{ "max_trans_parameter",                &battle_config.max_trans_parameter,             99,     10,     SHRT_MAX,       },
	{ "max_third_trans_parameter",          &battle_config.max_third_trans_parameter,       130,    10,     SHRT_MAX,       },
	{ "max_extended_parameter",             &battle_config.max_extended_parameter,          125,    10,     SHRT_MAX,       },
	{ "skill_amotion_leniency",             &battle_config.skill_amotion_leniency,          0,      0,      300,            },
	{ "mvp_tomb_enabled",                   &battle_config.mvp_tomb_enabled,                1,      0,      1,              },
	{ "mvp_tomb_delay",                     &battle_config.mvp_tomb_delay,               9000,      0,      INT_MAX,        },
	{ "feature.atcommand_suggestions",      &battle_config.atcommand_suggestions_enabled,   0,      0,      1,              },
	{ "min_npc_vendchat_distance",          &battle_config.min_npc_vendchat_distance,       3,      0,      100,            },
	{ "atcommand_mobinfo_type",             &battle_config.atcommand_mobinfo_type,          0,      0,      1,              },
	{ "homunculus_max_level",               &battle_config.hom_max_level,                   99,     0,      MAX_LEVEL,      },
	{ "homunculus_S_max_level",             &battle_config.hom_S_max_level,                 150,    0,      MAX_LEVEL,      },
	{ "mob_size_influence",                 &battle_config.mob_size_influence,              0,      0,      1,              },
	{ "skill_trap_type",                    &battle_config.skill_trap_type,                 0,      0,      3,              },
	{ "allow_consume_restricted_item",      &battle_config.allow_consume_restricted_item,   1,      0,      1,              },
	{ "allow_equip_restricted_item",        &battle_config.allow_equip_restricted_item,     1,      0,      1,              },
	{ "max_walk_path",                      &battle_config.max_walk_path,                  17,      1,      MAX_WALKPATH,   },
	{ "item_enabled_npc",                   &battle_config.item_enabled_npc,                1,      0,      1,              },
	{ "item_flooritem_check",               &battle_config.item_onfloor,                    1,      0,      1,              },
	{ "bowling_bash_area",                  &battle_config.bowling_bash_area,               0,      0,      20,             },
	{ "gm_ignore_warpable_area",            &battle_config.gm_ignore_warpable_area,         0,      2,      100,            },
	{ "snovice_call_type",                  &battle_config.snovice_call_type,               0,      0,      1,              },
	{ "guild_notice_changemap",             &battle_config.guild_notice_changemap,          2,      0,      2,              },
	{ "drop_rateincrease",                  &battle_config.drop_rateincrease,               0,      0,      1,              },
	{ "feature.auction",                    &battle_config.feature_auction,                 0,      0,      2,              },
	{ "mon_trans_disable_in_gvg",           &battle_config.mon_trans_disable_in_gvg,        0,      0,      1,              },
	{ "feature.banking",                    &battle_config.feature_banking,                 1,      0,      1,              },
	{ "homunculus_S_growth_level",          &battle_config.hom_S_growth_level,             99,      0,      MAX_LEVEL,      },
	{ "emblem_woe_change",                  &battle_config.emblem_woe_change,               0,      0,      1,              },
	{ "emblem_transparency_limit",          &battle_config.emblem_transparency_limit,      80,      0,    100,              },
#ifdef VIP_ENABLE
	{ "vip_storage_increase",               &battle_config.vip_storage_increase,           300,      0,      MAX_STORAGE - MIN_STORAGE, },
#else
	{ "vip_storage_increase",               &battle_config.vip_storage_increase,           300,      0,      MAX_STORAGE, },
#endif
	{ "vip_base_exp_increase",              &battle_config.vip_base_exp_increase,          50,      0,      INT_MAX,        },
	{ "vip_job_exp_increase",               &battle_config.vip_job_exp_increase,           50,      0,      INT_MAX,        },
	{ "vip_exp_penalty_base",               &battle_config.vip_exp_penalty_base,          100,      0,      INT_MAX,        },
	{ "vip_exp_penalty_job",                &battle_config.vip_exp_penalty_job,           100,      0,      INT_MAX,        },
	{ "vip_zeny_penalty",                   &battle_config.vip_zeny_penalty,                0,      0,      INT_MAX,        },
	{ "vip_bm_increase",                    &battle_config.vip_bm_increase,                 2,      0,      INT_MAX,        },
	{ "vip_drop_increase",                  &battle_config.vip_drop_increase,              50,      0,      INT_MAX,        },
	{ "vip_gemstone",                       &battle_config.vip_gemstone,                    2,      0,      2,              },
	{ "vip_disp_rate",                      &battle_config.vip_disp_rate,                   1,      0,      1,              },
	{ "discount_item_point_shop",           &battle_config.discount_item_point_shop,        0,      0,      3,              },
	{ "oktoberfest_ignorepalette",          &battle_config.oktoberfest_ignorepalette,       0,      0,      1,              },
	{ "update_enemy_position",              &battle_config.update_enemy_position,           0,      0,      1,              },
	{ "devotion_rdamage",                   &battle_config.devotion_rdamage,                0,      0,    100,              },
	{ "feature.autotrade",                  &battle_config.feature_autotrade,               1,      0,      1,              },
	{ "feature.autotrade_direction",        &battle_config.feature_autotrade_direction,     4,      -1,     7,              },
	{ "feature.autotrade_head_direction",   &battle_config.feature_autotrade_head_direction,0,      -1,     2,              },
	{ "feature.autotrade_sit",              &battle_config.feature_autotrade_sit,           1,      -1,     1,              },
	{ "feature.autotrade_open_delay",       &battle_config.feature_autotrade_open_delay,    5000,   1000,   INT_MAX,        },
	{ "feature.autotrade_move",             &battle_config.feature_autotrade_move,          0,      0,      1,              },
	{ "disp_servervip_msg",                 &battle_config.disp_servervip_msg,              0,      0,      1,              },
	{ "warg_can_falcon",                    &battle_config.warg_can_falcon,                 0,      0,      1,              },
	{ "path_blown_halt",                    &battle_config.path_blown_halt,                 1,      0,      1,              },
	{ "rental_mount_speed_boost",           &battle_config.rental_mount_speed_boost,        25,     0,      100,            },
	{ "feature.warp_suggestions",           &battle_config.warp_suggestions_enabled,        0,      0,      1,              },
	{ "taekwon_mission_mobname",            &battle_config.taekwon_mission_mobname,         0,      0,      2,              },
	{ "teleport_on_portal",                 &battle_config.teleport_on_portal,              0,      0,      1,              },
	{ "cart_revo_knockback",                &battle_config.cart_revo_knockback,             1,      0,      1,              },
	{ "guild_castle_invite",                &battle_config.guild_castle_invite,             0,      0,      1,              },
	{ "guild_castle_expulsion",             &battle_config.guild_castle_expulsion,          0,      0,      1,              },
	{ "transcendent_status_points",         &battle_config.transcendent_status_points,     52,      1,      INT_MAX,        },
	{ "taekwon_ranker_min_lv",              &battle_config.taekwon_ranker_min_lv,          90,      1,      MAX_LEVEL,      },
	{ "revive_onwarp",                      &battle_config.revive_onwarp,                   1,      0,      1,              },
	{ "fame_taekwon_mission",               &battle_config.fame_taekwon_mission,            1,      0,      INT_MAX,        },
	{ "fame_refine_lv1",                    &battle_config.fame_refine_lv1,                 1,      0,      INT_MAX,        },
	{ "fame_refine_lv1",                    &battle_config.fame_refine_lv1,                 1,      0,      INT_MAX,        },
	{ "fame_refine_lv2",                    &battle_config.fame_refine_lv2,                 25,     0,      INT_MAX,        },
	{ "fame_refine_lv3",                    &battle_config.fame_refine_lv3,                 1000,   0,      INT_MAX,        },
	{ "fame_forge",                         &battle_config.fame_forge,                      10,     0,      INT_MAX,        },
	{ "fame_pharmacy_3",                    &battle_config.fame_pharmacy_3,                 1,      0,      INT_MAX,        },
	{ "fame_pharmacy_5",                    &battle_config.fame_pharmacy_5,                 3,      0,      INT_MAX,        },
	{ "fame_pharmacy_7",                    &battle_config.fame_pharmacy_7,                 10,     0,      INT_MAX,        },
	{ "fame_pharmacy_10",                   &battle_config.fame_pharmacy_10,                50,     0,      INT_MAX,        },
	{ "mail_delay",                         &battle_config.mail_delay,                      1000,   1000,   INT_MAX,        },
	{ "at_monsterignore",                   &battle_config.autotrade_monsterignore,         0,      0,      1,              },
	{ "spawn_direction",                    &battle_config.spawn_direction,                 0,      0,      1,              },
	{ "arrow_shower_knockback",             &battle_config.arrow_shower_knockback,          1,      0,      1,              },
	{ "devotion_rdamage_skill_only",        &battle_config.devotion_rdamage_skill_only,     1,      0,      1,              },
	{ "max_extended_aspd",                  &battle_config.max_extended_aspd,               193,    100,    199,            },
	{ "max_summoner_aspd",                  &battle_config.max_summoner_aspd,               193,    100,    199,            },
	{ "knockback_left",                     &battle_config.knockback_left,                  1,      0,      1,              },
	{ "song_timer_reset",                   &battle_config.song_timer_reset,                0,      0,      1,              },
	{ "cursed_circle_in_gvg",               &battle_config.cursed_circle_in_gvg,            1,      0,      1,              },
	{ "snap_dodge",                         &battle_config.snap_dodge,                      0,      0,      1,              },
	{ "monster_chase_refresh",              &battle_config.mob_chase_refresh,               3,      0,      30,             },
	{ "mob_icewall_walk_block",             &battle_config.mob_icewall_walk_block,          75,     0,      255,            },
	{ "boss_icewall_walk_block",            &battle_config.boss_icewall_walk_block,         0,      0,      255,            },
	{ "stormgust_knockback",                &battle_config.stormgust_knockback,             1,      0,      1,              },
	{ "default_fixed_castrate",             &battle_config.default_fixed_castrate,          20,     0,      100,            },
	{ "default_bind_on_equip",              &battle_config.default_bind_on_equip,    BOUND_CHAR, BOUND_NONE, BOUND_MAX - 1, },
	{ "homunculus_evo_intimacy_need",       &battle_config.homunculus_evo_intimacy_need,    91100,  0,      INT_MAX,        },
	{ "homunculus_evo_intimacy_reset",      &battle_config.homunculus_evo_intimacy_reset,   1000,   0,      INT_MAX,        },
	{ "monster_loot_search_type",           &battle_config.monster_loot_search_type,        1,      0,      1,              },
	{ "max_homunculus_hp",                  &battle_config.max_homunculus_hp,               32767,  100,    INT_MAX,        },
	{ "max_homunculus_sp",                  &battle_config.max_homunculus_sp,               32767,  100,    INT_MAX,        },
	{ "max_homunculus_parameter",           &battle_config.max_homunculus_parameter,        150,    10,     SHRT_MAX,       },
	{ "feature.roulette",                   &battle_config.feature_roulette,                1,      0,      1,              },
	{ "monster_hp_bars_info",               &battle_config.monster_hp_bars_info,            1,      0,      1,              },
	{ "mvp_exp_reward_message",             &battle_config.mvp_exp_reward_message,          0,      0,      1,              },
	{ "max_summoner_parameter",             &battle_config.max_summoner_parameter,          120,    10,     SHRT_MAX,       },
	{ "monster_eye_range_bonus",            &battle_config.mob_eye_range_bonus,             0,      0,      10,             },
	{ "crimsonrock_knockback",              &battle_config.crimsonrock_knockback,           1,      0,      1,              },
	{ "tarotcard_equal_chance",             &battle_config.tarotcard_equal_chance,          0,      0,      1,              },
	{ "show_status_katar_crit",             &battle_config.show_status_katar_crit,          0,      0,      1,              },
	{ "dispel_song",                        &battle_config.dispel_song,                     0,      0,      1,              },
	{ "monster_stuck_warning",              &battle_config.mob_stuck_warning,               0,      0,      1,              },
	{ "guild_maprespawn_clones",            &battle_config.guild_maprespawn_clones,         0,      0,      1,              },
	{ "skill_eightpath_algorithm",          &battle_config.skill_eightpath_algorithm,       1,      0,      1,              },
	{ "can_damage_skill",                   &battle_config.can_damage_skill,                1,      0,      BL_ALL,         },
	{ "atcommand_levelup_events",           &battle_config.atcommand_levelup_events,        0,      0,      1,              },
	{ "hide_fav_sell",                      &battle_config.hide_fav_sell,                   0,      0,      1,              },
	{ "death_penalty_maxlv",                &battle_config.death_penalty_maxlv,             0,      0,      3,              },
	{ "exp_cost_redemptio",                 &battle_config.exp_cost_redemptio,              1,      0,      100,            },
	{ "exp_cost_redemptio_limit",           &battle_config.exp_cost_redemptio_limit,        5,      0,      MAX_PARTY,      },
	{ "exp_cost_inspiration",               &battle_config.exp_cost_inspiration,            1,      0,      100,            },
	{ "block_account_in_same_party",        &battle_config.block_account_in_same_party,     1,      0,      1,              },
	{ "change_party_leader_samemap",        &battle_config.change_party_leader_samemap,     1,      0,      1,              },
	{ "mail_daily_count",                   &battle_config.mail_daily_count,                100,    0,      INT32_MAX,      },
	{ "mail_zeny_fee",                      &battle_config.mail_zeny_fee,                   2,      0,      100,            },
	{ "mail_attachment_price",              &battle_config.mail_attachment_price,           2500,   0,      INT32_MAX,      },
	{ "mail_attachment_weight",             &battle_config.mail_attachment_weight,          2000,   0,      INT32_MAX,      },
	{ "enable_critical_multihit",           &battle_config.enable_critical_multihit,        1,      0,      1,              },
	{ "guild_leaderchange_delay",           &battle_config.guild_leaderchange_delay,        1440,   0,      INT32_MAX,      },
	{ "guild_leaderchange_woe",             &battle_config.guild_leaderchange_woe,          0,      0,      1,              },
	{ "banana_bomb_duration",               &battle_config.banana_bomb_duration,            0,      0,      UINT16_MAX,     },
	{ "guild_alliance_onlygm",              &battle_config.guild_alliance_onlygm,           0,      0,      1,              },
	{ "event_refine_chance",                &battle_config.event_refine_chance,             0,      0,      1,              },
	{ "feature.achievement",                &battle_config.feature_achievement,             1,      0,      1,              },
	{ "allow_bound_sell",                   &battle_config.allow_bound_sell,                0,      0,      1|2,            },
	{ "autoloot_adjust",                    &battle_config.autoloot_adjust,                 0,      0,      1,              },
	{ "show_skill_scale",                   &battle_config.show_skill_scale,                1,      0,      1,              },
	{ "millennium_shield_health",           &battle_config.millennium_shield_health,        1000,   1,      INT_MAX,        },
	{ "hesperuslit_bonus_stack",            &battle_config.hesperuslit_bonus_stack,         0,      0,      1,              },
	{ "load_custom_exp_tables",             &battle_config.load_custom_exp_tables,          0,      0,      1,              },
	{ "feature.pet_autofeed",               &battle_config.feature_pet_autofeed,            1,      0,      1,              },
	{ "pet_autofeed_always",                &battle_config.pet_autofeed_always,             1,      0,      1,              },
	{ "feature.homunculus_autofeed",        &battle_config.feature_homunculus_autofeed,     1,      0,      1,              },
	{ "homunculus_autofeed_always",         &battle_config.homunculus_autofeed_always,      1,      0,      1,              },
	{ "feature.attendance",                 &battle_config.feature_attendance,              1,      0,      1,              },
	{ "feature.privateairship",             &battle_config.feature_privateairship,          1,      0,      1,              },
	{ "feature.refineui",                   &battle_config.feature_refineui,                1,      0,      1,              },
	{ "feature.stylistui",                  &battle_config.feature_stylistui,               1,      0,      1,              },

#include "../custom/battle_config_init.inc"
};

/*==========================
 * Set battle settings
 *--------------------------*/
int battle_set_value(const char *w1, const char *w2)
{
	int val = config_switch(w2);
	int i;

	ARR_FIND(0, ARRAYLENGTH(battle_data), i, strcmpi(w1, battle_data[i].str) == 0);
	if (i == ARRAYLENGTH(battle_data))
		return 0; //Not found

	if (val < battle_data[i].min || val > battle_data[i].max) {
		ShowWarning("Value for setting '%s': %s is invalid (min:%i max:%i)! Defaulting to %i...\n", w1, w2, battle_data[i].min, battle_data[i].max, battle_data[i].defval);
		val = battle_data[i].defval;
	}

	*battle_data[i].val = val;
	return 1;
}

/*===========================
 * Get battle settings
 *---------------------------*/
int battle_get_value(const char *w1)
{
	int i;

	ARR_FIND(0, ARRAYLENGTH(battle_data), i, strcmpi(w1, battle_data[i].str) == 0);
	if (i == ARRAYLENGTH(battle_data))
		return 0; //Not found
	else
		return *battle_data[i].val;
}

/*======================
 * Set default settings
 *----------------------*/
void battle_set_defaults()
{
	int i;

	for (i = 0; i < ARRAYLENGTH(battle_data); i++)
		*battle_data[i].val = battle_data[i].defval;
}

/*==================================
 * Cap certain battle.conf settings
 *----------------------------------*/
void battle_adjust_conf()
{
	battle_config.monster_max_aspd = 2000 - battle_config.monster_max_aspd * 10;
	battle_config.max_aspd = 2000 - battle_config.max_aspd * 10;
	battle_config.max_third_aspd = 2000 - battle_config.max_third_aspd * 10;
	battle_config.max_extended_aspd = 2000 - battle_config.max_extended_aspd * 10;
	battle_config.max_summoner_aspd = 2000 - battle_config.max_summoner_aspd * 10;
	battle_config.max_walk_speed = 100 * DEFAULT_WALK_SPEED / battle_config.max_walk_speed;
	battle_config.max_cart_weight *= 10;

	if (battle_config.max_def > 100 && !battle_config.weapon_defense_type) //Added by [Skotlex]
		battle_config.max_def = 100;

	if (battle_config.min_hitrate > battle_config.max_hitrate)
		battle_config.min_hitrate = battle_config.max_hitrate;

	if (battle_config.pet_max_atk1 > battle_config.pet_max_atk2) //Skotlex
		battle_config.pet_max_atk1 = battle_config.pet_max_atk2;

	if (battle_config.day_duration && battle_config.day_duration < 60000) //Added by [Yor]
		battle_config.day_duration = 60000;

	if (battle_config.night_duration && battle_config.night_duration < 60000) //Added by [Yor]
		battle_config.night_duration = 60000;

#if PACKETVER < 20100427
	if (battle_config.feature_buying_store) {
		ShowWarning("conf/battle/feature.conf:buying_store is enabled but it requires PACKETVER 2010-04-27 or newer, disabling...\n");
		battle_config.feature_buying_store = 0;
	}
#endif

#if PACKETVER < 20100803
	if (battle_config.feature_search_stores) {
		ShowWarning("conf/battle/feature.conf:search_stores is enabled but it requires PACKETVER 2010-08-03 or newer, disabling...\n");
		battle_config.feature_search_stores = 0;
	}
#endif

#if PACKETVER > 20120000 && PACKETVER < 20130515 //Exact date (when it started) not known
	if (battle_config.feature_auction) {
		ShowWarning("conf/battle/feature.conf:feature.auction is enabled but it is not stable on PACKETVER "EXPAND_AND_QUOTE(PACKETVER)", disabling...\n");
		ShowWarning("conf/battle/feature.conf:feature.auction change value to '2' to silence this warning and maintain it enabled\n");
		battle_config.feature_auction = 0;
	}
#elif PACKETVER >= 20141112
	if (battle_config.feature_auction) {
		ShowWarning("conf/battle/feature.conf:feature.auction is enabled but it is not available for clients from 2014-11-12 on, disabling...\n");
		ShowWarning("conf/battle/feature.conf:feature.auction change value to '2' to silence this warning and maintain it enabled\n");
		battle_config.feature_auction = 0;
	}
#endif

#if PACKETVER < 20130724
	if (battle_config.feature_banking) {
		ShowWarning("conf/battle/feature.conf banking is enabled but it requires PACKETVER 2013-07-24 or newer, disabling...\n");
		battle_config.feature_banking = 0;
	}
#endif

#if PACKETVER < 20131223
	if (battle_config.mvp_exp_reward_message) {
		ShowWarning("conf/battle/client.conf MVP EXP reward message is enabled but it requires PACKETVER 2013-12-23 or newer, disabling...\n");
		battle_config.mvp_exp_reward_message = 0;
	}
#endif

#if PACKETVER < 20141022
	if (battle_config.feature_roulette) {
		ShowWarning("conf/battle/feature.conf roulette is enabled but it requires PACKETVER 2014-10-22 or newer, disabling...\n");
		battle_config.feature_roulette = 0;
	}
#endif

#if PACKETVER < 20150513
	if (battle_config.feature_achievement) {
		ShowWarning("conf/battle/feature.conf achievement is enabled but it requires PACKETVER 2015-05-13 or newer, disabling...\n");
		battle_config.feature_achievement = 0;
	}
#endif

#if PACKETVER < 20170920
	if (battle_config.feature_homunculus_autofeed) {
		ShowWarning("conf/battle/feature.conf homunculus autofeeding is enabled but it requires PACKETVER 2017-09-20 or newer, disabling...\n");
		battle_config.feature_homunculus_autofeed = 0;
	}
#endif

#if PACKETVER < 20150513
	if (battle_config.feature_pet_autofeed) {
		ShowWarning("conf/battle/feature.conf pet autofeeding is enabled but it requires PACKETVER 2015-05-13 or newer, disabling...\n");
		battle_config.feature_pet_autofeed = 0;
	}
#endif

#if PACKETVER < 20180307
	if (battle_config.feature_attendance) {
		ShowWarning("conf/battle/feature.conf attendance system is enabled but it requires PACKETVER 2018-03-07 or newer, disabling...\n");
		battle_config.feature_attendance = 0;
	}
#endif

#if PACKETVER < 20180321
	if (battle_config.feature_privateairship) {
		ShowWarning("conf/battle/feature.conf private airship system is enabled but it requires PACKETVER 2018-03-21 or newer, disabling...\n");
		battle_config.feature_privateairship = 0;
	}
#endif

#if PACKETVER < 20161012
	if (battle_config.feature_refineui) {
		ShowWarning("conf/battle/feature.conf refine UI is enabled but it requires PACKETVER 2016-10-12 or newer, disabling...\n");
		battle_config.feature_refineui = 0;
	}
#endif

#ifndef CELL_NOSTACK
	if (battle_config.custom_cell_stack_limit != 1)
		ShowWarning("Battle setting 'custom_cell_stack_limit' takes no effect as this server was compiled without Cell Stack Limit support.\n");
#endif
}

/*=====================================
 * Read battle.conf settings from file
 *-------------------------------------*/
int battle_config_read(const char *cfgName)
{
	FILE *fp;
	static int count = 0;

	if (count == 0)
		battle_set_defaults();

	count++;

	fp = fopen(cfgName,"r");
	if (fp == NULL)
		ShowError("File not found: %s\n", cfgName);
	else {
		char line[1024], w1[1024], w2[1024];

		while(fgets(line, sizeof(line), fp)) {
			if (line[0] == '/' && line[1] == '/')
				continue;
			if (sscanf(line, "%1023[^:]:%1023s", w1, w2) != 2)
				continue;
			if (strcmpi(w1, "import") == 0)
				battle_config_read(w2);
			else if
				(battle_set_value(w1, w2) == 0)
				ShowWarning("Unknown setting '%s' in file %s\n", w1, cfgName);
		}

		fclose(fp);
	}

	count--;

	if (count == 0)
		battle_adjust_conf();

	return 0;
}

/*==========================
 * Initialize battle timer
 *--------------------------*/
void do_init_battle(void)
{
	delay_damage_ers = ers_new(sizeof(struct delay_damage),"battle.c::delay_damage_ers",ERS_OPT_CLEAR);
	add_timer_func_list(battle_delay_damage_sub, "battle_delay_damage_sub");
}

/*==================
 * End battle timer
 *------------------*/
void do_final_battle(void)
{
	ers_destroy(delay_damage_ers);
}
