// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _SKILL_H_
#define _SKILL_H_

#include "../common/mmo.h" // MAX_SKILL, struct square
#include "../common/db.h"
#include "../common/timer.h"
#include "map.h" // struct block_list
struct map_session_data;
struct homun_data;
struct skill_unit;
struct skill_unit_group;
struct status_change_entry;

#define MAX_SKILL_DB MAX_SKILL //Max Skill DB
#define MAX_SKILL_PRODUCE_DB 280 //Max Produce DB
#define MAX_PRODUCE_RESOURCE 12 //Max Produce requirements
#define MAX_SKILL_ARROW_DB 150 //Max Arrow Creation DB
#define MAX_ARROW_RESULT 5 //Max Arrow results/created
#define MAX_SKILL_ABRA_DB 160 //Max Skill list of Abracadabra DB
#define MAX_SKILL_IMPROVISE_DB 20 //Max Skill for Improvise
#define MAX_SKILL_LEVEL 100 //Max Skill Level
#define MAX_SKILL_CRIMSON_MARKER 3 //Max Crimson Marker targets (RL_C_MARKER)
#define MAX_SKILL_HOWLING_MINE 5 //Max Howling Mine targets (RL_H_MINE)
#define SKILL_NAME_LENGTH 31 //Max Skill Name length
#define SKILL_DESC_LENGTH 31 //Max Skill Desc length

DBMap *skilldb_name2id;

//Constants to identify a skill's nk value (damage properties)
//The NK value applies only to non INF_GROUND_SKILL skills
//when determining skill castend function to invoke.
enum e_skill_nk {
	NK_NO_DAMAGE      = 0x01,
	NK_SPLASH         = 0x02|0x04, //0x4 = splash & split
	NK_SPLASHSPLIT    = 0x04,
	NK_NO_CARDFIX_ATK = 0x08,
	NK_NO_ELEFIX      = 0x10,
	NK_IGNORE_DEF     = 0x20,
	NK_IGNORE_FLEE    = 0x40,
	NK_NO_CARDFIX_DEF = 0x80,
};

//Constants to identify the skill's inf value:
enum e_skill_inf {
	INF_ATTACK_SKILL  = 0x01,
	INF_GROUND_SKILL  = 0x02,
	INF_SELF_SKILL    = 0x04, //Skills casted on self where target is automatically chosen
	//0x08 not assigned
	INF_SUPPORT_SKILL = 0x10,
	INF_TARGET_TRAP   = 0x20,
};

//A skill with 3 would be no damage + splash: area of effect.
//Constants to identify a skill's inf2 value.
enum e_skill_inf2 {
	INF2_QUEST_SKILL      = 0x00001,
	INF2_NPC_SKILL        = 0x00002, //NPC skills are those that players can't have in their skill tree.
	INF2_WEDDING_SKILL    = 0x00004,
	INF2_SPIRIT_SKILL     = 0x00008,
	INF2_GUILD_SKILL      = 0x00010,
	INF2_SONG_DANCE       = 0x00020,
	INF2_ENSEMBLE_SKILL   = 0x00040,
	INF2_TRAP             = 0x00080,
	INF2_TARGET_SELF      = 0x00100, //Refers to ground placed skills that will target the caster as well (like Grandcross)
	INF2_NO_TARGET_SELF   = 0x00200,
	INF2_PARTY_ONLY       = 0x00400,
	INF2_GUILD_ONLY       = 0x00800,
	INF2_NO_ENEMY         = 0x01000,
	INF2_CHORUS_SKILL     = 0x02000, //Chorus skill
	INF2_NO_BG_GVG_DMG    = 0x04000, //Skill that ignores bg and gvg reduction
	INF2_NO_NEARNPC       = 0x08000, //Disable cast skill if near with NPC [Cydh]
	INF2_HIT_TRAP         = 0x10000, //Can hit trap-type skill (INF2_TRAP) [Cydh]
	INF2_SHOW_SKILL_SCALE = 0x20000,
};

//Skill info type 3
enum e_skill_inf3 {
	INF3_NOLP               = 0x00001, //Skill that can ignore SA_LANDPROTECTOR
	INF3_HIT_HIDING         = 0x00002, //Skill that having an affect to hiding target
	INF3_USABLE_HIDING      = 0x00004, //Skill that can be used while in SC_HIDING
	INF3_USABLE_DANCE       = 0x00008, //Skill that can be used while in SC_DANCING
	INF3_HIT_EMP            = 0x00010, //Skill that could hit emperium
	INF3_AUTOSHADOWSPELL    = 0x00020, //Skill that can be auto casted by SC_AUTOSHADOWSPELL
	INF3_KAGEHUMI_BL        = 0x00040, //Skill blocked by KG_KAGEHUMI
	INF3_EFF_VULTURE        = 0x00080, //Skill range affected by AC_VULTURE
	INF3_EFF_SNAKEEYE       = 0x00100, //Skill range affected by GS_SNAKEEYE
	INF3_EFF_SHADOWJUMP     = 0x00200, //Skill range affected by NJ_SHADOWJUMP
	INF3_EFF_RADIUS         = 0x00400, //Skill range affected by WL_RADIUS
	INF3_EFF_RESEARCHTRAP   = 0x00800, //Skill range affected by RA_RESEARCHTRAP
	INF3_USABLE_MANHOLE     = 0x01000, //Skill that can be used even under SC__MANHOLE
	INF3_USABLE_WARG        = 0x02000, //Skill that can be used while riding warg
	INF3_SC_GLOOMYDAY       = 0x04000, //Skill that affected by SC_GLOOMYDAY
	INF3_SC_DANCEWITHWUG    = 0x08000, //Skill that affected by SC_DANCEWITHWUG
	INF3_BITE_BLOCK         = 0x10000, //Skill blocked by RA_WUGBITE
	INF3_SC_UNLIMIT         = 0x20000, //Skill that affected by SC_UNLIMIT
	INF3_BOOST_PASSIVE      = 0x40000, //Boost passive skill (active skill that boosts the effects of passive skills)
	INF3_USABLE_MADO        = 0x80000, //Skill that can be used while on madogear
};

//Walk intervals at which chase-skills are attempted to be triggered
//If you change this, make sure it's an odd value (for icewall block behavior)
#define WALK_SKILL_INTERVAL 5

//Time that's added to canact delay on castbegin and substracted on castend
//This is to prevent hackers from sending a skill packet after cast but before a timer triggers castend
#define SECURITY_CASTTIME 100

//Flags passed to skill_attack/skill_area_sub
enum e_skill_display {
	SD_LEVEL     = 0x1000, //skill_attack will send -1 instead of skill level (affects display of some skills)
	SD_ANIMATION = 0x2000, //skill_attack will use 'DMG_SPLASH' instead of the skill's 'type' (this makes skills show an animation), also being used in skill_attack for splash skill (NK_SPLASH) to check status_check_skilluse
	SD_SPLASH    = 0x4000, //skill_area_sub will count targets in skill_area_temp[2]
	SD_PREAMBLE  = 0x8000, //skill_area_sub will transmit a 'magic' damage packet (-30000 dmg) for the first target selected
};

#define MAX_SKILL_ITEM_REQUIRE 10 //Maximum required items
#define MAX_SKILL_STATUS_REQUIRE 3 //Maximum required statuses
#define MAX_SKILL_EQUIP_REQUIRE 10 //Maximum required equipped item

struct skill_condition {
	int hp, //HP cost
	    mhp, //MaxHP to trigger
	    sp, //SP cost
	    hp_rate, //HP cost (%)
	    sp_rate, //SP cost (%)
	    ammo, //Ammo type
	    ammo_qty, //Amount of ammo
	    weapon, //Weapon type
	    zeny, //Zeny cost
	    state, //State/condition
	    spiritball, //Spiritball cost
	    itemid[MAX_SKILL_ITEM_REQUIRE], //Required item
	    amount[MAX_SKILL_ITEM_REQUIRE]; //Amount of item
	uint16 *eqItem; //List of equipped item
	enum sc_type *status; //List of Status required (SC)
	uint8 status_count, //Count of SC
		eqItem_count; //Count of equipped item
};

struct s_skill_require {
	int hp[MAX_SKILL_LEVEL], //HP cost
	    mhp[MAX_SKILL_LEVEL], //MaxHP to trigger
	    sp[MAX_SKILL_LEVEL], //SP cost
	    hp_rate[MAX_SKILL_LEVEL], //HP cost (%)
	    sp_rate[MAX_SKILL_LEVEL], //SP cost (%)
	    zeny[MAX_SKILL_LEVEL], //Zeny cost
	    weapon, //Weapon type
	    ammo, //Ammo type
	    ammo_qty[MAX_SKILL_LEVEL], //Amount of ammo
	    state, //State/condition
	    spiritball[MAX_SKILL_LEVEL], //Spiritball cost
	    itemid[MAX_SKILL_ITEM_REQUIRE], //Required item
	    amount[MAX_SKILL_ITEM_REQUIRE]; //Amount of item
	uint16 *eqItem; //List of equipped item
	enum sc_type *status; //List of Status required (SC)
	uint8 status_count, //Count of SC
		eqItem_count; //Count of equipped item
};

//Database skills
struct s_skill_db {
	char name[SKILL_NAME_LENGTH];
	char desc[SKILL_DESC_LENGTH];
	int range[MAX_SKILL_LEVEL],hit,inf,element[MAX_SKILL_LEVEL],nk,splash[MAX_SKILL_LEVEL],max;
	int num[MAX_SKILL_LEVEL];
	int cast[MAX_SKILL_LEVEL],walkdelay[MAX_SKILL_LEVEL],delay[MAX_SKILL_LEVEL];
#ifdef RENEWAL_CAST
	int fixed_cast[MAX_SKILL_LEVEL];
#endif
	int upkeep_time[MAX_SKILL_LEVEL],upkeep_time2[MAX_SKILL_LEVEL],cooldown[MAX_SKILL_LEVEL];
	int castcancel,cast_def_rate;
	int inf2,maxcount[MAX_SKILL_LEVEL],skill_type,inf3;
	int blewcount[MAX_SKILL_LEVEL];
	struct s_skill_require require;
	int castnodex[MAX_SKILL_LEVEL], delaynodex[MAX_SKILL_LEVEL];
	int32 nocast;
	int unit_id[2];
	int unit_layout_type[MAX_SKILL_LEVEL];
	int unit_range[MAX_SKILL_LEVEL];
	int unit_interval;
	int unit_target;
	int unit_flag;
	uint8 unit_nonearnpc_range; //Additional range for UF_NONEARNPC or INF2_NO_NEARNPC [Cydh]
	uint8 unit_nonearnpc_type; //Type of NPC [Cydh]
#ifdef ADJUST_SKILL_DAMAGE
	struct s_skill_damage damage;
#endif
	struct s_copyable { //[Cydh]
		int option;
		uint16 joballowed, req_opt;
	} copyable;
};
extern struct s_skill_db skill_db[MAX_SKILL_DB];

