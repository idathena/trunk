// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _MAP_H_
#define _MAP_H_

#include "../common/cbasetypes.h"
#include "../common/core.h" // CORE_ST_LAST
#include "../common/mmo.h"
#include "../common/mapindex.h"
#include "../common/db.h"
#include "../common/msg_conf.h"

#include <stdarg.h>

struct npc_data;
struct item_data;
struct Channel;

enum E_MAPSERVER_ST {
	MAPSERVER_ST_RUNNING = CORE_ST_LAST,
	MAPSERVER_ST_STARTING,
	MAPSERVER_ST_SHUTDOWN,
	MAPSERVER_ST_LAST
};

#define msg_config_read(cfgName) map_msg_config_read(cfgName)
#define msg_txt(msg_number) map_msg_txt(msg_number)
#define do_final_msg() map_do_final_msg()
int map_msg_config_read(char *cfgName);
const char *map_msg_txt(int msg_number);
void map_do_final_msg(void);

#define MAX_NPC_PER_MAP 512
#define AREA_SIZE battle_config.area_size
#define CHAT_AREA_SIZE battle_config.chat_area_size
#define DAMAGELOG_SIZE 30
#define LOOTITEM_SIZE 10
#define MAX_MOBSKILL 50 //Max 128, see mob skill_idx type if need this higher
#define MAX_MOB_LIST_PER_MAP 128
#define MAX_EVENTQUEUE 4
#define MAX_EVENTTIMER 64
#define NATURAL_HEAL_INTERVAL 500
#define MIN_FLOORITEM 2
#define MAX_FLOORITEM START_ACCOUNT_NUM
#define MAX_LEVEL 175
#define MAX_DROP_PER_MAP 48
#define MAX_IGNORE_LIST 20 //Official is 14
#define MAX_VENDING 12
#define MAX_MAP_SIZE 512*512 //Wasn't there something like this already? Can't find it. [Shinryo]

//The following system marks a different job ID system used by the map server,
//which makes a lot more sense than the normal one. [Skotlex]
//
//These marks the "level" of the job.
#define JOBL_2_1 0x100 //256
#define JOBL_2_2 0x200 //512
#define JOBL_2 0x300 //768

#define JOBL_UPPER 0x1000 //4096
#define JOBL_BABY 0x2000  //8192
#define JOBL_THIRD 0x4000 //16384

//For filtering and quick checking.
#define MAPID_BASEMASK 0x00ff
#define MAPID_UPPERMASK 0x0fff
#define MAPID_THIRDMASK (JOBL_THIRD|MAPID_UPPERMASK)

//First Jobs
//Note the oddity of the novice:
//Super Novices are considered the 2-1 version of the novice! Novices are considered a first class type, too
enum e_mapid {
//Novice And 1-1 Jobs
	MAPID_NOVICE = 0x0,
	MAPID_SWORDMAN,
	MAPID_MAGE,
	MAPID_ARCHER,
	MAPID_ACOLYTE,
	MAPID_MERCHANT,
	MAPID_THIEF,
	MAPID_TAEKWON,
	MAPID_WEDDING,
	MAPID_GUNSLINGER,
	MAPID_NINJA,
	MAPID_XMAS,
	MAPID_SUMMER,
	MAPID_HANBOK,
	MAPID_GANGSI,
	MAPID_OKTOBERFEST,
	MAPID_SUMMONER,
//2-1 Jobs
	MAPID_SUPER_NOVICE = JOBL_2_1|MAPID_NOVICE,
	MAPID_KNIGHT,
	MAPID_WIZARD,
	MAPID_HUNTER,
	MAPID_PRIEST,
	MAPID_BLACKSMITH,
	MAPID_ASSASSIN,
	MAPID_STAR_GLADIATOR,
	MAPID_REBELLION = JOBL_2_1|MAPID_GUNSLINGER,
	MAPID_KAGEROUOBORO,
	MAPID_DEATH_KNIGHT = JOBL_2_1|MAPID_GANGSI,
//2-2 Jobs
	MAPID_CRUSADER = JOBL_2_2|MAPID_SWORDMAN,
	MAPID_SAGE,
	MAPID_BARDDANCER,
	MAPID_MONK,
	MAPID_ALCHEMIST,
	MAPID_ROGUE,
	MAPID_SOUL_LINKER,
	MAPID_DARK_COLLECTOR = JOBL_2_2|MAPID_GANGSI,
//Trans Novice And Trans 1-1 Jobs
	MAPID_NOVICE_HIGH = JOBL_UPPER|MAPID_NOVICE,
	MAPID_SWORDMAN_HIGH,
	MAPID_MAGE_HIGH,
	MAPID_ARCHER_HIGH,
	MAPID_ACOLYTE_HIGH,
	MAPID_MERCHANT_HIGH,
	MAPID_THIEF_HIGH,
//Trans 2-1 Jobs
	MAPID_LORD_KNIGHT = JOBL_UPPER|MAPID_KNIGHT,
	MAPID_HIGH_WIZARD,
	MAPID_SNIPER,
	MAPID_HIGH_PRIEST,
	MAPID_WHITESMITH,
	MAPID_ASSASSIN_CROSS,
//Trans 2-2 Jobs
	MAPID_PALADIN = JOBL_UPPER|MAPID_CRUSADER,
	MAPID_PROFESSOR,
	MAPID_CLOWNGYPSY,
	MAPID_CHAMPION,
	MAPID_CREATOR,
	MAPID_STALKER,
//Baby Novice And Baby 1-1 Jobs
	MAPID_BABY = JOBL_BABY|MAPID_NOVICE,
	MAPID_BABY_SWORDMAN,
	MAPID_BABY_MAGE,
	MAPID_BABY_ARCHER,
	MAPID_BABY_ACOLYTE,
	MAPID_BABY_MERCHANT,
	MAPID_BABY_THIEF,
	MAPID_BABY_TAEKWON,
	MAPID_BABY_GUNSLINGER = JOBL_BABY|MAPID_GUNSLINGER,
	MAPID_BABY_NINJA,
	MAPID_BABY_SUMMONER = JOBL_BABY|MAPID_SUMMONER,
//Baby 2-1 Jobs
	MAPID_SUPER_BABY = JOBL_BABY|MAPID_SUPER_NOVICE,
	MAPID_BABY_KNIGHT,
	MAPID_BABY_WIZARD,
	MAPID_BABY_HUNTER,
	MAPID_BABY_PRIEST,
	MAPID_BABY_BLACKSMITH,
	MAPID_BABY_ASSASSIN,
	MAPID_BABY_STAR_GLADIATOR,
	MAPID_BABY_REBELLION = JOBL_BABY|MAPID_REBELLION,
	MAPID_BABY_KAGEROUOBORO,
//Baby 2-2 Jobs
	MAPID_BABY_CRUSADER = JOBL_BABY|MAPID_CRUSADER,
	MAPID_BABY_SAGE,
	MAPID_BABY_BARDDANCER,
	MAPID_BABY_MONK,
	MAPID_BABY_ALCHEMIST,
	MAPID_BABY_ROGUE,
	MAPID_BABY_SOUL_LINKER,
//3-1 Jobs
	MAPID_SUPER_NOVICE_E = JOBL_THIRD|MAPID_SUPER_NOVICE,
	MAPID_RUNE_KNIGHT,
	MAPID_WARLOCK,
	MAPID_RANGER,
	MAPID_ARCH_BISHOP,
	MAPID_MECHANIC,
	MAPID_GUILLOTINE_CROSS,
//3-2 Jobs
	MAPID_ROYAL_GUARD = JOBL_THIRD|MAPID_CRUSADER,
	MAPID_SORCERER,
	MAPID_MINSTRELWANDERER,
	MAPID_SURA,
	MAPID_GENETIC,
	MAPID_SHADOW_CHASER,
//Trans 3-1 Jobs
	MAPID_RUNE_KNIGHT_T = JOBL_THIRD|MAPID_LORD_KNIGHT,
	MAPID_WARLOCK_T,
	MAPID_RANGER_T,
	MAPID_ARCH_BISHOP_T,
	MAPID_MECHANIC_T,
	MAPID_GUILLOTINE_CROSS_T,
//Trans 3-2 Jobs
	MAPID_ROYAL_GUARD_T = JOBL_THIRD|MAPID_PALADIN,
	MAPID_SORCERER_T,
	MAPID_MINSTRELWANDERER_T,
	MAPID_SURA_T,
	MAPID_GENETIC_T,
	MAPID_SHADOW_CHASER_T,
//Baby 3-1 Jobs
	MAPID_SUPER_BABY_E = JOBL_THIRD|MAPID_SUPER_BABY,
	MAPID_BABY_RUNE,
	MAPID_BABY_WARLOCK,
	MAPID_BABY_RANGER,
	MAPID_BABY_BISHOP,
	MAPID_BABY_MECHANIC,
	MAPID_BABY_CROSS,
//Baby 3-2 Jobs
	MAPID_BABY_GUARD = JOBL_THIRD|MAPID_BABY_CRUSADER,
	MAPID_BABY_SORCERER,
	MAPID_BABY_MINSTRELWANDERER,
	MAPID_BABY_SURA,
	MAPID_BABY_GENETIC,
	MAPID_BABY_CHASER,
};