#define MAX_SQUARE_LAYOUT 7 //15 * 15 Placement of a maximum unit
#define MAX_SKILL_UNIT_LAYOUT (50 + MAX_SQUARE_LAYOUT) //50 special ones + the square ones
#define MAX_SKILL_UNIT_COUNT ((MAX_SQUARE_LAYOUT * 2 + 1) * (MAX_SQUARE_LAYOUT * 2 + 1))
struct s_skill_unit_layout {
	int count;
	int dx[MAX_SKILL_UNIT_COUNT];
	int dy[MAX_SKILL_UNIT_COUNT];
};

struct s_skill_nounit_layout {
	int count;
	int dx[MAX_SKILL_UNIT_COUNT];
	int dy[MAX_SKILL_UNIT_COUNT];
};

#define MAX_SKILLTIMERSKILL 50
struct skill_timerskill {
	int timer;
	int src_id;
	int target_id;
	int map;
	short x,y;
	uint16 skill_id,skill_lv;
	int type; //a BF_ type (NOTE: some places use this as general-purpose storage...)
	int flag;
};

#define MAX_SKILLUNITGROUP 25 //Maximum skill unit group (for same skill each source)
struct skill_unit_group {
	int src_id; //Caster ID/RID, if player is account_id
	int party_id; //Party ID
	int guild_id; //Guild ID
	int bg_id; //Battleground ID
	int map; //Map
	int target_flag; //Holds BCT_* flag for battle_check_target
	int bl_flag; //Holds BL_* flag for map_foreachin* functions
	unsigned int tick; //Tick when skill unit initialized
	int limit, //Life time
		interval; //Timer interval
	uint16 skill_id, //Skill ID
		skill_lv; //Skill level
	int val1, val2, val3; //Values
	char *valstr; //String value, used for HT_TALKIEBOX & RG_GRAFFITI
	int unit_id; //Unit ID (for client effect)
	int group_id; //Skill Group ID
	int link_group_id; //Linked group that should be deleted if this one is deleted
	int unit_count, //Number of unit at this group
		alive_count; //Number of alive unit
	int item_id; //Store item used.
	struct skill_unit *unit; //Skill Unit
	struct {
		unsigned ammo_consume : 1; //Need to consume ammo
		unsigned song_dance : 2; //0x1 Song/Dance, 0x2 Ensemble
		unsigned guildaura : 1; //Guild Aura
	} state;
};

//Skill unit
struct skill_unit {
	struct block_list bl;
	struct skill_unit_group *group; //Skill group reference
	int limit;
	int val1, val2, val3, val4;
	short range;
	unsigned alive : 1;
	int prev;
	unsigned hidden : 1;
};

#define MAX_SKILLUNITGROUPTICKSET 25
struct skill_unit_group_tickset {
	unsigned int tick;
	int id;
};

enum {
	UF_DEFNOTENEMY   = 0x0001, //If 'defunit_not_enemy' is set, the target is changed to 'friend'
	UF_NOREITERATION = 0x0002, //Unit cannot be stacked
	UF_NOFOOTSET     = 0x0004, //Unit cannot be cast near/on targets
	UF_NOOVERLAP     = 0x0008, //Unit effects do not overlap
	UF_PATHCHECK     = 0x0010, //Only cells with a shootable path will be placed
	UF_NOPC          = 0x0020, //May not target players
	UF_NOMOB         = 0x0040, //May not target mobs
	UF_SKILL         = 0x0080, //May target skills
	UF_DANCE         = 0x0100, //Dance
	UF_ENSEMBLE      = 0x0200, //Duet
	UF_SONG          = 0x0400, //Song
	UF_DUALMODE      = 0x0800, //Unit should trigger both ontimer and onplace/onout/onleft effects
	UF_HIDDEN_TRAP   = 0x1000, //Hidden trap [Cydh]
	UF_RANGEDSINGLEUNIT = 0x2000 //Hack for ranged layout, only display center
};

//Create Database item
struct s_skill_produce_db {
	unsigned short nameid, //Product ID
		unique_id, //Unique ID
		req_skill, //Required Skill
		req_skill_lv, //Required Skill Level
		itemlv, //Item Level
		mat_id[MAX_PRODUCE_RESOURCE], //Materials needed
		mat_amount[MAX_PRODUCE_RESOURCE]; //Amount of each materials
};
extern struct s_skill_produce_db skill_produce_db[MAX_SKILL_PRODUCE_DB];

//Creating database arrow
struct s_skill_arrow_db {
	unsigned short nameid, //Material ID
		cre_id[MAX_ARROW_RESULT], //Arrow created
		cre_amount[MAX_ARROW_RESULT]; //Amount of each arrow created
};
extern struct s_skill_arrow_db skill_arrow_db[MAX_SKILL_ARROW_DB];

//Abracadabra database
struct s_skill_abra_db {
	uint16 skill_id; //Skill ID
	char name[SKILL_NAME_LENGTH]; //Shouted skill name
	int per[MAX_SKILL_LEVEL]; //Probability summoned
};
extern struct s_skill_abra_db skill_abra_db[MAX_SKILL_ABRA_DB];

void do_init_skill(void);
void do_final_skill(void);

//Cast type
enum {
	CAST_GROUND,
	CAST_DAMAGE,
	CAST_NODAMAGE
};
//Returns the cast type of the skill: ground cast, castend damage, castend no damage
int skill_get_casttype(uint16 skill_id); //[Skotlex]
const char *skill_get_name(uint16 skill_id); //[Skotlex]
const char *skill_get_desc(uint16 skill_id); //[Skotlex]
int skill_tree_get_max(uint16 skill_id, int b_class); //Celest

//Accessor to the skills database
int skill_get_index(uint16 skill_id);
int skill_get_type(uint16 skill_id);
int skill_get_hit(uint16 skill_id);
int skill_get_inf(uint16 skill_id);
int skill_get_ele(uint16 skill_id, uint16 skill_lv);
int skill_get_nk(uint16 skill_id);
int skill_get_max(uint16 skill_id);
int skill_get_range(uint16 skill_id, uint16 skill_lv);
int skill_get_range2(struct block_list *bl, uint16 skill_id, uint16 skill_lv, bool isServer);
int skill_get_splash(uint16 skill_id, uint16 skill_lv);
int skill_get_num(uint16 skill_id, uint16 skill_lv);
int skill_get_cast(uint16 skill_id, uint16 skill_lv);
int skill_get_delay(uint16 skill_id, uint16 skill_lv);
int skill_get_walkdelay(uint16 skill_id, uint16 skill_lv);
int skill_get_time(uint16 skill_id, uint16 skill_lv);
int skill_get_time2(uint16 skill_id, uint16 skill_lv);
int skill_get_castnodex(uint16 skill_id, uint16 skill_lv);
int skill_get_castdef(uint16 skill_id);
int skill_get_nocast(uint16 skill_id);
int skill_get_unit_id(uint16 skill_id, int flag);
int skill_get_unit_range(uint16 skill_id, uint16 skill_lv);
int skill_get_inf2(uint16 skill_id);
int skill_get_castcancel(uint16 skill_id);
int skill_get_maxcount(uint16 skill_id, uint16 skill_lv);
int skill_get_blewcount(uint16 skill_id, uint16 skill_lv);
int skill_get_unit_flag(uint16 skill_id);
int skill_get_unit_target(uint16 skill_id);
int skill_get_inf3(uint16 skill_id);

//Accessor for skill requirements
int skill_get_hp(uint16 skill_id, uint16 skill_lv);
int skill_get_mhp(uint16 skill_id, uint16 skill_lv);
int skill_get_sp(uint16 skill_id, uint16 skill_lv);
int skill_get_hp_rate(uint16 skill_id, uint16 skill_lv);
int skill_get_sp_rate(uint16 skill_id, uint16 skill_lv);
int skill_get_zeny(uint16 skill_id, uint16 skill_lv);
int skill_get_weapontype(uint16 skill_id);
int skill_get_ammotype(uint16 skill_id);
int skill_get_ammo_qty(uint16 skill_id, uint16 skill_lv);
int skill_get_state(uint16 skill_id);
//int skill_get_status(uint16 skill_id, int idx);
int skill_get_status_count(uint16 skill_id);
int skill_get_spiritball(uint16 skill_id, uint16 skill_lv);
int skill_get_itemid(uint16 skill_id, int idx);
int skill_get_itemqty(uint16 skill_id, int idx);
unsigned short skill_dummy2skill_id(unsigned short skill_id);

int skill_name2id(const char *name);

int skill_isammotype(struct map_session_data *sd, uint16 skill_id);
TIMER_FUNC(skill_castend_id);
TIMER_FUNC(skill_castend_pos);
int skill_castend_map(struct map_session_data *sd,uint16 skill_id, const char *map);

int skill_cleartimerskill(struct block_list *src);
int skill_addtimerskill(struct block_list *src,unsigned int tick,int target,int x,int y,uint16 skill_id,uint16 skill_lv,int type,int flag);

//Results Added
int skill_additional_effect(struct block_list *src, struct block_list *bl,uint16 skill_id,uint16 skill_lv,int attack_type,int dmg_lv,unsigned int tick);
int skill_counter_additional_effect(struct block_list *src, struct block_list *bl,uint16 skill_id,uint16 skill_lv,int attack_type,unsigned int tick);
short skill_blown(struct block_list *src, struct block_list *target, char count, int8 dir, unsigned char flag);
int skill_break_equip(struct block_list *src, struct block_list *bl, unsigned short pos, int rate, int flag);
bool skill_strip_equip(struct block_list *src, struct block_list *bl, uint16 skill_id, uint16 skill_lv);
//Skills unit
struct skill_unit_group *skill_id2group(int group_id);
struct skill_unit_group *skill_unitsetting(struct block_list *src, uint16 skill_id, uint16 skill_lv, short x, short y, int flag);
struct skill_unit *skill_initunit(struct skill_unit_group *group, int idx, int x, int y, int val1, int val2, int val3, int val4, bool hidden);
int skill_delunit(struct skill_unit *unit);
struct skill_unit_group *skill_initunitgroup(struct block_list *src, int count, uint16 skill_id, uint16 skill_lv, int unit_id, int limit, int interval);
int skill_delunitgroup_(struct skill_unit_group *group, const char *file, int line, const char *func);
#define skill_delunitgroup(group) skill_delunitgroup_(group,__FILE__,__LINE__,__FUNCTION__)
void skill_clear_unitgroup(struct block_list *src);
int skill_clear_group(struct block_list *bl, int flag);
void ext_skill_unit_onplace(struct skill_unit *unit, struct block_list *bl, unsigned int tick);
int skill_unit_ondamaged(struct skill_unit *unit, int64 damage);
//Skill unit visibility [Cydh]
void skill_getareachar_skillunit_visibilty(struct skill_unit *su, enum send_target target);
void skill_getareachar_skillunit_visibilty_single(struct skill_unit *su, struct block_list *bl);