//Max size for inputs to Graffiti, Talkie Box and Vending text prompts
#define MESSAGE_SIZE (79 + 1)
//String length you can write in the 'talking box'
#define CHATBOX_SIZE (70 + 1)
//Chatroom-related string sizes
#define CHATROOM_TITLE_SIZE (36 + 1)
#define CHATROOM_PASS_SIZE (8 + 1)
//Max allowed chat text length
#define CHAT_SIZE_MAX (255 + 1)

#define DEFAULT_AUTOSAVE_INTERVAL 5 * 60 * 1000

//Specifies maps where players may hit each other
#define map_flag_vs(m) (map[m].flag.pvp || map[m].flag.gvg_dungeon || map[m].flag.gvg || ((agit_flag || agit2_flag) && map[m].flag.gvg_castle) || map[m].flag.gvg_te || (agit3_flag && map[m].flag.gvg_te_castle) || map[m].flag.battleground)
//Versus map: PVP, BG, GVG, GVG Dungeons, and GVG Castles (regardless of agit_flag status)
#define map_flag_vs2(m) (map[m].flag.pvp || map[m].flag.gvg_dungeon || map[m].flag.gvg || map[m].flag.gvg_castle || map[m].flag.gvg_te || map[m].flag.gvg_te_castle || map[m].flag.battleground)
//Specifies maps that have special GvG/WoE restrictions
#define map_flag_gvg(m) (map[m].flag.gvg || ((agit_flag || agit2_flag) && map[m].flag.gvg_castle) || map[m].flag.gvg_te || (agit3_flag && map[m].flag.gvg_te_castle))
//Specifies if the map is tagged as GvG/WoE (regardless of agit_flag status)
#define map_flag_gvg2(m) (map[m].flag.gvg || map[m].flag.gvg_te || map[m].flag.gvg_castle || map[m].flag.gvg_te_castle)
//No Kill Steal Protection
#define map_flag_ks(m) (map[m].flag.town || map[m].flag.pvp || map[m].flag.gvg || map[m].flag.gvg_te || map[m].flag.battleground)

//WOE:TE Maps (regardless of agit_flag status) [Cydh]
#define map_flag_gvg2_te(m) (map[m].flag.gvg_te || map[m].flag.gvg_te_castle)
//Check if map is GVG maps exclusion for item, skill, and status restriction check (regardless of agit_flag status) [Cydh]
#define map_flag_gvg2_no_te(m) (map[m].flag.gvg || map[m].flag.gvg_castle)

//This stackable implementation does not means a BL can be more than one type at a time, but it's
//meant to make it easier to check for multiple types at a time on invocations such as map_foreach* calls [Skotlex]
enum bl_type {
	BL_NUL   = 0x000,
	BL_PC    = 0x001,
	BL_MOB   = 0x002,
	BL_PET   = 0x004,
	BL_HOM   = 0x008,
	BL_MER   = 0x010,
	BL_ITEM  = 0x020,
	BL_SKILL = 0x040,
	BL_NPC   = 0x080,
	BL_CHAT  = 0x100,
	BL_ELEM  = 0x200,

	BL_ALL   = 0xFFF,
};

//For common mapforeach calls. Since pets cannot be affected, they aren't included here yet.
#define BL_CHAR (BL_PC|BL_MOB|BL_HOM|BL_MER|BL_ELEM)

//NPC Subtype
enum npc_subtype {
	NPCTYPE_WARP, //Warp
	NPCTYPE_SHOP, //Shop
	NPCTYPE_SCRIPT, //Script
	NPCTYPE_CASHSHOP, //Cash Shop
	NPCTYPE_ITEMSHOP, //Item Shop
	NPCTYPE_POINTSHOP, //Point Shop
	NPCTYPE_TOMB, //Monster Tomb
	NPCTYPE_MARKETSHOP, //Market Shop
};

enum e_race {
	RC_NONE_ = -1,   //Don't give us bonus
	RC_FORMLESS = 0, //Nothing
	RC_UNDEAD,       //Undead
	RC_BRUTE,        //Animal
	RC_PLANT,        //Plant
	RC_INSECT,       //Insect
	RC_FISH,         //Fish
	RC_DEMON,        //Devil
	RC_DEMIHUMAN,    //Human
	RC_ANGEL,        //Angel
	RC_DRAGON,       //Dragon
	RC_ALL,          //All
	RC_MAX           //Auto upd enum for array size
};

enum e_classAE {
	CLASS_NONE = -1, //Don't give us bonus
	CLASS_NORMAL = 0,
	CLASS_BOSS,
	CLASS_GUARDIAN,
	CLASS_BATTLEFIELD,
	CLASS_ALL,
	CLASS_MAX //Auto upd enum for array len
};

enum e_race2 {
	RC2_NONE = 0,
	RC2_GOBLIN,
	RC2_KOBOLD,
	RC2_ORC,
	RC2_GOLEM,
	RC2_GUARDIAN,
	RC2_GVG,
	RC2_BATTLEFIELD,
	RC2_TREASURE,
	RC2_NINJA,
	RC2_PORING,
	RC2_INSECT,
	RC2_MANUK,
	RC2_SPLENDIDE,
	RC2_TURTLE,
	RC2_SCARABA,
	RC2_MORA,
	RC2_MALAYA,
	RC2_MALAYA_BOSS,
	RC2_HIDDEN,
	RC2_FACEWORM,
	RC2_MAX
};

//Element list
enum e_element {
	ELE_NONE = -1,   //Nothing
	ELE_NEUTRAL = 0, //Neutral
	ELE_WATER,       //Water
	ELE_EARTH,       //Ground
	ELE_FIRE,        //Fire
	ELE_WIND,        //Wind
	ELE_POISON,      //Poison
	ELE_HOLY,        //Saint
	ELE_DARK,        //Darkness
	ELE_GHOST,       //Telekinesis
	ELE_UNDEAD,      //Undead
	ELE_ALL,
	ELE_MAX          //Auto upd enum for array len
};

#define MAX_ELE_LEVEL 4 //Maximum Element level

/**
 * Types of spirit charms
 * NOTE: Code assumes that this matches the first entries in enum elements
 */
enum spirit_charm_types {
	CHARM_TYPE_NONE = 0,
	CHARM_TYPE_WATER,
	CHARM_TYPE_LAND,
	CHARM_TYPE_FIRE,
	CHARM_TYPE_WIND
};

enum mob_ai {
	AI_NONE = 0,
	AI_ATTACK,
	AI_SPHERE,
	AI_FLORA,
	AI_ZANZOU,
	AI_LEGION,
	AI_FAW,
	AI_MAX
};

enum auto_trigger_flag {
	ATF_SELF   = 0x01,
	ATF_TARGET = 0x02,
	ATF_SHORT  = 0x04,
	ATF_LONG   = 0x08,
	ATF_WEAPON = 0x10,
	ATF_MAGIC  = 0x20,
	ATF_MISC   = 0x40,

	ATF_SKILL  = ATF_MAGIC|ATF_MISC,
};

struct block_list {
	struct block_list *next, *prev;
	int id;
	int16 m, x, y;
	enum bl_type type;
	int val1;
};

// Mob List Held in memory for Dynamic Mobs [Wizputer]
// Expanded to specify all mob-related spawn data by [Skotlex]
struct spawn_data {
	short id; //ID, used because a mob can change it's class
	unsigned short m, x, y;	//Spawn information (map, point, spawn-area around point)
	signed short xs, ys;
	unsigned short num; //Number of mobs using this structure
	unsigned short active; //Number of mobs that are already spawned (for mob_remove_damaged: no)
	unsigned int delay1, delay2; //Spawn delay (fixed base + random variance)
	unsigned int level;
	struct {
		unsigned int size : 2; //Holds if mob has to be tiny/large
		enum mob_ai ai; //Special ai for summoned monsters
		unsigned int dynamic : 1; //Whether this data is indexed by a map's dynamic mob list
		uint8 boss; //0: Non-boss monster | 1: Boss monster | 2: MVP
	} state;
	char name[NAME_LENGTH], eventname[EVENT_NAME_LENGTH]; //Name/event
};

struct flooritem_data {
	struct block_list bl;
	unsigned char subx,suby;
	int cleartimer;
	int first_get_charid,second_get_charid,third_get_charid;
	unsigned int first_get_tick,second_get_tick,third_get_tick;
	struct item item;
	unsigned short mob_id; //ID of monster who dropped it. 0 for non-monster who dropped it
};

enum _sp {
	SP_SPEED,SP_BASEEXP,SP_JOBEXP,SP_KARMA,SP_MANNER,SP_HP,SP_MAXHP,SP_SP, // 0-7
	SP_MAXSP,SP_STATUSPOINT,SP_0a,SP_BASELEVEL,SP_SKILLPOINT,SP_STR,SP_AGI,SP_VIT, // 8-15
	SP_INT,SP_DEX,SP_LUK,SP_CLASS,SP_ZENY,SP_SEX,SP_NEXTBASEEXP,SP_NEXTJOBEXP, // 16-23
	SP_WEIGHT,SP_MAXWEIGHT,SP_1a,SP_1b,SP_1c,SP_1d,SP_1e,SP_1f, // 24-31
	SP_USTR,SP_UAGI,SP_UVIT,SP_UINT,SP_UDEX,SP_ULUK,SP_26,SP_27, // 32-39
	SP_28,SP_ATK1,SP_ATK2,SP_MATK1,SP_MATK2,SP_DEF1,SP_DEF2,SP_MDEF1, // 40-47
	SP_MDEF2,SP_HIT,SP_FLEE1,SP_FLEE2,SP_CRITICAL,SP_ASPD,SP_36,SP_JOBLEVEL, // 48-55
	SP_UPPER,SP_PARTNER,SP_CART,SP_FAME,SP_UNBREAKABLE, //56-60
	SP_CARTINFO = 99, // 99