int skill_castfix(struct block_list *bl, uint16 skill_id, uint16 skill_lv);
int skill_castfix_sc(struct block_list *bl, double time, uint16 skill_id, uint16 skill_lv);
#ifdef RENEWAL_CAST
int skill_vfcastfix(struct block_list *bl, double time, uint16 skill_id, uint16 skill_lv);
#endif
int skill_delayfix(struct block_list *bl, uint16 skill_id, uint16 skill_lv);

//Skill conditions check and remove [Inkfish]
bool skill_check_condition_castbegin(struct map_session_data *sd, uint16 skill_id, uint16 skill_lv);
bool skill_check_condition_castend(struct map_session_data *sd, uint16 skill_id, uint16 skill_lv);
int skill_check_condition_char_sub(struct block_list *bl, va_list ap);
void skill_consume_requirement(struct map_session_data *sd, uint16 skill_id, uint16 skill_lv, short type);
struct skill_condition skill_get_requirement(struct map_session_data *sd, uint16 skill_id, uint16 skill_lv);
int skill_disable_check(struct status_change *sc, uint16 skill_id);

int skill_check_pc_partner(struct map_session_data *sd, uint16 skill_id, uint16 *skill_lv, int range, int cast_flag);
//Added skill_check_unit_cell
int skill_check_unit_cell(uint16 skill_id, int16 m, int16 x, int16 y, int unit_id);
int skill_unit_out_all(struct block_list *bl, unsigned int tick, int range);
int skill_unit_move(struct block_list *bl, unsigned int tick, int flag);
void skill_unit_move_unit_group(struct skill_unit_group *group, int16 m, int16 dx, int16 dy);

struct skill_unit_group *skill_check_dancing(struct block_list *src);

//Cast canceled
int skill_castcancel(struct block_list *bl,int type);

int skill_sit(struct map_session_data *sd, int type);
void skill_repairweapon(struct map_session_data *sd, int idx);
void skill_identify(struct map_session_data *sd,int idx);
void skill_weaponrefine(struct map_session_data *sd,int idx); //[Celest]
int skill_autospell(struct map_session_data *md,uint16 skill_id);

int skill_calc_heal(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, bool heal);

bool skill_check_cloaking(struct block_list *bl, struct status_change_entry *sce);
bool skill_can_cloak(struct map_session_data *sd);
int skill_check_cloaking_end(struct block_list *bl, va_list ap);

//Abnormal status
void skill_enchant_elemental_end(struct block_list *bl, int type);
bool skill_isNotOk(uint16 skill_id, struct map_session_data *sd);
bool skill_isNotOk_hom(uint16 skill_id, struct homun_data *hd);
bool skill_isNotOk_mercenary(uint16 skill_id, struct mercenary_data *md);

bool skill_isNotOk_npcRange(struct block_list *src, uint16 skill_id, uint16 skill_lv, int pos_x, int pos_y);

//Item creation
short skill_can_produce_mix(struct map_session_data *sd, unsigned short nameid, unsigned short unique_id, int trigger, int qty);
bool skill_produce_mix(struct map_session_data *sd, uint16 skill_id, unsigned short nameid, unsigned short unique_id, int slot1, int slot2, int slot3, int qty);

bool skill_arrow_create(struct map_session_data *sd, unsigned short nameid);

//Skills for the mob
int skill_castend_nodamage_id(struct block_list *src, struct block_list *bl, uint16 skill_id, uint16 skill_lv, unsigned int tick, int flag);
int skill_castend_damage_id(struct block_list *src, struct block_list *bl, uint16 skill_id, uint16 skill_lv, unsigned int tick, int flag);
int skill_castend_pos2(struct block_list *src, int x, int y, uint16 skill_id, uint16 skill_lv, unsigned int tick, int flag);

int skill_blockpc_start(struct map_session_data *sd, uint16 skill_id, int tick);
int skill_blockpc_get(struct map_session_data *sd, uint16 skill_id);
int skill_blockpc_clear(struct map_session_data *sd);
TIMER_FUNC(skill_blockpc_end);
int skill_blockhomun_start(struct homun_data*, uint16 skill_id, int tick);
int skill_blockmerc_start(struct mercenary_data*, uint16 skill_id, int tick);

//(Epoque:) To-do: replace this macro with some sort of skill tree check (rather than hard-coded skill names)
#define skill_ischangesex(id) ( \
	((id) >= BD_ADAPTATION     && (id) <= DC_SERVICEFORYOU) || ((id) >= CG_ARROWVULCAN && (id) <= CG_MARIONETTE) || \
	((id) >= CG_LONGINGFREEDOM && (id) <= CG_TAROTCARD)     || ((id) >= WA_SWING_DANCE && (id) <= WM_UNLIMITED_HUMMING_VOICE))

//Skill action, (return dmg,heal)
int skill_attack(int attack_type, struct block_list *src, struct block_list *dsrc, struct block_list *bl, uint16 skill_id, uint16 skill_lv, unsigned int tick, int flag);

void skill_reload(void);

//List of State Requirements
enum e_require_state {
	ST_NONE,
	ST_HIDDEN,
	ST_RIDING,
	ST_FALCON,
	ST_CART,
	ST_SHIELD,
	ST_RECOV_WEIGHT_RATE,
	ST_MOVE_ENABLE,
	ST_WATER,
	ST_RIDINGDRAGON,
	ST_WUG,
	ST_RIDINGWUG,
	ST_MADO,
	ST_ELEMENTALSPIRIT,
	ST_PECO,
};

//List of Skills
enum e_skill {
	NV_BASIC = 1,

	SM_SWORD,
	SM_TWOHAND,
	SM_RECOVERY,
	SM_BASH,
	SM_PROVOKE,
	SM_MAGNUM,
	SM_ENDURE,

	MG_SRECOVERY,
	MG_SIGHT,
	MG_NAPALMBEAT,
	MG_SAFETYWALL,
	MG_SOULSTRIKE,
	MG_COLDBOLT,
	MG_FROSTDIVER,
	MG_STONECURSE,
	MG_FIREBALL,
	MG_FIREWALL,
	MG_FIREBOLT,
	MG_LIGHTNINGBOLT,
	MG_THUNDERSTORM,

	AL_DP,
	AL_DEMONBANE,
	AL_RUWACH,
	AL_PNEUMA,
	AL_TELEPORT,
	AL_WARP,
	AL_HEAL,
	AL_INCAGI,
	AL_DECAGI,
	AL_HOLYWATER,
	AL_CRUCIS,
	AL_ANGELUS,
	AL_BLESSING,
	AL_CURE,

	MC_INCCARRY,
	MC_DISCOUNT,
	MC_OVERCHARGE,
	MC_PUSHCART,
	MC_IDENTIFY,
	MC_VENDING,
	MC_MAMMONITE,

	AC_OWL,
	AC_VULTURE,
	AC_CONCENTRATION,
	AC_DOUBLE,
	AC_SHOWER,

	TF_DOUBLE,
	TF_MISS,
	TF_STEAL,
	TF_HIDING,
	TF_POISON,
	TF_DETOXIFY,

	ALL_RESURRECTION,

	KN_SPEARMASTERY,
	KN_PIERCE,
	KN_BRANDISHSPEAR,
	KN_SPEARSTAB,
	KN_SPEARBOOMERANG,
	KN_TWOHANDQUICKEN,
	KN_AUTOCOUNTER,
	KN_BOWLINGBASH,
	KN_RIDING,
	KN_CAVALIERMASTERY,

	PR_MACEMASTERY,
	PR_IMPOSITIO,
	PR_SUFFRAGIUM,
	PR_ASPERSIO,
	PR_BENEDICTIO,
	PR_SANCTUARY,
	PR_SLOWPOISON,
	PR_STRECOVERY,
	PR_KYRIE,
	PR_MAGNIFICAT,
	PR_GLORIA,
	PR_LEXDIVINA,
	PR_TURNUNDEAD,
	PR_LEXAETERNA,
	PR_MAGNUS,

	WZ_FIREPILLAR,
	WZ_SIGHTRASHER,
	WZ_FIREIVY,
	WZ_METEOR,
	WZ_JUPITEL,
	WZ_VERMILION,
	WZ_WATERBALL,
	WZ_ICEWALL,
	WZ_FROSTNOVA,
	WZ_STORMGUST,
	WZ_EARTHSPIKE,
	WZ_HEAVENDRIVE,
	WZ_QUAGMIRE,
	WZ_ESTIMATION,

	BS_IRON,
	BS_STEEL,
	BS_ENCHANTEDSTONE,
	BS_ORIDEOCON,
	BS_DAGGER,
	BS_SWORD,
	BS_TWOHANDSWORD,
	BS_AXE,
	BS_MACE,
	BS_KNUCKLE,
	BS_SPEAR,
	BS_HILTBINDING,
	BS_FINDINGORE,
	BS_WEAPONRESEARCH,
	BS_REPAIRWEAPON,
	BS_SKINTEMPER,
	BS_HAMMERFALL,
	BS_ADRENALINE,
	BS_WEAPONPERFECT,
	BS_OVERTHRUST,
	BS_MAXIMIZE,

	HT_SKIDTRAP,
	HT_LANDMINE,
	HT_ANKLESNARE,
	HT_SHOCKWAVE,
	HT_SANDMAN,
	HT_FLASHER,
	HT_FREEZINGTRAP,
	HT_BLASTMINE,
	HT_CLAYMORETRAP,
	HT_REMOVETRAP,
	HT_TALKIEBOX,
	HT_BEASTBANE,
	HT_FALCON,
	HT_STEELCROW,
	HT_BLITZBEAT,
	HT_DETECTING,
	HT_SPRINGTRAP,