	SP_BASEJOB = 119, // 100 + 19 - celest
	SP_BASECLASS = 120, //Hmm.. why 100 + 19? I just use the next one [Skotlex]
	SP_KILLERRID = 121,
	SP_KILLEDRID = 122,
	SP_SITTING = 123,
	SP_CHARMOVE = 124,
	SP_CHARRENAME = 125,
	SP_CHARFONT = 126,
	SP_BANK_VAULT = 127,
	SP_ROULETTE_BRONZE = 128,
	SP_ROULETTE_SILVER = 129,
	SP_ROULETTE_GOLD = 130,
	SP_KILLEDRID_X = 131,
	SP_KILLEDRID_Y = 132,
	SP_CASHPOINTS,SP_KAFRAPOINTS,
	SP_PCDIECOUNTER,SP_COOKMASTERY,

	// Mercenaries
	SP_MERCFLEE = 165,SP_MERCKILLS = 189,SP_MERCFAITH = 190,

	// Original 1000-
	SP_ATTACKRANGE = 1000,SP_ATKELE,SP_DEFELE, // 1000-1002
	SP_CASTRATE,SP_MAXHPRATE,SP_MAXSPRATE,SP_SPRATE, // 1003-1006
	SP_ADDDEF_ELE,SP_ADDRACE,SP_ADDSIZE,SP_SUBELE,SP_SUBRACE, // 1007-1011
	SP_ADDEFF,SP_RESEFF, // 1012-1013
	SP_BASE_ATK,SP_ASPD_RATE,SP_HP_RECOV_RATE,SP_SP_RECOV_RATE,SP_SPEED_RATE, // 1014-1018
	SP_CRITICAL_DEF,SP_NEAR_ATK_DEF,SP_LONG_ATK_DEF, // 1019-1021
	SP_DOUBLE_RATE,SP_DOUBLE_ADD_RATE,SP_SKILL_HEAL,SP_MATK_RATE, // 1022-1025
	SP_IGNORE_DEF_ELE,SP_IGNORE_DEF_RACE, // 1026-1027
	SP_ATK_RATE,SP_SPEED_ADDRATE,SP_SP_REGEN_RATE, // 1028-1030
	SP_MAGIC_ATK_DEF,SP_MISC_ATK_DEF, // 1031-1032
	SP_IGNORE_MDEF_ELE,SP_IGNORE_MDEF_RACE, // 1033-1034
	SP_MAGIC_ADDDEF_ELE,SP_MAGIC_ADDRACE,SP_MAGIC_ADDSIZE, // 1035-1037
	SP_PERFECT_HIT_RATE,SP_PERFECT_HIT_ADD_RATE,SP_CRITICAL_RATE,SP_GET_ZENY_NUM,SP_ADD_GET_ZENY_NUM, // 1038-1042
	SP_ADD_DAMAGE_CLASS,SP_ADD_MAGIC_DAMAGE_CLASS,SP_ADD_DEF_MONSTER,SP_ADD_MDEF_MONSTER, // 1043-1046
	SP_ADD_MONSTER_DROP_ITEM,SP_DEF_RATIO_ATK_ELE,SP_DEF_RATIO_ATK_RACE,SP_UNBREAKABLE_GARMENT, // 1047-1050
	SP_HIT_RATE,SP_FLEE_RATE,SP_FLEE2_RATE,SP_DEF_RATE,SP_DEF2_RATE,SP_MDEF_RATE,SP_MDEF2_RATE, // 1051-1057
	SP_SPLASH_RANGE,SP_SPLASH_ADD_RANGE,SP_AUTOSPELL,SP_HP_DRAIN_RATE,SP_SP_DRAIN_RATE, // 1058-1062
	SP_SHORT_WEAPON_DAMAGE_RETURN,SP_LONG_WEAPON_DAMAGE_RETURN,SP_WEAPON_COMA_ELE,SP_WEAPON_COMA_RACE, // 1063-1066
	SP_ADDEFF2,SP_BREAK_WEAPON_RATE,SP_BREAK_ARMOR_RATE,SP_ADD_STEAL_RATE, // 1067-1070
	SP_MAGIC_DAMAGE_RETURN,SP_ALL_STATS = 1073,SP_AGI_VIT,SP_AGI_DEX_STR,SP_PERFECT_HIDE, // 1071-1076
	SP_NO_KNOCKBACK,SP_CLASSCHANGE, // 1077-1078
	SP_HP_DRAIN_VALUE,SP_SP_DRAIN_VALUE, // 1079-1080
	SP_WEAPON_ATK_RATE,SP_WEAPON_MATK_RATE, // 1081-1082
	SP_DELAYRATE,SP_HP_DRAIN_VALUE_RACE,SP_SP_DRAIN_VALUE_RACE, // 1083-1085
	SP_IGNORE_MDEF_RACE_RATE,SP_IGNORE_DEF_RACE_RATE,SP_SKILL_HEAL2,SP_ADDEFF_ONSKILL, //1086-1089
	SP_ADD_HEAL_RATE,SP_ADD_HEAL2_RATE,SP_ABSORB_DMG_MAXHP,SP_CRITICAL_RANGEATK,SP_NO_WALKDELAY, //1090-1094

	SP_RESTART_FULL_RECOVER = 2000,SP_NO_CASTCANCEL,SP_NO_SIZEFIX,SP_NO_MAGIC_DAMAGE,SP_NO_WEAPON_DAMAGE,SP_NO_GEMSTONE, // 2000-2005
	SP_NO_CASTCANCEL2,SP_NO_MISC_DAMAGE,SP_UNBREAKABLE_WEAPON,SP_UNBREAKABLE_ARMOR,SP_UNBREAKABLE_HELM, // 2006-2010
	SP_UNBREAKABLE_SHIELD,SP_LONG_ATK_RATE, // 2011-2012

	SP_CRIT_ATK_RATE,SP_CRITICAL_ADDRACE,SP_NO_REGEN,SP_ADDEFF_WHENHIT,SP_AUTOSPELL_WHENHIT, // 2013-2017
	SP_SKILL_ATK,SP_UNSTRIPABLE,SP_AUTOSPELL_ONSKILL, // 2018-2020
	SP_SP_GAIN_VALUE,SP_HP_REGEN_RATE,SP_HP_LOSS_RATE,SP_ADDRACE2,SP_HP_GAIN_VALUE, // 2021-2025
	SP_SUBSIZE,SP_HP_DRAIN_VALUE_CLASS,SP_ADD_ITEM_HEAL_RATE,SP_SP_DRAIN_VALUE_CLASS,SP_EXP_ADDRACE, // 2026-2030
	SP_SP_GAIN_RACE,SP_SUBRACE2,SP_UNBREAKABLE_SHOES, // 2031-2033
	SP_UNSTRIPABLE_WEAPON,SP_UNSTRIPABLE_ARMOR,SP_UNSTRIPABLE_HELM,SP_UNSTRIPABLE_SHIELD, // 2034-2037
	SP_INTRAVISION,SP_ADD_MONSTER_DROP_ITEMGROUP,SP_SP_LOSS_RATE, // 2038-2040
	SP_ADD_SKILL_BLOW,SP_SP_VANISH_RATE,SP_MAGIC_SP_GAIN_VALUE,SP_MAGIC_HP_GAIN_VALUE,SP_ADD_MONSTER_ID_DROP_ITEM, // 2041-2045
	SP_EMATK,SP_SP_VANISH_RACE_RATE,SP_HP_VANISH_RACE_RATE,SP_SKILL_USE_SP_RATE, // 2046-2049
	SP_SKILL_COOLDOWN,SP_SKILL_FIXEDCAST,SP_SKILL_VARIABLECAST,SP_FIXCASTRATE,SP_VARCASTRATE, // 2050-2054
	SP_SKILL_USE_SP,SP_MAGIC_ATK_ELE,SP_ADD_FIXEDCAST,SP_ADD_VARIABLECAST, // 2055-2058
	SP_SET_DEF_RACE,SP_SET_MDEF_RACE,SP_SUB_SKILL,SP_HP_VANISH_RATE, // 2059-2062

	SP_IGNORE_DEF_CLASS,SP_IGNORE_DEF_CLASS_RATE,SP_IGNORE_MDEF_CLASS,SP_IGNORE_MDEF_CLASS_RATE, //2063-2066
	SP_DEF_RATIO_ATK_CLASS,SP_ADDCLASS,SP_SUBCLASS,SP_MAGIC_ADDCLASS,SP_WEAPON_COMA_CLASS, //2067-2071
	SP_SUBDEF_ELE,SP_EXP_ADDCLASS,SP_ADD_CLASS_DROP_ITEM,SP_ADD_CLASS_DROP_ITEMGROUP, //2072-2075
	SP_ADDMAXWEIGHT,SP_ADD_ITEMGROUP_HEAL_RATE,SP_STATE_NORECOVER_RACE,SP_DROP_ADDRACE,SP_DROP_ADDCLASS, //2076-2080
	SP_MAGIC_ADDRACE2,SP_NO_MAGIC_GEAR_FUEL //2082
};

enum _look {
	LOOK_BASE,           //Job
	LOOK_HAIR,           //Head
	LOOK_WEAPON,         //Weapon
	LOOK_HEAD_BOTTOM,    //Accessory
	LOOK_HEAD_TOP,       //Accessory2
	LOOK_HEAD_MID,       //Accessory3
	LOOK_HAIR_COLOR,     //Headpalette
	LOOK_CLOTHES_COLOR,  //Bodypalette
	LOOK_SHIELD,         //Shield
	LOOK_SHOES,          //Shoes
	LOOK_COSTUMEBODY,    //Costume Body - Used to be Body and didn't do anything at the time of testing
	LOOK_RESET_COSTUMES, //Reset Costumes - Makes all headgear sprites on player vanish when activated
	LOOK_ROBE,           //Robe
	LOOK_BODY2,          //Body2 - Changes body appearance
};

// Used by map_setcell()
typedef enum {
	CELL_WALKABLE,
	CELL_SHOOTABLE,
	CELL_WATER,

	CELL_NPC,
	CELL_BASILICA,
	CELL_LANDPROTECTOR,
	CELL_NOVENDING,
	CELL_NOCHAT,
	CELL_MAELSTROM,
	CELL_ICEWALL,
	CELL_NOICEWALL

} cell_t;

// Used by map_getcell()
typedef enum {
	CELL_GETTYPE,          // Retrieves a cell's 'gat' type

	CELL_CHKWALL,          // Whether the cell is a wall (gat type 1)
	CELL_CHKWATER,         // Whether the cell is water (gat type 3)
	CELL_CHKCLIFF,         // Whether the cell is a cliff/gap (gat type 5)

	CELL_CHKPASS,          // Whether the cell is passable (gat type not 1 and 5)
	CELL_CHKREACH,         // Whether the cell is passable, but ignores the cell stacking limit
	CELL_CHKNOPASS,        // Whether the cell is non-passable (gat types 1 and 5)
	CELL_CHKNOREACH,       // Whether the cell is non-passable, but ignores the cell stacking limit
	CELL_CHKSTACK,         // Whether the cell is full (reached cell stacking limit)

	CELL_CHKNPC,           // Whether the cell has an OnTouch NPC
	CELL_CHKBASILICA,      // Whether the cell has Basilica
	CELL_CHKLANDPROTECTOR, // Whether the cell has Land Protector
	CELL_CHKNOVENDING,     // Whether the cell denies MC_VENDING skill
	CELL_CHKNOCHAT,        // Whether the cell denies Player Chat Window
	CELL_CHKMAELSTROM,     // Whether the cell has Maelstrom
	CELL_CHKICEWALL,       // Whether the cell has Ice Wall
	CELL_CHKNOICEWALL      // Whether the cell isn't allowed to cast Ice Wall
} cell_chk;

struct mapcell
{
	// Terrain flags
	unsigned char
		walkable : 1,
		shootable : 1,
		water : 1;

	// Dynamic flags
	unsigned char
		npc : 1,
		basilica : 1,
		landprotector : 1,
		novending : 1,
		nochat : 1,
		icewall : 1,
		maelstrom : 1,
		noicewall : 1;

#ifdef CELL_NOSTACK
	unsigned char cell_bl; // Holds amount of bls in this cell.
#endif
};