	AS_RIGHT,
	AS_LEFT,
	AS_KATAR,
	AS_CLOAKING,
	AS_SONICBLOW,
	AS_GRIMTOOTH,
	AS_ENCHANTPOISON,
	AS_POISONREACT,
	AS_VENOMDUST,
	AS_SPLASHER,

	NV_FIRSTAID,
	NV_TRICKDEAD,
	SM_MOVINGRECOVERY,
	SM_FATALBLOW,
	SM_AUTOBERSERK,
	AC_MAKINGARROW,
	AC_CHARGEARROW,
	TF_SPRINKLESAND,
	TF_BACKSLIDING,
	TF_PICKSTONE,
	TF_THROWSTONE,
	MC_CARTREVOLUTION,
	MC_CHANGECART,
	MC_LOUD,
	AL_HOLYLIGHT,
	MG_ENERGYCOAT,

	NPC_PIERCINGATT,
	NPC_MENTALBREAKER,
	NPC_RANGEATTACK,
	NPC_ATTRICHANGE,
	NPC_CHANGEWATER,
	NPC_CHANGEGROUND,
	NPC_CHANGEFIRE,
	NPC_CHANGEWIND,
	NPC_CHANGEPOISON,
	NPC_CHANGEHOLY,
	NPC_CHANGEDARKNESS,
	NPC_CHANGETELEKINESIS,
	NPC_CRITICALSLASH,
	NPC_COMBOATTACK,
	NPC_GUIDEDATTACK,
	NPC_SELFDESTRUCTION,
	NPC_SPLASHATTACK,
	NPC_SUICIDE,
	NPC_POISON,
	NPC_BLINDATTACK,
	NPC_SILENCEATTACK,
	NPC_STUNATTACK,
	NPC_PETRIFYATTACK,
	NPC_CURSEATTACK,
	NPC_SLEEPATTACK,
	NPC_RANDOMATTACK,
	NPC_WATERATTACK,
	NPC_GROUNDATTACK,
	NPC_FIREATTACK,
	NPC_WINDATTACK,
	NPC_POISONATTACK,
	NPC_HOLYATTACK,
	NPC_DARKNESSATTACK,
	NPC_TELEKINESISATTACK,
	NPC_MAGICALATTACK,
	NPC_METAMORPHOSIS,
	NPC_PROVOCATION,
	NPC_SMOKING,
	NPC_SUMMONSLAVE,
	NPC_EMOTION,
	NPC_TRANSFORMATION,
	NPC_BLOODDRAIN,
	NPC_ENERGYDRAIN,
	NPC_KEEPING,
	NPC_DARKBREATH,
	NPC_DARKBLESSING,
	NPC_BARRIER,
	NPC_DEFENDER,
	NPC_LICK,
	NPC_HALLUCINATION,
	NPC_REBIRTH,
	NPC_SUMMONMONSTER,

	RG_SNATCHER,
	RG_STEALCOIN,
	RG_BACKSTAP,
	RG_TUNNELDRIVE,
	RG_RAID,
	RG_STRIPWEAPON,
	RG_STRIPSHIELD,
	RG_STRIPARMOR,
	RG_STRIPHELM,
	RG_INTIMIDATE,
	RG_GRAFFITI,
	RG_FLAGGRAFFITI,
	RG_CLEANER,
	RG_GANGSTER,
	RG_COMPULSION,
	RG_PLAGIARISM,

	AM_AXEMASTERY,
	AM_LEARNINGPOTION,
	AM_PHARMACY,
	AM_DEMONSTRATION,
	AM_ACIDTERROR,
	AM_POTIONPITCHER,
	AM_CANNIBALIZE,
	AM_SPHEREMINE,
	AM_CP_WEAPON,
	AM_CP_SHIELD,
	AM_CP_ARMOR,
	AM_CP_HELM,
	AM_BIOETHICS,
	AM_BIOTECHNOLOGY,
	AM_CREATECREATURE,
	AM_CULTIVATION,
	AM_FLAMECONTROL,
	AM_CALLHOMUN,
	AM_REST,
	AM_DRILLMASTER,
	AM_HEALHOMUN,
	AM_RESURRECTHOMUN,

	CR_TRUST,
	CR_AUTOGUARD,
	CR_SHIELDCHARGE,
	CR_SHIELDBOOMERANG,
	CR_REFLECTSHIELD,
	CR_HOLYCROSS,
	CR_GRANDCROSS,
	CR_DEVOTION,
	CR_PROVIDENCE,
	CR_DEFENDER,
	CR_SPEARQUICKEN,

	MO_IRONHAND,
	MO_SPIRITSRECOVERY,
	MO_CALLSPIRITS,
	MO_ABSORBSPIRITS,
	MO_TRIPLEATTACK,
	MO_BODYRELOCATION,
	MO_DODGE,
	MO_INVESTIGATE,
	MO_FINGEROFFENSIVE,
	MO_STEELBODY,
	MO_BLADESTOP,
	MO_EXPLOSIONSPIRITS,
	MO_EXTREMITYFIST,
	MO_CHAINCOMBO,
	MO_COMBOFINISH,

	SA_ADVANCEDBOOK,
	SA_CASTCANCEL,
	SA_MAGICROD,
	SA_SPELLBREAKER,
	SA_FREECAST,
	SA_AUTOSPELL,
	SA_FLAMELAUNCHER,
	SA_FROSTWEAPON,
	SA_LIGHTNINGLOADER,
	SA_SEISMICWEAPON,
	SA_DRAGONOLOGY,
	SA_VOLCANO,
	SA_DELUGE,
	SA_VIOLENTGALE,
	SA_LANDPROTECTOR,
	SA_DISPELL,
	SA_ABRACADABRA,
	SA_MONOCELL,
	SA_CLASSCHANGE,
	SA_SUMMONMONSTER,
	SA_REVERSEORCISH,
	SA_DEATH,
	SA_FORTUNE,
	SA_TAMINGMONSTER,
	SA_QUESTION,
	SA_GRAVITY,
	SA_LEVELUP,
	SA_INSTANTDEATH,
	SA_FULLRECOVERY,
	SA_COMA,

	BD_ADAPTATION,
	BD_ENCORE,
	BD_LULLABY,
	BD_RICHMANKIM,
	BD_ETERNALCHAOS,
	BD_DRUMBATTLEFIELD,
	BD_RINGNIBELUNGEN,
	BD_ROKISWEIL,
	BD_INTOABYSS,
	BD_SIEGFRIED,
	BD_RAGNAROK,

	BA_MUSICALLESSON,
	BA_MUSICALSTRIKE,
	BA_DISSONANCE,
	BA_FROSTJOKER,
	BA_WHISTLE,
	BA_ASSASSINCROSS,
	BA_POEMBRAGI,
	BA_APPLEIDUN,

	DC_DANCINGLESSON,
	DC_THROWARROW,
	DC_UGLYDANCE,
	DC_SCREAM,
	DC_HUMMING,
	DC_DONTFORGETME,
	DC_FORTUNEKISS,
	DC_SERVICEFORYOU,

	NPC_RANDOMMOVE,
	NPC_SPEEDUP,
	NPC_REVENGE,

	WE_MALE,
	WE_FEMALE,
	WE_CALLPARTNER,

	ITM_TOMAHAWK,

	NPC_DARKCROSS,
	NPC_GRANDDARKNESS,
	NPC_DARKSTRIKE,
	NPC_DARKTHUNDER,
	NPC_STOP,
	NPC_WEAPONBRAKER,
	NPC_ARMORBRAKE,
	NPC_HELMBRAKE,
	NPC_SHIELDBRAKE,
	NPC_UNDEADATTACK,
	NPC_CHANGEUNDEAD,
	NPC_POWERUP,
	NPC_AGIUP,
	NPC_SIEGEMODE,
	NPC_CALLSLAVE,
	NPC_INVISIBLE,
	NPC_RUN,

	LK_AURABLADE,
	LK_PARRYING,
	LK_CONCENTRATION,
	LK_TENSIONRELAX,
	LK_BERSERK,
	LK_FURY,
	HP_ASSUMPTIO,
	HP_BASILICA,
	HP_MEDITATIO,
	HW_SOULDRAIN,
	HW_MAGICCRASHER,
	HW_MAGICPOWER,
	PA_PRESSURE,
	PA_SACRIFICE,
	PA_GOSPEL,
	CH_PALMSTRIKE,
	CH_TIGERFIST,
	CH_CHAINCRUSH,
	PF_HPCONVERSION,
	PF_SOULCHANGE,
	PF_SOULBURN,
	ASC_KATAR,
	ASC_HALLUCINATION,
	ASC_EDP,
	ASC_BREAKER,
	SN_SIGHT,
	SN_FALCONASSAULT,
	SN_SHARPSHOOTING,
	SN_WINDWALK,
	WS_MELTDOWN,
	WS_CREATECOIN,
	WS_CREATENUGGET,
	WS_CARTBOOST,
	WS_SYSTEMCREATE,
	ST_CHASEWALK,
	ST_REJECTSWORD,
	ST_STEALBACKPACK,
	CR_ALCHEMY,
	CR_SYNTHESISPOTION,
	CG_ARROWVULCAN,
	CG_MOONLIT,
	CG_MARIONETTE,
	LK_SPIRALPIERCE,
	LK_HEADCRUSH,
	LK_JOINTBEAT,
	HW_NAPALMVULCAN,
	CH_SOULCOLLECT,
	PF_MINDBREAKER,
	PF_MEMORIZE,
	PF_FOGWALL,
	PF_SPIDERWEB,
	ASC_METEORASSAULT,
	ASC_CDP,
	WE_BABY,
	WE_CALLPARENT,
	WE_CALLBABY,

	TK_RUN,
	TK_READYSTORM,
	TK_STORMKICK,
	TK_READYDOWN,
	TK_DOWNKICK,
	TK_READYTURN,
	TK_TURNKICK,
	TK_READYCOUNTER,
	TK_COUNTER,
	TK_DODGE,
	TK_JUMPKICK,
	TK_HPTIME,
	TK_SPTIME,
	TK_POWER,
	TK_SEVENWIND,
	TK_HIGHJUMP,

	SG_FEEL,
	SG_SUN_WARM,
	SG_MOON_WARM,
	SG_STAR_WARM,
	SG_SUN_COMFORT,
	SG_MOON_COMFORT,
	SG_STAR_COMFORT,
	SG_HATE,
	SG_SUN_ANGER,
	SG_MOON_ANGER,
	SG_STAR_ANGER,
	SG_SUN_BLESS,
	SG_MOON_BLESS,
	SG_STAR_BLESS,
	SG_DEVIL,
	SG_FRIEND,
	SG_KNOWLEDGE,
	SG_FUSION,

	SL_ALCHEMIST,
	AM_BERSERKPITCHER,
	SL_MONK,
	SL_STAR,
	SL_SAGE,
	SL_CRUSADER,
	SL_SUPERNOVICE,
	SL_KNIGHT,
	SL_WIZARD,
	SL_PRIEST,
	SL_BARDDANCER,
	SL_ROGUE,
	SL_ASSASIN,
	SL_BLACKSMITH,
	BS_ADRENALINE2,
	SL_HUNTER,
	SL_SOULLINKER,
	SL_KAIZEL,
	SL_KAAHI,
	SL_KAUPE,
	SL_KAITE,
	SL_KAINA,
	SL_STIN,
	SL_STUN,
	SL_SMA,
	SL_SWOO,
	SL_SKE,
	SL_SKA,

	SM_SELFPROVOKE,
	NPC_EMOTION_ON,
	ST_PRESERVE,
	ST_FULLSTRIP,
	WS_WEAPONREFINE,
	CR_SLIMPITCHER,
	CR_FULLPROTECTION,
	PA_SHIELDCHAIN,
	HP_MANARECHARGE,
	PF_DOUBLECASTING,
	HW_GANBANTEIN,
	HW_GRAVITATION,
	WS_CARTTERMINATION,
	WS_OVERTHRUSTMAX,
	CG_LONGINGFREEDOM,
	CG_HERMODE,
	CG_TAROTCARD,
	CR_ACIDDEMONSTRATION,
	CR_CULTIVATION,
	ITEM_ENCHANTARMS,
	TK_MISSION,
	SL_HIGH,
	KN_ONEHAND,
	AM_TWILIGHT1,
	AM_TWILIGHT2,
	AM_TWILIGHT3,
	HT_POWER,

	GS_GLITTERING,
	GS_FLING,
	GS_TRIPLEACTION,
	GS_BULLSEYE,
	GS_MADNESSCANCEL,
	GS_ADJUSTMENT,
	GS_INCREASING,
	GS_MAGICALBULLET,
	GS_CRACKER,
	GS_SINGLEACTION,
	GS_SNAKEEYE,
	GS_CHAINACTION,
	GS_TRACKING,
	GS_DISARM,
	GS_PIERCINGSHOT,
	GS_RAPIDSHOWER,
	GS_DESPERADO,
	GS_GATLINGFEVER,
	GS_DUST,
	GS_FULLBUSTER,
	GS_SPREADATTACK,
	GS_GROUNDDRIFT,

	NJ_TOBIDOUGU,
	NJ_SYURIKEN,
	NJ_KUNAI,
	NJ_HUUMA,
	NJ_ZENYNAGE,
	NJ_TATAMIGAESHI,
	NJ_KASUMIKIRI,
	NJ_SHADOWJUMP,
	NJ_KIRIKAGE,
	NJ_UTSUSEMI,
	NJ_BUNSINJYUTSU,
	NJ_NINPOU,
	NJ_KOUENKA,
	NJ_KAENSIN,
	NJ_BAKUENRYU,
	NJ_HYOUSENSOU,
	NJ_SUITON,
	NJ_HYOUSYOURAKU,
	NJ_HUUJIN,
	NJ_RAIGEKISAI,
	NJ_KAMAITACHI,
	NJ_NEN,
	NJ_ISSEN,

	MB_FIGHTING,
	MB_NEUTRAL,
	MB_TAIMING_PUTI,
	MB_WHITEPOTION,
	MB_MENTAL,
	MB_CARDPITCHER,
	MB_PETPITCHER,
	MB_BODYSTUDY,
	MB_BODYALTER,
	MB_PETMEMORY,
	MB_M_TELEPORT,
	MB_B_GAIN,
	MB_M_GAIN,
	MB_MISSION,
	MB_MUNAKKNOWLEDGE,
	MB_MUNAKBALL,
	MB_SCROLL,
	MB_B_GATHERING,
	MB_M_GATHERING,
	MB_B_EXCLUDE,
	MB_B_DRIFT,
	MB_B_WALLRUSH,
	MB_M_WALLRUSH,
	MB_B_WALLSHIFT,
	MB_M_WALLCRASH,
	MB_M_REINCARNATION,
	MB_B_EQUIP,

	SL_DEATHKNIGHT,
	SL_COLLECTOR,
	SL_NINJA,
	SL_GUNNER,
	AM_TWILIGHT4,
	DA_RESET,
	DE_BERSERKAIZER,
	DA_DARKPOWER,

	DE_PASSIVE,
	DE_PATTACK,
	DE_PSPEED,
	DE_PDEFENSE,
	DE_PCRITICAL,
	DE_PHP,
	DE_PSP,
	DE_RESET,
	DE_RANKING,
	DE_PTRIPLE,
	DE_ENERGY,
	DE_NIGHTMARE,
	DE_SLASH,
	DE_COIL,
	DE_WAVE,
	DE_REBIRTH,
	DE_AURA,
	DE_FREEZER,
	DE_CHANGEATTACK,
	DE_PUNISH,
	DE_POISON,
	DE_INSTANT,
	DE_WARNING,
	DE_RANKEDKNIFE,
	DE_RANKEDGRADIUS,
	DE_GAUGE,
	DE_GTIME,
	DE_GPAIN,
	DE_GSKILL,
	DE_GKILL,
	DE_ACCEL,
	DE_BLOCKDOUBLE,
	DE_BLOCKMELEE,
	DE_BLOCKFAR,
	DE_FRONTATTACK,
	DE_DANGERATTACK,
	DE_TWINATTACK,
	DE_WINDATTACK,
	DE_WATERATTACK,

	DA_ENERGY,
	DA_CLOUD,
	DA_FIRSTSLOT,
	DA_HEADDEF,
	DA_SPACE,
	DA_TRANSFORM,
	DA_EXPLOSION,
	DA_REWARD,
	DA_CRUSH,
	DA_ITEMREBUILD,
	DA_ILLUSION,
	DA_NUETRALIZE,
	DA_RUNNER,
	DA_TRANSFER,
	DA_WALL,
	DA_ZENY,
	DA_REVENGE,
	DA_EARPLUG,
	DA_CONTRACT,
	DA_BLACK,
	DA_DREAM,
	DA_MAGICCART,
	DA_COPY,
	DA_CRYSTAL,
	DA_EXP,
	DA_CARTSWING,
	DA_REBUILD,
	DA_JOBCHANGE,
	DA_EDARKNESS,
	DA_EGUARDIAN,
	DA_TIMEOUT,
	ALL_TIMEIN,
	DA_ZENYRANK,
	DA_ACCESSORYMIX,

	NPC_EARTHQUAKE,
	NPC_FIREBREATH,
	NPC_ICEBREATH,
	NPC_THUNDERBREATH,
	NPC_ACIDBREATH,
	NPC_DARKNESSBREATH,
	NPC_DRAGONFEAR,
	NPC_BLEEDING,
	NPC_PULSESTRIKE,
	NPC_HELLJUDGEMENT,
	NPC_WIDESILENCE,
	NPC_WIDEFREEZE,
	NPC_WIDEBLEEDING,
	NPC_WIDESTONE,
	NPC_WIDECONFUSE,
	NPC_WIDESLEEP,
	NPC_WIDESIGHT,
	NPC_EVILLAND,
	NPC_MAGICMIRROR,
	NPC_SLOWCAST,
	NPC_CRITICALWOUND,
	NPC_EXPULSION,
	NPC_STONESKIN,
	NPC_ANTIMAGIC,
	NPC_WIDECURSE,
	NPC_WIDESTUN,
	NPC_VAMPIRE_GIFT,
	NPC_WIDESOULDRAIN,

	ALL_INCCARRY,
	NPC_TALK,
	NPC_HELLPOWER,
	NPC_WIDEHELLDIGNITY,
	NPC_INVINCIBLE,
	NPC_INVINCIBLEOFF,
	NPC_ALLHEAL,
	GM_SANDMAN,
	CASH_BLESSING,
	CASH_INCAGI,
	CASH_ASSUMPTIO,
	ALL_CATCRY,
	ALL_PARTYFLEE,
	ALL_ANGEL_PROTECT,
	ALL_DREAM_SUMMERNIGHT,
	NPC_CHANGEUNDEAD2,
	ALL_REVERSEORCISH,
	ALL_WEWISH,
	ALL_SONKRAN,
	NPC_WIDEHEALTHFEAR,
	NPC_WIDEBODYBURNNING,
	NPC_WIDEFROSTMISTY,
	NPC_WIDECOLD,
	NPC_WIDE_DEEP_SLEEP,
	NPC_WIDESIREN,
	NPC_VENOMFOG,
	NPC_MILLENNIUMSHIELD,
	NPC_COMET,
	NPC_ICEMINE,
	NPC_ICEEXPLO,
	NPC_FLAMECROSS,
	NPC_PULSESTRIKE2,
	NPC_DANCINGBLADE,
	NPC_DANCINGBLADE_ATK,
	NPC_DARKPIERCING,
	NPC_MAXPAIN,
	NPC_MAXPAIN_ATK,
	NPC_DEATHSUMMON,
	NPC_HELLBURNING,
	NPC_JACKFROST,
	NPC_WIDEWEB,
	NPC_WIDESUCK,
	NPC_STORMGUST2,
	NPC_FIRESTORM,
	NPC_REVERBERATION,
	NPC_REVERBERATION_ATK,
	NPC_LEX_AETERNA,
	NPC_ARROWSTORM,
	NPC_CHEAL,
	NPC_SR_CURSEDCIRCLE,
	NPC_DRAGONBREATH,
	NPC_FATALMENACE,
	NPC_MAGMA_ERUPTION,
	NPC_MAGMA_ERUPTION_DOTDAMAGE,
	NPC_MANDRAGORA,
	NPC_PSYCHIC_WAVE,
	NPC_RAYOFGENESIS,
	NPC_VENOMIMPRESS,
	NPC_CLOUD_KILL,
	NPC_IGNITIONBREAK,
	NPC_PHANTOMTHRUST,
	NPC_POISON_BUSTER,
	NPC_HALLUCINATIONWALK,
	NPC_ELECTRICWALK,
	NPC_FIREWALK,
	NPC_WIDEDISPEL,
	NPC_LEASH,
	NPC_WIDELEASH,
	NPC_WIDECRITICALWOUND,