struct iwall_data {
	char wall_name[50];
	short m, x, y, size;
	int8 dir;
	bool shootable;
};

#ifdef ADJUST_SKILL_DAMAGE
// Struct of skill damage adjustment
struct s_skill_damage {
	unsigned int map; // Maps (used for skill_damage_db.txt)
	uint16 skill_id; // Skill ID (used for mapflag)
	// Additional rates
	int pc, // Rate to Player
		mob, // Rate to Monster
		boss, // Rate to Boss-Monster
		other; // Rate to Other target
	uint8 caster; // Caster type
};
extern struct eri *map_skill_damage_ers;
#endif

struct mapflag_skill_adjust {
	unsigned short skill_id;
	unsigned short modifier;
};

struct questinfo_req {
	int quest_id;
	unsigned state : 2; // 0: Doesn't have, 1: Active/Inactive, 2: Complete
};

struct questinfo {
	struct npc_data *nd;
	unsigned short icon;
	unsigned char color;
	int quest_id;
	unsigned short min_level,
		max_level;
	uint8 req_count;
	uint8 jobid_count;
	struct questinfo_req *req;
	unsigned short *jobid;
};

struct map_data {
	char name[MAP_NAME_LENGTH];
	uint16 index; // The map index used by the mapindex* functions.
	struct mapcell *cell; // Holds the information of each map cell (NULL if the map is not on this map-server).
	struct block_list **block;
	struct block_list **block_mob;
	int16 m;
	int16 xs, ys; // Map dimensions (in cells)
	int16 bxs, bys; // Map dimensions (in blocks)
	int16 bgscore_lion, bgscore_eagle; // Battleground ScoreBoard
	int npc_num;
	int users;
	int users_pvp;
	int iwall_num; // Total of invisible walls in this map
	struct map_flag {
		unsigned town : 1; // [Suggestion to protect Mail System]
		unsigned autotrade : 1;
		unsigned allowks : 1; // [Kill Steal Protection]
		unsigned nomemo : 1;
		unsigned noteleport : 1;
		unsigned noreturn : 1;
		unsigned monster_noteleport : 1;
		unsigned nosave : 1;
		unsigned nobranch : 1;
		unsigned noexppenalty : 1;
		unsigned pvp : 1;
		unsigned pvp_noparty : 1;
		unsigned pvp_noguild : 1;
		unsigned pvp_nightmaredrop :1;
		unsigned pvp_nocalcrank : 1;
		unsigned gvg_castle : 1;
		unsigned gvg : 1; // Now it identifies gvg vs-maps that are active 24/7
		unsigned gvg_dungeon : 1; // Celest
		unsigned gvg_noparty : 1;
		unsigned battleground : 2; // [BattleGround System]
		unsigned nozenypenalty : 1;
		unsigned notrade : 1;
		unsigned noskill : 1;
		unsigned nowarp : 1;
		unsigned nowarpto : 1;
		unsigned noicewall : 1; // [Valaris]
		unsigned snow : 1; // [Valaris]
		unsigned clouds : 1;
		unsigned clouds2 : 1; // [Valaris]
		unsigned fog : 1; // [Valaris]
		unsigned fireworks : 1;
		unsigned sakura : 1; // [Valaris]
		unsigned leaves : 1; // [Valaris]
		unsigned nogo : 1; // [Valaris]
		unsigned nobaseexp : 1; // [Lorky] added by Lupus
		unsigned nojobexp : 1; // [Lorky]
		unsigned nomobloot : 1; // [Lorky]
		unsigned nomvploot : 1; // [Lorky]
		unsigned nightenabled :1; // For night display. [Skotlex]
		unsigned restricted : 1; // [Komurka]
		unsigned nodrop : 1;
		unsigned novending : 1;
		unsigned loadevent : 1;
		unsigned nochat : 1;
		unsigned partylock : 1;
		unsigned guildlock : 1;
		unsigned reset : 1; // [Daegaladh]
		unsigned nochmautojoin : 1; // Prevent to auto join map channel
		unsigned nousecart : 1;	// Prevent open up cart FIXME: client side only atm
		unsigned noitemconsumption : 1; // Prevent item usage
		unsigned nosumstarmiracle : 1; // Allow SG miracle to happen ?
		unsigned nomineeffect : 1; // Allow /mineeffect
		unsigned nolockon : 1;
		unsigned notomb : 1;
		unsigned nocashshop : 1;
		unsigned nobanking : 1;
		unsigned gvg_te : 1; // GVG WOE:TE. This was added as purpose to change 'gvg' for GVG TE, so item_noequp, skill_nocast exlude GVG TE maps from 'gvg' (flag &4)
		unsigned gvg_te_castle : 1; // GVG WOE:TE Castle
		unsigned nocostume : 1; // Disable costume sprites [Cydh]
		unsigned hidemobhpbar : 1;
#ifdef ADJUST_SKILL_DAMAGE
		unsigned skill_damage : 1;
#endif
	} flag;
	struct point save;
	struct npc_data *npc[MAX_NPC_PER_MAP];
	struct {
		int drop_id;
		int drop_type;
		int drop_per;
	} drop_list[MAX_DROP_PER_MAP];

	struct spawn_data *moblist[MAX_MOB_LIST_PER_MAP]; // [Wizputer]
	int mob_delete_timer; // Timer ID for map_removemobs_timer [Skotlex]
	uint32 zone; // Zone number (for item/skill restrictions)
	int nocommand; //Blocks @/# commands for non-gms [Skotlex]
	struct {
		int jexp; // Map experience multiplicator
		int bexp; // Map experience multiplicator
#ifdef ADJUST_SKILL_DAMAGE
		struct s_skill_damage damage;
#endif
	} adjust;
#ifdef ADJUST_SKILL_DAMAGE
	struct {
		struct s_skill_damage **entries;
		uint8 count;
	} skill_damage;
#endif

	// Instance Variables
	int instance_id;
	int instance_src_map;

	// rAthena Local Chat
	struct Channel *channel;

	// Adjust_unit_duration mapflag
	struct mapflag_skill_adjust **units;
	unsigned short unit_count;
	// Adjust_skill_damage mapflag
	struct mapflag_skill_adjust **skills;
	unsigned short skill_count;

	// Speeds up clif_updatestatus processing by causing hpmeter to run only when someone with the permission can view it
	unsigned short hpmeter_visible;

	// ShowEvent Data Cache
	struct questinfo *qi_data;
	unsigned short qi_count;
};

/// Stores information about a remote map (for multi-mapserver setups).
/// Beginning of data structure matches 'map_data', to allow typecasting.
struct map_data_other_server {
	char name[MAP_NAME_LENGTH];
	unsigned short index; // Index is the map index used by the mapindex* functions.
	struct mapcell *cell; // If this is NULL, the map is not on this map-server
	uint32 ip;
	uint16 port;
};

int map_getcell(int16 m,int16 x,int16 y,cell_chk cellchk);
int map_getcellp(struct map_data *m,int16 x,int16 y,cell_chk cellchk);
void map_setcell(int16 m, int16 x, int16 y, cell_t cell, bool flag);
void map_setgatcell(int16 m, int16 x, int16 y, int gat);

extern struct map_data map[];
extern int map_num;

extern int autosave_interval;
extern int minsave_interval;
extern int save_settings;
extern int night_flag; // 0 = day, 1 = night [Yor]
extern int enable_spy; // Determines if @spy commands are active.
extern char db_path[256];

// Agit Flags
extern bool agit_flag;
extern bool agit2_flag;
extern bool agit3_flag;
#define is_agit_start() (agit_flag || agit2_flag || agit3_flag)

extern char motd_txt[];
extern char help_txt[];
extern char help2_txt[];
extern char charhelp_txt[];
extern char channel_conf[];

extern char wisp_server_name[];

// Type of 'save_settings'
enum save_settings_type {
	CHARSAVE_NONE    = 0,
	CHARSAVE_TRADE       = 0x001, //After every successful trade
	CHARSAVE_VENDING     = 0x002, //After opening vending/every vending transaction
	CHARSAVE_STORAGE     = 0x004, //After closing storage/guild storage
	CHARSAVE_PET         = 0x008, //After hatching/returning to egg a pet
	CHARSAVE_MAIL        = 0x010, //After successfully sending a mail with attachment
	CHARSAVE_AUCTION     = 0x020, //After successfully submitting an item for auction
	CHARSAVE_QUEST       = 0x040, //After successfully get/delete/complete a quest
	CHARSAVE_BUYINGSTORE = 0x080, //After every buyingstore transaction
	CHARSAVE_BANK        = 0x100, //After every bank transaction (deposit/withdraw)
	CHARSAVE_ALL         = 0x1FF,
};

struct s_map_default {
	char mapname[MAP_NAME_LENGTH];
	unsigned short x;
	unsigned short y;
};
extern struct s_map_default map_default;

// Users
void map_setusers(int);
int map_getusers(void);
int map_usercount(void);