	KN_CHARGEATK = 1001,
	CR_SHRINK,
	AS_SONICACCEL,
	AS_VENOMKNIFE,
	RG_CLOSECONFINE,
	WZ_SIGHTBLASTER,
	SA_CREATECON,
	SA_ELEMENTWATER,
	HT_PHANTASMIC,
	BA_PANGVOICE,
	DC_WINKCHARM,
	BS_UNFAIRLYTRICK,
	BS_GREED,
	PR_REDEMPTIO,
	MO_KITRANSLATION,
	MO_BALKYOUNG,
	SA_ELEMENTGROUND,
	SA_ELEMENTFIRE,
	SA_ELEMENTWIND,

	RK_ENCHANTBLADE = 2001,
	RK_SONICWAVE,
	RK_DEATHBOUND,
	RK_HUNDREDSPEAR,
	RK_WINDCUTTER,
	RK_IGNITIONBREAK,
	RK_DRAGONTRAINING,
	RK_DRAGONBREATH,
	RK_DRAGONHOWLING,
	RK_RUNEMASTERY,
	RK_MILLENNIUMSHIELD,
	RK_CRUSHSTRIKE,
	RK_REFRESH,
	RK_GIANTGROWTH,
	RK_STONEHARDSKIN,
	RK_VITALITYACTIVATION,
	RK_STORMBLAST,
	RK_FIGHTINGSPIRIT,
	RK_ABUNDANCE,
	RK_PHANTOMTHRUST,

	GC_VENOMIMPRESS,
	GC_CROSSIMPACT,
	GC_DARKILLUSION,
	GC_RESEARCHNEWPOISON,
	GC_CREATENEWPOISON,
	GC_ANTIDOTE,
	GC_POISONINGWEAPON,
	GC_WEAPONBLOCKING,
	GC_COUNTERSLASH,
	GC_WEAPONCRUSH,
	GC_VENOMPRESSURE,
	GC_POISONSMOKE,
	GC_CLOAKINGEXCEED,
	GC_PHANTOMMENACE,
	GC_HALLUCINATIONWALK,
	GC_ROLLINGCUTTER,
	GC_CROSSRIPPERSLASHER,

	AB_JUDEX,
	AB_ANCILLA,
	AB_ADORAMUS,
	AB_CLEMENTIA,
	AB_CANTO,
	AB_CHEAL,
	AB_EPICLESIS,
	AB_PRAEFATIO,
	AB_ORATIO,
	AB_LAUDAAGNUS,
	AB_LAUDARAMUS,
	AB_EUCHARISTICA,
	AB_RENOVATIO,
	AB_HIGHNESSHEAL,
	AB_CLEARANCE,
	AB_EXPIATIO,
	AB_DUPLELIGHT,
	AB_DUPLELIGHT_MELEE,
	AB_DUPLELIGHT_MAGIC,
	AB_SILENTIUM,

	WL_WHITEIMPRISON = 2201,
	WL_SOULEXPANSION,
	WL_FROSTMISTY,
	WL_JACKFROST,
	WL_MARSHOFABYSS,
	WL_RECOGNIZEDSPELL,
	WL_SIENNAEXECRATE,
	WL_RADIUS,
	WL_STASIS,
	WL_DRAINLIFE,
	WL_CRIMSONROCK,
	WL_HELLINFERNO,
	WL_COMET,
	WL_CHAINLIGHTNING,
	WL_CHAINLIGHTNING_ATK,
	WL_EARTHSTRAIN,
	WL_TETRAVORTEX,
	WL_TETRAVORTEX_FIRE,
	WL_TETRAVORTEX_WATER,
	WL_TETRAVORTEX_WIND,
	WL_TETRAVORTEX_GROUND,
	WL_SUMMONFB,
	WL_SUMMONBL,
	WL_SUMMONWB,
	WL_SUMMON_ATK_FIRE,
	WL_SUMMON_ATK_WIND,
	WL_SUMMON_ATK_WATER,
	WL_SUMMON_ATK_GROUND,
	WL_SUMMONSTONE,
	WL_RELEASE,
	WL_READING_SB,
	WL_FREEZE_SP,

	RA_ARROWSTORM,
	RA_FEARBREEZE,
	RA_RANGERMAIN,
	RA_AIMEDBOLT,
	RA_DETONATOR,
	RA_ELECTRICSHOCKER,
	RA_CLUSTERBOMB,
	RA_WUGMASTERY,
	RA_WUGRIDER,
	RA_WUGDASH,
	RA_WUGSTRIKE,
	RA_WUGBITE,
	RA_TOOTHOFWUG,
	RA_SENSITIVEKEEN,
	RA_CAMOUFLAGE,
	RA_RESEARCHTRAP,
	RA_MAGENTATRAP,
	RA_COBALTTRAP,
	RA_MAIZETRAP,
	RA_VERDURETRAP,
	RA_FIRINGTRAP,
	RA_ICEBOUNDTRAP,

	NC_MADOLICENCE,
	NC_BOOSTKNUCKLE,
	NC_PILEBUNKER,
	NC_VULCANARM,
	NC_FLAMELAUNCHER,
	NC_COLDSLOWER,
	NC_ARMSCANNON,
	NC_ACCELERATION,
	NC_HOVERING,
	NC_F_SIDESLIDE,
	NC_B_SIDESLIDE,
	NC_MAINFRAME,
	NC_SELFDESTRUCTION,
	NC_SHAPESHIFT,
	NC_EMERGENCYCOOL,
	NC_INFRAREDSCAN,
	NC_ANALYZE,
	NC_MAGNETICFIELD,
	NC_NEUTRALBARRIER,
	NC_STEALTHFIELD,
	NC_REPAIR,
	NC_TRAININGAXE,
	NC_RESEARCHFE,
	NC_AXEBOOMERANG,
	NC_POWERSWING,
	NC_AXETORNADO,
	NC_SILVERSNIPER,
	NC_MAGICDECOY,
	NC_DISJOINT,

	SC_FATALMENACE,
	SC_REPRODUCE,
	SC_AUTOSHADOWSPELL,
	SC_SHADOWFORM,
	SC_TRIANGLESHOT,
	SC_BODYPAINT,
	SC_INVISIBILITY,
	SC_DEADLYINFECT,
	SC_ENERVATION,
	SC_GROOMY,
	SC_IGNORANCE,
	SC_LAZINESS,
	SC_UNLUCKY,
	SC_WEAKNESS,
	SC_STRIPACCESSARY,
	SC_MANHOLE,
	SC_DIMENSIONDOOR,
	SC_CHAOSPANIC,
	SC_MAELSTROM,
	SC_BLOODYLUST,
	SC_FEINTBOMB,

	LG_CANNONSPEAR = 2307,
	LG_BANISHINGPOINT,
	LG_TRAMPLE,
	LG_SHIELDPRESS,
	LG_REFLECTDAMAGE,
	LG_PINPOINTATTACK,
	LG_FORCEOFVANGUARD,
	LG_RAGEBURST,
	LG_SHIELDSPELL,
	LG_EXEEDBREAK,
	LG_OVERBRAND,
	LG_PRESTIGE,
	LG_BANDING,
	LG_MOONSLASHER,
	LG_RAYOFGENESIS,
	LG_PIETY,
	LG_EARTHDRIVE,
	LG_HESPERUSLIT,
	LG_INSPIRATION,

	SR_DRAGONCOMBO,
	SR_SKYNETBLOW,
	SR_EARTHSHAKER,
	SR_FALLENEMPIRE,
	SR_TIGERCANNON,
	SR_HELLGATE,
	SR_RAMPAGEBLASTER,
	SR_CRESCENTELBOW,
	SR_CURSEDCIRCLE,
	SR_LIGHTNINGWALK,
	SR_KNUCKLEARROW,
	SR_WINDMILL,
	SR_RAISINGDRAGON,
	SR_GENTLETOUCH,
	SR_ASSIMILATEPOWER,
	SR_POWERVELOCITY,
	SR_CRESCENTELBOW_AUTOSPELL,
	SR_GATEOFHELL,
	SR_GENTLETOUCH_QUIET,
	SR_GENTLETOUCH_CURE,
	SR_GENTLETOUCH_ENERGYGAIN,
	SR_GENTLETOUCH_CHANGE,
	SR_GENTLETOUCH_REVITALIZE,

	WA_SWING_DANCE = 2350,
	WA_SYMPHONY_OF_LOVER,
	WA_MOONLIT_SERENADE,

	MI_RUSH_WINDMILL = 2381,
	MI_ECHOSONG,
	MI_HARMONIZE,

	WM_LESSON = 2412,
	WM_METALICSOUND,
	WM_REVERBERATION,
	WM_REVERBERATION_MELEE,
	WM_REVERBERATION_MAGIC,
	WM_DOMINION_IMPULSE,
	WM_SEVERE_RAINSTORM,
	WM_POEMOFNETHERWORLD,
	WM_VOICEOFSIREN,
	WM_DEADHILLHERE,
	WM_LULLABY_DEEPSLEEP,
	WM_SIRCLEOFNATURE,
	WM_RANDOMIZESPELL,
	WM_GLOOMYDAY,
	WM_GREAT_ECHO,
	WM_SONG_OF_MANA,
	WM_DANCE_WITH_WUG,
	WM_SOUND_OF_DESTRUCTION,
	WM_SATURDAY_NIGHT_FEVER,
	WM_LERADS_DEW,
	WM_MELODYOFSINK,
	WM_BEYOND_OF_WARCRY,
	WM_UNLIMITED_HUMMING_VOICE,

	SO_FIREWALK = 2443,
	SO_ELECTRICWALK,
	SO_SPELLFIST,
	SO_EARTHGRAVE,
	SO_DIAMONDDUST,
	SO_POISON_BUSTER,
	SO_PSYCHIC_WAVE,
	SO_CLOUD_KILL,
	SO_STRIKING,
	SO_WARMER,
	SO_VACUUM_EXTREME,
	SO_VARETYR_SPEAR,
	SO_ARRULLO,
	SO_EL_CONTROL,
	SO_SUMMON_AGNI,
	SO_SUMMON_AQUA,
	SO_SUMMON_VENTUS,
	SO_SUMMON_TERA,
	SO_EL_ACTION,
	SO_EL_ANALYSIS,
	SO_EL_SYMPATHY,
	SO_EL_CURE,
	SO_FIRE_INSIGNIA,
	SO_WATER_INSIGNIA,
	SO_WIND_INSIGNIA,
	SO_EARTH_INSIGNIA,