// Blocklist lock
int map_freeblock(struct block_list *bl);
int map_freeblock_lock(void);
int map_freeblock_unlock(void);
// Blocklist manipulation
int map_addblock(struct block_list *bl);
int map_delblock(struct block_list *bl);
int map_moveblock(struct block_list *bl, int x1, int y1, unsigned int tick);
int map_foreachinrange(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int type, ...);
int map_foreachinallrange(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int type, ...);
int map_foreachinshootrange(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int type, ...);
int map_foreachinarea(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, ...);
int map_foreachinallarea(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, ...);
int map_foreachinshootarea(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, ...);
int map_forcountinrange(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int count, int type, ...);
int map_forcountinarea(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int count, int type, ...);
int map_foreachinmovearea(int (*func)(struct block_list *, va_list), struct block_list *center, int16 range, int16 dx, int16 dy, int type, ...);
int map_foreachincell(int (*func)(struct block_list *, va_list), int16 m, int16 x, int16 y, int type, ...);
int map_foreachinpath(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int16 range, int length, int type, ...);
int map_foreachindir(int (*func)(struct block_list *, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int16 range, int length, int offset, int type, ...);
int map_foreachinmap(int (*func)(struct block_list *, va_list), int16 m, int type, ...);
// Blocklist nb in one cell
int map_count_oncell(int16 m, int16 x, int16 y, int type, int flag);
struct skill_unit *map_find_skill_unit_oncell(struct block_list *, int16 x, int16 y, uint16 skill_id, struct skill_unit *, int flag);
// Search and creation
int map_get_new_object_id(void);
int map_search_freecell(struct block_list *src, int16 m, int16 *x, int16 *y, int16 rx, int16 ry, int flag);
bool map_closest_freecell(int16 m, int16 *x, int16 *y, int type, int flag);
//
int map_quit(struct map_session_data *);
// Npc
bool map_addnpc(int16 m,struct npc_data *);

// Map item
int map_clearflooritem_timer(int tid, unsigned int tick, int id, intptr_t data);
int map_removemobs_timer(int tid, unsigned int tick, int id, intptr_t data);
void map_clearflooritem(struct block_list *bl);
int map_addflooritem(struct item *item, int amount, int16 m, int16 x, int16 y, int first_charid, int second_charid, int third_charid, int flags, unsigned short mob_id);

// Instances
int map_addinstancemap(const char *,int);
int map_delinstancemap(int);

// player to map session
void map_addnickdb(int charid, const char *nick);
void map_delnickdb(int charid, const char *nick);
void map_reqnickdb(struct map_session_data *sd,int charid);
const char *map_charid2nick(int charid);
struct map_session_data *map_charid2sd(int charid);

struct map_session_data *map_id2sd(int id);
struct mob_data *map_id2md(int id);
struct npc_data *map_id2nd(int id);
struct homun_data *map_id2hd(int id);
struct mercenary_data *map_id2mc(int id);
struct pet_data *map_id2pd(int id);
struct elemental_data *map_id2ed(int id);
struct chat_data *map_id2cd(int id);
struct block_list *map_id2bl(int id);
bool map_blid_exists(int id);

#define map_id2index(id) map[(id)].index
const char *map_mapid2mapname(int m);
int16 map_mapindex2mapid(unsigned short mapindex);
int16 map_mapname2mapid(const char *name);
int map_mapname2ipport(unsigned short name, uint32 *ip, uint16* port);
int map_setipport(unsigned short map, uint32 ip, uint16 port);
int map_eraseipport(unsigned short map, uint32 ip, uint16 port);
int map_eraseallipport(void);
void map_addiddb(struct block_list *);
void map_deliddb(struct block_list *bl);
void map_foreachpc(int (*func)(struct map_session_data *sd, va_list args), ...);
void map_foreachmob(int (*func)(struct mob_data *md, va_list args), ...);
void map_foreachnpc(int (*func)(struct npc_data *nd, va_list args), ...);
void map_foreachregen(int (*func)(struct block_list *bl, va_list args), ...);
void map_foreachiddb(int (*func)(struct block_list *bl, va_list args), ...);
struct map_session_data *map_nick2sd(const char *);
struct mob_data *map_getmob_boss(int16 m);
struct mob_data *map_id2boss(int id);

// reload config file looking only for npcs
void map_reloadnpc(bool clear);

/// Bitfield of flags for the iterator.
enum e_mapitflags
{
	MAPIT_NORMAL = 0,
//	MAPIT_PCISPLAYING = 1,// Unneeded as pc_db/id_db will only hold auth'ed, active players.
};

struct s_mapiterator;
struct s_mapiterator *mapit_alloc(enum e_mapitflags flags, enum bl_type types);
void mapit_free(struct s_mapiterator *mapit);
struct block_list *mapit_first(struct s_mapiterator *mapit);
struct block_list *mapit_last(struct s_mapiterator *mapit);
struct block_list *mapit_next(struct s_mapiterator *mapit);
struct block_list *mapit_prev(struct s_mapiterator *mapit);
bool mapit_exists(struct s_mapiterator *mapit);
#define mapit_getallusers() mapit_alloc(MAPIT_NORMAL,BL_PC)
#define mapit_geteachpc()   mapit_alloc(MAPIT_NORMAL,BL_PC)
#define mapit_geteachmob()  mapit_alloc(MAPIT_NORMAL,BL_MOB)
#define mapit_geteachnpc()  mapit_alloc(MAPIT_NORMAL,BL_NPC)
#define mapit_geteachiddb() mapit_alloc(MAPIT_NORMAL,BL_ALL)

int map_check_dir(int s_dir, int t_dir);
uint8 map_calc_dir(struct block_list *src, int16 x, int16 y);
uint8 map_calc_dir_xy(int16 srcx, int16 srcy, int16 x, int16 y, uint8 srcdir);
int map_random_dir(struct block_list *bl, short *x, short *y); // [Skotlex]

int cleanup_sub(struct block_list *bl, va_list ap);

int map_delmap(char *mapname);
void map_flags_init(void);

bool map_iwall_set(int16 m, int16 x, int16 y, int size, int8 dir, bool shootable, const char *wall_name); 
void map_iwall_get(struct map_session_data *sd);
void map_iwall_remove(const char *wall_name);

int map_addmobtolist(unsigned short m, struct spawn_data *spawn); // [Wizputer]
void map_spawnmobs(int16 m); // [Wizputer]
void map_removemobs(int16 m); // [Wizputer]
void do_reconnect_map(void); // Invoked on map-char reconnection [Skotlex]
void map_addmap2db(struct map_data *m);
void map_removemapdb(struct map_data *m);

#ifdef ADJUST_SKILL_DAMAGE
void map_skill_damage_free(struct map_data *m);
void map_skill_damage_add(struct map_data *m, uint16 skill_id, int pc, int mob, int boss, int other, uint8 caster);
#endif

#define CHK_ELEMENT(ele) ((ele) > ELE_NONE && (ele) < ELE_MAX) // Check valid Element
#define CHK_ELEMENT_LEVEL(lv) ((lv) >= 1 && (lv) <= MAX_ELE_LEVEL) // Check valid element level
#define CHK_RACE(race) ((race) > RC_NONE_ && (race) < RC_MAX) // Check valid Race
#define CHK_RACE2(race2) ((race2) >= RC2_NONE && (race2) < RC2_MAX) // Check valid Race2
#define CHK_CLASS(class_) ((class_) > CLASS_NONE && (class_) < CLASS_MAX) // Check valid Class

struct questinfo *map_add_questinfo(int m, struct questinfo *qi);
bool map_remove_questinfo(int m, struct npc_data *nd);
struct questinfo *map_has_questinfo(int m, struct npc_data *nd, int quest_id);

extern char *INTER_CONF_NAME;
extern char *LOG_CONF_NAME;
extern char *MAP_CONF_NAME;
extern char *BATTLE_CONF_FILENAME;
extern char *ATCOMMAND_CONF_FILENAME;
extern char *SCRIPT_CONF_NAME;
extern char *MSG_CONF_NAME;
extern char *GRF_PATH_FILENAME;

//Useful typedefs from jA [Skotlex]
typedef struct map_session_data TBL_PC;
typedef struct npc_data         TBL_NPC;
typedef struct mob_data         TBL_MOB;
typedef struct flooritem_data   TBL_ITEM;
typedef struct chat_data        TBL_CHAT;
typedef struct skill_unit       TBL_SKILL;
typedef struct pet_data         TBL_PET;
typedef struct homun_data       TBL_HOM;
typedef struct mercenary_data   TBL_MER;
typedef struct elemental_data	TBL_ELEM;

#define BL_CAST(type_, bl) \
	( ((bl) == (struct block_list *)NULL || (bl)->type != (type_)) ? (T ## type_ *)NULL : (T ## type_ *)(bl) )

#ifdef BETA_THREAD_TEST

extern char default_codepage[32];
extern int map_server_port;
extern char map_server_ip[32];
extern char map_server_id[32];
extern char map_server_pw[32];
extern char map_server_db[32];

extern char log_db_ip[32];
extern int log_db_port;
extern char log_db_id[32];
extern char log_db_pw[32];
extern char log_db_db[32];

#endif

#include "../common/sql.h"

extern int db_use_sqldbs;

extern Sql *mmysql_handle;
extern Sql *qsmysql_handle;
extern Sql *logmysql_handle;

extern char buyingstores_db[32];
extern char buyingstore_items_db[32];
extern char item_db_db[32];
extern char item_db2_db[32];
extern char item_db_re_db[32];
extern char markets_db[32];
extern char mob_db_db[32];
extern char mob_db_re_db[32];
extern char mob_db2_db[32];
extern char mob_skill_db_db[32];
extern char mob_skill_db_re_db[32];
extern char mob_skill_db2_db[32];
extern char vendings_db[32];
extern char vending_items_db[32];

void do_shutdown(void);

#endif /* _MAP_H_ */