	GN_TRAINING_SWORD = 2474,
	GN_REMODELING_CART,
	GN_CART_TORNADO,
	GN_CARTCANNON,
	GN_CARTBOOST,
	GN_THORNS_TRAP,
	GN_BLOOD_SUCKER,
	GN_SPORE_EXPLOSION,
	GN_WALLOFTHORN,
	GN_CRAZYWEED,
	GN_CRAZYWEED_ATK,
	GN_DEMONIC_FIRE,
	GN_FIRE_EXPANSION,
	GN_FIRE_EXPANSION_SMOKE_POWDER,
	GN_FIRE_EXPANSION_TEAR_GAS,
	GN_FIRE_EXPANSION_ACID,
	GN_HELLS_PLANT,
	GN_HELLS_PLANT_ATK,
	GN_MANDRAGORA,
	GN_SLINGITEM,
	GN_CHANGEMATERIAL,
	GN_MIX_COOKING,
	GN_MAKEBOMB,
	GN_S_PHARMACY,
	GN_SLINGITEM_RANGEMELEEATK,

	AB_SECRAMENT = 2515,
	WM_SEVERE_RAINSTORM_MELEE,
	SR_HOWLINGOFLION,
	SR_RIDEINLIGHTNING,
	LG_OVERBRAND_BRANDISH,
	LG_OVERBRAND_PLUSATK,

	ALL_ODINS_RECALL = 2533,
	RETURN_TO_ELDICASTES,
	ALL_BUYING_STORE,
	ALL_GUARDIAN_RECALL,
	ALL_ODINS_POWER,
	BEER_BOTTLE_CAP,
	NPC_ASSASSINCROSS,
	NPC_DISSONANCE,
	NPC_UGLYDANCE,
	ALL_TETANY,
	ALL_RAY_OF_PROTECTION,
	MC_CARTDECORATE,
	GM_ITEM_ATKMAX,
	GM_ITEM_ATKMIN,
	GM_ITEM_MATKMAX,
	GM_ITEM_MATKMIN,
	ALL_LIGHTGUARD,

	RL_GLITTERING_GREED = 2551,
	RL_RICHS_COIN,
	RL_MASS_SPIRAL,
	RL_BANISHING_BUSTER,
	RL_B_TRAP,
	RL_FLICKER,
	RL_S_STORM,
	RL_E_CHAIN,
	RL_QD_SHOT,
	RL_C_MARKER,
	RL_FIREDANCE,
	RL_H_MINE,
	RL_P_ALTER,
	RL_FALLEN_ANGEL,
	RL_R_TRIP,
	RL_D_TAIL,
	RL_FIRE_RAIN,
	RL_HEAT_BARREL,
	RL_AM_BLAST,
	RL_SLUGSHOT,
	RL_HAMMER_OF_GOD,
	RL_R_TRIP_PLUSATK,
	RL_B_FLICKER_ATK,
	RL_GLITTERING_GREED_ATK,

	KO_YAMIKUMO = 3001,
	KO_RIGHT,
	KO_LEFT,
	KO_JYUMONJIKIRI,
	KO_SETSUDAN,
	KO_BAKURETSU,
	KO_HAPPOKUNAI,
	KO_MUCHANAGE,
	KO_HUUMARANKA,
	KO_MAKIBISHI,
	KO_MEIKYOUSISUI,
	KO_ZANZOU,
	KO_KYOUGAKU,
	KO_JYUSATSU,
	KO_KAHU_ENTEN,
	KO_HYOUHU_HUBUKI,
	KO_KAZEHU_SEIRAN,
	KO_DOHU_KOUKAI,
	KO_KAIHOU,
	KO_ZENKAI,
	KO_GENWAKU,
	KO_IZAYOI,
	KG_KAGEHUMI,
	KG_KYOMU,
	KG_KAGEMUSYA,
	OB_ZANGETSU,
	OB_OBOROGENSOU,
	OB_OBOROGENSOU_TRANSITION_ATK,
	OB_AKAITSUKI,

	ECL_SNOWFLIP = 3031,
	ECL_PEONYMAMY,
	ECL_SADAGUI,
	ECL_SEQUOIADUST,
	ECLAGE_RECALL,
	BA_POEMBRAGI2,
	DC_FORTUNEKISS2,
	ITEM_OPTION_SPLASH_ATTACK,
	GM_FORCE_TRANSFER,
	GM_WIDE_RESURRECTION,
	ALL_NIFLHEIM_RECALL,
	ALL_PRONTERA_RECALL,
	ALL_GLASTHEIM_RECALL,
	ALL_THANATOS_RECALL,

	GC_DARKCROW = 5001,
	RA_UNLIMIT,
	GN_ILLUSIONDOPING,
	RK_DRAGONBREATH_WATER,
	RK_LUXANIMA,
	NC_MAGMA_ERUPTION,
	WM_FRIGG_SONG,
	SO_ELEMENTAL_SHIELD,
	SR_FLASHCOMBO,
	SC_ESCAPE,
	AB_OFFERTORIUM,
	WL_TELEKINESIS_INTENSE,
	LG_KINGS_GRACE,
	ALL_FULL_THROTTLE,
	NC_MAGMA_ERUPTION_DOTDAMAGE,

	SU_BASIC_SKILL = 5018,
	SU_BITE,
	SU_HIDE,
	SU_SCRATCH,
	SU_STOOP,
	SU_LOPE,
	SU_SPRITEMABLE,
	SU_POWEROFLAND,
	SU_SV_STEMSPEAR,
	SU_CN_POWDERING,
	SU_CN_METEOR,
	SU_SV_ROOTTWIST,
	SU_SV_ROOTTWIST_ATK,
	SU_POWEROFLIFE,
	SU_SCAROFTAROU,
	SU_PICKYPECK,
	SU_PICKYPECK_DOUBLE_ATK,
	SU_ARCLOUSEDASH,
	SU_LUNATICCARROTBEAT,
	SU_POWEROFSEA,
	SU_TUNABELLY,
	SU_TUNAPARTY,
	SU_BUNCHOFSHRIMP,
	SU_FRESHSHRIMP,
	SU_CN_METEOR2,
	SU_LUNATICCARROTBEAT2,
	SU_SOULATTACK,
	SU_POWEROFFLOCK,
	SU_SVG_SPIRIT,
	SU_HISS,
	SU_NYANGGRASS,
	SU_GROOMING,
	SU_PURRING,
	SU_SHRIMPARTY,
	SU_SPIRITOFLIFE,
	SU_MEOWMEOW,
	SU_SPIRITOFLAND,
	SU_CHATTERING,
	SU_SPIRITOFSEA,

	WE_CALLALLFAMILY = 5063,
	WE_ONEFOREVER,
	WE_CHEERUP,

	AB_VITUPERATUM = 5072,
	AB_CONVENIO,

	HLIF_HEAL = 8001,
	HLIF_AVOID,
	HLIF_BRAIN,
	HLIF_CHANGE,
	HAMI_CASTLE,
	HAMI_DEFENCE,
	HAMI_SKIN,
	HAMI_BLOODLUST,
	HFLI_MOON,
	HFLI_FLEET,
	HFLI_SPEED,
	HFLI_SBR44,
	HVAN_CAPRICE,
	HVAN_CHAOTIC,
	HVAN_INSTRUCT,
	HVAN_EXPLOSION,
	MUTATION_BASEJOB,
	MH_SUMMON_LEGION,
	MH_NEEDLE_OF_PARALYZE,
	MH_POISON_MIST,
	MH_PAIN_KILLER,
	MH_LIGHT_OF_REGENE,
	MH_OVERED_BOOST,
	MH_ERASER_CUTTER,
	MH_XENO_SLASHER,
	MH_SILENT_BREEZE,
	MH_STYLE_CHANGE,
	MH_SONIC_CRAW,
	MH_SILVERVEIN_RUSH,
	MH_MIDNIGHT_FRENZY,
	MH_STAHL_HORN,
	MH_GOLDENE_FERSE,
	MH_STEINWAND,
	MH_HEILIGE_STANGE,
	MH_ANGRIFFS_MODUS,
	MH_TINDER_BREAKER,
	MH_CBC,
	MH_EQC,
	MH_MAGMA_FLOW,
	MH_GRANITIC_ARMOR,
	MH_LAVA_SLIDE,
	MH_PYROCLASTIC,
	MH_VOLCANIC_ASH,

	MS_BASH = 8201,
	MS_MAGNUM,
	MS_BOWLINGBASH,
	MS_PARRYING,
	MS_REFLECTSHIELD,
	MS_BERSERK,
	MA_DOUBLE,
	MA_SHOWER,
	MA_SKIDTRAP,
	MA_LANDMINE,
	MA_SANDMAN,
	MA_FREEZINGTRAP,
	MA_REMOVETRAP,
	MA_CHARGEARROW,
	MA_SHARPSHOOTING,
	ML_PIERCE,
	ML_BRANDISH,
	ML_SPIRALPIERCE,
	ML_DEFENDER,
	ML_AUTOGUARD,
	ML_DEVOTION,
	MER_MAGNIFICAT,
	MER_QUICKEN,
	MER_SIGHT,
	MER_CRASH,
	MER_REGAIN,
	MER_TENDER,
	MER_BENEDICTION,
	MER_RECUPERATE,
	MER_MENTALCURE,
	MER_COMPRESS,
	MER_PROVOKE,
	MER_AUTOBERSERK,
	MER_DECAGI,
	MER_SCAPEGOAT,
	MER_LEXDIVINA,
	MER_ESTIMATION,
	MER_KYRIE,
	MER_BLESSING,
	MER_INCAGI,
	MER_INVINCIBLEOFF2,

	EL_CIRCLE_OF_FIRE = 8401,
	EL_FIRE_CLOAK,
	EL_FIRE_MANTLE,
	EL_WATER_SCREEN,
	EL_WATER_DROP,
	EL_WATER_BARRIER,
	EL_WIND_STEP,
	EL_WIND_CURTAIN,
	EL_ZEPHYR,
	EL_SOLID_SKIN,
	EL_STONE_SHIELD,
	EL_POWER_OF_GAIA,
	EL_PYROTECHNIC,
	EL_HEATER,
	EL_TROPIC,
	EL_AQUAPLAY,
	EL_COOLER,
	EL_CHILLY_AIR,
	EL_GUST,
	EL_BLAST,
	EL_WILD_STORM,
	EL_PETROLOGY,
	EL_CURSED_SOIL,
	EL_UPHEAVAL,
	EL_FIRE_ARROW,
	EL_FIRE_BOMB,
	EL_FIRE_BOMB_ATK,
	EL_FIRE_WAVE,
	EL_FIRE_WAVE_ATK,
	EL_ICE_NEEDLE,
	EL_WATER_SCREW,
	EL_WATER_SCREW_ATK,
	EL_TIDAL_WEAPON,
	EL_WIND_SLASH,
	EL_HURRICANE,
	EL_HURRICANE_ATK,
	EL_TYPOON_MIS,
	EL_TYPOON_MIS_ATK,
	EL_STONE_HAMMER,
	EL_ROCK_CRUSHER,
	EL_ROCK_CRUSHER_ATK,
	EL_STONE_RAIN,
};

//The client view ids for land skills.
enum {
	UNT_SAFETYWALL = 0x7e,
	UNT_FIREWALL,
	UNT_WARP_WAITING,
	UNT_WARP_ACTIVE,
	UNT_BENEDICTIO, //TODO
	UNT_SANCTUARY,
	UNT_MAGNUS,
	UNT_PNEUMA,
	UNT_DUMMYSKILL, //These show no effect on the client
	UNT_FIREPILLAR_WAITING,
	UNT_FIREPILLAR_ACTIVE,
	UNT_HIDDEN_TRAP, //TODO
	UNT_TRAP, //TODO
	UNT_HIDDEN_WARP_NPC, //TODO
	UNT_USED_TRAPS,
	UNT_ICEWALL,
	UNT_QUAGMIRE,
	UNT_BLASTMINE,
	UNT_SKIDTRAP,
	UNT_ANKLESNARE,
	UNT_VENOMDUST,
	UNT_LANDMINE,
	UNT_SHOCKWAVE,
	UNT_SANDMAN,
	UNT_FLASHER,
	UNT_FREEZINGTRAP,
	UNT_CLAYMORETRAP,
	UNT_TALKIEBOX,
	UNT_VOLCANO,
	UNT_DELUGE,
	UNT_VIOLENTGALE,
	UNT_LANDPROTECTOR,
	UNT_LULLABY,
	UNT_RICHMANKIM,
	UNT_ETERNALCHAOS,
	UNT_DRUMBATTLEFIELD,
	UNT_RINGNIBELUNGEN,
	UNT_ROKISWEIL,
	UNT_INTOABYSS,
	UNT_SIEGFRIED,
	UNT_DISSONANCE,
	UNT_WHISTLE,
	UNT_ASSASSINCROSS,
	UNT_POEMBRAGI,
	UNT_APPLEIDUN,
	UNT_UGLYDANCE,
	UNT_HUMMING,
	UNT_DONTFORGETME,
	UNT_FORTUNEKISS,
	UNT_SERVICEFORYOU,
	UNT_GRAFFITI,
	UNT_DEMONSTRATION,
	UNT_CALLFAMILY,
	UNT_GOSPEL,
	UNT_BASILICA,
	UNT_MOONLIT,
	UNT_FOGWALL,
	UNT_SPIDERWEB,
	UNT_GRAVITATION,
	UNT_HERMODE,
	UNT_KAENSIN, //TODO
	UNT_SUITON,
	UNT_TATAMIGAESHI,
	UNT_KAEN,
	UNT_GROUNDDRIFT_WIND,
	UNT_GROUNDDRIFT_DARK,
	UNT_GROUNDDRIFT_POISON,
	UNT_GROUNDDRIFT_WATER,
	UNT_GROUNDDRIFT_FIRE,
	UNT_DEATHWAVE, //TODO
	UNT_WATERATTACK, //TODO
	UNT_WINDATTACK, //TODO
	UNT_EARTHQUAKE,
	UNT_EVILLAND,
	UNT_DARK_RUNNER, //TODO
	UNT_DARK_TRANSFER, //TODO
	UNT_EPICLESIS,
	UNT_EARTHSTRAIN,
	UNT_MANHOLE,
	UNT_DIMENSIONDOOR,
	UNT_CHAOSPANIC,
	UNT_MAELSTROM,
	UNT_BLOODYLUST,
	UNT_FEINTBOMB,
	UNT_MAGENTATRAP,
	UNT_COBALTTRAP,
	UNT_MAIZETRAP,
	UNT_VERDURETRAP,
	UNT_FIRINGTRAP,
	UNT_ICEBOUNDTRAP,
	UNT_ELECTRICSHOCKER,
	UNT_CLUSTERBOMB,
	UNT_REVERBERATION,
	UNT_SEVERE_RAINSTORM,
	UNT_FIREWALK,
	UNT_ELECTRICWALK,
	UNT_POEMOFNETHERWORLD,
	UNT_PSYCHIC_WAVE,
	UNT_CLOUD_KILL,
	UNT_POISONSMOKE,
	UNT_NEUTRALBARRIER,
	UNT_STEALTHFIELD,
	UNT_WARMER,
	UNT_THORNS_TRAP,
	UNT_WALLOFTHORN,
	UNT_DEMONIC_FIRE,
	UNT_FIRE_EXPANSION_SMOKE_POWDER,
	UNT_FIRE_EXPANSION_TEAR_GAS,
	UNT_HELLS_PLANT,
	UNT_VACUUM_EXTREME,
	UNT_BANDING,
	UNT_FIRE_MANTLE,
	UNT_WATER_BARRIER,
	UNT_ZEPHYR,
	UNT_POWER_OF_GAIA,
	UNT_FIRE_INSIGNIA,
	UNT_WATER_INSIGNIA,
	UNT_WIND_INSIGNIA,
	UNT_EARTH_INSIGNIA,
	UNT_POISON_MIST,
	UNT_LAVA_SLIDE,
	UNT_VOLCANIC_ASH,
	UNT_ZENKAI_WATER,
	UNT_ZENKAI_LAND,
	UNT_ZENKAI_FIRE,
	UNT_ZENKAI_WIND,
	UNT_MAKIBISHI,
	UNT_VENOMFOG,
	UNT_ICEMINE,
	UNT_FLAMECROSS,
	UNT_HELLBURNING,
	UNT_MAGMA_ERUPTION,
	UNT_KINGS_GRACE,
	UNT_GLITTERING_GREED,
	UNT_B_TRAP,
	UNT_FIRE_RAIN,
	UNT_CN_POWDERING,
	UNT_NYANGGRASS,
	UNT_GROUNDDRIFT_NEUTRAL,

	//Guild Auras
	UNT_GD_LEADERSHIP = 0xc1,
	UNT_GD_GLORYWOUNDS = 0xc2,
	UNT_GD_SOULCOLD = 0xc3,
	UNT_GD_HAWKEYES = 0xc4,

	UNT_MAX
};
/**
 * Skill Unit Save
 */
void skill_usave_add(struct map_session_data *sd, uint16 skill_id, uint16 skill_lv);
void skill_usave_trigger(struct map_session_data *sd);

#define MAX_SKILL_SPELLBOOK_DB	17
enum wl_spheres {
	WLS_FIRE = 0x44,
	WLS_WIND,
	WLS_WATER,
	WLS_STONE,
};
struct s_skill_spellbook_db {
	unsigned short nameid,
		skill_id,
		point;
};
extern struct s_skill_spellbook_db skill_spellbook_db[MAX_SKILL_SPELLBOOK_DB];
void skill_spellbook(struct map_session_data *sd, unsigned short nameid);
int skill_block_check(struct block_list *bl, enum sc_type type, uint16 skill_id);

#define MAX_SKILL_MAGICMUSHROOM_DB 25
struct s_skill_magicmushroom_db {
	uint16 skill_id;
};
extern struct s_skill_magicmushroom_db skill_magicmushroom_db[MAX_SKILL_MAGICMUSHROOM_DB];

int skill_detonator(struct block_list *bl, va_list ap);
bool skill_check_camouflage(struct block_list *bl, struct status_change_entry *sce);

int skill_magicdecoy(struct map_session_data *sd, unsigned short nameid);

int skill_poisoningweapon(struct map_session_data *sd, unsigned short nameid);

int skill_maelstrom_suction(struct block_list *bl, va_list ap);
void skill_select_menu(struct map_session_data *sd, uint16 skill_id);

int skill_elementalanalysis(struct map_session_data *sd, int n, uint16 skill_lv, unsigned short *item_list);

int skill_changematerial(struct map_session_data *sd, int n, unsigned short *item_list);
int skill_akaitsuki_damage(struct block_list *src, struct block_list *bl, int damage, uint16 skill_id, uint16 skill_lv, unsigned int tick);
int skill_get_elemental_type(uint16 skill_id, uint16 skill_lv);

int skill_is_combo(uint16 skill_id);
void skill_combo_toggle_inf(struct block_list *bl, uint16 skill_id, int inf);
void skill_combo(struct block_list *src, struct block_list *dsrc, struct block_list *bl, uint16 skill_id, uint16 skill_lv, int tick);

void skill_toggle_magicpower(struct block_list *bl, uint16 skill_id);

bool skill_check_condition_target(struct block_list *src, struct block_list *bl, uint16 skill_id, uint16 skill_lv);
void skill_reveal_trap_inarea(struct block_list *src, int range, int x, int y);
int skill_banding_count(struct map_session_data *sd);
int skill_chorus_count(struct map_session_data *sd, uint8 flag);
void skill_generate_millenniumshield(struct map_session_data *sd, uint16 skill_id, uint16 skill_lv);

//Skill Damage target
#ifdef ADJUST_SKILL_DAMAGE
enum e_skill_damage_caster {
	SDC_PC   = 0x01,
	SDC_MOB  = 0x02,
	SDC_PET  = 0x04,
	SDC_HOM  = 0x08,
	SDC_MER  = 0x10,
	SDC_ELEM = 0x20,
	SDC_ALL  = SDC_PC|SDC_MOB|SDC_PET|SDC_HOM|SDC_MER|SDC_ELEM,
};
#endif

//Variable name of copied skill by Plagiarism
#define SKILL_VAR_PLAGIARISM "CLONE_SKILL"
//Variable name of copied skill level by Plagiarism
#define SKILL_VAR_PLAGIARISM_LV "CLONE_SKILL_LV"

//Variable name of copied skill by Reproduce
#define SKILL_VAR_REPRODUCE "REPRODUCE_SKILL"
//Variable name of copied skill level by Reproduce
#define SKILL_VAR_REPRODUCE_LV "REPRODUCE_SKILL_LV"

#endif /* _SKILL_H_ */
