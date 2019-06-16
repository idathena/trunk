// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/malloc.h"
#include "../common/utils.h"
#include "../common/ers.h"
#include "../common/strlib.h"
#include "../common/conf.h"

#include "map.h"
#include "path.h"
#include "pc.h"
#include "pet.h"
#include "npc.h"
#include "mob.h"
#include "clif.h"
#include "guild.h"
#include "skill.h"
#include "itemdb.h"
#include "battle.h"
#include "battleground.h"
#include "chrif.h"
#include "skill.h"
#include "status.h"
#include "script.h"
#include "unit.h"
#include "homunculus.h"
#include "mercenary.h"
#include "elemental.h"
#include "vending.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include <math.h>

//Regen related flags
enum e_regen {
	RGN_NONE = 0x00,
	RGN_HP = 0x01,
	RGN_SP = 0x02,
	RGN_SHP = 0x04,
	RGN_SSP = 0x08,
};

static int atkmods[3][MAX_WEAPON_TYPE];	/// ATK weapon modification for size (size_fix.txt)

static struct eri *sc_data_ers; /// For sc_data entries
static struct status_data dummy_status;

short current_equip_item_index; /// Contains inventory index of an equipped item. To pass it into the EQUIP_SCRIPT [Lupus]
unsigned int current_equip_pos; /// For combo items we need to save the position of all involved items here
int current_equip_card_id; /// To prevent card-stacking (from jA) [Skotlex]
bool running_npc_stat_calc_event; /// Indicate if OnPCStatCalcEvent is running
// We need it for new cards 15 Feb 2005, to check if the combo cards are insrerted into the CURRENT weapon only to avoid cards exploits
short current_equip_opt_index; /// Contains random option index of an equipped item [Secret]

unsigned int SCDisabled[SC_MAX]; /// List of disabled SC on map zones [Cydh]
static bool status_change_isDisabledOnMap_(sc_type type, bool mapIsVS, bool mapIsPVP, bool mapIsGVG, bool mapIsBG, unsigned int mapZone, bool mapIsTE);
#define status_change_isDisabledOnMap(type, m) ( status_change_isDisabledOnMap_((type), map_flag_vs2((m)), map[(m)].flag.pvp, map_flag_gvg2_no_te((m)), map[(m)].flag.battleground, map[(m)].zone << 3, map_flag_gvg2_te((m))) )

sc_type SkillStatusChangeTable[MAX_SKILL];
int StatusIconChangeTable[SC_MAX];
unsigned int StatusChangeFlagTable[SC_MAX];
int StatusSkillChangeTable[SC_MAX];
int StatusRelevantBLTypes[SI_MAX];
unsigned int StatusChangeStateTable[SC_MAX];
bool StatusDisplayType[SC_MAX];
sc_conf_type StatusConfig[SC_MAX];

/**
 * Returns the status change associated with a skill.
 * @param skill The skill to look up
 * @return The status registered for this skill
 */
sc_type status_skill2sc(uint16 skill_id)
{
	uint16 idx = skill_get_index(skill_id);

	if( !idx ) {
		ShowError("status_skill2sc: Unsupported skill id %d\n", skill_id);
		return SC_NONE;
	}
	return SkillStatusChangeTable[idx];
}

/**
 * Returns the FIRST skill (in order of definition in initChangeTables) to use a given status change.
 * Utilized for various duration lookups. Use with caution!
 * @param sc The status to look up
 * @return A skill associated with the status
 */
int status_sc2skill(sc_type sc)
{
	if( sc < 0 || sc >= SC_MAX ) {
		ShowError("status_sc2skill: Unsupported status change id %d\n", sc);
		return 0;
	}
	return StatusSkillChangeTable[sc];
}

/**
 * Returns the status calculation flag associated with a given status change.
 * @param sc The status to look up
 * @return The scb_flag registered for this status (see enum scb_flag)
 */
unsigned int status_sc2scb_flag(sc_type sc)
{
	if( sc < 0 || sc >= SC_MAX ) {
		ShowError("status_sc2scb_flag: Unsupported status change id %d\n", sc);
		return SCB_NONE;
	}
	return StatusChangeFlagTable[sc];
}

/**
 * Returns the bl types which require a status change packet to be sent for a given client status identifier.
 * @param type The client-side status identifier to look up (see enum si_type)
 * @return The bl types relevant to the type (see enum bl_type)
 */
int status_type2relevant_bl_types(int type)
{
	if( type < 0 || type >= SI_MAX ) {
		ShowError("status_type2relevant_bl_types: Unsupported type %d\n", type);
		return SI_BLANK;
	}
	return StatusRelevantBLTypes[type];
}

/**
 * Returns the status configuration flag associated with a given status change.
 * @param sc The status to look up
 * @return The flag registered for this status
 */
sc_conf_type status_get_sc_type(sc_type sc)
{
	if( sc < 0 || sc >= SC_MAX ) {
		ShowError("status_get_sc_type: Unsupported status change id %d\n", sc);
		return 0;
	}
	return StatusConfig[sc];
}

#define add_sc(skill,sc) set_sc(skill,sc,SI_BLANK,SCB_NONE)
//Indicates that the status displays a visual effect for the affected unit, and should be sent to the client for all supported units
#define set_sc_with_vfx(skill, sc, icon, flag) set_sc((skill), (sc), (icon), (flag)); if((icon) < SI_MAX) StatusRelevantBLTypes[(icon)] |= BL_SCEFFECT

static void set_sc(uint16 skill_id, sc_type sc, int icon, unsigned int flag)
{
	uint16 idx = skill_get_index(skill_id);

	if( !idx ) {
		ShowError("set_sc: Unsupported skill id %d\n", skill_id);
		return;
	}
	if( sc < 0 || sc >= SC_MAX ) {
		ShowError("set_sc: Unsupported status change id %d\n", sc);
		return;
	}
	if( !StatusSkillChangeTable[sc] )
		StatusSkillChangeTable[sc] = skill_id;
	if( StatusIconChangeTable[sc] == SI_BLANK )
		StatusIconChangeTable[sc] = icon;
	StatusChangeFlagTable[sc] |= flag;
	if( SkillStatusChangeTable[idx] == SC_NONE )
		SkillStatusChangeTable[idx] = sc;
}

void initChangeTables(void) {
	int i;

	for (i = 0; i < SC_MAX; i++)
		StatusIconChangeTable[i] = SI_BLANK;

	for (i = 0; i < MAX_SKILL; i++)
		SkillStatusChangeTable[i] = SC_NONE;

	for (i = 0; i < SI_MAX; i++)
		StatusRelevantBLTypes[i] = BL_PC;

	memset(StatusSkillChangeTable, 0, sizeof(StatusSkillChangeTable));
	memset(StatusChangeFlagTable, 0, sizeof(StatusChangeFlagTable));
	memset(StatusChangeStateTable, 0, sizeof(StatusChangeStateTable));
	memset(StatusDisplayType, 0, sizeof(StatusDisplayType));
	memset(SCDisabled, 0, sizeof(SCDisabled));

	//First we define the skill for common ailments, these are used in skill_additional_effect through sc cards [Skotlex]
	set_sc( NPC_PETRIFYATTACK    , SC_STONE           , SI_BLANK           , SCB_DEF|SCB_MDEF|SCB_DEF_ELE );
	set_sc( NPC_WIDEFREEZE       , SC_FREEZE          , SI_BLANK           , SCB_DEF|SCB_MDEF|SCB_DEF_ELE );
	set_sc( NPC_STUNATTACK       , SC_STUN            , SI_BLANK           , SCB_NONE );
	set_sc( NPC_SLEEPATTACK      , SC_SLEEP           , SI_BLANK           , SCB_NONE );
	set_sc( NPC_POISON           , SC_POISON          , SI_BLANK           , SCB_DEF2|SCB_REGEN );
	set_sc( NPC_WIDECURSE        , SC_CURSE           , SI_BLANK           , SCB_LUK|SCB_BATK|SCB_WATK|SCB_SPEED );
	set_sc( NPC_SILENCEATTACK    , SC_SILENCE         , SI_BLANK           , SCB_NONE );
	set_sc( NPC_WIDECONFUSE      , SC_CONFUSION       , SI_BLANK           , SCB_NONE );
	set_sc( NPC_BLINDATTACK      , SC_BLIND           , SI_BLANK           , SCB_HIT|SCB_FLEE );
	set_sc( NPC_BLEEDING         , SC_BLEEDING        , SI_BLOODING        , SCB_REGEN );
	set_sc( NPC_POISON           , SC_DPOISON         , SI_BLANK           , SCB_DEF2|SCB_REGEN );
	set_sc( NPC_WIDEHEALTHFEAR   , SC_FEAR            , SI_BLANK           , SCB_HIT|SCB_FLEE );
	set_sc( NPC_WIDEBODYBURNNING , SC_BURNING         , SI_BLANK           , SCB_MDEF );
	set_sc_with_vfx( NPC_WIDEFROSTMISTY, SC_FREEZING  , SI_FROSTMISTY      , SCB_DEF|SCB_SPEED|SCB_ASPD );
	set_sc_with_vfx( NPC_WIDECOLD, SC_CRYSTALIZE      , SI_COLD            , SCB_NONE );
	set_sc( NPC_WIDESIREN        , SC_VOICEOFSIREN    , SI_SIREN           , SCB_NONE );
	set_sc_with_vfx( NPC_WIDE_DEEP_SLEEP, SC_DEEPSLEEP, SI_DEEP_SLEEP      , SCB_NONE );

	//The main status definitions
	add_sc( SM_BASH              , SC_STUN            );
	set_sc( SM_PROVOKE           , SC_PROVOKE         , SI_PROVOKE         , SCB_BATK|SCB_WATK|SCB_DEF|SCB_DEF2 );
	add_sc( SM_MAGNUM            , SC_WATK_ELEMENT    );
	set_sc( SM_ENDURE            , SC_ENDURE          , SI_ENDURE          , SCB_MDEF|SCB_DSPD );
	add_sc( MG_SIGHT             , SC_SIGHT           );
	add_sc( MG_SAFETYWALL        , SC_SAFETYWALL      );
	add_sc( MG_FROSTDIVER        , SC_FREEZE          );
	add_sc( MG_STONECURSE        , SC_STONE           );
	add_sc( AL_RUWACH            , SC_RUWACH          );
	add_sc( AL_PNEUMA            , SC_PNEUMA          );
	set_sc( AL_INCAGI            , SC_INCREASEAGI     , SI_INC_AGI         , SCB_AGI|SCB_SPEED );
	set_sc( AL_DECAGI            , SC_DECREASEAGI     , SI_DEC_AGI         , SCB_AGI|SCB_SPEED );
	set_sc( AL_CRUCIS            , SC_SIGNUMCRUCIS    , SI_CRUCIS          , SCB_DEF );
	set_sc( AL_ANGELUS           , SC_ANGELUS         , SI_ANGELUS         , SCB_DEF2 );
	set_sc( AL_BLESSING          , SC_BLESSING        , SI_BLESSING        , SCB_STR|SCB_INT|SCB_DEX );
	set_sc( AC_CONCENTRATION     , SC_CONCENTRATE     , SI_CONCENTRATION   , SCB_AGI|SCB_DEX );
	set_sc( TF_HIDING            , SC_HIDING          , SI_HIDING          , SCB_SPEED );
	add_sc( TF_POISON            , SC_POISON          );
	set_sc( KN_TWOHANDQUICKEN    , SC_TWOHANDQUICKEN  , SI_TWOHANDQUICKEN  , SCB_ASPD );
	set_sc( KN_AUTOCOUNTER       , SC_AUTOCOUNTER     , SI_AUTOCOUNTER     , SCB_NONE );
	set_sc( PR_IMPOSITIO         , SC_IMPOSITIO       , SI_IMPOSITIO       , SCB_WATK );
	set_sc( PR_SUFFRAGIUM        , SC_SUFFRAGIUM      , SI_SUFFRAGIUM      , SCB_NONE );
	set_sc( PR_ASPERSIO          , SC_ASPERSIO        , SI_ASPERSIO        , SCB_ATK_ELE );
	set_sc( PR_BENEDICTIO        , SC_BENEDICTIO      , SI_BENEDICTIO      , SCB_DEF_ELE );
	set_sc( PR_SLOWPOISON        , SC_SLOWPOISON      , SI_SLOWPOISON      , SCB_REGEN );
	set_sc( PR_KYRIE             , SC_KYRIE           , SI_KYRIE           , SCB_NONE );
	set_sc( PR_MAGNIFICAT        , SC_MAGNIFICAT      , SI_MAGNIFICAT      , SCB_REGEN );
	set_sc( PR_GLORIA            , SC_GLORIA          , SI_GLORIA          , SCB_LUK );
	add_sc( PR_STRECOVERY        , SC_BLIND           );
	add_sc( PR_LEXDIVINA         , SC_SILENCE         );
	set_sc( PR_LEXAETERNA        , SC_AETERNA         , SI_LEXAETERNA      , SCB_NONE );
	add_sc( WZ_METEOR            , SC_STUN            );
	add_sc( WZ_VERMILION         , SC_BLIND           );
	add_sc( WZ_FROSTNOVA         , SC_FREEZE          );
	add_sc( WZ_STORMGUST         , SC_FREEZE          );
	set_sc( WZ_QUAGMIRE          , SC_QUAGMIRE        , SI_QUAGMIRE        , SCB_AGI|SCB_DEX|SCB_SPEED|SCB_ASPD );
	add_sc( BS_HAMMERFALL        , SC_STUN            );
	set_sc( BS_ADRENALINE        , SC_ADRENALINE      , SI_ADRENALINE      , SCB_ASPD );
	set_sc( BS_WEAPONPERFECT     , SC_WEAPONPERFECTION, SI_WEAPONPERFECT   , SCB_NONE );
	set_sc( BS_OVERTHRUST        , SC_OVERTHRUST      , SI_OVERTHRUST      , SCB_NONE );
	set_sc( BS_MAXIMIZE          , SC_MAXIMIZEPOWER   , SI_MAXIMIZE        , SCB_REGEN );
	add_sc( HT_LANDMINE          , SC_STUN            );
	set_sc( HT_ANKLESNARE        , SC_ANKLE           , SI_ANKLESNARE      , SCB_NONE );
	add_sc( HT_SANDMAN           , SC_SLEEP           );
	add_sc( HT_FLASHER           , SC_BLIND           );
	add_sc( HT_FREEZINGTRAP      , SC_FREEZE          );
	set_sc( AS_CLOAKING          , SC_CLOAKING        , SI_CLOAKING        , SCB_CRI|SCB_SPEED );
	add_sc( AS_SONICBLOW         , SC_STUN            );
	set_sc( AS_ENCHANTPOISON     , SC_ENCPOISON       , SI_ENCHANTPOISON   , SCB_ATK_ELE );
	set_sc( AS_POISONREACT       , SC_POISONREACT     , SI_POISONREACT     , SCB_NONE );
	add_sc( AS_VENOMDUST         , SC_POISON          );
	set_sc( AS_SPLASHER          , SC_SPLASHER        , SI_SPLASHER        , SCB_NONE );
	set_sc( NV_TRICKDEAD         , SC_TRICKDEAD       , SI_TRICKDEAD       , SCB_REGEN );
	set_sc( SM_AUTOBERSERK       , SC_AUTOBERSERK     , SI_AUTOBERSERK     , SCB_NONE );
	add_sc( TF_SPRINKLESAND      , SC_BLIND           );
	add_sc( TF_THROWSTONE        , SC_STUN            );
	set_sc( MC_LOUD              , SC_LOUD            , SI_SHOUT           , SCB_STR );
	set_sc( MG_ENERGYCOAT        , SC_ENERGYCOAT      , SI_ENERGYCOAT      , SCB_NONE );
	set_sc( NPC_EMOTION          , SC_MODECHANGE      , SI_BLANK           , SCB_MODE );
	add_sc( NPC_EMOTION_ON       , SC_MODECHANGE      );
	set_sc( NPC_ATTRICHANGE      , SC_ELEMENTALCHANGE , SI_ARMOR_PROPERTY  , SCB_DEF_ELE );
	add_sc( NPC_CHANGEWATER      , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEGROUND     , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEFIRE       , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEWIND       , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEPOISON     , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEHOLY       , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEDARKNESS   , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGETELEKINESIS, SC_ELEMENTALCHANGE );
	add_sc( NPC_POISON           , SC_POISON          );
	add_sc( NPC_BLINDATTACK      , SC_BLIND           );
	add_sc( NPC_SILENCEATTACK    , SC_SILENCE         );
	add_sc( NPC_STUNATTACK       , SC_STUN            );
	add_sc( NPC_PETRIFYATTACK    , SC_STONE           );
	add_sc( NPC_CURSEATTACK      , SC_CURSE           );
	add_sc( NPC_SLEEPATTACK      , SC_SLEEP           );
	add_sc( NPC_MAGICALATTACK    , SC_MAGICALATTACK   );
	set_sc( NPC_KEEPING          , SC_KEEPING         , SI_BLANK           , SCB_DEF );
	add_sc( NPC_DARKBLESSING     , SC_COMA            );
	set_sc( NPC_BARRIER          , SC_BARRIER         , SI_BARRIER         , SCB_DEF|SCB_MDEF );
	add_sc( NPC_DEFENDER         , SC_ARMOR           );
	add_sc( NPC_LICK             , SC_STUN            );
	set_sc( NPC_HALLUCINATION    , SC_HALLUCINATION   , SI_ILLUSION        , SCB_NONE );
	add_sc( NPC_REBIRTH          , SC_REBIRTH         );
	add_sc( RG_RAID              , SC_STUN            );
#ifdef RENEWAL
	set_sc( RG_RAID              , SC_RAID            , SI_RAID            , SCB_NONE );
	add_sc( RG_BACKSTAP          , SC_STUN            );
#endif
	set_sc( RG_STRIPWEAPON       , SC_STRIPWEAPON     , SI_NOEQUIPWEAPON   ,
#ifdef RENEWAL
		SCB_BATK
#else
		SCB_WATK
#endif
		);
	set_sc( RG_STRIPSHIELD       , SC_STRIPSHIELD     , SI_NOEQUIPSHIELD   , SCB_DEF );
	set_sc( RG_STRIPARMOR        , SC_STRIPARMOR      , SI_NOEQUIPARMOR    , SCB_VIT );
	set_sc( RG_STRIPHELM         , SC_STRIPHELM       , SI_NOEQUIPHELM     , SCB_INT );
	add_sc( AM_ACIDTERROR        , SC_BLEEDING        );
	set_sc( AM_CP_WEAPON         , SC_CP_WEAPON       , SI_PROTECTWEAPON   , SCB_NONE );
	set_sc( AM_CP_SHIELD         , SC_CP_SHIELD       , SI_PROTECTSHIELD   , SCB_NONE );
	set_sc( AM_CP_ARMOR          , SC_CP_ARMOR        , SI_PROTECTARMOR    , SCB_NONE );
	set_sc( AM_CP_HELM           , SC_CP_HELM         , SI_PROTECTHELM     , SCB_NONE );
	set_sc( CR_AUTOGUARD         , SC_AUTOGUARD       , SI_AUTOGUARD       , SCB_NONE );
	add_sc( CR_SHIELDCHARGE      , SC_STUN            );
	set_sc( CR_REFLECTSHIELD     , SC_REFLECTSHIELD   , SI_REFLECTSHIELD   , SCB_NONE );
	add_sc( CR_HOLYCROSS         , SC_BLIND           );
	add_sc( CR_GRANDCROSS        , SC_BLIND           );
	set_sc( CR_DEVOTION          , SC_DEVOTION        , SI_DEVOTION        , SCB_NONE);
	set_sc( CR_PROVIDENCE        , SC_PROVIDENCE      , SI_PROVIDENCE      , SCB_ALL );
	set_sc( CR_DEFENDER          , SC_DEFENDER        , SI_DEFENDER        , SCB_SPEED|SCB_ASPD );
	set_sc( CR_SPEARQUICKEN      , SC_SPEARQUICKEN    , SI_SPEARQUICKEN    , SCB_FLEE|SCB_ASPD|SCB_CRI );
	set_sc( MO_STEELBODY         , SC_STEELBODY       , SI_STEELBODY       , SCB_DEF|SCB_MDEF|SCB_SPEED|SCB_ASPD );
	add_sc( MO_BLADESTOP         , SC_BLADESTOP_WAIT  );
	set_sc( MO_BLADESTOP         , SC_BLADESTOP       , SI_BLADESTOP       , SCB_NONE );
	set_sc( MO_EXPLOSIONSPIRITS  , SC_EXPLOSIONSPIRITS, SI_EXPLOSIONSPIRITS, SCB_CRI|SCB_REGEN );
	set_sc( MO_EXTREMITYFIST     ,
#ifndef RENEWAL
		SC_EXTREMITYFIST    , SI_BLANK           , SCB_REGEN
#else
		SC_EXTREMITYFIST2   , SI_EXTREMITYFIST   , SCB_NONE
#endif
		);
	set_sc( SA_MAGICROD          , SC_MAGICROD        , SI_MAGICROD        , SCB_NONE );
	set_sc( SA_AUTOSPELL         , SC_AUTOSPELL       , SI_AUTOSPELL       , SCB_NONE );
	set_sc( SA_FLAMELAUNCHER     , SC_FIREWEAPON      , SI_PROPERTYFIRE    , SCB_ATK_ELE );
	set_sc( SA_FROSTWEAPON       , SC_WATERWEAPON     , SI_PROPERTYWATER   , SCB_ATK_ELE );
	set_sc( SA_LIGHTNINGLOADER   , SC_WINDWEAPON      , SI_PROPERTYWIND    , SCB_ATK_ELE );
	set_sc( SA_SEISMICWEAPON     , SC_EARTHWEAPON     , SI_PROPERTYGROUND  , SCB_ATK_ELE );
	set_sc( SA_VOLCANO           , SC_VOLCANO         , SI_GROUNDMAGIC     ,
#ifdef RENEWAL
		SCB_NONE
#else
		SCB_WATK
#endif
		);
	set_sc( SA_DELUGE            , SC_DELUGE          , SI_GROUNDMAGIC     , SCB_MAXHP );
	set_sc( SA_VIOLENTGALE       , SC_VIOLENTGALE     , SI_GROUNDMAGIC     , SCB_FLEE );
	add_sc( SA_REVERSEORCISH     , SC_ORCISH          );
	add_sc( SA_COMA              , SC_COMA            );
	set_sc( BD_ENCORE            , SC_DANCING         , SI_BDPLAYING       , SCB_SPEED|SCB_REGEN );
	set_sc( BD_RICHMANKIM        , SC_RICHMANKIM      , SI_RICHMANKIM      , SCB_NONE  );
	set_sc( BD_ETERNALCHAOS      , SC_ETERNALCHAOS    , SI_ETERNALCHAOS    , SCB_DEF2 );
	set_sc( BD_DRUMBATTLEFIELD   , SC_DRUMBATTLE      , SI_DRUMBATTLEFIELD , SCB_DEF
#ifndef RENEWAL
		|SCB_WATK
#endif
		);
	set_sc( BD_RINGNIBELUNGEN    , SC_NIBELUNGEN      , SI_RINGNIBELUNGEN  ,
#ifdef RENEWAL
		SCB_NONE
#else
		SCB_WATK
#endif
		);
	set_sc( BD_ROKISWEIL         , SC_ROKISWEIL       , SI_ROKISWEIL       , SCB_NONE );
	set_sc( BD_INTOABYSS         , SC_INTOABYSS       , SI_INTOABYSS       , SCB_NONE );
	set_sc( BD_SIEGFRIED         , SC_SIEGFRIED       , SI_SIEGFRIED       , SCB_ALL );
	add_sc( BA_FROSTJOKER        , SC_FREEZE          );
	set_sc( BA_WHISTLE           , SC_WHISTLE         , SI_WHISTLE         , SCB_FLEE|SCB_FLEE2 );
	set_sc( BA_ASSASSINCROSS     , SC_ASSNCROS        , SI_ASSASSINCROSS   , SCB_ASPD );
	set_sc( BA_POEMBRAGI         , SC_POEMBRAGI       , SI_POEMBRAGI       , SCB_NONE  );
	set_sc( BA_APPLEIDUN         , SC_APPLEIDUN       , SI_APPLEIDUN       , SCB_MAXHP );
	add_sc( DC_SCREAM            , SC_STUN            );
	set_sc( DC_HUMMING           , SC_HUMMING         , SI_HUMMING         , SCB_HIT );
	set_sc( DC_DONTFORGETME      , SC_DONTFORGETME    , SI_DONTFORGETME    , SCB_SPEED|SCB_ASPD );
	set_sc( DC_FORTUNEKISS       , SC_FORTUNE         , SI_FORTUNEKISS     , SCB_CRI );
	set_sc( DC_SERVICEFORYOU     , SC_SERVICE4U       , SI_SERVICEFORYOU   , SCB_ALL );
	add_sc( NPC_DARKCROSS        , SC_BLIND           );
	add_sc( NPC_GRANDDARKNESS    , SC_BLIND           );
	set_sc( NPC_STOP             , SC_STOP            , SI_STOP            , SCB_NONE );
	set_sc( NPC_WEAPONBRAKER     , SC_BROKENWEAPON    , SI_BROKENWEAPON    , SCB_NONE );
	set_sc( NPC_ARMORBRAKE       , SC_BROKENARMOR     , SI_BROKENARMOR     , SCB_NONE );
	set_sc( NPC_CHANGEUNDEAD     , SC_CHANGEUNDEAD    , SI_PROPERTYUNDEAD  , SCB_DEF_ELE );
	set_sc( NPC_POWERUP          , SC_INCHITRATE      , SI_BLANK           , SCB_NONE );
	set_sc( NPC_AGIUP            , SC_INCFLEERATE     , SI_BLANK           , SCB_FLEE );
	add_sc( NPC_INVISIBLE        , SC_CLOAKING        );
	set_sc( LK_AURABLADE         , SC_AURABLADE       , SI_AURABLADE       , SCB_NONE );
	set_sc( LK_PARRYING          , SC_PARRYING        , SI_PARRYING        , SCB_NONE );
	set_sc( LK_CONCENTRATION     , SC_CONCENTRATION   , SI_LKCONCENTRATION ,
#ifndef RENEWAL
		SCB_BATK|SCB_WATK|SCB_HIT|SCB_DEF|SCB_DEF2
#else
		SCB_HIT|SCB_DEF
#endif
		);
	set_sc( LK_TENSIONRELAX      , SC_TENSIONRELAX    , SI_TENSIONRELAX    , SCB_REGEN );
	set_sc( LK_BERSERK           , SC_BERSERK         , SI_BERSERK         ,
#ifndef RENEWAL
		SCB_MAXHP|SCB_FLEE|SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_MDEF2|SCB_SPEED|SCB_ASPD|SCB_REGEN
#else
		SCB_MAXHP|SCB_SPEED|SCB_ASPD|SCB_REGEN
#endif
		);
	set_sc( HP_ASSUMPTIO         , SC_ASSUMPTIO       ,
#ifndef RENEWAL
		SI_ASSUMPTIO             ,
#else
		SI_ASSUMPTIO2            ,
#endif
		SCB_NONE );
	set_sc( HP_BASILICA          , SC_BASILICA        , SI_BASILICA        , SCB_NONE );
	set_sc( HW_MAGICPOWER        , SC_MAGICPOWER      , SI_MAGICPOWER      , SCB_MATK );
	set_sc( PA_SACRIFICE         , SC_SACRIFICE       , SI_SACRIFICE       , SCB_NONE );
	set_sc( PA_GOSPEL            , SC_GOSPEL          , SI_GOSPEL          , SCB_SPEED|SCB_ASPD );
	add_sc( PA_GOSPEL            , SC_SCRESIST        );
	add_sc( CH_TIGERFIST         , SC_STOP            );
	set_sc( ASC_EDP              , SC_EDP             , SI_EDP             , SCB_NONE );
	add_sc( ASC_EDP              , SC_DPOISON         );
	set_sc( SN_SIGHT             , SC_TRUESIGHT       , SI_TRUESIGHT       , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK|SCB_HIT|SCB_CRI );
	set_sc( SN_WINDWALK          , SC_WINDWALK        , SI_WINDWALK        , SCB_FLEE|SCB_SPEED );
	set_sc( WS_MELTDOWN          , SC_MELTDOWN        , SI_MELTDOWN        , SCB_NONE );
	set_sc( WS_CARTBOOST         , SC_CARTBOOST       , SI_CARTBOOST       , SCB_SPEED );
	set_sc( ST_CHASEWALK         , SC_CHASEWALK       , SI_CHASEWALK       , SCB_SPEED );
	set_sc( ST_REJECTSWORD       , SC_REJECTSWORD     , SI_SWORDREJECT     , SCB_NONE );
	set_sc( CG_TAROTCARD         , SC_TAROTCARD       , SI_TAROTCARD       , SCB_NONE );
	set_sc( CG_MARIONETTE        , SC_MARIONETTE      , SI_MARIONETTE_MASTER, SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	set_sc( CG_MARIONETTE        , SC_MARIONETTE2     , SI_MARIONETTE      , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	add_sc( LK_SPIRALPIERCE      , SC_STOP            );
	add_sc( LK_HEADCRUSH         , SC_BLEEDING        );
	set_sc( LK_JOINTBEAT         , SC_JOINTBEAT       , SI_JOINTBEAT       , SCB_BATK|SCB_DEF2|SCB_SPEED|SCB_ASPD );
	add_sc( HW_NAPALMVULCAN      , SC_CURSE           );
	set_sc( PF_MINDBREAKER       , SC_MINDBREAKER     , SI_MINDBREAKER     , SCB_NONE );
	set_sc( PF_MEMORIZE          , SC_MEMORIZE        , SI_MEMORIZE        , SCB_NONE );
	set_sc( PF_FOGWALL           , SC_FOGWALL         , SI_FOGWALL         , SCB_NONE );
	set_sc( PF_SPIDERWEB         , SC_SPIDERWEB       , SI_SPIDERWEB       , SCB_FLEE );
	set_sc( WE_BABY              , SC_BABY            , SI_PROTECTEXP      , SCB_NONE );
	set_sc( TK_RUN               , SC_RUN             , SI_RUN             , SCB_SPEED|SCB_DSPD );
	set_sc( TK_RUN               , SC_STRUP           , SI_STRUP           , SCB_STR );
	set_sc( TK_READYSTORM        , SC_READYSTORM      , SI_STORMKICK_ON    , SCB_NONE );
	set_sc( TK_READYDOWN         , SC_READYDOWN       , SI_DOWNKICK_ON     , SCB_NONE );
	add_sc( TK_DOWNKICK          , SC_STUN            );
	set_sc( TK_READYTURN         , SC_READYTURN       , SI_TURNKICK_ON     , SCB_NONE );
	set_sc( TK_READYCOUNTER      , SC_READYCOUNTER    , SI_COUNTER_ON      , SCB_NONE );
	set_sc( TK_DODGE             , SC_DODGE           , SI_DODGE_ON        , SCB_NONE );
	set_sc( TK_SPTIME            , SC_EARTHSCROLL     , SI_EARTHSCROLL     , SCB_NONE );
	add_sc( TK_SEVENWIND         , SC_SEVENWIND       );
	set_sc( TK_SEVENWIND         , SC_GHOSTWEAPON     , SI_PROPERTYTELEKINESIS, SCB_ATK_ELE );
	set_sc( TK_SEVENWIND         , SC_SHADOWWEAPON    , SI_PROPERTYDARK    , SCB_ATK_ELE );
	set_sc( SG_SUN_WARM          , SC_WARM            , SI_SG_SUN_WARM     , SCB_NONE );
	set_sc( SG_MOON_WARM         , SC_WARM            , SI_SG_MOON_WARM    , SCB_NONE );
	set_sc( SG_STAR_WARM         , SC_WARM            , SI_SG_STAR_WARM    , SCB_NONE );
	set_sc( SG_SUN_COMFORT       , SC_SUN_COMFORT     , SI_SUN_COMFORT     , SCB_DEF2 );
	set_sc( SG_MOON_COMFORT      , SC_MOON_COMFORT    , SI_MOON_COMFORT    , SCB_FLEE );
	set_sc( SG_STAR_COMFORT      , SC_STAR_COMFORT    , SI_STAR_COMFORT    , SCB_ASPD );
	add_sc( SG_FRIEND            , SC_SKILLRATE_UP    );
	set_sc( SG_KNOWLEDGE         , SC_KNOWLEDGE       , SI_BLANK           , SCB_ALL );
	set_sc( SG_FUSION            , SC_FUSION          , SI_BLANK           , SCB_SPEED );
	set_sc( BS_ADRENALINE2       , SC_ADRENALINE2     , SI_ADRENALINE2     , SCB_ASPD );
	set_sc( SL_KAIZEL            , SC_KAIZEL          , SI_KAIZEL          , SCB_NONE );
	set_sc( SL_KAAHI             , SC_KAAHI           , SI_KAAHI           , SCB_NONE );
	set_sc( SL_KAUPE             , SC_KAUPE           , SI_KAUPE           , SCB_NONE );
	set_sc( SL_KAITE             , SC_KAITE           , SI_KAITE           , SCB_NONE );
	add_sc( SL_STUN              , SC_STUN            );
	set_sc( SL_SWOO              , SC_SWOO            , SI_SWOO            , SCB_SPEED );
	set_sc( SL_SKE               , SC_SKE             , SI_BLANK           , SCB_BATK|SCB_WATK|SCB_DEF|SCB_DEF2 );
	set_sc( SL_SKA               , SC_SKA             , SI_BLANK           , SCB_DEF2|SCB_MDEF2|SCB_SPEED|SCB_ASPD );
	set_sc( SL_SMA               , SC_SMA             , SI_SMA_READY       , SCB_NONE );
	set_sc( SM_SELFPROVOKE       , SC_PROVOKE         , SI_PROVOKE         , SCB_BATK|SCB_WATK|SCB_DEF|SCB_DEF2 );
	set_sc( ST_PRESERVE          , SC_PRESERVE        , SI_PRESERVE        , SCB_NONE );
	set_sc( PF_DOUBLECASTING     , SC_DOUBLECAST      , SI_DOUBLECASTING   , SCB_NONE );
	set_sc( HW_GRAVITATION       , SC_GRAVITATION     , SI_GRAVITATION     , SCB_ASPD );
	add_sc( WS_CARTTERMINATION   , SC_STUN            );
	set_sc( WS_OVERTHRUSTMAX     , SC_MAXOVERTHRUST   , SI_OVERTHRUSTMAX   , SCB_NONE );
	set_sc( CG_LONGINGFREEDOM    , SC_LONGING         , SI_LONGING         , SCB_SPEED|SCB_ASPD );
	set_sc( CG_HERMODE           , SC_HERMODE         , SI_HERMODE         , SCB_NONE    );
	set_sc( ITEM_ENCHANTARMS     , SC_ENCHANTARMS     , SI_BLANK           , SCB_ATK_ELE );
	set_sc( SL_HIGH              , SC_SPIRIT          , SI_SOULLINK        , SCB_ALL );
	set_sc( KN_ONEHAND           , SC_ONEHAND         , SI_ONEHANDQUICKEN  , SCB_ASPD );
	set_sc( GS_FLING             , SC_FLING           , SI_BLANK           , SCB_DEF|SCB_DEF2 );
	add_sc( GS_CRACKER           , SC_STUN            );
	add_sc( GS_DISARM            , SC_STRIPWEAPON     );
	add_sc( GS_PIERCINGSHOT      , SC_BLEEDING        );
	set_sc( GS_MADNESSCANCEL     , SC_MADNESSCANCEL   , SI_GS_MADNESSCANCEL, SCB_ASPD
#ifndef RENEWAL
		|SCB_BATK
#endif
		);
	set_sc( GS_ADJUSTMENT        , SC_ADJUSTMENT      , SI_GS_ADJUSTMENT   , SCB_HIT|SCB_FLEE );
	set_sc( GS_INCREASING        , SC_INCREASING      , SI_GS_ACCURACY     , SCB_AGI|SCB_DEX|SCB_HIT );
#ifdef RENEWAL
	set_sc( GS_MAGICALBULLET     , SC_MAGICALBULLET   , SI_GS_MAGICAL_BULLET, SCB_NONE );
#endif
	set_sc( GS_GATLINGFEVER      , SC_GATLINGFEVER    , SI_GS_GATLINGFEVER , SCB_FLEE|SCB_SPEED|SCB_ASPD
#ifndef RENEWAL
		|SCB_BATK
#endif
		);
	set_sc( NJ_TATAMIGAESHI      , SC_TATAMIGAESHI    , SI_BLANK           , SCB_NONE );
	set_sc( NJ_SUITON            , SC_SUITON          , SI_BLANK           , SCB_AGI|SCB_SPEED );
	add_sc( NJ_HYOUSYOURAKU      , SC_FREEZE          );
	set_sc( NJ_NEN               , SC_NEN             , SI_NJ_NEN          , SCB_STR|SCB_INT );
	set_sc( NJ_UTSUSEMI          , SC_UTSUSEMI        , SI_NJ_UTSUSEMI     , SCB_NONE );
	set_sc( NJ_BUNSINJYUTSU      , SC_BUNSINJYUTSU    , SI_NJ_BUNSINJYUTSU , SCB_DYE );

	add_sc( NPC_ICEBREATH        , SC_FREEZE          );
	add_sc( NPC_ACIDBREATH       , SC_POISON          );
	add_sc( NPC_HELLJUDGEMENT    , SC_CURSE           );
	add_sc( NPC_WIDESILENCE      , SC_SILENCE         );
	add_sc( NPC_WIDEFREEZE       , SC_FREEZE          );
	add_sc( NPC_WIDEBLEEDING     , SC_BLEEDING        );
	add_sc( NPC_WIDESTONE        , SC_STONE           );
	add_sc( NPC_WIDECONFUSE      , SC_CONFUSION       );
	add_sc( NPC_WIDESLEEP        , SC_SLEEP           );
	add_sc( NPC_WIDESIGHT        , SC_SIGHT           );
	add_sc( NPC_EVILLAND         , SC_BLIND           );
	add_sc( NPC_MAGICMIRROR      , SC_MAGICMIRROR     );
	set_sc( NPC_SLOWCAST         , SC_SLOWCAST        , SI_SLOWCAST        , SCB_NONE );
	set_sc( NPC_CRITICALWOUND    , SC_CRITICALWOUND   , SI_CRITICALWOUND   , SCB_NONE );
	set_sc( NPC_STONESKIN        , SC_ARMORCHANGE     , SI_BLANK           ,
#ifndef RENEWAL
		SCB_DEF|SCB_MDEF
#else
		SCB_NONE
#endif
		);
	add_sc( NPC_ANTIMAGIC        , SC_ARMORCHANGE     );
	add_sc( NPC_WIDECURSE        , SC_CURSE           );
	add_sc( NPC_WIDESTUN         , SC_STUN            );

	set_sc( NPC_HELLPOWER        , SC_HELLPOWER       , SI_HELLPOWER       , SCB_NONE );
	set_sc( NPC_WIDEHELLDIGNITY  , SC_HELLPOWER       , SI_HELLPOWER       , SCB_NONE );
	set_sc( NPC_INVINCIBLE       , SC_INVINCIBLE      , SI_INVINCIBLE      , SCB_SPEED );
	set_sc( NPC_INVINCIBLEOFF    , SC_INVINCIBLEOFF   , SI_BLANK           , SCB_SPEED );

	set_sc( CASH_BLESSING        , SC_BLESSING        , SI_BLESSING        , SCB_STR|SCB_INT|SCB_DEX );
	set_sc( CASH_INCAGI          , SC_INCREASEAGI     , SI_INC_AGI         , SCB_AGI|SCB_SPEED );
	set_sc( CASH_ASSUMPTIO       , SC_ASSUMPTIO       ,
#ifndef RENEWAL
		SI_ASSUMPTIO             ,
#else
		SI_ASSUMPTIO2            ,
#endif
		SCB_NONE );

	set_sc( ALL_PARTYFLEE        , SC_PARTYFLEE       , SI_PARTYFLEE       , SCB_FLEE );
	set_sc( ALL_ODINS_POWER      , SC_ODINS_POWER     , SI_ODINS_POWER     , SCB_WATK|SCB_MATK|SCB_DEF|SCB_MDEF );

	set_sc( CR_SHRINK            , SC_SHRINK          , SI_CR_SHRINK       , SCB_NONE );
	set_sc( RG_CLOSECONFINE      , SC_CLOSECONFINE2   , SI_RG_CCONFINE_S   , SCB_NONE );
	set_sc( RG_CLOSECONFINE      , SC_CLOSECONFINE    , SI_RG_CCONFINE_M   , SCB_FLEE );
	set_sc( WZ_SIGHTBLASTER      , SC_SIGHTBLASTER    , SI_WZ_SIGHTBLASTER , SCB_NONE );
	set_sc( DC_WINKCHARM         , SC_WINKCHARM       , SI_DC_WINKCHARM    , SCB_NONE );
	add_sc( MO_BALKYOUNG         , SC_STUN            );
	add_sc( SA_ELEMENTWATER      , SC_ELEMENTALCHANGE );
	add_sc( SA_ELEMENTFIRE       , SC_ELEMENTALCHANGE );
	add_sc( SA_ELEMENTGROUND     , SC_ELEMENTALCHANGE );
	add_sc( SA_ELEMENTWIND       , SC_ELEMENTALCHANGE );

	set_sc( HLIF_AVOID           , SC_AVOID           , SI_BLANK           , SCB_SPEED );
	set_sc( HLIF_CHANGE          , SC_CHANGE          , SI_BLANK           , SCB_VIT|SCB_INT );
	set_sc( HFLI_FLEET           , SC_FLEET           , SI_BLANK           , SCB_BATK|SCB_WATK|SCB_ASPD );
	set_sc( HFLI_SPEED           , SC_SPEED           , SI_BLANK           , SCB_FLEE );
	set_sc( HAMI_DEFENCE         , SC_DEFENCE         , SI_BLANK           ,
#ifndef RENEWAL
		SCB_DEF
#else
		SCB_VIT
#endif
		);
	set_sc( HAMI_BLOODLUST       , SC_BLOODLUST       , SI_BLANK           , SCB_BATK|SCB_WATK );

	add_sc( MER_CRASH            , SC_STUN            );
	set_sc( MER_PROVOKE          , SC_PROVOKE         , SI_PROVOKE         , SCB_BATK|SCB_WATK|SCB_DEF|SCB_DEF2 );
	add_sc( MS_MAGNUM            , SC_WATK_ELEMENT    );
	add_sc( MER_SIGHT            , SC_SIGHT           );
	set_sc( MER_DECAGI           , SC_DECREASEAGI     , SI_DEC_AGI         , SCB_AGI|SCB_SPEED );
	set_sc( MER_MAGNIFICAT       , SC_MAGNIFICAT      , SI_MAGNIFICAT      , SCB_REGEN );
	add_sc( MER_LEXDIVINA        , SC_SILENCE         );
	add_sc( MA_LANDMINE          , SC_STUN            );
	add_sc( MA_SANDMAN           , SC_SLEEP           );
	add_sc( MA_FREEZINGTRAP      , SC_FREEZE          );
	set_sc( MER_AUTOBERSERK      , SC_AUTOBERSERK     , SI_AUTOBERSERK     , SCB_NONE );
	set_sc( ML_AUTOGUARD         , SC_AUTOGUARD       , SI_AUTOGUARD       , SCB_NONE );
	set_sc( MS_REFLECTSHIELD     , SC_REFLECTSHIELD   , SI_REFLECTSHIELD   , SCB_NONE );
	set_sc( ML_DEFENDER          , SC_DEFENDER        , SI_DEFENDER        , SCB_SPEED|SCB_ASPD );
	set_sc( MS_PARRYING          , SC_PARRYING        , SI_PARRYING        , SCB_NONE );
	set_sc( MS_BERSERK           , SC_BERSERK         , SI_BERSERK         ,
#ifndef RENEWAL
		SCB_MAXHP|SCB_FLEE|SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_MDEF2|SCB_SPEED|SCB_ASPD|SCB_REGEN
#else
		SCB_MAXHP|SCB_SPEED|SCB_ASPD|SCB_REGEN
#endif
		);
	add_sc( ML_SPIRALPIERCE      , SC_STOP            );
	set_sc( MER_QUICKEN          , SC_MERC_QUICKEN    , SI_BLANK           , SCB_ASPD );
	add_sc( ML_DEVOTION          , SC_DEVOTION        );
	set_sc( MER_KYRIE            , SC_KYRIE           , SI_KYRIE           , SCB_NONE );
	set_sc( MER_BLESSING         , SC_BLESSING        , SI_BLESSING        , SCB_STR|SCB_INT|SCB_DEX );
	set_sc( MER_INCAGI           , SC_INCREASEAGI     , SI_INC_AGI         , SCB_AGI|SCB_SPEED );
	set_sc( MER_INVINCIBLEOFF2   , SC_INVINCIBLEOFF   , SI_BLANK           , SCB_SPEED );

	set_sc( GD_LEADERSHIP        , SC_LEADERSHIP      , SI_BLANK           , SCB_STR );
	set_sc( GD_GLORYWOUNDS       , SC_GLORYWOUNDS     , SI_BLANK           , SCB_VIT );
	set_sc( GD_SOULCOLD          , SC_SOULCOLD        , SI_BLANK           , SCB_AGI );
	set_sc( GD_HAWKEYES          , SC_HAWKEYES        , SI_BLANK           , SCB_DEX );

	set_sc( GD_BATTLEORDER       , SC_BATTLEORDERS    , SI_GDSKILL_BATTLEORDER , SCB_STR|SCB_INT|SCB_DEX );
	set_sc( GD_REGENERATION      , SC_REGENERATION    , SI_GDSKILL_REGENERATION, SCB_REGEN );

	set_sc( RK_ENCHANTBLADE      , SC_ENCHANTBLADE      , SI_ENCHANTBLADE      , SCB_NONE );
	add_sc( RK_DRAGONHOWLING     , SC_FEAR              );
	set_sc( RK_DEATHBOUND        , SC_DEATHBOUND        , SI_DEATHBOUND        , SCB_NONE );
	add_sc( RK_WINDCUTTER        , SC_FEAR              );
	add_sc( RK_DRAGONBREATH      , SC_BURNING           );
	add_sc( RK_MILLENNIUMSHIELD  , SC_MILLENNIUMSHIELD  );
	set_sc( RK_REFRESH           , SC_REFRESH           , SI_REFRESH           , SCB_NONE );
	set_sc( RK_GIANTGROWTH       , SC_GIANTGROWTH       , SI_GIANTGROWTH       , SCB_STR  );
	set_sc( RK_STONEHARDSKIN     , SC_STONEHARDSKIN     , SI_STONEHARDSKIN     , SCB_DEF|SCB_MDEF );
	set_sc( RK_VITALITYACTIVATION, SC_VITALITYACTIVATION, SI_VITALITYACTIVATION, SCB_REGEN );
	set_sc( RK_FIGHTINGSPIRIT    , SC_FIGHTINGSPIRIT    , SI_FIGHTINGSPIRIT    , SCB_ASPD );
	set_sc( RK_ABUNDANCE         , SC_ABUNDANCE         , SI_ABUNDANCE         , SCB_NONE );
	set_sc( RK_CRUSHSTRIKE       , SC_CRUSHSTRIKE       , SI_CRUSHSTRIKE       , SCB_NONE );
	add_sc( RK_DRAGONBREATH_WATER, SC_FREEZING          );
	add_sc( RK_LUXANIMA          , SC_LUXANIMA          );

	set_sc_with_vfx( GC_VENOMIMPRESS , SC_VENOMIMPRESS , SI_VENOMIMPRESS       , SCB_NONE );
	set_sc( GC_POISONINGWEAPON   , SC_POISONINGWEAPON  , SI_POISONINGWEAPON    , SCB_NONE );
	set_sc( GC_WEAPONBLOCKING    , SC_WEAPONBLOCKING   , SI_WEAPONBLOCKING     , SCB_NONE );
	set_sc( GC_CLOAKINGEXCEED    , SC_CLOAKINGEXCEED   , SI_CLOAKINGEXCEED     , SCB_SPEED );
	set_sc( GC_HALLUCINATIONWALK , SC_HALLUCINATIONWALK, SI_HALLUCINATIONWALK  , SCB_FLEE );
	set_sc( GC_ROLLINGCUTTER     , SC_ROLLINGCUTTER    , SI_ROLLINGCUTTER      , SCB_NONE );
	set_sc_with_vfx( GC_DARKCROW , SC_DARKCROW         , SI_DARKCROW           , SCB_NONE );

	set_sc( AB_ADORAMUS          , SC_ADORAMUS        , SI_ADORAMUS        , SCB_AGI|SCB_SPEED );
	set_sc( AB_CLEMENTIA         , SC_BLESSING        , SI_BLESSING        , SCB_STR|SCB_INT|SCB_DEX );
	set_sc( AB_CANTO             , SC_INCREASEAGI     , SI_INC_AGI         , SCB_AGI|SCB_SPEED );
	set_sc( AB_EPICLESIS         , SC_EPICLESIS       , SI_EPICLESIS       , SCB_MAXHP );
	set_sc( AB_PRAEFATIO         , SC_KYRIE           , SI_KYRIE           , SCB_NONE );
	set_sc_with_vfx( AB_ORATIO   , SC_ORATIO          , SI_ORATIO          , SCB_NONE );
	set_sc( AB_LAUDAAGNUS        , SC_LAUDAAGNUS      , SI_LAUDAAGNUS      , SCB_MAXHP );
	set_sc( AB_LAUDARAMUS        , SC_LAUDARAMUS      , SI_LAUDARAMUS      , SCB_ALL  );
	set_sc( AB_RENOVATIO         , SC_RENOVATIO       , SI_RENOVATIO       , SCB_NONE );
	set_sc( AB_EXPIATIO          , SC_EXPIATIO        , SI_EXPIATIO        , SCB_NONE );
	set_sc( AB_DUPLELIGHT        , SC_DUPLELIGHT      , SI_DUPLELIGHT      , SCB_NONE );
	set_sc( AB_SECRAMENT         , SC_SECRAMENT       , SI_AB_SECRAMENT    , SCB_NONE );
	set_sc( AB_OFFERTORIUM       , SC_OFFERTORIUM     , SI_OFFERTORIUM     , SCB_NONE );
	add_sc( AB_VITUPERATUM       , SC_AETERNA         );

	add_sc( WL_WHITEIMPRISON      , SC_WHITEIMPRISON  );
	add_sc( WL_FROSTMISTY         , SC_FREEZING       );
	add_sc( WL_JACKFROST          , SC_FREEZE         );
	set_sc( WL_MARSHOFABYSS       , SC_MARSHOFABYSS   , SI_MARSHOFABYSS    , SCB_FLEE|SCB_DEF|SCB_DEF2|SCB_SPEED );
	set_sc( WL_RECOGNIZEDSPELL    , SC_RECOGNIZEDSPELL, SI_RECOGNIZEDSPELL , SCB_MATK );
	add_sc( WL_SIENNAEXECRATE     , SC_STONE          );
	set_sc( WL_STASIS             , SC_STASIS         , SI_STASIS          , SCB_NONE );
	add_sc( WL_CRIMSONROCK        , SC_STUN           );
	add_sc( WL_HELLINFERNO        , SC_BURNING        );
	add_sc( WL_COMET              , SC_BURNING        );
	set_sc( WL_TELEKINESIS_INTENSE, SC_TELEKINESIS_INTENSE, SI_TELEKINESIS_INTENSE, SCB_NONE );

	set_sc( RA_FEARBREEZE        , SC_FEARBREEZE      , SI_FEARBREEZE      , SCB_NONE );
	set_sc( RA_ELECTRICSHOCKER   , SC_ELECTRICSHOCKER , SI_ELECTRICSHOCKER , SCB_NONE );
	set_sc( RA_WUGDASH           , SC_WUGDASH         , SI_WUGDASH         , SCB_SPEED|SCB_DSPD );
	set_sc( RA_WUGBITE           , SC_BITE            , SI_WUGBITE         , SCB_NONE );
	set_sc( RA_CAMOUFLAGE        , SC_CAMOUFLAGE      , SI_CAMOUFLAGE      , SCB_SPEED );
	add_sc( RA_FIRINGTRAP        , SC_BURNING         );
	add_sc( RA_ICEBOUNDTRAP      , SC_FREEZING        );
	set_sc( RA_UNLIMIT           , SC_UNLIMIT         , SI_UNLIMIT         , SCB_NONE );

	add_sc( NC_FLAMELAUNCHER     , SC_BURNING         );
	add_sc( NC_COLDSLOWER        , SC_FREEZE          );
	add_sc( NC_COLDSLOWER        , SC_FREEZING        );
	set_sc( NC_ACCELERATION      , SC_ACCELERATION    , SI_ACCELERATION    , SCB_SPEED );
	set_sc( NC_HOVERING          , SC_HOVERING        , SI_HOVERING        , SCB_SPEED );
	set_sc( NC_SHAPESHIFT        , SC_SHAPESHIFT      , SI_SHAPESHIFT      , SCB_DEF_ELE );
	set_sc( NC_INFRAREDSCAN      , SC_INFRAREDSCAN    , SI_INFRAREDSCAN    , SCB_FLEE );
	set_sc( NC_ANALYZE           , SC_ANALYZE         , SI_ANALYZE         , SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_MDEF2 );
	set_sc( NC_MAGNETICFIELD     , SC_MAGNETICFIELD   , SI_MAGNETICFIELD   , SCB_NONE );
	set_sc( NC_NEUTRALBARRIER    , SC_NEUTRALBARRIER  , SI_NEUTRALBARRIER  , SCB_NONE );
	set_sc_with_vfx( NC_STEALTHFIELD , SC_STEALTHFIELD, SI_STEALTHFIELD    , SCB_SPEED );
	add_sc( NC_MAGMA_ERUPTION    , SC_STUN            );
	add_sc( NC_MAGMA_ERUPTION_DOTDAMAGE, SC_BURNING   );

	set_sc( LG_REFLECTDAMAGE     , SC_REFLECTDAMAGE   , SI_LG_REFLECTDAMAGE, SCB_NONE );
	set_sc( LG_FORCEOFVANGUARD   , SC_FORCEOFVANGUARD , SI_FORCEOFVANGUARD , SCB_MAXHP );
	set_sc( LG_EXEEDBREAK        , SC_EXEEDBREAK      , SI_EXEEDBREAK      , SCB_NONE );
	set_sc( LG_PRESTIGE          , SC_PRESTIGE        , SI_PRESTIGE        , SCB_DEF );
	set_sc( LG_BANDING           , SC_BANDING_DEFENCE , SI_BANDING_DEFENCE , SCB_SPEED );
	set_sc( LG_PIETY             , SC_BENEDICTIO      , SI_BENEDICTIO      , SCB_DEF_ELE );
	set_sc( LG_EARTHDRIVE        , SC_EARTHDRIVE      , SI_EARTHDRIVE      , SCB_DEF|SCB_ASPD );
	set_sc( LG_INSPIRATION       , SC_INSPIRATION     , SI_INSPIRATION     , SCB_MAXHP|SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK|SCB_WATK|SCB_HIT );
	set_sc( LG_KINGS_GRACE       , SC_KINGS_GRACE     , SI_KINGS_GRACE     , SCB_NONE );

	set_sc( SC_REPRODUCE         , SC__REPRODUCE      , SI_REPRODUCE       , SCB_NONE );
	set_sc( SC_AUTOSHADOWSPELL   , SC__AUTOSHADOWSPELL, SI_AUTOSHADOWSPELL , SCB_NONE );
	set_sc( SC_SHADOWFORM        , SC__SHADOWFORM     , SI_SHADOWFORM      , SCB_NONE );
	set_sc( SC_BODYPAINT         , SC__BODYPAINT      , SI_BODYPAINT       , SCB_ASPD );
	set_sc( SC_INVISIBILITY      , SC__INVISIBILITY   , SI_INVISIBILITY    , SCB_ASPD|SCB_CRI|SCB_ATK_ELE );
	set_sc( SC_DEADLYINFECT      , SC__DEADLYINFECT   , SI_DEADLYINFECT    , SCB_NONE );
	set_sc( SC_ENERVATION        , SC__ENERVATION     , SI_ENERVATION      , SCB_BATK|SCB_WATK );
	set_sc( SC_GROOMY            , SC__GROOMY         , SI_GROOMY          , SCB_HIT|SCB_ASPD );
	set_sc( SC_IGNORANCE         , SC__IGNORANCE      , SI_IGNORANCE       , SCB_NONE );
	set_sc( SC_LAZINESS          , SC__LAZINESS       , SI_LAZINESS        , SCB_FLEE|SCB_SPEED );
	set_sc( SC_UNLUCKY           , SC__UNLUCKY        , SI_UNLUCKY         , SCB_CRI|SCB_FLEE2 );
	set_sc( SC_WEAKNESS          , SC__WEAKNESS       , SI_WEAKNESS        , SCB_MAXHP );
	set_sc( SC_STRIPACCESSARY    , SC__STRIPACCESSORY , SI_STRIPACCESSARY  , SCB_INT|SCB_DEX|SCB_LUK );
	set_sc_with_vfx( SC_MANHOLE  , SC__MANHOLE        , SI_MANHOLE         , SCB_NONE );
	add_sc( SC_CHAOSPANIC        , SC_CONFUSION       );
	set_sc( SC_BLOODYLUST        , SC_BERSERK         , SI_BERSERK         ,
#ifndef RENEWAL
		SCB_MAXHP|SCB_FLEE|SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_MDEF2|SCB_SPEED|SCB_ASPD|SCB_REGEN
#else
		SCB_MAXHP|SCB_SPEED|SCB_ASPD|SCB_REGEN
#endif
		);
	add_sc( SC_FEINTBOMB         , SC__FEINTBOMB      );
	add_sc( SC_ESCAPE            , SC_ANKLE           );

	add_sc( SR_DRAGONCOMBO           , SC_STUN               );
	add_sc( SR_EARTHSHAKER           , SC_STUN               );
	set_sc( SR_FALLENEMPIRE          , SC_FALLENEMPIRE       , SI_FALLENEMPIRE          , SCB_NONE );
	set_sc( SR_CRESCENTELBOW         , SC_CRESCENTELBOW      , SI_CRESCENTELBOW         , SCB_NONE );
	set_sc_with_vfx( SR_CURSEDCIRCLE , SC_CURSEDCIRCLE_TARGET, SI_CURSEDCIRCLE_TARGET   , SCB_NONE );
	set_sc( SR_LIGHTNINGWALK         , SC_LIGHTNINGWALK      , SI_LIGHTNINGWALK         , SCB_NONE );
	set_sc( SR_RAISINGDRAGON         , SC_RAISINGDRAGON      , SI_RAISINGDRAGON         , SCB_MAXHP|SCB_MAXSP|SCB_REGEN );
	set_sc( SR_GENTLETOUCH_ENERGYGAIN, SC_GT_ENERGYGAIN      , SI_GENTLETOUCH_ENERGYGAIN, SCB_NONE );
	set_sc( SR_GENTLETOUCH_CHANGE    , SC_GT_CHANGE          , SI_GENTLETOUCH_CHANGE    , SCB_MAXHP|SCB_MDEF|SCB_ASPD );
	set_sc( SR_GENTLETOUCH_REVITALIZE, SC_GT_REVITALIZE      , SI_GENTLETOUCH_REVITALIZE, SCB_MAXHP|SCB_REGEN );
	add_sc( SR_HOWLINGOFLION         , SC_FEAR               );
	set_sc( SR_FLASHCOMBO            , SC_FLASHCOMBO         , SI_FLASHCOMBO            , SCB_NONE );

	set_sc( WA_SWING_DANCE            , SC_SWINGDANCE           , SI_SWING                  , SCB_SPEED|SCB_ASPD );
	set_sc( WA_SYMPHONY_OF_LOVER      , SC_SYMPHONYOFLOVER      , SI_SYMPHONY_LOVE          , SCB_MDEF );
	set_sc( WA_MOONLIT_SERENADE       , SC_MOONLITSERENADE      , SI_MOONLIT_SERENADE       , SCB_MATK );
	set_sc( MI_RUSH_WINDMILL          , SC_RUSHWINDMILL         , SI_RUSH_WINDMILL          , SCB_NONE );
	set_sc( MI_ECHOSONG               , SC_ECHOSONG             , SI_ECHOSONG               , SCB_NONE );
	set_sc( MI_HARMONIZE              , SC_HARMONIZE            , SI_HARMONIZE              , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	set_sc_with_vfx( WM_POEMOFNETHERWORLD, SC_NETHERWORLD       , SI_NETHERWORLD            , SCB_NONE );
	add_sc( WM_VOICEOFSIREN           , SC_VOICEOFSIREN         );
	add_sc( WM_LULLABY_DEEPSLEEP      , SC_DEEPSLEEP            );
	set_sc( WM_SIRCLEOFNATURE         , SC_SIRCLEOFNATURE       , SI_SIRCLEOFNATURE         , SCB_NONE );
	set_sc( WM_GLOOMYDAY              , SC_GLOOMYDAY            , SI_GLOOMYDAY              , SCB_FLEE|SCB_SPEED|SCB_ASPD );
	set_sc( WM_SONG_OF_MANA           , SC_SONGOFMANA           , SI_SONG_OF_MANA           , SCB_NONE );
	set_sc( WM_DANCE_WITH_WUG         , SC_DANCEWITHWUG         , SI_DANCE_WITH_WUG         , SCB_ASPD );
	set_sc( WM_SATURDAY_NIGHT_FEVER   , SC_SATURDAYNIGHTFEVER   , SI_SATURDAY_NIGHT_FEVER   , SCB_FLEE|SCB_DEF|SCB_DEF2|SCB_REGEN );
	set_sc( WM_LERADS_DEW             , SC_LERADSDEW            , SI_LERADS_DEW             , SCB_MAXHP );
	set_sc( WM_MELODYOFSINK           , SC_MELODYOFSINK         , SI_MELODYOFSINK           , SCB_INT );
	set_sc( WM_BEYOND_OF_WARCRY       , SC_BEYONDOFWARCRY       , SI_BEYOND_OF_WARCRY       , SCB_MAXHP|SCB_STR|SCB_CRI );
	set_sc( WM_UNLIMITED_HUMMING_VOICE, SC_UNLIMITEDHUMMINGVOICE, SI_UNLIMITED_HUMMING_VOICE, SCB_NONE );
	set_sc( WM_FRIGG_SONG             , SC_FRIGG_SONG           , SI_FRIGG_SONG             , SCB_MAXHP );

	set_sc( SO_FIREWALK          , SC_PROPERTYWALK    , SI_PROPERTYWALK    , SCB_NONE );
	set_sc( SO_ELECTRICWALK      , SC_PROPERTYWALK    , SI_PROPERTYWALK    , SCB_NONE );
	set_sc( SO_SPELLFIST         , SC_SPELLFIST       , SI_SPELLFIST       , SCB_NONE );
	add_sc( SO_DIAMONDDUST       , SC_CRYSTALIZE      );
	set_sc( SO_CLOUD_KILL        , SC_CLOUD_KILL      , SI_CLOUD_KILL      , SCB_NONE );
	set_sc( SO_STRIKING          , SC_STRIKING        , SI_STRIKING        , SCB_WATK|SCB_CRI );
	set_sc( SO_WARMER            , SC_WARMER          , SI_WARMER          , SCB_NONE );
	set_sc( SO_VACUUM_EXTREME    , SC_VACUUM_EXTREME  , SI_VACUUM_EXTREME  , SCB_NONE );
	add_sc( SO_ARRULLO           , SC_DEEPSLEEP       );
	set_sc( SO_FIRE_INSIGNIA     , SC_FIRE_INSIGNIA   , SI_FIRE_INSIGNIA   , SCB_WATK|SCB_MATK|SCB_ATK_ELE|SCB_REGEN );
	set_sc( SO_WATER_INSIGNIA    , SC_WATER_INSIGNIA  , SI_WATER_INSIGNIA  , SCB_WATK|SCB_ATK_ELE|SCB_REGEN );
	set_sc( SO_WIND_INSIGNIA     , SC_WIND_INSIGNIA   , SI_WIND_INSIGNIA   , SCB_WATK|SCB_ASPD|SCB_ATK_ELE|SCB_REGEN );
	set_sc( SO_EARTH_INSIGNIA    , SC_EARTH_INSIGNIA  , SI_EARTH_INSIGNIA  , SCB_MAXHP|SCB_MAXSP|SCB_WATK|SCB_DEF|SCB_MDEF|SCB_ATK_ELE|SCB_REGEN );

	set_sc( GN_CARTBOOST                  , SC_GN_CARTBOOST   , SI_GN_CARTBOOST               , SCB_SPEED );
	set_sc( GN_THORNS_TRAP                , SC_THORNSTRAP     , SI_THORNS_TRAP                , SCB_NONE );
	set_sc_with_vfx( GN_BLOOD_SUCKER      , SC_BLOODSUCKER    , SI_BLOOD_SUCKER               , SCB_NONE );
	set_sc( GN_SPORE_EXPLOSION            , SC_SPORE_EXPLOSION, SI_SPORE_EXPLOSION            , SCB_NONE );
	set_sc( GN_DEMONIC_FIRE               , SC_DEMONIC_FIRE   , SI_DEMONIC_FIRE               , SCB_NONE );
	set_sc( GN_FIRE_EXPANSION_SMOKE_POWDER, SC_SMOKEPOWDER    , SI_FIRE_EXPANSION_SMOKE_POWDER, SCB_FLEE );
	set_sc( GN_FIRE_EXPANSION_TEAR_GAS    , SC_TEARGAS        , SI_FIRE_EXPANSION_TEAR_GAS    , SCB_HIT|SCB_FLEE );
	set_sc( GN_MANDRAGORA                 , SC_MANDRAGORA     , SI_MANDRAGORA                 , SCB_INT );
	set_sc_with_vfx( GN_ILLUSIONDOPING    , SC_ILLUSIONDOPING , SI_ILLUSIONDOPING             , SCB_HIT );

	add_sc( MH_STAHL_HORN         , SC_STUN            );
	set_sc( MH_ANGRIFFS_MODUS     , SC_ANGRIFFS_MODUS  , SI_ANGRIFFS_MODUS     , SCB_MAXHP|SCB_BATK|SCB_WATK|SCB_FLEE|SCB_DEF );
	set_sc( MH_GOLDENE_FERSE      , SC_GOLDENE_FERSE   , SI_GOLDENE_FERSE      , SCB_FLEE|SCB_ASPD );
	add_sc( MH_STEINWAND          , SC_SAFETYWALL      );
	set_sc( MH_LIGHT_OF_REGENE    , SC_LIGHT_OF_REGENE , SI_LIGHT_OF_REGENE    , SCB_NONE );
	set_sc( MH_OVERED_BOOST       , SC_OVERED_BOOST    , SI_OVERED_BOOST       , SCB_FLEE|SCB_DEF|SCB_ASPD );
	set_sc( MH_VOLCANIC_ASH       , SC_ASH             , SI_VOLCANIC_ASH       , SCB_BATK|SCB_HIT|SCB_FLEE|SCB_DEF|SCB_DEF2 );
	set_sc( MH_GRANITIC_ARMOR     , SC_GRANITIC_ARMOR  , SI_GRANITIC_ARMOR     , SCB_NONE );
	set_sc( MH_MAGMA_FLOW         , SC_MAGMA_FLOW      , SI_MAGMA_FLOW         , SCB_NONE );
	set_sc( MH_PYROCLASTIC        , SC_PYROCLASTIC     , SI_PYROCLASTIC        , SCB_WATK|SCB_ATK_ELE );
	add_sc( MH_LAVA_SLIDE         , SC_BURNING         );
	set_sc( MH_NEEDLE_OF_PARALYZE , SC_PARALYSIS       , SI_NEEDLE_OF_PARALYZE , SCB_DEF|SCB_MDEF );
	add_sc( MH_POISON_MIST        , SC_BLIND           );
	set_sc( MH_PAIN_KILLER        , SC_PAIN_KILLER     , SI_PAIN_KILLER     , SCB_ASPD );
	set_sc( MH_SILENT_BREEZE      , SC_SILENCE         , SI_SILENT_BREEZE   , SCB_NONE );
	add_sc( MH_STYLE_CHANGE       , SC_STYLE_CHANGE    );
	add_sc( MH_SILVERVEIN_RUSH    , SC_STUN            );
	add_sc( MH_MIDNIGHT_FRENZY    , SC_FEAR            );
	set_sc( MH_TINDER_BREAKER     , SC_TINDER_BREAKER  , SI_TINDER_BREAKER  , SCB_FLEE );
	set_sc( MH_CBC                , SC_CBC             , SI_CBC             , SCB_NONE );
	set_sc( MH_EQC                , SC_EQC             , SI_EQC             , SCB_MAXHP|SCB_DEF2 );

	set_sc( EL_CIRCLE_OF_FIRE  , SC_CIRCLE_OF_FIRE_OPTION, SI_CIRCLE_OF_FIRE_OPTION, SCB_NONE );
	set_sc( EL_FIRE_CLOAK      , SC_FIRE_CLOAK_OPTION    , SI_FIRE_CLOAK_OPTION    , SCB_ALL );
	set_sc( EL_WATER_SCREEN    , SC_WATER_SCREEN_OPTION  , SI_WATER_SCREEN_OPTION  , SCB_NONE );
	set_sc( EL_WATER_DROP      , SC_WATER_DROP_OPTION    , SI_WATER_DROP_OPTION    , SCB_ALL );
	set_sc( EL_WATER_BARRIER   , SC_WATER_BARRIER        , SI_WATER_BARRIER        , SCB_FLEE );
	set_sc( EL_WIND_STEP       , SC_WIND_STEP_OPTION     , SI_WIND_STEP_OPTION     , SCB_FLEE|SCB_SPEED );
	set_sc( EL_WIND_CURTAIN    , SC_WIND_CURTAIN_OPTION  , SI_WIND_CURTAIN_OPTION  , SCB_ALL );
	set_sc( EL_ZEPHYR          , SC_ZEPHYR               , SI_ZEPHYR               , SCB_FLEE );
	set_sc( EL_SOLID_SKIN      , SC_SOLID_SKIN_OPTION    , SI_SOLID_SKIN_OPTION    , SCB_MAXHP|SCB_DEF );
	set_sc( EL_STONE_SHIELD    , SC_STONE_SHIELD_OPTION  , SI_STONE_SHIELD_OPTION  , SCB_ALL );
	set_sc( EL_POWER_OF_GAIA   , SC_POWER_OF_GAIA        , SI_POWER_OF_GAIA        , SCB_MAXHP|SCB_DEF|SCB_SPEED );
	set_sc( EL_PYROTECHNIC     , SC_PYROTECHNIC_OPTION   , SI_PYROTECHNIC_OPTION   , SCB_NONE );
	set_sc( EL_HEATER          , SC_HEATER_OPTION        , SI_HEATER_OPTION        , SCB_NONE );
	set_sc( EL_TROPIC          , SC_TROPIC_OPTION        , SI_TROPIC_OPTION        , SCB_NONE );
	set_sc( EL_AQUAPLAY        , SC_AQUAPLAY_OPTION      , SI_AQUAPLAY_OPTION      , SCB_MATK );
	set_sc( EL_COOLER          , SC_COOLER_OPTION        , SI_COOLER_OPTION        , SCB_MATK );
	set_sc( EL_CHILLY_AIR      , SC_CHILLY_AIR_OPTION    , SI_CHILLY_AIR_OPTION    , SCB_MATK );
	set_sc( EL_GUST            , SC_GUST_OPTION          , SI_GUST_OPTION          , SCB_ASPD );
	set_sc( EL_BLAST           , SC_BLAST_OPTION         , SI_BLAST_OPTION         , SCB_ASPD );
	set_sc( EL_WILD_STORM      , SC_WILD_STORM_OPTION    , SI_WILD_STORM_OPTION    , SCB_ASPD );
	set_sc( EL_PETROLOGY       , SC_PETROLOGY_OPTION     , SI_PETROLOGY_OPTION     , SCB_MAXHP );
	set_sc( EL_CURSED_SOIL     , SC_CURSED_SOIL_OPTION   , SI_CURSED_SOIL_OPTION   , SCB_MAXHP );
	set_sc( EL_UPHEAVAL        , SC_UPHEAVAL_OPTION      , SI_UPHEAVAL_OPTION      , SCB_MAXHP );
	set_sc( EL_TIDAL_WEAPON    , SC_TIDAL_WEAPON_OPTION  , SI_TIDAL_WEAPON_OPTION  , SCB_ATK_ELE );
	set_sc( EL_ROCK_CRUSHER    , SC_ROCK_CRUSHER         , SI_ROCK_CRUSHER         , SCB_DEF );
	set_sc( EL_ROCK_CRUSHER_ATK, SC_ROCK_CRUSHER_ATK     , SI_ROCK_CRUSHER_ATK     , SCB_SPEED );

	add_sc( KO_YAMIKUMO        , SC_HIDING               );
	set_sc_with_vfx( KO_JYUMONJIKIRI, SC_JYUMONJIKIRI    , SI_KO_JYUMONJIKIRI      , SCB_NONE );
	add_sc( KO_MAKIBISHI       , SC_STUN                 );
	set_sc( KO_MEIKYOUSISUI    , SC_MEIKYOUSISUI         , SI_MEIKYOUSISUI         , SCB_NONE );
	set_sc( KO_KYOUGAKU        , SC_KYOUGAKU             , SI_KYOUGAKU             , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	add_sc( KO_JYUSATSU        , SC_CURSE                );
	set_sc( KO_ZENKAI          , SC_ZENKAI               , SI_ZENKAI               , SCB_NONE );
	add_sc( KO_GENWAKU         , SC_CONFUSION            );
	set_sc( KO_IZAYOI          , SC_IZAYOI               , SI_IZAYOI               , SCB_MATK );
	set_sc( KG_KYOMU           , SC_KYOMU                , SI_KYOMU                , SCB_NONE );
	set_sc( KG_KAGEMUSYA       , SC_KAGEMUSYA            , SI_KAGEMUSYA            , SCB_NONE );
	set_sc( KG_KAGEHUMI        , SC_KAGEHUMI             , SI_KG_KAGEHUMI          , SCB_NONE );
	set_sc( OB_ZANGETSU        , SC_ZANGETSU             , SI_ZANGETSU             , SCB_NONE );
	set_sc_with_vfx( OB_AKAITSUKI, SC_AKAITSUKI          , SI_AKAITSUKI            , SCB_NONE );
	set_sc( OB_OBOROGENSOU     , SC_GENSOU               , SI_GENSOU               , SCB_NONE );

	set_sc( ALL_FULL_THROTTLE  , SC_FULL_THROTTLE        , SI_FULL_THROTTLE        , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK|SCB_SPEED );

	add_sc( ALL_REVERSEORCISH  , SC_ORCISH               );
	set_sc( ALL_ANGEL_PROTECT  , SC_ANGEL_PROTECT        , SI_ANGEL_PROTECT        , SCB_REGEN );

	add_sc( NPC_WIDEHEALTHFEAR    , SC_FEAR              );
	add_sc( NPC_WIDEBODYBURNNING  , SC_BURNING           );
	add_sc( NPC_WIDEFROSTMISTY    , SC_FREEZING          );
	add_sc( NPC_WIDECOLD          , SC_CRYSTALIZE        );
	add_sc( NPC_WIDE_DEEP_SLEEP   , SC_DEEPSLEEP         );
	add_sc( NPC_WIDESIREN         , SC_VOICEOFSIREN      );
	add_sc( NPC_COMET             , SC_BURNING           );
	set_sc_with_vfx( NPC_MAXPAIN  , SC_MAXPAIN           , SI_MAXPAIN               , SCB_NONE );
	add_sc( NPC_JACKFROST         , SC_FREEZE            );
	set_sc( NPC_WIDEWEB           , SC_WIDEWEB           , SI_WIDEWEB               , SCB_FLEE );
	set_sc_with_vfx( NPC_FIRESTORM, SC_BURNT             , SI_BURNT                 , SCB_NONE );
	add_sc( NPC_GUIDEDATTACK      , SC_GUIDEDATTACK      );

	add_sc( RL_MASS_SPIRAL      , SC_BLEEDING           );
	add_sc( RL_HAMMER_OF_GOD    , SC_STUN               );
	set_sc( RL_B_TRAP           , SC_B_TRAP             , SI_B_TRAP               , SCB_SPEED         );
	set_sc( RL_E_CHAIN          , SC_E_CHAIN            , SI_E_CHAIN              , SCB_NONE          );
	set_sc_with_vfx( RL_C_MARKER, SC_C_MARKER           , SI_C_MARKER             , SCB_FLEE          );
	set_sc_with_vfx( RL_H_MINE  , SC_H_MINE             , SI_H_MINE               , SCB_NONE          );
	set_sc_with_vfx( RL_H_MINE  , SC_H_MINE_EXPLOSION   , SI_H_MINE_EXPLOSION     , SCB_NONE          );
	set_sc( RL_P_ALTER          , SC_P_ALTER            , SI_P_ALTER              , SCB_NONE          );
	set_sc( RL_FALLEN_ANGEL     , SC_FALLEN_ANGEL       , SI_FALLEN_ANGEL         , SCB_NONE          );
	set_sc( RL_SLUGSHOT         , SC_STUN               , SI_SLUGSHOT             , SCB_NONE          );
	set_sc_with_vfx( RL_AM_BLAST, SC_ANTI_M_BLAST       , SI_ANTI_M_BLAST         , SCB_NONE          );
	set_sc( RL_HEAT_BARREL      , SC_HEAT_BARREL        , SI_HEAT_BARREL          , SCB_HIT|SCB_ASPD  );

	set_sc( SU_HIDE                 , SC_SUHIDE       , SI_SUHIDE          , SCB_NONE  );
	add_sc( SU_SCRATCH              , SC_BLEEDING     );
	set_sc( SU_STOOP                , SC_SU_STOOP     , SI_SU_STOOP        , SCB_NONE  );
	add_sc( SU_SV_STEMSPEAR         , SC_BLEEDING     );
	set_sc( SU_CN_POWDERING         , SC_CATNIPPOWDER , SI_CATNIPPOWDER    , SCB_BATK|SCB_MATK|SCB_SPEED|SCB_REGEN );
	set_sc_with_vfx( SU_SV_ROOTTWIST, SC_SV_ROOTTWIST , SI_SV_ROOTTWIST    , SCB_NONE  );
	set_sc( SU_SCAROFTAROU          , SC_BITESCAR     , SI_BITESCAR        , SCB_NONE  );
	set_sc( SU_ARCLOUSEDASH         , SC_ARCLOUSEDASH , SI_ARCLOUSEDASH    , SCB_AGI|SCB_SPEED );
	set_sc( SU_TUNAPARTY            , SC_TUNAPARTY    , SI_TUNAPARTY       , SCB_NONE  );
	set_sc( SU_BUNCHOFSHRIMP        , SC_SHRIMP       , SI_SHRIMP          , SCB_MATK  );
	set_sc( SU_FRESHSHRIMP          , SC_FRESHSHRIMP  , SI_FRESHSHRIMP     , SCB_NONE  );
	set_sc( SU_HISS                 , SC_HISS         , SI_HISS            , SCB_FLEE2 );
	set_sc( SU_NYANGGRASS           , SC_NYANGGRASS   , SI_NYANGGRASS      , SCB_DEF|SCB_MDEF );
	set_sc( SU_GROOMING             , SC_GROOMING     , SI_GROOMING        , SCB_FLEE  );
	set_sc( SU_SHRIMPARTY           , SC_SHRIMPBLESSING, SI_PROTECTIONOFSHRIMP, SCB_REGEN );
	set_sc( SU_CHATTERING           , SC_CHATTERING   , SI_CHATTERING      , SCB_WATK|SCB_MATK );

	set_sc( WE_CHEERUP              , SC_CHEERUP      , SI_CHEERUP         , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );

	//Storing the target job rather than simply SC_SPIRIT simplifies code later on
	SkillStatusChangeTable[SL_ALCHEMIST]   = (sc_type)MAPID_ALCHEMIST,
	SkillStatusChangeTable[SL_MONK]        = (sc_type)MAPID_MONK,
	SkillStatusChangeTable[SL_STAR]        = (sc_type)MAPID_STAR_GLADIATOR,
	SkillStatusChangeTable[SL_SAGE]        = (sc_type)MAPID_SAGE,
	SkillStatusChangeTable[SL_CRUSADER]    = (sc_type)MAPID_CRUSADER,
	SkillStatusChangeTable[SL_SUPERNOVICE] = (sc_type)MAPID_SUPER_NOVICE,
	SkillStatusChangeTable[SL_KNIGHT]      = (sc_type)MAPID_KNIGHT,
	SkillStatusChangeTable[SL_WIZARD]      = (sc_type)MAPID_WIZARD,
	SkillStatusChangeTable[SL_PRIEST]      = (sc_type)MAPID_PRIEST,
	SkillStatusChangeTable[SL_BARDDANCER]  = (sc_type)MAPID_BARDDANCER,
	SkillStatusChangeTable[SL_ROGUE]       = (sc_type)MAPID_ROGUE,
	SkillStatusChangeTable[SL_ASSASIN]     = (sc_type)MAPID_ASSASSIN,
	SkillStatusChangeTable[SL_BLACKSMITH]  = (sc_type)MAPID_BLACKSMITH,
	SkillStatusChangeTable[SL_HUNTER]      = (sc_type)MAPID_HUNTER,
	SkillStatusChangeTable[SL_SOULLINKER]  = (sc_type)MAPID_SOUL_LINKER,
	SkillStatusChangeTable[SL_NINJA]       = (sc_type)MAPID_NINJA,
	SkillStatusChangeTable[SL_GUNNER]      = (sc_type)MAPID_GUNSLINGER,

	//Status that don't have a skill associated
	StatusIconChangeTable[SC_WEIGHT50] = SI_WEIGHTOVER50;
	StatusIconChangeTable[SC_WEIGHT90] = SI_WEIGHTOVER90;
	StatusIconChangeTable[SC_ASPDPOTION0] = SI_ATTHASTE_POTION1;
	StatusIconChangeTable[SC_ASPDPOTION1] = SI_ATTHASTE_POTION2;
	StatusIconChangeTable[SC_ASPDPOTION2] = SI_ATTHASTE_POTION3;
	StatusIconChangeTable[SC_ASPDPOTION3] = SI_ATTHASTE_INFINITY;
	StatusIconChangeTable[SC_SPEEDUP0] = SI_MOVHASTE_HORSE;
	StatusIconChangeTable[SC_SPEEDUP1] = SI_MOVHASTE_POTION;
	StatusIconChangeTable[SC_CHASEWALK2] = SI_CHASEWALK2;
	StatusIconChangeTable[SC_MIRACLE] = SI_SOULLINK;
	StatusIconChangeTable[SC_INTRAVISION] = SI_CLAIRVOYANCE;
	StatusIconChangeTable[SC_STRFOOD] = SI_FOOD_STR;
	StatusIconChangeTable[SC_AGIFOOD] = SI_FOOD_AGI;
	StatusIconChangeTable[SC_VITFOOD] = SI_FOOD_VIT;
	StatusIconChangeTable[SC_INTFOOD] = SI_FOOD_INT;
	StatusIconChangeTable[SC_DEXFOOD] = SI_FOOD_DEX;
	StatusIconChangeTable[SC_LUKFOOD] = SI_FOOD_LUK;
	StatusIconChangeTable[SC_CRIFOOD] = SI_FOOD_CRITICALSUCCESSVALUE;
	StatusIconChangeTable[SC_HITFOOD] = SI_FOOD_BASICHIT;
	StatusIconChangeTable[SC_FLEEFOOD] = SI_FOOD_BASICAVOIDANCE;
	StatusIconChangeTable[SC_ATKPOTION] = SI_PLUSATTACKPOWER;
	StatusIconChangeTable[SC_MATKPOTION] = SI_PLUSMAGICPOWER;
	StatusIconChangeTable[SC_FOOD_STR_CASH] = SI_FOOD_STR_CASH;
	StatusIconChangeTable[SC_FOOD_AGI_CASH] = SI_FOOD_AGI_CASH;
	StatusIconChangeTable[SC_FOOD_VIT_CASH] = SI_FOOD_VIT_CASH;
	StatusIconChangeTable[SC_FOOD_DEX_CASH] = SI_FOOD_DEX_CASH;
	StatusIconChangeTable[SC_FOOD_INT_CASH] = SI_FOOD_INT_CASH;
	StatusIconChangeTable[SC_FOOD_LUK_CASH] = SI_FOOD_LUK_CASH;
	StatusIconChangeTable[SC_EXPBOOST] = SI_CASH_PLUSEXP;
	StatusIconChangeTable[SC_ITEMBOOST] = SI_CASH_RECEIVEITEM;
	StatusIconChangeTable[SC_JEXPBOOST] = SI_CASH_PLUSONLYJOBEXP;
	StatusIconChangeTable[SC_LIFEINSURANCE] = SI_CASH_DEATHPENALTY;
	StatusIconChangeTable[SC_BOSSMAPINFO] = SI_CASH_BOSS_ALARM;
	StatusIconChangeTable[SC_DEF_RATE] = SI_PROTECT_DEF;
	StatusIconChangeTable[SC_MDEF_RATE] = SI_PROTECT_MDEF;
	StatusIconChangeTable[SC_INCCRI] = SI_CRITICALPERCENT;
	StatusIconChangeTable[SC_INCFLEE2] = SI_PLUSAVOIDVALUE;
	StatusIconChangeTable[SC_INCHEALRATE] = SI_HEALPLUS;
	StatusIconChangeTable[SC_S_LIFEPOTION] = SI_S_LIFEPOTION;
	StatusIconChangeTable[SC_L_LIFEPOTION] = SI_L_LIFEPOTION;
	StatusIconChangeTable[SC_SPCOST_RATE] = SI_ATKER_BLOOD;
	StatusIconChangeTable[SC_COMMONSC_RESIST] = SI_TARGET_BLOOD;
	StatusIconChangeTable[SC_MERC_FLEEUP] = SI_MER_FLEE;
	StatusIconChangeTable[SC_MERC_ATKUP] = SI_MER_ATK;
	StatusIconChangeTable[SC_MERC_HPUP] = SI_MER_HP;
	StatusIconChangeTable[SC_MERC_SPUP] = SI_MER_SP;
	StatusIconChangeTable[SC_MERC_HITUP] = SI_MER_HIT;
	StatusIconChangeTable[SC_SPHERE_1] = SI_SUMMON1;
	StatusIconChangeTable[SC_SPHERE_2] = SI_SUMMON2;
	StatusIconChangeTable[SC_SPHERE_3] = SI_SUMMON3;
	StatusIconChangeTable[SC_SPHERE_4] = SI_SUMMON4;
	StatusIconChangeTable[SC_SPHERE_5] = SI_SUMMON5;
	StatusIconChangeTable[SC_SPELLBOOK1] = SI_SPELLBOOK1;
	StatusIconChangeTable[SC_SPELLBOOK2] = SI_SPELLBOOK2;
	StatusIconChangeTable[SC_SPELLBOOK3] = SI_SPELLBOOK3;
	StatusIconChangeTable[SC_SPELLBOOK4] = SI_SPELLBOOK4;
	StatusIconChangeTable[SC_SPELLBOOK5] = SI_SPELLBOOK5;
	StatusIconChangeTable[SC_SPELLBOOK6] = SI_SPELLBOOK6;
	StatusIconChangeTable[SC_MAXSPELLBOOK] = SI_SPELLBOOK7;
	StatusIconChangeTable[SC_FREEZE_SP] = SI_FREEZE_SP;
	StatusIconChangeTable[SC_NEUTRALBARRIER_MASTER] = SI_NEUTRALBARRIER_MASTER;
	StatusIconChangeTable[SC_STEALTHFIELD_MASTER] = SI_STEALTHFIELD_MASTER;
	StatusIconChangeTable[SC_OVERHEAT] = SI_OVERHEAT;
	StatusIconChangeTable[SC_OVERHEAT_LIMITPOINT] = SI_OVERHEAT_LIMITPOINT;
	StatusIconChangeTable[SC_HALLUCINATIONWALK_POSTDELAY] = SI_HALLUCINATIONWALK_POSTDELAY;
	StatusIconChangeTable[SC_TOXIN] = SI_TOXIN;
	StatusIconChangeTable[SC_PARALYSE] = SI_PARALYSE;
	StatusIconChangeTable[SC_VENOMBLEED] = SI_VENOMBLEED;
	StatusIconChangeTable[SC_MAGICMUSHROOM] = SI_MAGICMUSHROOM;
	StatusIconChangeTable[SC_DEATHHURT] = SI_DEATHHURT;
	StatusIconChangeTable[SC_PYREXIA] = SI_PYREXIA;
	StatusIconChangeTable[SC_OBLIVIONCURSE] = SI_OBLIVIONCURSE;
	StatusIconChangeTable[SC_LEECHESEND] = SI_LEECHESEND;
	StatusIconChangeTable[SC_BANDING] = SI_BANDING;
	StatusIconChangeTable[SC_SHIELDSPELL_DEF] = SI_SHIELDSPELL_DEF;
	StatusIconChangeTable[SC_SHIELDSPELL_MDEF] = SI_SHIELDSPELL_MDEF;
	StatusIconChangeTable[SC_SHIELDSPELL_REF] = SI_SHIELDSPELL_REF;
	StatusIconChangeTable[SC_SITDOWN_FORCE] = SI_SITDOWN_FORCE;
	StatusIconChangeTable[SC_CURSEDCIRCLE_ATKER] = SI_CURSEDCIRCLE_ATKER;
	StatusIconChangeTable[SC__BLOODYLUST] = SI_BLOODYLUST;
	StatusIconChangeTable[SC_STOMACHACHE] = SI_STOMACHACHE;
	StatusIconChangeTable[SC_MYSTERIOUS_POWDER] = SI_MYSTERIOUS_POWDER;
	StatusIconChangeTable[SC_MELON_BOMB] = SI_MELON_BOMB;
	StatusIconChangeTable[SC_BANANA_BOMB] = SI_BANANA_BOMB;
	StatusIconChangeTable[SC_BANANA_BOMB_SITDOWN] = SI_BANANA_BOMB_SITDOWN_POSTDELAY;
	StatusIconChangeTable[SC_PROMOTE_HEALTH_RESERCH] = SI_PROMOTE_HEALTH_RESERCH;
	StatusIconChangeTable[SC_ENERGY_DRINK_RESERCH] = SI_ENERGY_DRINK_RESERCH;
	StatusIconChangeTable[SC_EXTRACT_WHITE_POTION_Z] = SI_EXTRACT_WHITE_POTION_Z;
	StatusIconChangeTable[SC_VITATA_500] = SI_VITATA_500;
	StatusIconChangeTable[SC_EXTRACT_SALAMINE_JUICE] = SI_EXTRACT_SALAMINE_JUICE;
	StatusIconChangeTable[SC_BOOST500] = SI_BOOST500;
	StatusIconChangeTable[SC_FULL_SWING_K] = SI_FULL_SWING_K;
	StatusIconChangeTable[SC_MANA_PLUS] = SI_MANA_PLUS;
	StatusIconChangeTable[SC_MUSTLE_M] = SI_MUSTLE_M;
	StatusIconChangeTable[SC_LIFE_FORCE_F] = SI_LIFE_FORCE_F;
	StatusIconChangeTable[SC_SAVAGE_STEAK] = SI_SAVAGE_STEAK;
	StatusIconChangeTable[SC_COCKTAIL_WARG_BLOOD] = SI_COCKTAIL_WARG_BLOOD;
	StatusIconChangeTable[SC_MINOR_BBQ] = SI_MINOR_BBQ;
	StatusIconChangeTable[SC_SIROMA_ICE_TEA] = SI_SIROMA_ICE_TEA;
	StatusIconChangeTable[SC_DROCERA_HERB_STEAMED] = SI_DROCERA_HERB_STEAMED;
	StatusIconChangeTable[SC_PUTTI_TAILS_NOODLES] = SI_PUTTI_TAILS_NOODLES;
	StatusIconChangeTable[SC_CIRCLE_OF_FIRE] = SI_CIRCLE_OF_FIRE;
	StatusIconChangeTable[SC_FIRE_CLOAK] = SI_FIRE_CLOAK;
	StatusIconChangeTable[SC_WATER_SCREEN] = SI_WATER_SCREEN;
	StatusIconChangeTable[SC_WATER_DROP] = SI_WATER_DROP;
	StatusIconChangeTable[SC_WIND_STEP] = SI_WIND_STEP;
	StatusIconChangeTable[SC_WIND_CURTAIN] = SI_WIND_CURTAIN;
	StatusIconChangeTable[SC_SOLID_SKIN] = SI_SOLID_SKIN;
	StatusIconChangeTable[SC_STONE_SHIELD] = SI_STONE_SHIELD;
	StatusIconChangeTable[SC_PYROTECHNIC] = SI_PYROTECHNIC;
	StatusIconChangeTable[SC_HEATER] = SI_HEATER;
	StatusIconChangeTable[SC_TROPIC] = SI_TROPIC;
	StatusIconChangeTable[SC_AQUAPLAY] = SI_AQUAPLAY;
	StatusIconChangeTable[SC_COOLER] = SI_COOLER;
	StatusIconChangeTable[SC_CHILLY_AIR] = SI_CHILLY_AIR;
	StatusIconChangeTable[SC_GUST] = SI_GUST;
	StatusIconChangeTable[SC_BLAST] = SI_BLAST;
	StatusIconChangeTable[SC_WILD_STORM] = SI_WILD_STORM;
	StatusIconChangeTable[SC_PETROLOGY] = SI_PETROLOGY;
	StatusIconChangeTable[SC_CURSED_SOIL] = SI_CURSED_SOIL;
	StatusIconChangeTable[SC_UPHEAVAL] = SI_UPHEAVAL;
	StatusIconChangeTable[SC_TIDAL_WEAPON] = SI_TIDAL_WEAPON;
	StatusIconChangeTable[SC_REBOUND] = SI_REBOUND;
	StatusIconChangeTable[SC_DEFSET_NUM] = SI_SET_NUM_DEF;
	StatusIconChangeTable[SC_MDEFSET_NUM] = SI_SET_NUM_MDEF;
	StatusIconChangeTable[SC_DEFSET_PER] = SI_SET_PER_DEF;
	StatusIconChangeTable[SC_MDEFSET_PER] = SI_SET_PER_MDEF;
	StatusIconChangeTable[SC_MONSTER_TRANSFORM] = SI_MONSTER_TRANSFORM;
	StatusIconChangeTable[SC_ACTIVE_MONSTER_TRANSFORM] = SI_ACTIVE_MONSTER_TRANSFORM;
	StatusIconChangeTable[SC_ALL_RIDING] = SI_ALL_RIDING;
	StatusIconChangeTable[SC_PUSH_CART] = SI_ON_PUSH_CART;
	StatusIconChangeTable[SC_MTF_ASPD] = SI_MTF_ASPD;
	StatusIconChangeTable[SC_MTF_RANGEATK] = SI_MTF_RANGEATK;
	StatusIconChangeTable[SC_MTF_MATK] = SI_MTF_MATK;
	StatusIconChangeTable[SC_MTF_MLEATKED] = SI_MTF_MLEATKED;
	StatusIconChangeTable[SC_MTF_CRIDAMAGE] = SI_MTF_CRIDAMAGE;
	StatusIconChangeTable[SC_MOONSTAR] = SI_MOONSTAR;
	StatusIconChangeTable[SC_SUPER_STAR] = SI_SUPER_STAR;
	StatusIconChangeTable[SC_QD_SHOT_READY] = SI_E_QD_SHOT_READY;
	StatusIconChangeTable[SC_STRANGELIGHTS] = SI_STRANGELIGHTS;
	StatusIconChangeTable[SC_DECORATION_OF_MUSIC] = SI_DECORATION_OF_MUSIC;
	StatusIconChangeTable[SC_QUEST_BUFF1] = SI_QUEST_BUFF1;
	StatusIconChangeTable[SC_QUEST_BUFF2] = SI_QUEST_BUFF2;
	StatusIconChangeTable[SC_QUEST_BUFF3] = SI_QUEST_BUFF3;
	StatusIconChangeTable[SC_MTF_ASPD2] = SI_MTF_ASPD2;
	StatusIconChangeTable[SC_MTF_RANGEATK2] = SI_MTF_RANGEATK2;
	StatusIconChangeTable[SC_MTF_MATK2] = SI_MTF_MATK2;
	StatusIconChangeTable[SC_ALMIGHTY] = SI_ALMIGHTY;
	StatusIconChangeTable[SC_JP_EVENT04] = SI_JP_EVENT04;
	StatusIconChangeTable[SC_MTF_HITFLEE] = SI_MTF_HITFLEE;
	StatusIconChangeTable[SC_MTF_MHP] = SI_MTF_MHP;
	StatusIconChangeTable[SC_MTF_MSP] = SI_MTF_MSP;
	StatusIconChangeTable[SC_MTF_PUMPKIN] = SI_MTF_PUMPKIN;
	StatusIconChangeTable[SC_NORECOVER_STATE] = SI_HANDICAPSTATE_NORECOVER;
	StatusIconChangeTable[SC_ATTHASTE_CASH] = SI_ATTHASTE_CASH;
	StatusIconChangeTable[SC_REUSE_LIMIT_A] = SI_REUSE_LIMIT_A;
	StatusIconChangeTable[SC_REUSE_LIMIT_B] = SI_REUSE_LIMIT_B;
	StatusIconChangeTable[SC_REUSE_LIMIT_C] = SI_REUSE_LIMIT_C;
	StatusIconChangeTable[SC_REUSE_LIMIT_D] = SI_REUSE_LIMIT_D;
	StatusIconChangeTable[SC_REUSE_LIMIT_E] = SI_REUSE_LIMIT_E;
	StatusIconChangeTable[SC_REUSE_LIMIT_F] = SI_REUSE_LIMIT_F;
	StatusIconChangeTable[SC_REUSE_LIMIT_G] = SI_REUSE_LIMIT_G;
	StatusIconChangeTable[SC_REUSE_LIMIT_H] = SI_REUSE_LIMIT_H;
	StatusIconChangeTable[SC_REUSE_MILLENNIUMSHIELD] = SI_REUSE_MILLENNIUMSHIELD;
	StatusIconChangeTable[SC_REUSE_CRUSHSTRIKE] = SI_REUSE_CRUSHSTRIKE;
	StatusIconChangeTable[SC_REUSE_REFRESH] = SI_REUSE_REFRESH;
	StatusIconChangeTable[SC_REUSE_STORMBLAST] = SI_REUSE_STORMBLAST;
	StatusIconChangeTable[SC_ALL_RIDING_REUSE_LIMIT] = SI_ALL_RIDING_REUSE_LIMIT;
	StatusIconChangeTable[SC_REUSE_LIMIT_MTF] = SI_REUSE_LIMIT_MTF;
	StatusIconChangeTable[SC_REUSE_LIMIT_ECL] = SI_REUSE_LIMIT_ECL;
	StatusIconChangeTable[SC_REUSE_LIMIT_RECALL] = SI_REUSE_LIMIT_RECALL;
	StatusIconChangeTable[SC_REUSE_LIMIT_ASPD_POTION] = SI_REUSE_LIMIT_ASPD_POTION;
	StatusIconChangeTable[SC_SPRITEMABLE] = SI_SPRITEMABLE;
	StatusIconChangeTable[SC_SOULATTACK] = SI_SOULATTACK;
	StatusIconChangeTable[SC_SONIC_CLAW_POSTDELAY] = SI_SONIC_CLAW_POSTDELAY;
	StatusIconChangeTable[SC_SILVERVEIN_RUSH_POSTDELAY] = SI_SILVERVEIN_RUSH_POSTDELAY;
	StatusIconChangeTable[SC_MIDNIGHT_FRENZY_POSTDELAY] = SI_MIDNIGHT_FRENZY_POSTDELAY;
	StatusIconChangeTable[SC_TINDER_BREAKER_POSTDELAY] = SI_TINDER_BREAKER_POSTDELAY;
	StatusIconChangeTable[SC_CBC_POSTDELAY] = SI_CBC_POSTDELAY;
	StatusIconChangeTable[SC_TELEPORT_FIXEDCASTINGDELAY] = SI_TELEPORT_FIXEDCASTINGDELAY;
	StatusIconChangeTable[SC_HAT_EFFECT] = SI_HAT_EFFECT;
	StatusIconChangeTable[SC_QSCARABA] = SI_QSCARABA;
	StatusIconChangeTable[SC_LJOSALFAR] = SI_LJOSALFAR;
	StatusIconChangeTable[SC_MAPLE_FALLS] = SI_MAPLE_FALLS;
	StatusIconChangeTable[SC_MERMAID_LONGING] = SI_MERMAID_LONGING;
	StatusIconChangeTable[SC_TIME_ACCESSORY] = SI_TIME_ACCESSORY;
	StatusIconChangeTable[SC_GEFFEN_MAGIC1] = SI_GEFFEN_MAGIC1;
	StatusIconChangeTable[SC_GEFFEN_MAGIC2] = SI_GEFFEN_MAGIC2;
	StatusIconChangeTable[SC_GEFFEN_MAGIC3] = SI_GEFFEN_MAGIC3;
	StatusIconChangeTable[SC_FENRIR_CARD] = SI_FENRIR_CARD;
	StatusIconChangeTable[SC_CLAN_INFO] = SI_CLAN_INFO;
	StatusIconChangeTable[SC_SWORDCLAN] = SI_SWORDCLAN;
	StatusIconChangeTable[SC_ARCWANDCLAN] = SI_ARCWANDCLAN;
	StatusIconChangeTable[SC_GOLDENMACECLAN] = SI_GOLDENMACECLAN;
	StatusIconChangeTable[SC_CROSSBOWCLAN] = SI_CROSSBOWCLAN;
	StatusIconChangeTable[SC_JUMPINGCLAN] = SI_JUMPINGCLAN;
	StatusIconChangeTable[SC_RAY_OF_PROTECTION] = SI_RAY_OF_PROTECTION;
	StatusIconChangeTable[SC_GVG_GIANT] = SI_GVG_GIANT;
	StatusIconChangeTable[SC_GVG_GOLEM] = SI_GVG_GOLEM;
	StatusIconChangeTable[SC_GVG_STUN] = SI_GVG_STUN;
	StatusIconChangeTable[SC_GVG_STONE] = SI_GVG_STONE;
	StatusIconChangeTable[SC_GVG_FREEZ] = SI_GVG_FREEZ;
	StatusIconChangeTable[SC_GVG_SLEEP] = SI_GVG_SLEEP;
	StatusIconChangeTable[SC_GVG_CURSE] = SI_GVG_CURSE;
	StatusIconChangeTable[SC_GVG_SILENCE] = SI_GVG_SILENCE;
	StatusIconChangeTable[SC_GVG_BLIND] = SI_GVG_BLIND;
	StatusIconChangeTable[SC_DAILYSENDMAILCNT] = SI_DAILYSENDMAILCNT;
	StatusIconChangeTable[SC_DORAM_BUF_01] = SI_DORAM_BUF_01;
	StatusIconChangeTable[SC_DORAM_BUF_02] = SI_DORAM_BUF_02;
	StatusIconChangeTable[SC_CHILL] = SI_CHILL;
	StatusIconChangeTable[SC_DRESSUP] = SI_DRESS_UP;

	//Other SC which are not necessarily associated to skills
	StatusChangeFlagTable[SC_ASPDPOTION0] |= SCB_ASPD;
	StatusChangeFlagTable[SC_ASPDPOTION1] |= SCB_ASPD;
	StatusChangeFlagTable[SC_ASPDPOTION2] |= SCB_ASPD;
	StatusChangeFlagTable[SC_ASPDPOTION3] |= SCB_ASPD;
	StatusChangeFlagTable[SC_SPEEDUP0] |= SCB_SPEED;
	StatusChangeFlagTable[SC_SPEEDUP1] |= SCB_SPEED;
	StatusChangeFlagTable[SC_INCALLSTATUS] |= SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK;
	StatusChangeFlagTable[SC_INCSTR] |= SCB_STR;
	StatusChangeFlagTable[SC_INCAGI] |= SCB_AGI;
	StatusChangeFlagTable[SC_INCVIT] |= SCB_VIT;
	StatusChangeFlagTable[SC_INCINT] |= SCB_INT;
	StatusChangeFlagTable[SC_INCDEX] |= SCB_DEX;
	StatusChangeFlagTable[SC_INCLUK] |= SCB_LUK;
	StatusChangeFlagTable[SC_INCHIT] |= SCB_HIT;
	StatusChangeFlagTable[SC_INCFLEE] |= SCB_FLEE;
	StatusChangeFlagTable[SC_INCFLEERATE] |= SCB_FLEE;
	StatusChangeFlagTable[SC_INCCRI] |= SCB_CRI;
	StatusChangeFlagTable[SC_INCASPDRATE] |= SCB_ASPD;
	StatusChangeFlagTable[SC_INCFLEE2] |= SCB_FLEE2;
	StatusChangeFlagTable[SC_INCMHPRATE] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_INCMSPRATE] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_INCMHP] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_INCMSP] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_INCATKRATE] |= SCB_BATK;
	StatusChangeFlagTable[SC_STRFOOD] |= SCB_STR;
	StatusChangeFlagTable[SC_AGIFOOD] |= SCB_AGI;
	StatusChangeFlagTable[SC_VITFOOD] |= SCB_VIT;
	StatusChangeFlagTable[SC_INTFOOD] |= SCB_INT;
	StatusChangeFlagTable[SC_DEXFOOD] |= SCB_DEX;
	StatusChangeFlagTable[SC_LUKFOOD] |= SCB_LUK;
	StatusChangeFlagTable[SC_CRIFOOD] |= SCB_CRI;
	StatusChangeFlagTable[SC_HITFOOD] |= SCB_HIT;
	StatusChangeFlagTable[SC_FLEEFOOD] |= SCB_FLEE;
	StatusChangeFlagTable[SC_BATKFOOD] |= SCB_BATK;
	StatusChangeFlagTable[SC_WATKFOOD] |= SCB_WATK;
	StatusChangeFlagTable[SC_MATKFOOD] |= SCB_MATK;
	StatusChangeFlagTable[SC_ARMOR_RESIST] |= SCB_ALL;
	StatusChangeFlagTable[SC_SPCOST_RATE] |= SCB_ALL;
	StatusChangeFlagTable[SC_WALKSPEED] |= SCB_SPEED;
	StatusChangeFlagTable[SC_SLOWDOWN] |= SCB_SPEED;
	StatusChangeFlagTable[SC_FOOD_STR_CASH] |= SCB_STR;
	StatusChangeFlagTable[SC_FOOD_AGI_CASH] |= SCB_AGI;
	StatusChangeFlagTable[SC_FOOD_VIT_CASH] |= SCB_VIT;
	StatusChangeFlagTable[SC_FOOD_DEX_CASH] |= SCB_DEX;
	StatusChangeFlagTable[SC_FOOD_INT_CASH] |= SCB_INT;
	StatusChangeFlagTable[SC_FOOD_LUK_CASH] |= SCB_LUK;
	StatusChangeFlagTable[SC_MERC_FLEEUP] |= SCB_FLEE;
	StatusChangeFlagTable[SC_MERC_ATKUP] |= SCB_WATK;
	StatusChangeFlagTable[SC_MERC_HPUP] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_MERC_SPUP] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_MERC_HITUP] |= SCB_HIT;
	StatusChangeFlagTable[SC_HALLUCINATIONWALK_POSTDELAY] |= SCB_SPEED|SCB_ASPD;
	StatusChangeFlagTable[SC_PARALYSE] |= SCB_FLEE|SCB_SPEED|SCB_ASPD;
	StatusChangeFlagTable[SC_DEATHHURT] |= SCB_REGEN;
	StatusChangeFlagTable[SC_VENOMBLEED] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_PYREXIA] |= SCB_HIT|SCB_FLEE;
	StatusChangeFlagTable[SC_OBLIVIONCURSE] |= SCB_REGEN;
	StatusChangeFlagTable[SC_SHIELDSPELL_DEF] |= SCB_WATK;
	StatusChangeFlagTable[SC_SHIELDSPELL_REF] |= SCB_DEF;
	StatusChangeFlagTable[SC_BANDING] |= SCB_WATK|SCB_DEF|SCB_REGEN;
	StatusChangeFlagTable[SC_STOMACHACHE] |= SCB_STR|SCB_AGI|SCB_VIT|SCB_DEX|SCB_INT|SCB_LUK;
	StatusChangeFlagTable[SC_MYSTERIOUS_POWDER] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_MELON_BOMB] |= SCB_SPEED|SCB_ASPD;
	StatusChangeFlagTable[SC_BANANA_BOMB] |= SCB_LUK;
	StatusChangeFlagTable[SC_PROMOTE_HEALTH_RESERCH] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_ENERGY_DRINK_RESERCH] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_EXTRACT_WHITE_POTION_Z] |= SCB_REGEN;
	StatusChangeFlagTable[SC_VITATA_500] |= SCB_MAXSP|SCB_REGEN;
	StatusChangeFlagTable[SC_EXTRACT_SALAMINE_JUICE] |= SCB_ASPD;
	StatusChangeFlagTable[SC_BOOST500] |= SCB_ASPD;
	StatusChangeFlagTable[SC_FULL_SWING_K] |= SCB_WATK;
	StatusChangeFlagTable[SC_MANA_PLUS] |= SCB_MATK;
	StatusChangeFlagTable[SC_MUSTLE_M] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_LIFE_FORCE_F] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_SAVAGE_STEAK] |= SCB_STR;
	StatusChangeFlagTable[SC_COCKTAIL_WARG_BLOOD] |= SCB_INT;
	StatusChangeFlagTable[SC_MINOR_BBQ] |= SCB_VIT;
	StatusChangeFlagTable[SC_SIROMA_ICE_TEA] |= SCB_DEX;
	StatusChangeFlagTable[SC_DROCERA_HERB_STEAMED] |= SCB_AGI;
	StatusChangeFlagTable[SC_PUTTI_TAILS_NOODLES] |= SCB_LUK;
	StatusChangeFlagTable[SC_TIDAL_WEAPON] |= SCB_WATK|SCB_ATK_ELE;
	StatusChangeFlagTable[SC_REBOUND] |= SCB_SPEED|SCB_REGEN;
	StatusChangeFlagTable[SC_DEFSET_NUM] |= SCB_DEF|SCB_DEF2;
	StatusChangeFlagTable[SC_MDEFSET_NUM] |= SCB_MDEF|SCB_MDEF2;
	StatusChangeFlagTable[SC_DEFSET_PER] = SCB_DEF;
	StatusChangeFlagTable[SC_MDEFSET_PER] = SCB_MDEF;
	StatusChangeFlagTable[SC_WEDDING] |= SCB_SPEED;
	StatusChangeFlagTable[SC_ALL_RIDING] |= SCB_SPEED;
	StatusChangeFlagTable[SC_PUSH_CART] |= SCB_SPEED;
	StatusChangeFlagTable[SC_MTF_ASPD] |= SCB_HIT|SCB_ASPD;
	StatusChangeFlagTable[SC_MTF_MATK] |= SCB_MATK;
	StatusChangeFlagTable[SC_MTF_RANGEATK] |= SCB_ALL;
	StatusChangeFlagTable[SC_MTF_RANGEATK2] |= SCB_ALL;
	StatusChangeFlagTable[SC_MTF_MLEATKED] |= SCB_ALL;
	StatusChangeFlagTable[SC_MTF_CRIDAMAGE] |= SCB_ALL;
	StatusChangeFlagTable[SC_QUEST_BUFF1] |= SCB_MATK;
	StatusChangeFlagTable[SC_QUEST_BUFF2] |= SCB_MATK;
	StatusChangeFlagTable[SC_QUEST_BUFF3] |= SCB_MATK;
	StatusChangeFlagTable[SC_CHASEWALK2] |= SCB_STR;
	StatusChangeFlagTable[SC_MTF_ASPD2] |= SCB_HIT|SCB_ASPD;
	StatusChangeFlagTable[SC_MTF_MATK2] |= SCB_MATK;
	StatusChangeFlagTable[SC_ALMIGHTY] |= SCB_MATK|SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK;
	StatusChangeFlagTable[SC_MTF_HITFLEE] |= SCB_HIT|SCB_FLEE;
	StatusChangeFlagTable[SC_MTF_MHP] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_MTF_MSP] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_ATTHASTE_CASH] |= SCB_ASPD;
	StatusChangeFlagTable[SC_GEFFEN_MAGIC1] |= SCB_ALL;
	StatusChangeFlagTable[SC_GEFFEN_MAGIC2] |= SCB_ALL;
	StatusChangeFlagTable[SC_GEFFEN_MAGIC3] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_GIANT] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_GOLEM] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_STUN] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_STONE] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_FREEZ] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_SLEEP] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_CURSE] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_SILENCE] |= SCB_ALL;
	StatusChangeFlagTable[SC_GVG_BLIND] |= SCB_ALL;
	StatusChangeFlagTable[SC_FENRIR_CARD] |= SCB_MATK;
	StatusChangeFlagTable[SC_SWORDCLAN] |= SCB_STR|SCB_VIT|SCB_MAXHP|SCB_MAXSP;
	StatusChangeFlagTable[SC_ARCWANDCLAN] |= SCB_INT|SCB_DEX|SCB_MAXHP|SCB_MAXSP;
	StatusChangeFlagTable[SC_GOLDENMACECLAN] |= SCB_LUK|SCB_INT|SCB_MAXHP|SCB_MAXSP;
	StatusChangeFlagTable[SC_CROSSBOWCLAN] |= SCB_DEX|SCB_AGI|SCB_MAXHP|SCB_MAXSP;
	StatusChangeFlagTable[SC_JUMPINGCLAN] |= SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK;
	StatusChangeFlagTable[SC_DORAM_WALKSPEED] |= SCB_SPEED;
	StatusChangeFlagTable[SC_DORAM_MATK] |= SCB_MATK;
	StatusChangeFlagTable[SC_DORAM_FLEE2] |= SCB_FLEE2;
	StatusChangeFlagTable[SC_EL_WAIT] |= SCB_REGEN;
	StatusChangeFlagTable[SC_EL_PASSIVE] |= SCB_REGEN;
	StatusChangeFlagTable[SC_EL_DEFENSIVE] |= SCB_REGEN;
	StatusChangeFlagTable[SC_EL_OFFENSIVE] |= SCB_REGEN;

	//StatusDisplayType Table [Ind]
	StatusDisplayType[SC_ALL_RIDING]	  = true;
	StatusDisplayType[SC_PUSH_CART]		  = true;
	StatusDisplayType[SC_EXPLOSIONSPIRITS]	  = true;
	StatusDisplayType[SC_SPHERE_1]		  = true;
	StatusDisplayType[SC_SPHERE_2]		  = true;
	StatusDisplayType[SC_SPHERE_3]		  = true;
	StatusDisplayType[SC_SPHERE_4]		  = true;
	StatusDisplayType[SC_SPHERE_5]		  = true;
	StatusDisplayType[SC_CAMOUFLAGE]	  = true;
	StatusDisplayType[SC_STEALTHFIELD]	  = true;
	StatusDisplayType[SC_DUPLELIGHT]	  = true;
	StatusDisplayType[SC_ORATIO]		  = true;
	StatusDisplayType[SC_BURNING]		  = true;
	StatusDisplayType[SC_FREEZING]		  = true;
	StatusDisplayType[SC_WHITEIMPRISON]	  = true;
	StatusDisplayType[SC_VENOMIMPRESS]	  = true;
	StatusDisplayType[SC_HALLUCINATIONWALK]	  = true;
	StatusDisplayType[SC_ROLLINGCUTTER]	  = true;
	StatusDisplayType[SC_BANDING]		  = true;
	StatusDisplayType[SC_CRYSTALIZE]	  = true;
	StatusDisplayType[SC_STRIKING]		  = true;
	StatusDisplayType[SC_DEEPSLEEP]		  = true;
	StatusDisplayType[SC_CURSEDCIRCLE_ATKER]  = true;
	StatusDisplayType[SC_CURSEDCIRCLE_TARGET] = true;
	StatusDisplayType[SC_NETHERWORLD]	  = true;
	StatusDisplayType[SC_VOICEOFSIREN]	  = true;
	StatusDisplayType[SC_BLOODSUCKER]	  = true;
	StatusDisplayType[SC__SHADOWFORM]	  = true;
	StatusDisplayType[SC__MANHOLE]		  = true;
	StatusDisplayType[SC_JYUMONJIKIRI]	  = true;
	StatusDisplayType[SC_MEIKYOUSISUI]	  = true;
	StatusDisplayType[SC_KYOUGAKU]		  = true;
	StatusDisplayType[SC_KYOMU]		  = true;
	StatusDisplayType[SC_KAGEMUSYA]		  = true;
	StatusDisplayType[SC_ZANGETSU]		  = true;
	StatusDisplayType[SC_GENSOU]		  = true;
	StatusDisplayType[SC_AKAITSUKI]		  = true;
	StatusDisplayType[SC_GRANITIC_ARMOR]	  = true;
	StatusDisplayType[SC_MONSTER_TRANSFORM]	  = true;
	StatusDisplayType[SC_ACTIVE_MONSTER_TRANSFORM]	= true;
	StatusDisplayType[SC_DARKCROW]		  = true;
	StatusDisplayType[SC_OFFERTORIUM]	  = true;
	StatusDisplayType[SC_TELEKINESIS_INTENSE] = true;
	StatusDisplayType[SC_UNLIMIT]		  = true;
	StatusDisplayType[SC_C_MARKER]		  = true;
	StatusDisplayType[SC_ANTI_M_BLAST]	  = true;
	StatusDisplayType[SC_H_MINE]		  = true;
	StatusDisplayType[SC_MOONSTAR]		  = true;
	StatusDisplayType[SC_SUPER_STAR]	  = true;
	StatusDisplayType[SC_STRANGELIGHTS]	  = true;
	StatusDisplayType[SC_DECORATION_OF_MUSIC] = true;
	StatusDisplayType[SC_SUHIDE]		  = true;
	StatusDisplayType[SC_SU_STOOP]		  = true;
	StatusDisplayType[SC_SPRITEMABLE]	  = true;
	StatusDisplayType[SC_SV_ROOTTWIST]	  = true;
	StatusDisplayType[SC_SOULATTACK]	  = true;
	StatusDisplayType[SC_TUNAPARTY]		  = true;
	StatusDisplayType[SC_HAT_EFFECT]	  = true;
	StatusDisplayType[SC_QSCARABA]		  = true;
	StatusDisplayType[SC_LJOSALFAR]		  = true;
	StatusDisplayType[SC_MAPLE_FALLS]	  = true;
	StatusDisplayType[SC_MERMAID_LONGING] = true;
	StatusDisplayType[SC_TIME_ACCESSORY]  = true;
	StatusDisplayType[SC_CLAN_INFO]		  = true;
	StatusDisplayType[SC_MAXPAIN]		  = true;
	StatusDisplayType[SC_DRESSUP]		  = true;

	//StatusChangeState (SCS_) NOMOVE
	StatusChangeStateTable[SC_ANKLE]                |= SCS_NOMOVE;
	StatusChangeStateTable[SC_AUTOCOUNTER]          |= SCS_NOMOVE;
	StatusChangeStateTable[SC_TRICKDEAD]            |= SCS_NOMOVE;
	StatusChangeStateTable[SC_BLADESTOP]            |= SCS_NOMOVE;
	StatusChangeStateTable[SC_BLADESTOP_WAIT]       |= SCS_NOMOVE;
	StatusChangeStateTable[SC_GOSPEL]               |= SCS_NOMOVE|SCS_NOMOVECOND;
	StatusChangeStateTable[SC_BASILICA]             |= SCS_NOMOVE|SCS_NOMOVECOND;
	StatusChangeStateTable[SC_SPIDERWEB]            |= SCS_NOMOVE;
	StatusChangeStateTable[SC_WIDEWEB]              |= SCS_NOMOVE;
	StatusChangeStateTable[SC_STOP]                 |= SCS_NOMOVE;
	StatusChangeStateTable[SC_CLOSECONFINE]         |= SCS_NOMOVE;
	StatusChangeStateTable[SC_CLOSECONFINE2]        |= SCS_NOMOVE;
	StatusChangeStateTable[SC_MADNESSCANCEL]        |= SCS_NOMOVE;
	StatusChangeStateTable[SC_GRAVITATION]          |= SCS_NOMOVE|SCS_NOMOVECOND;
	StatusChangeStateTable[SC_ELECTRICSHOCKER]      |= SCS_NOMOVE;
	StatusChangeStateTable[SC_BITE]                 |= SCS_NOMOVE;
	StatusChangeStateTable[SC_CAMOUFLAGE]           |= SCS_NOMOVE|SCS_NOMOVECOND;
	StatusChangeStateTable[SC_MAGNETICFIELD]        |= SCS_NOMOVE;
	StatusChangeStateTable[SC__MANHOLE]             |= SCS_NOMOVE;
	StatusChangeStateTable[SC_FALLENEMPIRE]         |= SCS_NOMOVE;
	StatusChangeStateTable[SC_CURSEDCIRCLE_ATKER]   |= SCS_NOMOVE;
	StatusChangeStateTable[SC_CURSEDCIRCLE_TARGET]  |= SCS_NOMOVE;
	StatusChangeStateTable[SC_NETHERWORLD]          |= SCS_NOMOVE;
	StatusChangeStateTable[SC_DEEPSLEEP]            |= SCS_NOMOVE;
	StatusChangeStateTable[SC_CRYSTALIZE]           |= SCS_NOMOVE;
	StatusChangeStateTable[SC_VACUUM_EXTREME]       |= SCS_NOMOVE;
	StatusChangeStateTable[SC_THORNSTRAP]           |= SCS_NOMOVE;
	StatusChangeStateTable[SC_KAGEHUMI]             |= SCS_NOMOVE;
	StatusChangeStateTable[SC_PARALYSIS]            |= SCS_NOMOVE;
	StatusChangeStateTable[SC_TINDER_BREAKER]       |= SCS_NOMOVE;
	StatusChangeStateTable[SC_KINGS_GRACE]          |= SCS_NOMOVE;
	StatusChangeStateTable[SC_SUHIDE]               |= SCS_NOMOVE;
	StatusChangeStateTable[SC_SV_ROOTTWIST]         |= SCS_NOMOVE;

	//StatusChangeState (SCS_) NOPICKUPITEMS
	StatusChangeStateTable[SC_HIDING]              |= SCS_NOPICKITEM;
	StatusChangeStateTable[SC_CLOAKING]            |= SCS_NOPICKITEM;
	StatusChangeStateTable[SC_TRICKDEAD]           |= SCS_NOPICKITEM;
	StatusChangeStateTable[SC_BLADESTOP]           |= SCS_NOPICKITEM;
	StatusChangeStateTable[SC_CLOAKINGEXCEED]      |= SCS_NOPICKITEM;
	StatusChangeStateTable[SC_NOCHAT]              |= SCS_NOPICKITEM|SCS_NOPICKITEMCOND;
	StatusChangeStateTable[SC_DEEPSLEEP]           |= SCS_NOPICKITEM;
	StatusChangeStateTable[SC_SUHIDE]              |= SCS_NOPICKITEM;

	//StatusChangeState (SCS_) NODROPITEMS
	StatusChangeStateTable[SC_AUTOCOUNTER]         |= SCS_NODROPITEM;
	StatusChangeStateTable[SC_BLADESTOP]           |= SCS_NODROPITEM;
	StatusChangeStateTable[SC_NOCHAT]              |= SCS_NODROPITEM|SCS_NODROPITEMCOND;

	//StatusChangeState (SCS_) NOCAST (skills)
	StatusChangeStateTable[SC_SILENCE]             |= SCS_NOCAST;
	StatusChangeStateTable[SC_STEELBODY]           |= SCS_NOCAST;
	StatusChangeStateTable[SC_BERSERK]             |= SCS_NOCAST;
	StatusChangeStateTable[SC_OBLIVIONCURSE]       |= SCS_NOCAST;
	StatusChangeStateTable[SC__SHADOWFORM]         |= SCS_NOCAST;
	StatusChangeStateTable[SC__INVISIBILITY]       |= SCS_NOCAST;
	StatusChangeStateTable[SC__IGNORANCE]          |= SCS_NOCAST;
	StatusChangeStateTable[SC__MANHOLE]            |= SCS_NOCAST;
	StatusChangeStateTable[SC_CURSEDCIRCLE_TARGET] |= SCS_NOCAST;
	StatusChangeStateTable[SC_SATURDAYNIGHTFEVER]  |= SCS_NOCAST;
	StatusChangeStateTable[SC_DEEPSLEEP]           |= SCS_NOCAST;
	StatusChangeStateTable[SC_CRYSTALIZE]          |= SCS_NOCAST;
	StatusChangeStateTable[SC_KINGS_GRACE]         |= SCS_NOCAST;

	//StatusChangeState (SCS_) NOCHAT (skills)
	StatusChangeStateTable[SC_BERSERK]             |= SCS_NOCHAT;
	StatusChangeStateTable[SC_SATURDAYNIGHTFEVER]  |= SCS_NOCHAT;
	StatusChangeStateTable[SC_DEEPSLEEP]           |= SCS_NOCHAT;
	StatusChangeStateTable[SC_NOCHAT]              |= SCS_NOCHAT|SCS_NOCHATCOND;
}

static void initDummyData(void)
{
	memset(&dummy_status, 0, sizeof(dummy_status));
	dummy_status.hp =
	dummy_status.max_hp =
	dummy_status.max_sp =
	dummy_status.str =
	dummy_status.agi =
	dummy_status.vit =
	dummy_status.int_ =
	dummy_status.dex =
	dummy_status.luk =
	dummy_status.hit = 1;
	dummy_status.speed = 2000;
	dummy_status.adelay = 4000;
	dummy_status.amotion = 2000;
	dummy_status.dmotion = 2000;
	dummy_status.ele_lv = 1; //Min elemental level
	dummy_status.mode = MD_CANMOVE;
}

//For copying a status_data structure from b to a, without overwriting current HP and SP
static inline void status_cpy(struct status_data *a, const struct status_data *b)
{
	memcpy((void *)&a->max_hp, (const void *)&b->max_hp, sizeof(struct status_data) - (sizeof(a->hp) + sizeof(a->sp)));
}

//Sets HP to given value. Flag is the flag passed to status_heal in case
//final value is higher than current (use 2 to make a healing effect display
//on players) It will always succeed (overrides Berserk block), but it can't kill.
int status_set_hp(struct block_list *bl, unsigned int hp, int flag)
{
	struct status_data *status;

	if (hp < 1)
		return 0;
	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0;
	if (hp > status->max_hp)
		hp = status->max_hp;
	if (hp == status->hp)
		return 0;
	if (hp > status->hp)
		return status_heal(bl, hp - status->hp, 0, 1|flag);
	return status_zap(bl, status->hp - hp, 0);
}

/**
 * Sets MaxHP to a given value
 * @param bl: Object whose MaxHP will be set [PC|MOB|HOM|MER|ELEM|NPC]
 * @param maxhp: What the MaxHP is to be set as
 * @param flag: Used in case final value is higher than current
 *		Use 2 to display healing effect
 * @return heal or zapped HP if valid
 */
int status_set_maxhp(struct block_list *bl, unsigned int maxhp, int flag)
{
	struct status_data *status;

	if (maxhp < 1)
		return 0;
	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0;
	if (maxhp == status->max_hp)
		return 0;
	if (maxhp > status->max_hp)
		status_heal(bl, maxhp - status->max_hp, 0, 1|flag);
	else
		status_zap(bl, status->max_hp - maxhp, 0);
	status->max_hp = maxhp;
	return maxhp;
}

//Sets SP to given value. Flag is the flag passed to status_heal in case
//final value is higher than current (use 2 to make a healing effect display
//on players)
int status_set_sp(struct block_list *bl, unsigned int sp, int flag)
{
	struct status_data *status;

	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0;
	if (sp > status->max_sp)
		sp = status->max_sp;
	if (sp == status->sp)
		return 0;
	if (sp > status->sp)
		return status_heal(bl, 0, sp - status->sp, 1|flag);
	return status_zap(bl, 0, status->sp - sp);
}

/**
 * Sets MaxSP to a given value
 * @param bl: Object whose MaxSP will be set [PC|HOM|MER|ELEM]
 * @param maxsp: What the MaxSP is to be set as
 * @param flag: Used in case final value is higher than current
 *		Use 2 to display healing effect
 * @return heal or zapped HP if valid
 */
int status_set_maxsp(struct block_list *bl, unsigned int maxsp, int flag)
{
	struct status_data *status;

	if (maxsp < 1)
		return 0;
	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0;
	if (maxsp == status->max_sp)
		return 0;
	if (maxsp > status->max_sp) {
		status_heal(bl, maxsp - status->max_sp, 0, 1|flag);
	} else
		status_zap(bl, status->max_sp - maxsp, 0);
	status->max_sp = maxsp;
	return maxsp;
}

int status_charge(struct block_list *bl, int64 hp, int64 sp)
{
	if (!(bl->type&BL_CONSUME))
		return (int)(hp + sp); //Assume all was charged so there are no 'not enough' fails
	return status_damage(NULL, bl, hp, sp, 0, 3);
}

//Inflicts damage on the target with the according walkdelay
//If flag&1, damage is passive and does not triggers cancelling status changes
//If flag&2, fail if target does not has enough to substract
//If flag&4, if killed, mob must not give exp/loot
//flag will be set to &8 when damaging sp of a dead character
int status_damage(struct block_list *src, struct block_list *target, int64 in_hp, int64 in_sp, int walkdelay, int flag)
{
	struct status_data *status;
	struct status_change *sc;
	int hp, sp;

	nullpo_ret(target);

	//Here onwards we consider it a 32-type, the client does not support higher and from here onwards the value doesn't get thru percentage modifiers
	hp = (int)cap_value(in_hp, INT_MIN, INT_MAX);
	sp = (int)cap_value(in_sp, INT_MIN, INT_MAX);

	if (sp && !(target->type&BL_CONSUME))
		sp = 0; //Not a valid SP target

	if (hp < 0) { //Assume absorbed damage
		status_heal(target, -hp, 0, 1);
		hp = 0;
	}

	if (sp < 0) {
		status_heal(target, 0, -sp, 1);
		sp = 0;
	}

	if (target->type == BL_SKILL) {
		if (!src || src->type&battle_config.can_damage_skill)
			return skill_unit_ondamaged((struct skill_unit *)target, hp);
		return 0;
	}

	status = status_get_status_data(target);
	if (!status || status == &dummy_status)
		return 0;

	if ((unsigned int)hp >= status->hp) {
		if (flag&2)
			return 0;
		hp = status->hp;
	}

	if ((unsigned int)sp > status->sp) {
		if (flag&2)
			return 0;
		sp = status->sp;
	}

	if (!hp && !sp)
		return 0;

	if (!status->hp)
		flag |= 8;

	//Let through, battle.c/skill.c have the whole logic of when it's possible or
	//not to hurt someone (and this check breaks pet catching) [Skotlex]
	//if (!target->prev && !(flag&2))
		//return 0; //Cannot damage a bl not on a map, except when "charging" hp/sp

	sc = status_get_sc(target);
	if (hp && battle_config.invincible_nodamage && src && sc && sc->data[SC_INVINCIBLE] && !sc->data[SC_INVINCIBLEOFF])
		hp = 1;

	if (hp && !(flag&1)) {
		if (sc) {
			struct status_change_entry *sce = NULL;

			if (sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
				status_change_end(target, SC_STONE, INVALID_TIMER);
			status_change_end(target, SC_FREEZE, INVALID_TIMER);
			status_change_end(target, SC_SLEEP, INVALID_TIMER);
			status_change_end(target, SC_DEEPSLEEP, INVALID_TIMER);
			status_change_end(target, SC_WINKCHARM, INVALID_TIMER);
			status_change_end(target, SC_VOICEOFSIREN,INVALID_TIMER);
			status_change_end(target, SC_TRICKDEAD, INVALID_TIMER);
			status_change_end(target, SC_HIDING, INVALID_TIMER);
			status_change_end(target, SC_CLOAKING, INVALID_TIMER);
			status_change_end(target, SC_CHASEWALK, INVALID_TIMER);
			status_change_end(target, SC_CAMOUFLAGE, INVALID_TIMER);
			status_change_end(target, SC_SITDOWN_FORCE, INVALID_TIMER);
			status_change_end(target, SC_BANANA_BOMB_SITDOWN, INVALID_TIMER);
			status_change_end(target, SC_SUHIDE, INVALID_TIMER);
			//Endure count is only reduced by non-players on non-gvg maps
			if ((sce = sc->data[SC_ENDURE]) && !sce->val4 && //val4 signals infinite endure [Skotlex]
				src && src->type != BL_PC && !map_flag_gvg2(target->m) && !map[target->m].flag.battleground && --(sce->val2) <= 0)
				status_change_end(target, SC_ENDURE, INVALID_TIMER);
			if ((sce = sc->data[SC_GRAVITATION]) && sce->val3 == BCT_SELF) {
				struct skill_unit_group *group = skill_id2group(sce->val4);

				if (group) {
					skill_delunitgroup(group);
					sce->val4 = 0;
					status_change_end(target, SC_GRAVITATION, INVALID_TIMER);
				}
			}
			if (sc->data[SC_DANCING] && (unsigned int)hp > (status->max_hp>>2))
				status_change_end(target, SC_DANCING, INVALID_TIMER);
			if (sc->data[SC_CLOAKINGEXCEED] && --(sc->data[SC_CLOAKINGEXCEED]->val2) <= 0)
				status_change_end(target, SC_CLOAKINGEXCEED, INVALID_TIMER);
			if (sc->data[SC_KAGEMUSYA] && --(sc->data[SC_KAGEMUSYA]->val2) <= 0)
				status_change_end(target, SC_KAGEMUSYA, INVALID_TIMER);
		}

		if (target->type == BL_PC)
			pc_bonus_script_clear(BL_CAST(BL_PC, target), BSF_REM_ON_DAMAGED);
		unit_skillcastcancel(target, 2);
	}

	status->hp -= hp;
	status->sp -= sp;

	if (sc && hp && status->hp) {
		if (sc->data[SC_AUTOBERSERK] && (!sc->data[SC_PROVOKE] || !sc->data[SC_PROVOKE]->val2) && status->hp < status->max_hp>>2)
			sc_start4(src, target, SC_PROVOKE, 100, 10, 1, 0, 0, 60000);
		if (sc->data[SC_BERSERK] && status->hp <= 100)
			status_change_end(target, SC_BERSERK, INVALID_TIMER);
		if (sc->data[SC_RAISINGDRAGON] && status->hp <= 1000)
			status_change_end(target, SC_RAISINGDRAGON, INVALID_TIMER);
		if (sc->data[SC_SATURDAYNIGHTFEVER] && status->hp <= 100)
			status_change_end(target, SC_SATURDAYNIGHTFEVER, INVALID_TIMER);
	}

	switch (target->type) {
		case BL_PC:  pc_damage((TBL_PC *)target, src, hp, sp); break;
		case BL_MOB: mob_damage((TBL_MOB *)target, src, hp); break;
		case BL_HOM: hom_damage((TBL_HOM *)target); break;
		case BL_MER: mercenary_heal((TBL_MER *)target, hp, sp); break;
		case BL_ELEM: elemental_heal((TBL_ELEM *)target, hp, sp); break;
	}

	//Stop walking when attacked in disguise to prevent walk-delay bug
	if (src && target->type == BL_PC && ((TBL_PC *)target)->disguise)
		unit_stop_walking(target, USW_FIXPOS);

	if (status->hp || (flag&8)) { //Still lives or has been dead before this damage
		if (walkdelay)
			unit_set_walkdelay(target, gettick(), walkdelay, 0);
		return (int)(hp + sp);
	}

	status->hp = 0;
	//NOTE: These dead functions should return: [Skotlex]
	//0: Death cancelled, auto-revived
	//Non-zero: Standard death. Clear status, cancel move/attack, etc
	//&2: Also remove object from map
	//&4: Also delete object from memory
	switch (target->type) {
		case BL_PC:  flag = pc_dead((TBL_PC *)target,src); break;
		case BL_MOB: flag = mob_dead((TBL_MOB *)target, src, (flag&4 ? 3 : 0)); break;
		case BL_HOM: flag = hom_dead((TBL_HOM *)target); break;
		case BL_MER: flag = mercenary_dead((TBL_MER *)target); break;
		case BL_ELEM: flag = elemental_dead((TBL_ELEM *)target); break;
		default: //Unhandled case, do nothing to object
			flag = 0;
			break;
	}

	if (!flag) //Death cancelled
		return (int)(hp + sp);

	//Normal death
	if (battle_config.clear_unit_ondeath && battle_config.clear_unit_ondeath&target->type)
		skill_clear_unitgroup(target);

	if (target->type&BL_REGEN) { //Reset regen ticks
		struct regen_data *regen = status_get_regen_data(target);

		if (regen) {
			memset(&regen->tick,0,sizeof(regen->tick));
			if (regen->sregen)
				memset(&regen->sregen->tick,0,sizeof(regen->sregen->tick));
			if (regen->ssregen)
				memset(&regen->ssregen->tick,0,sizeof(regen->ssregen->tick));
		}
	}

	if (sc && sc->data[SC_KAIZEL] && !map_flag_gvg2(target->m)) { //flag&8 = disable Kaizel
		int time = skill_get_time2(SL_KAIZEL,sc->data[SC_KAIZEL]->val1);

		//Look for Osiris Card's bonus effect on the character and revive 100% or revive normally
		if (target->type == BL_PC && BL_CAST(BL_PC,target)->special_state.restart_full_recover)
			status_revive(target,100,100);
		else
			status_revive(target,sc->data[SC_KAIZEL]->val2,0);
		status_change_clear(target,0);
		clif_skill_nodamage(target,target,ALL_RESURRECTION,1,1);
		sc_start(src,target,status_skill2sc(PR_KYRIE),100,10,time);
		if (target->type == BL_MOB)
			((TBL_MOB *)target)->state.rebirth = 1;
		return (int)(hp + sp);
	}

	//Ensure the monster has not already rebirthed before doing so
	if (target->type == BL_MOB && sc && sc->data[SC_REBIRTH] && !((TBL_MOB *) target)->state.rebirth) {
		status_revive(target,sc->data[SC_REBIRTH]->val2,0);
		status_change_clear(target,0);
		((TBL_MOB *)target)->state.rebirth = 1;
		return (int)(hp + sp);
	}

	status_change_clear(target,0);

	if (flag&4) //Delete from memory (also invokes map removal code)
		unit_free(target,CLR_DEAD);
	else if (flag&2) //Remove from map
		unit_remove_map(target,CLR_DEAD);
	else { //Some death states that would normally be handled by unit_remove_map
		unit_stop_attack(target);
		unit_stop_walking(target,USW_FIXPOS);
		unit_skillcastcancel(target,0);
		clif_clearunit_area(target,CLR_DEAD);
		skill_unit_move(target,gettick(),4);
		skill_cleartimerskill(target);
	}

	//Always run NPC scripts for players last
	//FIXME: Those ain't always run if a player die if he was resurect meanwhile
	//cf SC_REBIRTH, SC_KAIZEL, pc_dead
	if (target->type == BL_PC) {
		TBL_PC *sd = BL_CAST(BL_PC,target);

		if (sd->bg_id) {
			struct battleground_data *bg;

			if ((bg = bg_team_search(sd->bg_id)) && bg->die_event[0])
				npc_event(sd,bg->die_event,0);
		}
		npc_script_event(sd,NPCE_DIE);
	}
	return (int)(hp + sp);
}

//Heals a character
//If flag&1, this is forced healing (otherwise, stuff like Berserk can block it)
//If flag&2, when the player is healed, show the HP/SP heal effect
int status_heal(struct block_list *bl, int64 in_hp, int64 in_sp, int flag)
{
	struct status_data *status;
	struct status_change *sc;
	int hp, sp;

	status = status_get_status_data(bl);

	if (status == &dummy_status || !status->hp)
		return 0;

	//Here onwards we consider it a 32-type, the client does not support higher and,
	//from here onwards the value doesn't get thru percentage modifiers
	hp = (int)cap_value(in_hp, INT_MIN, INT_MAX);
	sp = (int)cap_value(in_sp, INT_MIN, INT_MAX);

	sc = status_get_sc(bl);
	if (sc && !sc->count)
		sc = NULL;

	if (hp < 0) {
		if (hp == INT_MIN)
			hp++; //-INT_MIN == INT_MIN in some architectures!
		status_damage(NULL, bl, -hp, 0, 0, 1);
		hp = 0;
	}

	if (hp) {
		if (sc && (sc->data[SC_BERSERK] || sc->data[SC_SATURDAYNIGHTFEVER])) {
			if (flag&1)
				flag &= ~2;
			else
				hp = 0;
		}
		if ((unsigned int)hp > status->max_hp - status->hp)
			hp = status->max_hp - status->hp;
	}

	if (sp < 0) {
		if (sp == INT_MIN)
			sp++;
		status_damage(NULL, bl, 0, -sp, 0, 1);
		sp = 0;
	}

	if (sp && (unsigned int)sp > status->max_sp - status->sp)
		sp = status->max_sp - status->sp;

	if (!sp && !hp)
		return 0;

	status->hp += hp;
	status->sp += sp;

	if (hp && sc && sc->data[SC_AUTOBERSERK] && sc->data[SC_PROVOKE] && sc->data[SC_PROVOKE]->val2 && status->hp >= status->max_hp>>2)
		status_change_end(bl, SC_PROVOKE, INVALID_TIMER); //End auto berserk

	switch (bl->type) { //Send hp update to client
		case BL_PC:  pc_heal((TBL_PC *)bl, hp, sp, (flag&2 ? 1 : 0)); break;
		case BL_MOB: mob_heal((TBL_MOB *)bl, hp); break;
		case BL_HOM: hom_heal((TBL_HOM *)bl); break;
		case BL_MER: mercenary_heal((TBL_MER *)bl, hp, sp); break;
		case BL_ELEM: elemental_heal((TBL_ELEM *)bl, hp, sp); break;
	}

	return (int)(hp + sp);
}

/**
 * Applies percentage based damage to a unit.
 * If a mob is killed this way and there is no src, no EXP/Drops will be awarded.
 * @param src: Object initiating HP/SP modification [PC|MOB|PET|HOM|MER|ELEM]
 * @param target: Object to modify HP/SP
 * @param hp_rate: Percentage of HP to modify. If > 0:percent is of current HP, if < 0:percent is of max HP
 * @param sp_rate: Percentage of SP to modify. If > 0:percent is of current SP, if < 0:percent is of max SP
 * @param flag:
 *		0: Heal target
 *		1: Use status_damage
 *		2: Use status_damage and make sure target must not die from subtraction
 * @return hp + sp through status_heal()
 */
int status_percent_change(struct block_list *src, struct block_list *target, int8 hp_rate, int8 sp_rate, uint8 flag)
{
	struct status_data *status;
	unsigned int hp = 0, sp = 0;

	status = status_get_status_data(target);

	//It's safe now [MarkZD]
	if (hp_rate > 99)
		hp = status->hp;
	else if (hp_rate > 0)
		hp = apply_rate(status->hp, hp_rate);
	else if (hp_rate < -99)
		hp = status->max_hp;
	else if (hp_rate < 0)
		hp = apply_rate(status->max_hp, -hp_rate);
	if (hp_rate && !hp)
		hp = 1;

	if (flag == 2 && hp >= status->hp)
		hp = status->hp - 1; //Must not kill target

	if (sp_rate > 99)
		sp = status->sp;
	else if (sp_rate > 0)
		sp = apply_rate(status->sp, sp_rate);
	else if (sp_rate < -99)
		sp = status->max_sp;
	else if (sp_rate < 0)
		sp = apply_rate(status->max_sp, -sp_rate);
	if (sp_rate && !sp)
		sp = 1;

	//Ugly check in case damage dealt is too much for the received args of status_heal/status_damage [Skotlex]
	if (hp > INT_MAX) {
		hp -= INT_MAX;
		if (flag)
			status_damage(src, target, INT_MAX, 0, 0, (!src || src == target ? 5 : 1));
		else
			status_heal(target, INT_MAX, 0, 0);
	}
	if (sp > INT_MAX) {
		sp -= INT_MAX;
		if (flag)
			status_damage(src, target, 0, INT_MAX, 0, (!src || src == target ? 5 : 1));
		else
			status_heal(target, 0, INT_MAX, 0);
	}
	if (flag)
		return status_damage(src, target, hp, sp, 0, (!src || src == target ? 5 : 1));
	return status_heal(target, hp, sp, 0);
}

int status_revive(struct block_list *bl, unsigned char per_hp, unsigned char per_sp)
{
	struct status_data *status;
	unsigned int hp, sp;

	if (!status_isdead(bl))
		return 0;

	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0; //Invalid target

	hp = status->max_hp * per_hp / 100;
	sp = status->max_sp * per_sp / 100;

	if(hp > status->max_hp - status->hp)
		hp = status->max_hp - status->hp;
	else if (per_hp && !hp)
		hp = 1;

	if(sp > status->max_sp - status->sp)
		sp = status->max_sp - status->sp;
	else if (per_sp && !sp)
		sp = 1;

	status->hp += hp;
	status->sp += sp;

	if (bl->prev) //Animation only if character is already on a map
		clif_resurrection(bl, 1);
	switch (bl->type) {
		case BL_PC:  pc_revive((TBL_PC *)bl, hp, sp); break;
		case BL_MOB: mob_revive((TBL_MOB *)bl, hp); break;
		case BL_HOM: hom_revive((TBL_HOM *)bl, hp, sp); break;
	}
	return 1;
}

//Fixed HP/SP recovery resurrection function needed for the WM_DEADHILLHERE skill [Rytech]
int status_fixed_revive(struct block_list *bl, unsigned int per_hp, unsigned int per_sp)
{
	struct status_data *status;
	unsigned int hp, sp;

	if (!status_isdead(bl))
		return 0;

	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0; //Invalid target

	hp = per_hp;
	sp = per_sp;

	if (hp > status->max_hp - status->hp)
		hp = status->max_hp - status->hp;
	else if (!hp)
		hp = 1;

	if (sp > status->max_sp - status->sp)
		sp = status->max_sp - status->sp;
	else if (!sp)
		sp = 1;

	status->hp += hp;
	status->sp += sp;

	if (bl->prev) //Animation only if character is already on a map
		clif_resurrection(bl, 1);
	switch (bl->type) {
		case BL_PC:  pc_revive((TBL_PC *)bl, hp, sp); break;
		case BL_MOB: mob_revive((TBL_MOB *)bl, hp); break;
		case BL_HOM: hom_revive((TBL_HOM *)bl, hp, sp); break;
	}
	return 1;
}

/*==========================================
 * Checks whether the src can use the skill on the target,
 * taking into account status/option of both source/target. [Skotlex]
 * flag:
 *	0 - Trying to use skill on target.
 *	1 - Cast bar is done.
 *	2 - Skill already pulled off, check is due to ground-based skills or splash-damage ones.
 * src MAY be null to indicate we shouldn't check it, this is a ground-based skill attack.
 * target MAY be null, in which case the checks are only to see
 * whether the source can cast or not the skill on the ground.
 *------------------------------------------*/
bool status_check_skilluse(struct block_list *src, struct block_list *target, uint16 skill_id, int flag)
{
	struct status_data *status;
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	int hide_flag;

	status = (src ? status_get_status_data(src) : &dummy_status);

	if (src && src->type != BL_PC && status_isdead(src))
		return false;

	if (!skill_id) { //Normal attack checks
		if (!status_has_mode(status, MD_CANATTACK))
			return false; //This mode is only needed for melee attacking
		if (target && status_isdead(target))
			return false; //Dead state isn't checked as some skills can be used on dead characters, said checks are left to skill.c [Skotlex]
	}

	switch (skill_id) {
		case AL_TELEPORT:
		case ALL_ODINS_RECALL:
			if (src && map_getcell(src->m, src->x, src->y, CELL_CHKLANDPROTECTOR) &&
				!status_has_mode(status, MD_STATUS_IMMUNE) && (src->type != BL_PC || ((TBL_PC *)src)->skillitem != skill_id))
				return false; //Should fail when used on top of Land Protector [Skotlex]
			break;
		case ALL_RESURRECTION:
		case WE_ONEFOREVER:
			if (target && battle_check_undead(status_get_race(target), status_get_element(target)))
				break;
		//Fall through
		case WM_DEADHILLHERE:
			if (target && !status_isdead(target))
				return false; //Can't cast on non-dead characters
			break;
	}

	if (sc && sc->count) {
		if (sc->data[SC_ALL_RIDING])
			return false; //New mounts can't attack nor use skills in the client, this check makes it cheat-safe [Ind]
		if (sc->opt1 && sc->opt1 != OPT1_STONEWAIT && sc->opt1 != OPT1_BURNING &&
			skill_id != RK_REFRESH && skill_id != SR_GENTLETOUCH_CURE &&
			skill_id != SU_GROOMING && skill_id != SU_PURRING) { //Check for BODYSTATE effect
			if (flag != 1)
				return false; //Can't cast, casted spells can't damage
			if (skill_get_casttype(skill_id) == CAST_DAMAGE)
				return false; //Damage skills can't come off
		}
		if ((sc->data[SC_TRICKDEAD] && skill_id != NV_TRICKDEAD) ||
			(sc->data[SC_AUTOCOUNTER] && !flag && skill_id) ||
			(sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_SELF && skill_id != PA_GOSPEL) ||
			(sc->data[SC_SUHIDE] && skill_id != SU_HIDE))
			return false;
		if (sc->data[SC_WINKCHARM] && target && flag != 2) {
			struct block_list *winkcharm_target = map_id2bl(sc->data[SC_WINKCHARM]->val2);

			if (winkcharm_target) {
				if (unit_bl2ud(src) && (unit_bl2ud(src))->walktimer == INVALID_TIMER)
					unit_walktobl(src, map_id2bl(sc->data[SC_WINKCHARM]->val2), 3, 1);
				return false;
			} else
				status_change_end(src, SC_WINKCHARM, INVALID_TIMER);
		}
		if (sc->data[SC_BLADESTOP]) {
			switch (sc->data[SC_BLADESTOP]->val1) {
				case 5: if (skill_id == MO_EXTREMITYFIST) break;
				case 4: if (skill_id == MO_CHAINCOMBO) break;
				case 3: if (skill_id == MO_INVESTIGATE) break;
				case 2: if (skill_id == MO_FINGEROFFENSIVE) break;
				default:
					return false;
			}
		}
		if (sc->data[SC_DANCING] && flag != 2) {
			if (src->type == BL_PC && ((skill_id >= WA_SWING_DANCE && skill_id <= WM_UNLIMITED_HUMMING_VOICE) || skill_id == WM_FRIGG_SONG)) {
				if (pc_checkskill((TBL_PC *)src, WM_LESSON) < 5) //Lvl 5 Lesson or higher allow you use 3rd job skills while dancing
					return false;
			} else if (sc->data[SC_LONGING]) { //Allow everything except dancing/re-dancing [Skotlex]
				if (skill_id == BD_ENCORE || (skill_get_inf2(skill_id)&(INF2_SONG_DANCE|INF2_ENSEMBLE_SKILL)))
					return false;
			} else if (!(skill_get_inf3(skill_id)&INF3_USABLE_DANCE)) //Skills that can be used in dancing state
				return false;
			if ((sc->data[SC_DANCING]->val1&0xFFFF) == CG_HERMODE && skill_id == BD_ADAPTATION)
				return false; //Can't amp out of Wand of Hermode [Skotlex]
		}
		if (skill_id && //Specific skill blocking
			(src->type != BL_PC || ((TBL_PC *)src)->skillitem != skill_id)) { //'itemskill' still can be casted
			if (!flag &&
				(sc->cant.cast || //Through SCS_NOCAST
				(sc->data[SC_NOCHAT] && (sc->data[SC_NOCHAT]->val1&MANNER_NOSKILL)) ||
				(sc->data[SC_VOLCANO] && skill_id == WZ_ICEWALL) ||
				(sc->data[SC_ROKISWEIL] && skill_id != BD_ADAPTATION) ||
				(sc->data[SC_HERMODE] && (skill_get_inf(skill_id)&INF_SUPPORT_SKILL)) ||
				(sc->data[SC_BASILICA] && //Only Basilica's caster that can cast, and only Basilica to cancel it
				(sc->data[SC_BASILICA]->val4 != src->id || skill_id != HP_BASILICA)) ||
				(sc->data[SC_MARIONETTE] && skill_id != CG_MARIONETTE) || //Only skill you can use is marionette again to cancel it
				(sc->data[SC_MARIONETTE2] && skill_id == CG_MARIONETTE) || //Cannot use marionette if you are being buffed by another
				(sc->data[SC_ANKLE] && skill_block_check(src, SC_ANKLE, skill_id)) ||
				(sc->data[SC_STASIS] && skill_block_check(src, SC_STASIS, skill_id))))
				return false;
		}
		if (sc->option) {
			if ((sc->option&OPTION_HIDE) && src->type == BL_PC && (!skill_id || !(skill_get_inf3(skill_id)&INF3_USABLE_HIDING)))
				return false;
			if ((sc->option&OPTION_CHASEWALK) && skill_id != ST_CHASEWALK)
				return false;
		}
	}

	if (!target || target == src)
		return true; //No further checking needed

	if (tsc && tsc->count) {
		//Attacks in invincible are capped to 1 damage and handled in batte.c
		if (tsc->data[SC_INVINCIBLE] && !tsc->data[SC_INVINCIBLEOFF] && skill_id && !(skill_id&(SA_SPELLBREAKER|SL_SKE)))
			return false; //Allow spell breaker and eske for sealed shrine GDB when in INVINCIBLE state
		if (!skill_id && tsc->data[SC_TRICKDEAD])
			return false;
		if ((skill_id == WZ_STORMGUST || skill_id == WZ_FROSTNOVA || skill_id == NJ_HYOUSYOURAKU) && tsc->data[SC_FREEZE])
			return false;
		if (skill_id == PR_LEXAETERNA && (tsc->data[SC_FREEZE] || (tsc->data[SC_STONE] && tsc->opt1 == OPT1_STONE)))
			return false;
		if (tsc->data[SC__MANHOLE] && !(skill_get_inf3(skill_id)&INF3_USABLE_MANHOLE))
			return false; //Skill that can be used to target while under Man Hole effect
	}

	//If targetting, cloak + hide protect you, otherwise only hiding does
	hide_flag = (flag ? OPTION_HIDE : (OPTION_HIDE|OPTION_CLOAK|OPTION_CHASEWALK));

	if (skill_get_inf3(skill_id)&INF3_HIT_HIDING) //Skill that can hit hidden target
		hide_flag &= ~OPTION_HIDE; //It works when already casted and target suddenly hides
	else {
		switch (skill_id) {
			case LG_OVERBRAND:
			case LG_OVERBRAND_BRANDISH:
			case LG_OVERBRAND_PLUSATK:
				hide_flag &= ~OPTION_CLOAK|OPTION_CHASEWALK;
				break;
		}
	}

	if (tsc && tsc->data[SC_STEALTHFIELD] && !((src && status_get_class_(src) == CLASS_BOSS) || status_has_mode(status, MD_DETECTOR)) && (!skill_id || !flag))
		return false;

	switch (target->type) {
		case BL_PC: {
				struct map_session_data *tsd = (TBL_PC *)target;
				bool is_boss = (src && status_get_class_(src) == CLASS_BOSS);
				bool is_detector = status_has_mode(status, MD_DETECTOR);

				if (pc_isinvisible(tsd))
					return false;
				if (tsc) {
					if ((tsc->option&hide_flag) && !is_boss && (tsd->special_state.perfect_hiding || !is_detector))
						return false;
					if (tsc->data[SC_CLOAKINGEXCEED] && !is_boss && (tsd->special_state.perfect_hiding || is_detector))
						return false;
					if ((tsc->data[SC_CAMOUFLAGE] || tsc->data[SC_SUHIDE]) && !(is_boss || is_detector) && (!skill_id || !flag))
						return false;
				}
			}
			break;
		case BL_ITEM: //Allow targetting of items to pick'em up (or in the case of mobs, to loot them)
			//@TODO: Would be nice if this could be used to judge whether the player can or not pick up the item it targets [Skotlex]
			if (status_has_mode(status, MD_LOOTER))
				return true;
			return false;
		case BL_HOM:
		case BL_MER:
		case BL_ELEM:
			if (target->type == BL_HOM && (battle_config.hom_setting&HOMSET_NO_SUPPORT_SKILL) && skill_id && (skill_get_inf(skill_id)&INF_SUPPORT_SKILL) && skill_id != GN_WALLOFTHORN && battle_get_master(target) != src)
				return false; //Can't use support skills on Homunculus (Only Master/Self)
			if (target->type == BL_MER && (skill_id == PR_ASPERSIO || (skill_id >= SA_FLAMELAUNCHER && skill_id <= SA_SEISMICWEAPON)) && battle_get_master(target) != src)
				return false; //Can't use Weapon endow skills on Mercenary (Only Master)
			if (skill_id == AM_POTIONPITCHER && (target->type == BL_MER || target->type == BL_ELEM))
				return false; //Can't use Potion Pitcher on Mercenaries
			break;
		default:
			if (tsc && (tsc->option&hide_flag) && !status_has_mode(status, MD_DETECTOR))
				return false;
			break;
	}

	return true;
}

//Checks whether the source can see and chase target.
int status_check_visibility(struct block_list *src, struct block_list *target)
{
	int view_range;
	struct status_data *status = status_get_status_data(src);
	struct status_change *tsc = status_get_sc(target);

	switch( src->type ) {
		case BL_MOB:
			view_range = ((TBL_MOB *)src)->min_chase;
			break;
		case BL_PET:
			view_range = ((TBL_PET *)src)->db->range2;
			break;
		default:
			view_range = AREA_SIZE;
			break;
	}

	if( src->m != target->m || !check_distance_bl(src, target, view_range) )
		return 0;

	if( src->type == BL_NPC ) //NPCs don't care for the rest
		return 1;

	if( tsc ) {
		bool is_boss = (status_get_class_(src) == CLASS_BOSS);
		bool is_detector = status_has_mode(status, MD_DETECTOR);

		switch( target->type ) { //Check for chase-walk/hiding/cloaking opponents
			case BL_PC: {
					struct map_session_data *tsd = (TBL_PC *)target;

					if( ((tsc->option&(OPTION_HIDE|OPTION_CLOAK|OPTION_CHASEWALK)) || tsc->data[SC_CAMOUFLAGE] ||
						tsc->data[SC_STEALTHFIELD] || tsc->data[SC_SUHIDE]) && !is_boss &&
						(tsd->special_state.perfect_hiding || !is_detector) )
						return 0;
					if( tsc->data[SC_CLOAKINGEXCEED] && !is_boss && (tsd->special_state.perfect_hiding || is_detector) )
						return 0;
					if( tsc->data[SC__FEINTBOMB] )
						return 0;
				}
				break;
			default:
				if( ((tsc->option&(OPTION_HIDE|OPTION_CLOAK|OPTION_CHASEWALK)) || tsc->data[SC_CAMOUFLAGE] ||
					tsc->data[SC_STEALTHFIELD] || tsc->data[SC_SUHIDE]) && !(is_boss || is_detector) )
					return 0;
				break;
		}
	}

	return 1;
}

/**
 * Base ASPD value taken from the job tables
 * @param sd: Player object
 * @param status: Player status
 * @return base amotion after single/dual weapon and shield adjustments [RENEWAL]
 *	  base amotion after single/dual weapon and stats adjustments [PRE-RENEWAL]
 */
int status_base_amotion_pc(struct map_session_data *sd, struct status_data *status)
{
	int amotion;
	int classidx = pc_class2idx(sd->status.class_);
#ifdef RENEWAL_ASPD
	uint8 lv = 0, val = 0;
	float temp_aspd = 0;

	amotion = job_info[classidx].aspd_base[sd->weapontype1]; //Single weapon
	if( sd->status.shield )
		amotion += job_info[classidx].aspd_base[MAX_WEAPON_TYPE];
	else if( sd->weapontype2 && sd->equip_index[EQI_HAND_R] != sd->equip_index[EQI_HAND_L] )
		amotion += job_info[classidx].aspd_base[sd->weapontype2] / 4; //Dual-wield

	switch( sd->status.weapon ) {
		case W_BOW:	case W_MUSICAL:
		case W_WHIP:	case W_REVOLVER:
		case W_RIFLE:	case W_GATLING:
		case W_SHOTGUN:	case W_GRENADE:
			temp_aspd = status->dex * status->dex / 7.0f + status->agi * status->agi * 0.5f;
			break;
		default:
			temp_aspd = status->dex * status->dex / 5.0f + status->agi * status->agi * 0.5f;
			break;
	}
	temp_aspd = (float)(sqrt(temp_aspd) * 0.25f) + 0xc4;
	if( (lv = pc_checkskill(sd, SA_ADVANCEDBOOK)) > 0 && sd->status.weapon == W_BOOK )
		val += (lv - 1) / 2 + 1;
	if( (lv = pc_checkskill(sd, GS_SINGLEACTION)) > 0 && sd->status.weapon >= W_REVOLVER && sd->status.weapon <= W_GRENADE )
		val += ((lv + 1) / 2);
	amotion = ((int)(temp_aspd + ((float)(status_calc_aspd(&sd->bl, &sd->sc, true) + val) * status->agi / 200)) - min(amotion, 200));
#else
	//Angra Manyu disregards aspd_base and similar
	if( pc_checkequip2(sd, ITEMID_ANGRA_MANYU, EQI_ACC_L, EQI_MAX) )
		return 0;

	//Base weapon delay
	amotion = (sd->status.weapon < MAX_WEAPON_TYPE)
			? (job_info[classidx].aspd_base[sd->status.weapon]) //Single weapon
			: (job_info[classidx].aspd_base[sd->weapontype1]
			+ job_info[classidx].aspd_base[sd->weapontype2]) * 7 / 10; //Dual-wield

	//Percentual delay reduction from stats
	amotion -= amotion * (4 * status->agi + status->dex) / 1000;

	//Raw delay adjustment from bAspd bonus
	amotion += sd->bonus.aspd_add;
#endif

	return amotion;
}

/**
 * Base attack value calculated for units
 * @param bl: Object to get attack for [BL_CHAR|BL_NPC]
 * @param status: Object status
 * @return base attack
 */
static unsigned short status_base_atk(const struct block_list *bl, const struct status_data *status, int level)
{
	int flag = 0, str, dex, dstr;

	if( !(bl->type&battle_config.enable_baseatk) )
		return 0;

	if( bl->type == BL_PC ) {
		switch( ((TBL_PC *)bl)->status.weapon ) {
			case W_BOW:	case W_MUSICAL:
			case W_WHIP:	case W_REVOLVER:
			case W_RIFLE:	case W_GATLING:
			case W_SHOTGUN:	case W_GRENADE:
				flag = 1;
				break;
		}
	}
	if( flag ) {
#ifdef RENEWAL
		dstr =
#endif
		str = status->dex;
		dex = status->str;
	} else {
#ifdef RENEWAL
		dstr =
#endif
		str = status->str;
		dex = status->dex;
	}
	//Normally only players have base-atk, but homunc have a different batk
	//equation, hinting that perhaps non-players should use this for batk [Skotlex]
	switch( bl->type ) {
		case BL_HOM:
#ifdef RENEWAL
			str = 2 * (level + status_get_homstr(bl));
#else
			dstr = str / 10;
			str += dstr * dstr;
#endif
			break;
		case BL_PC:
#ifdef RENEWAL
			str = (dstr * 10 + dex * 10 / 5 + status->luk * 10 / 3 + level * 10 / 4) / 10;
#else
			dstr = str / 10;
			str += dstr * dstr;
			str += dex / 5 + status->luk / 5;
#endif
			break;
		default:
#ifdef RENEWAL
			str = dstr + level;
#else
			dstr = str / 10;
			str += dstr * dstr;
			str += dex / 5 + status->luk / 5;
#endif
			break;
	}
	return cap_value(str, 0, USHRT_MAX);
}

#ifdef RENEWAL
/**
 * Weapon attack value calculated for Players
 * @param watk: Weapon attack
 * @return weapon attack
 */
unsigned short status_weapon_atk(struct weapon_atk *watk)
{
	return watk->atk + watk->atk2;
}
#endif

#ifndef RENEWAL
unsigned short status_base_matk_min(const struct status_data *status) { return status->int_ + (status->int_ / 7) * (status->int_ / 7); }
unsigned short status_base_matk_max(const struct status_data *status) { return status->int_ + (status->int_ / 5) * (status->int_ / 5); }
#else
/**
 * Calculates minimum attack variance 80% from db's ATK1 for non BL_PC
 * status->batk (base attack) will be added in battle_calc_base_damage
 */
unsigned short status_base_atk_min(struct block_list *bl, const struct status_data *status, int level)
{
	switch( bl->type ) {
		case BL_PET:
		case BL_MOB:
		case BL_MER:
		case BL_ELEM:
			return status->rhw.atk * 80 / 100;
		case BL_HOM:
			return (status_get_homstr(bl) + status_get_homdex(bl)) / 5;
		default:
			return status->rhw.atk;
	}
}

/**
 * Calculates maximum attack variance 120% from db's ATK1 for non BL_PC
 * status->batk (base attack) will be added in battle_calc_base_damage
 */
unsigned short status_base_atk_max(struct block_list *bl, const struct status_data *status, int level)
{
	switch( bl->type ) {
		case BL_PET:
		case BL_MOB:
		case BL_MER:
		case BL_ELEM:
			return status->rhw.atk * 120 / 100;
		case BL_HOM:
			return (status_get_homluk(bl) + status_get_homstr(bl) + status_get_homdex(bl)) / 3;
		default:
			return status->rhw.atk2;
	}
}

/**
 * Calculates base magic attack
 */
unsigned short status_base_matk(struct block_list *bl, const struct status_data *status, int level)
{
	switch( bl->type ) {
		case BL_PET:
		case BL_MOB:
		case BL_MER:
		case BL_ELEM:
			return status->int_ + level;
		case BL_HOM:
			return status_get_homint(bl) + level;
		case BL_PC:
		default:
			return status->int_ + (status->int_ / 2) + (status->dex / 5) + (status->luk / 3) + (level / 4);
	}
}
#endif

/**
 * Get bl's matk_max and matk_min values depending on flag
 * @param flag:
 *  0 - Get MATK
 *  1 - Get MATK w/o SC bonuses
 *  2 - Get unmodified MATK
 *  3 - Get MATK w/o eMATK & SC bonuses
 */
void status_get_matk_sub(struct block_list *bl, unsigned short *matk_max, unsigned short *matk_min, uint8 flag)
{
	struct status_data *status;
	struct status_change *sc;
	struct map_session_data *sd;
	bool same_matk = false;

	nullpo_retv(bl);

	status = status_get_status_data(bl);
	sc = status_get_sc(bl);
	sd = BL_CAST(BL_PC, bl);

#ifdef RENEWAL
	//RE MATK Formula
	//MATK = (sMATK + wMATK + eMATK) * Multiplicative Modifiers
	*matk_min = status_base_matk(bl, status, status_get_lv(bl));

	//Any +MATK you get from skills and cards, including cards in weapon, is added here
	if( flag < 3 ) {
		if( sd && sd->bonus.ematk > 0 )
			*matk_min += sd->bonus.ematk;
		if( !flag ) {
			*matk_min = status_calc_ematk(bl, sc, *matk_min, false);
			status->rhw.matk = status_calc_ematk(bl, sc, status->rhw.matk, true);
		}
	}

	*matk_max = *matk_min;

	switch( bl->type ) {
		case BL_PC:
			//This is the only portion in MATK that varies depending on the weapon level and refinement rate
			if( (status->rhw.matk + status->lhw.matk) > 0 ) {
				int wMatk = status->rhw.matk + status->lhw.matk; //Left and right MATK stacks
				int variance = wMatk * status->rhw.wlv / 10; //Only use right hand weapon level

				*matk_min += wMatk - variance;
				*matk_max += wMatk + variance;
			}
			break;
		case BL_PET:
		case BL_MOB:
		case BL_MER:
		case BL_ELEM:
			*matk_min += status->rhw.matk * 70 / 100;
			*matk_max += status->rhw.matk * 130 / 100;
			break;
		case BL_HOM:
			*matk_min += (status_get_homint(bl) + status_get_homdex(bl)) / 5;
			*matk_max += (status_get_homluk(bl) + status_get_homint(bl) + status_get_homdex(bl)) / 3;
			break;
	}
#else
	*matk_min = status_base_matk_min(status) + (sd ? sd->bonus.ematk : 0);
	*matk_max = status_base_matk_max(status) + (sd ? sd->bonus.ematk : 0);
#endif

	if( sd ) {
		uint8 passive_matk_rate = 0;

		if( pc_checkskill(sd, SU_POWEROFLAND) > 0 && pc_checkskill_summoner(sd, TYPE_PLANT) >= 20 )
			passive_matk_rate += 20;
		if( (sd->matk_rate + passive_matk_rate) != 100 ) {
			*matk_min = (*matk_min) * (sd->matk_rate + passive_matk_rate) / 100;
			*matk_max = (*matk_max) * (sd->matk_rate + passive_matk_rate) / 100;
		}
	}

	if( sc && sc->count ) {
		/* Another SC modifier goes here */
		if( sc->data[SC_RECOGNIZEDSPELL] )
			same_matk = true;
	}

	if( (bl->type&BL_HOM) && (battle_config.hom_setting&HOMSET_SAME_MATK) )
		same_matk = true;

	if( same_matk )
		*matk_min = *matk_max;

#ifdef RENEWAL
	if( sd ) {
		int overrefine = sd->right_weapon.overrefine + 1;

		*matk_max += rnd()%overrefine; //Over refine matk bonus not affected by Recognized Spell
	}
#endif

	*matk_min = status_calc_matk(bl, sc, *matk_min, false);
	*matk_max = status_calc_matk(bl, sc, *matk_max, false);
}

/**
 * Gets a random matk value depending on min matk and max matk
 */
unsigned short status_get_rand_matk(unsigned short matk_max, unsigned short matk_min)
{
	if( matk_max > matk_min )
		return matk_min + rnd()%(matk_max - matk_min);
	else
		return matk_min;
}

/**
 * Get bl's matk value depending on flag
 * @author [malufett]
 */
int status_get_matk(struct block_list *bl, uint8 flag)
{
	struct status_data *status;
	unsigned short matk_max, matk_min;

	nullpo_ret(bl);

	if( flag < 1 || flag > 3 ) {
		ShowError("status_get_matk: Unknown flag %d!\n", flag);
		return 0;
	}

	if( !(status = status_get_status_data(bl)) )
		return 0;

	//Just get MATK
	if( flag == 2 )
		return status_get_rand_matk(status->matk_max, status->matk_min);

	status_get_matk_sub(bl, &matk_max, &matk_min, flag);

	//Get modified from sc MATK
	return status_get_rand_matk(matk_max, matk_min);
}

/**
 * Updates bl's MATK values
 */
static void status_update_matk(struct block_list *bl)
{
	struct status_data *status;
	struct status_change *sc;
	unsigned short matk_max, matk_min;

	nullpo_retv(bl);

	if( !(status = status_get_status_data(bl)) )
		return;

	if( !(sc = status_get_sc(bl)) )
		return;

	status_get_matk_sub(bl, &matk_max, &matk_min, 0);

	//Update MATK
	status->matk_min = status_calc_matk(bl, sc, matk_min, true);
	status->matk_max = status_calc_matk(bl, sc, matk_max, true);
}

//Fills in the misc data that can be calculated from the other status info (except for level)
void status_calc_misc(struct block_list *bl, struct status_data *status, int level)
{
	int stat;

	if( bl->type != BL_PC ) { //Non players get the value set, players need to stack with previous bonuses
		status->batk = status->def2 = status->mdef2 = status->cri = status->flee2 = 0;
		if( bl->type != BL_ELEM )
			status->hit = status->flee = 0;
	}

#ifndef RENEWAL
	if( bl->type != BL_ELEM ) {
		//Hit
		stat = status->hit;
		stat += level + status->dex;
		status->hit = cap_value(stat, 1, SHRT_MAX);
		//Flee
		stat = status->flee;
		stat += level + status->agi;
		status->flee = cap_value(stat, 1, SHRT_MAX);
		//Def2
		stat = status->def2;
		stat += status->vit;
		status->def2 = cap_value(stat, 0, SHRT_MAX);
		//Mdef2
		stat = status->mdef2;
		stat += status->int_ + (status->vit>>1);
		status->mdef2 = cap_value(stat, 0, SHRT_MAX);
	}
#else
	if( bl->type == BL_HOM ) {
		//Hit
		stat = level + status->dex + 150;
		status->hit = cap_value(stat, 1, SHRT_MAX);
		//Flee
		stat = level + status_get_homagi(bl);
		status->flee = cap_value(stat, 1, SHRT_MAX);
		//Def2
		stat = (int)(status_get_homvit(bl) + ((float)status_get_homagi(bl) / 2));
		status->def2 = cap_value(stat, 0, SHRT_MAX);
		//Mdef2
		stat = (int)((float)(status_get_homvit(bl) + status_get_homint(bl)) / 2);
		status->mdef2 = cap_value(stat, 0, SHRT_MAX);
		//Def
		stat = status->def;
		stat += (int)(status_get_homvit(bl) + ((float)level / 2));
		status->def = cap_value(stat, 0, SHRT_MAX);
		//Mdef
		stat = (int)(((float)(status_get_homvit(bl) + level) / 4) + ((float)status_get_homint(bl) / 2));
		status->mdef = cap_value(stat, 0, SHRT_MAX);
	} else if( bl->type != BL_ELEM ) {
		//Base level + (every 1 dex = +1 hit) + (every 3 luk = +1 hit) + 175
		stat = status->hit;
		stat += (int)(level + status->dex + (bl->type == BL_PC ? ((float)status->luk / 3) + 175 : 170));
		status->hit = cap_value(stat, 1, SHRT_MAX);
		//Base level + (every 1 agi = +1 flee) + (every 5 luk = +1 flee) + 100
		stat = status->flee;
		stat += (int)(level + status->agi + (bl->type == BL_MER ? 0 : (bl->type == BL_PC ? ((float)status->luk / 5) : 0)) + 100);
		status->flee = cap_value(stat, 1, SHRT_MAX);
		if( bl->type == BL_MER )
			stat = (int)(status->vit + ((float)level / 10) + ((float)status->vit / 5));
		else { //Base level + (every 2 vit = +1 def) + (every 5 agi = +1 def)
			stat = status->def2;
			stat += (int)(((float)(level + status->vit) / 2) + (bl->type == BL_PC ? ((float)status->agi / 5) : 0));
		}
		status->def2 = cap_value(stat, 0, SHRT_MAX);
		if( bl->type == BL_MER )
			stat = (int)(((float)level / 10) + ((float)status->int_ / 5));
		else { //(Every 4 base level = +1 mdef) + (every 1 int = +1 mdef) + (every 5 dex = +1 mdef) + (every 5 vit = +1 mdef)
			stat = status->mdef2;
			stat += (int)(bl->type == BL_PC ? (status->int_ + ((float)level / 4) + ((float)(status->dex + status->vit) / 5)) : ((float)(level + status->int_) / 4));
		}
		status->mdef2 = cap_value(stat, 0, SHRT_MAX);
	}
	//Atk
	if( bl->type != BL_PC ) {
		status->rhw.atk2 = status_base_atk_max(bl, status, level);
		status->rhw.atk = status_base_atk_min(bl, status, level);
	}
#endif
	//Matk
	status_get_matk_sub(bl, &status->matk_max, &status->matk_min, 0);

	//Critical
	if( bl->type&battle_config.enable_critical ) {
		stat = status->cri;
		stat += (int)(10 + ((float)(status->luk * 10) / 3)); //(Every 1 luk = +0.3 critical)
		status->cri = cap_value(stat, 1, SHRT_MAX);
	} else
		status->cri = 0;

	if( bl->type&battle_config.enable_perfect_flee ) {
		stat = status->flee2;
		stat += status->luk + 10; //(Every 10 luk = +1 perfect flee)
		status->flee2 = cap_value(stat, 0, SHRT_MAX);
	} else
		status->flee2 = 0;

	if( status->batk ) {
		int temp = status->batk + status_base_atk(bl, status, level);

		status->batk = cap_value(temp, 0, USHRT_MAX);
	} else
		status->batk = status_base_atk(bl, status, level);

	if( status->cri ) {
		switch( bl->type ) {
			case BL_MOB:
				if( battle_config.mob_critical_rate != 100 )
					status->cri = cap_value(status->cri * battle_config.mob_critical_rate / 100, 1, SHRT_MAX);
				if( !status->cri && battle_config.mob_critical_rate )
					status->cri = 10;
				break;
			case BL_PC:
				//Players don't have a critical adjustment setting as of yet
				break;
			default:
				if( battle_config.critical_rate != 100 )
					status->cri = cap_value(status->cri * battle_config.critical_rate / 100, 1, SHRT_MAX);
				if( !status->cri && battle_config.critical_rate )
					status->cri = 10;
				break;
		}
	}

	if( bl->type&BL_REGEN )
		status_calc_regen(bl, status, status_get_regen_data(bl));
}

//Skotlex: Calculates the initial status for the given mob
//first will only be false when the mob leveled up or got a GuardUp level
int status_calc_mob_(struct mob_data *md, enum e_status_calc_opt opt)
{
	struct status_data *status;
	struct block_list *mbl = NULL;
	int flag = 0, guardup_lv = 0;

	if (opt&SCO_FIRST) { //Set basic level on respawn
		if (md->level > 0 && md->level <= MAX_LEVEL && md->level != md->db->lv)
			;
		else
			md->level = md->db->lv;
	}

	//Check if we need custom base-status
	if (battle_config.mobs_level_up && md->level > md->db->lv)
		flag |= 1;

	if (md->special_state.size)
		flag |= 2;

	if (md->guardian_data && md->guardian_data->g && (guardup_lv = guild_checkskill(md->guardian_data->g, GD_GUARDUP)))
		flag |= 4;

	if (battle_config.slaves_inherit_speed && md->master_id)
		flag |= 8;

	if (md->master_id && md->special_state.ai > AI_ATTACK)
		flag |= 16;

	if (md->master_id && battle_config.slaves_inherit_mode)
		flag |= 32;

	if (!flag) { //No special status required
		if (md->base_status) {
			aFree(md->base_status);
			md->base_status = NULL;
		}
		if (opt&SCO_FIRST)
			memcpy(&md->status, &md->db->status, sizeof(struct status_data));
		return 0;
	}

	if (!md->base_status)
		md->base_status = (struct status_data *)aCalloc(1, sizeof(struct status_data));

	status = md->base_status;
	memcpy(status, &md->db->status, sizeof(struct status_data));

	if (flag&(8|16))
		mbl = map_id2bl(md->master_id);

	if (flag&8 && mbl) {
		struct status_data *mstatus = status_get_base_status(mbl);

		if (mstatus && (battle_config.slaves_inherit_speed&(status_has_mode(mstatus, MD_CANMOVE) ? 1 : 2)))
			status->speed = mstatus->speed;
		if (status->speed < 2) //Minimum for the unit to function properly
			status->speed = 2;
	}

	if (flag&32)
		status_calc_slave_mode(md, map_id2md(md->master_id));

	if (flag&1) { //Increase from mobs leveling up [Valaris]
		int diff = md->level - md->db->lv;

		status->str += diff;
		status->agi += diff;
		status->vit += diff;
		status->int_ += diff;
		status->dex += diff;
		status->luk += diff;
		status->max_hp += diff * status->vit;
		status->max_sp += diff * status->int_;
		status->hp = status->max_hp;
		status->sp = status->max_sp;
		status->speed -= cap_value(diff, 0, status->speed - 10);
	}

	if (flag&2 && battle_config.mob_size_influence) { //Change for sized monsters [Valaris]
		if (md->special_state.size == SZ_MEDIUM) {
			status->max_hp /= 2;
			status->max_sp /= 2;
			if (!status->max_hp)
				status->max_hp = 1;
			if (!status->max_sp)
				status->max_sp = 1;
			status->hp = status->max_hp;
			status->sp = status->max_sp;
			status->str /= 2;
			status->agi /= 2;
			status->vit /= 2;
			status->int_ /= 2;
			status->dex /= 2;
			status->luk /= 2;
			if (!status->str)
				status->str = 1;
			if (!status->agi)
				status->agi = 1;
			if (!status->vit)
				status->vit = 1;
			if (!status->int_)
				status->int_ = 1;
			if (!status->dex)
				status->dex = 1;
			if (!status->luk)
				status->luk = 1;
		} else if (md->special_state.size == SZ_BIG) {
			status->max_hp *= 2;
			status->max_sp *= 2;
			status->hp = status->max_hp;
			status->sp = status->max_sp;
			status->str *= 2;
			status->agi *= 2;
			status->vit *= 2;
			status->int_ *= 2;
			status->dex *= 2;
			status->luk *= 2;
		}
	}

	status_calc_misc(&md->bl, status, md->level);

	if (flag&4) { //Strengthen Guardians
		struct guild_castle *gc = NULL;

		if (!(gc = guild_mapname2gc(map[md->bl.m].name)))
			ShowError("status_calc_mob: No castle set at map %s\n", map[md->bl.m].name);
		else if (gc->castle_id < 24 && gc->castle_id > 33) {
			if (md->mob_id == MOBID_EMPERIUM) {
#ifdef RENEWAL
				status->max_hp += 50 * (gc->defense / 5);
#else
				status->max_hp += 1000 * gc->defense;
#endif
			} else {
				status->max_hp += 1000 * gc->defense;		
				status->batk += status->batk * (2 * guardup_lv + 8) / 100;
				status->rhw.atk += status->rhw.atk * (2 * guardup_lv + 8) / 100;
#ifndef RENEWAL
				status->rhw.atk2 += status->rhw.atk2 * (2 * guardup_lv + 8) / 100;
#endif
				status->aspd_rate -= 10 * (2 * guardup_lv + 3);
			}
			status->hp = status->max_hp;
			status->def += (gc->defense + 2) / 3;
			status->mdef += (gc->defense + 2) / 3;
		}
	}

	if (flag&16 && mbl) {
		struct unit_data *ud = unit_bl2ud(mbl);

		//Remove special AI when this is used by regular mobs
		if (mbl->type == BL_MOB && !((TBL_MOB *)mbl)->special_state.ai)
			md->special_state.ai = AI_NONE;
		if (ud) { //Different levels of HP according to skill level
			switch (ud->skill_id) {
				case AM_SPHEREMINE:
					status->max_hp = 2000 + 400 * ud->skill_lv;
					break;
				case AM_CANNIBALIZE:
					status->max_hp = 1500 + 200 * ud->skill_lv + 10 * status_get_lv(mbl);
					status->mode |= MD_AGGRESSIVE|MD_CANATTACK;
					break;
				case NC_SILVERSNIPER: {
						short atkbonus[5] = { 0, 200, 400, 800, 1000 };

#ifdef RENEWAL
						status->rhw.atk += atkbonus[ud->skill_lv - 1];
#else
						status->rhw.atk = status->rhw.atk2 += atkbonus[ud->skill_lv - 1];
#endif
						status->max_hp = status_get_max_hp(mbl) / 3 + ud->skill_lv * 1000 + status_get_lv(mbl) * 12;
					}
					break;
				case NC_MAGICDECOY:
#ifdef RENEWAL
					status->rhw.atk2 =
#else
					status->matk_min = status->matk_max =
#endif
						250 + 50 * ud->skill_lv;
					status->max_hp = status_get_max_sp(mbl) * 4 + ud->skill_lv * 1000 + status_get_lv(mbl) * 12;
					break;
				case KO_ZANZOU:
					status->max_hp = 3000 + 3000 * ud->skill_lv + status_get_max_sp(battle_get_master(mbl));
					break;
				case MH_SUMMON_LEGION: {
						int homblvl = status_get_lv(mbl);

						status->rhw.atk = 100 * (ud->skill_lv + 5) / 2;
#ifdef RENEWAL
						status->def = 40 * (ud->skill_lv + 1);
						status->amotion = max(10 * (2 * (20 - ud->skill_lv) - homblvl / 10),100);
#else
						status->rhw.atk2 = status->rhw.atk;
						status->def = 10 + 5 * ud->skill_lv;
#endif
						status->max_hp = 10 * (100 * (ud->skill_lv + 2) + homblvl);
						status->mode |= MD_AGGRESSIVE|MD_CANATTACK;
					}
					break;
			}
			status->hp = status->max_hp;
		}
	}

	if (opt&SCO_FIRST) //Initial battle status
		memcpy(&md->status, status, sizeof(struct status_data));

	return 1;
}

//Skotlex: Calculates the stats of the given pet
void status_calc_pet_(struct pet_data *pd, enum e_status_calc_opt opt)
{
	nullpo_retv(pd);

	if (opt&SCO_FIRST) {
		memcpy(&pd->status, &pd->db->status, sizeof(struct status_data));
		pd->status.mode = MD_CANMOVE; //Pets discard all modes, except walking
		pd->status.class_ = CLASS_NORMAL;
		pd->status.speed = pet_get_walk_speed(pd->master);
		//Attack support requires the pet to be able to attack
		if (battle_config.pet_attack_support || battle_config.pet_damage_support)
			pd->status.mode |= MD_CANATTACK;
	}

	if (battle_config.pet_lv_rate && pd->master) {
		struct map_session_data *sd = pd->master;
		int lv;

		lv = max(sd->status.base_level * battle_config.pet_lv_rate / 100, 1);
		if (lv != pd->pet.level || (opt&SCO_FIRST)) {
			struct status_data *bstat = &pd->db->status, *status = &pd->status;

			pd->pet.level = lv;
			if (!(opt&SCO_FIRST)) //Lv Up animation
				clif_misceffect(&pd->bl, 0);
			status->rhw.atk = (bstat->rhw.atk * lv) / pd->db->lv;
			status->rhw.atk2 = (bstat->rhw.atk2 * lv) / pd->db->lv;
			status->str = (bstat->str * lv) / pd->db->lv;
			status->agi = (bstat->agi * lv) / pd->db->lv;
			status->vit = (bstat->vit * lv) / pd->db->lv;
			status->int_ = (bstat->int_ * lv) / pd->db->lv;
			status->dex = (bstat->dex * lv) / pd->db->lv;
			status->luk = (bstat->luk * lv) / pd->db->lv;
			status->rhw.atk = cap_value(status->rhw.atk, 1, battle_config.pet_max_atk1);
			status->rhw.atk2 = cap_value(status->rhw.atk2, 2, battle_config.pet_max_atk2);
			status->str = cap_value(status->str, 1, battle_config.pet_max_stats);
			status->agi = cap_value(status->agi, 1, battle_config.pet_max_stats);
			status->vit = cap_value(status->vit, 1, battle_config.pet_max_stats);
			status->int_= cap_value(status->int_, 1, battle_config.pet_max_stats);
			status->dex = cap_value(status->dex, 1, battle_config.pet_max_stats);
			status->luk = cap_value(status->luk, 1, battle_config.pet_max_stats);
			status_calc_misc(&pd->bl, &pd->status, lv);
			if (!(opt&SCO_FIRST)) //Not done the first time because the pet is not visible yet
				clif_send_petstatus(sd);
		}
	} else if (opt&SCO_FIRST) {
		status_calc_misc(&pd->bl, &pd->status, pd->db->lv);
		if (!battle_config.pet_lv_rate && pd->pet.level != pd->db->lv)
			pd->pet.level = pd->db->lv;
	}

	pd->rate_fix = min(1000 * (pd->pet.intimate - battle_config.pet_support_min_friendly) / (1000 - battle_config.pet_support_min_friendly) + 500, UINT_MAX);
	pd->rate_fix = min(apply_rate(pd->rate_fix, battle_config.pet_support_rate), UINT_MAX);
}

/** [Cydh]
 * Get HP bonus modifiers
 * @param bl: block_list that will be checked
 * @param type: type of e_status_bonus (STATUS_BONUS_FIX or STATUS_BONUS_RATE)
 * @return bonus: total bonus for HP
 */
int status_get_hpbonus(struct block_list *bl, enum e_status_bonus type) {
	int bonus = 0;

	if (type == STATUS_BONUS_FIX) {
		struct status_change *sc = status_get_sc(bl);

		if (bl->type == BL_PC) { //Only for BL_PC
			struct map_session_data *sd = map_id2sd(bl->id);
			uint8 i;

			bonus += sd->bonus.hp;
			if ((i = pc_checkskill(sd,CR_TRUST)) > 0)
				bonus += i * 200;
			if (pc_checkskill(sd,SU_SPRITEMABLE) > 0)
				bonus += 1000;
			if (pc_checkskill(sd,SU_POWEROFSEA) > 0) {
				bonus += 1000;
				if (pc_checkskill_summoner(sd,TYPE_SEAFOOD) >= 20)
					bonus += 3000;
			}
#ifndef HP_SP_TABLES
			if ((sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE && sd->status.base_level >= 99)
				bonus += 2000; //Super novice lvl 99 hp bonus
			if ((sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE && sd->status.base_level >= 150)
				bonus += 2000; //Supernovice lvl150 hp bonus
#endif
		}
		if (sc) { //Bonus by SC
			if (sc->data[SC_INCMHP])
				bonus += sc->data[SC_INCMHP]->val1;
			if (sc->data[SC_INSPIRATION])
				bonus += 600 * sc->data[SC_INSPIRATION]->val1;
			if (sc->data[SC_LERADSDEW])
				bonus += sc->data[SC_LERADSDEW]->val3;
			if (sc->data[SC_PROMOTE_HEALTH_RESERCH])
				bonus += sc->data[SC_PROMOTE_HEALTH_RESERCH]->val4;
			if (sc->data[SC_SOLID_SKIN_OPTION])
				bonus += 2000;
			if (sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2)
				bonus += 500;
			if (sc->data[SC_MTF_MHP])
				bonus += sc->data[SC_MTF_MHP]->val1;
			if (sc->data[SC_SWORDCLAN])
				bonus += 30;
			if (sc->data[SC_ARCWANDCLAN])
				bonus += 30;
			if (sc->data[SC_GOLDENMACECLAN])
				bonus += 30;
			if (sc->data[SC_CROSSBOWCLAN])
				bonus += 30;
			if (sc->data[SC_MARIONETTE])
				bonus -= 1000;
		}
	} else if (type == STATUS_BONUS_RATE) {
		struct status_change *sc = status_get_sc(bl);

		if (bl->type == BL_PC) { //Only for BL_PC
			struct map_session_data *sd = map_id2sd(bl->id);

			bonus += -100 + sd->hprate; //Default hprate is 100, so it should be add 0%
		}
		if (sc) { //Bonus by SC
			//Increasing
			if (sc->data[SC_INCMHPRATE])
				bonus += sc->data[SC_INCMHPRATE]->val1;
			if (sc->data[SC_APPLEIDUN])
				bonus += sc->data[SC_APPLEIDUN]->val2;
			if (sc->data[SC_DELUGE])
				bonus += sc->data[SC_DELUGE]->val2;
			if (sc->data[SC_BERSERK])
				bonus += 200; //+200%
			if (sc->data[SC_MERC_HPUP])
				bonus += sc->data[SC_MERC_HPUP]->val2;
			if (sc->data[SC_LAUDAAGNUS])
				bonus += 2 + 2 * sc->data[SC_LAUDAAGNUS]->val1;
			if (sc->data[SC_EPICLESIS])
				bonus += sc->data[SC_EPICLESIS]->val2;
			if (sc->data[SC_FORCEOFVANGUARD])
				bonus += 3 * sc->data[SC_FORCEOFVANGUARD]->val1;
			if (sc->data[SC_INSPIRATION])
				bonus += 5 * sc->data[SC_INSPIRATION]->val1;
			if (sc->data[SC_RAISINGDRAGON])
				bonus += 2 + sc->data[SC_RAISINGDRAGON]->val1;
			if (sc->data[SC_GT_REVITALIZE])
				bonus += 2 * sc->data[SC_GT_REVITALIZE]->val1;
			if (sc->data[SC_MUSTLE_M])
				bonus += sc->data[SC_MUSTLE_M]->val1;
			if (sc->data[SC_PETROLOGY_OPTION])
				bonus += sc->data[SC_PETROLOGY_OPTION]->val2;
			if (sc->data[SC_POWER_OF_GAIA])
				bonus += sc->data[SC_POWER_OF_GAIA]->val3;
			if (sc->data[SC_CURSED_SOIL_OPTION])
				bonus += sc->data[SC_CURSED_SOIL_OPTION]->val2;
			if (sc->data[SC_UPHEAVAL_OPTION])
				bonus += sc->data[SC_UPHEAVAL_OPTION]->val3;
			if (sc->data[SC_ANGRIFFS_MODUS])
				bonus += 5 * sc->data[SC_ANGRIFFS_MODUS]->val1;
			if (sc->data[SC_FRIGG_SONG])
				bonus += sc->data[SC_FRIGG_SONG]->val2;
			//Decreasing
			if (sc->data[SC_VENOMBLEED])
				bonus -= 15;
			if (sc->data[SC__WEAKNESS])
				bonus -= sc->data[SC__WEAKNESS]->val2;
			if (sc->data[SC_MYSTERIOUS_POWDER])
				bonus -= sc->data[SC_MYSTERIOUS_POWDER]->val1;
			if (sc->data[SC_GT_CHANGE]) // MaxHP decrease: [Skill Level x 4] %
				bonus -= 4 * sc->data[SC_GT_CHANGE]->val1;
			if (sc->data[SC_BEYONDOFWARCRY])
				bonus -= sc->data[SC_BEYONDOFWARCRY]->val4;
			if (sc->data[SC_EQC])
				bonus -= sc->data[SC_EQC]->val3;
		}
	}

	return min(bonus,INT_MAX);
}

/** [Cydh]
 * Get SP bonus modifiers
 * @param bl: block_list that will be checked
 * @param type: type of e_status_bonus (STATUS_BONUS_FIX or STATUS_BONUS_RATE)
 * @return bonus: total bonus for SP
 */
int status_get_spbonus(struct block_list *bl, enum e_status_bonus type) {
	int bonus = 0;

	if (type == STATUS_BONUS_FIX) {
		struct status_change *sc = status_get_sc(bl);

		if (bl->type == BL_PC) { //Only for BL_PC
			struct map_session_data *sd = map_id2sd(bl->id);
			uint8 i;

			bonus += sd->bonus.sp;
			if ((i = pc_checkskill(sd,SL_KAINA)) > 0)
				bonus += 30 * i;
			if ((i = pc_checkskill(sd,RA_RESEARCHTRAP)) > 0)
				bonus += 200 + 20 * i;
			if ((i = pc_checkskill(sd,WM_LESSON)) > 0)
				bonus += 30 * i;
			if (pc_checkskill(sd,SU_SPRITEMABLE) > 0)
				bonus += 100;
			if (pc_checkskill(sd,SU_POWEROFSEA) > 0) {
				bonus += 100;
				if (pc_checkskill_summoner(sd,TYPE_SEAFOOD) >= 20)
					bonus += 300;
			}
		}
		if (sc) { //Bonus by SC
			if (sc->data[SC_INCMSP])
				bonus += sc->data[SC_INCMSP]->val1;
			if (sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 3)
				bonus += 50;
			if (sc->data[SC_MTF_MSP])
				bonus += sc->data[SC_MTF_MSP]->val1;
			if (sc->data[SC_SWORDCLAN])
				bonus += 10;
			if (sc->data[SC_ARCWANDCLAN])
				bonus += 10;
			if (sc->data[SC_GOLDENMACECLAN])
				bonus += 10;
			if (sc->data[SC_CROSSBOWCLAN])
				bonus += 10;
		}
	} else if (type == STATUS_BONUS_RATE) {
		struct status_change *sc = status_get_sc(bl);

		if (bl->type == BL_PC) { //Only for BL_PC
			struct map_session_data *sd = map_id2sd(bl->id);
			uint8 i;

			bonus += -100 + sd->sprate; //Default sprate is 100, so it should be add 0%
			if ((i = pc_checkskill(sd,HP_MEDITATIO)) > 0)
				bonus += i;
			if ((i = pc_checkskill(sd,HW_SOULDRAIN)) > 0)
				bonus += 2 * i;
		}
		if (sc) { //Bonus by SC
			if (sc->data[SC_INCMSPRATE])
				bonus += sc->data[SC_INCMSPRATE]->val1;
			if (sc->data[SC_SERVICE4U])
				bonus += sc->data[SC_SERVICE4U]->val2;
			if (sc->data[SC_MERC_SPUP])
				bonus += sc->data[SC_MERC_SPUP]->val2;
			if (sc->data[SC_RAISINGDRAGON])
				bonus += 2 + sc->data[SC_RAISINGDRAGON]->val1;
			if (sc->data[SC_ENERGY_DRINK_RESERCH])
				bonus += sc->data[SC_ENERGY_DRINK_RESERCH]->val4;
			if (sc->data[SC_VITATA_500])
				bonus += sc->data[SC_VITATA_500]->val2;
			if (sc->data[SC_LIFE_FORCE_F])
				bonus += sc->data[SC_LIFE_FORCE_F]->val1;
		}
	}

	return min(bonus,INT_MAX);
}

/**
 * Get final MaxHP or MaxSP for player. References: http://irowiki.org/wiki/Max_HP and http://irowiki.org/wiki/Max_SP
 * The calculation needs base_level, battle_status (vit or int), additive modifier, and multiplicative modifier
 * @param sd Player
 * @param isHP true - calculates MaxHP, false - calculated MaxSP
 * @return dmax The max value of HP or SP
 */
unsigned int status_calc_maxhpsp_pc(struct map_session_data *sd, bool isHP)
{
	double dmax = 0;
	uint16 idx, level, job_id;

	nullpo_ret(sd);

	job_id = pc_mapid2jobid(sd->class_, sd->status.sex);
	idx = pc_class2idx(job_id);
	level = umax(sd->status.base_level, 1);

	if (isHP) { //Calculates MaxHP
		dmax = job_info[idx].base_hp[level - 1] * (1 + max(sd->battle_status.vit, 1) * 0.01) * (sd->class_&JOBL_UPPER ? 1.25 : (pc_is_taekwon_ranker(sd) ? 3 : 1));
		dmax += status_get_hpbonus(&sd->bl, STATUS_BONUS_FIX);
		dmax += (int64)(dmax * status_get_hpbonus(&sd->bl, STATUS_BONUS_RATE) / 100); //Aegis accuracy
	} else { //Calculates MaxSP
		dmax = job_info[idx].base_sp[level - 1] * (1 + max(sd->battle_status.int_, 1) * 0.01) * (sd->class_&JOBL_UPPER ? 1.25 : (pc_is_taekwon_ranker(sd) ? 3 : 1));
		dmax += status_get_spbonus(&sd->bl, STATUS_BONUS_FIX);
		dmax += (int64)(dmax * status_get_spbonus(&sd->bl, STATUS_BONUS_RATE) / 100);
	}

	return (unsigned int)cap_value(dmax, 1, UINT_MAX);
}

/**
 * Calculates player's weight
 * @param sd: Player object
 * @param flag: Calculation type
 *   CALCWT_ITEM - Item weight
 *   CALCWT_MAXBONUS - Skill/Status/Configuration max weight bonus
 * @return false - failed, true - success
 */
bool status_calc_weight(struct map_session_data *sd, enum e_status_calc_weight_opt flag)
{
	int b_weight, b_max_weight, lv, i;
	struct status_change *sc;

	nullpo_retr(false, sd);

	sc = &sd->sc;
	b_max_weight = sd->max_weight; //Store max weight for later comparison
	b_weight = sd->weight; //Store current weight for later comparison
	sd->max_weight = job_info[pc_class2idx(sd->status.class_)].max_weight_base + sd->status.str * 300; //Recalculate max weight

	if (flag&CALCWT_ITEM) {
		sd->weight = 0; //Reset current weight
		for (i = 0; i < MAX_INVENTORY; i++) {
			if (!sd->inventory.u.items_inventory[i].nameid || !sd->inventory_data[i])
				continue;
			sd->weight += sd->inventory_data[i]->weight * sd->inventory.u.items_inventory[i].amount;
		}
	}

	//Skill/Status bonus weight increases
	if (flag&CALCWT_MAXBONUS) {
		sd->max_weight += sd->add_max_weight; //From bAddMaxWeight
		if ((lv = pc_checkskill(sd, MC_INCCARRY)) > 0)
			sd->max_weight += lv * 2000;
		if (pc_isriding(sd))
			sd->max_weight += 10000;
		if (pc_isridingdragon(sd) && (lv = pc_checkskill(sd, RK_DRAGONTRAINING)) > 0)
			sd->max_weight += 5000 + lv * 2000;
		if (sc->data[SC_KNOWLEDGE])
			sd->max_weight += sd->max_weight * sc->data[SC_KNOWLEDGE]->val1 / 10;
		if ((lv = pc_checkskill(sd, ALL_INCCARRY)) > 0)
			sd->max_weight += lv * 2000;
		if (pc_ismadogear(sd))
			sd->max_weight += 15000;
	}

	//Update the client if the new weight calculations don't match
	if (b_weight != sd->weight)
		clif_updatestatus(sd, SP_WEIGHT);
	if (b_max_weight != sd->max_weight) {
		clif_updatestatus(sd, SP_MAXWEIGHT);
		pc_updateweightstatus(sd);
	}

	return true;
}

/**
 * Calculates player's cart weight
 * @param sd: Player object
 * @param flag: Calculation type
 *   CALCWT_ITEM - Cart item weight
 *   CALCWT_MAXBONUS - Skill/Status/Configuration max weight bonus
 *   CALCWT_CARTSTATE - Whether to check for cart state
 * @return false - failed, true - success
 */
bool status_calc_cart_weight(struct map_session_data *sd, enum e_status_calc_weight_opt flag)
{
	int b_cart_weight_max, i;

	nullpo_retr(false, sd);

	if (!pc_iscarton(sd) && !(flag&CALCWT_CARTSTATE))
		return false;

	b_cart_weight_max = sd->cart_weight_max; //Store cart max weight for later comparison
	sd->cart_weight_max = battle_config.max_cart_weight; //Recalculate max weight

	if (flag&CALCWT_ITEM) {
		sd->cart_weight = 0; //Reset current cart weight
		sd->cart_num = 0; //Reset current cart item count
		for (i = 0; i < MAX_CART; i++) {
			if (!sd->cart.u.items_cart[i].nameid)
				continue;
			sd->cart_weight += itemdb_weight(sd->cart.u.items_cart[i].nameid) * sd->cart.u.items_cart[i].amount; //Recalculate current cart weight
			sd->cart_num++; //Recalculate current cart item count
		}
	}

	//Skill bonus max weight increases
	if (flag&CALCWT_MAXBONUS)
		sd->cart_weight_max += pc_checkskill(sd, GN_REMODELING_CART) * 5000;

	//Update the client if the new weight calculations don't match
	if (b_cart_weight_max != sd->cart_weight_max)
		clif_updatestatus(sd, SP_CARTINFO);

	return true;
}

//Calculates player data from scratch without counting SC adjustments.
//Should be invoked whenever players raise stats, learn passive skills or change equipment.
int status_calc_pc_(struct map_session_data *sd, enum e_status_calc_opt opt)
{
	static int calculating = 0; //Check for recursive call preemption [Skotlex]
	struct status_data *status; //Pointer to the player's base status
	const struct status_change *sc = &sd->sc;
	struct s_skill b_skill[MAX_SKILL]; //Previous skill tree
	int i, lv, refinedef = 0;
	uint8 passive_matk_rate = 0;
	short index = -1;

	if (++calculating > 10) //Too many recursive calls!
		return -1;

	//Remember player-specific values that are currently being shown to the client (for refresh purposes)
	memcpy(b_skill, &sd->status.skill, sizeof(b_skill));

	pc_calc_skilltree(sd); //SkillTree calculation

	if (opt&SCO_FIRST) {
		//Load Hp/SP from char-received data
		sd->battle_status.hp = sd->status.hp;
		sd->battle_status.sp = sd->status.sp;
		sd->regen.sregen = &sd->sregen;
		sd->regen.ssregen = &sd->ssregen;
	}

	status = &sd->base_status;
	//These are not zeroed [zzo]
	sd->hprate = 100;
	sd->sprate = 100;
	sd->castrate = 100;
	sd->delayrate = 100;
	sd->dsprate = 100;
	sd->hprecov_rate = 100;
	sd->sprecov_rate = 100;
	sd->matk_rate = 100;
	sd->critical_rate = sd->hit_rate = sd->flee_rate = sd->flee2_rate = 100;
	sd->def_rate = sd->def2_rate = sd->mdef_rate = sd->mdef2_rate = 100;
	sd->regen.state.block = 0;
	sd->regen.state.walk = 0;
	sd->add_max_weight = 0;

	//Zeroed arrays, order follows the order in pc.h
	//Add new arrays to the end of zeroed area in pc.h (see comments) and size here [zzo]
	memset(sd->param_bonus, 0, sizeof(sd->param_bonus)
		+ sizeof(sd->param_equip)
		+ sizeof(sd->subele)
		+ sizeof(sd->subdefele)
		+ sizeof(sd->subrace)
		+ sizeof(sd->subrace2)
		+ sizeof(sd->subclass)
		+ sizeof(sd->subsize)
		+ sizeof(sd->reseff)
		+ sizeof(sd->weapon_coma_ele)
		+ sizeof(sd->weapon_coma_race)
		+ sizeof(sd->weapon_coma_class)
		+ sizeof(sd->arrow_adddefele)
		+ sizeof(sd->arrow_addrace)
		+ sizeof(sd->arrow_addclass)
		+ sizeof(sd->arrow_addsize)
		+ sizeof(sd->shield_adddefele)
		+ sizeof(sd->shield_addrace)
		+ sizeof(sd->shield_addclass)
		+ sizeof(sd->shield_addsize)
		+ sizeof(sd->magic_adddefele)
		+ sizeof(sd->magic_addrace)
		+ sizeof(sd->magic_addclass)
		+ sizeof(sd->magic_addsize)
		+ sizeof(sd->magic_atkele)
		+ sizeof(sd->critaddrace)
		+ sizeof(sd->expaddrace)
		+ sizeof(sd->expaddclass)
		+ sizeof(sd->ignore_mdef_by_race)
		+ sizeof(sd->ignore_mdef_by_class)
		+ sizeof(sd->ignore_def_by_race)
		+ sizeof(sd->ignore_def_by_class)
		+ sizeof(sd->sp_gain_race)
		+ sizeof(sd->dropaddrace)
		+ sizeof(sd->dropaddclass)
		+ sizeof(sd->magic_addrace2)
		);

	memset(&sd->right_weapon.overrefine, 0, sizeof(sd->right_weapon) - sizeof(sd->right_weapon.atkmods));
	memset(&sd->left_weapon.overrefine, 0, sizeof(sd->left_weapon) - sizeof(sd->left_weapon.atkmods));

	if (sd->special_state.intravision && !sc->data[SC_INTRAVISION]) //Clear intravision as long as nothing else is using it
		clif_status_load(&sd->bl, SI_CLAIRVOYANCE, 0);

	if (sd->special_state.no_walkdelay)
		clif_status_load(&sd->bl, SI_ENDURE, 0);

	memset(&sd->special_state, 0, sizeof(sd->special_state));

	if (pc_isvip(sd))
		sd->special_state.no_gemstone = battle_config.vip_gemstone; //Magic Stone requirement avoidance for VIP

	if (!sd->state.permanent_speed) {
		memset(&status->max_hp, 0, sizeof(struct status_data) - (sizeof(status->hp) + sizeof(status->sp)));
		status->speed = DEFAULT_WALK_SPEED;
	} else {
		int pSpeed = status->speed;

		memset(&status->max_hp, 0, sizeof(struct status_data) - (sizeof(status->hp) + sizeof(status->sp)));
		status->speed = pSpeed;
	}

	//FIXME: Most of these stuff should be calculated once, but how do I fix the memset above to do that? [Skotlex]
	//Give them all modes except these (useful for clones)
	status->mode = (enum e_mode)(MD_MASK&~(MD_NORANDOM_WALK|MD_NOCAST_SKILL|MD_ANGRY|MD_TARGETWEAK|MD_RANDOMTARGET|MD_IGNOREMELEE|MD_IGNOREMAGIC|MD_IGNORERANGED|MD_IGNOREMISC|MD_DETECTOR|MD_STATUS_IMMUNE|MD_SKILL_IMMUNE));

	status->size = (sd->class_&JOBL_BABY || (sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER) ? SZ_SMALL : SZ_MEDIUM;
	if (battle_config.character_size && (pc_isriding(sd) || pc_isridingdragon(sd))) { //[Lupus]
		if (sd->class_&JOBL_BABY) {
			if (battle_config.character_size&SZ_BIG)
				status->size++;
		} else if (battle_config.character_size&SZ_MEDIUM)
			status->size++;
	}
	status->aspd_rate = 1000;
	status->ele_lv = 1;
	status->race = ((sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER) ? RC_BRUTE : RC_DEMIHUMAN;
	status->class_ = CLASS_NORMAL;

	//Zero up structures
	memset(&sd->autospell, 0, sizeof(sd->autospell)
		+ sizeof(sd->autospell2)
		+ sizeof(sd->autospell3)
		+ sizeof(sd->addeff)
		+ sizeof(sd->addeff2)
		+ sizeof(sd->addeff3)
		+ sizeof(sd->skillatk)
		+ sizeof(sd->skillusesprate)
		+ sizeof(sd->skillusesp)
		+ sizeof(sd->skillheal)
		+ sizeof(sd->skillheal2)
		+ sizeof(sd->hp_loss)
		+ sizeof(sd->sp_loss)
		+ sizeof(sd->hp_regen)
		+ sizeof(sd->sp_regen)
		+ sizeof(sd->skillblown)
		+ sizeof(sd->skillcast)
		+ sizeof(sd->add_def)
		+ sizeof(sd->add_mdef)
		+ sizeof(sd->add_mdmg)
		+ sizeof(sd->add_drop)
		+ sizeof(sd->itemhealrate)
		+ sizeof(sd->subele2)
		+ sizeof(sd->skillcooldown)
		+ sizeof(sd->skillfixcast)
		+ sizeof(sd->skillvarcast)
		+ sizeof(sd->skillfixcastrate)
		+ sizeof(sd->def_set_race)
		+ sizeof(sd->mdef_set_race)
		+ sizeof(sd->norecover_state_race)
		+ sizeof(sd->hp_vanish_race)
		+ sizeof(sd->sp_vanish_race)
		+ sizeof(sd->subskill)
		+ sizeof(sd->skilldelay)
	);

	if (sd->bonus.speed_rate < 0)
		clif_status_load(&sd->bl, SI_MOVHASTE_INFINITY, 0);

	memset(&sd->bonus, 0, sizeof(sd->bonus));

	//Autobonus
	pc_delautobonus(sd, sd->autobonus, ARRAYLENGTH(sd->autobonus), true);
	pc_delautobonus(sd, sd->autobonus2, ARRAYLENGTH(sd->autobonus2), true);
	pc_delautobonus(sd, sd->autobonus3, ARRAYLENGTH(sd->autobonus3), true);

	pc_itemgrouphealrate_clear(sd);

	running_npc_stat_calc_event = true;
	npc_script_event(sd, NPCE_STATCALC);
	running_npc_stat_calc_event = false;

	//Parse equipment
	for (i = 0; i < EQI_MAX; i++) {
		//We pass INDEX to current_equip_item_index - for EQUIP_SCRIPT (new cards solution) [Lupus]
		current_equip_item_index = index = sd->equip_index[i];
		current_equip_pos = 0;
		if (index < 0)
			continue;
		if (i == EQI_AMMO)
			continue;
		if (pc_is_same_equip_index((enum equip_index)i, sd->equip_index, index))
			continue;
		if (!sd->inventory_data[index])
			continue;
		status->def += sd->inventory_data[index]->def;
		//Items may be equipped, their effects however are nullified
		if ((opt&(SCO_FIRST|SCO_FORCE)) && sd->inventory_data[index]->equip_script && (pc_has_permission(sd, PC_PERM_USE_ALL_EQUIPMENT) ||
			!itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m))) { //Execute equip-script
			run_script(sd->inventory_data[index]->equip_script, 0, sd->bl.id, 0);
			if (!calculating)
				return 1; //Abort, run_script retriggered status_calc_pc [Skotlex]
		}
		//Sanitize the refine level in case someone decreased the value inbetween
		if (sd->inventory.u.items_inventory[index].refine > MAX_REFINE)
			sd->inventory.u.items_inventory[index].refine = MAX_REFINE;
		if (sd->inventory_data[index]->type == IT_WEAPON) {
			int r = sd->inventory.u.items_inventory[index].refine, wlv = sd->inventory_data[index]->wlv;
			struct weapon_data *wd;
			struct weapon_atk *watk;

			if (wlv >= REFINE_TYPE_MAX)
				wlv = REFINE_TYPE_MAX - 1;
			if (i == EQI_HAND_L && sd->inventory.u.items_inventory[index].equip == EQP_HAND_L) {
				wd = &sd->left_weapon; //Left-hand weapon
				watk = &status->lhw;
			} else {
				wd = &sd->right_weapon;
				watk = &status->rhw;
			}
			if (sd->inventory_data[index]->script && (pc_has_permission(sd, PC_PERM_USE_ALL_EQUIPMENT) ||
				!itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m))) {
				if (wd == &sd->left_weapon) {
					sd->state.lr_flag = 1;
					run_script(sd->inventory_data[index]->script, 0, sd->bl.id, 0);
					sd->state.lr_flag = 0;
				} else
					run_script(sd->inventory_data[index]->script, 0, sd->bl.id, 0);
				if (!calculating)
					return 1;
			}
			watk->atk = sd->inventory_data[index]->atk;
			if (sd->bonus.weapon_atk_rate)
				watk->atk += watk->atk * sd->bonus.weapon_atk_rate / 100;
			if (r)
				watk->atk2 += refine_info[wlv].bonus[r - 1] / 100;
#ifdef RENEWAL
			watk->matk = sd->inventory_data[index]->matk;
			if (sd->bonus.weapon_matk_rate)
				watk->matk += watk->matk * sd->bonus.weapon_matk_rate / 100;
			switch (sd->status.weapon) {
				case W_BOW:	case W_REVOLVER:
				case W_RIFLE:	case W_GATLING:
				case W_SHOTGUN:	case W_GRENADE:
					break;
				default:
					if (r) //Renewal magic attack refine bonus
						watk->matk += refine_info[wlv].bonus[r - 1] / 100;
					break;
			}
			watk->wlv = wlv;
#endif
			if (r) //Overrefine bonus
				wd->overrefine = refine_info[wlv].randombonus_max[r - 1] / 100;
			watk->range += sd->inventory_data[index]->range;
			if (sd->inventory.u.items_inventory[index].card[0] == CARD0_FORGE) { //Forged weapon
				wd->star += (sd->inventory.u.items_inventory[index].card[1]>>8);
				if (wd->star >= 15)
					wd->star = 40; //3 Star Crumbs now give +40 dmg
				if (pc_famerank(MakeDWord(sd->inventory.u.items_inventory[index].card[2], sd->inventory.u.items_inventory[index].card[3]), MAPID_BLACKSMITH))
					wd->star += 10;
				if (!watk->ele) //Do not overwrite element from previous bonuses
					watk->ele = (sd->inventory.u.items_inventory[index].card[1]&0x0f);
			}
		} else if (sd->inventory_data[index]->type == IT_ARMOR) {
			int r;

			if ((r = sd->inventory.u.items_inventory[index].refine))
				refinedef += refine_info[REFINE_TYPE_ARMOR].bonus[r - 1];
			if (sd->inventory_data[index]->script && (pc_has_permission(sd, PC_PERM_USE_ALL_EQUIPMENT) ||
				!itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m))) {
				if (i == EQI_HAND_L)
					sd->state.lr_flag = 3;  //Shield
				run_script(sd->inventory_data[index]->script, 0, sd->bl.id, 0);
				if (i == EQI_HAND_L)
					sd->state.lr_flag = 0;
				if (!calculating)
					return 1;
			}
		} else if (sd->inventory_data[index]->type == IT_SHADOWGEAR) { //Shadow System
			int r = sd->inventory.u.items_inventory[index].refine;

			if (sd->inventory_data[index]->equip == EQP_SHADOW_WEAPON) {
				if (r) { //ATK, MATK + 1 for each refine
#ifndef RENEWAL
					status->batk += r;
#else
					sd->bonus.eatk += r;
					sd->bonus.ematk += r;
#endif
				}
			} else if (r) //Max HP + 10 for each refine
				sd->bonus.hp += 10 * r;
			if (sd->inventory_data[index]->script && (pc_has_permission(sd, PC_PERM_USE_ALL_EQUIPMENT) ||
				!itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m))) {
				run_script(sd->inventory_data[index]->script, 0, sd->bl.id, 0);
				if (!calculating)
					return 1;
			}
		}
	}

	//Store equipment script bonuses
	memcpy(sd->param_equip, sd->param_bonus, sizeof(sd->param_equip));
	memset(sd->param_bonus, 0, sizeof(sd->param_bonus));

	status->def += (refinedef + 50) / 100;

	//Parse Cards
	for (i = 0; i < EQI_MAX; i++) {
		//We pass INDEX to current_equip_item_index - for EQUIP_SCRIPT (new cards solution) [Lupus]
		current_equip_item_index = index = sd->equip_index[i];
		current_equip_pos = 0;
		if (index < 0)
			continue;
		if (i == EQI_AMMO)
			continue;
		if (pc_is_same_equip_index((enum equip_index)i, sd->equip_index, index))
			continue;
		if (sd->inventory_data[index]) {
			int j;
			struct item_data *data;

			//Card script execution
			if (itemdb_isspecial(sd->inventory.u.items_inventory[index].card[0]))
				continue;
			for (j = 0; j < MAX_SLOTS; j++) { //Uses MAX_SLOTS to support Soul Bound system [Inkfish]
				int c = sd->inventory.u.items_inventory[index].card[j];

				current_equip_card_id = c;
				if (!c)
					continue;
				data = itemdb_exists(c);
				if (!data)
					continue;
				if ((opt&(SCO_FIRST|SCO_FORCE)) && data->equip_script && (pc_has_permission(sd, PC_PERM_USE_ALL_EQUIPMENT) ||
					!itemdb_isNoEquip(data, sd->bl.m))) {
					run_script(data->equip_script, 0, sd->bl.id, 0);
					if(!calculating)
						return 1;
				}
				if (!data->script)
					continue;
				if (!pc_has_permission(sd, PC_PERM_USE_ALL_EQUIPMENT) && itemdb_isNoEquip(data, sd->bl.m)) //Card restriction checks
					continue;
				if (i == EQI_HAND_L && sd->inventory.u.items_inventory[index].equip == EQP_HAND_L) { //Left hand status
					sd->state.lr_flag = 1;
					run_script(data->script, 0, sd->bl.id, 0);
					sd->state.lr_flag = 0;
				} else
					run_script(data->script, 0, sd->bl.id, 0);
				if (!calculating)
					return 1;
			}
		}
	}

	current_equip_card_id = 0; //Clear stored card ID [Secret]

	//Parse random options
	for (i = 0; i < EQI_MAX; i++) {
		current_equip_item_index = index = sd->equip_index[i];
		current_equip_pos = 0;
		current_equip_opt_index = -1;
		if (index < 0)
			continue;
		if (i == EQI_AMMO)
			continue;
		if (pc_is_same_equip_index((enum equip_index)i, sd->equip_index, index))
			continue;
		if (sd->inventory_data[index]) {
			int j;
			struct s_random_opt_data *data;

			for (j = 0; j < MAX_ITEM_RDM_OPT; j++) {
				short opt_id = sd->inventory.u.items_inventory[index].option[j].id;

				if (!opt_id)
					continue;
				current_equip_opt_index = j;
				data = itemdb_randomopt_exists(opt_id);
				if (!data || !data->script)
					continue;
				if (!pc_has_permission(sd, PC_PERM_USE_ALL_EQUIPMENT) && itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m))
					continue;
				if (i == EQI_HAND_L && sd->inventory.u.items_inventory[index].equip == EQP_HAND_L) {
					sd->state.lr_flag = 1;
					run_script(data->script, 0, sd->bl.id, 0);
					sd->state.lr_flag = 0;
				} else
					run_script(data->script, 0, sd->bl.id, 0);
				if (!calculating)
					return 1;
			}
		}
		current_equip_opt_index = -1;
	}

	if ((index = sd->equip_index[EQI_AMMO]) >= 0) {
		if (sd->inventory_data[index]) { //Arrows
			struct item_data *id = sd->inventory_data[index];

			sd->bonus.arrow_atk += (id->look != AMMO_CANNONBALL ? id->atk : 0);
			sd->state.lr_flag = 2;
			if (id->look != AMMO_THROWABLE_ITEM)
				run_script(id->script, 0, sd->bl.id, 0);
			sd->state.lr_flag = 0;
			if (!calculating)
				return 1;
		}
	}

	//We've got combos to process and check
	if (sd->combos.count) {
		for (i = 0; i < sd->combos.count; i++) {
			uint8 j = 0;
			bool no_run = false;
			struct item_combo *combo = NULL;

			current_equip_item_index = -1;
			current_equip_pos = sd->combos.pos[i];
			if (!sd->combos.bonus[i] || !(combo = itemdb_combo_exists(sd->combos.id[i])))
				continue;
			//Check combo items
			while (j < combo->count) {
				struct item_data *id = itemdb_exists(combo->nameid[j]);

				//Don't run the script if at least one of combo's pair has restriction
				if (id && !pc_has_permission(sd, PC_PERM_USE_ALL_EQUIPMENT) && itemdb_isNoEquip(id, sd->bl.m)) {
					no_run = true;
					break;
				}
				j++;
			}
			if (no_run)
				continue;
			run_script(sd->combos.bonus[i], 0, sd->bl.id, 0);
			if (!calculating)
				return 1;
		}
	}

	pc_bonus_script(sd);

	if (sd->pd) { //Pet Bonus
		struct pet_data *pd = sd->pd;

		if (pd) {
			if (pd->petDB && pd->petDB->pet_friendly_script && pd->pet.intimate >= battle_config.pet_bonus_min_friendly) {
				current_equip_item_index = -1;
				current_equip_pos = EQP_PET;
				run_script(pd->petDB->pet_friendly_script, 0, sd->bl.id, 0);
				if (!calculating)
					return 1;
			}
			if (pd->pet.intimate >= PETINTIMATE_AWKWARD && (!battle_config.pet_equip_required || pd->pet.equip > 0) && pd->state.skillbonus == 1 && pd->bonus) {
				if (pd->bonus->val2)
					pc_bonus2(sd, pd->bonus->type, pd->bonus->val1, pd->bonus->val2);
				else
					pc_bonus(sd, pd->bonus->type, pd->bonus->val1);
			}
		}
	}

	if ((i = pc_checkskill(sd, SU_SOULATTACK)) > 0)
		status->rhw.range += skill_get_range2(&sd->bl, SU_SOULATTACK, i, true);

	//Param_bonus now holds card bonuses
	status->rhw.range = max(status->rhw.range, 1);
	status->lhw.range = max(status->lhw.range, 1);
	status->rhw.range = max(status->rhw.range, status->lhw.range);

	sd->bonus.double_rate += sd->bonus.double_add_rate;
	sd->bonus.perfect_hit += sd->bonus.perfect_hit_add;
	sd->bonus.splash_range += sd->bonus.splash_add_range;

	//Damage modifiers from weapon type
	sd->right_weapon.atkmods[0] = atkmods[0][sd->weapontype1];
	sd->right_weapon.atkmods[1] = atkmods[1][sd->weapontype1];
	sd->right_weapon.atkmods[2] = atkmods[2][sd->weapontype1];
	sd->left_weapon.atkmods[0] = atkmods[0][sd->weapontype2];
	sd->left_weapon.atkmods[1] = atkmods[1][sd->weapontype2];
	sd->left_weapon.atkmods[2] = atkmods[2][sd->weapontype2];

	//When mounting with spear, damage modifier to mid-class becomes same as versus large size
	if ((pc_isriding(sd) || pc_isridingdragon(sd)) && (sd->status.weapon == W_1HSPEAR || sd->status.weapon == W_2HSPEAR)) {
		sd->right_weapon.atkmods[1] = sd->right_weapon.atkmods[2];
		sd->left_weapon.atkmods[1] = sd->left_weapon.atkmods[2];
	}

	//----- STATS CALCULATION -----
	//Job bonuses
	index = pc_class2idx(sd->status.class_);
	for (i = 0; i < (int)sd->status.job_level && i < MAX_LEVEL; i++) {
		if (!job_info[index].job_bonus[i])
			continue;
		switch (job_info[index].job_bonus[i]) {
			case 1: status->str++; break;
			case 2: status->agi++; break;
			case 3: status->vit++; break;
			case 4: status->int_++; break;
			case 5: status->dex++; break;
			case 6: status->luk++; break;
		}
	}

	//If a Super Novice has never died and is at least job lv 70, he gets all stats +10
	if ((sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE && (sd->status.job_level >= 70 || sd->class_&JOBL_THIRD) && !sd->die_counter) {
		status->str += 10;
		status->agi += 10;
		status->vit += 10;
		status->int_+= 10;
		status->dex += 10;
		status->luk += 10;
	}

	//Absolute modifiers from passive skills
	if (pc_checkskill(sd, BS_HILTBINDING) > 0)
		status->str++;
	if ((lv = pc_checkskill(sd, SA_DRAGONOLOGY)) > 0)
		status->int_ += (lv + 1) / 2; //+1 INT per 2 level
	if ((lv = pc_checkskill(sd, AC_OWL)) > 0)
		status->dex += lv;
	if ((lv = pc_checkskill(sd, RA_RESEARCHTRAP)) > 0)
		status->int_ += lv;
	if (pc_checkskill(sd, SU_POWEROFLAND) > 0)
		status->int_ += 20;

	//Bonuses from cards and equipment as well as base stat, remember to avoid overflows
	i = status->str + sd->status.str + sd->param_bonus[0] + sd->param_equip[0];
	status->str = cap_value(i, 0, USHRT_MAX);
	i = status->agi + sd->status.agi + sd->param_bonus[1] + sd->param_equip[1];
	status->agi = cap_value(i, 0, USHRT_MAX);
	i = status->vit + sd->status.vit + sd->param_bonus[2] + sd->param_equip[2];
	status->vit = cap_value(i, 0, USHRT_MAX);
	i = status->int_+ sd->status.int_+ sd->param_bonus[3] + sd->param_equip[3];
	status->int_ = cap_value(i, 0, USHRT_MAX);
	i = status->dex + sd->status.dex + sd->param_bonus[4] + sd->param_equip[4];
	status->dex = cap_value(i, 0, USHRT_MAX);
	i = status->luk + sd->status.luk + sd->param_bonus[5] + sd->param_equip[5];
	status->luk = cap_value(i, 0, USHRT_MAX);

	if (sd->special_state.no_walkdelay) {
		if (sc->data[SC_ENDURE]) {
			if (sc->data[SC_ENDURE]->val4)
				sc->data[SC_ENDURE]->val4 = 0;
			status_change_end(&sd->bl, SC_ENDURE, INVALID_TIMER);
		}
		clif_status_load(&sd->bl, SI_ENDURE, 1);
		status->mdef++;
	}

	//------ ATTACK CALCULATION ------
	//Base batk value is set in status_calc_misc
#ifndef RENEWAL
	//Absolute modifiers from passive skills
	if (pc_checkskill(sd, BS_HILTBINDING) > 0)
		status->batk += 4;
#else
	status->watk = status_weapon_atk(&status->rhw);
	status->watk2 = status_weapon_atk(&status->lhw);
	status->eatk = sd->bonus.eatk;
#endif

	//----- MAX HP CALCULATION -----
	status->max_hp = status_calc_maxhpsp_pc(sd, true);
	if (battle_config.hp_rate != 100)
		status->max_hp = (unsigned int)(battle_config.hp_rate * (status->max_hp / 100.));
	status->max_hp = cap_value(status->max_hp, 1, (unsigned int)battle_config.max_hp);
	sd->status.max_hp = status->max_hp;

	//----- MAX SP CALCULATION -----
	status->max_sp = status_calc_maxhpsp_pc(sd, false);
	if (battle_config.sp_rate != 100)
		status->max_sp = (unsigned int)(battle_config.sp_rate * (status->max_sp / 100.));
	status->max_sp = cap_value(status->max_sp, 1, (unsigned int)battle_config.max_sp);
	sd->status.max_sp = status->max_sp;

	//----- RESPAWN HP/SP -----
	//Calc respawn hp and store it on base_status
	if (sd->special_state.restart_full_recover) {
		status->hp = status->max_hp;
		status->sp = status->max_sp;
	} else {
		if ((sd->class_&MAPID_BASEMASK) == MAPID_NOVICE && !(sd->class_&JOBL_2) && battle_config.restart_hp_rate < 50)
			status->hp = status->max_hp>>1;
		else
			status->hp = (int64)status->max_hp * battle_config.restart_hp_rate / 100;
		if (!status->hp)
			status->hp = 1;
		status->sp = (int64)status->max_sp * battle_config.restart_sp_rate / 100;
		if (!status->sp) //The minimum for the respawn setting is SP: 1
			status->sp = 1;
	}

	//----- MISC CALCULATION -----
	status_calc_misc(&sd->bl, status, sd->status.base_level);

	//Equipment modifiers for misc settings
	sd->matk_rate = max(sd->matk_rate, 0);
	if (pc_checkskill(sd, SU_POWEROFLAND) > 0 && pc_checkskill_summoner(sd, TYPE_PLANT) >= 20)
		passive_matk_rate += 20;
	if ((sd->matk_rate + passive_matk_rate) != 100) {
		status->matk_max = status->matk_max * (sd->matk_rate + passive_matk_rate) / 100;
		status->matk_min = status->matk_min * (sd->matk_rate + passive_matk_rate) / 100;
	}

	sd->hit_rate = max(sd->hit_rate, 0);
	if (sd->hit_rate != 100)
		status->hit = status->hit * sd->hit_rate / 100;

	sd->flee_rate = max(sd->flee_rate, 0);
	if (sd->flee_rate != 100)
		status->flee = status->flee * sd->flee_rate / 100;

	sd->def2_rate = max(sd->def2_rate, 0);
	if (sd->def2_rate != 100)
		status->def2 = status->def2 * sd->def2_rate / 100;

	sd->mdef2_rate = max(sd->mdef2_rate, 0);
	if (sd->mdef2_rate != 100)
		status->mdef2 = status->mdef2 * sd->mdef2_rate / 100;

	sd->critical_rate = max(sd->critical_rate, 0);
	if (sd->critical_rate != 100)
		status->cri = cap_value(status->cri * sd->critical_rate / 100, SHRT_MIN, SHRT_MAX);
	if (pc_checkskill(sd, SU_POWEROFLIFE) > 0)
		status->cri += 200;

	sd->flee2_rate = max(sd->flee2_rate, 0);
	if (sd->flee2_rate != 100)
		status->flee2 = status->flee2 * sd->flee2_rate / 100;

	//----- HIT CALCULATION -----
	//Absolute modifiers from passive skills
#ifndef RENEWAL
	if ((lv = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)
		status->hit += lv * 2;
#endif
	if ((lv = pc_checkskill(sd, AC_VULTURE)) > 0) {
#ifndef RENEWAL
		status->hit += lv;
#endif
		if (sd->status.weapon == W_BOW)
			status->rhw.range += lv;
	}
	if (sd->status.weapon >= W_REVOLVER && sd->status.weapon <= W_GRENADE) {
		if ((lv = pc_checkskill(sd, GS_SINGLEACTION)) > 0)
			status->hit += lv * 2;
		if ((lv = pc_checkskill(sd, GS_SNAKEEYE)) > 0) {
			status->hit += lv;
			status->rhw.range += lv;
		}
	}
	if ((lv = pc_checkskill(sd, NC_TRAININGAXE)) > 0) {
		if (sd->status.weapon == W_1HAXE || sd->status.weapon == W_2HAXE)
			status->hit += lv * 3;
		if (sd->status.weapon == W_MACE || sd->status.weapon == W_2HMACE)
			status->hit += lv * 2;
	}
	if (pc_checkskill(sd, SU_POWEROFLIFE) > 0)
		status->hit += 20;

	//----- FLEE CALCULATION -----
	//Absolute modifiers from passive skills
	if ((lv = pc_checkskill(sd, TF_MISS)) > 0)
		status->flee += lv * (sd->class_&JOBL_2 && (sd->class_&MAPID_BASEMASK) == MAPID_THIEF ? 4 : 3);
	if ((lv = pc_checkskill(sd, MO_DODGE)) > 0)
		status->flee += (lv * 3)>>1;
	if (pc_checkskill(sd, SU_POWEROFLIFE) > 0)
		status->flee += 20;

	//----- EQUIPMENT-DEF CALCULATION -----
	//Apply relative modifiers from equipment
	if (sd->def_rate < 0)
		sd->def_rate = 0;
	if (sd->def_rate != 100) {
		i = status->def * sd->def_rate / 100;
		status->def = (defType)cap_value(i, DEFTYPE_MIN, DEFTYPE_MAX);
	}
	if (pc_checkskill(sd, NC_MAINFRAME) > 0) //Defense bonus is granted even without the Madogear
		status->def += 20 + pc_checkskill(sd, NC_MAINFRAME) * 20;

#ifndef RENEWAL
	if (!battle_config.weapon_defense_type && status->def > battle_config.max_def) {
		status->def2 += battle_config.over_def_bonus * (status->def - battle_config.max_def);
		status->def = (defType)battle_config.max_def;
	}
#endif

	//----- EQUIPMENT-MDEF CALCULATION -----
	//Apply relative modifiers from equipment
	if (sd->mdef_rate < 0)
		sd->mdef_rate = 0;
	if (sd->mdef_rate != 100) {
		i =  status->mdef * sd->mdef_rate / 100;
		status->mdef = (defType)cap_value(i, DEFTYPE_MIN, DEFTYPE_MAX);
	}

#ifndef RENEWAL
	if (!battle_config.magic_defense_type && status->mdef > battle_config.max_def) {
		status->mdef2 += battle_config.over_def_bonus * (status->mdef - battle_config.max_def);
		status->mdef = (defType)battle_config.max_def;
	}
#endif

	//----- ASPD CALCULATION -----
	//Unlike other stats, ASPD rate modifiers from skills/SCs/items/etc are first all added together, then the final modifier is applied
	//Basic ASPD value
	i = status_base_amotion_pc(sd, status);
	status->amotion = cap_value(i, pc_maxaspd(sd), 2000);

	//Relative modifiers from passive skills
#ifndef RENEWAL_ASPD
	if ((lv = pc_checkskill(sd, SA_ADVANCEDBOOK)) > 0 && sd->status.weapon == W_BOOK)
		status->aspd_rate -= lv * 5;
	if ((lv = pc_checkskill(sd, SG_DEVIL)) > 0 && pc_is_maxjoblv(sd))
		status->aspd_rate -= lv * 30;
	if ((lv = pc_checkskill(sd, GS_SINGLEACTION)) > 0 && sd->status.weapon >= W_REVOLVER && sd->status.weapon <= W_GRENADE)
		status->aspd_rate -= ((lv + 1) / 2) * 10;
	if ((lv = pc_checkskill(sd, KN_CAVALIERMASTERY)) >= 0 && pc_isriding(sd))
		status->aspd_rate += 500 - lv * 100;
	else if ((lv = pc_checkskill(sd, RK_DRAGONTRAINING)) >= 0 && pc_isridingdragon(sd))
		status->aspd_rate += 250 - lv * 50;
#else //Needs more info
	if ((lv = pc_checkskill(sd, SG_DEVIL)) > 0 && pc_is_maxjoblv(sd))
		status->aspd_rate += 10 + lv * 10;
	if ((lv = pc_checkskill(sd, KN_CAVALIERMASTERY)) >= 0 && pc_isriding(sd))
		status->aspd_rate -= 500 - lv * 100;
	else if ((lv = pc_checkskill(sd, RK_DRAGONTRAINING)) >= 0 && pc_isridingdragon(sd))
		status->aspd_rate -= 250 - lv * 50;
#endif
	status->adelay = 2 * status->amotion;

	//----- DMOTION -----
	i = 800 - status->agi * 4;
	status->dmotion = cap_value(i, 400, 800);
	if (battle_config.pc_damage_delay_rate != 100)
		status->dmotion = status->dmotion * battle_config.pc_damage_delay_rate / 100;

	//----- MISC CALCULATIONS -----
	//Weight
	status_calc_weight(sd, CALCWT_MAXBONUS);
	status_calc_cart_weight(sd, CALCWT_MAXBONUS);

	if (pc_checkskill(sd, SM_MOVINGRECOVERY) > 0 ||  pc_ismadogear(sd))
		sd->regen.state.walk = 1;

	//Skill SP cost
	if ((lv = pc_checkskill(sd, HP_MANARECHARGE)) > 0)
		sd->dsprate -= lv * 4;

	if (sc->data[SC_SERVICE4U])
		sd->dsprate -= sc->data[SC_SERVICE4U]->val3;

	if (sc->data[SC_SPCOST_RATE])
		sd->dsprate -= sc->data[SC_SPCOST_RATE]->val1;

	//Underflow protections
	if (sd->dsprate < 0)
		sd->dsprate = 0;
	if (sd->castrate < 0)
		sd->castrate = 0;
	if (sd->delayrate < 0)
		sd->delayrate = 0;
	if (sd->hprecov_rate < 0)
		sd->hprecov_rate = 0;
	if (sd->sprecov_rate < 0)
		sd->sprecov_rate = 0;

	//Additional bonus statuses
	if ((lv = sd->charmball) > 0)
		sd->subele[sd->charmball_type] += lv * 2;
	if ((lv = pc_checkskill(sd, CR_TRUST)) > 0)
		sd->subele[ELE_HOLY] += lv * 5;
	if ((lv = pc_checkskill(sd, BS_SKINTEMPER)) > 0) {
		sd->subele[ELE_NEUTRAL] += lv;
		sd->subele[ELE_FIRE] += lv * 4;
	}
	if ((lv = pc_checkskill(sd, SA_DRAGONOLOGY)) > 0) {
#ifdef RENEWAL
		lv *= 2;
#else
		lv *= 4;
#endif
		sd->right_weapon.addrace[RC_DRAGON] += lv;
		sd->left_weapon.addrace[RC_DRAGON] += lv;
		sd->magic_addrace[RC_DRAGON] += lv;
		sd->subrace[RC_DRAGON] += lv;
	}
	/*
	if ((lv = pc_checkskill(sd, AB_EUCHARISTICA)) > 0) {
		sd->right_weapon.addrace[RC_DEMON] += lv;
		sd->right_weapon.adddefele[ELE_DARK] += lv;
		sd->left_weapon.addrace[RC_DEMON] += lv;
		sd->left_weapon.adddefele[ELE_DARK] += lv;
		sd->magic_addrace[RC_DEMON] += lv;
		sd->magic_adddefele[ELE_DARK] += lv;
		sd->subrace[RC_DEMON] += lv;
		sd->subdefele[ELE_DARK] += lv;
	}
	*/
	if (pc_checkskill(sd, SU_POWEROFLIFE) > 0 && pc_checkskill_summoner(sd, TYPE_ANIMAL) >= 20)
		sd->bonus.long_attack_atk_rate += 20;
	if (sc->count) {
		if (sc->data[SC_CONCENTRATE]) { //Update the card-bonus data
			sc->data[SC_CONCENTRATE]->val3 = sd->param_bonus[1]; //Agi
			sc->data[SC_CONCENTRATE]->val4 = sd->param_bonus[4]; //Dex
		}
		if (sc->data[SC_SIEGFRIED] && (i = sc->data[SC_SIEGFRIED]->val2)) {
			sd->subele[ELE_WATER] += i;
			sd->subele[ELE_EARTH] += i;
			sd->subele[ELE_FIRE] += i;
			sd->subele[ELE_WIND] += i;
			sd->subele[ELE_POISON] += i;
			sd->subele[ELE_HOLY] += i;
			sd->subele[ELE_DARK] += i;
			sd->subele[ELE_GHOST] += i;
			sd->subele[ELE_UNDEAD] += i;
		}
		if (sc->data[SC_PROVIDENCE] && (i = sc->data[SC_PROVIDENCE]->val2)) {
			sd->subele[ELE_HOLY] += i;
			sd->subrace[RC_DEMON] += i;
		}
		//Undead Scroll
		if (sc->data[SC_ARMOR_RESIST]) {
			sd->subele[ELE_WATER] += sc->data[SC_ARMOR_RESIST]->val1;
			sd->subele[ELE_EARTH] += sc->data[SC_ARMOR_RESIST]->val2;
			sd->subele[ELE_FIRE] += sc->data[SC_ARMOR_RESIST]->val3;
			sd->subele[ELE_WIND] += sc->data[SC_ARMOR_RESIST]->val4;
		}
		//Arch Bishop
		if (sc->data[SC_LAUDARAMUS])
			sd->bonus.crit_atk_rate += 5 * sc->data[SC_LAUDARAMUS]->val1;
		//Sorcerer's Elemental statuses
		if (sc->data[SC_FIRE_CLOAK_OPTION] && (i = sc->data[SC_FIRE_CLOAK_OPTION]->val2)) {
			sd->subele[ELE_FIRE] += i;
			sd->subele[ELE_WATER] -= i;
		}
		if (sc->data[SC_WATER_DROP_OPTION] && (i = sc->data[SC_WATER_DROP_OPTION]->val2)) {
			sd->subele[ELE_WATER] += i;
			sd->subele[ELE_WIND] -= i;
		}
		if (sc->data[SC_WIND_CURTAIN_OPTION] && (i = sc->data[SC_WIND_CURTAIN_OPTION]->val2)) {
			sd->subele[ELE_WIND] += i;
			sd->subele[ELE_EARTH] -= i;
		}
		if (sc->data[SC_STONE_SHIELD_OPTION] && (i = sc->data[SC_STONE_SHIELD_OPTION]->val2)) {
			sd->subele[ELE_EARTH] += i;
			sd->subele[ELE_FIRE] -= i;
		}
		if (sc->data[SC_ARCLOUSEDASH] && (sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER)
			sd->bonus.long_attack_atk_rate += 10;
		//Transform scroll statuses
		if (sc->data[SC_MTF_RANGEATK])
			sd->bonus.long_attack_atk_rate += sc->data[SC_MTF_RANGEATK]->val1;
		if (sc->data[SC_MTF_RANGEATK2])
			sd->bonus.long_attack_atk_rate += sc->data[SC_MTF_RANGEATK2]->val1;
		if (sc->data[SC_MTF_MLEATKED])
			sd->subele[ELE_NEUTRAL] += sc->data[SC_MTF_MLEATKED]->val1;
		if (sc->data[SC_MTF_CRIDAMAGE])
			sd->bonus.crit_atk_rate += sc->data[SC_MTF_CRIDAMAGE]->val1;
		//Geffen Magic Tour. statuses
		if (sc->data[SC_GEFFEN_MAGIC1]) {
			sd->right_weapon.addrace[RC_DEMIHUMAN] += sc->data[SC_GEFFEN_MAGIC1]->val1;
			sd->left_weapon.addrace[RC_DEMIHUMAN] += sc->data[SC_GEFFEN_MAGIC1]->val1;
		}
		if (sc->data[SC_GEFFEN_MAGIC2])
			sd->magic_addrace[RC_DEMIHUMAN] += sc->data[SC_GEFFEN_MAGIC2]->val1;
		if (sc->data[SC_GEFFEN_MAGIC3])
			sd->subrace[RC_DEMIHUMAN] += sc->data[SC_GEFFEN_MAGIC3]->val1;
		if (sc->data[SC_GVG_GIANT]) {
			sd->right_weapon.addrace[RC_DEMIHUMAN] += sc->data[SC_GVG_GIANT]->val3;
			sd->left_weapon.addrace[RC_DEMIHUMAN] += sc->data[SC_GVG_GIANT]->val3;
			sd->magic_addrace[RC_DEMIHUMAN] += sc->data[SC_GVG_GIANT]->val3;
		}
		//Training Edition WOE statuses
		if (sc->data[SC_GVG_GOLEM])
			sd->subrace[RC_DEMIHUMAN] += sc->data[SC_GVG_GOLEM]->val3;
		if (sc->data[SC_GVG_STUN])
			sd->reseff[SC_STUN] = 10000;
		if (sc->data[SC_GVG_STONE])
			sd->reseff[SC_STONE] = 10000;
		if (sc->data[SC_GVG_FREEZ])
			sd->reseff[SC_FREEZE] = 10000;
		if (sc->data[SC_GVG_SLEEP])
			sd->reseff[SC_SLEEP] = 10000;
		if (sc->data[SC_GVG_CURSE])
			sd->reseff[SC_CURSE] = 10000;
		if (sc->data[SC_GVG_SILENCE])
			sd->reseff[SC_SILENCE] = 10000;
		if (sc->data[SC_GVG_BLIND])
			sd->reseff[SC_BLIND] = 10000;
	}
	status_cpy(&sd->battle_status, status);

	//----- CLIENT-SIDE REFRESH -----
	if (!sd->bl.prev) { //Will update on LoadEndAck
		calculating = 0;
		return 0;
	}
	if (memcmp(b_skill, sd->status.skill, sizeof(sd->status.skill)))
		clif_skillinfoblock(sd);
	//If the skill is learned, the status is infinite
	if (pc_checkskill(sd, SU_SPRITEMABLE) > 0 && !sc->data[SC_SPRITEMABLE])
		sc_start(&sd->bl, &sd->bl, SC_SPRITEMABLE, 100, 1, -1);
	if (pc_checkskill(sd, SU_SOULATTACK) > 0 && !sc->data[SC_SOULATTACK])
		sc_start(&sd->bl, &sd->bl, SC_SOULATTACK, 100, 1, -1);

	calculating = 0;

	return 0;
}

int status_calc_mercenary_(struct mercenary_data *md, enum e_status_calc_opt opt)
{
	struct status_data *status = &md->base_status;
	struct s_mercenary *merc = &md->mercenary;

	if( opt&SCO_FIRST ) {
		memcpy(status, &md->db->status, sizeof(struct status_data));
		status->class_ = CLASS_NORMAL;
		status->mode = (enum e_mode)(MD_CANMOVE|MD_CANATTACK);
		status->hp = status->max_hp;
		status->sp = status->max_sp;
		md->battle_status.hp = merc->hp;
		md->battle_status.sp = merc->sp;
		if( md->master )
			status->speed = status_get_speed(&md->master->bl);
	}

	status_calc_misc(&md->bl, status, md->db->lv);
	status_cpy(&md->battle_status, status);

	return 0;
}

int status_calc_homunculus_(struct homun_data *hd, enum e_status_calc_opt opt)
{
	struct status_data *status = &hd->base_status;
	struct s_homunculus *hom = &hd->homunculus;
	uint16 skill_lv;
	int amotion;

	status->str = hom->str / 10;
	status->agi = hom->agi / 10;
	status->vit = hom->vit / 10;
	status->dex = hom->dex / 10;
	status->int_ = hom->int_ / 10;
	status->luk = hom->luk / 10;

	APPLY_HOMUN_LEVEL_STATWEIGHT();

	if( opt&SCO_FIRST ) { //[orn]
		const struct s_homunculus_db *db = hd->homunculusDB;

		status->def_ele = db->element;
		status->ele_lv = 1;
		status->race = db->race;
		status->class_ = CLASS_NORMAL;
		status->size = (hom->class_ == db->evo_class) ? db->evo_size : db->base_size;
		status->rhw.range = 1 + status->size;
		status->mode = (enum e_mode)(MD_CANMOVE|MD_CANATTACK);
		status->speed = DEFAULT_WALK_SPEED;
		if( (battle_config.hom_setting&HOMSET_COPY_SPEED) && hd->master )
			status->speed = status_get_speed(&hd->master->bl);
		status->hp = 1;
		status->sp = 1;
	}

	status->aspd_rate = 1000;
#ifdef RENEWAL
	status->def = status->mdef = 0;
	amotion = hd->homunculusDB->baseASPD;
	amotion = amotion - amotion * (status->dex + hom->dex_value) / 1000 - (status->agi + hom->agi_value) * amotion / 250;
#else
	skill_lv = hom->level / 10 + status->vit / 5;
	status->def = cap_value(skill_lv, 0, 99);
	skill_lv = hom->level / 10 + status->int_ / 5;
	status->mdef = cap_value(skill_lv, 0, 99);
	amotion = (1000 - 4 * status->agi - status->dex) * hd->homunculusDB->baseASPD / 1000;
#endif
	status->amotion = cap_value(amotion, battle_config.max_aspd, 2000);
	status->adelay = status->amotion; //It seems adelay = amotion for Homunculus
	status->max_hp = hom->max_hp;
	status->max_sp = hom->max_sp;
	hom_calc_skilltree(hd, 0);

	if( (skill_lv = hom_checkskill(hd, HAMI_SKIN)) > 0 )
		status->def +=	skill_lv * 4;

	if( (skill_lv = hom_checkskill(hd, HVAN_INSTRUCT)) > 0 ) {
		status->int_ += 1 + skill_lv / 2 + skill_lv / 4 + skill_lv / 5;
		status->str  += 1 + skill_lv / 3 + skill_lv / 3 + skill_lv / 4;
		status->int_ = min(status->int_, battle_config.max_homunculus_parameter);
		status->str = min(status->str, battle_config.max_homunculus_parameter);
	}

	if( (skill_lv = hom_checkskill(hd, HAMI_SKIN)) > 0 ) {
		status->max_hp += skill_lv * 2 * status->max_hp / 100;
		status->max_hp = umin(status->max_hp, (unsigned int)battle_config.max_homunculus_hp);
	}

	if( (skill_lv = hom_checkskill(hd, HLIF_BRAIN)) > 0 ) {
		status->max_sp += (1 + skill_lv / 2 - skill_lv / 4 + skill_lv / 5) * status->max_sp / 100;
		status->max_sp = umin(status->max_sp, (unsigned int)battle_config.max_homunculus_sp);
	}

	if( opt&SCO_FIRST ) {
		hd->battle_status.hp = hom->hp;
		hd->battle_status.sp = hom->sp;
		if( hom->class_ == 6052 ) { //Eleanor
			clif_status_change(&hd->master->bl, SI_STYLE_CHANGE, 1, -1, 0, 0, 0);
			sc_start(&hd->bl, &hd->bl, SC_STYLE_CHANGE, 100, MH_MD_FIGHTING, -1);
		}
	}

#ifndef RENEWAL
	status->rhw.atk = status->dex;
	status->rhw.atk2 = status->str + hom->level;
#endif
	status_calc_misc(&hd->bl, status, hom->level);
	status_cpy(&hd->battle_status, status);

	return 1;
}

int status_calc_elemental_(struct elemental_data *ed, enum e_status_calc_opt opt) {
	struct status_data *status = &ed->base_status;
	struct s_elemental *ele = &ed->elemental;
	struct map_session_data *sd = ed->master;

	if( !sd )
		return 0;

	if( opt&SCO_FIRST ) {
		memcpy(status, &ed->db->status, sizeof(struct status_data));
		status->max_hp = ele->max_hp;
		status->max_sp = ele->max_sp;
		status->hp = ele->hp;
		status->sp = ele->sp;
#ifdef RENEWAL
		status->rhw.atk = ele->atk;
		status->rhw.matk = ele->matk;
#else
		status->rhw.atk = ele->atk * 80 / 100;
		status->rhw.atk2 = ele->atk * 120 / 100;
		if( sd ) //For pre-re elemental matk calculation [exneval]
			status->int_ = status_get_int(&sd->bl);
#endif
		status->def = ele->def;
		status->mdef = ele->mdef;
		status->flee = ele->flee;
		status->hit = ele->hit;
		status->size = ele->scale - 1;
		status->amotion = ele->amotion;
		if( sd )
			status->speed = status_get_speed(&sd->bl);
		else
			status->speed = DEFAULT_WALK_SPEED;
		status_calc_misc(&ed->bl, status, 0);
		memcpy(&ed->battle_status, status, sizeof(struct status_data));
	} else {
		status_calc_misc(&ed->bl, status, 0);
		status_cpy(&ed->battle_status, status);
	}

	return 0;
}

int status_calc_npc_(struct npc_data *nd, enum e_status_calc_opt opt) {
	struct status_data *status;

	if (!nd)
		return 0;

	status = &nd->status;

	if (opt&SCO_FIRST) {
		status->hp = 1;
		status->sp = 1;
		status->max_hp = 1;
		status->max_sp = 1;
		status->def_ele = ELE_NEUTRAL;
		status->ele_lv = 1;
		status->race = RC_DEMIHUMAN;
		status->class_ = CLASS_NORMAL;
		status->size = nd->size;
		status->rhw.range = 1 + status->size;
		status->mode = (enum e_mode)(MD_CANMOVE|MD_CANATTACK);
		status->speed = nd->speed;
	}

	status->str = nd->stat_point + nd->params.str;
	status->agi = nd->stat_point + nd->params.agi;
	status->vit = nd->stat_point + nd->params.vit;
	status->int_= nd->stat_point + nd->params.int_;
	status->dex = nd->stat_point + nd->params.dex;
	status->luk = nd->stat_point + nd->params.luk;

	status_calc_misc(&nd->bl, status, nd->level);
	status_cpy(&nd->status, status);

	return 0;
}

//Calculates base regen values
void status_calc_regen(struct block_list *bl, struct status_data *status, struct regen_data *regen)
{
	struct map_session_data *sd;
	struct status_change *sc;
	int val, reg_flag;
	uint16 lv;

	nullpo_retv(bl);
	nullpo_retv(status);

	if( !(bl->type&BL_REGEN) || !regen )
		return;

	sd = BL_CAST(BL_PC, bl);
	sc = status_get_sc(bl);

	val = 1 + (status->vit / 5) + (status->max_hp / 200);
	if( sd && sd->hprecov_rate != 100 )
		val = val * sd->hprecov_rate / 100;
	reg_flag = (bl->type == BL_PC ? 0 : 1);
	regen->hp = cap_value(val, reg_flag, SHRT_MAX);

	val = 1 + (status->int_ / 6) + (status->max_sp / 100);
	if( status->int_ >= 120 )
		val += ((status->int_ - 120)>>1) + 4;
	if( sd && sd->sprecov_rate != 100 )
		val = val * sd->sprecov_rate / 100;
	regen->sp = cap_value(val, reg_flag, SHRT_MAX);

	if( sd ) {
		struct regen_data_sub *sregen;

		if( (lv = pc_checkskill(sd, HP_MEDITATIO)) > 0 ) {
			val = regen->sp * (100 + lv * 3) / 100;
			regen->sp = cap_value(val, 1, SHRT_MAX);
		}

		//Skill regeneration
		sregen = regen->sregen;
		val = 0;
		if( (lv = pc_checkskill(sd, SM_RECOVERY)) > 0 )
			val += lv * 5 + lv * status->max_hp / 500;
		sregen->hp = cap_value(val, 0, SHRT_MAX);
		val = 0;
		if( (lv = pc_checkskill(sd, MG_SRECOVERY)) > 0 )
			val += lv * 3 + lv * status->max_sp / 500;
		if( (lv = pc_checkskill(sd, NJ_NINPOU)) > 0 )
			val += lv * 3 + lv * status->max_sp / 500;
		if( (lv = pc_checkskill(sd, WM_LESSON)) > 0 )
			val += lv * 3 + lv * status->max_sp / 500;
		if( sc && sc->count && sc->data[SC_SHRIMPBLESSING] )
			val *= 150 / 100;
		sregen->sp = cap_value(val, 0, SHRT_MAX);

		//Sitting skill regeneration
		sregen = regen->ssregen;
		val = 0;
		if( (lv = pc_checkskill(sd, MO_SPIRITSRECOVERY)) > 0 )
			val += lv * 4 + lv * status->max_hp / 500;
		if( (lv = pc_checkskill(sd, TK_HPTIME)) > 0 && sd->state.rest )
			val += lv * 30 + lv * status->max_hp / 500;
		sregen->hp = cap_value(val, 0, SHRT_MAX);
		val = 0;
		if( (lv = pc_checkskill(sd, TK_SPTIME)) > 0 && sd->state.rest ) {
			val += lv * 3 + lv * status->max_sp / 500;
			if( (lv = pc_checkskill(sd, SL_KAINA)) > 0 ) //Power up Enjoyable Rest
				val += (30 + lv * 10) * val / 100;
		}
		if( (lv = pc_checkskill(sd, MO_SPIRITSRECOVERY)) > 0 )
			val += lv * 2 + lv * status->max_sp / 500;
		sregen->sp = cap_value(val, 0, SHRT_MAX);
	}

	if( bl->type == BL_HOM ) {
		struct homun_data *hd = (TBL_HOM *)bl;

		if( (lv = hom_checkskill(hd, HAMI_SKIN)) > 0 ) {
			val = regen->hp * (100 + lv * 5) / 100;
			regen->hp = cap_value(val, 1, SHRT_MAX);
		}
		if( (lv = hom_checkskill(hd, HLIF_BRAIN)) > 0 ) {
			val = regen->sp * (100 + lv * 3) / 100;
			regen->sp = cap_value(val, 1, SHRT_MAX);
		}
	} else if( bl->type == BL_MER ) {
		val = (status->max_hp * status->vit / 10000 + 1) * 6;
		regen->hp = cap_value(val, 1, SHRT_MAX);
		val = (status->max_sp * (status->int_ + 10) / 750) + 1;
		regen->sp = cap_value(val, 1, SHRT_MAX);
	} else if( bl->type == BL_ELEM ) { //Elemental summon recovers 2% of max HP/SP [malufett]
		val = status->max_hp * 2 / 100;
		regen->hp = cap_value(val, 1, SHRT_MAX);
		val = status->max_sp * 2 / 100;
		regen->sp = cap_value(val, 1, SHRT_MAX);
	}
}

//Calculates SC related regen rates
void status_calc_regen_rate(struct block_list *bl, struct regen_data *regen, struct status_change *sc)
{
	if( !(bl->type&BL_REGEN) || !regen )
		return;

	regen->flag = RGN_HP|RGN_SP;

	if( regen->sregen ) {
		if( regen->sregen->hp )
			regen->flag |= RGN_SHP;
		if( regen->sregen->sp )
			regen->flag |= RGN_SSP;
		regen->sregen->rate.hp = regen->sregen->rate.sp = 100;
	}

	if( regen->ssregen ) {
		if( regen->ssregen->hp )
			regen->flag |= RGN_SHP;
		if( regen->ssregen->sp )
			regen->flag |= RGN_SSP;
		regen->ssregen->rate.hp = regen->ssregen->rate.sp = 100;
	}

	regen->rate.hp = regen->rate.sp = 100;

	if( !sc || !sc->count )
		return;

	if( (sc->data[SC_POISON] && !sc->data[SC_SLOWPOISON]) ||
		(sc->data[SC_DPOISON] && !sc->data[SC_SLOWPOISON]) ||
		sc->data[SC_BERSERK] ||
		sc->data[SC_TRICKDEAD] ||
		sc->data[SC_BLEEDING] ||
		sc->data[SC_SATURDAYNIGHTFEVER] ||
		sc->data[SC_REBOUND] ||
		sc->data[SC_EL_PASSIVE] ||
		sc->data[SC_EL_DEFENSIVE] ||
		sc->data[SC_EL_OFFENSIVE] )
		regen->flag = RGN_NONE;

	if( sc->data[SC_DANCING] ||
#ifdef RENEWAL
		sc->data[SC_MAXIMIZEPOWER] ||
		sc->data[SC_FUSION] ||
#else
		(bl->type == BL_PC && (((TBL_PC *)bl)->class_&MAPID_UPPERMASK) == MAPID_MONK &&
		(sc->data[SC_EXPLOSIONSPIRITS] || sc->data[SC_EXTREMITYFIST]) &&
		(!sc->data[SC_SPIRIT] || sc->data[SC_SPIRIT]->val2 != SL_MONK)) ||
#endif
		sc->data[SC_VITALITYACTIVATION] || sc->data[SC_TOXIN] || sc->data[SC_OBLIVIONCURSE] )
		regen->flag &= ~RGN_SP; //No natural SP regen

	if( sc->data[SC_MAGNIFICAT] )
		regen->rate.sp += 100; //2x natural SP regen

	if( sc->data[SC_TENSIONRELAX] ) {
		if( sc->data[SC_WEIGHT50] || sc->data[SC_WEIGHT90] )
			regen->state.overweight = 0; //1x natural HP regen
		else {
			regen->rate.hp += 200; //3x natural HP regen
			if( regen->sregen )
				regen->sregen->rate.hp += 200; //3x skill HP regen (HP Recovery)
		}
	}

	if( sc->data[SC_REGENERATION] ) {
		const struct status_change_entry *sce = sc->data[SC_REGENERATION];

		if( !sce->val4 ) {
			regen->rate.hp += sce->val2 * 100;
			regen->rate.sp += sce->val3 * 100;
		} else
			regen->flag &= ~sce->val4; //Remove regen as specified by val4
	}

	if( sc->data[SC_ANGEL_PROTECT] ) {
		regen->hp += cap_value(regen->hp * 50 / 100, 1, SHRT_MAX);
		regen->sp += cap_value(regen->sp * 50 / 100, 1, SHRT_MAX);
	}

	if( sc->data[SC_BANDING] && sc->data[SC_BANDING]->val2 > 1 )
		regen->hp += cap_value(regen->hp * 50 / 100, 1, SHRT_MAX);

	if( sc->data[SC_GT_REVITALIZE] ) {
		regen->hp += cap_value(regen->hp * sc->data[SC_GT_REVITALIZE]->val3 / 100, 1, SHRT_MAX);
		regen->state.walk = 1;
	}

	if( sc->data[SC_EXTRACT_WHITE_POTION_Z] )
		regen->hp += cap_value(regen->hp * sc->data[SC_EXTRACT_WHITE_POTION_Z]->val1 / 100, 1, SHRT_MAX);

	if( sc->data[SC_VITATA_500] )
		regen->sp += cap_value(regen->sp * sc->data[SC_VITATA_500]->val1 / 100, 1, SHRT_MAX);

	if( sc->data[SC_CATNIPPOWDER] ) {
		regen->rate.hp += 100;
		regen->rate.sp += 100;
	}

	if( sc->data[SC_EL_WAIT] ) { //Regenerates every 3 secs
		regen->rate.hp += 300;
		regen->rate.sp += 300;
	}

	if( bl->type == BL_ELEM ) {
		int class_ = status_get_class(bl);

		switch( class_ ) {
			case ELEMENTALID_AGNI_S:
			case ELEMENTALID_AGNI_M:
			case ELEMENTALID_AGNI_L:
				if( sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 1 )
					regen->rate.hp += 100;
				break;
			case ELEMENTALID_AQUA_S:
			case ELEMENTALID_AQUA_M:
			case ELEMENTALID_AQUA_L:
				if( sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 1 )
					regen->rate.hp += 100;
				break;
			case ELEMENTALID_VENTUS_S:
			case ELEMENTALID_VENTUS_M:
			case ELEMENTALID_VENTUS_L:
				if( sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 1 )
					regen->rate.hp += 100;
				break;
			case ELEMENTALID_TERA_S:
			case ELEMENTALID_TERA_M:
			case ELEMENTALID_TERA_L:
				if( sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 1 )
					regen->rate.hp += 100;
				break;
		}
	}
}

void status_calc_state(struct block_list *bl, struct status_change *sc, enum scs_flag flag, bool start)
{
	if( !sc || !sc->count ) { //No sc at all, we can zero without any extra weight over our conciousness
		memset(&sc->cant, 0, sizeof (sc->cant));
		return;
	}

	if( flag&SCS_NOMOVE ) { //Can move?
		if( !(flag&SCS_NOMOVECOND) )
			sc->cant.move += (start ? 1 : (sc->cant.move ? -1 : 0));
		else if( (sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_SELF) || //Can't move while gospel is in effect
			(sc->data[SC_BASILICA] && sc->data[SC_BASILICA]->val4 == bl->id) || //Basilica caster cannot move
			(sc->data[SC_GRAVITATION] && sc->data[SC_GRAVITATION]->val3 == BCT_SELF) ||
			(sc->data[SC_CAMOUFLAGE] && sc->data[SC_CAMOUFLAGE]->val1 < 3) )
			sc->cant.move += (start ? 1 : (sc->cant.move ? -1 : 0));
	}

	if( flag&SCS_NOCAST && !(flag&SCS_NOCASTCOND) ) //Can't use skills
		sc->cant.cast += (start ? 1 : -1);

	if( flag&SCS_NOCHAT ) { //Can't chat
		if( !(flag&SCS_NOCHATCOND) )
			sc->cant.chat += (start ? 1 : -1);
		else if( sc->data[SC_NOCHAT] && (sc->data[SC_NOCHAT]->val1&MANNER_NOCHAT) )
			sc->cant.chat += (start ? 1 : -1);
	}

	if( bl->type == BL_PC ) { //Player-only states
		if( flag&SCS_NOPICKITEM ) { //Can pick items?
			if( !(flag&SCS_NOPICKITEMCOND) )
				sc->cant.pickup += (start ? 1 : -1);
			else if( sc->data[SC_NOCHAT] && (sc->data[SC_NOCHAT]->val1&MANNER_NOITEM) )
				sc->cant.pickup += (start ? 1 : -1);
		}
		if( flag&SCS_NODROPITEM ) { //Can drop items?
			if( !(flag&SCS_NODROPITEMCOND) )
				sc->cant.drop += (start ? 1 : -1);
			else if( sc->data[SC_NOCHAT] && (sc->data[SC_NOCHAT]->val1&MANNER_NOITEM) )
				sc->cant.drop += (start ? 1 : -1);
		}
	}
}

/// Recalculates parts of an object's battle status according to the specified flags.
/// @param flag bitfield of values from enum scb_flag
void status_calc_bl_main(struct block_list *bl, /*enum scb_flag*/int flag)
{
	const struct status_data *b_status = status_get_base_status(bl);
	struct status_data *status = status_get_status_data(bl);
	struct status_change *sc = status_get_sc(bl);
	TBL_PC *sd = BL_CAST(BL_PC, bl);
	int temp = 0;

	if( !b_status || !status )
		return;

	//This needs to be done even if there is currently no status change active, because
	//we need to update the speed on the client when the last status change ends [Playtester]
	if( flag&SCB_SPEED ) {
		struct unit_data *ud = unit_bl2ud(bl);

		//Re-walk to adjust speed (we do not check if walktimer != INVALID_TIMER
		//because if you step on something while walking, the moment this
		//piece of code triggers the walk-timer is set on INVALID_TIMER) [Skotlex]
		if( ud )
			ud->state.change_walk_target = ud->state.speed_changed = 1;
	}

	if( flag&SCB_STR ) {
		status->str = status_calc_str(bl, sc, b_status->str);
		flag |= SCB_BATK;
		if( bl->type&BL_HOM )
			flag |= SCB_WATK;
	}

	if( flag&SCB_AGI ) {
		status->agi = status_calc_agi(bl, sc, b_status->agi);
		flag |= SCB_FLEE
#ifdef RENEWAL
			|SCB_DEF2
#endif
			;
		if( bl->type&(BL_PC|BL_HOM) )
			flag |= SCB_ASPD|SCB_DSPD;
	}

	if( flag&SCB_VIT ) {
		status->vit = status_calc_vit(bl, sc, b_status->vit);
		flag |= SCB_DEF2|SCB_MDEF2;
		if( bl->type&(BL_PC|BL_HOM|BL_MER|BL_ELEM) )
			flag |= SCB_MAXHP;
		if( bl->type&BL_HOM )
			flag |= SCB_DEF;
	}

	if( flag&SCB_INT ) {
		status->int_ = status_calc_int(bl, sc, b_status->int_);
		flag |= SCB_MATK|SCB_MDEF2;
		if( bl->type&(BL_PC|BL_HOM|BL_MER|BL_ELEM) )
			flag |= SCB_MAXSP;
		if( bl->type&BL_HOM )
			flag |= SCB_MDEF;
	}

	if( flag&SCB_DEX ) {
		status->dex = status_calc_dex(bl, sc, b_status->dex);
		flag |= SCB_BATK|SCB_HIT
#ifdef RENEWAL
			|SCB_MATK|SCB_MDEF2
#endif
			;
		if( bl->type&(BL_PC|BL_HOM) )
			flag |= SCB_ASPD;
		if( bl->type&BL_HOM )
			flag |= SCB_WATK;
	}

	if( flag&SCB_LUK ) {
		status->luk = status_calc_luk(bl, sc, b_status->luk);
		flag |= SCB_BATK|SCB_CRI|SCB_FLEE2
#ifdef RENEWAL
			|SCB_MATK|SCB_HIT|SCB_FLEE
#endif
			;
	}

	if( flag&SCB_BATK && b_status->batk ) {
		int level = status_get_lv(bl);

		status->batk = status_base_atk(bl, status, level);
		temp = b_status->batk - status_base_atk(bl, b_status, level);
		if( temp ) {
			temp += status->batk;
			status->batk = cap_value(temp, 0, USHRT_MAX);
		}
		status->batk = status_calc_batk(bl, sc, status->batk);
	}

	if( flag&SCB_WATK ) {
#ifndef RENEWAL
		status->rhw.atk = status_calc_watk(bl, sc, b_status->rhw.atk);
		if( !sd ) //Should not affect weapon refine bonus
			status->rhw.atk2 = status_calc_watk(bl, sc, b_status->rhw.atk2);
		if( b_status->lhw.atk ) {
			if( sd ) {
				sd->state.lr_flag = 1;
				status->lhw.atk = status_calc_watk(bl, sc, b_status->lhw.atk);
				sd->state.lr_flag = 0;
			} else {
				status->lhw.atk = status_calc_watk(bl, sc, b_status->lhw.atk);
				status->lhw.atk2 = status_calc_watk(bl, sc, b_status->lhw.atk2);
			}
		}
#else
		status->watk = status_calc_watk(bl, sc, b_status->watk);
		if( b_status->watk2 )
			status->watk2 = status_calc_watk(bl, sc, b_status->watk2);
#endif
	}

	if( flag&SCB_CRI && b_status->cri ) {
		if( status->luk == b_status->luk )
			status->cri = status_calc_critical(bl, sc, b_status->cri);
		else
			status->cri = status_calc_critical(bl, sc, b_status->cri + 3 * (status->luk - b_status->luk));
		//After status_calc_critical so the bonus is applied despite if you have or not a sc bugreport:5240
		if( battle_config.show_status_katar_crit && sd && sd->status.weapon == W_KATAR )
			status->cri *= 2;
	}

	if( flag&SCB_HIT ) {
		if( status->dex == b_status->dex
#ifdef RENEWAL
			&& status->luk == b_status->luk
#endif
			)
			status->hit = status_calc_hit(bl, sc, b_status->hit);
		else
			status->hit = status_calc_hit(bl, sc, b_status->hit + (status->dex - b_status->dex)
#ifdef RENEWAL
			 + (status->luk / 3 - b_status->luk / 3)
#endif
			);
	}

	if( flag&SCB_FLEE ) {
		if( status->agi == b_status->agi
#ifdef RENEWAL
			&& status->luk == b_status->luk
#endif
			)
			status->flee = status_calc_flee(bl, sc, b_status->flee);
		else
			status->flee = status_calc_flee(bl, sc, b_status->flee + (status->agi - b_status->agi)
#ifdef RENEWAL
			+ (status->luk / 5 - b_status->luk / 5)
#endif
			);
	}

	if( flag&SCB_FLEE2 && b_status->flee2 ) {
		if( status->luk == b_status->luk )
			status->flee2 = status_calc_flee2(bl, sc, b_status->flee2);
		else
			status->flee2 = status_calc_flee2(bl, sc, b_status->flee2 + (status->luk - b_status->luk));
	}

	if( flag&SCB_DEF ) {
		status->def = status_calc_def(bl, sc, b_status->def, true);
		if( bl->type&BL_HOM )
			status->def += (status->vit / 5 - b_status->vit / 5);
	}

	if( flag&SCB_DEF2 ) {
		if( status->vit == b_status->vit
#ifdef RENEWAL
			&& status->agi == b_status->agi
#endif
			)
			status->def2 = status_calc_def2(bl, sc, b_status->def2, true);
		else
			status->def2 = status_calc_def2(bl, sc, b_status->def2
#ifdef RENEWAL
			+ (int)(((float)status->vit / 2 - (float)b_status->vit / 2) + ((float)status->agi / 5 - (float)b_status->agi / 5))
#else
			+ (status->vit - b_status->vit)
#endif
			, true);
	}

	if( flag&SCB_MDEF ) {
		status->mdef = status_calc_mdef(bl, sc, b_status->mdef, true);
		if( bl->type&BL_HOM )
			status->mdef += (status->int_ / 5 - b_status->int_ / 5);
	}

	if( flag&SCB_MDEF2 ) {
		if( status->int_ == b_status->int_ && status->vit == b_status->vit
#ifdef RENEWAL
			&& status->dex == b_status->dex
#endif
			)
			status->mdef2 = status_calc_mdef2(bl, sc, b_status->mdef2, true);
		else
			status->mdef2 = status_calc_mdef2(bl, sc, b_status->mdef2 + (status->int_ - b_status->int_) +
#ifdef RENEWAL
			(int)(((float)status->dex / 5 - (float)b_status->dex / 5) + ((float)status->vit / 5 - (float)b_status->vit / 5))
#else
			((status->vit - b_status->vit)>>1)
#endif
			, true);
	}

	if( flag&SCB_SPEED ) {
		status->speed = status_calc_speed(bl, sc, b_status->speed);
		if( bl->type&BL_PC && !(sd && sd->state.permanent_speed) && status->speed < battle_config.max_walk_speed )
			status->speed = battle_config.max_walk_speed;
		if( bl->type&BL_PET && ((TBL_PET *)bl)->master)
			status->speed = status_get_speed(&((TBL_PET *)bl)->master->bl);
		if( bl->type&BL_HOM && (battle_config.hom_setting&HOMSET_COPY_SPEED) && ((TBL_HOM *)bl)->master)
			status->speed = status_get_speed(&((TBL_HOM *)bl)->master->bl);
		if( bl->type&BL_MER && ((TBL_MER *)bl)->master)
			status->speed = status_get_speed(&((TBL_MER *)bl)->master->bl);
		if( bl->type&BL_ELEM && ((TBL_ELEM *)bl)->master)
			status->speed = status_get_speed(&((TBL_ELEM *)bl)->master->bl);
	}

	if( flag&SCB_ATK_ELE ) {
		status->rhw.ele = status_calc_attack_element(bl, sc, b_status->rhw.ele);
		if( sd )
			sd->state.lr_flag = 1;
		status->lhw.ele = status_calc_attack_element(bl, sc, b_status->lhw.ele);
		if( sd )
			sd->state.lr_flag = 0;
	}

	if( flag&SCB_DEF_ELE ) {
		status->def_ele = status_calc_element(bl, sc, b_status->def_ele);
		status->ele_lv = status_calc_element_lv(bl, sc, b_status->ele_lv);
	}

	if( flag&SCB_MODE ) {
		status->mode = status_calc_mode(bl, sc, b_status->mode);
		//Only switch between Normal and Boss classes as the others are determined by the race value
		if( status->class_ == CLASS_NORMAL && status_has_mode(status, MD_STATUS_IMMUNE|MD_KNOCKBACK_IMMUNE|MD_DETECTOR) )
			status->class_ = CLASS_BOSS;
		else if( status->class_ == CLASS_BOSS && !status_has_mode(status, MD_STATUS_IMMUNE|MD_KNOCKBACK_IMMUNE|MD_DETECTOR) )
			status->class_ = CLASS_NORMAL;
		//Since mode changed, reset their state
		if( !status_has_mode(status, MD_CANATTACK) )
			unit_stop_attack(bl);
		if( !status_has_mode(status, MD_CANMOVE) )
			unit_stop_walking(bl, USW_FIXPOS);
	}

	//No status changes alter these yet
	//if(flag&SCB_SIZE)
	//if(flag&SCB_RACE)
	//if(flag&SCB_RANGE)

	if( flag&SCB_MAXHP ) {
		if( bl->type&BL_PC ) {
			status->max_hp = status_calc_maxhpsp_pc(sd, true);
			if( battle_config.hp_rate != 100 )
				status->max_hp = (unsigned int)(battle_config.hp_rate * (status->max_hp / 100.));
			status->max_hp = umin(status->max_hp, (unsigned int)battle_config.max_hp);
		} else
			status->max_hp = status_calc_maxhp(bl, b_status->max_hp);

		if( status->hp > status->max_hp ) { //FIXME: Should perhaps a status_zap should be issued?
			status->hp = status->max_hp;
			if( sd )
				clif_updatestatus(sd, SP_HP);
		}
	}

	if( flag&SCB_MAXSP ) {
		if( bl->type&BL_PC ) {
			status->max_sp = status_calc_maxhpsp_pc(sd, false);
			if( battle_config.sp_rate != 100 )
				status->max_sp = (unsigned int)(battle_config.sp_rate * (status->max_sp / 100.));
			status->max_sp = umin(status->max_sp, (unsigned int)battle_config.max_sp);
		} else
			status->max_sp = status_calc_maxsp(bl, b_status->max_sp);

		if( status->sp > status->max_sp ) {
			status->sp = status->max_sp;
			if( sd )
				clif_updatestatus(sd, SP_SP);
		}
	}

	if( flag&SCB_MATK )
		status_update_matk(bl);

	if( flag&SCB_DSPD ) {
		int dmotion;

		if( bl->type&BL_PC ) {
			if( b_status->agi == status->agi )
				status->dmotion = status_calc_dmotion(bl, sc, b_status->dmotion);
			else {
				dmotion = 800 - status->agi * 4;
				status->dmotion = cap_value(dmotion, 400, 800);
				if( battle_config.pc_damage_delay_rate != 100 )
					status->dmotion = status->dmotion * battle_config.pc_damage_delay_rate / 100;
				//It's safe to ignore b_status->dmotion since no bonus affects it
				status->dmotion = status_calc_dmotion(bl, sc, status->dmotion);
			}
		} else if( bl->type&BL_HOM ) {
			dmotion = 800 - status->agi * 4;
			status->dmotion = cap_value(dmotion, 400, 800);
			status->dmotion = status_calc_dmotion(bl, sc, b_status->dmotion);
		} else //Mercenary and mobs
			status->dmotion = status_calc_dmotion(bl, sc, b_status->dmotion);
	}

	if( flag&SCB_ASPD ) {
		int amotion;

		if( bl->type&BL_HOM ) {
#ifdef RENEWAL_ASPD
			amotion = ((TBL_HOM *)bl)->homunculusDB->baseASPD;
			amotion -= amotion * status_get_homdex(bl) / 1000 + status_get_homagi(bl) * amotion / 250;
			amotion = (amotion * status_calc_aspd(bl, sc, true) + status_calc_aspd(bl, sc, false)) / -100 + amotion;
#else
			amotion = (1000 - 4 * status->agi - status->dex) * ((TBL_HOM *)bl)->homunculusDB->baseASPD / 1000;
			amotion = status_calc_aspd_rate(bl, sc, amotion);
			amotion = amotion * status->aspd_rate / 1000;
#endif
			amotion = status_calc_fix_aspd(bl, sc, amotion);
			status->amotion = cap_value(amotion, battle_config.max_aspd, 2000);
			status->adelay = status->amotion;
		} else if( bl->type&BL_PC ) {
			uint8 lv = 0;

			amotion = status_base_amotion_pc(sd, status);
#ifndef RENEWAL_ASPD
			status->aspd_rate = status_calc_aspd_rate(bl, sc, b_status->aspd_rate);
#endif
			//Absolute ASPD % modifier
			amotion = amotion * status->aspd_rate / 1000;
			if( sd->ud.skilltimer != INVALID_TIMER && (lv = pc_checkskill(sd, SA_FREECAST)) > 0 )
				amotion = amotion * 5 * (lv + 10) / 100;
#ifdef RENEWAL_ASPD
			//RE ASPD % modifier
			amotion += (max(0xc3 - amotion, 2) * (status->aspd_rate2 + status_calc_aspd(bl, sc, false))) / 100;
			amotion = 10 * (200 - amotion);
			amotion += sd->bonus.aspd_add;
#endif
			amotion = status_calc_fix_aspd(bl, sc, amotion);
			status->amotion = cap_value(amotion, pc_maxaspd(sd), 2000);
			status->adelay = 2 * status->amotion;
		} else { //Mercenary, elemental and mobs
			amotion = b_status->amotion;
			status->aspd_rate = status_calc_aspd_rate(bl, sc, b_status->aspd_rate);
			amotion = amotion * status->aspd_rate / 1000;
			amotion = status_calc_fix_aspd(bl, sc, amotion);
			status->amotion = cap_value(amotion, battle_config.monster_max_aspd, 2000);
			temp = b_status->adelay * status->aspd_rate / 1000;
			status->adelay = cap_value(temp, battle_config.monster_max_aspd * 2, 4000);
			if( bl->type == BL_ELEM )
				status->adelay = status->amotion;
		}
	}

	if( flag&(SCB_VIT|SCB_MAXHP|SCB_INT|SCB_MAXSP) && bl->type&BL_REGEN )
		status_calc_regen(bl, status, status_get_regen_data(bl));

	if( flag&SCB_REGEN && bl->type&BL_REGEN )
		status_calc_regen_rate(bl, status_get_regen_data(bl), sc);
}

/// Recalculates parts of an object's base status and battle status according to the specified flags.
/// Also sends updates to the client wherever applicable.
/// @param flag bitfield of values from enum scb_flag
/// @param first if true, will cause status_calc_* functions to run their base status initialization code
void status_calc_bl_(struct block_list *bl, enum scb_flag flag, enum e_status_calc_opt opt)
{
	struct status_data b_status; //Previous battle status
	struct status_data *status; //Pointer to current battle status

	if( bl->type == BL_PC && ((TBL_PC *)bl)->delayed_damage ) {
		if( opt&SCO_FORCE )
			((TBL_PC *)bl)->state.hold_recalc = 0; //Clear and move on
		else {
			((TBL_PC *)bl)->state.hold_recalc = 1; //Flag and stop
			return;
		}
	}

	//Remember previous values
	status = status_get_status_data(bl);
	memcpy(&b_status, status, sizeof(struct status_data));

	if( flag&SCB_BASE ) { //Calculate the object's base status too
		switch( bl->type ) {
			case BL_PC:  status_calc_pc_(BL_CAST(BL_PC, bl), opt); break;
			case BL_MOB: status_calc_mob_(BL_CAST(BL_MOB, bl), opt); break;
			case BL_PET: status_calc_pet_(BL_CAST(BL_PET, bl), opt); break;
			case BL_HOM: status_calc_homunculus_(BL_CAST(BL_HOM, bl), opt); break;
			case BL_MER: status_calc_mercenary_(BL_CAST(BL_MER, bl), opt); break;
			case BL_ELEM: status_calc_elemental_(BL_CAST(BL_ELEM, bl), opt); break;
			case BL_NPC: status_calc_npc_(BL_CAST(BL_NPC, bl), opt); break;
		}
	}

	if( bl->type == BL_PET )
		return; //Pets are not affected by statuses

	if( (opt&SCO_FIRST) && bl->type == BL_MOB )
		return; //Assume there will be no statuses active

	status_calc_bl_main(bl, flag);

	if( (opt&SCO_FIRST) && bl->type == BL_HOM )
		return; //Client update handled by caller

	//Compare against new values and send client updates
	if( bl->type == BL_PC ) {
		TBL_PC *sd = BL_CAST(BL_PC, bl);

		if( b_status.str != status->str )
			clif_updatestatus(sd, SP_STR);
		if( b_status.agi != status->agi )
			clif_updatestatus(sd, SP_AGI);
		if( b_status.vit != status->vit )
			clif_updatestatus(sd, SP_VIT);
		if( b_status.int_ != status->int_ )
			clif_updatestatus(sd, SP_INT);
		if( b_status.dex != status->dex )
			clif_updatestatus(sd, SP_DEX);
		if( b_status.luk != status->luk )
			clif_updatestatus(sd, SP_LUK);
		if( b_status.hit != status->hit )
			clif_updatestatus(sd, SP_HIT);
		if( b_status.flee != status->flee )
			clif_updatestatus(sd, SP_FLEE1);
		if( b_status.amotion != status->amotion )
			clif_updatestatus(sd, SP_ASPD);
		if( b_status.speed != status->speed )
			clif_updatestatus(sd, SP_SPEED);

		if( b_status.batk != status->batk
#ifndef RENEWAL
			|| b_status.rhw.atk != status->rhw.atk || b_status.lhw.atk != status->lhw.atk
#endif
			)
			clif_updatestatus(sd, SP_ATK1);

		if( b_status.def != status->def ) {
			clif_updatestatus(sd, SP_DEF1);
#ifdef RENEWAL
			clif_updatestatus(sd, SP_DEF2);
#endif
		}

		if(
#ifdef RENEWAL
			b_status.watk != status->watk || b_status.watk2 != status->watk2 || b_status.eatk != status->eatk
#else
			b_status.rhw.atk2 != status->rhw.atk2 || b_status.lhw.atk2 != status->lhw.atk2
#endif
			)
			clif_updatestatus(sd, SP_ATK2);

		if( b_status.def2 != status->def2 ) {
			clif_updatestatus(sd, SP_DEF2);
#ifdef RENEWAL
			clif_updatestatus(sd, SP_DEF1);
#endif
		}
		if( b_status.flee2 != status->flee2 )
			clif_updatestatus(sd, SP_FLEE2);
		if( b_status.cri != status->cri )
			clif_updatestatus(sd, SP_CRITICAL);
#ifndef RENEWAL
		if( b_status.matk_max != status->matk_max )
			clif_updatestatus(sd, SP_MATK1);
		if( b_status.matk_min != status->matk_min )
			clif_updatestatus(sd, SP_MATK2);
#else
		if( b_status.matk_max != status->matk_max || b_status.matk_min != status->matk_min ) {
			clif_updatestatus(sd, SP_MATK2);
			clif_updatestatus(sd, SP_MATK1);
		}
#endif
		if( b_status.mdef != status->mdef ) {
			clif_updatestatus(sd, SP_MDEF1);
#ifdef RENEWAL
			clif_updatestatus(sd, SP_MDEF2);
#endif
		}
		if( b_status.mdef2 != status->mdef2 ) {
			clif_updatestatus(sd, SP_MDEF2);
#ifdef RENEWAL
			clif_updatestatus(sd, SP_MDEF1);
#endif
		}
		if( b_status.rhw.range != status->rhw.range )
			clif_updatestatus(sd, SP_ATTACKRANGE);
		if( b_status.max_hp != status->max_hp )
			clif_updatestatus(sd, SP_MAXHP);
		if( b_status.max_sp != status->max_sp )
			clif_updatestatus(sd, SP_MAXSP);
		if( b_status.hp != status->hp )
			clif_updatestatus(sd, SP_HP);
		if( b_status.sp != status->sp )
			clif_updatestatus(sd, SP_SP);
	} else if( bl->type == BL_HOM ) {
		TBL_HOM *hd = BL_CAST(BL_HOM, bl);

		if( hd->master && memcmp(&b_status, status, sizeof(struct status_data)) != 0 )
			clif_hominfo(hd->master, hd, 0);
	} else if( bl->type == BL_MER ) {
		TBL_MER *md = BL_CAST(BL_MER, bl);

		if( !md->master )
			return;

		if( b_status.rhw.atk != status->rhw.atk || b_status.rhw.atk2 != status->rhw.atk2 )
			clif_mercenary_updatestatus(md->master, SP_ATK1);
		if( b_status.matk_max != status->matk_max )
			clif_mercenary_updatestatus(md->master, SP_MATK1);
		if( b_status.hit != status->hit )
			clif_mercenary_updatestatus(md->master, SP_HIT);
		if( b_status.cri != status->cri )
			clif_mercenary_updatestatus(md->master, SP_CRITICAL);
		if( b_status.def != status->def )
			clif_mercenary_updatestatus(md->master, SP_DEF1);
		if( b_status.mdef != status->mdef )
			clif_mercenary_updatestatus(md->master, SP_MDEF1);
		if( b_status.flee != status->flee )
			clif_mercenary_updatestatus(md->master, SP_MERCFLEE);
		if( b_status.amotion != status->amotion )
			clif_mercenary_updatestatus(md->master, SP_ASPD);
		if( b_status.max_hp != status->max_hp )
			clif_mercenary_updatestatus(md->master, SP_MAXHP);
		if( b_status.max_sp != status->max_sp )
			clif_mercenary_updatestatus(md->master, SP_MAXSP);
		if( b_status.hp != status->hp )
			clif_mercenary_updatestatus(md->master, SP_HP);
		if( b_status.sp != status->sp )
			clif_mercenary_updatestatus(md->master, SP_SP);
	} else if( bl->type == BL_ELEM ) {
		TBL_ELEM *ed = BL_CAST(BL_ELEM, bl);

		if( !ed->master )
			return;

		if( b_status.max_hp != status->max_hp )
			clif_elemental_updatestatus(ed->master, SP_MAXHP);
		if( b_status.max_sp != status->max_sp )
			clif_elemental_updatestatus(ed->master, SP_MAXSP);
		if( b_status.hp != status->hp )
			clif_elemental_updatestatus(ed->master, SP_HP);
		if( b_status.sp != status->sp )
			clif_mercenary_updatestatus(ed->master, SP_SP);
	}
}

/*==========================================
 * Apply shared stat mods from status changes [DracoRPG]
 *------------------------------------------*/
unsigned short status_calc_str(struct block_list *bl, struct status_change *sc, int str)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(str,0,USHRT_MAX);

	if(sc->data[SC_FULL_THROTTLE])
		str += str * 20 / 100;
	if(sc->data[SC_INCALLSTATUS])
		str += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCSTR])
		str += sc->data[SC_INCSTR]->val1;
	if(sc->data[SC_STRFOOD])
		str += sc->data[SC_STRFOOD]->val1;
	if(sc->data[SC_FOOD_STR_CASH])
		str += sc->data[SC_FOOD_STR_CASH]->val1;
	if(bl->type == BL_PC) {
		if(sc->data[SC_BATTLEORDERS])
			str += 5;
		if(sc->data[SC_LEADERSHIP])
			str += sc->data[SC_LEADERSHIP]->val1;
	}
	if(sc->data[SC_LOUD])
		str += 4;
	if(sc->data[SC_TRUESIGHT])
		str += 5;
	if(sc->data[SC_STRUP])
		str += 10;
	if(sc->data[SC_NEN])
		str += sc->data[SC_NEN]->val1;
	if(sc->data[SC_BLESSING]) {
		if(sc->data[SC_BLESSING]->val2)
			str += sc->data[SC_BLESSING]->val2;
		else
			str /= 2;
	}
	if(sc->data[SC_CHASEWALK2])
		str += sc->data[SC_CHASEWALK2]->val1;
	if(sc->data[SC_GIANTGROWTH])
		str += 30;
	if(sc->data[SC_BEYONDOFWARCRY])
		str += sc->data[SC_BEYONDOFWARCRY]->val3;
	if(sc->data[SC_SAVAGE_STEAK])
		str += sc->data[SC_SAVAGE_STEAK]->val1;
	if(sc->data[SC_INSPIRATION])
		str += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_ALMIGHTY])
		str += sc->data[SC_ALMIGHTY]->val1;
	if(sc->data[SC_MARIONETTE2])
		str += ((sc->data[SC_MARIONETTE2]->val3)>>16)&0xFF;
	if(sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		str += ((sc->data[SC_SPIRIT]->val3)>>16)&0xFF;
	if(sc->data[SC_SWORDCLAN])
		str += 1;
	if(sc->data[SC_JUMPINGCLAN])
		str += 1;
	if(sc->data[SC_CHEERUP])
		str += 3;
	if(sc->data[SC_HARMONIZE])
		str -= sc->data[SC_HARMONIZE]->val2;
	if(sc->data[SC_STOMACHACHE])
		str -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		str -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		str -= ((sc->data[SC_MARIONETTE]->val3)>>16)&0xFF;

	return (unsigned short)cap_value(str,0,USHRT_MAX);
}

unsigned short status_calc_agi(struct block_list *bl, struct status_change *sc, int agi)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(agi,0,USHRT_MAX);

	if(sc->data[SC_FULL_THROTTLE])
		agi += agi * 20 / 100;
	if(sc->data[SC_CONCENTRATE] && !sc->data[SC_QUAGMIRE])
		agi += (agi - sc->data[SC_CONCENTRATE]->val3) * sc->data[SC_CONCENTRATE]->val2 / 100;
	if(sc->data[SC_INCALLSTATUS])
		agi += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCAGI])
		agi += sc->data[SC_INCAGI]->val1;
	if(sc->data[SC_AGIFOOD])
		agi += sc->data[SC_AGIFOOD]->val1;
	if(sc->data[SC_FOOD_AGI_CASH])
		agi += sc->data[SC_FOOD_AGI_CASH]->val1;
	if(sc->data[SC_SOULCOLD] && bl->type == BL_PC)
		agi += sc->data[SC_SOULCOLD]->val1;
	if(sc->data[SC_TRUESIGHT])
		agi += 5;
	if(sc->data[SC_INCREASEAGI])
		agi += sc->data[SC_INCREASEAGI]->val2;
	if(sc->data[SC_INCREASING])
		agi += 4; //Added based on skill updates [Reddozen]
	if(sc->data[SC_DROCERA_HERB_STEAMED])
		agi += sc->data[SC_DROCERA_HERB_STEAMED]->val1;
	if(sc->data[SC_INSPIRATION])
		agi += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_ALMIGHTY])
		agi += sc->data[SC_ALMIGHTY]->val1;
	if(sc->data[SC_MARIONETTE2])
		agi += ((sc->data[SC_MARIONETTE2]->val3)>>8)&0xFF;
	if(sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		agi += ((sc->data[SC_SPIRIT]->val3)>>8)&0xFF;
	if(sc->data[SC_ARCLOUSEDASH])
		agi += sc->data[SC_ARCLOUSEDASH]->val2;
	if(sc->data[SC_CROSSBOWCLAN])
		agi += 1;
	if(sc->data[SC_JUMPINGCLAN])
		agi += 1;
	if(sc->data[SC_CHEERUP])
		agi += 3;
	if(sc->data[SC_HARMONIZE])
		agi -= sc->data[SC_HARMONIZE]->val2;
	if(sc->data[SC_DECREASEAGI])
		agi -= sc->data[SC_DECREASEAGI]->val2;
	if(sc->data[SC_QUAGMIRE])
		agi -= sc->data[SC_QUAGMIRE]->val2;
	if(sc->data[SC_SUITON] && sc->data[SC_SUITON]->val2)
		agi -= sc->data[SC_SUITON]->val2;
	if(sc->data[SC_ADORAMUS])
		agi -= sc->data[SC_ADORAMUS]->val2;
	if(sc->data[SC_STOMACHACHE])
		agi -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		agi -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		agi -= ((sc->data[SC_MARIONETTE]->val3)>>8)&0xFF;

	return (unsigned short)cap_value(agi,0,USHRT_MAX);
}

unsigned short status_calc_vit(struct block_list *bl, struct status_change *sc, int vit)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(vit,0,USHRT_MAX);

	if(sc->data[SC_FULL_THROTTLE])
		vit += vit * 20 / 100;
	if(sc->data[SC_INCALLSTATUS])
		vit += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCVIT])
		vit += sc->data[SC_INCVIT]->val1;
	if(sc->data[SC_VITFOOD])
		vit += sc->data[SC_VITFOOD]->val1;
	if(sc->data[SC_FOOD_VIT_CASH])
		vit += sc->data[SC_FOOD_VIT_CASH]->val1;
	if(sc->data[SC_CHANGE])
		vit += sc->data[SC_CHANGE]->val2;
	if(sc->data[SC_GLORYWOUNDS] && bl->type == BL_PC)
		vit += sc->data[SC_GLORYWOUNDS]->val1;
	if(sc->data[SC_TRUESIGHT])
		vit += 5;
	if(sc->data[SC_MINOR_BBQ])
		vit += sc->data[SC_MINOR_BBQ]->val1;
	if(sc->data[SC_INSPIRATION])
		vit += sc->data[SC_INSPIRATION]->val3;
#ifdef RENEWAL
	if(sc->data[SC_DEFENCE])
		vit += sc->data[SC_DEFENCE]->val2;
#endif
	if(sc->data[SC_ALMIGHTY])
		vit += sc->data[SC_ALMIGHTY]->val1;
	if(sc->data[SC_MARIONETTE2])
		vit += sc->data[SC_MARIONETTE2]->val3&0xFF;
	if(sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		vit += sc->data[SC_SPIRIT]->val3&0xFF;
	if(sc->data[SC_SWORDCLAN])
		vit += 1;
	if(sc->data[SC_JUMPINGCLAN])
		vit += 1;
	if(sc->data[SC_CHEERUP])
		vit += 3;
	if(sc->data[SC_STRIPARMOR] && bl->type != BL_PC)
		vit -= vit * sc->data[SC_STRIPARMOR]->val2 / 100;
	if(sc->data[SC_HARMONIZE])
		vit -= sc->data[SC_HARMONIZE]->val2;
	if(sc->data[SC_STOMACHACHE])
		vit -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		vit -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		vit -= sc->data[SC_MARIONETTE]->val3&0xFF;

	return (unsigned short)cap_value(vit,0,USHRT_MAX);
}

unsigned short status_calc_int(struct block_list *bl, struct status_change *sc, int int_)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(int_,0,USHRT_MAX);

	if(sc->data[SC_FULL_THROTTLE])
		int_ += int_ * 20 / 100;
	if(sc->data[SC_INCALLSTATUS])
		int_ += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCINT])
		int_ += sc->data[SC_INCINT]->val1;
	if(sc->data[SC_INTFOOD])
		int_ += sc->data[SC_INTFOOD]->val1;
	if(sc->data[SC_FOOD_INT_CASH])
		int_ += sc->data[SC_FOOD_INT_CASH]->val1;
	if(sc->data[SC_CHANGE])
		int_ += sc->data[SC_CHANGE]->val3;
	if(sc->data[SC_BATTLEORDERS] && bl->type == BL_PC)
		int_ += 5;
	if(sc->data[SC_TRUESIGHT])
		int_ += 5;
	if(sc->data[SC_BLESSING]) {
		if (sc->data[SC_BLESSING]->val2)
			int_ += sc->data[SC_BLESSING]->val2;
		else
			int_ /= 2;
	}
	if(sc->data[SC_NEN])
		int_ += sc->data[SC_NEN]->val1;
	if(sc->data[SC_INSPIRATION])
		int_ += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_COCKTAIL_WARG_BLOOD])
		int_ += sc->data[SC_COCKTAIL_WARG_BLOOD]->val1;
	if(sc->data[SC_ALMIGHTY])
		int_ += sc->data[SC_ALMIGHTY]->val1;
	if(sc->data[SC_MARIONETTE2])
		int_ += ((sc->data[SC_MARIONETTE2]->val4)>>16)&0xFF;
	if(sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		int_ += ((sc->data[SC_SPIRIT]->val4)>>16)&0xFF;
	if(sc->data[SC_ARCWANDCLAN])
		int_ += 1;
	if(sc->data[SC_GOLDENMACECLAN])
		int_ += 1;
	if(sc->data[SC_JUMPINGCLAN])
		int_ += 1;
	if(sc->data[SC_CHEERUP])
		int_ += 3;
	if(bl->type != BL_PC) {
		if(sc->data[SC_STRIPHELM])
			int_ -= int_ * sc->data[SC_STRIPHELM]->val2 / 100;
		if(sc->data[SC__STRIPACCESSORY])
			int_ -= int_ * sc->data[SC__STRIPACCESSORY]->val2 / 100;
	}
	if(sc->data[SC_HARMONIZE])
		int_ -= sc->data[SC_HARMONIZE]->val2;
	if(sc->data[SC_MELODYOFSINK])
		int_ -= sc->data[SC_MELODYOFSINK]->val3;
	if(sc->data[SC_MANDRAGORA])
		int_ -= 4 * sc->data[SC_MANDRAGORA]->val1;
	if(sc->data[SC_STOMACHACHE])
		int_ -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		int_ -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		int_ -= ((sc->data[SC_MARIONETTE]->val4)>>16)&0xFF;

	return (unsigned short)cap_value(int_,0,USHRT_MAX);
}

unsigned short status_calc_dex(struct block_list *bl, struct status_change *sc, int dex)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(dex,0,USHRT_MAX);

	if(sc->data[SC_FULL_THROTTLE])
		dex += dex * 20 / 100;
	if(sc->data[SC_CONCENTRATE] && !sc->data[SC_QUAGMIRE])
		dex += (dex - sc->data[SC_CONCENTRATE]->val4) * sc->data[SC_CONCENTRATE]->val2 / 100;
	if(sc->data[SC_INCALLSTATUS])
		dex += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCDEX])
		dex += sc->data[SC_INCDEX]->val1;
	if(sc->data[SC_DEXFOOD])
		dex += sc->data[SC_DEXFOOD]->val1;
	if(sc->data[SC_FOOD_DEX_CASH])
		dex += sc->data[SC_FOOD_DEX_CASH]->val1;
	if(bl->type == BL_PC) {
		if(sc->data[SC_BATTLEORDERS])
			dex += 5;
		if(sc->data[SC_HAWKEYES])
			dex += sc->data[SC_HAWKEYES]->val1;
	}
	if(sc->data[SC_TRUESIGHT])
		dex += 5;
	if(sc->data[SC_BLESSING]) {
		if (sc->data[SC_BLESSING]->val2)
			dex += sc->data[SC_BLESSING]->val2;
		else
			dex /= 2;
	}
	if(sc->data[SC_INCREASING])
		dex += 4; //Added based on skill updates [Reddozen]
	if(sc->data[SC_SIROMA_ICE_TEA])
		dex += sc->data[SC_SIROMA_ICE_TEA]->val1;
	if(sc->data[SC_INSPIRATION])
		dex += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_ALMIGHTY])
		dex += sc->data[SC_ALMIGHTY]->val1;
	if(sc->data[SC_MARIONETTE2])
		dex += ((sc->data[SC_MARIONETTE2]->val4)>>8)&0xFF;
	if(sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		dex += ((sc->data[SC_SPIRIT]->val4)>>8)&0xFF;
	if(sc->data[SC_ARCWANDCLAN])
		dex += 1;
	if(sc->data[SC_CROSSBOWCLAN])
		dex += 1;
	if(sc->data[SC_JUMPINGCLAN])
		dex += 1;
	if(sc->data[SC_CHEERUP])
		dex += 3;
	if(sc->data[SC_HARMONIZE])
		dex -= sc->data[SC_HARMONIZE]->val2;
	if(sc->data[SC_QUAGMIRE])
		dex -= sc->data[SC_QUAGMIRE]->val2;
	if(sc->data[SC_STOMACHACHE])
		dex -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		dex -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC__STRIPACCESSORY] && bl->type != BL_PC)
		dex -= dex * sc->data[SC__STRIPACCESSORY]->val2 / 100;
	if(sc->data[SC_MARIONETTE])
		dex -= ((sc->data[SC_MARIONETTE]->val4)>>8)&0xFF;

	return (unsigned short)cap_value(dex,0,USHRT_MAX);
}

unsigned short status_calc_luk(struct block_list *bl, struct status_change *sc, int luk)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(luk,0,USHRT_MAX);

	if(sc->data[SC_CURSE])
		return 0;

	if(sc->data[SC_FULL_THROTTLE])
		luk += luk * 20 / 100;
	if(sc->data[SC_INCALLSTATUS])
		luk += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCLUK])
		luk += sc->data[SC_INCLUK]->val1;
	if(sc->data[SC_LUKFOOD])
		luk += sc->data[SC_LUKFOOD]->val1;
	if(sc->data[SC_FOOD_LUK_CASH])
		luk += sc->data[SC_FOOD_LUK_CASH]->val1;
	if(sc->data[SC_TRUESIGHT])
		luk += 5;
	if(sc->data[SC_GLORIA])
		luk += 30;
	if(sc->data[SC_PUTTI_TAILS_NOODLES])
		luk += sc->data[SC_PUTTI_TAILS_NOODLES]->val1;
	if(sc->data[SC_INSPIRATION])
		luk += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_ALMIGHTY])
		luk += sc->data[SC_ALMIGHTY]->val1;
	if(sc->data[SC_MARIONETTE2])
		luk += sc->data[SC_MARIONETTE2]->val4&0xFF;
	if(sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		luk += sc->data[SC_SPIRIT]->val4&0xFF;
	if(sc->data[SC_GOLDENMACECLAN])
		luk += 1;
	if(sc->data[SC_JUMPINGCLAN])
		luk += 1;
	if(sc->data[SC_CHEERUP])
		luk += 3;
	if(sc->data[SC__STRIPACCESSORY] && bl->type != BL_PC)
		luk -= luk * sc->data[SC__STRIPACCESSORY]->val2 / 100;
	if(sc->data[SC_BANANA_BOMB])
		luk -= 75;
	if(sc->data[SC_HARMONIZE])
		luk -= sc->data[SC_HARMONIZE]->val2;
	if(sc->data[SC_STOMACHACHE])
		luk -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		luk -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		luk -= sc->data[SC_MARIONETTE]->val4&0xFF;

	return (unsigned short)cap_value(luk,0,USHRT_MAX);
}

unsigned short status_calc_batk(struct block_list *bl, struct status_change *sc, int batk)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(batk,0,USHRT_MAX);

#ifndef RENEWAL
	if(sc->data[SC_MADNESSCANCEL])
		batk += 100;
	if(sc->data[SC_GATLINGFEVER])
		batk += sc->data[SC_GATLINGFEVER]->val3;
#endif
	if(sc->data[SC_BATKFOOD])
		batk += sc->data[SC_BATKFOOD]->val1;
	if(sc->data[SC_ANGRIFFS_MODUS])
		batk += sc->data[SC_ANGRIFFS_MODUS]->val2;
	if(sc->data[SC_SKE])
		batk += 3 * batk;
	if(sc->data[SC_INCATKRATE] && bl->type != BL_PC)
		batk += batk * sc->data[SC_INCATKRATE]->val1 / 100;
	if(sc->data[SC_PROVOKE]
#ifdef RENEWAL
		&& bl->type != BL_PC
#endif
		)
		batk += batk * sc->data[SC_PROVOKE]->val3 / 100;
#ifndef RENEWAL
	if(sc->data[SC_CONCENTRATION])
		batk += batk * sc->data[SC_CONCENTRATION]->val2 / 100;
#endif
	if(sc->data[SC_BLOODLUST])
		batk += batk * sc->data[SC_BLOODLUST]->val2 / 100;
	if(sc->data[SC_FLEET])
		batk += batk * sc->data[SC_FLEET]->val3 / 100;
	if(sc->data[SC_CURSE])
		batk -= batk * 25 / 100;
#ifdef RENEWAL
	if(sc->data[SC_STRIPWEAPON] && bl->type != BL_PC)
		batk -= batk * sc->data[SC_STRIPWEAPON]->val2 / 100;
#endif
	if(sc->data[SC_JOINTBEAT] && sc->data[SC_JOINTBEAT]->val2&BREAK_WAIST)
		batk -= batk * 25 / 100;
	if(sc->data[SC__ENERVATION])
		batk -= batk * sc->data[SC__ENERVATION]->val2 / 100;
	if(sc->data[SC_ASH])
		batk -= batk * sc->data[SC_ASH]->val4 / 100;
	if(sc->data[SC_CATNIPPOWDER] && bl->type != BL_PC)
		batk -= batk * sc->data[SC_CATNIPPOWDER]->val2 / 100;

	return (unsigned short)cap_value(batk,0,USHRT_MAX);
}

unsigned short status_calc_watk(struct block_list *bl, struct status_change *sc, int watk)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(watk,0,USHRT_MAX);

	if(sc->data[SC_WATKFOOD])
		watk += sc->data[SC_WATKFOOD]->val1;
	if(sc->data[SC_IMPOSITIO])
		watk += sc->data[SC_IMPOSITIO]->val2;
#ifndef RENEWAL
	if(sc->data[SC_VOLCANO])
		watk += sc->data[SC_VOLCANO]->val2;
	if(sc->data[SC_DRUMBATTLE])
		watk += sc->data[SC_DRUMBATTLE]->val2;
	if(sc->data[SC_NIBELUNGEN]) {
		if(bl->type != BL_PC)
			watk += sc->data[SC_NIBELUNGEN]->val2;
		else {
			TBL_PC *sd = (TBL_PC *)bl;
			short index = sd->equip_index[sd->state.lr_flag == 1 ? EQI_HAND_L : EQI_HAND_R];

			if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->wlv == 4)
				watk += sc->data[SC_NIBELUNGEN]->val2;
		}
	}
#endif
	if(sc->data[SC_MERC_ATKUP])
		watk += sc->data[SC_MERC_ATKUP]->val2;
	if(sc->data[SC_SKE])
		watk += 3 * watk;
	if(sc->data[SC_SHIELDSPELL_DEF] && sc->data[SC_SHIELDSPELL_DEF]->val1 == 3)
		watk += sc->data[SC_SHIELDSPELL_DEF]->val2;
	if(sc->data[SC_BANDING] && sc->data[SC_BANDING]->val2 > 1)
		watk += sc->data[SC_BANDING]->val2 * (10 + 10 * sc->data[SC_BANDING]->val1);
	if(sc->data[SC_INSPIRATION])
		watk += 40 * sc->data[SC_INSPIRATION]->val1 + 3 * sc->data[SC_INSPIRATION]->val2;
	if(sc->data[SC_STRIKING])
		watk += sc->data[SC_STRIKING]->val2;
	if(sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 2)
		watk += 50;
	if(sc->data[SC_PYROCLASTIC] && bl->type == BL_PC)
		watk += sc->data[SC_PYROCLASTIC]->val2;
	if(sc->data[SC_ODINS_POWER])
		watk += 40 + 30 * sc->data[SC_ODINS_POWER]->val1;
	if(sc->data[SC_FULL_SWING_K])
		watk += sc->data[SC_FULL_SWING_K]->val1;
	if(sc->data[SC_ANGRIFFS_MODUS])
		watk += sc->data[SC_ANGRIFFS_MODUS]->val2;
	if(sc->data[SC_CHATTERING])
		watk += sc->data[SC_CHATTERING]->val2;
	if(sc->data[SC_PROVOKE]
#ifdef RENEWAL
		&& bl->type != BL_PC
#endif
		)
		watk += watk * sc->data[SC_PROVOKE]->val3 / 100;
#ifndef RENEWAL
	if(sc->data[SC_CONCENTRATION])
		watk += watk * sc->data[SC_CONCENTRATION]->val2 / 100;
#endif
	if(sc->data[SC_FLEET])
		watk += watk * sc->data[SC_FLEET]->val3 / 100;
	if((sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2))
		watk += watk * 10 / 100;
	if(sc->data[SC_TIDAL_WEAPON])
		watk += watk * sc->data[SC_TIDAL_WEAPON]->val2 / 100;
	if(sc->data[SC_CURSE])
		watk -= watk * 25 / 100;
#ifndef RENEWAL
	if(sc->data[SC_STRIPWEAPON] && bl->type != BL_PC)
		watk -= watk * sc->data[SC_STRIPWEAPON]->val2 / 100;
#endif
	if(sc->data[SC__ENERVATION])
		watk -= watk * sc->data[SC__ENERVATION]->val2 / 100;

	return (unsigned short)cap_value(watk,0,USHRT_MAX);
}

unsigned short status_calc_matk(struct block_list *bl, struct status_change *sc, int matk, bool viewable)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(matk,0,USHRT_MAX);

	if(!viewable) { //Some statuses that are hidden in the status window
		if(sc->data[SC_MATKPOTION])
			matk += sc->data[SC_MATKPOTION]->val1;
		if(sc->data[SC_ZANGETSU])
			matk += sc->data[SC_ZANGETSU]->val3;
		if(sc->data[SC_INCMATKRATE])
			matk += matk * sc->data[SC_INCMATKRATE]->val1 / 100;
		if(sc->data[SC_MINDBREAKER])
			matk += matk * sc->data[SC_MINDBREAKER]->val2 / 100;
		return (unsigned short)cap_value(matk,0,USHRT_MAX);
	}
#ifndef RENEWAL
	//Take note fixed value first before % modifiers
	if(sc->data[SC_MATKFOOD])
		matk += sc->data[SC_MATKFOOD]->val1;
	if(sc->data[SC_MANA_PLUS])
		matk += sc->data[SC_MANA_PLUS]->val1;
	if(sc->data[SC_AQUAPLAY_OPTION])
		matk += sc->data[SC_AQUAPLAY_OPTION]->val2;
	if(sc->data[SC_CHILLY_AIR_OPTION])
		matk += sc->data[SC_CHILLY_AIR_OPTION]->val2;
	if(sc->data[SC_COOLER_OPTION])
		matk += sc->data[SC_COOLER_OPTION]->val2;
	if(sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 3)
		matk += 50;
	if(sc->data[SC_ODINS_POWER])
		matk += 40 + 30 * sc->data[SC_ODINS_POWER]->val1;
	if(sc->data[SC_MOONLITSERENADE])
		matk += sc->data[SC_MOONLITSERENADE]->val4;
	if(sc->data[SC_IZAYOI])
		matk += sc->data[SC_IZAYOI]->val2;
	if(sc->data[SC_QUEST_BUFF1])
		matk += sc->data[SC_QUEST_BUFF1]->val1;
	if(sc->data[SC_QUEST_BUFF2])
		matk += sc->data[SC_QUEST_BUFF2]->val1;
	if(sc->data[SC_QUEST_BUFF3])
		matk += sc->data[SC_QUEST_BUFF3]->val1;
	if(sc->data[SC_MTF_MATK2])
		matk += sc->data[SC_MTF_MATK2]->val1;
	if(sc->data[SC_ALMIGHTY])
		matk += 30;
	if(sc->data[SC_FENRIR_CARD])
		matk += sc->data[SC_FENRIR_CARD]->val1;
	if(sc->data[SC_CHATTERING])
		matk += sc->data[SC_CHATTERING]->val2;
	if(sc->data[SC_DORAM_MATK])
		matk += sc->data[SC_DORAM_MATK]->val2;
#endif
	if(sc->data[SC_MAGICPOWER] && sc->data[SC_MAGICPOWER]->val4)
		matk += matk * sc->data[SC_MAGICPOWER]->val3 / 100;
	if(sc->data[SC_MTF_MATK])
		matk += matk * sc->data[SC_MTF_MATK]->val1 / 100;
	if(sc->data[SC_SHRIMP])
		matk += matk * sc->data[SC_SHRIMP]->val2 / 100;
	if(sc->data[SC_CATNIPPOWDER])
		matk -= matk * sc->data[SC_CATNIPPOWDER]->val2 / 100;

	return (unsigned short)cap_value(matk,0,USHRT_MAX);
}

#ifdef RENEWAL
unsigned short status_calc_ematk(struct block_list *bl, struct status_change *sc, int matk, bool viewable)
{
	if(!sc || !sc->count)
		return (unsigned short)cap_value(matk,0,USHRT_MAX);

	if(viewable) {
		if(sc->data[SC_CHATTERING])
			matk += sc->data[SC_CHATTERING]->val2;
		return (unsigned short)cap_value(matk,0,USHRT_MAX);
	}

	if(sc->data[SC_MATKFOOD])
		matk += sc->data[SC_MATKFOOD]->val1;
	if(sc->data[SC_MANA_PLUS])
		matk += sc->data[SC_MANA_PLUS]->val1;
	if(sc->data[SC_AQUAPLAY_OPTION])
		matk += sc->data[SC_AQUAPLAY_OPTION]->val2;
	if(sc->data[SC_CHILLY_AIR_OPTION])
		matk += sc->data[SC_CHILLY_AIR_OPTION]->val2;
	if(sc->data[SC_COOLER_OPTION])
		matk += sc->data[SC_COOLER_OPTION]->val2;
	if(sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 3)
		matk += 50;
	if(sc->data[SC_ODINS_POWER])
		matk += 40 + 30 * sc->data[SC_ODINS_POWER]->val1;
	if(sc->data[SC_MOONLITSERENADE])
		matk += sc->data[SC_MOONLITSERENADE]->val4;
	if(sc->data[SC_IZAYOI])
		matk += sc->data[SC_IZAYOI]->val2;
	if(sc->data[SC_QUEST_BUFF1])
		matk += sc->data[SC_QUEST_BUFF1]->val1;
	if(sc->data[SC_QUEST_BUFF2])
		matk += sc->data[SC_QUEST_BUFF2]->val1;
	if(sc->data[SC_QUEST_BUFF3])
		matk += sc->data[SC_QUEST_BUFF3]->val1;
	if(sc->data[SC_MTF_MATK2])
		matk += sc->data[SC_MTF_MATK2]->val1;
	if(sc->data[SC_ALMIGHTY])
		matk += 30;
	if(sc->data[SC_FENRIR_CARD])
		matk += sc->data[SC_FENRIR_CARD]->val1;
	if(sc->data[SC_DORAM_MATK])
		matk += sc->data[SC_DORAM_MATK]->val2;

	return (unsigned short)cap_value(matk,0,USHRT_MAX);
}
#endif

short status_calc_critical(struct block_list *bl, struct status_change *sc, int critical)
{
	if(!sc || !sc->count)
		return (short)cap_value(critical,10,SHRT_MAX);

	if(sc->data[SC_INCCRI])
		critical += sc->data[SC_INCCRI]->val2;
	if(sc->data[SC_CRIFOOD])
		critical += sc->data[SC_CRIFOOD]->val2;
	if(sc->data[SC_EXPLOSIONSPIRITS])
		critical += sc->data[SC_EXPLOSIONSPIRITS]->val2;
	if(sc->data[SC_FORTUNE])
		critical += sc->data[SC_FORTUNE]->val2;
	if(sc->data[SC_TRUESIGHT])
		critical += sc->data[SC_TRUESIGHT]->val2;
	if(sc->data[SC_CLOAKING])
		critical += critical;
#ifdef RENEWAL
	if(sc->data[SC_SPEARQUICKEN])
		critical += sc->data[SC_SPEARQUICKEN]->val1 * 30;
#endif
	if(sc->data[SC_STRIKING])
		critical += sc->data[SC_STRIKING]->val1 * 10;
	if(sc->data[SC_BEYONDOFWARCRY])
		critical += sc->data[SC_BEYONDOFWARCRY]->val3 * 10;
	if(sc->data[SC__INVISIBILITY])
		critical += sc->data[SC__INVISIBILITY]->val2 * 10;
	if(sc->data[SC__UNLUCKY])
		critical -= sc->data[SC__UNLUCKY]->val2;

	return (short)cap_value(critical,10,SHRT_MAX);
}

short status_calc_hit(struct block_list *bl, struct status_change *sc, int hit)
{
	if(!sc || !sc->count)
		return (short)cap_value(hit,1,SHRT_MAX);

	if(sc->data[SC_INCHIT])
		hit += sc->data[SC_INCHIT]->val1;
	if(sc->data[SC_HITFOOD])
		hit += sc->data[SC_HITFOOD]->val1;
	if(sc->data[SC_TRUESIGHT])
		hit += sc->data[SC_TRUESIGHT]->val3;
	if(sc->data[SC_HUMMING])
		hit += sc->data[SC_HUMMING]->val2;
	if(sc->data[SC_CONCENTRATION])
		hit += sc->data[SC_CONCENTRATION]->val3;
	if(sc->data[SC_INCREASING])
		hit += 20; //RockmanEXE: Changed based on updated [Reddozen]
	if(sc->data[SC_INSPIRATION])
		hit += 5 * sc->data[SC_INSPIRATION]->val1 + sc->data[SC_INSPIRATION]->val2 / 2;
	if(sc->data[SC_MERC_HITUP])
		hit += sc->data[SC_MERC_HITUP]->val2;
	if(sc->data[SC_MTF_HITFLEE])
		hit += sc->data[SC_MTF_HITFLEE]->val1;
	if(sc->data[SC_MTF_ASPD])
		hit += sc->data[SC_MTF_ASPD]->val2;
	if(sc->data[SC_MTF_ASPD2])
		hit += sc->data[SC_MTF_ASPD2]->val2;
	if(sc->data[SC_ADJUSTMENT])
		hit -= 30;
	if(sc->data[SC_ILLUSIONDOPING])
		hit -= 50;
	if(sc->data[SC_BLIND] || sc->data[SC_PYREXIA])
		hit -= hit * 25 / 100;
	if(sc->data[SC_FEAR])
		hit -= hit * 20 / 100;
	if(sc->data[SC__GROOMY])
		hit -= hit * sc->data[SC__GROOMY]->val2 / 100;
	if(sc->data[SC_TEARGAS])
		hit -= hit * 50 / 100;
	if(sc->data[SC_ASH])
		hit -= hit * sc->data[SC_ASH]->val2 / 100;
	if(sc->data[SC_HEAT_BARREL])
		hit -= sc->data[SC_HEAT_BARREL]->val4;

	return (short)cap_value(hit,1,SHRT_MAX);
}

short status_calc_flee(struct block_list *bl, struct status_change *sc, int flee)
{
	if(bl->type == BL_PC) {
		if(map_flag_gvg2(bl->m))
			flee -= flee * battle_config.gvg_flee_penalty / 100;
		else if(map[bl->m].flag.battleground)
			flee -= flee * battle_config.bg_flee_penalty / 100;
	}

	if(!sc || !sc->count)
		return (short)cap_value(flee,1,SHRT_MAX);
	if(sc->data[SC_OVERED_BOOST]) //Should be final and unmodifiable by any means
		return sc->data[SC_OVERED_BOOST]->val2;
	if(sc->data[SC_TINDER_BREAKER])
		return 0;

	if(sc->data[SC_INCFLEE])
		flee += sc->data[SC_INCFLEE]->val1;
	if(sc->data[SC_FLEEFOOD])
		flee += sc->data[SC_FLEEFOOD]->val1;
	if(sc->data[SC_PARTYFLEE])
		flee += sc->data[SC_PARTYFLEE]->val1 * 10;
#ifdef RENEWAL
	if(sc->data[SC_SPEARQUICKEN])
		flee += 2 * sc->data[SC_SPEARQUICKEN]->val1;
#endif
	if(sc->data[SC_CLOSECONFINE])
		flee += 10;
	if(sc->data[SC_WHISTLE])
		flee += sc->data[SC_WHISTLE]->val2;
	if(sc->data[SC_VIOLENTGALE])
		flee += sc->data[SC_VIOLENTGALE]->val2;
	if(sc->data[SC_WINDWALK])
		flee += sc->data[SC_WINDWALK]->val2;
	if(sc->data[SC_MOON_COMFORT]) //SG skill [Komurka]
		flee += sc->data[SC_MOON_COMFORT]->val2;
	if(sc->data[SC_ADJUSTMENT])
		flee += 30;
	if(sc->data[SC_SPEED])
		flee += 10 + sc->data[SC_SPEED]->val1 * 10;
	if(sc->data[SC_MERC_FLEEUP])
		flee += sc->data[SC_MERC_FLEEUP]->val2;
	if(sc->data[SC_HALLUCINATIONWALK])
		flee += sc->data[SC_HALLUCINATIONWALK]->val2;
	if(sc->data[SC_ZEPHYR])
		flee += sc->data[SC_ZEPHYR]->val2;
	if(sc->data[SC_MTF_HITFLEE])
		flee += sc->data[SC_MTF_HITFLEE]->val2;
	if(sc->data[SC_GOLDENE_FERSE])
		flee += sc->data[SC_GOLDENE_FERSE]->val2;
	if(sc->data[SC_INCFLEERATE])
		flee += flee * sc->data[SC_INCFLEERATE]->val1 / 100;
	if(sc->data[SC_WIND_STEP_OPTION])
		flee += flee * sc->data[SC_WIND_STEP_OPTION]->val2 / 100;
	if(sc->data[SC_SMOKEPOWDER])
		flee += flee * 20 / 100;
	if(sc->data[SC_GROOMING])
		flee += sc->data[SC_GROOMING]->val2;
	if(sc->data[SC_GATLINGFEVER])
		flee -= sc->data[SC_GATLINGFEVER]->val4;
	if(sc->data[SC_MARSHOFABYSS])
		flee -= sc->data[SC_MARSHOFABYSS]->val2;
	if(sc->data[SC_WATER_BARRIER])
		flee -= sc->data[SC_WATER_BARRIER]->val2;
	if(sc->data[SC_ANGRIFFS_MODUS])
		flee -= sc->data[SC_ANGRIFFS_MODUS]->val3;
	if(sc->data[SC_C_MARKER])
		flee -= 10;
	if(sc->data[SC_SPIDERWEB] || sc->data[SC_WIDEWEB])
		flee -= flee * 50 / 100;
#ifndef RENEWAL
	if(sc->data[SC_BERSERK])
		flee -= flee * 50 / 100;
#endif
	if(sc->data[SC_BLIND] || sc->data[SC_PYREXIA])
		flee -= flee * 25 / 100;
	if(sc->data[SC_FEAR])
		flee -= flee * 20 / 100;
	if(sc->data[SC_PARALYSE])
		flee -= flee * 10 / 100; //10% Flee reduction
	if(sc->data[SC_INFRAREDSCAN])
		flee -= flee * 30 / 100;
	if(sc->data[SC__LAZINESS])
		flee -= flee * sc->data[SC__LAZINESS]->val2 / 100;
	if(sc->data[SC_GLOOMYDAY])
		flee -= flee * sc->data[SC_GLOOMYDAY]->val2 / 100;
	if(sc->data[SC_SATURDAYNIGHTFEVER])
		flee -= flee * (40 + 10 * sc->data[SC_SATURDAYNIGHTFEVER]->val1) / 100;
	if(sc->data[SC_TEARGAS])
		flee -= flee * 50 / 100;
	if(sc->data[SC_ASH])
		flee -= flee * sc->data[SC_ASH]->val4 / 100;

	return (short)cap_value(flee,1,SHRT_MAX);
}

short status_calc_flee2(struct block_list *bl, struct status_change *sc, int flee2)
{
	if(!sc || !sc->count)
		return (short)cap_value(flee2,10,SHRT_MAX);

	if(sc->data[SC_INCFLEE2])
		flee2 += sc->data[SC_INCFLEE2]->val2;
	if(sc->data[SC_WHISTLE])
		flee2 += sc->data[SC_WHISTLE]->val3;
	if(sc->data[SC_HISS])
		flee2 += sc->data[SC_HISS]->val2;
	if(sc->data[SC_DORAM_FLEE2])
		flee2 += sc->data[SC_DORAM_FLEE2]->val2;
	if(sc->data[SC__UNLUCKY])
		flee2 -= sc->data[SC__UNLUCKY]->val2;

	return (short)cap_value(flee2,10,SHRT_MAX);
}

defType status_calc_def(struct block_list *bl, struct status_change *sc, int def, bool viewable)
{
	if(!sc || !sc->count)
		return (defType)cap_value(def,DEFTYPE_MIN,DEFTYPE_MAX);

	if(!viewable) {
#ifdef RENEWAL
		if(sc->data[SC_ASSUMPTIO])
			def *= 2; //only eDEF is doubled
#endif
		if(sc->data[SC_INCDEFRATE])
			def += def * sc->data[SC_INCDEFRATE]->val1 / 100;
		if(sc->data[SC_NEUTRALBARRIER])
			def += def * sc->data[SC_NEUTRALBARRIER]->val2 / 100;
		if(sc->data[SC_FORCEOFVANGUARD])
			def += def * 2 * sc->data[SC_FORCEOFVANGUARD]->val1 / 100;
		if(sc->data[SC_ECHOSONG])
			def += def * sc->data[SC_ECHOSONG]->val4 / 100;
		if(sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
			def -= def * 50 / 100;
		if(sc->data[SC_FREEZE])
			def -= def * 50 / 100;
		if(sc->data[SC_CAMOUFLAGE])
			def -= def * 5 * sc->data[SC_CAMOUFLAGE]->val3 / 100;
		if(sc->data[SC_OVERED_BOOST] && bl->type == BL_PC)
			def -= def * sc->data[SC_OVERED_BOOST]->val4 / 100;
		return (defType)cap_value(def,DEFTYPE_MIN,DEFTYPE_MAX);
	}

#ifndef RENEWAL
	if(sc->data[SC_BERSERK])
		return 0;
#endif
	if(sc->data[SC_BARRIER])
		return 100;
	if(sc->data[SC_KEEPING])
		return 90;
#ifndef RENEWAL //Does not provide 90 DEF in renewal mode
	if(sc->data[SC_STEELBODY])
		return 90;
#endif
	if(sc->data[SC_DEFSET_NUM])
		return sc->data[SC_DEFSET_NUM]->val1;
	if(sc->data[SC_NYANGGRASS] && bl->type == BL_PC)
		return 0;

#ifndef RENEWAL
	if(sc->data[SC_ARMORCHANGE])
		def += sc->data[SC_ARMORCHANGE]->val2;
	if(sc->data[SC_DEFENCE]) //[orn]
		def += sc->data[SC_DEFENCE]->val2;
#endif
	if(sc->data[SC_DRUMBATTLE])
		def += sc->data[SC_DRUMBATTLE]->val3;
	if(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2)
		def += 50;
	if(sc->data[SC_STONEHARDSKIN])
		def += sc->data[SC_STONEHARDSKIN]->val2;
	if(sc->data[SC_SHIELDSPELL_REF] && sc->data[SC_SHIELDSPELL_REF]->val1 == 2)
		def += sc->data[SC_SHIELDSPELL_REF]->val2;
	if(sc->data[SC_PRESTIGE])
		def += sc->data[SC_PRESTIGE]->val2;
	if(sc->data[SC_BANDING] && sc->data[SC_BANDING]->val2 > 1) {
		def += sc->data[SC_BANDING]->val2 * (5 + sc->data[SC_BANDING]->val1)
#ifndef RENEWAL
		/ 10
#endif
		;
	}
	if(sc->data[SC_POWER_OF_GAIA])
		def += 100;
	if(sc->data[SC_SOLID_SKIN_OPTION])
		def *= 2;
	if(sc->data[SC_DEFSET_PER])
		def += def * sc->data[SC_DEFSET_PER]->val1 / 100;
	if(sc->data[SC_ODINS_POWER])
		def -= 20 * sc->data[SC_ODINS_POWER]->val1;
	if(sc->data[SC_ANGRIFFS_MODUS])
		def -= 30 + 20 * sc->data[SC_ANGRIFFS_MODUS]->val1;
	if(sc->data[SC_SKE])
		def -= def * 50 / 100;
	if(sc->data[SC_SIGNUMCRUCIS])
		def -= def * sc->data[SC_SIGNUMCRUCIS]->val2 / 100;
	if(sc->data[SC_CONCENTRATION])
		def -= def * sc->data[SC_CONCENTRATION]->val4 / 100;
	if(sc->data[SC_PROVOKE] && bl->type != BL_PC) //Provoke doesn't alter player eDEF
		def -= def * sc->data[SC_PROVOKE]->val4 / 100;
	if(sc->data[SC_STRIPSHIELD] && bl->type != BL_PC)
		def -= def * sc->data[SC_STRIPSHIELD]->val2 / 100;
	if(sc->data[SC_FLING])
		def -= def * sc->data[SC_FLING]->val2 / 100;
	if(sc->data[SC_FREEZING])
		def -= def * sc->data[SC_FREEZING]->val2 / 100;
	if(sc->data[SC_MARSHOFABYSS])
		def -= def * sc->data[SC_MARSHOFABYSS]->val2 / 100;
	if(sc->data[SC_ANALYZE])
		def -= def * 14 * sc->data[SC_ANALYZE]->val1 / 100;
	if(sc->data[SC_SATURDAYNIGHTFEVER])
		def -= def * (10 + 10 * sc->data[SC_SATURDAYNIGHTFEVER]->val1) / 100;
	if(sc->data[SC_EARTHDRIVE])
		def -= def * 25 / 100;
	if(sc->data[SC_ROCK_CRUSHER])
		def -= def * sc->data[SC_ROCK_CRUSHER]->val2 / 100;
	if(sc->data[SC_PARALYSIS])
		def -= def * sc->data[SC_PARALYSIS]->val2 / 100;
	if(sc->data[SC_ASH])
		def -= def * sc->data[SC_ASH]->val3 / 100;
	if(sc->data[SC_OVERED_BOOST] && bl->type == BL_HOM)
		def -= def * sc->data[SC_OVERED_BOOST]->val4 / 100;
	if(sc->data[SC_NYANGGRASS] && bl->type != BL_PC)
		def -= def * 50 / 100;

	return (defType)cap_value(def,DEFTYPE_MIN,DEFTYPE_MAX);
}

short status_calc_def2(struct block_list *bl, struct status_change *sc, int def2, bool viewable)
{
	if(!sc || !sc->count) {
#ifdef RENEWAL
		return (short)cap_value(def2,SHRT_MIN,SHRT_MAX);
#else
		return (short)cap_value(def2,1,SHRT_MAX);
#endif
	}

	if(!viewable) {
		if(sc->data[SC_GT_REVITALIZE])
			def2 += sc->data[SC_GT_REVITALIZE]->val2;
		if(sc->data[SC_CAMOUFLAGE])
			def2 -= def2 * 5 * sc->data[SC_CAMOUFLAGE]->val3 / 100;
#ifdef RENEWAL
		return (short)cap_value(def2,SHRT_MIN,SHRT_MAX);
#else
		return (short)cap_value(def2,1,SHRT_MAX);
#endif
	}

#ifndef RENEWAL
	if(sc->data[SC_BERSERK])
		return 0;
#endif
	if(sc->data[SC_ETERNALCHAOS])
		return 0;
	if(sc->data[SC_DEFSET_NUM])
		return sc->data[SC_DEFSET_NUM]->val1;

	if(sc->data[SC_SUN_COMFORT])
		def2 += sc->data[SC_SUN_COMFORT]->val2;
#ifdef RENEWAL
	if(sc->data[SC_SKA])
		def2 += 80;
#endif
	if(sc->data[SC_ANGELUS])
#ifdef RENEWAL //Renewal: Only the VIT stat bonus is boosted by Angelus
		def2 += status_get_vit(bl) / 2 * sc->data[SC_ANGELUS]->val2 / 100;
#else
		def2 += def2 * sc->data[SC_ANGELUS]->val2 / 100;
	if(sc->data[SC_CONCENTRATION])
		def2 -= def2 * sc->data[SC_CONCENTRATION]->val4 / 100;
#endif
	if(sc->data[SC_POISON])
		def2 -= def2 * 25 / 100;
	if(sc->data[SC_DPOISON])
		def2 -= def2 * 25 / 100;
	if(sc->data[SC_SKE])
		def2 -= def2 * 50 / 100;
	if(sc->data[SC_PROVOKE])
		def2 -= def2 * sc->data[SC_PROVOKE]->val4 / 100;
	if(sc->data[SC_JOINTBEAT])
		def2 -= def2 * (sc->data[SC_JOINTBEAT]->val2&BREAK_SHOULDER ? 50 : 0) / 100 +
				def2 * (sc->data[SC_JOINTBEAT]->val2&BREAK_WAIST ? 25 : 0) / 100;
	if(sc->data[SC_FLING])
		def2 -= def2 * sc->data[SC_FLING]->val3 / 100;
	if(sc->data[SC_MARSHOFABYSS])
		def2 -= def2 * sc->data[SC_MARSHOFABYSS]->val2 / 100;
	if(sc->data[SC_ANALYZE])
		def2 -= def2 * 14 * sc->data[SC_ANALYZE]->val1 / 100;
	if(sc->data[SC_SATURDAYNIGHTFEVER])
		def2 -= def2 * (10 + 10 * sc->data[SC_SATURDAYNIGHTFEVER]->val1) / 100;
	if(sc->data[SC_ASH])
		def2 -= def2 * sc->data[SC_ASH]->val3 / 100;
	if(sc->data[SC_EQC])
		def2 -= def2 * sc->data[SC_EQC]->val2 / 100;

#ifdef RENEWAL
	return (short)cap_value(def2,SHRT_MIN,SHRT_MAX);
#else
	return (short)cap_value(def2,1,SHRT_MAX);
#endif
}

defType status_calc_mdef(struct block_list *bl, struct status_change *sc, int mdef, bool viewable)
{
	if(!sc || !sc->count)
		return (defType)cap_value(mdef,DEFTYPE_MIN,DEFTYPE_MAX);

	if(!viewable) {
#ifdef RENEWAL
		if(sc->data[SC_ASSUMPTIO])
			mdef *= 2; //Only eMDEF is doubled
#endif
		if(sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
			mdef += mdef * 25 / 100;
		if(sc->data[SC_FREEZE])
			mdef += mdef * 25 / 100;
		if(sc->data[SC_NEUTRALBARRIER])
			mdef += mdef * sc->data[SC_NEUTRALBARRIER]->val2 / 100;
		return (defType)cap_value(mdef,DEFTYPE_MIN,DEFTYPE_MAX);
	}

#ifndef RENEWAL
	if(sc->data[SC_BERSERK])
		return 0;
#endif
	if(sc->data[SC_BARRIER])
		return 100;
#ifndef RENEWAL //No longer provides 90 MDEF in renewal mode
	if(sc->data[SC_STEELBODY])
		return 90;
#endif
	if(sc->data[SC_MDEFSET_NUM])
		return sc->data[SC_MDEFSET_NUM]->val1;
	if(sc->data[SC_NYANGGRASS] && bl->type == BL_PC)
		return 0;

#ifndef RENEWAL
	if(sc->data[SC_ARMORCHANGE])
		mdef += sc->data[SC_ARMORCHANGE]->val3;
#endif
	if(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 3)
		mdef += 50;
	if(sc->data[SC_ENDURE] && !sc->data[SC_ENDURE]->val3)
		mdef += (!sc->data[SC_ENDURE]->val4 ? sc->data[SC_ENDURE]->val1 : 1); //Eddga card only grants 1 MDEF
	if(sc->data[SC_STONEHARDSKIN])
		mdef += sc->data[SC_STONEHARDSKIN]->val2;
	if(sc->data[SC_SYMPHONYOFLOVER])
		mdef += mdef * sc->data[SC_SYMPHONYOFLOVER]->val4 / 100;
	if(sc->data[SC_MDEFSET_PER])
		mdef += mdef * sc->data[SC_MDEFSET_PER]->val1 / 100;
	if(sc->data[SC_GT_CHANGE]) {
		mdef -= sc->data[SC_GT_CHANGE]->val4;
		if(mdef < 0)
			return 0;
	}
	if(sc->data[SC_ODINS_POWER])
		mdef -= 20 * sc->data[SC_ODINS_POWER]->val1;
	if(sc->data[SC_BURNING])
		mdef -= mdef * 25 / 100;
	if(sc->data[SC_ANALYZE])
		mdef -= mdef * 14 * sc->data[SC_ANALYZE]->val1 / 100;
	if(sc->data[SC_PARALYSIS])
		mdef -= mdef * sc->data[SC_PARALYSIS]->val2 / 100;
	if(sc->data[SC_NYANGGRASS] && bl->type != BL_PC)
		mdef -= mdef * 50 / 100;

	return (defType)cap_value(mdef,DEFTYPE_MIN,DEFTYPE_MAX);
}

short status_calc_mdef2(struct block_list *bl, struct status_change *sc, int mdef2, bool viewable)
{
	if(!sc || !sc->count) {
#ifdef RENEWAL
		return (short)cap_value(mdef2,SHRT_MIN,SHRT_MAX);
#else
		return (short)cap_value(mdef2,1,SHRT_MAX);
#endif
	}

	if(!viewable) {
		if(sc->data[SC_MINDBREAKER])
			mdef2 -= mdef2 * sc->data[SC_MINDBREAKER]->val3 / 100;
#ifdef RENEWAL
		return (short)cap_value(mdef2,SHRT_MIN,SHRT_MAX);
#else
		return (short)cap_value(mdef2,1,SHRT_MAX);
#endif
	}

#ifndef RENEWAL
	if(sc->data[SC_BERSERK])
		return 0;
#endif
	if(sc->data[SC_SKA])
		return 90;
	if(sc->data[SC_MDEFSET_NUM])
		return sc->data[SC_MDEFSET_NUM]->val1;

	if(sc->data[SC_ANALYZE])
		mdef2 -= mdef2 * 14 * sc->data[SC_ANALYZE]->val1 / 100;

#ifdef RENEWAL
	return (short)cap_value(mdef2,SHRT_MIN,SHRT_MAX);
#else
	return (short)cap_value(mdef2,1,SHRT_MAX);
#endif
}

unsigned short status_calc_speed(struct block_list *bl, struct status_change *sc, int speed)
{
	TBL_PC *sd = BL_CAST(BL_PC, bl);
	int speed_rate;

	if( !sc || (sd && sd->state.permanent_speed) )
		return (unsigned short)cap_value(speed,MIN_WALK_SPEED,MAX_WALK_SPEED);

	if( sd && pc_ismadogear(sd) ) { //Recieve fixed speed value
		int val = 0;

		if( pc_checkskill(sd,NC_MADOLICENCE) < 5 )
			val += 50 - 10 * pc_checkskill(sd,NC_MADOLICENCE);
		else
			val -= 25;
		if( sc->data[SC_ACCELERATION] )
			val -= 25;
		speed += speed * val / 100;
		return (unsigned short)cap_value(speed,MIN_WALK_SPEED,MAX_WALK_SPEED);
	}

	if( sd && sd->ud.skilltimer != INVALID_TIMER && (pc_checkskill(sd,SA_FREECAST) > 0 || sd->ud.skill_id == LG_EXEEDBREAK) ) {
		if( sd->ud.skill_id == LG_EXEEDBREAK )
			speed_rate = 160 - 10 * sd->ud.skill_lv; //-50% at skill_lv 1 -> -10% at skill_lv 5
		else
			speed_rate = 175 - 5 * pc_checkskill(sd,SA_FREECAST);
	} else {
		speed_rate = 100;

		//GetMoveHasteValue2()
		{
			int val = 0;

			if( sc->data[SC_FUSION] )
				val = 25;
			if( sc->data[SC_ALL_RIDING] )
				val = battle_config.rental_mount_speed_boost;
			else if( sd ) {
				if( pc_isriding(sd) || pc_isridingdragon(sd) )
					val = 25; //Same bonus
				else if( pc_isridingwug(sd) )
					val = 15 + 5 * pc_checkskill(sd,RA_WUGRIDER);
			}

			speed_rate -= val;
		}

		//GetMoveSlowValue()
		{
			int val = 0;

			if( sd && sc->data[SC_HIDING] && pc_checkskill(sd,RG_TUNNELDRIVE) > 0 )
				val = 120 - 6 * pc_checkskill(sd,RG_TUNNELDRIVE);
			else if( sd && sc->data[SC_CHASEWALK] && sc->data[SC_CHASEWALK]->val3 < 0 )
				val = sc->data[SC_CHASEWALK]->val3;
			else {
				//Longing for Freedom cancels song/dance penalty
				if( sc->data[SC_LONGING] )
					val = max( val, 50 - 10 * sc->data[SC_LONGING]->val1 );
				else if( sc->data[SC_DANCING] )
					val = max( val, 500 - (40 + 10 * (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_BARDDANCER)) *
						(sd ? pc_checkskill(sd, (sd->status.sex ? BA_MUSICALLESSON : DC_DANCINGLESSON)) : 10) );
				if( sc->data[SC_DECREASEAGI] )
					val = max( val, 25 );
				if( sc->data[SC_QUAGMIRE] )
					val = max( val, sc->data[SC_QUAGMIRE]->val2 );
				if( sc->data[SC_DONTFORGETME] )
					val = max( val, sc->data[SC_DONTFORGETME]->val3 );
				if( sc->data[SC_CURSE] )
					val = max( val, 300 );
				if( sc->data[SC_CHASEWALK] )
					val = max( val, sc->data[SC_CHASEWALK]->val3 );
				if( sc->data[SC_WEDDING] )
					val = max( val, 100 );
				if( sc->data[SC_JOINTBEAT] && sc->data[SC_JOINTBEAT]->val2&(BREAK_ANKLE|BREAK_KNEE) )
					val = max( val, (sc->data[SC_JOINTBEAT]->val2&BREAK_ANKLE ? 50 : 0) + (sc->data[SC_JOINTBEAT]->val2&BREAK_KNEE ? 30 : 0) );
				if( sc->data[SC_CLOAKING] && (sc->data[SC_CLOAKING]->val4&1) == 0 )
					val = max( val, (sc->data[SC_CLOAKING]->val1 < 3 ? 300 : 30 - 3 * sc->data[SC_CLOAKING]->val1) );
				if( sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_ENEMY )
					val = max( val, 75 );
				if( sc->data[SC_SLOWDOWN] ) //Slow Potion
					val = max( val, sc->data[SC_SLOWDOWN]->val1 );
				if( sc->data[SC_GATLINGFEVER] )
					val = max( val, 100 );
				if( sc->data[SC_SUITON] )
					val = max( val, sc->data[SC_SUITON]->val3 );
				if( sc->data[SC_SWOO] )
					val = max( val, 300 );
				if( sc->data[SC_SKA] )
					val = max( val, 25 );
				if( sc->data[SC_ADORAMUS] )
					val = max( val, 25 );
				if( sc->data[SC_HALLUCINATIONWALK_POSTDELAY] )
					val = max( val, 50 );
				if( sc->data[SC_GLOOMYDAY] && sc->data[SC_GLOOMYDAY]->val1 == 2 )
					val = max( val, 50 );
				if( sc->data[SC_FREEZING] )
					val = max( val, 70 );
				if( sc->data[SC_MARSHOFABYSS] )
					val = max( val, sc->data[SC_MARSHOFABYSS]->val3 );
				if( sc->data[SC_CAMOUFLAGE] && sc->data[SC_CAMOUFLAGE]->val1 > 2 )
					val = max( val, 25 * (5 - sc->data[SC_CAMOUFLAGE]->val1) );
				if( sc->data[SC_STEALTHFIELD] )
					val = max( val, 20 );
				if( sc->data[SC__LAZINESS] )
					val = max( val, 25 );
				if( sc->data[SC_BANDING_DEFENCE] )
					val = max( val, 90 );
				if( sc->data[SC_ROCK_CRUSHER_ATK] )
					val = max( val, sc->data[SC_ROCK_CRUSHER_ATK]->val2 );
				if( sc->data[SC_POWER_OF_GAIA] )
					val = max( val, sc->data[SC_POWER_OF_GAIA]->val2 );
				if( sc->data[SC_MELON_BOMB] )
					val = max( val, sc->data[SC_MELON_BOMB]->val2 );
				if( sc->data[SC_REBOUND] )
					val = max( val, 25 );
				if( sc->data[SC_B_TRAP] )
					val = max( val, 90 );
				if( sc->data[SC_CATNIPPOWDER] )
					val = max( val, sc->data[SC_CATNIPPOWDER]->val3 );
				if( sd && sd->bonus.speed_rate + sd->bonus.speed_add_rate > 0 ) //Permanent item-based speedup
					val = max( val, sd->bonus.speed_rate + sd->bonus.speed_add_rate );
			}

			speed_rate += val;
		}

		if( speed_rate > 150 && sc->data[SC_MARSHOFABYSS] )
			speed_rate = 150;

		//GetMoveHasteValue1()
		{
			int val = 0;

			if( sc->data[SC_SPEEDUP1] )
				val = max( val, sc->data[SC_SPEEDUP1]->val1 );
			if( sc->data[SC_INCREASEAGI] )
				val = max( val, 25 );
			if( sc->data[SC_WINDWALK] )
				val = max( val, 2 * sc->data[SC_WINDWALK]->val1 );
			if( sc->data[SC_CARTBOOST] )
				val = max( val, 20 );
			if( sd && (sd->class_&MAPID_UPPERMASK) == MAPID_ASSASSIN && pc_checkskill(sd,TF_MISS) > 0 )
				val = max( val, 1 * pc_checkskill(sd,TF_MISS) );
			if( sc->data[SC_CLOAKING] && (sc->data[SC_CLOAKING]->val4&1) == 1 )
				val = max( val, (sc->data[SC_CLOAKING]->val1 >= 10 ? 25 : 3 * sc->data[SC_CLOAKING]->val1 - 3) );
			if( sc->data[SC_BERSERK] )
				val = max( val, 25 );
			if( sc->data[SC_RUN] )
				val = max( val, 55 );
			if( sc->data[SC_AVOID] )
				val = max( val, 10 * sc->data[SC_AVOID]->val1 );
			if( sc->data[SC_INVINCIBLE] && !sc->data[SC_INVINCIBLEOFF] )
				val = max( val, 75 );
			if( sc->data[SC_CLOAKINGEXCEED] )
				val = max( val, sc->data[SC_CLOAKINGEXCEED]->val3 );
			if( sc->data[SC_GN_CARTBOOST] )
				val = max( val, sc->data[SC_GN_CARTBOOST]->val2 );
			if( sc->data[SC_SWINGDANCE] )
				val = max( val, 5 * sc->data[SC_SWINGDANCE]->val1 );
			if( sc->data[SC_WIND_STEP_OPTION] )
				val = max( val, sc->data[SC_WIND_STEP_OPTION]->val2 );
			if( sc->data[SC_ARCLOUSEDASH] )
				val = max( val, sc->data[SC_ARCLOUSEDASH]->val3 );
			if( sc->data[SC_DORAM_WALKSPEED] )
				val = max( val, 60 );
			if( sc->data[SC_SPEEDUP0] )
				val = max( val, sc->data[SC_SPEEDUP0]->val1 );
			if( sd && sd->bonus.speed_rate + sd->bonus.speed_add_rate < 0 )
				val = max( val, -(sd->bonus.speed_rate + sd->bonus.speed_add_rate) );

			speed_rate -= val;
		}

		if( speed_rate < 40 )
			speed_rate = 40;
	}

	//GetSpeed()
	{
		if( sd && pc_iscarton(sd) )
			speed += speed * (50 - 5 * pc_checkskill(sd,MC_PUSHCART)) / 100;
		if( sc->data[SC_PARALYSE] )
			speed += speed * 50 / 100;
		if( speed_rate != 100 )
			speed = speed * speed_rate / 100;
		if( sc->data[SC_STEELBODY] )
			speed = 200;
		if( sc->data[SC_DEFENDER] )
			speed = max( speed, 200 );
		if( sc->data[SC_WALKSPEED] && sc->data[SC_WALKSPEED]->val1 > 0 ) //ChangeSpeed
			speed = speed * 100 / sc->data[SC_WALKSPEED]->val1;
		if( sc->data[SC_FULL_THROTTLE] )
			speed = 60;
	}

	return (unsigned short)cap_value(speed,MIN_WALK_SPEED,MAX_WALK_SPEED);
}

#ifdef RENEWAL_ASPD
/**
 * Renewal attack speed modifiers after base calculation
 * NOTE: This function only affects RENEWAL players
 * @param bl: Object to change aspd (PC)
 * @param sc: Object's status change information
 * @param fixed: true - fixed value [malufett]
 *               false - percentage value
 * @return modified aspd
 */
short status_calc_aspd(struct block_list *bl, struct status_change *sc, bool fixed)
{
	int bonus = 0;

	if(!sc || !sc->count)
		return 0;
	if(fixed) {
		enum sc_type sc_val;

		if(!sc->data[SC_QUAGMIRE]) {
			struct status_change_entry *sce;

			if((sc->data[SC_TWOHANDQUICKEN] ||
				sc->data[SC_ONEHAND] ||
				sc->data[SC_MERC_QUICKEN] ||
				sc->data[SC_SPEARQUICKEN]) && bonus < 7)
				bonus = 7;
			else if(((sce = sc->data[SC_ADRENALINE]) || (sce = sc->data[SC_ADRENALINE2])) && bonus < (sce->val2 ? 7 : 6))
				bonus = (sce->val2 ? 7 : 6);
			else if(sc->data[SC_FLEET] && bonus < 5)
				bonus = 5;
		}
		if(sc->data[SC_MADNESSCANCEL] && bonus < 20)
			bonus = 20;
		else if(sc->data[SC_BERSERK] && bonus < 15)
			bonus = 15;
		if(sc->data[sc_val = SC_ASPDPOTION3] ||
			sc->data[sc_val = SC_ASPDPOTION2] ||
			sc->data[sc_val = SC_ASPDPOTION1] ||
			sc->data[sc_val = SC_ASPDPOTION0])
			bonus += sc->data[sc_val]->val1;
		if(sc->data[SC_ATTHASTE_CASH])
			bonus += sc->data[SC_ATTHASTE_CASH]->val1;
	} else {
		if(sc->data[SC_ASSNCROS] && bonus < sc->data[SC_ASSNCROS]->val2 / 10) {
			if(bl->type != BL_PC)
				bonus = sc->data[SC_ASSNCROS]->val2 / 10;
			else {
				switch(((TBL_PC *)bl)->status.weapon) {
					case W_BOW:	case W_REVOLVER:
					case W_RIFLE:	case W_GATLING:
					case W_SHOTGUN:	case W_GRENADE:
						break;
					default:
						bonus = sc->data[SC_ASSNCROS]->val2 / 10;
						break;
				}
			}
		}
		if(sc->data[SC_SWINGDANCE] && bonus < sc->data[SC_SWINGDANCE]->val3)
			bonus = sc->data[SC_SWINGDANCE]->val3;
		if(sc->data[SC_DANCEWITHWUG] && bonus < sc->data[SC_DANCEWITHWUG]->val3)
			bonus = sc->data[SC_DANCEWITHWUG]->val3;
		if(sc->data[SC_GT_CHANGE])
			bonus += sc->data[SC_GT_CHANGE]->val3;
		if(sc->data[SC_BOOST500])
			bonus += sc->data[SC_BOOST500]->val1;
		if(sc->data[SC_EXTRACT_SALAMINE_JUICE])
			bonus += sc->data[SC_EXTRACT_SALAMINE_JUICE]->val1;
		if(sc->data[SC_GOLDENE_FERSE])
			bonus += sc->data[SC_GOLDENE_FERSE]->val3;
		if(sc->data[SC_INCASPDRATE])
			bonus += sc->data[SC_INCASPDRATE]->val1;
		if(sc->data[SC_GATLINGFEVER])
			bonus += sc->data[SC_GATLINGFEVER]->val1;
		if(sc->data[SC_STAR_COMFORT])
			bonus += 3 * sc->data[SC_STAR_COMFORT]->val1;
		if(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 2)
			bonus += 10;
		if(sc->data[SC_DONTFORGETME])
			bonus -= sc->data[SC_DONTFORGETME]->val2 / 10;
		if(sc->data[SC_LONGING])
			bonus -= sc->data[SC_LONGING]->val2 / 10;
		if(sc->data[SC_STEELBODY])
			bonus -= 25;
		if(sc->data[SC_SKA])
			bonus -= 25;
		if(sc->data[SC_DEFENDER])
			bonus -= sc->data[SC_DEFENDER]->val4 / 10;
		if(sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_ENEMY)
			bonus -= 75;
		if(sc->data[SC_GRAVITATION] && sc->data[SC_GRAVITATION]->val3 != BCT_SELF)
			bonus -= sc->data[SC_GRAVITATION]->val2 / 10;
		if(sc->data[SC_JOINTBEAT]) { //Needs more info
			if( sc->data[SC_JOINTBEAT]->val2&BREAK_WRIST )
				bonus -= 25;
			if( sc->data[SC_JOINTBEAT]->val2&BREAK_KNEE )
				bonus -= 10;
		}
		if(sc->data[SC_FREEZING])
			bonus -= 30;
		if(sc->data[SC_HALLUCINATIONWALK_POSTDELAY])
			bonus -= 50;
		if(sc->data[SC_PARALYSE])
			bonus -= 10;
		if(sc->data[SC__BODYPAINT])
			bonus -= 5 * sc->data[SC__BODYPAINT]->val1;
		if(sc->data[SC__INVISIBILITY])
			bonus -= sc->data[SC__INVISIBILITY]->val3;
		if(sc->data[SC__GROOMY])
			bonus -= sc->data[SC__GROOMY]->val3;
		if(sc->data[SC_GLOOMYDAY])
			bonus -= sc->data[SC_GLOOMYDAY]->val3;
		if(sc->data[SC_EARTHDRIVE])
			bonus -= 25;
		if(sc->data[SC_MELON_BOMB])
			bonus -= sc->data[SC_MELON_BOMB]->val3;
		if(sc->data[SC_PAIN_KILLER])
			bonus -= sc->data[SC_PAIN_KILLER]->val2;
	}

	return bonus;
}
#endif

/*==========================================
* Modifies ASPD by a number, rather than a percentage (10 = 1 ASPD)
* A subtraction reduces the delay, meaning an increase in ASPD
* NOTE: This comes after the percentage changes
* @param bl: Object to change aspd (PC|HOM|MERC|MOB|ELEM)
* @param sc: Object's status change information
* @param aspd: Object's current ASPD
* @return modified aspd
*------------------------------------------*/
short status_calc_fix_aspd(struct block_list *bl, struct status_change *sc, int aspd)
{
	if (!sc || !sc->count)
		return cap_value(aspd, 0, 2000);
	if (sc->data[SC_OVERED_BOOST])
		return cap_value(2000 - 10 * sc->data[SC_OVERED_BOOST]->val3, 0, 2000);

	if ((sc->data[SC_GUST_OPTION] || sc->data[SC_BLAST_OPTION] || sc->data[SC_WILD_STORM_OPTION]))
		aspd -= 50; //+5 ASPD
	if (sc->data[SC_FIGHTINGSPIRIT])
		aspd -= sc->data[SC_FIGHTINGSPIRIT]->val2;
	if (sc->data[SC_HEAT_BARREL])
		aspd -= 10 * sc->data[SC_HEAT_BARREL]->val1;
	if (sc->data[SC_MTF_ASPD])
		aspd -= sc->data[SC_MTF_ASPD]->val1;
	if (sc->data[SC_MTF_ASPD2])
		aspd -= sc->data[SC_MTF_ASPD2]->val1;

    return cap_value(aspd, 0, 2000); //Will be recap for proper bl anyway
}

/*==========================================
* Calculates an object's ASPD modifier (alters the base amotion value).
* NOTE: The scale of aspd_rate is 1000 = 100%.
* Note2: This only affects Homunculus, Mercenaries, and Pre-renewal Players
* @param bl: Object to change aspd (PC|HOM|MERC|MOB|ELEM)
* @param sc: Object's status change information
* @param aspd_rate: Object's current ASPD
* @return modified aspd_rate
*------------------------------------------*/
short status_calc_aspd_rate(struct block_list *bl, struct status_change *sc, int aspd_rate)
{
	int i;

	if(!sc || !sc->count)
		return (short)cap_value(aspd_rate,0,SHRT_MAX);

	if(!sc->data[SC_QUAGMIRE]) {
		int max = 0;

		if(sc->data[SC_STAR_COMFORT])
			max = sc->data[SC_STAR_COMFORT]->val2;
		if(sc->data[SC_TWOHANDQUICKEN] &&
			max < sc->data[SC_TWOHANDQUICKEN]->val2)
			max = sc->data[SC_TWOHANDQUICKEN]->val2;
		if(sc->data[SC_ONEHAND] &&
			max < sc->data[SC_ONEHAND]->val2)
			max = sc->data[SC_ONEHAND]->val2;
		if(sc->data[SC_MERC_QUICKEN] &&
			max < sc->data[SC_MERC_QUICKEN]->val2)
			max = sc->data[SC_MERC_QUICKEN]->val2;
		if(sc->data[SC_ADRENALINE2] &&
			max < sc->data[SC_ADRENALINE2]->val3)
			max = sc->data[SC_ADRENALINE2]->val3;
		if(sc->data[SC_ADRENALINE] &&
			max < sc->data[SC_ADRENALINE]->val3)
			max = sc->data[SC_ADRENALINE]->val3;
		if(sc->data[SC_SPEARQUICKEN] &&
			max < sc->data[SC_SPEARQUICKEN]->val2)
			max = sc->data[SC_SPEARQUICKEN]->val2;
		if(sc->data[SC_GATLINGFEVER] &&
			max < sc->data[SC_GATLINGFEVER]->val2)
			max = sc->data[SC_GATLINGFEVER]->val2;
		if(sc->data[SC_FLEET] &&
			max < sc->data[SC_FLEET]->val2)
			max = sc->data[SC_FLEET]->val2;
		if(sc->data[SC_ASSNCROS] &&
			max < sc->data[SC_ASSNCROS]->val2) {
			if(bl->type != BL_PC)
				max = sc->data[SC_ASSNCROS]->val2;
			else {
				switch(((TBL_PC *)bl)->status.weapon) {
					case W_BOW:	case W_REVOLVER:
					case W_RIFLE:	case W_GATLING:
					case W_SHOTGUN:	case W_GRENADE:
						break;
					default:
						max = sc->data[SC_ASSNCROS]->val2;
						break;
				}
			}
		}
		aspd_rate -= max;
		//These stack with the rest of bonuses
		max = 0;
		if(sc->data[SC_BERSERK] && max < 300)
			max = 300;
		if(sc->data[SC_MADNESSCANCEL] && max < 200)
			max = 200;
		if(sc->data[SC_SWINGDANCE] && max < 10 * sc->data[SC_SWINGDANCE]->val3)
			max = 10 * sc->data[SC_SWINGDANCE]->val3;
		if(sc->data[SC_DANCEWITHWUG] && max < 10 * sc->data[SC_DANCEWITHWUG]->val3)
			max = 10 * sc->data[SC_DANCEWITHWUG]->val3;
		aspd_rate -= max;
	}
	if(sc->data[i = SC_ASPDPOTION3] ||
		sc->data[i = SC_ASPDPOTION2] ||
		sc->data[i = SC_ASPDPOTION1] ||
		sc->data[i = SC_ASPDPOTION0])
		aspd_rate -= sc->data[i]->val2;
	if(sc->data[SC_ATTHASTE_CASH])
		aspd_rate -= sc->data[SC_ATTHASTE_CASH]->val2;
	if(sc->data[SC_GT_CHANGE])
		aspd_rate -= 10 * sc->data[SC_GT_CHANGE]->val3;
	if(sc->data[SC_BOOST500])
		aspd_rate -= 10 * sc->data[SC_BOOST500]->val1;
	if(sc->data[SC_EXTRACT_SALAMINE_JUICE])
		aspd_rate -= 10 * sc->data[SC_EXTRACT_SALAMINE_JUICE]->val1;
	if(sc->data[SC_INCASPDRATE])
		aspd_rate -= 10 * sc->data[SC_INCASPDRATE]->val1;
	if(sc->data[SC_GOLDENE_FERSE])
		aspd_rate -= 10 * sc->data[SC_GOLDENE_FERSE]->val3;
	if(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 2)
		aspd_rate -= 100;
	if(sc->data[SC_DONTFORGETME])
		aspd_rate += sc->data[SC_DONTFORGETME]->val2;
	if(sc->data[SC_LONGING])
		aspd_rate += sc->data[SC_LONGING]->val2;
	if(sc->data[SC_STEELBODY])
		aspd_rate += 250;
	if(sc->data[SC_SKA])
		aspd_rate += 250;
	if(sc->data[SC_DEFENDER])
		aspd_rate += sc->data[SC_DEFENDER]->val4;
	if(sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_ENEMY)
		aspd_rate += 750;
	if(sc->data[SC_GRAVITATION] && sc->data[SC_GRAVITATION]->val3 != BCT_SELF)
		aspd_rate += sc->data[SC_GRAVITATION]->val2;
	if(sc->data[SC_JOINTBEAT]) {
		if(sc->data[SC_JOINTBEAT]->val2&BREAK_WRIST)
			aspd_rate += 250;
		if(sc->data[SC_JOINTBEAT]->val2&BREAK_KNEE)
			aspd_rate += 100;
	}
	if(sc->data[SC_FREEZING])
		aspd_rate += 300;
	if(sc->data[SC_HALLUCINATIONWALK_POSTDELAY])
		aspd_rate += 500;
	if(sc->data[SC_PARALYSE])
		aspd_rate += 100;
	if(sc->data[SC__BODYPAINT])
		aspd_rate += 50 * sc->data[SC__BODYPAINT]->val1;
	if(sc->data[SC__INVISIBILITY])
		aspd_rate += 10 * sc->data[SC__INVISIBILITY]->val3;
	if(sc->data[SC__GROOMY])
		aspd_rate += 10 * sc->data[SC__GROOMY]->val3;
	if(sc->data[SC_GLOOMYDAY])
		aspd_rate += 10 * sc->data[SC_GLOOMYDAY]->val3;
	if(sc->data[SC_EARTHDRIVE])
		aspd_rate += 250;
	if(sc->data[SC_MELON_BOMB])
		aspd_rate += 10 * sc->data[SC_MELON_BOMB]->val3;
	if(sc->data[SC_PAIN_KILLER])
		aspd_rate += 10 * sc->data[SC_PAIN_KILLER]->val2;

	return (short)cap_value(aspd_rate,0,SHRT_MAX);
}

/**
 * Modifies the damage delay time based on status changes
 * The lower your delay, the quicker you can act after taking damage
 * @param bl: Object to change aspd [PC|MOB|HOM|MER|ELEM]
 * @param sc: Object's status change information
 * @param dmotion: Object's current damage delay
 * @return modified delay rate
 */
unsigned short status_calc_dmotion(struct block_list *bl, struct status_change *sc, int dmotion)
{
	if(bl->type == BL_MOB && status_get_class_(bl) == CLASS_BOSS)
		return 0; //It has been confirmed on official servers that mvp mobs have no dmotion even without endure
	if(bl->type == BL_PC && ((TBL_PC *)bl)->special_state.no_walkdelay)
		return 0;
	if(!sc || !sc->count || map_flag_gvg2(bl->m) || map[bl->m].flag.battleground)
		return (unsigned short)cap_value(dmotion,0,USHRT_MAX);
	if(sc->data[SC_ENDURE] || sc->data[SC_RUN] || sc->data[SC_WUGDASH])
		return 0;

	return (unsigned short)cap_value(dmotion,0,USHRT_MAX);
}

/**
 * Calculates a MaxHP based on status changes
 * Values can either be percentages or fixed, based on how equations are formulated
 * @param bl: Object's block_list data
 * @param maxhp: Object's current MaxHP
 * @return modified maxhp
 */
unsigned int status_calc_maxhp(struct block_list *bl, uint64 maxhp)
{
	int rate = 100;

	maxhp += status_get_hpbonus(bl,STATUS_BONUS_FIX);

	if((rate += status_get_hpbonus(bl,STATUS_BONUS_RATE)) != 100)
		maxhp = maxhp * rate / 100;

	return (unsigned int)cap_value(maxhp,1,UINT_MAX);
}

/**
 * Calculates a MaxSP based on status changes
 * Values can either be percentages or fixed, bas ed on how equations are formulated
 * @param bl: Object's block_list data
 * @param maxsp: Object's current MaxSP
 * @return modified maxsp
 */
unsigned int status_calc_maxsp(struct block_list *bl, uint64 maxsp)
{
	int rate = 100;

	maxsp += status_get_spbonus(bl,STATUS_BONUS_FIX);

	if((rate += status_get_spbonus(bl,STATUS_BONUS_RATE)) != 100)
		maxsp = maxsp * rate / 100;

	return (unsigned int)cap_value(maxsp,1,UINT_MAX);
}

/**
 * Changes a player's element based on status changes
 * @param bl: Object to change aspd [PC|MOB|HOM|MER|ELEM]
 * @param sc: Object's status change information
 * @param element: Object's current element
 * @return new element
 */
unsigned char status_calc_element(struct block_list *bl, struct status_change *sc, int element)
{
	if(!sc || !sc->count)
		return (unsigned char)cap_value(element,0,UCHAR_MAX);

	if(sc->data[SC_FREEZE])
		return ELE_WATER;
	if(sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
		return ELE_EARTH;
	if(sc->data[SC_BENEDICTIO])
		return ELE_HOLY;
	if(sc->data[SC_CHANGEUNDEAD])
		return ELE_UNDEAD;
	if(sc->data[SC_ELEMENTALCHANGE])
		return sc->data[SC_ELEMENTALCHANGE]->val2;
	if(sc->data[SC_SHAPESHIFT])
		return sc->data[SC_SHAPESHIFT]->val2;

	return (unsigned char)cap_value(element,0,UCHAR_MAX);
}

/**
 * Changes a player's element level based on status changes
 * @param bl: Object to change aspd (PC|HOM|MERC|MOB|ELEM)
 * @param sc: Object's status change information
 * @param lv: Object's current element level
 * @return new element level
 */
unsigned char status_calc_element_lv(struct block_list *bl, struct status_change *sc, int lv)
{
	if(!sc || !sc->count)
		return cap_value(lv,1,4);

	if(sc->data[SC_FREEZE])
		return 1;
	if(sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
		return 1;
	if(sc->data[SC_BENEDICTIO])
		return 1;
	if(sc->data[SC_CHANGEUNDEAD])
		return 1;
	if(sc->data[SC_ELEMENTALCHANGE])
		return sc->data[SC_ELEMENTALCHANGE]->val1;
	if(sc->data[SC_SHAPESHIFT])
		return 1;

	return (unsigned char)cap_value(lv,1,4);
}

/**
 * Changes a player's attack element based on status changes
 * @param bl: Object to change aspd (PC|HOM|MERC|MOB|ELEM)
 * @param sc: Object's status change information
 * @param element: Object's current attack element
 * @return new attack element
 */
unsigned char status_calc_attack_element(struct block_list *bl, struct status_change *sc, int element)
{
	if(!sc || !sc->count)
		return (unsigned char)cap_value(element,0,UCHAR_MAX);

	if(sc->data[SC_ENCHANTARMS])
		return sc->data[SC_ENCHANTARMS]->val2;
	if(sc->data[SC_WATERWEAPON] ||
		(sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 2))
		return ELE_WATER;
	if(sc->data[SC_EARTHWEAPON] ||
		(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2))
		return ELE_EARTH;
	if(sc->data[SC_FIREWEAPON] ||
		(sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 2))
		return ELE_FIRE;
	if(sc->data[SC_WINDWEAPON] ||
		(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 2))
		return ELE_WIND;
	if(sc->data[SC_ENCPOISON])
		return ELE_POISON;
	if(sc->data[SC_ASPERSIO])
		return ELE_HOLY;
	if(sc->data[SC_SHADOWWEAPON])
		return ELE_DARK;
	if(sc->data[SC_GHOSTWEAPON] || sc->data[SC__INVISIBILITY])
		return ELE_GHOST;
	if(sc->data[SC_TIDAL_WEAPON_OPTION] || sc->data[SC_TIDAL_WEAPON])
		return ELE_WATER;
	if(sc->data[SC_PYROCLASTIC])
		return ELE_FIRE;

	return (unsigned char)cap_value(element,0,UCHAR_MAX);
}

/**
 * Changes the mode of an object
 * @param bl: Object whose mode to change [PC|MOB|PET|HOM|NPC]
 * @param sc: Object's status change data
 * @param mode: Original mode
 * @return mode with cap_value(mode,0,INT_MAX)
 */
enum e_mode status_calc_mode(struct block_list *bl, struct status_change *sc, enum e_mode mode)
{
	if(!sc || !sc->count)
		return cap_value(mode,0,INT_MAX);

	if(sc->data[SC_MODECHANGE]) {
		if (sc->data[SC_MODECHANGE]->val2)
			mode = (mode&~MD_MASK)|sc->data[SC_MODECHANGE]->val2; //Set mode
		if (sc->data[SC_MODECHANGE]->val3)
			mode |= sc->data[SC_MODECHANGE]->val3; //Add mode
		if (sc->data[SC_MODECHANGE]->val4)
			mode &= ~sc->data[SC_MODECHANGE]->val4; //Del mode
	}

	return cap_value(mode,0,INT_MAX);
}

/**
 * Changes the mode of a slave mob
 * @param md: Slave mob whose mode to change
 * @param mmd: Master of slave mob
 */
void status_calc_slave_mode(struct mob_data *md, struct mob_data *mmd)
{
	switch (battle_config.slaves_inherit_mode) {
		case 1: //Always aggressive
			if (!status_has_mode(&md->status,MD_AGGRESSIVE))
				sc_start4(NULL,&md->bl,SC_MODECHANGE,100,1,0,MD_AGGRESSIVE,0,0);
			break;
		case 2: //Always passive
			if (status_has_mode(&md->status,MD_AGGRESSIVE))
				sc_start4(NULL,&md->bl,SC_MODECHANGE,100,1,0,0,MD_AGGRESSIVE,0);
			break;
		case 4: //Overwrite with slave mode
			sc_start4(NULL,&md->bl,SC_MODECHANGE,100,1,MD_CANMOVE|MD_NORANDOM_WALK|MD_CANATTACK,0,0,0);
			break;
		default: //Copy master
			if (status_has_mode(&mmd->status,MD_AGGRESSIVE))
				sc_start4(NULL,&md->bl,SC_MODECHANGE,100,1,0,MD_AGGRESSIVE,0,0);
			else
				sc_start4(NULL,&md->bl,SC_MODECHANGE,100,1,0,0,MD_AGGRESSIVE,0);
			break;
	}
}

const char *status_get_name(struct block_list *bl) {
	nullpo_ret(bl);

	switch (bl->type) {
		case BL_PC:  return ((TBL_PC *)bl)->fakename[0] != '\0' ? ((TBL_PC *)bl)->fakename : ((TBL_PC *)bl)->status.name;
		case BL_MOB: return ((TBL_MOB *)bl)->name;
		case BL_PET: return ((TBL_PET *)bl)->pet.name;
		case BL_HOM: return ((TBL_HOM *)bl)->homunculus.name;
		//case BL_MER: //They only have database names which are global, not specific to GID
		case BL_NPC: return ((TBL_NPC *)bl)->name;
		//case BL_ELEM: //They only have database names which are global, not specific to GID
	}
	return "Unknown";
}

/*==========================================
 * Get the class of the current bl
 * Return:
 *	0 = fail
 *	class_id = success
 *------------------------------------------*/
int status_get_class(struct block_list *bl) {
	nullpo_ret(bl);

	switch( bl->type ) {
		case BL_PC:  return ((TBL_PC *)bl)->status.class_;
		case BL_MOB: return ((TBL_MOB *)bl)->vd->class_; //Class used on all code should be the view class of the mob
		case BL_PET: return ((TBL_PET *)bl)->pet.class_;
		case BL_HOM: return ((TBL_HOM *)bl)->homunculus.class_;
		case BL_MER: return ((TBL_MER *)bl)->mercenary.class_;
		case BL_NPC: return ((TBL_NPC *)bl)->class_;
		case BL_ELEM: return ((TBL_ELEM *)bl)->db->vd.class_;
	}
	return 0;
}

/*==========================================
 * Get the base level of the current bl
 * Return:
 *	1 = fail
 *	level = success
 *------------------------------------------*/
int status_get_lv(struct block_list *bl) {
	nullpo_ret(bl);

	switch (bl->type) {
		case BL_PC:  return ((TBL_PC *)bl)->status.base_level;
		case BL_MOB: return ((TBL_MOB *)bl)->level;
		case BL_PET: return ((TBL_PET *)bl)->pet.level;
		case BL_HOM: return ((TBL_HOM *)bl)->homunculus.level;
		case BL_MER: return ((TBL_MER *)bl)->db->lv;
		case BL_ELEM: return ((TBL_ELEM *)bl)->db->lv;
		case BL_NPC: return ((TBL_NPC *)bl)->level;
	}
	return 1;
}

/*==========================================
 * Get the job level of the current bl
 * Return:
 *	1 = fail
 *	level = success
 *------------------------------------------*/
int status_get_job_lv(struct block_list *bl) {
	nullpo_ret(bl);

	switch (bl->type) {
		case BL_PC:
			return ((TBL_PC *)bl)->status.job_level;
		case BL_MOB:
		case BL_PET:
		case BL_HOM:
		case BL_MER:
		case BL_ELEM:
		case BL_NPC:
			return 50;
	}
	return 1;
}

struct regen_data *status_get_regen_data(struct block_list *bl)
{
	nullpo_retr(NULL, bl);

	switch (bl->type) {
		case BL_PC:  return &((TBL_PC *)bl)->regen;
		case BL_HOM: return &((TBL_HOM *)bl)->regen;
		case BL_MER: return &((TBL_MER *)bl)->regen;
		case BL_ELEM: return &((TBL_ELEM *)bl)->regen;
		default:
			return NULL;
	}
}

struct status_data *status_get_status_data(struct block_list *bl)
{
	nullpo_retr(&dummy_status, bl);

	switch (bl->type) {
		case BL_PC:  return &((TBL_PC *)bl)->battle_status;
		case BL_MOB: return &((TBL_MOB *)bl)->status;
		case BL_PET: return &((TBL_PET *)bl)->status;
		case BL_HOM: return &((TBL_HOM *)bl)->battle_status;
		case BL_MER: return &((TBL_MER *)bl)->battle_status;
		case BL_ELEM: return &((TBL_ELEM *)bl)->battle_status;
		case BL_NPC: return (!mobdb_checkid(((TBL_NPC *)bl)->class_) ? &((TBL_NPC *)bl)->status : &dummy_status);
		default:
			return &dummy_status;
	}
}

struct status_data *status_get_base_status(struct block_list *bl)
{
	nullpo_retr(&dummy_status, bl);

	switch (bl->type) {
		case BL_PC:  return &((TBL_PC *)bl)->base_status;
		case BL_MOB: return (((TBL_MOB *)bl)->base_status ? ((TBL_MOB *)bl)->base_status : &((TBL_MOB *)bl)->db->status);
		case BL_PET: return &((TBL_PET *)bl)->db->status;
		case BL_HOM: return &((TBL_HOM *)bl)->base_status;
		case BL_MER: return &((TBL_MER *)bl)->base_status;
		case BL_ELEM: return &((TBL_ELEM *)bl)->base_status;
		case BL_NPC: return (((mobdb_checkid(((TBL_NPC *)bl)->class_) == 0) ? &((TBL_NPC *)bl)->status : &dummy_status));
		default:
			return &dummy_status;
	}
}

defType status_get_def(struct block_list *bl) {
	struct unit_data *ud;
	struct status_data *status = status_get_status_data(bl);
	int def = (status ? status->def : 0);

	ud = unit_bl2ud(bl);
	if (ud && ud->skilltimer != INVALID_TIMER)
		def -= def * skill_get_castdef(ud->skill_id) / 100;

	return (defType)cap_value(def,DEFTYPE_MIN,DEFTYPE_MAX);
}

unsigned short status_get_speed(struct block_list *bl)
{
	if (bl->type == BL_NPC) //Only BL with speed data but no status_data [Skotlex]
		return ((struct npc_data *)bl)->speed;
	return status_get_status_data(bl)->speed;
}

int status_get_party_id(struct block_list *bl) {
	nullpo_ret(bl);

	switch (bl->type) {
		case BL_PC:
			return ((TBL_PC *)bl)->status.party_id;
		case BL_PET:
			if (((TBL_PET *)bl)->master)
				return ((TBL_PET *)bl)->master->status.party_id;
			break;
		case BL_MOB: {
				struct mob_data *md = (TBL_MOB *)bl;

				if (md->master_id > 0) {
					struct map_session_data *msd = map_id2sd(md->master_id);

					if (md->special_state.ai && msd)
						return msd->status.party_id;
					return -md->master_id;
				}
			}
			break;
		case BL_HOM:
			if (((TBL_HOM *)bl)->master)
				return ((TBL_HOM *)bl)->master->status.party_id;
			break;
		case BL_MER:
			if (((TBL_MER *)bl)->master)
				return ((TBL_MER *)bl)->master->status.party_id;
			break;
		case BL_SKILL:
			if (((TBL_SKILL *)bl)->group)
				return ((TBL_SKILL *)bl)->group->party_id;
			break;
		case BL_ELEM:
			if (((TBL_ELEM *)bl)->master)
				return ((TBL_ELEM *)bl)->master->status.party_id;
			break;
	}
	return 0;
}

int status_get_guild_id(struct block_list *bl) {
	nullpo_ret(bl);

	switch (bl->type) {
		case BL_PC:
			return ((TBL_PC *)bl)->status.guild_id;
		case BL_PET:
			if (((TBL_PET *)bl)->master)
				return ((TBL_PET *)bl)->master->status.guild_id;
			break;
		case BL_MOB: {
				struct mob_data *md = (struct mob_data *)bl;
				struct map_session_data *msd = map_id2sd(md->master_id);

				//Guardian's guild [Skotlex]
				//Guardian guild data may not been available yet, castle data is always set
				if (md->guardian_data)
					return (md->guardian_data->g) ? md->guardian_data->g->guild_id : md->guardian_data->castle->guild_id;
				if (md->special_state.ai && msd)
					return msd->status.guild_id; //Alchemist's mobs [Skotlex]
			}
			break;
		case BL_HOM:
			if (((TBL_HOM *)bl)->master)
				return ((TBL_HOM *)bl)->master->status.guild_id;
			break;
		case BL_MER:
			if (((TBL_MER *)bl)->master)
				return ((TBL_MER *)bl)->master->status.guild_id;
			break;
		case BL_NPC:
			if (((TBL_NPC *)bl)->subtype == NPCTYPE_SCRIPT)
				return ((TBL_NPC *)bl)->u.scr.guild_id;
			break;
		case BL_SKILL:
			if (((TBL_SKILL *)bl)->group)
				return ((TBL_SKILL *)bl)->group->guild_id;
			break;
		case BL_ELEM:
			if (((TBL_ELEM *)bl)->master)
				return ((TBL_ELEM *)bl)->master->status.guild_id;
			break;
	}
	return 0;
}

int status_get_emblem_id(struct block_list *bl) {
	nullpo_ret(bl);

	switch (bl->type) {
		case BL_PC:
			return ((TBL_PC *)bl)->guild_emblem_id;
		case BL_PET:
			if (((TBL_PET *)bl)->master)
				return ((TBL_PET *)bl)->master->guild_emblem_id;
			break;
		case BL_MOB: {
				struct mob_data *md = (struct mob_data *)bl;
				struct map_session_data *msd = map_id2sd(md->master_id);

				if (md->guardian_data) //Guardian's guild [Skotlex]
					return (md->guardian_data->g) ? md->guardian_data->g->emblem_id : 0;
				if (md->special_state.ai && msd)
					return msd->guild_emblem_id; //Alchemist's mobs [Skotlex]
			}
			break;
		case BL_HOM:
			if (((TBL_HOM *)bl)->master)
				return ((TBL_HOM *)bl)->master->guild_emblem_id;
			break;
		case BL_MER:
			if (((TBL_MER *)bl)->master)
				return ((TBL_MER *)bl)->master->guild_emblem_id;
			break;
		case BL_NPC:
			if (((TBL_NPC *)bl)->subtype == NPCTYPE_SCRIPT && ((TBL_NPC *)bl)->u.scr.guild_id > 0) {
				struct guild *g = guild_search(((TBL_NPC *)bl)->u.scr.guild_id);

				if (g)
					return g->emblem_id;
			}
			break;
		case BL_ELEM:
			if (((TBL_ELEM *)bl)->master)
				return ((TBL_ELEM *)bl)->master->guild_emblem_id;
			break;
	}
	return 0;
}

/**
 * Gets the race2 of a mob or pet
 * @param bl: Object whose race2 to get [MOB|PET]
 * @return race2
 */
enum e_race2 status_get_race2(struct block_list *bl)
{
	nullpo_retr(RC2_NONE,bl);

	if (bl->type == BL_MOB)
		return ((struct mob_data *)bl)->db->race2;
	if (bl->type == BL_PET)
		return ((struct pet_data *)bl)->db->race2;
	return RC2_NONE;
}

bool status_isdead(struct block_list *bl)
{
	nullpo_ret(bl);

	return !status_get_status_data(bl)->hp;
}

bool status_isimmune(struct block_list *bl)
{
	struct status_change *sc = status_get_sc(bl);
	struct map_session_data *sd = map_id2sd(bl->id);

	if (sc && sc->data[SC_HERMODE])
		return true;
	if (sd && rnd()%100 < sd->special_state.no_magic_damage)
		return true;
	return false;
}

struct view_data *status_get_viewdata(struct block_list *bl)
{
	nullpo_retr(NULL,bl);

	switch (bl->type) {
		case BL_PC:  return &((TBL_PC *)bl)->vd;
		case BL_MOB: return ((TBL_MOB *)bl)->vd;
		case BL_PET: return &((TBL_PET *)bl)->vd;
		case BL_NPC: return &((TBL_NPC *)bl)->vd;
		case BL_HOM: return ((TBL_HOM *)bl)->vd;
		case BL_MER: return ((TBL_MER *)bl)->vd;
		case BL_ELEM: return ((TBL_ELEM *)bl)->vd;
	}
	return NULL;
}

void status_set_viewdata(struct block_list *bl, int class_)
{
	struct view_data *vd = NULL;

	nullpo_retv(bl);

	if (mobdb_checkid(class_) || mob_is_clone(class_))
		vd = mob_get_viewdata(class_);
	else if (npcdb_checkid(class_))
		vd = npc_get_viewdata(class_);
	else if (homdb_checkid(class_))
		vd = hom_get_viewdata(class_);
	else if (mercenary_class(class_))
		vd = mercenary_get_viewdata(class_);
	else if (elemental_class(class_))
		vd = elemental_get_viewdata(class_);

	switch (bl->type) {
		case BL_PC: {
				TBL_PC *sd = (TBL_PC *)bl;

				if (pcdb_checkid(class_)) {
					if (sd->sc.option&OPTION_RIDING) {
						switch (class_) { //Adapt class to a Mounted one
							case JOB_KNIGHT:
								class_ = JOB_KNIGHT2;
								break;
							case JOB_CRUSADER:
								class_ = JOB_CRUSADER2;
								break;
							case JOB_LORD_KNIGHT:
								class_ = JOB_LORD_KNIGHT2;
								break;
							case JOB_PALADIN:
								class_ = JOB_PALADIN2;
								break;
							case JOB_BABY_KNIGHT:
								class_ = JOB_BABY_KNIGHT2;
								break;
							case JOB_BABY_CRUSADER:
								class_ = JOB_BABY_CRUSADER2;
								break;
						}
					}
					sd->vd.class_ = class_;
					clif_get_weapon_view(sd,&sd->vd.weapon,&sd->vd.shield);
					sd->vd.head_top = sd->status.head_top;
					sd->vd.head_mid = sd->status.head_mid;
					sd->vd.head_bottom = sd->status.head_bottom;
					if( (sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER ) {
						sd->vd.hair_style = cap_value(sd->status.hair,MIN_DORAM_HAIR_STYLE,MAX_DORAM_HAIR_STYLE);
						sd->vd.hair_color = cap_value(sd->status.hair_color,MIN_DORAM_HAIR_COLOR,MAX_DORAM_HAIR_COLOR);
						sd->vd.cloth_color = cap_value(sd->status.clothes_color,MIN_DORAM_CLOTH_COLOR,MAX_DORAM_CLOTH_COLOR);
					} else {
						sd->vd.hair_style = cap_value(sd->status.hair,MIN_HAIR_STYLE,MAX_HAIR_STYLE);
						sd->vd.hair_color = cap_value(sd->status.hair_color,MIN_HAIR_COLOR,MAX_HAIR_COLOR);
						sd->vd.cloth_color = cap_value(sd->status.clothes_color,MIN_CLOTH_COLOR,MAX_CLOTH_COLOR);
					}
					sd->vd.body_style = cap_value(sd->status.body,MIN_BODY_STYLE,MAX_BODY_STYLE);
					sd->vd.robe = sd->status.robe;
					sd->vd.sex = sd->status.sex;
					if (sd->vd.cloth_color) {
						if (sd->sc.option&OPTION_WEDDING && battle_config.wedding_ignorepalette)
							sd->vd.cloth_color = 0;
						if (sd->sc.option&OPTION_XMAS && battle_config.xmas_ignorepalette)
							sd->vd.cloth_color = 0;
						if (sd->sc.option&(OPTION_SUMMER|OPTION_SUMMER2) && battle_config.summer_ignorepalette)
							sd->vd.cloth_color = 0;
						if (sd->sc.option&OPTION_HANBOK && battle_config.hanbok_ignorepalette)
							sd->vd.cloth_color = 0;
						if (sd->sc.option&OPTION_OKTOBERFEST && battle_config.oktoberfest_ignorepalette)
							sd->vd.cloth_color = 0;
						if (sd->vd.body_style && sd->sc.option&OPTION_COSTUME)
							sd->vd.body_style = 0;
					}
				} else if (vd)
					memcpy(&sd->vd,vd,sizeof(struct view_data));
				else
					ShowError("status_set_viewdata (PC): No view data for class %d\n ",class_);
			}
			break;
		case BL_MOB: {
				TBL_MOB *md = (TBL_MOB *)bl;

				if (vd) {
					mob_free_dynamic_viewdata(md);
					md->vd = vd;
				} else if (pcdb_checkid(class_)) {
					mob_set_dynamic_viewdata(md);
					md->vd->class_ = class_;
					if (class_ == JOB_SUMMONER || class_ == JOB_BABY_SUMMONER)
						md->vd->hair_style = cap_value(md->vd->hair_style,MIN_DORAM_HAIR_STYLE,MAX_DORAM_HAIR_STYLE);
					else
						md->vd->hair_style = cap_value(md->vd->hair_style,MIN_HAIR_STYLE,MAX_HAIR_STYLE);
				} else
					ShowError("status_set_viewdata (MOB): No view data for class %d\n ",class_);
			}
			break;
		case BL_PET: {
				TBL_PET *pd = (TBL_PET *)bl;

				if (vd) {
					memcpy(&pd->vd,vd,sizeof(struct view_data));
					if (!pcdb_checkid(vd->class_)) {
						pd->vd.hair_style = battle_config.pet_hair_style;
						if (pd->pet.equip) {
							pd->vd.head_bottom = itemdb_viewid(pd->pet.equip);
							if (!pd->vd.head_bottom)
								pd->vd.head_bottom = pd->pet.equip;
						}
					}
				} else
					ShowError("status_set_viewdata (PET): No view data for class %d\n ",class_);
			}
			break;
		case BL_NPC: {
				TBL_NPC *nd = (TBL_NPC *)bl;

				if (vd)
					memcpy(&nd->vd,vd,sizeof(struct view_data));
				else if (pcdb_checkid(class_)) {
					memset(&nd->vd,0,sizeof(struct view_data));
					nd->vd.class_ = class_;
					if (class_ == JOB_SUMMONER || class_ == JOB_BABY_SUMMONER)
						nd->vd.hair_style = cap_value(nd->vd.hair_style,MIN_DORAM_HAIR_STYLE,MAX_DORAM_HAIR_STYLE);
					else
						nd->vd.hair_style = cap_value(nd->vd.hair_style,MIN_HAIR_STYLE,MAX_HAIR_STYLE);
				} else
					ShowError("status_set_viewdata (NPC): No view data for class %d (name=%s)\n ",class_,nd->name);
			}
			break;
		case BL_HOM: { //[blackhole89]
				struct homun_data *hd = (struct homun_data *)bl;

				if (vd)
					hd->vd = vd;
				else
					ShowError("status_set_viewdata (HOMUNCULUS): No view data for class %d\n ",class_);
			}
			break;
		case BL_MER: {
				struct mercenary_data *md = (struct mercenary_data *)bl;

				if (vd)
					md->vd = vd;
				else
					ShowError("status_set_viewdata (MERCENARY): No view data for class %d\n ",class_);
			}
			break;
		case BL_ELEM: {
				struct elemental_data *ed = (struct elemental_data *)bl;

				if (vd)
					ed->vd = vd;
				else
					ShowError("status_set_viewdata (ELEMENTAL): No view data for class %d\n ",class_);
			}
			break;
	}
}

/// Returns the status_change data of bl or NULL if it doesn't exist.
struct status_change *status_get_sc(struct block_list *bl) {
	if (bl) {
		switch (bl->type) {
			case BL_PC:  return &((TBL_PC *)bl)->sc;
			case BL_MOB: return &((TBL_MOB *)bl)->sc;
			case BL_NPC: return &((TBL_NPC *)bl)->sc;
			case BL_HOM: return &((TBL_HOM *)bl)->sc;
			case BL_MER: return &((TBL_MER *)bl)->sc;
			case BL_ELEM: return &((TBL_ELEM *)bl)->sc;
		}
	}
	return NULL;
}

void status_change_init(struct block_list *bl)
{
	struct status_change *sc = status_get_sc(bl);

	nullpo_retv(sc);

	memset(sc,0,sizeof (struct status_change));
}

/**
 * Returns the interval for status changes that iterate multiple times
 * through the timer (e.g. those that deal damage in regular intervals)
 * @author [Playtester]
 * @param type: Status change (SC_*)
 */
int status_get_sc_interval(enum sc_type type)
{
	switch (type) {
		case SC_VACUUM_EXTREME:
			return 500;
		case SC_POISON:
		case SC_DPOISON:
		case SC_LEECHESEND:
		case SC_SV_ROOTTWIST:
			return 1000;
		case SC_BURNING:
		case SC_PYREXIA:
			return 3000;
		case SC_MAGICMUSHROOM:
			return 4000;
		case SC_STONE:
			return 5000;
		case SC_BLEEDING:
		case SC_TOXIN:
			return 10000;
	}
	return 0;
}

/**
 * Applies SC defense to a given status change.
 *
 * @see status_change_start for the expected parameters.
 * @return the adjusted duration based on flag values.
 */
int status_get_sc_def(struct block_list *src, struct block_list *bl, enum sc_type type, int rate, int val1, int val2, int val3, int val4, int tick, unsigned char flag)
{
	//Resistance rate: 10000 = 100%
	//Example:  50% (5000) -> sc_def = 5000 -> 25%;
	//          5000ms -> tick_def = 5000 -> 2500ms
	int sc_def = 0, tick_def = -1; //-1 = use sc_def
	//Fixed resistance value (after rate calculation)
	//Example:  25% (2500) -> sc_def2 = 2000 -> 5%;
	//          2500ms -> tick_def2 = 2000 -> 500ms
	int sc_def2 = 0, tick_def2 = 0;

	struct status_data *status, *status_src, *b_status;
	struct status_change *sc;
	struct map_session_data *sd;

	nullpo_ret(bl);

	if (!src) //This should not happen in current implementation, but leave it anyway
		return (tick ? tick : 1);

//Returns the 'bl's level, capped to 'cap'
#define SCDEF_LVL_CAP(bl,cap) ((bl) ? (status_get_lv(bl) > (cap) ? (cap) : status_get_lv(bl)) : 0)
//Returns the difference between the levels of 'bl' and 'src', both capped to 'maxlv', multiplied by 'factor'
#define SCDEF_LVL_DIFF(bl,src,maxlv,factor) ((SCDEF_LVL_CAP((bl),(maxlv)) - SCDEF_LVL_CAP((src),(maxlv))) * (factor))
//Returns the 'bl's job level, capped to 'cap'
#define SCDEF_JOBLVL_CAP(bl,cap) ((bl) ? (status_get_job_lv(bl) > (cap) ? (cap) : status_get_job_lv(bl)) : 0)

	if (status_isimmune(bl)) {
		switch (type) { //Status effects that are blocked by Golden Thief Bug card or Wand of Hermod
			case SC_CONFUSION:
				if (val4)
					return 0;
				break;
			case SC_BERSERK:
				if (val3 == SC__BLOODYLUST)
					return 0;
				break;
			case SC_DECREASEAGI:	case SC_SILENCE:	case SC_COMA:
			case SC_INCREASEAGI:	case SC_BLESSING:	case SC_SLOWPOISON:
			case SC_IMPOSITIO:	case SC_AETERNA:	case SC_SUFFRAGIUM:
			case SC_BENEDICTIO:	case SC_PROVIDENCE:	case SC_KYRIE:
			case SC_ASSUMPTIO:	case SC_ANGELUS:	case SC_MAGNIFICAT:
			case SC_GLORIA:		case SC_WINDWALK:	case SC_MAGICROD:
			case SC_HALLUCINATION:	case SC_STONE:		case SC_QUAGMIRE:
			case SC_SUITON:		case SC_SECRAMENT:	case SC_ADORAMUS:
			case SC_WHITEIMPRISON:	case SC__MANHOLE:	case SC_FIRE_INSIGNIA:
			case SC_WATER_INSIGNIA:	case SC_WIND_INSIGNIA:	case SC_EARTH_INSIGNIA:
			case SC_BLOODSUCKER:
				return 0;
		}
	}

	sd = BL_CAST(BL_PC,bl);
	status = status_get_status_data(bl);
	status_src = status_get_status_data(src);
	b_status = status_get_base_status(bl);
	sc = status_get_sc(bl);

	if (sc && !sc->count)
		sc = NULL;

	switch (type) {
		case SC_POISON:
		case SC_DPOISON:
			sc_def = status->vit * 100;
#ifndef RENEWAL
			sc_def2 = status->luk * 10 + SCDEF_LVL_DIFF(bl,src,99,10);
			if (sd) { //For players: 60000 - 450 * vit - 100 * luk
				tick_def = status->vit * 75;
				tick_def2 = status->luk * 100;
			} else { //For monsters: 30000 - 200 * vit
				tick /= 2;
				tick_def = status->vit * 200 / 3;
			}
#endif
			break;
		case SC_STUN:
			sc_def = status->vit * 100;
			sc_def2 = status->luk * 10 + SCDEF_LVL_DIFF(bl,src,99,10);
			tick_def2 = status->luk * 10;
			break;
		case SC_SILENCE:
			sc_def =
#ifdef RENEWAL
				status->int_
#else
				status->vit
#endif
				* 100;
			sc_def2 =
#ifdef RENEWAL
				(status->vit + status->luk) * 5
#else
				status->luk * 10
#endif
				+ SCDEF_LVL_DIFF(bl,src,99,10);
			tick_def2 = status->luk * 10;
			break;
		case SC_BLEEDING:
			sc_def =
#ifdef RENEWAL
				status->agi
#else
				status->vit
#endif
				* 100;
			sc_def2 =
#ifdef RENEWAL
				(status->vit + status->luk) * 5
#else
				status->luk * 10
#endif
				+ SCDEF_LVL_DIFF(bl,src,99,10);
			tick_def2 = status->luk * 10;
			break;
		case SC_SLEEP:
			sc_def =
#ifdef RENEWAL
				status->agi
#else
				status->int_
#endif
				* 100;
			sc_def2 =
#ifdef RENEWAL
				(status->vit + status->luk) * 5
#else
				status->luk * 10
#endif
				+ SCDEF_LVL_DIFF(bl,src,99,10);
			tick_def2 = status->luk * 10;
			break;
		case SC_STONE:
			sc_def = status->mdef * 100;
			sc_def2 = status->luk * 10 + SCDEF_LVL_DIFF(bl,src,99,10);
			tick_def = 0; //No duration reduction
			break;
		case SC_FREEZE:
			sc_def = status->mdef * 100;
			sc_def2 = status->luk * 10 + SCDEF_LVL_DIFF(bl,src,99,10);
			tick_def2 = status_src->luk * -10; //Caster can increase final duration with luk
			break;
		case SC_CURSE:
			if (!status->luk)
				return 0; //Special property: Immunity when luk is zero
			sc_def = status->luk * 100;
			sc_def2 = status->luk * 10 - SCDEF_LVL_CAP(src,99) * 10; //Curse only has a level penalty and no resistance
			tick_def = status->vit * 100;
			tick_def2 = status->luk * 10;
			break;
		case SC_BLIND:
			sc_def =
#ifdef RENEWAL
				status->int_
#else
				status->vit
#endif
				* 100;
			sc_def2 =
#ifdef RENEWAL
				(status->vit + status->luk) * 5
#else
				status->luk * 10
#endif
				+ SCDEF_LVL_DIFF(bl,src,99,10);
			tick_def2 = status->luk * 10;
			break;
		case SC_CONFUSION:
			sc_def =
#ifdef RENEWAL
				status->luk * 100
#else
				(status->str + status->int_) * 50
#endif
				;
			sc_def2 = SCDEF_LVL_DIFF(src,bl,99,10) - status->luk * 10; //Reversed sc_def2
			tick_def2 = status->luk * 10;
			break;
		case SC_DECREASEAGI:
			if (sd)
				tick /= 2; //Half duration for players
			sc_def = status->mdef * 100;
			tick_def = 0; //No duration reduction
			break;
		case SC_ANKLE:
			tick_def2 = status->agi * 100;
			break;
		case SC_MARSHOFABYSS: //5 secs (Fixed) + 25 secs - {((INT + LUK) / 20 secs)}
			tick_def2 = (status->int_ + status->luk) * 50;
			break;
		case SC_WHITEIMPRISON:
			if (!sd)
				tick_def2 = (status->vit + status->luk) * 50;
			break;
		case SC_STASIS: //10 secs (fixed) + {(Stasis Skill level * 10 - (Target's VIT + DEX) / 20)}
			tick_def2 = (status->vit + status->dex) * 50;
			break;
		case SC_BURNING:
			tick_def2 = status->agi * 125 + status->luk * 75;
			break;
		case SC_FREEZING:
			tick_def2 = (status->vit + status->dex) * 50;
			break;
		case SC_OBLIVIONCURSE: //(100 - 0.8 x INT)%
			sc_def = status->int_ * 80;
			tick_def = 0;
		//Fall through
		case SC_TOXIN:
		case SC_PARALYSE:
		case SC_VENOMBLEED:
		case SC_MAGICMUSHROOM:
		case SC_DEATHHURT:
		case SC_PYREXIA:
		case SC_LEECHESEND:
			tick_def2 = (status->vit + status->luk) * 500;
			break;
		case SC_BITE: //{(Base Success chance) - (Target's AGI / 4)}
			sc_def2 = status->agi * 25;
			break;
		case SC_ELECTRICSHOCKER:
			tick_def2 = (status->vit + status->agi) * 70;
			break;
		case SC_DEEPSLEEP:
			tick_def2 = b_status->int_ * 25 + status_get_lv(bl) * 50;
			break;
		case SC_VOICEOFSIREN:
			tick_def2 = status_get_lv(bl) * 100 + status_get_job_lv(bl) * 200;
			break;
		case SC_NETHERWORLD:
			tick_def2 = SCDEF_LVL_CAP(bl,150) * 20 + SCDEF_JOBLVL_CAP(bl,50) * 100;
			break;
		case SC_CRYSTALIZE:
			tick_def2 = (sd ? sd->status.vit : b_status->vit) * 100;
			break;
		case SC_VACUUM_EXTREME:
			tick_def2 = (sd ? sd->status.str : b_status->str) * 50;
			break;
		case SC_KYOUGAKU:
			tick_def2 = status->int_ * 50;
			break;
		case SC_PARALYSIS:
			tick_def2 = (status->vit + status->luk) * 50;
			break;
		case SC_B_TRAP:
			tick_def2 = (sd ? sd->status.str : b_status->str) * 50;
			break;
		case SC_NORECOVER_STATE:
			tick_def2 = status->luk * 100;
			break;
		default: //Effect that cannot be reduced? Likely a buff
			if (!(rnd()%10000 < rate))
				return 0;
			return (tick ? tick : 1);
	}

	if (sd) {
		if (battle_config.pc_sc_def_rate != 100) {
			sc_def = sc_def * battle_config.pc_sc_def_rate / 100;
			sc_def2 = sc_def2 * battle_config.pc_sc_def_rate / 100;
		}
		sc_def = min(sc_def,battle_config.pc_max_sc_def * 100);
		sc_def2 = min(sc_def2,battle_config.pc_max_sc_def * 100);
		if (battle_config.pc_sc_def_rate != 100) {
			if (tick_def > 0)
				tick_def = tick_def * battle_config.pc_sc_def_rate / 100;
			tick_def2 = tick_def2 * battle_config.pc_sc_def_rate / 100;
		}
	} else {
		if (battle_config.mob_sc_def_rate != 100) {
			sc_def = sc_def * battle_config.mob_sc_def_rate / 100;
			sc_def2 = sc_def2 * battle_config.mob_sc_def_rate / 100;
		}
		sc_def = min(sc_def,battle_config.mob_max_sc_def * 100);
		sc_def2 = min(sc_def2,battle_config.mob_max_sc_def * 100);
		if (battle_config.mob_sc_def_rate != 100) {
			if (tick_def > 0)
				tick_def = tick_def * battle_config.mob_sc_def_rate / 100;
			tick_def2 = tick_def2 * battle_config.mob_sc_def_rate / 100;
		}
	}

	if (sc) {
		if (sc->data[SC_SCRESIST])
			sc_def += sc->data[SC_SCRESIST]->val1 * 100; //Status resist
		else if (sc->data[SC_SIEGFRIED])
			sc_def += sc->data[SC_SIEGFRIED]->val3 * 100; //Status resistance
		else if (sc->data[SC_SHIELDSPELL_REF] && sc->data[SC_SHIELDSPELL_REF]->val1 == 2)
			sc_def += sc->data[SC_SHIELDSPELL_REF]->val3 * 100;
	}

	if (tick_def == -1) //When tick def not set, reduction is the same for both
		tick_def = sc_def;

	if (!(flag&SCFLAG_FIXEDRATE)) { //Rate reduction
		rate -= rate * sc_def / 10000;
		rate -= sc_def2;
		switch (type) { //Minimum chances
			case SC_OBLIVIONCURSE:
			case SC_KYOUGAKU:
				rate = max(rate,500); //5%
				break;
			case SC_BITE:
				rate = max(rate,5000); //50%
				break;
		}
		if (sd) {
			int bonus = 0;

			if (sd->reseff[type] && type > SC_NONE && type < SC_MAX)
				bonus += rate * sd->reseff[type] / 10000;
			if (sd->sc.data[SC_COMMONSC_RESIST] && type >= SC_COMMON_MIN && type <= SC_COMMON_MAX)
				bonus += rate * sd->sc.data[SC_COMMONSC_RESIST]->val1 / 100;
			rate -= bonus; //Item resistance (only applies to rate%)
		}
		if (rate > 0 && rate%10)
			rate += (10 - rate%10); //Aegis accuracy
	}

	if (!(rnd()%10000 < rate))
		return 0;

	//Tick reduction
	if (flag&SCFLAG_FIXEDTICK)
		return max(tick,1);

	tick -= tick * tick_def / 10000;
	tick -= tick_def2;

	switch (type) { //Minimum durations
		case SC_NETHERWORLD:
			tick = max(tick,4000); //4 secs
			break;
		case SC_ANKLE:
			if (status_has_mode(status,MD_STATUS_IMMUNE))
				tick /= 5; //Lasts 5 times less on bosses
			tick = max(tick,5000 + status_get_lv(src) * 10); //5 secs + (Caster's Base Level * 0.01)
			break;
		case SC_MARSHOFABYSS:
		case SC_PARALYSIS:
			tick = max(tick,5000); //5 secs
			break;
		case SC_WHITEIMPRISON:
			if (!sd)
		//Fall through
		case SC_BURNING:
		case SC_FREEZING:
		case SC_VOICEOFSIREN:
			tick = max(tick,10000); //10 secs
			break;
		case SC_STASIS:
			if (tick >= 0) //Minimun duration won't be applied if the tick is negative [exneval]
				tick = max(tick,10000);
			break;
		case SC_KYOUGAKU:
			tick = max(tick,1000); //1 sec
			break;
	}

	return max(tick,0);
#undef SCDEF_LVL_CAP
#undef SCDEF_LVL_DIFF
#undef SCDEF_JOBLVL_CAP
}

/**
 * Applies SC effect
 * @param bl: Source to apply effect
 * @param type: Status change (SC_*)
 * @param dval1~3: Depends on type of status change
 * Author: Ind
 */
void status_display_add(struct block_list *bl, enum sc_type type, int dval1, int dval2, int dval3) {
	struct eri *eri;
	struct sc_display_entry **sc_display;
	struct sc_display_entry ***sc_display_ptr;
	struct sc_display_entry *entry;
	int i;
	unsigned char sc_display_count;
	unsigned char *sc_display_count_ptr;

	nullpo_retv(bl);

	switch( bl->type ) {
		case BL_PC: {
				struct map_session_data *sd = map_id2sd(bl->id);

				sc_display_ptr = &sd->sc_display;
				sc_display_count_ptr = &sd->sc_display_count;
				eri = pc_sc_display_ers;
			}
			break;
		case BL_NPC: {
				struct npc_data *nd = map_id2nd(bl->id);

				sc_display_ptr = &nd->sc_display;
				sc_display_count_ptr = &nd->sc_display_count;
				eri = npc_sc_display_ers;
			}
			break;
		case BL_MOB: {
				struct mob_data *md = map_id2md(bl->id);

				sc_display_ptr = &md->sc_display;
				sc_display_count_ptr = &md->sc_display_count;
				eri = mob_sc_display_ers;
			}
			break;
		default:
			return;
	}

	sc_display = *sc_display_ptr;
	sc_display_count = *sc_display_count_ptr;

	ARR_FIND(0, sc_display_count, i, sc_display[i]->type == type);

	if( i != sc_display_count ) {
		sc_display[i]->val1 = dval1;
		sc_display[i]->val2 = dval2;
		sc_display[i]->val3 = dval3;
		return;
	}
	entry = ers_alloc(eri, struct sc_display_entry);
	entry->type = type;
	entry->val1 = dval1;
	entry->val2 = dval2;
	entry->val3 = dval3;
	RECREATE(sc_display, struct sc_display_entry *, ++sc_display_count);
	sc_display[sc_display_count - 1] = entry;
	*sc_display_ptr = sc_display;
	*sc_display_count_ptr = sc_display_count;
}

/**
 * Removes SC effect
 * @param bl: Source to remove effect
 * @param type: Status change (SC_*)
 * Author: Ind
 */
void status_display_remove(struct block_list *bl, enum sc_type type) {
	struct eri *eri;
	struct sc_display_entry **sc_display;
	struct sc_display_entry ***sc_display_ptr;
	int i;
	unsigned char sc_display_count;
	unsigned char *sc_display_count_ptr;

	nullpo_retv(bl);

	switch( bl->type ) {
		case BL_PC: {
				struct map_session_data *sd = map_id2sd(bl->id);

				sc_display_ptr = &sd->sc_display;
				sc_display_count_ptr = &sd->sc_display_count;
				eri = pc_sc_display_ers;
			}
			break;
		case BL_NPC: {
				struct npc_data *nd = map_id2nd(bl->id);

				sc_display_ptr = &nd->sc_display;
				sc_display_count_ptr = &nd->sc_display_count;
				eri = npc_sc_display_ers;
			}
			break;
		case BL_MOB: {
				struct mob_data *md = map_id2md(bl->id);

				sc_display_ptr = &md->sc_display;
				sc_display_count_ptr = &md->sc_display_count;
				eri = mob_sc_display_ers;
			}
			break;
		default:
			return;
	}

	sc_display = *sc_display_ptr;
	sc_display_count = *sc_display_count_ptr;

	ARR_FIND(0, sc_display_count, i, sc_display[i]->type == type);

	if( i != sc_display_count ) {
		int cursor;

		ers_free(eri, sc_display[i]);
		sc_display[i] = NULL;
		for( i = 0, cursor = 0; i < sc_display_count; i++ ) { //The all-mighty compact-o-matic
			if( !sc_display[i] )
				continue;
			if( i != cursor )
				sc_display[cursor] = sc_display[i];
			cursor++;
		}
		if( !(sc_display_count = cursor) ) {
			aFree(sc_display);
			sc_display = NULL;
		}
		*sc_display_ptr = sc_display;
		*sc_display_count_ptr = sc_display_count;
	}
}

/**
 * Starts a status change.
 *
 * @param src  Status change source bl.
 * @param bl   Status change target bl.
 * @param type Status change type.
 * @param rate Base success rate. 1 means 0.01%, 10000 means 100%.
 * @param val1 Additional value (meaning depends on type).
 * @param val2 Additional value (meaning depends on type).
 * @param val3 Additional value (meaning depends on type).
 * @param val4 Additional value (meaning depends on type).
 * @param tick Base duration (milliseconds).
 * @param flag Special flags (@see enum scstart_flag).
 *
 * @return 0 if no status change happened.
 * @return 1 if the status change was successfully applied.
 */
int status_change_start(struct block_list *src, struct block_list *bl, enum sc_type type, int rate, int val1, int val2, int val3, int val4, int tick, unsigned char flag)
{
	struct map_session_data *sd = NULL;
	struct status_change *sc = NULL;
	struct status_change_entry *sce = NULL;
	struct status_data *status = NULL;
	struct view_data *vd = NULL;
	int opt_flag, calc_flag, undead_flag, val_flag = 0, icon = SI_BLANK,
		tick_time = 0; //[GodLesZ] tick time
	bool sc_isnew = true;

	nullpo_ret(bl);

	sc = status_get_sc(bl);
	status = status_get_status_data(bl);

	if( type <= SC_NONE || type >= SC_MAX ) {
		ShowError("status_change_start: invalid status change (%d)!\n",type);
		return 0;
	}

	if( !sc ) //Unable to receive status changes
		return 0;

	if( bl->type != BL_NPC && status_isdead(bl) && type != SC_NOCHAT ) //SC_NOCHAT should work even on dead characters
		return 0;

	if( status_change_isDisabledOnMap(type,bl->m) )
		return 0;

	//Immunes against status effects
	if( sc->data[SC_REFRESH] ) {
		if( type >= SC_COMMON_MIN && type <= SC_COMMON_MAX )
			return 0;
		switch( type ) {
			case SC_FEAR:		case SC_BURNING:	case SC_FREEZING:
			case SC_WHITEIMPRISON:	case SC_MARSHOFABYSS:	case SC_TOXIN:
			case SC_PARALYSE:	case SC_VENOMBLEED:	case SC_MAGICMUSHROOM:
			case SC_DEATHHURT:	case SC_PYREXIA:	case SC_OBLIVIONCURSE:
			case SC_LEECHESEND:	case SC_DEEPSLEEP:	case SC_CRYSTALIZE:
			case SC_MANDRAGORA:
				return 0;
		}
	}

	if( sc->data[SC_INSPIRATION] ) {
		if( type >= SC_COMMON_MIN && type <= SC_COMMON_MAX )
			return 0;
		switch( type ) {
			case SC_FEAR:		case SC_BURNING:	case SC_FREEZING:
			case SC_TOXIN:		case SC_PARALYSE:	case SC_VENOMBLEED:
			case SC_MAGICMUSHROOM:	case SC_DEATHHURT:	case SC_PYREXIA:
			case SC_OBLIVIONCURSE:	case SC_LEECHESEND:	case SC__BODYPAINT:
			case SC__ENERVATION:	case SC__GROOMY:	case SC__IGNORANCE:
			case SC__LAZINESS:	case SC__UNLUCKY:	case SC__WEAKNESS:
			case SC_DEEPSLEEP:	case SC_CRYSTALIZE:	case SC_SATURDAYNIGHTFEVER:
				return 0;
		}
	}

	if( sc->data[SC_KINGS_GRACE] ) {
		if( type >= SC_COMMON_MIN && type <= SC_COMMON_MAX )
			return 0;
		switch( type ) {
			case SC_HALLUCINATION:	case SC_FEAR:		case SC_BURNING:
			case SC_FREEZING:	case SC_DEEPSLEEP:	case SC_CRYSTALIZE:
			case SC_MANDRAGORA:
				return 0;
		}
	}

	if( sc->data[SC_RAY_OF_PROTECTION] && type >= SC_COMMON_MIN && type <= SC_COMMON_MAX )
		return 0;

	//Adjust tick according to status resistances
	if( !(flag&(SCFLAG_NOAVOID|SCFLAG_LOADED)) && !(tick = status_get_sc_def(src,bl,type,rate,val1,val2,val3,val4,tick,flag)) )
		return 0; //Don't bother trying to start the status

	sd = BL_CAST(BL_PC,bl);
	vd = status_get_viewdata(bl);
	undead_flag = battle_check_undead(status->race,status->def_ele);

	switch( type ) { //Check for immunities/sc fails
		case SC_DECREASEAGI:
		case SC_QUAGMIRE:
		case SC_DONTFORGETME:
			if( sc->data[SC_SPEEDUP1] )
				return 0;
			break;
		case SC_ANGRIFFS_MODUS:
		case SC_GOLDENE_FERSE:
			if( (type == SC_GOLDENE_FERSE && sc->data[SC_ANGRIFFS_MODUS]) ||
				(type == SC_ANGRIFFS_MODUS && sc->data[SC_GOLDENE_FERSE]) )
				return 0;
			break;
		case SC_STONE:
			if( sc->data[SC_POWER_OF_GAIA] )
				return 0;
		//Fall through
		case SC_FREEZE:
			if( undead_flag && !(flag&SCFLAG_NOAVOID) )
				return 0; //Undead are immune to Stone/Freeze
			if( type == SC_FREEZE && sc->data[SC_WARMER] )
				return 0; //Warmer makes you immune to freezing
		//Fall through
		case SC_STUN:
		case SC_SLEEP:
		case SC_BURNING:
			if( type == SC_BURNING && sc->data[SC_FREEZING] )
				return 0; //Burning can't be given to someone in freezing status
		//Fall through
		case SC_WHITEIMPRISON:
			if( sc->opt1 )
				return 0; //Cannot override other OPT1 status changes [Skotlex]
			break;
		case SC_FREEZING:
		case SC_CRYSTALIZE:
			if( (type == SC_FREEZING && sc->data[SC_BURNING]) || sc->data[SC_WARMER] )
				return 0; //Burning makes you immune to freezing
			break;
		case SC_BLIND:
			if( sc->data[SC_FEAR] )
				return 0;
			break;
		case SC_BLEEDING:
			if( sc->data[SC_POWER_OF_GAIA] )
				return 0;
			break;
		case SC_PROVOKE:
			if( undead_flag )
				return 0;
			break;
		case SC_BERSERK: //There all like berserk, do not everlap each other
			if( sc->data[SC_SATURDAYNIGHTFEVER] )
				return 0;
			break;
		case SC_SIGNUMCRUCIS: //Only affects demons and undead element (but not players)
			if( bl->type == BL_PC || (!undead_flag && status->race != RC_DEMON) )
				return 0;
			break;
		case SC_AETERNA:
			if( (sc->data[SC_STONE] && sc->opt1 == OPT1_STONE) || sc->data[SC_FREEZE] )
				return 0;
			break;
		case SC_OVERTHRUST:
			if( sc->data[SC_MAXOVERTHRUST] )
				return 0; //Overthrust can't take effect if under Max Overthrust [Skotlex]
		case SC_MAXOVERTHRUST:
			if( sc->option&OPTION_MADOGEAR )
				return 0; //Overthrust and Overthrust Max cannot be used on Mado Gear [Ind]
			break;
		case SC_ADRENALINE:
		case SC_ADRENALINE2:
			if( sc->data[SC_QUAGMIRE] || sc->data[SC_DECREASEAGI] )
				return 0;
			break;
		case SC_MAGNIFICAT:
			if( sc->option&OPTION_MADOGEAR ) //Mado is immune to magnificat
				return 0;
			break;
		case SC_ONEHAND:
		case SC_MERC_QUICKEN:
		case SC_TWOHANDQUICKEN:
			if( sc->data[SC_DECREASEAGI] )
				return 0;
		//Fall through
		case SC_INCREASEAGI:
		case SC_CONCENTRATE:
		case SC_SPEARQUICKEN:
		case SC_TRUESIGHT:
		case SC_WINDWALK:
		case SC_CARTBOOST:
		case SC_ASSNCROS:
			if( sc->option&OPTION_MADOGEAR )
				return 0; //Mado is immune to increase agi, wind walk, cart boost, etc (others above) [Ind]
			if( sc->data[SC_QUAGMIRE] )
				return 0;
			break;
		case SC_CLOAKING:
			if( sd && !skill_can_cloak(sd) )
				return 0;
			break;
		case SC_MODECHANGE: {
				enum e_mode mode;
				struct status_data *bstatus = status_get_base_status(bl);

				if( !bstatus )
					return 0;
				if( sc->data[type] ) { //Pile up with previous values
					if( !val2 )
						val2 = sc->data[type]->val2;
					val3 |= sc->data[type]->val3;
					val4 |= sc->data[type]->val4;
				}
				mode = (val2 ? ((val2&~MD_MASK)|val2) : bstatus->mode); //Base mode
				if( val4 )
					mode &= ~val4; //Del mode
				if( val3 )
					mode |= val3; //Add mode
				if( mode == bstatus->mode ) { //No change
					if( sc->data[type] ) //Abort previous status
						return status_change_end(bl,type,INVALID_TIMER);
					return 0;
				}
			}
			break;
		//Strip skills, need to divest something or it fails
		case SC_STRIPWEAPON:
			if( sd && !(flag&SCFLAG_LOADED) ) { //Apply sc anyway if loading saved sc_data
				short i = sd->equip_index[EQI_HAND_R];

				opt_flag = 0; //Reuse to check success condition
				if( i >= 0 && sd->inventory_data[i] && sd->inventory_data[i]->type == IT_WEAPON ) {
					opt_flag |= 2;
					pc_unequipitem(sd,i,1|2);
				}
				if( !opt_flag )
					return 0;
			}
			if( tick == 1 )
				return 1; //Minimal duration: Only strip without causing the SC
			break;
		case SC_STRIPSHIELD:
			if( val2 == 1 )
				val2 = 0; //GX effect, do not take shield off
			else if( sd && !(flag&SCFLAG_LOADED) ) {
				short i = sd->equip_index[EQI_HAND_L];

				if( i < 0 || !sd->inventory_data[i] || sd->inventory_data[i]->type != IT_ARMOR )
					return 0;
				pc_unequipitem(sd,i,1|2);
			}
			if( tick == 1 )
				return 1; //Minimal duration: Only strip without causing the SC
			break;
		case SC_STRIPARMOR:
			if( sd && !(flag&SCFLAG_LOADED) ) {
				short i = sd->equip_index[EQI_ARMOR];

				if( i < 0 || !sd->inventory_data[i] )
					return 0;
				pc_unequipitem(sd,i,1|2);
			}
			if( tick == 1 )
				return 1; //Minimal duration: Only strip without causing the SC
			break;
		case SC_STRIPHELM:
			if( sd && !(flag&SCFLAG_LOADED) ) {
				short i = sd->equip_index[EQI_HEAD_TOP];

				if( i < 0 || !sd->inventory_data[i] )
					return 0;
				pc_unequipitem(sd,i,1|2);
			}
			if( tick == 1 )
				return 1; //Minimal duration: Only strip without causing the SC
			break;
		case SC_MERC_FLEEUP:
		case SC_MERC_ATKUP:
		case SC_MERC_HPUP:
		case SC_MERC_SPUP:
		case SC_MERC_HITUP:
			if( bl->type != BL_MER )
				return 0; //Stats only for Mercenaries
			break;
		case SC_STRFOOD:
			if( sc->data[SC_FOOD_STR_CASH] && sc->data[SC_FOOD_STR_CASH]->val1 > val1 )
				return 0;
			break;
		case SC_AGIFOOD:
			if( sc->data[SC_FOOD_AGI_CASH] && sc->data[SC_FOOD_AGI_CASH]->val1 > val1 )
				return 0;
			break;
		case SC_VITFOOD:
			if( sc->data[SC_FOOD_VIT_CASH] && sc->data[SC_FOOD_VIT_CASH]->val1 > val1 )
				return 0;
			break;
		case SC_INTFOOD:
			if( sc->data[SC_FOOD_INT_CASH] && sc->data[SC_FOOD_INT_CASH]->val1 > val1 )
				return 0;
			break;
		case SC_DEXFOOD:
			if( sc->data[SC_FOOD_DEX_CASH] && sc->data[SC_FOOD_DEX_CASH]->val1 > val1 )
				return 0;
			break;
		case SC_LUKFOOD:
			if( sc->data[SC_FOOD_LUK_CASH] && sc->data[SC_FOOD_LUK_CASH]->val1 > val1 )
				return 0;
			break;
		case SC_FOOD_STR_CASH:
		case SC_FOOD_AGI_CASH:
		case SC_FOOD_VIT_CASH:
		case SC_FOOD_INT_CASH:
		case SC_FOOD_DEX_CASH:
		case SC_FOOD_LUK_CASH:
			if( sc->data[SC_ALMIGHTY] )
				return 0;
			break;
		case SC_EPICLESIS:
			if( bl->type != BL_PC || undead_flag )
				return 0;
		//Fall through
		case SC_LERADSDEW:
			if( sc->data[SC_BERSERK] || sc->data[SC_SATURDAYNIGHTFEVER] )
				return 0;
			break;
		case SC_RENOVATIO:
			if( undead_flag && bl->type == BL_PC )
				return 0;
			break;
		case SC_CAMOUFLAGE:
			if( sd && pc_checkskill(sd,RA_CAMOUFLAGE) < 2 && !skill_check_camouflage(bl,NULL) )
				return 0;
			break;
		case SC__STRIPACCESSORY:
			if( sd ) {
				short i;

				if( pc_ismadogear(sd) )
					return 0;
				if( (i = sd->equip_index[EQI_ACC_L]) >= 0 && sd->inventory_data[i] && sd->inventory_data[i]->type == IT_ARMOR )
					pc_unequipitem(sd,i,1|2); //L-Accessory
				if( (i = sd->equip_index[EQI_ACC_R]) >= 0 && sd->inventory_data[i] && sd->inventory_data[i]->type == IT_ARMOR )
					pc_unequipitem(sd,i,1|2); //R-Accessory
				if( i < 0 )
					return 0;
			}
			if( tick == 1 )
				return 1; //Minimal duration: Only strip without causing the SC
			break;
		case SC_TOXIN:
		case SC_PARALYSE:
		case SC_VENOMBLEED:
		case SC_MAGICMUSHROOM:
		case SC_DEATHHURT:
		case SC_PYREXIA:
		case SC_OBLIVIONCURSE:
		case SC_LEECHESEND:
			{ //It doesn't stack or even renewed
				int i = SC_TOXIN;

				for( ; i <= SC_LEECHESEND; i++ )
					if( sc->data[i] )
						return 0;
			}
			break;
		case SC_MAGNETICFIELD:
			if( sc->data[SC_HOVERING] )
				return 0;
			break;
		case SC_SATURDAYNIGHTFEVER:
			if( sc->data[SC_BERSERK] )
				return 0;
			break;
		case SC_ALL_RIDING:
			if( sc->option&(OPTION_RIDING|OPTION_DRAGON|OPTION_WUGRIDER|OPTION_MADOGEAR) )
				return 0;
			if( sc->data[SC_CLOAKING] || sc->data[SC_CHASEWALK] ||
				sc->data[SC_CLOAKINGEXCEED] || sc->data[SC_CAMOUFLAGE] ||
				sc->data[SC_STEALTHFIELD] || sc->data[SC__FEINTBOMB] )
				return 0;
			if( sc->data[type] ) { //Already mounted, just dismount
				status_change_end(bl,type,INVALID_TIMER);
				return 0;
			}
			break;
		case SC_KINGS_GRACE:
			if( sc->data[SC_WHITEIMPRISON] )
				return 0;
			break;
		case SC_BURNT:
			if( sc->data[SC_CHILL] )
				return 0;
			break;
		case SC_WEDDING:
		case SC_XMAS:
		case SC_SUMMER:
		case SC_HANBOK:
		case SC_OKTOBERFEST:
		case SC_DRESSUP:
			if( !vd )
				return 0;
			break;
	}

	if( status_has_mode(status,MD_STATUS_IMMUNE) && !(flag&SCFLAG_NOAVOID) ) { //Check for resistances
		 if( type >= SC_COMMON_MIN && type <= SC_COMMON_MAX )
			 return 0;
		 switch( type ) {
			case SC_BLESSING:
			case SC_DECREASEAGI:
			case SC_PROVOKE:
			case SC_COMA:
			case SC_ELEMENTALCHANGE:
			case SC_STOP:
			case SC_GRAVITATION:
			case SC_SUITON:
			case SC_STRIPWEAPON:
			case SC_STRIPARMOR:
			case SC_STRIPSHIELD:
			case SC_STRIPHELM:
			case SC_RICHMANKIM:
			case SC_ROKISWEIL:
			case SC_FOGWALL:
			case SC_FEAR:
			case SC_BURNING:
			case SC_FREEZING:
			case SC_ADORAMUS:
			case SC_MARSHOFABYSS:
			case SC_WHITEIMPRISON:
			case SC_TOXIN:
			case SC_PARALYSE:
			case SC_VENOMBLEED:
			case SC_MAGICMUSHROOM:
			case SC_DEATHHURT:
			case SC_PYREXIA:
			case SC_OBLIVIONCURSE:
			case SC_LEECHESEND:
			case SC_BANDING_DEFENCE:
			case SC__ENERVATION:
			case SC__GROOMY:
			case SC__IGNORANCE:
			case SC__LAZINESS:
			case SC__UNLUCKY:
			case SC__WEAKNESS:
			case SC__STRIPACCESSORY:
			case SC_CURSEDCIRCLE_TARGET:
			case SC_NETHERWORLD:
			case SC_TEARGAS:
			case SC_TEARGAS_SOB:
			case SC_ASH:
			case SC_PARALYSIS:
			case SC_PAIN_KILLER:
			case SC_KYOMU:
			case SC_AKAITSUKI:
			case SC_CATNIPPOWDER:
			case SC_BITESCAR:
			case SC_FRESHSHRIMP:
				return 0;
		}
	}

	if( status_has_mode(status,MD_MVP) && !(flag&SCFLAG_NOAVOID) ) { //Check for MVP resistance
		switch( type ) {
			case SC_COMA:
			//Continue list
				return 0;
		}
	}

	switch( type ) { //Before overlapping fail, one must check for status cured
		case SC_ENDURE:
			if( sd && sd->special_state.no_walkdelay )
				return 1;
			break;
		case SC_BLESSING:
			if( bl->type == BL_PC || (!undead_flag && status->race != RC_DEMON) ) {
				if( sc->data[SC_STONE] && sc->opt1 == OPT1_STONE )
					status_change_end(bl,SC_STONE,INVALID_TIMER);
				if( sc->data[SC_CURSE] ) {
					status_change_end(bl,SC_CURSE,INVALID_TIMER);
					return 1;
				}
			}
			if( sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH )
				status_change_end(bl,SC_SPIRIT,INVALID_TIMER);
			break;
		case SC_INCREASEAGI:
			status_change_end(bl,SC_DECREASEAGI,INVALID_TIMER);
			if( sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH )
				status_change_end(bl,SC_SPIRIT,INVALID_TIMER);
			break;
		case SC_IMPOSITIO:
		case SC_KAAHI:
		case SC_MAGICPOWER:
		case SC_FIGHTINGSPIRIT:
		case SC_OVERED_BOOST:
			//These status changes always overwrite themselves even when a lower level is cast
			status_change_end(bl,type,INVALID_TIMER);
			break;
		case SC_QUAGMIRE:
			status_change_end(bl,SC_LOUD,INVALID_TIMER);
			status_change_end(bl,SC_CONCENTRATE,INVALID_TIMER);
			status_change_end(bl,SC_TRUESIGHT,INVALID_TIMER);
			status_change_end(bl,SC_WINDWALK,INVALID_TIMER);
			status_change_end(bl,SC_MAGNETICFIELD,INVALID_TIMER);
		//Fall through
		case SC_DECREASEAGI:
			if( type == SC_DECREASEAGI ) {
				status_change_end(bl,SC_DECREASEAGI,INVALID_TIMER);
				status_change_end(bl,SC_ADORAMUS,INVALID_TIMER);
			}
			status_change_end(bl,SC_CARTBOOST,INVALID_TIMER);
			status_change_end(bl,SC_GN_CARTBOOST,INVALID_TIMER);
		//Fall through
		case SC_DONTFORGETME:
			status_change_end(bl,SC_INCREASEAGI,INVALID_TIMER);
			status_change_end(bl,SC_ADRENALINE,INVALID_TIMER);
			status_change_end(bl,SC_ADRENALINE2,INVALID_TIMER);
			status_change_end(bl,SC_SPEARQUICKEN,INVALID_TIMER);
			status_change_end(bl,SC_TWOHANDQUICKEN,INVALID_TIMER);
			status_change_end(bl,SC_ONEHAND,INVALID_TIMER);
			status_change_end(bl,SC_MERC_QUICKEN,INVALID_TIMER);
			status_change_end(bl,SC_ACCELERATION,INVALID_TIMER);
			break;
		case SC_ONEHAND: //Removes the Aspd potion effect,as reported by Vicious [Skotlex]
			status_change_end(bl,SC_ASPDPOTION0,INVALID_TIMER);
			status_change_end(bl,SC_ASPDPOTION1,INVALID_TIMER);
			status_change_end(bl,SC_ASPDPOTION2,INVALID_TIMER);
			status_change_end(bl,SC_ASPDPOTION3,INVALID_TIMER);
			break;
		case SC_MAXOVERTHRUST: //Cancels Normal Overthrust [Skotlex]
			status_change_end(bl,SC_OVERTHRUST,INVALID_TIMER);
			break;
		case SC_MAGNIFICAT:
			status_change_end(bl,SC_OFFERTORIUM,INVALID_TIMER);
			break;
		case SC_KYRIE: //Cancels Assumptio
			status_change_end(bl,SC_ASSUMPTIO,INVALID_TIMER);
			break;
		case SC_DELUGE:
			if( sc->data[SC_FOGWALL] && sc->data[SC_BLIND] )
				status_change_end(bl,SC_BLIND,INVALID_TIMER);
			break;
		case SC_FREEZE:
			status_change_end(bl,SC_AETERNA,INVALID_TIMER);
			break;
		case SC_SILENCE:
			if( sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_SELF )
				status_change_end(bl,SC_GOSPEL,INVALID_TIMER);
			break;
		case SC_HIDING:
			status_change_end(bl,SC_CLOSECONFINE,INVALID_TIMER);
			status_change_end(bl,SC_CLOSECONFINE2,INVALID_TIMER);
		//Fall through
		case SC_CLOAKING:
		case SC_CLOAKINGEXCEED:
			status_change_end(bl,SC_HALLUCINATIONWALK,INVALID_TIMER);
			break;
		case SC_BERSERK:
			if( val3 == SC__BLOODYLUST )
				break;
			if( battle_config.berserk_cancels_buffs ) {
				status_change_end(bl,SC_ONEHAND,INVALID_TIMER);
				status_change_end(bl,SC_TWOHANDQUICKEN,INVALID_TIMER);
				status_change_end(bl,SC_CONCENTRATION,INVALID_TIMER);
				status_change_end(bl,SC_PARRYING,INVALID_TIMER);
				status_change_end(bl,SC_AURABLADE,INVALID_TIMER);
				status_change_end(bl,SC_MERC_QUICKEN,INVALID_TIMER);
			}
			break;
		case SC_ASSUMPTIO:
			status_change_end(bl,SC_KYRIE,INVALID_TIMER);
			status_change_end(bl,SC_KAITE,INVALID_TIMER);
			break;
		case SC_KAITE:
			status_change_end(bl,SC_ASSUMPTIO,INVALID_TIMER);
			break;
		case SC_CARTBOOST:
		case SC_GN_CARTBOOST: //Cancel Decrease Agi, but take no further effect [Skotlex]
			if( sc->data[SC_DECREASEAGI] ) {
				status_change_end(bl,SC_DECREASEAGI,INVALID_TIMER);
				return 0;
			}
			break;
		case SC_FUSION:
			status_change_end(bl,SC_SPIRIT,INVALID_TIMER);
			break;
		//NPC_CHANGEUNDEAD will debuff Blessing and Agi Up
		case SC_CHANGEUNDEAD:
			status_change_end(bl,SC_BLESSING,INVALID_TIMER);
			status_change_end(bl,SC_INCREASEAGI,INVALID_TIMER);
			break;
		case SC_STRFOOD:
		case SC_AGIFOOD:
		case SC_VITFOOD:
		case SC_INTFOOD:
		case SC_DEXFOOD:
		case SC_LUKFOOD:
			status_change_end(bl,type,INVALID_TIMER);
			break;
		case SC_FOOD_STR_CASH:
		case SC_FOOD_AGI_CASH:
		case SC_FOOD_VIT_CASH:
		case SC_FOOD_INT_CASH:
		case SC_FOOD_DEX_CASH:
		case SC_FOOD_LUK_CASH:
			if( type == SC_FOOD_STR_CASH )
				status_change_end(bl,SC_STRFOOD,INVALID_TIMER);
			if( type == SC_FOOD_AGI_CASH )
				status_change_end(bl,SC_AGIFOOD,INVALID_TIMER);
			if( type == SC_FOOD_VIT_CASH )
				status_change_end(bl,SC_VITFOOD,INVALID_TIMER);
			if( type == SC_FOOD_INT_CASH )
				status_change_end(bl,SC_INTFOOD,INVALID_TIMER);
			if( type == SC_FOOD_DEX_CASH )
				status_change_end(bl,SC_DEXFOOD,INVALID_TIMER);
			if( type == SC_FOOD_LUK_CASH )
				status_change_end(bl,SC_LUKFOOD,INVALID_TIMER);
			status_change_end(bl,type,INVALID_TIMER);
			break;
		case SC_FEAR:
			status_change_end(bl,SC_BLIND,INVALID_TIMER);
			break;
		case SC_ADORAMUS:
			status_change_end(bl,SC_DECREASEAGI,INVALID_TIMER);
			break;
		case SC_MARSHOFABYSS:
			status_change_end(bl,SC_INCREASEAGI,INVALID_TIMER);
			status_change_end(bl,SC_WINDWALK,INVALID_TIMER);
			status_change_end(bl,SC_ASPDPOTION0,INVALID_TIMER);
			status_change_end(bl,SC_ASPDPOTION1,INVALID_TIMER);
			status_change_end(bl,SC_ASPDPOTION2,INVALID_TIMER);
			status_change_end(bl,SC_ASPDPOTION3,INVALID_TIMER);
			break;
		case SC_SWINGDANCE:
		case SC_SYMPHONYOFLOVER:
		case SC_MOONLITSERENADE:
		case SC_RUSHWINDMILL:
		case SC_ECHOSONG:
		case SC_HARMONIZE: //Group A doesn't overlap
			if( type != SC_SWINGDANCE )
				status_change_end(bl,SC_SWINGDANCE,INVALID_TIMER);
			if( type != SC_SYMPHONYOFLOVER )
				status_change_end(bl,SC_SYMPHONYOFLOVER,INVALID_TIMER);
			if( type != SC_MOONLITSERENADE )
				status_change_end(bl,SC_MOONLITSERENADE,INVALID_TIMER);
			if( type != SC_RUSHWINDMILL )
				status_change_end(bl,SC_RUSHWINDMILL,INVALID_TIMER);
			if( type != SC_ECHOSONG )
				status_change_end(bl,SC_ECHOSONG,INVALID_TIMER);
			if( type != SC_HARMONIZE )
				status_change_end(bl,SC_HARMONIZE,INVALID_TIMER);
			break;
		case SC_VOICEOFSIREN:
		case SC_DEEPSLEEP:
		case SC_SIRCLEOFNATURE:
		case SC_GLOOMYDAY:
		case SC_SONGOFMANA:
		case SC_DANCEWITHWUG:
		case SC_SATURDAYNIGHTFEVER:
		case SC_LERADSDEW:
		case SC_MELODYOFSINK:
		case SC_BEYONDOFWARCRY:
		case SC_UNLIMITEDHUMMINGVOICE: //Group B
			if( type != SC_VOICEOFSIREN )
				status_change_end(bl,SC_VOICEOFSIREN,INVALID_TIMER);
			if( type != SC_DEEPSLEEP )
				status_change_end(bl,SC_DEEPSLEEP,INVALID_TIMER);
			if( type != SC_SIRCLEOFNATURE )
				status_change_end(bl,SC_SIRCLEOFNATURE,INVALID_TIMER);
			if( type != SC_LERADSDEW )
				status_change_end(bl,SC_LERADSDEW,INVALID_TIMER);
			if( type != SC_MELODYOFSINK )
				status_change_end(bl,SC_MELODYOFSINK,INVALID_TIMER);
			if( type != SC_BEYONDOFWARCRY )
				status_change_end(bl,SC_BEYONDOFWARCRY,INVALID_TIMER);
			if( type != SC_UNLIMITEDHUMMINGVOICE )
				status_change_end(bl,SC_UNLIMITEDHUMMINGVOICE,INVALID_TIMER);
			if( type != SC_GLOOMYDAY )
				status_change_end(bl,SC_GLOOMYDAY,INVALID_TIMER);
			if( type != SC_SONGOFMANA )
				status_change_end(bl,SC_SONGOFMANA,INVALID_TIMER);
			if( type != SC_DANCEWITHWUG )
				status_change_end(bl,SC_DANCEWITHWUG,INVALID_TIMER);
			if( type != SC_SATURDAYNIGHTFEVER && sc->data[SC_SATURDAYNIGHTFEVER] ) {
				sc->data[SC_SATURDAYNIGHTFEVER]->val2 = 0;
				status_change_end(bl,SC_SATURDAYNIGHTFEVER,INVALID_TIMER);
			}
			break;
		case SC_REFLECTSHIELD:
			status_change_end(bl,SC_REFLECTDAMAGE,INVALID_TIMER);
			break;
		case SC_REFLECTDAMAGE:
			status_change_end(bl,SC_REFLECTSHIELD,INVALID_TIMER);
			break;
		case SC_SHIELDSPELL_DEF:
		case SC_SHIELDSPELL_MDEF:
		case SC_SHIELDSPELL_REF:
			status_change_end(bl,SC_MAGNIFICAT,INVALID_TIMER);
			if( type != SC_SHIELDSPELL_DEF )
				status_change_end(bl,SC_SHIELDSPELL_DEF,INVALID_TIMER);
			if( type != SC_SHIELDSPELL_MDEF )
				status_change_end(bl,SC_SHIELDSPELL_MDEF,INVALID_TIMER);
			if( type != SC_SHIELDSPELL_REF )
				status_change_end(bl,SC_SHIELDSPELL_REF,INVALID_TIMER);
			break;
		case SC_BANDING:
			status_change_end(bl,SC_PRESTIGE,INVALID_TIMER);
			break;
		case SC_GT_ENERGYGAIN:
		case SC_GT_CHANGE:
		case SC_GT_REVITALIZE:
			if( type != SC_GT_REVITALIZE )
				status_change_end(bl,SC_GT_REVITALIZE,INVALID_TIMER);
			if( type != SC_GT_ENERGYGAIN )
				status_change_end(bl,SC_GT_ENERGYGAIN,INVALID_TIMER);
			if( type != SC_GT_CHANGE )
				status_change_end(bl,SC_GT_CHANGE,INVALID_TIMER);
			break;
		case SC_WARMER:
			status_change_end(bl,SC_FREEZE,INVALID_TIMER);
			status_change_end(bl,SC_FREEZING,INVALID_TIMER);
			status_change_end(bl,SC_CRYSTALIZE,INVALID_TIMER);
			break;
		case SC_INVINCIBLE:
			status_change_end(bl,SC_INVINCIBLEOFF,INVALID_TIMER);
			break;
		case SC_INVINCIBLEOFF:
			status_change_end(bl,SC_INVINCIBLE,INVALID_TIMER);
			break;
		case SC_OFFERTORIUM:
			status_change_end(bl,SC_MAGNIFICAT,INVALID_TIMER);
			status_change_end(bl,SC_BLIND,INVALID_TIMER);
			status_change_end(bl,SC_CURSE,INVALID_TIMER);
			status_change_end(bl,SC_POISON,INVALID_TIMER);
			status_change_end(bl,SC_HALLUCINATION,INVALID_TIMER);
			status_change_end(bl,SC_CONFUSION,INVALID_TIMER);
			status_change_end(bl,SC_BLEEDING,INVALID_TIMER);
			status_change_end(bl,SC_BURNING,INVALID_TIMER);
			status_change_end(bl,SC_FREEZING,INVALID_TIMER);
			status_change_end(bl,SC_TOXIN,INVALID_TIMER);
			status_change_end(bl,SC_PARALYSE,INVALID_TIMER);
			status_change_end(bl,SC_VENOMBLEED,INVALID_TIMER);
			status_change_end(bl,SC_MAGICMUSHROOM,INVALID_TIMER);
			status_change_end(bl,SC_DEATHHURT,INVALID_TIMER);
			status_change_end(bl,SC_PYREXIA,INVALID_TIMER);
			status_change_end(bl,SC_LEECHESEND,INVALID_TIMER);
			status_change_end(bl,SC_MANDRAGORA,INVALID_TIMER);
			break;
		case SC_KINGS_GRACE:
			status_change_end(bl,SC_POISON,INVALID_TIMER);
			status_change_end(bl,SC_BLIND,INVALID_TIMER);
			status_change_end(bl,SC_FREEZE,INVALID_TIMER);
			status_change_end(bl,SC_STONE,INVALID_TIMER);
			status_change_end(bl,SC_STUN,INVALID_TIMER);
			status_change_end(bl,SC_SLEEP,INVALID_TIMER);
			status_change_end(bl,SC_BLEEDING,INVALID_TIMER);
			status_change_end(bl,SC_CURSE,INVALID_TIMER);
			status_change_end(bl,SC_CONFUSION,INVALID_TIMER);
			status_change_end(bl,SC_HALLUCINATION,INVALID_TIMER);
			status_change_end(bl,SC_SILENCE,INVALID_TIMER);
			status_change_end(bl,SC_BURNING,INVALID_TIMER);
			status_change_end(bl,SC_CRYSTALIZE,INVALID_TIMER);
			status_change_end(bl,SC_FREEZING,INVALID_TIMER);
			status_change_end(bl,SC_DEEPSLEEP,INVALID_TIMER);
			status_change_end(bl,SC_FEAR,INVALID_TIMER);
			status_change_end(bl,SC_MANDRAGORA,INVALID_TIMER);
			status_change_end(bl,SC_DEVOTION,INVALID_TIMER);
			break;
		case SC_ALMIGHTY:
			status_change_end(bl,SC_FOOD_STR_CASH,INVALID_TIMER);
			status_change_end(bl,SC_FOOD_AGI_CASH,INVALID_TIMER);
			status_change_end(bl,SC_FOOD_VIT_CASH,INVALID_TIMER);
			status_change_end(bl,SC_FOOD_INT_CASH,INVALID_TIMER);
			status_change_end(bl,SC_FOOD_DEX_CASH,INVALID_TIMER);
			status_change_end(bl,SC_FOOD_LUK_CASH,INVALID_TIMER);
			break;
		case SC_SONIC_CLAW_POSTDELAY:
		case SC_SILVERVEIN_RUSH_POSTDELAY:
		case SC_MIDNIGHT_FRENZY_POSTDELAY:
		case SC_TINDER_BREAKER_POSTDELAY:
		case SC_CBC_POSTDELAY:
			if( sc->data[type] ) //Don't remove status that was just activated
				break;
			status_change_end(bl,SC_SONIC_CLAW_POSTDELAY,INVALID_TIMER);
			status_change_end(bl,SC_SILVERVEIN_RUSH_POSTDELAY,INVALID_TIMER);
			status_change_end(bl,SC_MIDNIGHT_FRENZY_POSTDELAY,INVALID_TIMER);
			status_change_end(bl,SC_TINDER_BREAKER_POSTDELAY,INVALID_TIMER);
			status_change_end(bl,SC_CBC_POSTDELAY,INVALID_TIMER);
			break;
		case SC_RAY_OF_PROTECTION:
			status_change_end(bl,SC_STUN,INVALID_TIMER);
			status_change_end(bl,SC_SLEEP,INVALID_TIMER);
			status_change_end(bl,SC_CURSE,INVALID_TIMER);
			status_change_end(bl,SC_STONE,INVALID_TIMER);
			status_change_end(bl,SC_POISON,INVALID_TIMER);
			status_change_end(bl,SC_BLIND,INVALID_TIMER);
			status_change_end(bl,SC_SILENCE,INVALID_TIMER);
			status_change_end(bl,SC_BLEEDING,INVALID_TIMER);
			status_change_end(bl,SC_CONFUSION,INVALID_TIMER);
			status_change_end(bl,SC_FREEZE,INVALID_TIMER);
			break;
		case SC_GROOMING:
			status_change_end(bl,SC_STUN,INVALID_TIMER);
			status_change_end(bl,SC_FREEZE,INVALID_TIMER);
			status_change_end(bl,SC_STONE,INVALID_TIMER);
			status_change_end(bl,SC_SLEEP,INVALID_TIMER);
			status_change_end(bl,SC_SILENCE,INVALID_TIMER);
			status_change_end(bl,SC_BLEEDING,INVALID_TIMER);
			status_change_end(bl,SC_POISON,INVALID_TIMER);
			status_change_end(bl,SC_FEAR,INVALID_TIMER);
			status_change_end(bl,SC_MANDRAGORA,INVALID_TIMER);
			status_change_end(bl,SC_CRYSTALIZE,INVALID_TIMER);
			status_change_end(bl,SC_FREEZING,INVALID_TIMER);
			break;
		case SC_CHILL:
			status_change_end(bl,SC_BURNT,INVALID_TIMER);
			break;
		case SC_EL_OFFENSIVE:
		case SC_EL_DEFENSIVE:
		case SC_EL_PASSIVE:
			if( sc->data[type] ) {
				status_change_end(bl,type,INVALID_TIMER);
				return 0;
			}
			status_change_end(bl,SC_EL_WAIT,INVALID_TIMER);
			status_change_end(bl,SC_EL_OFFENSIVE,INVALID_TIMER);
			status_change_end(bl,SC_EL_DEFENSIVE,INVALID_TIMER);
			status_change_end(bl,SC_EL_PASSIVE,INVALID_TIMER);
			break;
	}

	if( (sce = sc->data[type]) ) { //Check for overlapping fails
		switch( type ) {
			case SC_MERC_FLEEUP:
			case SC_MERC_ATKUP:
			case SC_MERC_HPUP:
			case SC_MERC_SPUP:
			case SC_MERC_HITUP:
				if( sce->val1 > val1 )
					val1 = sce->val1;
				break;
			case SC_ADRENALINE:
			case SC_ADRENALINE2:
			case SC_WEAPONPERFECTION:
			case SC_OVERTHRUST:
				if( sce->val2 > val2 )
					return 0;
				break;
			case SC_S_LIFEPOTION:
			case SC_L_LIFEPOTION:
			case SC_BOSSMAPINFO:
			case SC_STUN:
			case SC_SLEEP:
			case SC_POISON:
			case SC_CURSE:
			case SC_SILENCE:
			case SC_CONFUSION:
			case SC_BLIND:
			case SC_BLEEDING:
			case SC_ANKLE:
			case SC_DPOISON:
			case SC_BERSERK:
			case SC_BASILICA:
			case SC_CLOSECONFINE2: //Can't be re-closed in
			case SC_MARIONETTE:
			case SC_MARIONETTE2:
			case SC_NOCHAT:
			case SC_FEAR:
			case SC_BURNING:
			case SC_FREEZING:
			case SC_ABUNDANCE:
			case SC_ADORAMUS:
			case SC_WHITEIMPRISON:
			case SC_TOXIN:
			case SC_PARALYSE:
			case SC_VENOMBLEED:
			case SC_MAGICMUSHROOM:
			case SC_DEATHHURT:
			case SC_PYREXIA:
			case SC_OBLIVIONCURSE:
			case SC_LEECHESEND:
			case SC_BITE:
			case SC_ELECTRICSHOCKER:
			case SC_BANDING_DEFENCE:
			case SC__ENERVATION:
			case SC__GROOMY:
			case SC__IGNORANCE:
			case SC__LAZINESS:
			case SC__UNLUCKY:
			case SC__WEAKNESS:
			case SC_CURSEDCIRCLE_TARGET:
			case SC_DEEPSLEEP:
			case SC_NETHERWORLD:
			case SC_CRYSTALIZE:
			case SC_THORNSTRAP:
			case SC_B_TRAP:
			case SC_DEFSET_NUM:
			case SC_MDEFSET_NUM:
			case SC_NORECOVER_STATE:
			case SC_REUSE_LIMIT_A:
			case SC_REUSE_LIMIT_B:
			case SC_REUSE_LIMIT_C:
			case SC_REUSE_LIMIT_D:
			case SC_REUSE_LIMIT_E:
			case SC_REUSE_LIMIT_F:
			case SC_REUSE_LIMIT_G:
			case SC_REUSE_LIMIT_H:
			case SC_REUSE_MILLENNIUMSHIELD:
			case SC_REUSE_CRUSHSTRIKE:
			case SC_REUSE_REFRESH:
			case SC_REUSE_STORMBLAST:
			case SC_ALL_RIDING_REUSE_LIMIT:
			case SC_REUSE_LIMIT_MTF:
			case SC_REUSE_LIMIT_ECL:
			case SC_REUSE_LIMIT_RECALL:
			case SC_REUSE_LIMIT_ASPD_POTION:
				return 0;
			case SC_COMBO:
			case SC_DANCING:
			case SC_DEVOTION:
			case SC_ASPDPOTION0:
			case SC_ASPDPOTION1:
			case SC_ASPDPOTION2:
			case SC_ASPDPOTION3:
			case SC_ATKPOTION:
			case SC_MATKPOTION:
			case SC_ENCHANTARMS:
			case SC_ARMOR_RESIST:
			case SC_ATTHASTE_CASH:
			case SC_SHAPESHIFT:
			case SC_PROPERTYWALK:
			case SC_PUSH_CART:
			case SC_C_MARKER:
			case SC_H_MINE:
				break;
			case SC_GOSPEL:
				if( sce->val4 == BCT_SELF )
					return 0;  //Must not override a casting gospel char
				if( sce->val1 > val1 )
					return 1;
				break;
			case SC_ENDURE:
				if( sce->val4 && !val4 )
					return 1;
				if( sce->val1 > val1 )
					return 1;
				break;
			case SC_JAILED: //When a player is already jailed, do not edit the jail data
				val2 = sce->val2;
				val3 = sce->val3;
				val4 = sce->val4;
				break;
			case SC_LEADERSHIP:
			case SC_GLORYWOUNDS:
			case SC_SOULCOLD:
			case SC_HAWKEYES:
				if( sce->val4 && !val4 ) //You cannot override master guild aura
					return 0;
				break;
			case SC_JOINTBEAT:
				val2 |= sce->val2; //Stackable ailments
			//Fall through
			default:
				if( sce->val1 > val1 )
					return 1; //Return true to not mess up skill animations [Skotlex]
				break;
		}
	}

	calc_flag = StatusChangeFlagTable[type];
	icon = StatusIconChangeTable[type];

	if( !(flag&SCFLAG_LOADED) ) { //Do not parse val settings when loading SCs
		switch( type ) {
			case SC_DECREASEAGI:
			case SC_INCREASEAGI:
			case SC_ADORAMUS:
				val2 = 2 + val1; //Agi change
				break;
			case SC_ENDURE:
				val2 = 7; //Hit-count [Celest]
				if( !(flag&SCFLAG_NOAVOID) && (bl->type&(BL_PC|BL_MER)) && !map_flag_gvg2(bl->m) && !map[bl->m].flag.battleground && !val4 ) {
					struct map_session_data *tsd = NULL;

					if( sd ) {
						int i;

						for( i = 0; i < MAX_DEVOTION; i++ ) {
							if( sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) )
								status_change_start(src,&tsd->bl,type,10000,val1,val2,val3,val4,tick,SCFLAG_NOAVOID|SCFLAG_NOICON);
						}
						if( (tsd = map_id2sd(sd->shadowform_id)) ) //Set 'val3' as flag status inherit from Shadow Form [exneval]
							status_change_start(src,&tsd->bl,type,10000,val1,val2,1,val4,tick,SCFLAG_NOAVOID|SCFLAG_NOICON);
					} else if( bl->type == BL_MER && ((TBL_MER *)bl)->devotion_flag && (tsd = ((TBL_MER *)bl)->master) )
						status_change_start(src,&tsd->bl,type,10000,val1,val2,val3,val4,tick,SCFLAG_NOAVOID);
				}
				break;
			case SC_AUTOBERSERK:
				if( status->hp < status->max_hp>>2 && (!sc->data[SC_PROVOKE] || !sc->data[SC_PROVOKE]->val2) )
					sc_start4(src,bl,SC_PROVOKE,100,10,1,0,0,60000);
				break;
			case SC_SIGNUMCRUCIS:
				val2 = 10 + 4 * val1; //Def reduction
				clif_emotion(bl,E_SWT);
				break;
			case SC_MAXIMIZEPOWER:
				tick_time = val2 = tick;
				break;
			case SC_EDP: //[Celest]
				val2 = val1 + 2; //Chance to Poison enemies
#ifndef RENEWAL
				val3 = 50 * (val1 + 1); //Damage increase (+50+50*lv%)
#endif
				if( sd ) //[Ind] - iROwiki says each level increases its duration by 3 seconds
					tick += pc_checkskill(sd,GC_RESEARCHNEWPOISON) * 3000;
				break;
			case SC_POISONREACT:
				val2 = (val1 + 1) / 2 + val1 / 10; //Number of counters [Skotlex]
				val3 = 50; //+5*val1; //Chance to counter [Skotlex]
				break;
			case SC_MAGICROD:
				val2 = val1 * 20; //SP gained
				break;
			case SC_KYRIE:
				if( !val4 ) { //Kyrie Eleison
					val2 = status->max_hp * (val1 * 2 + 10) / 100; //% MaxHP to absorb
					val3 = val1 / 2 + 5; //Hits
				} else if( val4 == RL_P_ALTER ) { //Platinum Alter
					val2 = status->max_hp * val1 * 5 / 100;
					val3 = 3 + val1;
				} else { //Praefatio
					val2 = status->max_hp * (val1 * 2 + 10 + val4 * 2) / 100;
					val3 = 6 + val1;
				}
				break;
			case SC_MAGICPOWER:
				val2 = 1; //Lasts 1 invocation
				val3 = 5 * val1; //% Matk increase
				val4 = 0; //0 = Ready to be used, 1 = Activated and running
				break;
			case SC_SACRIFICE:
				val2 = 5; //Lasts 5 hits
				break;
			case SC_ENCPOISON:
				val2 = 250 + 50 * val1; //Poisoning chance (2.5 + 0.5%) in 1/10000 rate
			//Fall through
			case SC_ASPERSIO:
			case SC_FIREWEAPON:
			case SC_WATERWEAPON:
			case SC_WINDWEAPON:
			case SC_EARTHWEAPON:
			case SC_SHADOWWEAPON:
			case SC_GHOSTWEAPON:
				skill_enchant_elemental_end(bl,type);
				break;
			case SC_ELEMENTALCHANGE:
				//val1 : Element lvl (if called by skill lvl 1, takes random value between 1 and 4)
				//val2 : Element (when no element, random one is picked)
				//val3 : 0 = Called by skill 1 = Called by script (fixed level)
				if( !val2 )
					val2 = rnd()%ELE_ALL;
				if( val1 == 1 && !val3 )
					val1 = 1 + rnd()%4;
				else if( val1 > 4 )
					val1 = 4; //Max level
				val3 = 0; //Not need to keep this info
				break;
			case SC_PROVIDENCE:
				val2 = val1 * 5; //Race/Ele resist
				break;
			case SC_REFLECTSHIELD:
				if( !(flag&SCFLAG_NOAVOID) ) {
					val2 = 10 + val1 * 3; //% Dmg reflected
					//val4 used to mark if reflect shield is an inheritance bonus from Devotion
					if( bl->type&(BL_PC|BL_MER) ) {
						struct map_session_data *tsd;

						if( sd ) {
							int i;

							for( i = 0; i < MAX_DEVOTION; i++ )
								if( sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) )
									status_change_start(src,&tsd->bl,type,10000,val1,val2,0,1,tick,SCFLAG_NOAVOID|SCFLAG_NOICON);
						} else if( bl->type == BL_MER && ((TBL_MER *)bl)->devotion_flag && (tsd = ((TBL_MER *)bl)->master) )
							status_change_start(src,&tsd->bl,type,10000,val1,val2,0,1,tick,SCFLAG_NOAVOID|SCFLAG_NOICON);
					}
				}
				break;
			case SC_STRIPWEAPON:
				if( !sd ) //Watk reduction
					val2 = 25;
				break;
			case SC_STRIPSHIELD:
				if( !sd ) //Def reduction
					val2 = 15;
				break;
			case SC_STRIPARMOR:
				if( !sd ) //Vit reduction
					val2 = 40;
				break;
			case SC_STRIPHELM:
				if( !sd ) //Int reduction
					val2 = 40;
				break;
			case SC_AUTOSPELL:
				//Val1 Skill Lv of Autospell
				//Val2 Skill Id to cast
				//Val3 Max Lv to cast
				val4 = 5 + val1 * 2; //Chance of casting
				break;
			case SC_VOLCANO: {
					int8 enchant_eff[] = { 10,14,17,19,20 }; //Enchant addition
					uint8 i = max((val1 - 1)%5, 0);

					val2 = val1 * 10; //Atk increase
#ifndef RENEWAL
					if( status->def_ele != ELE_FIRE )
						val2 = 0;
#endif
					val3 = enchant_eff[i];
				}
				break;
			case SC_VIOLENTGALE: {
					int8 enchant_eff[] = { 10,14,17,19,20 }; //Enchant addition
					uint8 i = max((val1 - 1)%5, 0);

					val2 = val1 * 3; //Flee increase
#ifndef RENEWAL
					if( status->def_ele != ELE_WIND )
						val2 = 0;
#endif
					val3 = enchant_eff[i];
				}
				break;
			case SC_DELUGE: {
					int8 deluge_eff[]  = {  5, 9,12,14,15 }; //HP addition rate n/100
					int8 enchant_eff[] = { 10,14,17,19,20 }; //Enchant addition
					uint8 i = max((val1 - 1)%5, 0);

					val2 = deluge_eff[i]; //HP increase
#ifndef RENEWAL
					if( status->def_ele != ELE_WATER )
						val2 = 0;
#endif
					val3 = enchant_eff[i];
				}
				break;
			case SC_SUITON:
				if( !val2 || (sd && (sd->class_&MAPID_BASEMASK) == MAPID_NINJA) ) { //No penalties
					val2 = 0;
					val3 = 0;
					break;
				}
				val2 = (val1 < 2) ? 0 : (val1 > 1 && val1 < 5) ? 3 : (val1 > 4 && val1 < 8) ? 5 : 8 ; //Agi penalty
				val3 = 50; //Walk speed penalty
				//Suiton is a special case, stop effect is forced and only happens when target enters it
				if( !unit_blown_immune(bl,0x1) )
					unit_stop_walking(bl,USW_FIXPOS|USW_FORCE_STOP);
				break;
			case SC_ONEHAND:
			case SC_TWOHANDQUICKEN:
				val2 = 300;
				if( val1 > 10 ) //For boss casted skills [Skotlex]
					val2 += 20 * (val1 - 10);
				break;
			case SC_MERC_QUICKEN:
				val2 = 300;
				break;
#ifndef RENEWAL_ASPD
			case SC_SPEARQUICKEN:
				val2 = 200 + 10 * val1;
				break;
#endif
			case SC_DANCING:
				//val1 : Skill ID + lvl
				//val2 : Skill Group of the Dance
				//val3 : Brings the skill_lv (merged into val1 here)
				//val4 : Partner
				if( val1 == CG_MOONLIT )
					clif_status_change(bl,SI_MOON,1,tick,0,0,0);
				val1 |= (val3<<16);
				tick_time = 1000;
				val3 = tick / tick_time; //Tick duration
				break;
			case SC_LONGING:
				val2 = 500 - 100 * val1; //Aspd penalty
				break;
			case SC_EXPLOSIONSPIRITS:
				val2 = 75 + 25 * val1; //Cri bonus
				break;
			case SC_ASPDPOTION0:
			case SC_ASPDPOTION1:
			case SC_ASPDPOTION2:
			case SC_ASPDPOTION3:
				val2 = 50 * (2 + type - SC_ASPDPOTION0);
				break;
			case SC_ATTHASTE_CASH:
				val2 = 50 * val1; //Pre-re customize
				break;
			case SC_NOCHAT:
				//A hardcoded interval of 60 seconds is expected, as the time that SC_NOCHAT uses is defined by
				//mmocharstatus.manner, each negative point results in 1 minute with this status activated
				//This is done this way because the message that the client displays is hardcoded, and only
				//shows how many minutes are remaining [Panikon]
				tick = 60000;
				val1 = battle_config.manner_system; //Mute filters
				if( sd ) {
					clif_changestatus(sd,SP_MANNER,sd->status.manner);
					clif_updatestatus(sd,SP_MANNER);
				}
				break;
			case SC_STONE:
				val3 = max(val3,100); //Incubation time
				val4 = max(tick - val3,100); //Petrify time
				tick = val3;
				calc_flag = 0; //Actual status changes take effect on petrified state
				break;
			case SC_DPOISON: {
					//Get 15% penalty of your life as long as it doesn't brings life below 25%
					//But there's an condition that when your life below the penalty, it will kill you immediately
					int diff = status->max_hp * 15 / 100;

					if( status->hp < diff ) {
						status_percent_damage(NULL,bl,-15,0,true);
						return 0;
					} else if( status->hp > status->max_hp>>2 )
						status_zap(bl,diff,0);
				}
			//Fall through
			case SC_POISON:
			case SC_BLEEDING:
			case SC_BURNING:
			case SC_TOXIN:
			case SC_MAGICMUSHROOM:
			case SC_LEECHESEND:
			case SC_SV_ROOTTWIST:
				tick_time = status_get_sc_interval(type);
				val4 = tick - tick_time; //Remaining time
				break;
			case SC_PYREXIA:
				//Causes blind for duration of pyrexia, unreducable and unavoidable, but can be healed with e.g. green potion
				status_change_start(src,bl,SC_BLIND,10000,val1,0,0,0,tick,SCFLAG_NOAVOID|SCFLAG_FIXEDTICK|SCFLAG_FIXEDRATE);
				tick_time = status_get_sc_interval(type);
				val4 = tick - tick_time;
				break;
			case SC_CONFUSION:
				if( !val4 )
					clif_emotion(bl,E_WHAT);
				break;
			case SC_S_LIFEPOTION:
			case SC_L_LIFEPOTION:
				if( !val1 )
					return 0;
				//val1 = heal percent/amout
				//val2 = seconds between heals
				//val4 = total of heals
				tick_time = val2;
				val4 = tick / tick_time;
				break;
			case SC_BOSSMAPINFO:
				if( sd ) {
					struct mob_data *boss_md = map_getmob_boss(bl->m); //Search for Boss on this Map

					if( !boss_md ) { //No MVP on this map
						clif_bossmapinfo(sd,NULL,BOSS_INFO_NOT);
						return 0; //No need to start SC
					}
					val1 = boss_md->bl.id;
					tick_time = 1000;
					val4 = tick / tick_time;
				}
				break;
			case SC_HIDING:
				tick_time = 1000;
				val2 = tick / tick_time;
				val3 = 0; //Unused, previously speed adjustment
				val4 = val1 + 3; //Seconds before SP substraction happen
				break;
			case SC_CHASEWALK:
				tick_time = val2 = tick; //Interval at which SP is drained
				val3 = 35 - 5 * val1; //Speed adjustment
				if( sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_ROGUE )
					val3 -= 40;
				val4 = 10 + val1 * 2; //SP cost
				if( map_flag_gvg2(bl->m) || map[bl->m].flag.battleground )
					val4 *= 5;
				break;
			case SC_CLOAKING:
				if( !sd ) //Monsters should be able to walk with no penalties [Skotlex]
					val1 = 10;
				tick_time = val2 = tick; //SP consumption rate
				val3 = 0; //Unused, previously walk speed adjustment
				//val4&1 signals the presence of a wall
				//val4&2 makes cloak not end on normal attacks [Skotlex]
				//val4&4 makes cloak not end on using skills
				if( bl->type == BL_PC || (bl->type == BL_MOB && ((TBL_MOB *)bl)->special_state.clone) ) //Standard cloaking
					val4 |= battle_config.pc_cloak_check_type&7;
				else
					val4 |= battle_config.monster_cloak_check_type&7;
				break;
			case SC_SIGHT: //Splash status
			case SC_RUWACH:
			case SC_SIGHTBLASTER:
				val3 = skill_get_splash(val2,val1); //val2 should bring the skill_id
				tick_time = 20;
				val2 = tick / tick_time;
				break;
			case SC_AUTOGUARD:
				if( !(flag&SCFLAG_NOAVOID) ) {
					int i;

					for( i = val2 = 0; i < val1; i++ ) {
						int t = 5 - (i>>1);

						val2 += (t < 0) ? 1 : t;
					}
					if( bl->type&(BL_PC|BL_MER) ) {
						struct map_session_data *tsd = NULL;

						if( sd ) {
							for( i = 0; i < MAX_DEVOTION; i++ )
								if( sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) )
									status_change_start(src,&tsd->bl,type,10000,val1,val2,0,0,tick,SCFLAG_NOAVOID|SCFLAG_NOICON);
						} else if( bl->type == BL_MER && ((TBL_MER *)bl)->devotion_flag && (tsd = ((TBL_MER *)bl)->master) )
							status_change_start(src,&tsd->bl,type,10000,val1,val2,0,0,tick,SCFLAG_NOAVOID|SCFLAG_NOICON);
					}
				}
				break;
			case SC_DEFENDER:
				if( !(flag&SCFLAG_NOAVOID) ) {
					val2 = 5 + 15 * val1; //Damage reduction
					val3 = 0; //Unused, previously speed adjustment
					val4 = 250 - 50 * val1; //Aspd adjustment
					if( sd ) {
						struct map_session_data *tsd = NULL;
						int i;

						for( i = 0; i < MAX_DEVOTION; i++ ) //See if there are devoted characters, and pass the status to them [Skotlex]
							if( sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) )
								status_change_start(src,&tsd->bl,type,10000,val1,val2,val3,val4,tick,SCFLAG_NOAVOID|SCFLAG_NOICON);
					}
				}
				break;
			case SC_TENSIONRELAX:
				if( sd ) {
					pc_setsit(sd);
					skill_sit(sd,true);
					clif_sitting(&sd->bl);
				}
				val2 = 12; //SP cost
				tick_time = 10000;
				val3 = tick / tick_time;
				break;
			case SC_PARRYING:
				val2 = 20 + val1 * 3; //Block Chance
				break;
			case SC_WINDWALK:
				val2 = (val1 + 1) / 2; //Flee bonus is 1/1/2/2/3/3/4/4/5/5
				break;
			case SC_JOINTBEAT:
				if( val2&BREAK_NECK )
					sc_start(src,bl,SC_BLEEDING,100,val1,skill_get_time2(status_sc2skill(type),val1));
				break;
			case SC_BERSERK:
				if( val3 == SC__BLOODYLUST )
					sc_start(src,bl,(sc_type)val3,100,val1,tick + 100);
				else
					sc_start4(src,bl,SC_ENDURE,100,10,0,0,1,tick);
				//HP healing is performing after the calc_status call
				//val2 holds HP penalty
				//val3 holds HP drain interval
				//val4 holds skill duration
				tick_time = val3 = skill_get_time2(status_sc2skill(type),val1);
				val4 = tick / tick_time;
				break;
			case SC_GOSPEL:
				if( val4 == BCT_SELF ) { //Self effect
					tick_time = 10000;
					val2 = tick / tick_time;
					status_change_clear_buffs(bl,SCCB_BUFFS|SCCB_DEBUFFS|SCCB_CHEM_PROTECT|SCCB_REFRESH,0); //Remove buffs/debuffs
				}
				break;
			case SC_MARIONETTE: {
					int stat;

					val3 = 0;
					val4 = 0;
					stat = (sd ? sd->status.str : status_get_base_status(bl)->str) / 2; val3 |= cap_value(stat,0,0xFF)<<16;
					stat = (sd ? sd->status.agi : status_get_base_status(bl)->agi) / 2; val3 |= cap_value(stat,0,0xFF)<<8;
					stat = (sd ? sd->status.vit : status_get_base_status(bl)->vit) / 2; val3 |= cap_value(stat,0,0xFF);
					stat = (sd ? sd->status.int_ : status_get_base_status(bl)->int_) / 2; val4 |= cap_value(stat,0,0xFF)<<16;
					stat = (sd ? sd->status.dex : status_get_base_status(bl)->dex) / 2; val4 |= cap_value(stat,0,0xFF)<<8;
					stat = (sd ? sd->status.luk : status_get_base_status(bl)->luk) / 2; val4 |= cap_value(stat,0,0xFF);
				}
				break;
			case SC_MARIONETTE2: {
					int stat, max_stat;
					//Fetch caster information
					struct block_list *pbl = map_id2bl(val1);
					struct status_change *psc = (pbl ? status_get_sc(pbl) : NULL);
					struct status_change_entry *psce = (psc ? psc->data[SC_MARIONETTE] : NULL);

					if( !psce )
						return 0;

					val3 = 0;
					val4 = 0;
					max_stat = battle_config.max_parameter; //Cap to 99 (default)
					stat = (psce->val3>>16)&0xFF; stat = min(stat,max_stat - status_get_base_status(bl)->str); val3 |= cap_value(stat,0,0xFF)<<16;
					stat = (psce->val3>>8)&0xFF; stat = min(stat,max_stat - status_get_base_status(bl)->agi); val3 |= cap_value(stat,0,0xFF)<<8;
					stat = (psce->val3>>0)&0xFF; stat = min(stat,max_stat - status_get_base_status(bl)->vit); val3 |= cap_value(stat,0,0xFF);
					stat = (psce->val4>>16)&0xFF; stat = min(stat,max_stat - status_get_base_status(bl)->int_); val4 |= cap_value(stat,0,0xFF)<<16;
					stat = (psce->val4>>8)&0xFF; stat = min(stat,max_stat - status_get_base_status(bl)->dex); val4 |= cap_value(stat,0,0xFF)<<8;
					stat = (psce->val4>>0)&0xFF; stat = min(stat,max_stat - status_get_base_status(bl)->luk); val4 |= cap_value(stat,0,0xFF);
				}
				break;
			case SC_SPIRIT: //1st Transcendent Spirit works similar to Marionette Control
				if( sd && val2 == SL_HIGH ) {
					int stat, max_stat;

					val3 = 0;
					val4 = 0;
					max_stat = (status_get_lv(bl) - 10 < 50) ? status_get_lv(bl) - 10 : 50;
					stat = max(0,max_stat - status_get_base_status(bl)->str); val3 |= cap_value(stat,0,0xFF)<<16;
					stat = max(0,max_stat - status_get_base_status(bl)->agi); val3 |= cap_value(stat,0,0xFF)<<8;
					stat = max(0,max_stat - status_get_base_status(bl)->vit); val3 |= cap_value(stat,0,0xFF);
					stat = max(0,max_stat - status_get_base_status(bl)->int_); val4 |= cap_value(stat,0,0xFF)<<16;
					stat = max(0,max_stat - status_get_base_status(bl)->dex); val4 |= cap_value(stat,0,0xFF)<<8;
					stat = max(0,max_stat - status_get_base_status(bl)->luk); val4 |= cap_value(stat,0,0xFF);
				}
				break;
			case SC_REJECTSWORD:
				val2 = 15 * val1; //Reflect chance
				val3 = 3; //Reflections
				break;
			case SC_MEMORIZE:
				val2 = 5; //Memorized casts
				break;
			case SC_GRAVITATION:
				val2 = 50 * val1; //ASPD reduction
				break;
			case SC_REGENERATION:
				if( val1 == 1 )
					val2 = 2;
				else
					val2 = val1; //HP Regerenation rate: 200% 200% 300%
				val3 = val1; //SP Regeneration Rate: 100% 200% 300%
				//If val4 comes set, this blocks regen rather than increase it
				break;
			case SC_DEVOTION: {
					struct block_list *d_bl = NULL;
					struct status_change *d_sc = NULL;

					if( (d_bl = map_id2bl(val1)) && (d_sc = status_get_sc(d_bl)) && d_sc->count ) { //Inherits status from source
						const enum sc_type types[] = { SC_AUTOGUARD,SC_REFLECTSHIELD,SC_DEFENDER,SC_ENDURE };
						int i = (map_flag_gvg2(bl->m) || map[bl->m].flag.battleground) ? 2 : 3;

						while( i >= 0 ) {
							enum sc_type type2 = types[i];

							if( d_sc->data[type2] )
								status_change_start(d_bl,bl,type2,10000,d_sc->data[type2]->val1,0,0,(type2 == SC_REFLECTSHIELD ? 1 : 0),skill_get_time(status_sc2skill(type2),d_sc->data[type2]->val1),SCFLAG_NOICON);
							i--;
						}
					}
				}
				break;
			case SC_COMA: //Coma, sends a char to 1 HP, if val2, do not zap SP
				status_zap(bl,status->hp - 1,(val2 ? 0 : status->sp));
				return 1;
			case SC_CLOSECONFINE2: {
					struct block_list *src2 = (val2 ? map_id2bl(val2) : NULL);
					struct status_change *sc2 = (src2 ? status_get_sc(src2) : NULL);
					struct status_change_entry *sce2 = (sc2 ? sc2->data[SC_CLOSECONFINE] : NULL);

					if( src2 && sc2 ) {
						if( !sce2 ) //Start lock on caster
							sc_start4(src2,src2,SC_CLOSECONFINE,100,val1,1,0,0,tick + 1000);
						else { //Increase count of locked enemies and refresh time
							sce2->val2++;
							delete_timer(sce2->timer,status_change_timer);
							sce2->timer = add_timer(gettick() + tick + 1000,status_change_timer,src2->id,SC_CLOSECONFINE);
						}
					} else //Status failed
						return 0;
				}
				break;
			case SC_KAITE:
				val2 = 1 + val1 / 5; //Number of bounces: 1 + skill_lv / 5
				break;
			case SC_KAUPE:
				switch( val1 ) {
					case 3: //33*3 + 1 -> 100%
						val2++;
					//Fall through
					case 1:
					case 2: //33, 66%
						val2 += 33 * val1;
						val3 = 1; //Dodge 1 attack total
						break;
					default: //Custom. For high level mob usage, higher level means more blocks [Skotlex]
						val2 = 100;
						val3 = val1 - 2;
						break;
				}
				break;
			case SC_COMBO: {
					//val1: Skill ID
					//val2: When given, target (for autotargetting skills)
					//val3: When set, this combo time should NOT delay attack/movement
					//val3: If set to 2 this combo will delay ONLY attack
					//val3: TK: Last used kick
					//val4: TK: Combo time
					struct unit_data *ud = unit_bl2ud(bl);

					if( ud && (!val3 || val3 == 2) ) {
						ud->attackabletime = gettick() + tick;
						if( !val3 )
							unit_set_walkdelay(bl,gettick(),tick,1);
					}
					val3 = 0;
					val4 = tick;
				}
				break;
			case SC_EARTHSCROLL:
				val2 = 11 - val1; //Chance to consume: 11 - % skill_lv
				break;
			case SC_RUN:
			case SC_WUGDASH:
				val4 = gettick(); //Store time at which you started running
				break;
			case SC_KAAHI:
				val2 = 200 * val1; //HP heal
				val3 = 5 * val1; //SP cost
				break;
			case SC_BLESSING:
				if( bl->type == BL_PC || (!undead_flag && status->race != RC_DEMON) )
					val2 = val1;
				else
					val2 = 0; //Offensive blessing -> Half stat
				break;
			case SC_TRICKDEAD:
				if( vd )
					vd->dead_sit = 1;
				break;
			case SC_CONCENTRATE:
				val2 = 2 + val1;
				if( sd ) { //Store the card-bonus data that should not count in the %
					val3 = sd->param_bonus[1]; //Agi
					val4 = sd->param_bonus[4]; //Dex
				} else
					val3 = val4 = 0;
				break;
			case SC_MAXOVERTHRUST:
				val2 = 20 * val1; //Power increase
				break;
			case SC_OVERTHRUST:
			case SC_ADRENALINE:
			case SC_ADRENALINE2:
			case SC_WEAPONPERFECTION:
				{
					struct map_session_data *tsd = BL_CAST(BL_PC,src);

					//val2 holds if it was casted on self, or is bonus received from others
					if( type == SC_OVERTHRUST )
						val3 = (val2 ? 5 * val1 : 5); //Power increase
					else if( type == SC_ADRENALINE || type == SC_ADRENALINE2 )
						val3 = (val2 ? 300 : 200); //Aspd increase
					if( tsd && pc_checkskill(tsd,BS_HILTBINDING) > 0 )
						tick += tick / 10; //If caster has Hilt Binding, duration increases by 10%
				}
				break;
			case SC_CONCENTRATION:
				val2 = 5 * val1; //Atk Increase
				val3 = 10 * val1; //Hit Increase
				val4 = 5 * val1; //Def reduction
				sc_start(src,bl,SC_ENDURE,100,1,tick); //Endure effect
				break;
			case SC_ANGELUS:
				val2 = 5 * val1; //Def increase
				break;
			case SC_IMPOSITIO:
				val2 = 5 * val1; //Atk increase
				break;
			case SC_MELTDOWN:
				val2 = 100 * val1; //Chance to break weapon
				val3 = 70 * val1; //Change to break armor
				break;
			case SC_TRUESIGHT:
				val2 = 10 * val1; //Critical increase
				val3 = 3 * val1; //Hit increase
				break;
			case SC_SUN_COMFORT:
				val2 = (status_get_lv(bl) + status->dex + status->luk) / 2; //Def increase
				break;
			case SC_MOON_COMFORT:
				val2 = (status_get_lv(bl) + status->dex + status->luk) / 10; //Flee increase
				break;
			case SC_STAR_COMFORT:
				val2 = (status_get_lv(bl) + status->dex + status->luk); //Aspd increase
				break;
			case SC_QUAGMIRE:
				val2 = (bl->type == BL_PC ? 5 : 10) * val1; //Agi/Dex/Move Speed reduction
				break;
			case SC_GATLINGFEVER:
				val2 = 20 * val1; //Aspd increase
				val3 = 20 + 10 * val1; //Atk increase
				val4 = 5 * val1; //Flee decrease
				break;
			case SC_FLING:
				if( bl->type == BL_PC )
					val2 = 0; //No armor reduction to players
				else
					val2 = 5 * val1; //Def reduction
				val3 = 5 * val1; //Def2 reduction
				break;
#ifdef RENEWAL
			case SC_TATAMIGAESHI:
				val2 = 0;
				break;
#endif
			case SC_PROVOKE:
				//val2 signals autoprovoke
				val3 = 2 + 3 * val1; //Atk increase
				val4 = 5 + 5 * val1; //Def reduction
				break;
			case SC_AVOID:
				//val2 = 10 * val1; //Speed change rate
				break;
			case SC_DEFENCE:
#ifdef RENEWAL
				val2 = 5 + 5 * val1; //Vit bonus
#else
				val2 = 2 * val1; //Def bonus
#endif
				break;
			case SC_BLOODLUST:
				val2 = 20 + 10 * val1; //Atk rate change
				val3 = 3 * val1; //Leech chance
				val4 = 20; //Leech percent
				break;
			case SC_FLEET:
				val2 = 30 * val1; //Aspd change.
				val3 = 5 + 5 * val1; //Batk/Watk rate change
				break;
			case SC_MINDBREAKER:
				val2 = 20 * val1; //Matk increase
				val3 = 12 * val1; //Mdef2 reduction
				break;
			case SC_JAILED:
				//val1 is duration in minutes (Use INT_MAX to specify 'unlimited' time)
				//When first called:
				//val2 Jail map_index
				//val3 x
				//val4 y
				//When renewing status' information:
				//val3 Return map_index
				//val4 return coordinates
				tick = (val1 > 0 ? 1000 : 250);
				if( sd ) {
					if( sd->mapindex != val2 ) {
						int pos = (bl->x&0xFFFF)|(bl->y<<16), //Current coordinates
							map_idx = sd->mapindex; //Current map

						//1. Place in jail (val2 -> Jail Map, val3 -> x, val4 -> y)
						pc_setpos(sd,(unsigned short)val2,val3,val4,CLR_TELEPORT);
						//2. Set restore point (val3 -> return map, val4 return coords)
						val3 = map_idx;
						val4 = pos;
					} else if( !val3 || val3 == sd->mapindex ||
						!sd->sc.data[SC_JAILED] ) //If player is being jailed and is already in jail (issue: 8206)
					{ //Use save point
						val3 = sd->status.save_point.map;
						val4 = (sd->status.save_point.x&0xFFFF)|(sd->status.save_point.y<<16);
					}
				}
				break;
			case SC_UTSUSEMI:
				val2 = (val1 + 1) / 2; //Number of hits blocked
				val3 = skill_get_blewcount(NJ_UTSUSEMI,val1); //Knockback value
				break;
			case SC_BUNSINJYUTSU:
				val2 = (val1 + 1) / 2; //Number of hits blocked
				break;
			case SC_CHANGE:
				val2 = 30 * val1; //Vit increase
				val3 = 20 * val1; //Int increase
				break;
			case SC_SWOO:
				if( status_has_mode(status,MD_STATUS_IMMUNE) )
					tick /= 5;
				break;
			case SC_ENCHANTARMS:
				skill_enchant_elemental_end(bl,type); //End previous enchants
				//Make sure the received element is valid
				if( val2 >= ELE_ALL )
					val2 = val2%ELE_ALL;
				else if( val2 < 0 )
					val2 = rnd()%ELE_ALL;
				icon = SI_ATTACK_PROPERTY_NOTHING + val2;
				break;
			case SC_CRITICALWOUND:
				val2 = 20 * val1; //Heal effectiveness decrease
				break;
			case SC_MAGICMIRROR:
				val1 = 1 + ((val1 - 1)%5); //Level 6 ~ 10 use effect of level 1 ~ 5
			//Fall through
			case SC_SLOWCAST:
				val2 = 20 * val1; //Magic reflection/cast rate
				break;
			case SC_ARMORCHANGE:
				if( val2 == NPC_ANTIMAGIC ) {
					val2 = -20;
					val3 = 20; //Boost % MDEF
				} else {
					val2 = 20; //Boost % DEF
					val3 = -20;
				}
				//Level 6 ~ 10 use effect of level 1 ~ 5
				val1 = 1 + ((val1 - 1)%5);
				val2 *= val1; //20% per level
				val3 *= val1;
				break;
			case SC_INCFLEE2:
			case SC_INCCRI:
			case SC_CRIFOOD:
				val2 = 10 * val1; //Actual boost (since 100% = 1000)
				break;
			case SC_SUFFRAGIUM:
				val2 = 15 * val1; //Speed cast decrease
				break;
			case SC_DOUBLECAST:
				val2 = 30 + 10 * val1; //Trigger rate
				break;
			case SC_KAIZEL:
				val2 = 10 * val1; //% of life to be revived with
				break;
			case SC_MERC_FLEEUP:
			case SC_MERC_ATKUP:
			case SC_MERC_HITUP:
				val2 = 15 * val1;
				break;
			case SC_MERC_HPUP:
			case SC_MERC_SPUP:
				val2 = 5 * val1;
				break;
			case SC_REBIRTH:
				val2 = 20 * val1; //% of life to be revived with
				break;
			case SC_FEAR:
				status_change_start(src,bl,SC_ANKLE,10000,val1,0,0,0,2000,SCFLAG_NOAVOID|SCFLAG_FIXEDTICK|SCFLAG_FIXEDRATE);
				break;
			case SC_ENCHANTBLADE:
				val2 = (100 + 20 * val1) * status_get_lv(src) / 150 + status_get_int(src);
				break;
			case SC_DEATHBOUND:
				val2 = 500 + 100 * val1;
				break;
			case SC_REFRESH: {
					int heal = status_get_max_hp(bl) * 25 / 100;

					status_heal(bl,heal,0,1);
					status_change_clear_buffs(bl,SCCB_REFRESH,0);
				}
				break;
			case SC_STONEHARDSKIN: { //Get src data for Lux Anima effect
					struct map_session_data *ssd = map_id2sd(src->id);

					val2 = status_get_job_lv(src) * (ssd ? pc_checkskill(ssd,RK_RUNEMASTERY) : 10) / 4
#ifndef RENEWAL
						/ 10
#endif
						; //DEF/MDEF Increase
				}
				break;
			case SC_ABUNDANCE:
				tick_time = 10000;
				val4 = tick / tick_time;
				break;
			case SC_CRUSHSTRIKE:
				if( sd ) { //ATK [{Weapon Level * (Weapon Upgrade Level + 6) * 100} + (Weapon ATK) + (Weapon Weight)]%
					short index = sd->equip_index[EQI_HAND_R];

					if( index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON )
						val2 = sd->inventory_data[index]->wlv * (sd->inventory.u.items_inventory[index].refine + 6) * 100 +
							sd->inventory_data[index]->atk + sd->inventory_data[index]->weight / 10;
				}
				break;
			case SC_GIANTGROWTH:
				val2 = 15; //Success rate
				break;
			case SC_RENOVATIO:
				tick_time = 5000;
				val4 = tick / tick_time;
				break;
			case SC_LAUDAAGNUS:
				clif_specialeffect(bl,747,AREA);
				break;
			case SC_LAUDARAMUS:
				clif_specialeffect(bl,748,AREA);
				break;
			case SC_ORATIO:
				val2 = 2 * val1;
				break;
			case SC_SECRAMENT:
				val2 = 10 * val1;
				break;
			case SC_EPICLESIS:
				val2 = 5 * val1; //HP rate bonus
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_VENOMIMPRESS:
				val2 = 10 * val1;
				break;
			case SC_WEAPONBLOCKING:
				val2 = 10 + 2 * val1; //Chance
				tick_time = 5000;
				val4 = tick / tick_time;
				break;
			case SC_OBLIVIONCURSE:
				tick_time = 3000;
				val4 = tick / tick_time;
				break;
			case SC_CLOAKINGEXCEED:
				val2 = (val1 + 1) / 2; //Hits
				val3 = (val1 - 1) * 10; //Walk speed
				if( bl->type == BL_PC )
					val4 |= battle_config.pc_cloak_check_type&7;
				else
					val4 |= battle_config.monster_cloak_check_type&7;
				tick_time = 1000;
				break;
			case SC_HALLUCINATIONWALK:
				val2 = 50 * val1; //Flee bonus
				val3 = 10 * val1; //Evasion rate of magical attacks
				break;
			case SC_FREEZING:
				val2 = (bl->type == BL_MOB ? 10 : 30); //DEF reduction
				break;
			case SC_MARSHOFABYSS:
				val2 = 10 + status_get_int(src) / 10 + status_get_job_lv(src) / 2; //Flee and % Defense reduction
				val3 = 40 + 10 * val1; //Movement Speed Reduction
				break;
			case SC_FREEZE_SP:
				//val2 = SP drain per 10 seconds
				tick_time = 10000;
				break;
			case SC_SPHERE_1:
			case SC_SPHERE_2:
			case SC_SPHERE_3:
			case SC_SPHERE_4:
			case SC_SPHERE_5:
				if( !sd )
					return 0; //Should only work on players
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_SHAPESHIFT:
				switch( val1 ) {
					case 1: val2 = ELE_FIRE; break;
					case 2: val2 = ELE_EARTH; break;
					case 3: val2 = ELE_WIND; break;
					case 4: val2 = ELE_WATER; break;
				}
				break;
			case SC_NEUTRALBARRIER:
				val2 = 10 + 5 * val1; //DEF/MDEF increase
				break;
			case SC_STEALTHFIELD_MASTER:
				tick_time = val3 = 2000 + 1000 * val1;
				val4 = tick / tick_time;
				break;
			case SC_OVERHEAT:
			case SC_OVERHEAT_LIMITPOINT:
			case SC_STEALTHFIELD:
				tick_time = tick;
				break;
			case SC_MAGNETICFIELD:
			case SC_CAMOUFLAGE:
			case SC_ELECTRICSHOCKER:
			case SC__REPRODUCE:
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC__AUTOSHADOWSPELL:
				//val1: Auto Shadow Spell LV
				//val2: Skill ID To Autocast
				//val3: Skill LV To Autocast
				//val4: Autocast Chance
				if( val1 >= 10 )
					val4 = 15; //Autocast fixed to 15 if LV is 10 or higher
				else
					val4 = 30 - 2 * val1;
				break;
			case SC__SHADOWFORM: {
					struct map_session_data *s_sd = map_id2sd(val2);

					if( s_sd ) {
						struct status_change *s_sc = NULL;
						struct status_change_entry *s_sce = NULL;

						s_sd->shadowform_id = bl->id;
						if( (s_sc = &s_sd->sc) && (s_sce = s_sc->data[SC_ENDURE]) && !s_sce->val4 )
							status_change_start(&s_sd->bl,bl,SC_ENDURE,10000,s_sce->val1,0,1,0,skill_get_time(SM_ENDURE,s_sce->val1),SCFLAG_NOICON);
					}
					tick_time = 1000;
					val4 = tick / tick_time;
				}
				break;
			case SC__INVISIBILITY:
				val2 = 20 * val1; //Critical Amount Increase
				val3 = 50 - 10 * val1; //ASPD Reduction
				tick_time = 1000;
				break;
			case SC__ENERVATION:
				if( sd )
					pc_delspiritball(sd,sd->spiritball,0);
				val2 = 20 + 10 * val1; //ATK Reduction
				break;
			case SC__GROOMY:
				val2 = 20 * val1; //HIT Reduction
				val3 = 20 + 10 * val1; //ASPD Reduction
				if( sd ) { //Removes Animals
					if( pc_isriding(sd) )
						pc_setriding(sd,0);
					if( pc_isridingdragon(sd) )
						pc_setoption(sd,sd->sc.option&~OPTION_DRAGON);
					if( pc_isridingwug(sd) )
						pc_setoption(sd,sd->sc.option&~OPTION_WUGRIDER);
					if( pc_isfalcon(sd) )
						pc_setoption(sd,sd->sc.option&~OPTION_FALCON);
					if( pc_iswug(sd) )
						pc_setoption(sd,sd->sc.option&~OPTION_WUG);
					if( sd->status.pet_id > 0 )
						pet_menu(sd,3);
					if( hom_is_active(sd->hd) )
						hom_vaporize(sd,HOM_ST_ACTIVE);
					//if( sd->md ) //Info shows nothing about Merc being removed. Probely true since their not a animal [Rytech]
						//mercenary_delete(sd->md,3);
					//Are rental mounts stripped as well? Well find out once I add them in
				}
				break;
			case SC__LAZINESS:
				val2 = 10 * val1; //Flee Reduction
				val3 = 10 + 10 * val1; //Increased Cast Time
				break;
			case SC__UNLUCKY:
				val2 = 100 * val1; //Critical and Perfect Dodge Reduction
				break;
			case SC__WEAKNESS:
				val2 = 10 * val1; //MaxHP Reduction
				//Bypasses coating protection and MADO
				sc_start(src,bl,SC_STRIPWEAPON,100,val1,tick);
				sc_start(src,bl,SC_STRIPSHIELD,100,val1,tick);
				break;
			case SC__STRIPACCESSORY:
				if( !sd )
					val2 = 20;
				break;
			case SC_GN_CARTBOOST:
				if( val1 < 3 )
					val2 = 50;
				else if( val1 < 5 )
					val2 = 75;
				else
					val2 = 100;
				break;
			case SC_PROPERTYWALK:
				val3 = 0;
				break;
			case SC_CLOUD_KILL:
				tick_time = 500;
				val4 = tick / tick_time;
				break;
			case SC_STRIKING:
				val3 = 6 - val1; //SP cost
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_WARMER:
				tick_time = 3000;
				val4 = tick / tick_time;
				break;
			case SC_CRYSTALIZE:
			case SC_THORNSTRAP:
			case SC_BLOODSUCKER:
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_FIRE_INSIGNIA:
			case SC_WATER_INSIGNIA:
			case SC_WIND_INSIGNIA:
			case SC_EARTH_INSIGNIA:
				tick_time = 5000;
				val4 = tick / tick_time;
				break;
			case SC_VACUUM_EXTREME:
				if( val4 < tick && !unit_blown_immune(bl,0x9) ) {
					tick_time = val4;
					val4 = tick - tick_time;
				} else
					val4 = 0;
				break;
			case SC_SWINGDANCE:
				val3 = 5 * val1 + val2; //ASPD Increase
				break;
			case SC_SYMPHONYOFLOVER:
				val4 = 12 * val1 + val2 + val3 / 4; //MDEF Increase In %
				break;
			case SC_MOONLITSERENADE:
			case SC_RUSHWINDMILL:
				val4 = 6 * val1 + val2 + val3 / 5; //MATK/ATK Increase
				break;
			case SC_ECHOSONG:
				val4 = 6 * val1 + val2 + val3 / 4; //DEF Increase In %
				break;
			case SC_HARMONIZE:
				val2 = 5 + 5 * val1;
				break;
			case SC_WINKCHARM:
			case SC_VOICEOFSIREN:
				tick_time = 2000;
				val4 = tick / tick_time;
				break;
			case SC_DEEPSLEEP:
				tick_time = 2000;
				val4 = tick / tick_time;
				break;
			case SC_SIRCLEOFNATURE:
				val2 = 40 * val1; //HP recovery
				val3 = 4 * val1; //SP drain
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_GLOOMYDAY:
				val2 = 20 + 5 * val1; //Flee reduction
				val3 = 15 + 5 * val1; //ASPD reduction
				val4 = rnd_value(15,val4 * 5 + val1 * 10); //Damage increase
				if( rnd()%100 < val1 ) { //Chance of super gloomy effect
					if( sd ) { //If successful, dismount the player. Only applies to Peco/Gryphon and Dragon
						if( pc_isriding(sd) )
							pc_setriding(sd,0);
						if( pc_isridingdragon(sd) )
							pc_setoption(sd,sd->sc.option&~OPTION_DRAGON);
					}
					val1 = 2; //Use to signal if speed reduction applies
				} else
					val1 = 1; //Signal no speed reduction but still give a value
				break;
			case SC_SONGOFMANA:
				val3 = 10 + min(5 * val2,35); //SP regen
				tick_time = 5000;
				val4 = tick / tick_time;
				break;
			case SC_DANCEWITHWUG:
				val3 = 5 + min(5 * val2,30); //ASPD Increase
				val4 = 20 + min(10 * val2,70); //Fixed Cast Time Reduction
				break;
			case SC_SATURDAYNIGHTFEVER:
				clif_status_change(bl,SI_BERSERK,1,tick,0,0,0);
				tick_time = val3 = skill_get_time2(status_sc2skill(type),val1);
				val4 = tick / tick_time;
				break;
			case SC_LERADSDEW:
				val3 = 200 * val1 + min(300 * val2,2500); //MaxHP Increase
				break;
			case SC_MELODYOFSINK:
				val3 = val1 * val3; //INT Reduction
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_BEYONDOFWARCRY:
				val3 = val1 * val3; //STR And Crit Increase
				val4 = 4 * val1 + min(4 * val2,40); //% MaxHP Reduction
				break;
			case SC_UNLIMITEDHUMMINGVOICE:
				val3 = 15 - min(3 * val2,15); //SP Cost
				break;
			case SC_SITDOWN_FORCE:
			case SC_BANANA_BOMB_SITDOWN:
				if( sd && !pc_issit(sd) ) {
					pc_setsit(sd);
					skill_sit(sd,true);
					clif_sitting(bl);
				}
				break;
			case SC_REFLECTDAMAGE:
				val2 = 15 + 5 * val1; //Reflect Amount
				val3 = 25 + 5 * val1; //Number of Reflects
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_FORCEOFVANGUARD:
				val2 = 8 + 12 * val1; //Chance Of Getting A Rage Counter
				val3 = 5 + 2 * val1; //Max Number of Rage Counter's Possible
				tick_time = 10000;
				val4 = tick / tick_time;
				break;
			case SC_EXEEDBREAK:
				val2 = 150 * val1;
				if( sd ) {
					short index = sd->equip_index[EQI_HAND_R];

					if( index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON )
						val2 += 15 * status_get_job_lv(bl) + sd->inventory_data[index]->weight / 10 * sd->inventory_data[index]->wlv * status_get_lv(bl) / 100;
				} else
					val2 += 750;
				break;
			case SC_PRESTIGE:
				val2 = 15 * val1 + (sd ? 10 * pc_checkskill(sd,CR_DEFENDER) : 50) * status_get_lv(bl) / 100
#ifndef RENEWAL
					/ 10
#endif
					; //DEF increase
				val3 = (status->int_ + status->luk) * val1 / 20 * status_get_lv(bl) / 200 + val1; //Chance to evade magic damage
				break;
			case SC_BANDING:
				val2 = (sd ? skill_banding_count(sd) : 1);
				tick_time = 5000;
				val3 = tick / tick_time;
				break;
			case SC_INSPIRATION:
				val2 = status_get_job_lv(bl);
				val3 = status_get_lv(bl) / 10 + val2 / 5; //All stat bonus
				tick_time = 5000;
				val4 = tick / tick_time;
				break;
			case SC_CRESCENTELBOW:
				val2 = status_get_job_lv(bl) / 2 + 50 + 5 * val1;
				break;
			case SC_LIGHTNINGWALK:
				val2 = status_get_job_lv(bl) / 2 + 40 + 5 * val1; //Chance: [(Job Level / 2) + (40 + 5 * Skill Level)] %
				val3 = ((val1 > 0 && val1 < 3) ? 3 : (val1 > 2 && val1 < 5) ? 2 : 1); //SP drain
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_RAISINGDRAGON:
				tick_time = 5000;
				val4 = tick / tick_time;
				break;
			case SC_GT_ENERGYGAIN:
				val2 = 10 + 5 * val1; //Sphere gain chance
				break;
			case SC_GT_CHANGE: {
					int casterint = status_get_int(src);

					if( casterint <= 0 )
						casterint = 1; //Prevents dividing by 0 since its possiable to reduce players stats to 0 [Rytech]
					val2 = (status_get_dex(src) / 4 + status_get_str(src) / 2) * val1 / 5; //ATK increase: ATK [{(Caster's DEX / 4) + (Caster's STR / 2)} x Skill Level / 5]
					val3 = status->agi * val1 / 60; //ASPD increase: [(Target's AGI x Skill Level) / 60] %
					val4 = 200 / casterint * val1; //MDEF decrease: MDEF [(200 / Caster's INT) x Skill Level]
				}
				break;
			case SC_GT_REVITALIZE:
				val2 = status_get_vit(src) / 4 * val1; //Stat DEF increase: [(Caster's VIT / 4) x Skill Level]
				val3 = val1 * 30 + 50; //Natural HP recovery increase: [(Skill Level x 30) + 50] %
				break;
			case SC_PYROTECHNIC_OPTION:
				val2 = 60; //Bonus Eatk
				break;
			case SC_HEATER_OPTION:
				val2 = 120; //Bonus Eatk
				val3 = status_get_job_lv(bl); //% Increase damage
				val4 = ELE_FIRE; //Change into fire element
				break;
			case SC_TROPIC_OPTION:
				val2 = 180; //Bonus Eatk
				val3 = MG_FIREBOLT;
				break;
			case SC_AQUAPLAY_OPTION:
				val2 = 40; //Bonus Matk
				break;
			case SC_COOLER_OPTION:
				val2 = 80; //Bonus Matk
				val3 = status_get_job_lv(bl); //% Freezing chance
				val4 = ELE_WATER; //Change into water elemet
				break;
			case SC_CHILLY_AIR_OPTION:
				val2 = 120; //Bonus Matk
				val3 = MG_COLDBOLT;
				break;
			case SC_WIND_STEP_OPTION:
				val2 = 50; //% Increase speed and flee
				break;
			case SC_BLAST_OPTION:
				val2 = status_get_job_lv(bl); //% Increase damage
				val3 = ELE_WIND; //Change into wind elemet
				break;
			case SC_WILD_STORM_OPTION:
				val2 = MG_LIGHTNINGBOLT;
				break;
			case SC_PETROLOGY_OPTION:
				val2 = 5; //Bonus % MaxHP
				val3 = 50; //% Increase Chance
				break;
			case SC_CURSED_SOIL_OPTION:
				val2 = 10; //Bonus % MaxHP
				val3 = status_get_job_lv(bl); //% Increase Damage
				val4 = ELE_EARTH; //Change into earth elemet
				break;
			case SC_UPHEAVAL_OPTION:
				val2 = WZ_EARTHSPIKE;
				val3 = 15; //Bonus % MaxHP
				break;
			case SC_CIRCLE_OF_FIRE_OPTION:
				val2 = 300;
				break;
			case SC_WATER_SCREEN_OPTION:
				tick_time = 10000;
				break;
			case SC_FIRE_CLOAK_OPTION:
			case SC_WATER_DROP_OPTION:
			case SC_WIND_CURTAIN_OPTION:
			case SC_STONE_SHIELD_OPTION:
				val2 = 100; //Elemental Resistance
				break;
			case SC_TROPIC:
			case SC_CHILLY_AIR:
			case SC_WILD_STORM:
			case SC_UPHEAVAL:
				val2 += 10;
			//Fall through
			case SC_HEATER:
			case SC_COOLER:
			case SC_BLAST:
			case SC_CURSED_SOIL:
				val2 += 10;
			//Fall through
			case SC_PYROTECHNIC:
			case SC_AQUAPLAY:
			case SC_GUST:
			case SC_PETROLOGY:
				val2 += 5;
				val3 += 9000;
			//Fall through
			case SC_CIRCLE_OF_FIRE:
			case SC_WATER_SCREEN:
			case SC_WIND_STEP:
			case SC_SOLID_SKIN:
			case SC_FIRE_CLOAK:
			case SC_WATER_DROP:
			case SC_WIND_CURTAIN:
			case SC_STONE_SHIELD:
				val2 += 5;
				val3 += 1000;
				val4 = tick;
				tick_time = val3;
				break;
			case SC_WATER_BARRIER:
				val2 = 30; //Reduce Atk, Flee
				break;
			case SC_ZEPHYR:
				val2 = 25; //Flee
				break;
			case SC_TIDAL_WEAPON:
				val2 = 20; //Increase Elemental attack
				break;
			case SC_POWER_OF_GAIA:
				val3 = 20; //% Increase summoner HP
			//Fall through
			case SC_ROCK_CRUSHER:
			case SC_ROCK_CRUSHER_ATK:
				val2 = 33; //% Reduce Def/Speed
				break;
			case SC_TEARGAS:
			case SC_DEMONIC_FIRE:
				tick_time = 2000;
				val4 = tick / tick_time;
				break;
			case SC_TEARGAS_SOB:
				tick_time = 3000;
				val4 = tick / tick_time;
				break;
			case SC_STOMACHACHE:
				val2 = 8; //SP consume
				tick_time = 10000;
				val4 = tick / tick_time;
				break;
			case SC_PROMOTE_HEALTH_RESERCH:
				//val1: 1 = Regular Potion, 2 = Thrown Potion
				//val2: 1 = Small Potion, 2 = Medium Potion, 3 = Large Potion
				//val3: BaseLV of Thrower For Thrown Potions
				//val4: MaxHP Increase By Fixed Amount
				if( val1 == 1 ) //If potion was normally used, take the user's BaseLv
					val4 = 1000 * val2 - 500 + status_get_lv(bl) * 10 / 3;
				else if( val1 == 2 ) //If potion was thrown at someone, take the thrower's BaseLv
					val4 = 1000 * val2 - 500 + val3 * 10 / 3;
				if( val4 <= 0 ) //Prevents a negeative value from happening
					val4 = 0;
				break;
			case SC_ENERGY_DRINK_RESERCH:
				//val1: 1 = Regular Potion, 2 = Thrown Potion
				//val2: 1 = Small Potion, 2 = Medium Potion, 3 = Large Potion
				//val3: BaseLV of Thrower For Thrown Potions
				//val4: MaxSP Increase By Percentage Amount
				if( val1 == 1 ) //If potion was normally used, take the user's BaseLv
					val4 = status_get_lv(bl) / 10 + 5 * val2 - 10;
				else if( val1 == 2 ) //If potion was thrown at someone, take the thrower's BaseLv
					val4 = val3 / 10 + 5 * val2 - 10;
				if( val4 <= 0 ) //Prevents a negeative value from happening
					val4 = 0;
				break;
			case SC_MEIKYOUSISUI:
				if( sd ) {
					pc_setsit(sd);
					skill_sit(sd,true);
					clif_sitting(&sd->bl);
				}
				val2 = val1 * 10; //Chance of nulling the attack
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_KYOUGAKU:
				val2 = rnd_value(val1 * 2,val1 * 3);
				val1 = MOBID_PORING;
				break;
			case SC_ZENKAI:
				if( sd ) {
					if( status->rhw.ele == val2 )
						val3 = status_get_lv(bl) + status_get_job_lv(bl);
					else
						val3 = 0;
				} else
					val3 = 0;
				break;
			case SC_KAGEMUSYA:
				val2 = val1 * 2;
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_IZAYOI:
				val2 = val1 * status_get_job_lv(bl) / 2;
				break;
			case SC_ZANGETSU:
				val2 = val4 = status_get_lv(bl) / 3 + 20 * val1;
				val3 = status_get_lv(bl) / 3 + 30 * val1;
				val2 = (!(status->hp%2) ? val2 : -val3);
				val3 = (!(status->sp%2) ? val4 : -val3);
				break;
			case SC_GENSOU:
#define PER(a, lvl) do { \
	int temp__ = (a); \
	if( temp__ <= 15 ) (lvl) = 1; \
	else if( temp__ <= 30 ) (lvl) = 2; \
	else if( temp__ <= 50 ) (lvl) = 3; \
	else if( temp__ <= 75 ) (lvl) = 4; \
	else (lvl) = 5; \
} while(0)
				{
					int hp = max(status->hp,1), sp = max(status->sp,1), lv = 5;

					PER(100 / (status->max_hp / hp),lv);
					status_heal(bl,(!(hp%2) ? (hp * (val1 + (6 - lv) * 4) / 100) : (hp * -(val1 + lv * 4) / 100)),0,1);
					PER(100 / (status->max_sp / sp),lv);
					status_heal(bl,0,(!(sp%2) ? (sp * (val1 + (6 - lv) * 3) / 100) : (sp * -(val1 + lv * 3) / 100)),1);
				}
				break;
			case SC_ANGRIFFS_MODUS:
				val2 = 50 + 20 * val1; //Atk bonus
				val3 = 40 + 20 * val1; //Flee reduction
				tick_time = 1000;
				val4 = tick / tick_time; //Hp/Sp reduction timer
				break;
			case SC_GOLDENE_FERSE:
				val2 = 10 + 10 * val1; //Flee bonus
				val3 = 6 + 4 * val1; //Aspd Bonus
				val4 = 2 + 2 * val1; //Chance of holy attack
				break;
			case SC_OVERED_BOOST:
				val2 = 300 + 40 * val1; //Flee bonus
				val3 = 179 + 2 * val1; //Aspd bonus
				val4 = 50; //Def reduction %
				break;
			case SC_GRANITIC_ARMOR:
				val2 = 2 * val1; //Dmg hp reduction
				val3 = 6 * val1; //Dmg hp on status end
				break;
			case SC_MAGMA_FLOW:
				val2 = 3 * val1; //Activation chance
				break;
			case SC_ASH:
				val2 = 50; //Hit reduction %
				if( status_get_race(bl) == RC_PLANT )
					val3 = 50; //Def reduction %
				if( status_get_element(bl) == ELE_WATER )
					val4 = 50; //Atk/Flee reduction
				break;
			case SC_PYROCLASTIC:
				val2 = 10 * val1 + status_get_lv(src); //Watk bonus
				val3 = 2 * val1; //Chance to autocast Hammer Fall %
				break;
			case SC_PARALYSIS:
				val2 = 5 * val1; //DEF/MDEF Reduction
				val3 = 5 * val1; //Increased Cast Time
				break;
			case SC_LIGHT_OF_REGENE:
				val2 = 20 * val1; //Hp reco on death %
				break;
			case SC_PAIN_KILLER:
				val2 = 10 * val1; //Aspd reduction %
				val3 = 200 * val1 * status_get_lv(src) / 150; //Flat dmg reduction
				sc_start(src,bl,SC_ENDURE,100,1,tick); //Start endure for same duration
				break;
			case SC_MIDNIGHT_FRENZY_POSTDELAY: {
					TBL_HOM *hd = BL_CAST(BL_HOM,bl);

					if( hd->master )
						clif_homskillinfo(hd->master,MH_SONIC_CRAW,INF_SELF_SKILL);
				}
				break;
			case SC_CBC:
				val1 = 0; //Prepares for tracking seconds in timer script
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_EQC:
				val2 = 5 * val1; //Atk/Def reduc %
				val3 = 2 * val1; //MaxHP reduc %
				break;
			case SC_FULL_THROTTLE:
				val2 = (val1 == 1 ? 6 : 6 - val1);
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_REBOUND:
				tick_time = 2000;
				val4 = tick / tick_time;
				break;
			case SC_KINGS_GRACE:
				val2 = 3 + val1;
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_TELEKINESIS_INTENSE:
				val2 = 10 * val1;
				val3 = 100 + 40 * val1;
				break;
			case SC_OFFERTORIUM:
				val2 = 30 * val1; //Heal Power Increase
				val3 = 100 + 20 * val1; //SP Requirement Increase
				break;
			case SC_FRIGG_SONG:
				val2 = 5 * val1; //MaxHP Increase
				val3 = 80 + 20 * val1;
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_DARKCROW:
				val2 = 30 * val1;
				break;
			case SC_UNLIMIT:
				val2 = 50 * val1;
				status_change_start(src,bl,SC_DEFSET_NUM,10000,1,0,0,1,tick,SCFLAG_FIXEDTICK);
				status_change_start(src,bl,SC_MDEFSET_NUM,10000,1,0,0,1,tick,SCFLAG_FIXEDTICK);
				break;
			case SC_FLASHCOMBO:
				val2 = 20 + 20 * val1;
				break;
			case SC_MONSTER_TRANSFORM:
			case SC_ACTIVE_MONSTER_TRANSFORM:
				if( !mobdb_checkid(val1) )
					val1 = MOBID_PORING;
				break;
			case SC_C_MARKER:
			case SC_BURNT:
				tick_time = 1000;
				val4 = tick / tick_time;
				break;
			case SC_HEAT_BARREL: {
					uint8 coin = (uint8)(sd ? sd->spiritball_old : 10);

					val2 = (6 + 2 * val1) * coin; //+Atk%
					val3 = 5 * coin; //-Fixed cast%
					val4 = 25 + 5 * val1; //-Hit
				}
				break;
			case SC_P_ALTER: {
					uint8 coin = (uint8)(sd ? sd->spiritball_old : 10);

					val2 = 10 * (val1 + coin); //+Atk
					sc_start4(src,bl,SC_KYRIE,100,val1,0,0,status_sc2skill(type),tick);
				}
				break;
			case SC_ANTI_M_BLAST:
				val2 = val1 * 10;
				break;
			case SC_CATNIPPOWDER:
				val2 = 50; //-Atk%, -Matk%
				val3 = 25 * val1; //Move speed reduction
				break;
			case SC_BITESCAR: {
					struct status_data *b_status = status_get_base_status(src);

					val2 = status_get_max_hp(bl) * (val1 + b_status->dex / 25) / status_get_max_hp(bl); //MaxHP% damage
					tick_time = 1000;
					val4 = tick / tick_time;
				}
				break;
			case SC_ARCLOUSEDASH:
				val2 = 15 + 5 * val1; //Agi
				val3 = 25; //Move speed increase
				break;
			case SC_SHRIMP: {
					int heal = skill_calc_heal(src,bl,status_sc2skill(type),val1,true);

					status_heal(bl,heal,0,3);
					val2 = 10; //Atk%, Matk%
				}
				break;
			case SC_FRESHSHRIMP:
				val2 = skill_calc_heal(src,bl,status_sc2skill(type),val1,true);
				val3 = 11000 - 1000 * val1; //Heal interval
				tick_time = val3;
				val4 = tick / tick_time;
				break;
			case SC_TUNAPARTY: {
					struct map_session_data *ssd = map_id2sd(src->id);

					val2 = status_get_max_hp(src) * val1 / 10; //%MaxHP to absorb
					if( ssd && pc_checkskill(ssd,SU_SPIRITOFSEA) > 0 )
						val2 *= 2;
				}
				break;
			case SC_HISS:
				val2 = 500; //Perfect Dodge
				break;
			case SC_GROOMING:
				val2 = 100; //Flee
				break;
			case SC_CHATTERING:
				val2 = 100; //Watk & Matk
				break;
			case SC_DORAM_MATK:
				val2 = status_get_lv(bl);
				break;
			case SC_DORAM_FLEE2:
				val2 = 10 * status_get_lv(bl) / 12;
				break;
			case SC_SWORDCLAN:
			case SC_ARCWANDCLAN:
			case SC_GOLDENMACECLAN:
			case SC_CROSSBOWCLAN:
			case SC_JUMPINGCLAN:
				sc_start2(src,bl,SC_CLAN_INFO,100,0,val2,tick);
				break;
			case SC_DORAM_BUF_01:
			case SC_DORAM_BUF_02:
				tick_time = 10000; //Every 10 seconds
				val4 = tick / tick_time;
				break;
			default:
				if( calc_flag == SCB_NONE && icon == SI_BLANK && !StatusSkillChangeTable[type] && !StatusChangeStateTable[type] ) {
					switch( type ) {
						case SC_XMAS:
						case SC_SUMMER:
						case SC_AUTOTRADE:
						case SC_KSPROTECTED:
						case SC_HANBOK:
						case SC_OKTOBERFEST:
						case SC_INCMATKRATE:
						case SC_INCDEFRATE:
						case SC_NETHERWORLD_POSTDELAY:
						case SC_DORAM_SVSP:
							break; //Avoid the error, because this status has no skill associated and all values already store in it
						default:
							ShowError("Unknown Status Change [%d]\n",type);
							return 0; //Status change with no calc, no icon, and no skill associated?
					}
				}
				break;
		}
	} else { //Special considerations when loading SC data
		switch( type ) {
			case SC_WEDDING:
			case SC_XMAS:
			case SC_SUMMER:
			case SC_HANBOK:
			case SC_OKTOBERFEST:
			case SC_DRESSUP:
				if( !vd )
					break;
				clif_changelook(bl,LOOK_BASE,vd->class_);
				clif_changelook(bl,LOOK_WEAPON,0);
				clif_changelook(bl,LOOK_SHIELD,0);
				clif_changelook(bl,LOOK_CLOTHES_COLOR,vd->cloth_color);
				clif_changelook(bl,LOOK_BODY2,0);
				break;
			case SC_STONE:
				if( val3 )
					break; //Incubation time still active
			//Fall through
			case SC_POISON:
			case SC_DPOISON:
			case SC_BLEEDING:
			case SC_BURNING:
			case SC_TOXIN:
			case SC_MAGICMUSHROOM:
			case SC_PYREXIA:
			case SC_LEECHESEND:
			case SC_SV_ROOTTWIST:
				tick_time = tick;
				tick = tick_time + max(val4,0);
				break;
			case SC_ENCHANTARMS:
				icon = SI_ATTACK_PROPERTY_NOTHING + val2;
				break;
			case SC_BANDING: {
					struct skill_unit_group *group = NULL;

					if( (group = skill_unitsetting(bl,LG_BANDING,val1,bl->x,bl->y,0)) )
						val4 = group->group_id;
				}
				break;
			case SC_KYOUGAKU:
				val1 = MOBID_PORING;
				break;
			case SC_SWORDCLAN:
			case SC_ARCWANDCLAN:
			case SC_GOLDENMACECLAN:
			case SC_CROSSBOWCLAN:
			case SC_JUMPINGCLAN:
			case SC_CLAN_INFO:
				if( sd && !sd->status.clan_id )
					return 0; //If the player still has a clan status, but was removed from his clan
				break;
		}
	}

	switch (type) { //Values that must be set regardless of SCFLAG_LOADED e.g. val_flag
		case SC_EXPBOOST:
		case SC_JEXPBOOST:
		case SC_ITEMBOOST:
		case SC_ROLLINGCUTTER:
		case SC_SPHERE_1:
		case SC_SPHERE_2:
		case SC_SPHERE_3:
		case SC_SPHERE_4:
		case SC_SPHERE_5:
		case SC_OVERHEAT:
		case SC_LIGHTNINGWALK:
		case SC_PUSH_CART:
		case SC_KYOUGAKU:
		case SC_MONSTER_TRANSFORM:
		case SC_ACTIVE_MONSTER_TRANSFORM:
		case SC_JP_EVENT04:
		case SC_SWORDCLAN:
		case SC_ARCWANDCLAN:
		case SC_GOLDENMACECLAN:
		case SC_CROSSBOWCLAN:
		case SC_JUMPINGCLAN:
		case SC_DRESSUP:
			val_flag |= 1;
			break;
		case SC_ENCHANTBLADE:
			val_flag |= 2;
			break;
		case SC_FIGHTINGSPIRIT:
		case SC_VENOMIMPRESS:
		case SC_WEAPONBLOCKING:
		case SC__INVISIBILITY:
		case SC__ENERVATION:
		case SC__WEAKNESS:
		case SC_PROPERTYWALK:
		case SC_SHIELDSPELL_DEF:
		case SC_SHIELDSPELL_MDEF:
		case SC_SHIELDSPELL_REF:
		case SC_CRESCENTELBOW:
		case SC_CHILLY_AIR_OPTION:
		case SC_GUST_OPTION:
		case SC_WILD_STORM_OPTION:
		case SC_UPHEAVAL_OPTION:
		case SC_CIRCLE_OF_FIRE_OPTION:
		case SC_CLAN_INFO:
		case SC_DAILYSENDMAILCNT:
			val_flag |= 1|2;
			break;
		case SC_POISONINGWEAPON:
		case SC_CLOAKINGEXCEED:
		case SC_HALLUCINATIONWALK:
		case SC__SHADOWFORM:
		case SC__GROOMY:
		case SC__LAZINESS:
		case SC__UNLUCKY:
		case SC_FORCEOFVANGUARD:
		case SC_SPELLFIST:
		case SC_CURSEDCIRCLE_ATKER:
		case SC_PYROTECHNIC_OPTION:
		case SC_HEATER_OPTION:
		case SC_AQUAPLAY_OPTION:
		case SC_COOLER_OPTION:
		case SC_BLAST_OPTION:
		case SC_PETROLOGY_OPTION:
		case SC_CURSED_SOIL_OPTION:
		case SC_WATER_BARRIER:
			val_flag |= 1|2|4;
			break;
		case SC_EL_WAIT:
		case SC_EL_PASSIVE:
		case SC_EL_DEFENSIVE:
		case SC_EL_OFFENSIVE:
			if (bl->type == BL_ELEM) {
				struct elemental_data *ed = BL_CAST(BL_ELEM,bl);

				elemental_unlocktarget(ed);
				elemental_clean_effect(ed);
				switch (type - SC_EL_PASSIVE) {
					case EL_MODE_DEFENSIVE:
						ed->battle_status.mode = (MD_CANMOVE|MD_ASSIST);
						break;
					case EL_MODE_OFFENSIVE:
						ed->battle_status.mode = (MD_CANMOVE|MD_AGGRESSIVE|MD_CANATTACK);
						break;
					case EL_MODE_WAIT:
					case EL_MODE_PASSIVE:
						ed->battle_status.mode = MD_CANMOVE;
						break;
				}
				elemental_change_mode_ack(ed,type - SC_EL_PASSIVE);
			}
			break;
	}

	switch (type) { //Permanent effects
		case SC_MODECHANGE:
		case SC_WEIGHT50:
		case SC_WEIGHT90:
		case SC_BROKENWEAPON:
		case SC_BROKENARMOR:
		case SC_TRICKDEAD:
		case SC_AUTOBERSERK:
		case SC_SIGNUMCRUCIS:
		case SC_AETERNA:
		case SC_CLOAKING:
		case SC_CLOAKINGEXCEED:
		case SC_MAXIMIZEPOWER:
		case SC_READYSTORM:
		case SC_READYDOWN:
		case SC_READYCOUNTER:
		case SC_READYTURN:
		case SC_DODGE:
		case SC_RUN:
		case SC_WUGDASH:
		case SC_TENSIONRELAX:
		case SC_SACRIFICE:
		case SC_REJECTSWORD:
		case SC_CHASEWALK:
		case SC_MEMORIZE:
		case SC_PUSH_CART:
		case SC_NEUTRALBARRIER:
		case SC_OVERHEAT:
		case SC_OVERHEAT_LIMITPOINT:
		case SC_STEALTHFIELD:
		case SC__REPRODUCE:
		case SC__INVISIBILITY:
		case SC_CLOUD_KILL:
		case SC_WARMER:
		case SC_FORCEOFVANGUARD:
		case SC_BANDING:
		case SC_STYLE_CHANGE:
		case SC_MOONSTAR:
		case SC_SUPER_STAR:
		case SC_STRANGELIGHTS:
		case SC_DECORATION_OF_MUSIC:
		case SC_ALL_RIDING:
		case SC_MILLENNIUMSHIELD:
		case SC_SPRITEMABLE:
		case SC_SOULATTACK:
		case SC_HAT_EFFECT:
		case SC_QSCARABA:
		case SC_LJOSALFAR:
		case SC_MAPLE_FALLS:
		case SC_MERMAID_LONGING:
		case SC_TIME_ACCESSORY:
		case SC_SWORDCLAN:
		case SC_ARCWANDCLAN:
		case SC_GOLDENMACECLAN:
		case SC_CROSSBOWCLAN:
		case SC_JUMPINGCLAN:
		case SC_CLAN_INFO:
		case SC_DAILYSENDMAILCNT:
			tick = -1;
			break;
		case SC_ENDURE:
		case SC_DEFSET_NUM:
		case SC_MDEFSET_NUM:
			if (val4)
				tick = -1;
			break;
	}

	switch (type) {
		//Those that make you stop attacking/walking
		case SC_WHITEIMPRISON:
		case SC_DEEPSLEEP:
		case SC_CRYSTALIZE:
			if (sd && pc_issit(sd))
				pc_setstand(sd); //Avoid sprite sync problems
		//Fall through
		case SC_FREEZE:
		case SC_STUN:
			if (sc->data[SC_DANCING])
				unit_stop_walking(bl,USW_FIXPOS);
		//Fall through
		case SC_TRICKDEAD:
			status_change_end(bl,SC_DANCING,INVALID_TIMER);
		//Fall through
		case SC_SLEEP:
		case SC_STONE:
			if (battle_config.sc_castcancel&bl->type)
				unit_skillcastcancel(bl,0); //Cancel cast when get status [LuzZza]
		//Fall through
		case SC__MANHOLE:
		case SC_CURSEDCIRCLE_ATKER:
		case SC_FALLENEMPIRE:
		case SC_KINGS_GRACE:
			unit_stop_attack(bl);
			if (type == SC_FREEZE || type == SC_STUN || type == SC_SLEEP || type == SC_STONE || type == SC_WHITEIMPRISON)
				break;
		//Fall through
		case SC_STOP:
		case SC_CONFUSION:
		case SC_CLOSECONFINE:
		case SC_CLOSECONFINE2:
		case SC_PARALYSIS:
		case SC_KAGEHUMI:
			unit_stop_walking(bl,USW_FIXPOS);
			break;
		case SC_CURSEDCIRCLE_TARGET:
			unit_stop_attack(bl);
		//Fall through
		case SC_BITE:
		case SC_MAGNETICFIELD:
		case SC_NETHERWORLD:
		case SC_THORNSTRAP:
		case SC_TINDER_BREAKER:
		case SC_SV_ROOTTWIST:
			if (!unit_blown_immune(bl,0x1))
				unit_stop_walking(bl,USW_FIXPOS);
			break;
		case SC_ANKLE:
		case SC_SPIDERWEB:
		case SC_WIDEWEB:
		case SC_ELECTRICSHOCKER:
			if (!unit_blown_immune(bl,0x9))
				unit_stop_walking(bl,USW_FIXPOS);
			break;
		case SC_WEIGHT90:
		case SC_HIDING:
		case SC_CLOAKING:
		case SC_CHASEWALK:
		case SC_CLOAKINGEXCEED:
		case SC_CAMOUFLAGE:
		case SC_STEALTHFIELD:
		case SC__INVISIBILITY:
		case SC_VOICEOFSIREN:
		case SC_ALL_RIDING:
		case SC_WEDDING:
		case SC_XMAS:
		case SC_SUMMER:
		case SC_HANBOK:
		case SC_OKTOBERFEST:
		case SC_DRESSUP:
		case SC_SUHIDE:
			unit_stop_attack(bl);
			break;
		case SC_SILENCE:
			if (battle_config.sc_castcancel&bl->type)
				unit_skillcastcancel(bl,0);
			break;
	}

	//Set option as needed
	opt_flag = 1;
	switch (type) {
		//OPT1
		case SC_STONE:
			sc->opt1 = (val3 ? OPT1_STONEWAIT : OPT1_STONE);
			break;
		case SC_FREEZE:
			sc->opt1 = OPT1_FREEZE;
			break;
		case SC_STUN:
			sc->opt1 = OPT1_STUN;
			break;
		case SC_SLEEP:
			sc->opt1 = OPT1_SLEEP;
			break;
		case SC_BURNING: //Burning need this to be showed correctly [pakpil]
			sc->opt1 = OPT1_BURNING;
			break;
		case SC_WHITEIMPRISON:
			sc->opt1 = OPT1_IMPRISON;
			break;
		//OPT2
		case SC_POISON:
			sc->opt2 |= OPT2_POISON;
			break;
		case SC_CURSE:
			sc->opt2 |= OPT2_CURSE;
			break;
		case SC_SILENCE:
			sc->opt2 |= OPT2_SILENCE;
			break;
		case SC_CONFUSION:
		case SC_SIGNUMCRUCIS:
			sc->opt2 |= OPT2_SIGNUMCRUCIS;
			break;
		case SC_BLIND:
			sc->opt2 |= OPT2_BLIND;
			break;
		case SC_ANGELUS:
			sc->opt2 |= OPT2_ANGELUS;
			break;
		case SC_BLEEDING:
			sc->opt2 |= OPT2_BLEEDING;
			break;
		case SC_DPOISON:
			sc->opt2 |= OPT2_DPOISON;
			break;
		case SC_FEAR:
			sc->opt2 |= OPT2_FEAR;
			break;
		//OPT3
		case SC_TWOHANDQUICKEN:
		case SC_ONEHAND:
		case SC_SPEARQUICKEN:
		case SC_CONCENTRATION:
		case SC_MERC_QUICKEN:
			sc->opt3 |= OPT3_QUICKEN;
			opt_flag = 0;
			break;
		case SC_MAXOVERTHRUST:
		case SC_OVERTHRUST:
		case SC_SWOO: //Why does it shares the same opt as Overthrust? Perhaps we'll never know
			sc->opt3 |= OPT3_OVERTHRUST;
			opt_flag = 0;
			break;
		case SC_ENERGYCOAT:
		case SC_SKE:
			sc->opt3 |= OPT3_ENERGYCOAT;
			opt_flag = 0;
			break;
		case SC_INCATKRATE:
			if (bl->type != BL_MOB) { //Simulate Explosion Spirits effect for NPC_POWERUP [Skotlex]
				opt_flag = 0;
				break;
			}
		//Fall through
		case SC_EXPLOSIONSPIRITS:
			sc->opt3 |= OPT3_EXPLOSIONSPIRITS;
			opt_flag = 0;
			break;
		case SC_STEELBODY:
		case SC_SKA:
			sc->opt3 |= OPT3_STEELBODY;
			opt_flag = 0;
			break;
		case SC_BLADESTOP:
			sc->opt3 |= OPT3_BLADESTOP;
			opt_flag = 0;
			break;
		case SC_AURABLADE:
			sc->opt3 |= OPT3_AURABLADE;
			opt_flag = 0;
			break;
		case SC_BERSERK:
		case SC_SATURDAYNIGHTFEVER:
			sc->opt3 |= OPT3_BERSERK;
			opt_flag = 0;
			break;
		//case ???: //Doesn't seem to do anything
			//sc->opt3 |= OPT3_LIGHTBLADE;
			//opt_flag = 0;
			//break;
		case SC_DANCING:
			if ((val1&0xFFFF) == CG_MOONLIT)
				sc->opt3 |= OPT3_MOONLIT;
			opt_flag = 0;
			break;
		case SC_MARIONETTE:
		case SC_MARIONETTE2:
			sc->opt3 |= OPT3_MARIONETTE;
			opt_flag = 0;
			break;
		case SC_ASSUMPTIO:
			sc->opt3 |= OPT3_ASSUMPTIO;
			opt_flag = 0;
			break;
		case SC_WARM: //SG skills [Komurka]
			sc->opt3 |= OPT3_WARM;
			opt_flag = 0;
			break;
		case SC_KAITE:
			sc->opt3 |= OPT3_KAITE;
			opt_flag = 0;
			break;
		case SC_BUNSINJYUTSU:
			sc->opt3 |= OPT3_BUNSIN;
			opt_flag = 0;
			break;
		case SC_SPIRIT:
			sc->opt3 |= OPT3_SOULLINK;
			opt_flag = 0;
			break;
		case SC_CHANGEUNDEAD:
			sc->opt3 |= OPT3_UNDEAD;
			opt_flag = 0;
			break;
		//case ???: //From DA_CONTRACT (looks like biolab mobs aura)
			//sc->opt3 |= OPT3_CONTRACT;
			//opt_flag = 0;
			//break;
		//OPTION
		case SC_HIDING:
			sc->option |= OPTION_HIDE;
			opt_flag = 2;
			break;
		case SC_CLOAKING:
		case SC_CLOAKINGEXCEED:
		case SC__INVISIBILITY:
			sc->option |= OPTION_CLOAK;
		//Fall through
		case SC_CAMOUFLAGE:
		case SC_STEALTHFIELD:
		case SC__SHADOWFORM:
			opt_flag = 2;
			break;
		case SC_CHASEWALK:
			sc->option |= OPTION_CHASEWALK|OPTION_CLOAK;
			opt_flag = 2;
			break;
		case SC__FEINTBOMB:
			sc->option |= OPTION_INVISIBLE;
			opt_flag |= 0x4;
			break;
		case SC_SIGHT:
			sc->option |= OPTION_SIGHT;
			break;
		case SC_RUWACH:
			sc->option |= OPTION_RUWACH;
			break;
		case SC_WEDDING:
			sc->option |= OPTION_WEDDING;
			opt_flag |= 0x4;
			break;
		case SC_XMAS:
			sc->option |= OPTION_XMAS;
			opt_flag |= 0x4;
			break;
		case SC_SUMMER:
			sc->option |= OPTION_SUMMER;
			opt_flag |= 0x4;
			break;
		case SC_HANBOK:
			sc->option |= OPTION_HANBOK;
			opt_flag |= 0x4;
			break;
		case SC_OKTOBERFEST:
			sc->option |= OPTION_OKTOBERFEST;
			opt_flag |= 0x4;
			break;
		case SC_DRESSUP:
			sc->option |= OPTION_SUMMER2;
			opt_flag |= 0x4;
			break;
		case SC_ORCISH:
			sc->option |= OPTION_ORCISH;
			break;
		case SC_FUSION:
			sc->option |= OPTION_FLYING;
			break;
		default:
			opt_flag = 0;
			break;
	}

	if (opt_flag) { //On Aegis, when turning on a status change, first goes the option packet, then the sc packet
		clif_changeoption(bl);
		if (sd && (opt_flag&0x4)) {
			if (vd) {
				clif_changelook(bl,LOOK_BASE,vd->class_);
				clif_changelook(bl,LOOK_CLOTHES_COLOR,vd->cloth_color);
			}
			clif_changelook(bl,LOOK_WEAPON,0);
			clif_changelook(bl,LOOK_SHIELD,0);
		}
	}

	if (calc_flag&SCB_DYE) { //Reset dye color
		if (vd && vd->cloth_color) {
			val4 = vd->cloth_color;
			clif_changelook(bl,LOOK_CLOTHES_COLOR,0);
		}
		calc_flag &= ~SCB_DYE;
	}

	//Might be needed in the future [Rytech]
	/*if (calc_flag&SCB_BODY) { //Reset body style
		if (vd && vd->body_style) {
			val4 = vd->body_style;
			clif_changelook(bl,LOOK_BODY2,0);
		}
		calc_flag &= ~SCB_BODY;
	}*/

	if (!(flag&SCFLAG_NOICON) && !(flag&SCFLAG_LOADED && StatusDisplayType[type]))
		clif_status_change(bl,icon,1,tick,(val_flag&1) ? val1 : 1,(val_flag&2) ? val2 : 0,(val_flag&4) ? val3 : 0);

	//Don't trust the previous sce assignment, in case the SC ended somewhere between there and here
	if ((sce = sc->data[type])) { //Reuse old sc
		if (sce->timer != INVALID_TIMER)
			delete_timer(sce->timer,status_change_timer);
		sc_isnew = false;
	} else { //New sc
		++sc->count;
		sce = sc->data[type] = ers_alloc(sc_data_ers,struct status_change_entry);
	}

	sce->val1 = val1;
	sce->val2 = val2;
	sce->val3 = val3;
	sce->val4 = val4;

	if (tick_time) { //Used as temporary storage for scs with interval ticks, so that the actual duration is sent to the client first
		if (status_get_sc_interval(type))
			sce->timer = add_timer(gettick() + tick_time,status_change_timer,bl->id,type);
		else
			sce->timer = add_timer(gettick(),status_change_timer,bl->id,type);
	} else if (tick >= 0)
		sce->timer = add_timer(gettick() + tick,status_change_timer,bl->id,type);
	else
		sce->timer = INVALID_TIMER; //Infinite duration

	if (calc_flag)
		status_calc_bl(bl,calc_flag);

	if (StatusChangeStateTable[type] && sc_isnew) //Non-zero
		status_calc_state(bl,sc,(enum scs_flag)StatusChangeStateTable[type],true);

	if (StatusDisplayType[type]) { //[Ind]
		int dval1 = 0, dval2 = 0, dval3 = 0;

		switch (type) {
			case SC_ALL_RIDING:
				dval1 = 1;
				break;
			case SC_CLAN_INFO:
				dval1 = sce->val1;
				dval2 = sce->val2;
				dval3 = sce->val3;
				break;
			default: //All others: just copy val1
				dval1 = sce->val1;
				break;
		}
		status_display_add(bl,type,dval1,dval2,dval3);
		clif_efst_set_enter(bl,bl,AREA);
	}

	if (sd) {
		if (sd->pd)
			pet_sc_check(sd,type); //Pet Status Effect Healing [Skotlex]
		switch (type) {
			case SC_BERSERK:
			case SC_MERC_HPUP:
			case SC_MERC_SPUP:
				status_calc_pc(sd,SCO_FORCE);
				break;
			default:
				if (!sd->state.connect_new)
					status_calc_pc(sd,SCO_NONE);
				break;
		}
	}

	//1st thing to execute when loading status
	switch (type) {
		case SC_BERSERK:
			if (!sce->val2) { //Don't heal if already set
				status_heal(bl,status->max_hp,0,1); //Do not use percent_heal as this healing must override BERSERK's block
				status_set_sp(bl,0,0); //Damage all SP
			}
			sce->val2 = 5 * status->max_hp / 100;
			break;
		case SC_RUN: {
				struct unit_data *ud = NULL;

				if ((ud = unit_bl2ud(bl)))
					ud->state.running = unit_run(bl,NULL,SC_RUN);
			}
			break;
		case SC_BOSSMAPINFO:
			if (sd)
				clif_bossmapinfo(sd,map_id2boss(sce->val1),BOSS_INFO_ALIVE_WITHMSG); //First Message
			break;
		case SC_MERC_HPUP:
		case SC_FULL_THROTTLE:
			status_percent_heal(bl,100,0); //Recover Full HP
			break;
		case SC_MERC_SPUP:
			status_percent_heal(bl,0,100); //Recover Full SP
			break;
		case SC_WUGDASH: {
				struct unit_data *ud = NULL;

				if ((ud = unit_bl2ud(bl)))
					ud->state.running = unit_run(bl,sd,SC_WUGDASH);
			}
			break;
		case SC_COMBO:
			switch (sce->val1) {
				case TK_STORMKICK:
					skill_combo_toggle_inf(bl,TK_JUMPKICK,0);
					clif_skill_nodamage(bl,bl,TK_READYSTORM,1,1);
					break;
				case TK_DOWNKICK:
					skill_combo_toggle_inf(bl,TK_JUMPKICK,0);
					clif_skill_nodamage(bl,bl,TK_READYDOWN,1,1);
					break;
				case TK_TURNKICK:
					skill_combo_toggle_inf(bl,TK_JUMPKICK,0);
					clif_skill_nodamage(bl,bl,TK_READYTURN,1,1);
					break;
				case TK_COUNTER:
					skill_combo_toggle_inf(bl,TK_JUMPKICK,0);
					clif_skill_nodamage(bl,bl,TK_READYCOUNTER,1,1);
					break;
				default: //Rest just toggle inf to enable autotarget
					skill_combo_toggle_inf(bl,sce->val1,INF_SELF_SKILL);
					break;
			}
			break;
		case SC_SATURDAYNIGHTFEVER:
			if (!sce->val2)
				status_heal(bl,status->max_hp,0,1);
			sce->val2 = status->max_hp / 100;
			break;
		case SC_C_MARKER: {
				struct map_session_data *ssd = NULL;

				if ((ssd = map_id2sd(src->id)))
					clif_crimson_marker(ssd,bl,0); //Send mini-map, don't wait for first timer triggered
			}
			break;
		case SC_GVG_GIANT:
		case SC_GVG_GOLEM:
		case SC_GVG_STUN:
		case SC_GVG_STONE:
		case SC_GVG_FREEZ:
		case SC_GVG_SLEEP:
		case SC_GVG_CURSE:
		case SC_GVG_SILENCE:
		case SC_GVG_BLIND:
			if (val1 || val2)
				status_zap(bl,(val1 ? val1 : 0),(val2 ? val2 : 0));
			break;
	}

	if ((opt_flag&2) && sd && sd->touching_id)
		npc_touchnext_areanpc(sd,false); //Run OnTouch_ on next char in range

	return 1;
}

/*==========================================
 * Ending all status except those listed.
 * @TODO: Maybe usefull for dispel instead reseting a list there.
 * type:
 * 0 - PC killed -> Place here statuses that do not dispel on death.
 * 1 - If for some reason status_change_end decides to still keep the status when quitting.
 * 2 - Do clif.
 * 3 - Do not remove some permanent/time-independent effects and also do clif.
 *------------------------------------------*/
int status_change_clear(struct block_list *bl, int type)
{
	struct status_change *sc = status_get_sc(bl);
	int i;

	if (!sc)
		return 0;

	//Cleaning all extras vars
	sc->pos_x = 0;
	sc->pos_y = 0;
#ifndef RENEWAL
	sc->sg_counter = 0;
#endif
	sc->bs_counter = 0;

	if (!sc->count)
		return 0;

	for (i = 0; i < SC_MAX; i++) {
		if (!sc->data[i])
			continue;
		if (!type) {
			if (status_get_sc_type(i)&SC_NO_REM_DEATH) {
				switch (i) { //Type 0: PC killed -> Place here statuses that do not dispel on death
					case SC_ELEMENTALCHANGE: //Only when its Holy or Dark that it doesn't dispell on death
						if (sc->data[i]->val2 != ELE_HOLY && sc->data[i]->val2 != ELE_DARK)
							break;
					//Fall through
					default:
						continue;
				}
			}
		}
		if (type == 3 && status_get_sc_type(i)&SC_NO_CLEAR)
			continue;
		status_change_end(bl,(sc_type)i,INVALID_TIMER);
		//If for some reason status_change_end decides to still keep the status when quitting [Skotlex]
		if (type == 1 && sc->data[i]) {
			sc->count--;
			if (sc->data[i]->timer != INVALID_TIMER)
				delete_timer(sc->data[i]->timer,status_change_timer);
			ers_free(sc_data_ers,sc->data[i]);
			sc->data[i] = NULL;
		}
	}

	sc->opt1 = 0;
	sc->opt2 = 0;
	sc->opt3 = 0;

	if (!type || type == 2 || type == 3)
		clif_changeoption(bl);

	return 1;
}

/*==========================================
 * Special condition we want to effectuate, check before ending a status.
 *------------------------------------------*/
int status_change_end_(struct block_list *bl, enum sc_type type, int tid, const char *file, int line)
{
	struct map_session_data *sd;
	struct status_change *sc;
	struct status_change_entry *sce;
	struct status_data *status;
	struct view_data *vd;
	int opt_flag = 0, calc_flag, icon = SI_BLANK;
	bool invisible = false;

	nullpo_ret(bl);

	sc = status_get_sc(bl);
	status = status_get_status_data(bl);

	if (type < 0 || type >= SC_MAX || !sc || !(sce = sc->data[type]))
		return 0;

	sd = BL_CAST(BL_PC,bl);

	if (sce->timer != tid && sce->timer != INVALID_TIMER && tid != INVALID_TIMER)
		return 0;

	if (tid == INVALID_TIMER) {
		if (type == SC_ENDURE && sce->val4)
			return 0;
		if (type == SC_SPIDERWEB) {
			//Delete the unit group first to expire found in the status change
			struct skill_unit_group *group = NULL, *group2 = NULL;
			unsigned int tick = gettick();
			int pos = 1;

			if (sce->val2 && !(group = skill_id2group(sce->val2)))
				sce->val2 = 0;
			if (sce->val3) {
				if (!(group2 = skill_id2group(sce->val3)))
					sce->val3 = 0;
				else if (!group || ((group->limit - DIFF_TICK(tick,group->tick)) > (group2->limit - DIFF_TICK(tick,group2->tick)))) {
					group = group2;
					pos = 2;
				}
			}
			if (sce->val4) {
				if (!(group2 = skill_id2group(sce->val4)))
					sce->val4 = 0;
				else if (!group || ((group->limit - DIFF_TICK(tick,group->tick)) > (group2->limit - DIFF_TICK(tick,group2->tick)))) {
					group = group2;
					pos = 3;
				}
			}
			if (pos == 1)
				sce->val2 = 0;
			else if (pos == 2)
				sce->val3 = 0;
			else if (pos == 3)
				sce->val4 = 0;
			if (group)
				skill_delunitgroup(group);
			if (!status_isdead(bl) && (sce->val2 || sce->val3 || sce->val4))
				return 0; //Don't end the status change yet as there are still unit groups associated with it
		}
		if (sce->timer != INVALID_TIMER) //Could be a SC with infinite duration
			delete_timer(sce->timer,status_change_timer);
		if (sc->opt1) {
			switch (type) {
				//"Ugly workaround" [Skotlex]
				//Delays status change ending so that a skill that sets OPT1 fails to trigger when it also removed one
				case SC_STONE:
					sce->val4 = -1; //Petrify time
				//Fall through
				case SC_FREEZE:
				case SC_STUN:
				case SC_SLEEP:
				case SC_WHITEIMPRISON:
					if (sce->val1) {
						//Removing the 'level' shouldn't affect anything in the code
						//since these SC are not affected by it, and it lets us know
						//if we have already delayed this attack or not
						sce->val1 = 0;
						sce->timer = add_timer(gettick() + 10,status_change_timer,bl->id,type);
						return 1;
					}
					break;
			}
		}
	}

	sc->count--;

	if (StatusChangeStateTable[type])
		status_calc_state(bl,sc,(enum scs_flag)StatusChangeStateTable[type],false);

	sc->data[type] = NULL;

	if (StatusDisplayType[type])
		status_display_remove(bl,type);

	if (sc->option&(OPTION_HIDE|OPTION_CLOAK))
		invisible = true;

	vd = status_get_viewdata(bl);
	calc_flag = StatusChangeFlagTable[type];
	icon = StatusIconChangeTable[type];

	switch (type) {
		case SC_ENCHANTARMS:
			icon = SI_ATTACK_PROPERTY_NOTHING + sce->val2;
			break;
		case SC_GRANITIC_ARMOR: {
				int damage = status->max_hp * sce->val3 / 100;

				if (damage >= status->hp) //Don't kill
					damage = status->hp - 1;
				status_zap(bl,damage,0);
			}
			break;
		case SC_PYROCLASTIC:
			if (sd)
				skill_break_equip(bl,bl,EQP_WEAPON,10000,BCT_SELF);
			break;
		case SC_KEEPING: {
				struct unit_data *ud = unit_bl2ud(bl);

				if (ud) //Need to reset these here for mob_class_change [exneval]
					ud->attackabletime = ud->canact_tick = ud->canmove_tick = gettick();
			}
			break;
		case SC_RUN: {
				struct unit_data *ud = unit_bl2ud(bl);
				bool begin_strup = true;

				if (ud) {
					if (!ud->state.running)
						begin_strup = false;
					ud->state.running = 0;
					if (ud->walktimer != INVALID_TIMER)
						unit_stop_walking(bl,USW_FIXPOS);
				}
				if (begin_strup && sce->val1 >= 7 && DIFF_TICK(gettick(),sce->val4) <= 1000 && (!sd || sd->status.weapon == W_FIST))
					sc_start(bl,bl,SC_STRUP,100,sce->val1,skill_get_time2(status_sc2skill(type),sce->val1));
			}
			break;
		case SC_AUTOBERSERK:
			if (sc->data[SC_PROVOKE] && sc->data[SC_PROVOKE]->val2)
				status_change_end(bl,SC_PROVOKE,INVALID_TIMER);
			break;
		case SC_ENDURE:
		case SC_AUTOGUARD:
		case SC_REFLECTSHIELD:
		case SC_DEFENDER:
			{
				struct map_session_data *tsd = NULL;

				if (bl->type == BL_PC) { //Clear status from others
					int i;

					for (i = 0; i < MAX_DEVOTION; i++) {
						if (sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) && tsd->sc.data[type])
							status_change_end(&tsd->bl,type,INVALID_TIMER);
					}
					if (type == SC_ENDURE && (tsd = map_id2sd(sd->shadowform_id)))
						status_change_end(&tsd->bl,type,INVALID_TIMER);
				} else if (bl->type == BL_MER && ((TBL_MER *)bl)->devotion_flag) { //Clear status from master
					if ((tsd = ((TBL_MER *)bl)->master) && tsd->sc.data[type])
						status_change_end(&tsd->bl,type,INVALID_TIMER);
				}
			}
			break;
		case SC_DEVOTION: {
				struct block_list *d_bl = map_id2bl(sce->val1);

				if (d_bl) {
					if (d_bl->type == BL_PC)
						((TBL_PC *)d_bl)->devotion[sce->val2] = 0;
					else if (d_bl->type == BL_MER)
						((TBL_MER *)d_bl)->devotion_flag = 0;
					clif_devotion(d_bl,NULL);
				}
				status_change_end(bl,SC_ENDURE,INVALID_TIMER);
				status_change_end(bl,SC_AUTOGUARD,INVALID_TIMER);
				status_change_end(bl,SC_REFLECTSHIELD,INVALID_TIMER);
				status_change_end(bl,SC_DEFENDER,INVALID_TIMER);
			}
			break;
		case SC_BLADESTOP:
			if (sce->val4) {
				int target_id = sce->val4; //Stop the status for the other guy of bladestop as well
				struct block_list *tbl = map_id2bl(target_id);
				struct status_change *tsc = status_get_sc(tbl);

				sce->val4 = 0;
				if (tbl && tsc && tsc->data[SC_BLADESTOP]) {
					tsc->data[SC_BLADESTOP]->val4 = 0;
					status_change_end(tbl,SC_BLADESTOP,INVALID_TIMER);
				}
				clif_bladestop(bl,target_id,0);
			}
			break;
		case SC_DANCING: {
				struct map_session_data *dsd;
				struct status_change_entry *dsc;

				if (sce->val4 && sce->val4 != BCT_SELF && (dsd = map_id2sd(sce->val4))) { //End status on partner as well
					dsc = dsd->sc.data[SC_DANCING];
					if (dsc) { //This will prevent recursive loops
						dsc->val2 = dsc->val4 = 0;
						status_change_end(&dsd->bl,SC_DANCING,INVALID_TIMER);
					}
				}
				if (sce->val2) { //Erase associated land skill
					struct skill_unit_group *group = skill_id2group(sce->val2);

					if (!group) {
						ShowDebug("status_change_end: SC_DANCING is missing skill unit group (val1=%d, val2=%d, val3=%d, val4=%d, timer=%d, tid=%d, char_id=%d, map=%s, x=%d, y=%d). Please report this!\n",
							sce->val1,sce->val2,sce->val3,sce->val4,sce->timer,tid,
							(sd ? sd->status.char_id : 0),
							mapindex_id2name(map_id2index(bl->m)),bl->x,bl->y);
					}
					sce->val2 = 0;
					if (group)
						skill_delunitgroup(group);
				}
				if ((sce->val1&0xFFFF) == CG_MOONLIT)
					clif_status_load(bl,SI_MOON,0);
				status_change_end(bl,SC_LONGING,INVALID_TIMER);
			}
			break;
		case SC_NOCHAT:
			if (sd && sd->status.manner < 0 && tid != INVALID_TIMER)
				sd->status.manner = 0;
			if (sd && tid == INVALID_TIMER) {
				clif_changestatus(sd,SP_MANNER,sd->status.manner);
				clif_updatestatus(sd,SP_MANNER);
			}
			break;
		case SC_SPLASHER:
		case SC_SPORE_EXPLOSION:
			{
				struct block_list *src = NULL;

				if ((src = map_id2bl(sce->val2)) && tid != INVALID_TIMER)
					skill_castend_damage_id(src,bl,status_sc2skill(type),sce->val1,gettick(),SD_LEVEL);
			}
			break;
		case SC_CLOSECONFINE2: {
				struct block_list *src = (sce->val2 ? map_id2bl(sce->val2) : NULL);
				struct status_change *sc2 = (src ? status_get_sc(src) : NULL);

				if (src && sc2 && sc2->data[SC_CLOSECONFINE]) //If status was already ended, do nothing
					if (--(sc2->data[SC_CLOSECONFINE]->val2) <= 0) //Decrease count
						status_change_end(src,SC_CLOSECONFINE,INVALID_TIMER); //No more holds, free him up
			}
			break;
		case SC_CLOSECONFINE:
			if (sce->val2 > 0) { //Caster has been unlocked, nearby chars need to be unlocked
				int range = 1 +
					skill_get_range2(bl,status_sc2skill(type),sce->val1,true) +
					skill_get_range2(bl,TF_BACKSLIDING,1,true); //Since most people use this to escape the hold

				map_foreachinallarea(status_change_timer_sub,
					bl->m,bl->x-range,bl->y-range,bl->x+range,bl->y+range,BL_CHAR,bl,sce,type,gettick());
			}
			break;
		case SC_COMBO:
			skill_combo_toggle_inf(bl,sce->val1,0);
			break;
		case SC_MARIONETTE:
		case SC_MARIONETTE2: //Marionette target
			if (sce->val1) { //Check for partner and end their marionette status as well
				enum sc_type type2 = (type == SC_MARIONETTE) ? SC_MARIONETTE2 : SC_MARIONETTE;
				struct block_list *pbl = map_id2bl(sce->val1);
				struct status_change *sc2 = pbl ? status_get_sc(pbl) : NULL;

				if (sc2 && sc2->data[type2]) {
					sc2->data[type2]->val1 = 0;
					status_change_end(pbl,type2,INVALID_TIMER);
				}
			}
			break;
		case SC_BERSERK:
			if (status->hp > 200 && sc->data[SC__BLOODYLUST]) {
				status_percent_heal(bl,100,0);
				status_change_end(bl,SC__BLOODYLUST,INVALID_TIMER);
			} else if (status->hp > 100 && sce->val2)
				status_set_hp(bl,100,0); //Check if status isn't ended by skill (when val2 = 0)
			if (sc->data[SC_ENDURE] && sc->data[SC_ENDURE]->val4) {
				sc->data[SC_ENDURE]->val4 = 0;
				status_change_end(bl,SC_ENDURE,INVALID_TIMER);
			}
			sc_start4(bl,bl,SC_REGENERATION,100,10,0,0,(RGN_HP|RGN_SP),skill_get_time(status_sc2skill(type),sce->val1));
			break;
		case SC_BASILICA: //Clear the skill area [Skotlex]
			if (sce->val3 && sce->val4 == bl->id) {
				struct skill_unit_group *group = skill_id2group(sce->val3);

				sce->val3 = 0;
				if (group) //Might have been cleared before status ended, e.g. land protector
					skill_delunitgroup(group);
			}
			break;
		case SC_GOSPEL:
			if (sce->val3) {
				struct skill_unit_group *group = skill_id2group(sce->val3);

				sce->val3 = 0;
				if (group)
					skill_delunitgroup(group);
			}
			break;
		case SC_HERMODE:
			if (sce->val3 == BCT_SELF)
				skill_clear_unitgroup(bl);
			break;
		case SC_TRICKDEAD:
			if (vd)
				vd->dead_sit = 0;
			break;
		case SC_WARM:
		case SC_BANDING:
			if (sce->val4) {
				struct skill_unit_group *group = skill_id2group(sce->val4);

				sce->val4 = 0;
				if (group)
					skill_delunitgroup(group);
			}
			break;
		case SC_JAILED:
			if (tid == INVALID_TIMER)
				break;
			if (sd && sd->mapindex == sce->val2) //Natural expiration
				pc_setpos(sd,(unsigned short)sce->val3,(sce->val4&0xFFFF),sce->val4>>16,CLR_TELEPORT);
			break;
		case SC_CHANGE:
			if (tid == INVALID_TIMER)
				break;
			//"Lose almost all their HP and SP" on natural expiration
			status_set_hp(bl,10,0);
			status_set_sp(bl,10,0);
			break;
		case SC_AUTOTRADE:
			if (tid == INVALID_TIMER)
				break;
			if (sd) {
				if (sd->state.vending)
					vending_closevending(sd);
				else if (sd->state.buyingstore)
					buyingstore_close(sd);
				map_quit(sd);
				//Because map_quit calls status_change_end with tid INVALID_TIMER
				//From here it's not neccesary to continue
				return 1;
			}
			break;
		case SC_STOP:
			if (sce->val2) {
				struct block_list *tbl = map_id2bl(sce->val2);

				sce->val2 = 0;
				if (tbl && (sc = status_get_sc(tbl)) && sc->data[type] && sc->data[type]->val2 == bl->id)
					status_change_end(tbl,type,INVALID_TIMER);
			}
			break;
		case SC_CONCENTRATION:
		case SC_PAIN_KILLER:
			if (sc->data[SC_ENDURE] && !sc->data[SC_ENDURE]->val4)
				status_change_end(bl,SC_ENDURE,INVALID_TIMER);
			break;
		case SC_TENSIONRELAX:
			if (sc->data[SC_WEIGHT50] || sc->data[SC_WEIGHT90])
				status_get_regen_data(bl)->state.overweight = 1; //Add the overweight flag back
			break;
		case SC_HALLUCINATIONWALK:
			sc_start(bl,bl,SC_HALLUCINATIONWALK_POSTDELAY,100,sce->val1,skill_get_time2(status_sc2skill(type),sce->val1));
			break;
		case SC_PYREXIA:
			status_change_end(bl,SC_BLIND,INVALID_TIMER);
			break;
		case SC_WHITEIMPRISON: {
				struct block_list *src = map_id2bl(sce->val2);
				int damage = 400 * sce->val1;

				if (tid == INVALID_TIMER || !src)
					break; //Terminated by damage
				status_fix_damage(src,bl,damage,clif_damage(bl,bl,gettick(),0,0,damage,1,DMG_NORMAL,0,false));
			}
			break;
		case SC_WUGDASH: {
				struct unit_data *ud = unit_bl2ud(bl);

				if (ud) {
					ud->state.running = 0;
					if (ud->walktimer != INVALID_TIMER)
						unit_stop_walking(bl,USW_FIXPOS);
				}
			}
			break;
		case SC_NEUTRALBARRIER_MASTER:
		case SC_STEALTHFIELD_MASTER:
			if (sce->val2) {
				struct skill_unit_group *group = skill_id2group(sce->val2);

				sce->val2 = 0;
				if (group)
					skill_delunitgroup(group);
			}
			break;
		case SC__SHADOWFORM: {
				struct map_session_data *s_sd = map_id2sd(sce->val2);

				if (s_sd)
					s_sd->shadowform_id = 0;
				status_change_end(bl,SC_ENDURE,INVALID_TIMER);
			}
			break;
		case SC_CURSEDCIRCLE_ATKER:
			if (sce->val2) //Used area size because there is a chance the caster could knock back and can't clear the target
				map_foreachinallrange(status_change_timer_sub,bl,AREA_SIZE + 3,BL_CHAR,bl,sce,SC_CURSEDCIRCLE_TARGET,gettick());
			break;
		case SC_CURSEDCIRCLE_TARGET: {
				struct block_list *src = map_id2bl(sce->val2);
				struct status_change *ssc = status_get_sc(src);

				if (ssc && ssc->data[SC_CURSEDCIRCLE_ATKER] && --(ssc->data[SC_CURSEDCIRCLE_ATKER]->val2) == 0)
					status_change_end(src,SC_CURSEDCIRCLE_ATKER,INVALID_TIMER);
			}
			break;
		case SC_RAISINGDRAGON:
			if (sd && !pc_isdead(sd)) {
				int i;

				if ((i = (sd->spiritball - 5)) > 0)
					pc_delspiritball(sd,i,0);
				status_change_end(bl,SC_EXPLOSIONSPIRITS,INVALID_TIMER);
			}
			break;
		case SC_NETHERWORLD:
			sc_start(bl,bl,SC_NETHERWORLD_POSTDELAY,100,sce->val1,2000);
			break;
		case SC_SATURDAYNIGHTFEVER:
			if (status->hp > 100 && sce->val2)
				status_set_hp(bl,100,0);
			clif_status_load(bl,SI_BERSERK,0);
			sc_start(bl,bl,SC_SITDOWN_FORCE,100,sce->val1,3000);
			sc_start4(bl,bl,SC_REGENERATION,100,10,0,0,(RGN_HP|RGN_SP),skill_get_time(status_sc2skill(type),sce->val1));
			break;
		case SC_THORNSTRAP:
			if (sce->val3) {
				struct skill_unit *unit = (struct skill_unit *)map_id2bl(sce->val3);

				if (unit && unit->alive)
					skill_delunit(unit);
			}
			break;
		case SC_BLOODSUCKER:
			if (sce->val2) {
				struct block_list *src = map_id2bl(sce->val2);

				if (src && !status_isdead(src)) {
					struct status_change *ssc = status_get_sc(src);

					if (ssc)
						ssc->bs_counter--;
				}
			}
			break;
		case SC_TEARGAS:
			status_change_end(bl,SC_TEARGAS_SOB,INVALID_TIMER);
			break;
		case SC_SITDOWN_FORCE:
		case SC_BANANA_BOMB_SITDOWN:
			if (sd && pc_issit(sd)) {
				pc_setstand(sd);
				skill_sit(sd,false);
			}
			break;
		case SC_INTRAVISION:
			calc_flag = SCB_ALL; //Required for overlapping
			break;
		case SC_OVERED_BOOST:
			switch (bl->type) {
				case BL_HOM: {
						struct homun_data *hd = BL_CAST(BL_HOM,bl);

						if (hd)
							hd->homunculus.hunger = max(1,hd->homunculus.hunger - 50);
					}
					break;
				case BL_PC:
					status_zap(bl,0,status_get_max_sp(bl) / 2);
					break;
			}
			break;
		case SC_UNLIMIT:
			status_change_end(bl,SC_DEFSET_NUM,INVALID_TIMER);
			status_change_end(bl,SC_MDEFSET_NUM,INVALID_TIMER);
			break;
		case SC_FULL_THROTTLE: {
				int sec = skill_get_time2(status_sc2skill(type),sce->val1);

				clif_status_change(bl,SI_DEC_AGI,1,sec,0,0,0);
				sc_start(bl,bl,SC_REBOUND,100,sce->val1,sec);
			}
			break;
		case SC_REBOUND:
			clif_status_load(bl,SI_DEC_AGI,0);
			break;
		case SC_MONSTER_TRANSFORM:
		case SC_ACTIVE_MONSTER_TRANSFORM:
			if (sce->val2 != SC_NONE)
				status_change_end(bl,(sc_type)sce->val2,INVALID_TIMER);
			else {
				if (sce->val1 == MOBID_DULLAHAN)
					clif_showscript(bl,"Apple's yummy!",SELF);
				if (sce->val1 == MOBID_LOLI_RURI)
					clif_showscript(bl,"I will be with you for ever!!",SELF);
			}
			break;
		case SC_C_MARKER: {
				struct map_session_data *tsd = NULL;
				uint8 i = 0;

				if (!(tsd = map_id2sd(sce->val2)))
					break;
				ARR_FIND(0,MAX_SKILL_CRIMSON_MARKER,i,tsd->c_marker[i] == bl->id);
				if (i < MAX_SKILL_CRIMSON_MARKER) { //Remove mark data from caster
					clif_crimson_marker(tsd,bl,1);
					tsd->c_marker[i] = 0;
				}
			}
			break;
		case SC_H_MINE: {
				struct map_session_data *tsd = NULL;
				struct item it;
				uint8 i = 0;

				if (!(tsd = map_id2sd(sce->val2)))
					break;
				ARR_FIND(0,MAX_SKILL_HOWLING_MINE,i,tsd->h_mine[i] == bl->id);
				if (i < MAX_SKILL_HOWLING_MINE)
					tsd->h_mine[i] = 0;
				if (sce->val3 || status_isdead(bl))
					break;
				if (!itemdb_exists(skill_get_itemid(RL_H_MINE,0)))
					break;
				//Drop the material from target if status expired
				memset(&it,0,sizeof(it));
				it.nameid = skill_get_itemid(RL_H_MINE,0);
				it.amount = max(skill_get_itemqty(RL_H_MINE,0),1);
				it.identify = 1;
				map_addflooritem(&it,it.amount,bl->m,bl->x,bl->y,tsd->status.char_id,0,0,4,0,false);
			}
			break;
		case SC_ALL_RIDING:
			if (sd && sd->state.autocast)
				sd->state.autocast = 0;
			break;
		case SC_MIDNIGHT_FRENZY_POSTDELAY: {
				TBL_HOM *hd = BL_CAST(BL_HOM,bl);

				if (hd->master)
					clif_homskillinfo(hd->master,MH_SONIC_CRAW,INF_ATTACK_SKILL);
			}
			break;
		case SC_SWORDCLAN:
		case SC_ARCWANDCLAN:
		case SC_GOLDENMACECLAN:
		case SC_CROSSBOWCLAN:
		case SC_JUMPINGCLAN:
			status_change_end(bl,SC_CLAN_INFO,INVALID_TIMER);
			break;
		case SC_EL_PASSIVE:
		case SC_EL_DEFENSIVE:
		case SC_EL_OFFENSIVE:
			sc_start(bl,bl,SC_EL_WAIT,100,1,-1);
			break;
	}

	opt_flag = 1;
	switch (type) {
		//OPT1
		case SC_STONE:
		case SC_FREEZE:
		case SC_STUN:
		case SC_SLEEP:
		case SC_BURNING:
		case SC_WHITEIMPRISON:
			sc->opt1 = 0;
			break;
		//OPT2
		case SC_POISON:
		case SC_CURSE:
		case SC_SILENCE:
		case SC_BLIND:
			sc->opt2 &= ~(1<<(type - SC_POISON));
			break;
		case SC_DPOISON:
			sc->opt2 &= ~OPT2_DPOISON;
			break;
		case SC_CONFUSION:
		case SC_SIGNUMCRUCIS:
			sc->opt2 &= ~OPT2_SIGNUMCRUCIS;
			break;
		case SC_FEAR:
			sc->opt2 &= ~OPT2_FEAR;
			break;
		//OPT3
		case SC_TWOHANDQUICKEN:
		case SC_ONEHAND:
		case SC_SPEARQUICKEN:
		case SC_CONCENTRATION:
		case SC_MERC_QUICKEN:
			sc->opt3 &= ~OPT3_QUICKEN;
			opt_flag = 0;
			break;
		case SC_OVERTHRUST:
		case SC_MAXOVERTHRUST:
		case SC_SWOO:
			sc->opt3 &= ~OPT3_OVERTHRUST;
			if (type == SC_SWOO)
				opt_flag = 8;
			else
				opt_flag = 0;
			break;
		case SC_ENERGYCOAT:
		case SC_SKE:
			sc->opt3 &= ~OPT3_ENERGYCOAT;
			opt_flag = 0;
			break;
		case SC_INCATKRATE:
			if (bl->type != BL_MOB) { //Simulated Explosion spirits effect
				opt_flag = 0;
				break;
			}
		//Fall through
		case SC_EXPLOSIONSPIRITS:
			sc->opt3 &= ~OPT3_EXPLOSIONSPIRITS;
			opt_flag = 0;
			break;
		case SC_STEELBODY:
		case SC_SKA:
			sc->opt3 &= ~OPT3_STEELBODY;
			if (type == SC_SKA)
				opt_flag = 8;
			else
				opt_flag = 0;
			break;
		case SC_BLADESTOP:
			sc->opt3 &= ~OPT3_BLADESTOP;
			opt_flag = 0;
			break;
		case SC_AURABLADE:
			sc->opt3 &= ~OPT3_AURABLADE;
			opt_flag = 0;
			break;
		case SC_BERSERK:
		case SC_SATURDAYNIGHTFEVER:
			sc->opt3 &= ~OPT3_BERSERK;
			opt_flag = 0;
			break;
		//case ???: //Doesn't seem to do anything
			//sc->opt3 &= ~OPT3_LIGHTBLADE;
			//opt_flag = 0;
			//break;
		case SC_DANCING:
			if ((sce->val1&0xFFFF) == CG_MOONLIT)
				sc->opt3 &= ~OPT3_MOONLIT;
			opt_flag = 0;
			break;
		case SC_MARIONETTE:
		case SC_MARIONETTE2:
			sc->opt3 &= ~OPT3_MARIONETTE;
			opt_flag = 0;
			break;
		case SC_ASSUMPTIO:
			sc->opt3 &= ~OPT3_ASSUMPTIO;
			opt_flag = 0;
			break;
		case SC_WARM: //SG skills [Komurka]
			sc->opt3 &= ~OPT3_WARM;
			opt_flag = 0;
			break;
		case SC_KAITE:
			sc->opt3 &= ~OPT3_KAITE;
			opt_flag = 0;
			break;
		case SC_BUNSINJYUTSU:
			sc->opt3 &= ~OPT3_BUNSIN;
			opt_flag = 0;
			break;
		case SC_SPIRIT:
			sc->opt3 &= ~OPT3_SOULLINK;
			opt_flag = 0;
			break;
		case SC_CHANGEUNDEAD:
			sc->opt3 &= ~OPT3_UNDEAD;
			opt_flag = 0;
			break;
		//case ???: //From DA_CONTRACT (looks like biolab mobs aura)
			//sc->opt3 &= ~OPT3_CONTRACT;
			//opt_flag = 0;
			//break;
		//OPTION
		case SC_HIDING:
			sc->option &= ~OPTION_HIDE;
			opt_flag |= 2|4; //Check for warp trigger + AOE trigger
			break;
		case SC_CLOAKING:
		case SC_CLOAKINGEXCEED:
		case SC__INVISIBILITY:
			sc->option &= ~OPTION_CLOAK;
		//Fall through
		case SC_CAMOUFLAGE:
		case SC_STEALTHFIELD:
		case SC__SHADOWFORM:
			opt_flag |= 2;
			break;
		case SC_CHASEWALK:
			sc->option &= ~(OPTION_CHASEWALK|OPTION_CLOAK);
			opt_flag |= 2;
			break;
		case SC__FEINTBOMB:
			sc->option &= ~OPTION_INVISIBLE;
			opt_flag |= 0x4;
			break;
		case SC_SIGHT:
			sc->option &= ~OPTION_SIGHT;
			break;
		case SC_WEDDING:
			sc->option &= ~OPTION_WEDDING;
			opt_flag |= 0x4;
			break;
		case SC_XMAS:
			sc->option &= ~OPTION_XMAS;
			opt_flag |= 0x4;
			break;
		case SC_SUMMER:
			sc->option &= ~OPTION_SUMMER;
			opt_flag |= 0x4;
			break;
		case SC_HANBOK:
			sc->option &= ~OPTION_HANBOK;
			opt_flag |= 0x4;
			break;
		case SC_OKTOBERFEST:
			sc->option &= ~OPTION_OKTOBERFEST;
			opt_flag |= 0x4;
			break;
		case SC_DRESSUP:
			sc->option &= ~OPTION_SUMMER2;
			opt_flag |= 0x4;
			break;
		case SC_ORCISH:
			sc->option &= ~OPTION_ORCISH;
			break;
		case SC_RUWACH:
			sc->option &= ~OPTION_RUWACH;
			break;
		case SC_FUSION:
			sc->option &= ~OPTION_FLYING;
			break;
		default:
			opt_flag = 0;
			break;
	}

	if (!battle_config.update_enemy_position && invisible && !(sc->option&(OPTION_HIDE|OPTION_CLOAK)))
		clif_blown(bl,bl);

	if (calc_flag&SCB_DYE) { //Restore DYE color
		if (vd && !vd->cloth_color && sce->val4)
			clif_changelook(bl,LOOK_CLOTHES_COLOR,sce->val4);
		calc_flag &= ~SCB_DYE;
	}

	//Might be needed in the future [Rytech]
	/*if (calc_flag&SCB_BODY) { //Restore body style
		if (vd && !vd->body_style && sce->val4)
			clif_changelook(bl,LOOK_BODY2,sce->val4);
		calc_flag &= ~SCB_BODY;
	}*/

	//On Aegis, when turning off a status change, first goes the sc packet, then the option packet
	clif_status_load(bl,icon,0);

	if (opt_flag&8) //bugreport:681
		clif_changeoption2(bl);
	else if (opt_flag) {
		clif_changeoption(bl);
		if (sd && (opt_flag&0x4)) {
			clif_changelook(bl,LOOK_BASE,sd->vd.class_);
			clif_get_weapon_view(sd,&sd->vd.weapon,&sd->vd.shield);
			clif_changelook(bl,LOOK_WEAPON,sd->vd.weapon);
			clif_changelook(bl,LOOK_SHIELD,sd->vd.shield);
			clif_changelook(bl,LOOK_CLOTHES_COLOR,cap_value(sd->status.clothes_color,0,battle_config.max_cloth_color));
			clif_changelook(bl,LOOK_BODY2,cap_value(sd->status.body,0,battle_config.max_body_style));
		}
	}

	if (calc_flag)
		status_calc_bl(bl,calc_flag);

	if (opt_flag&4) //Out of hiding, invoke on place
		skill_unit_move(bl,gettick(),1);

	if ((opt_flag&2) && sd && !sd->state.warping && map_getcell(bl->m,bl->x,bl->y,CELL_CHKNPC))
		npc_touch_areanpc(sd,bl->m,bl->x,bl->y); //Trigger on-touch event

	ers_free(sc_data_ers,sce);
	return 1;
}

/*==========================================
 * For recusive status, like for each 5s we drop sp etc.
 * Reseting the end timer.
 *------------------------------------------*/
TIMER_FUNC(status_change_timer)
{
	enum sc_type type = (sc_type)data;
	struct block_list *bl = NULL;
	struct map_session_data *sd = NULL;
	struct status_data *status = NULL;
	struct status_change *sc = NULL;
	struct status_change_entry *sce = NULL;
	int interval = status_get_sc_interval(type);
	bool dounlock = false;

	if( !(bl = map_id2bl(id)) ) {
		ShowDebug("status_change_timer: Null pointer id: %d data: %d\n",id,data);
		return 0;
	}

	if( !(sc = status_get_sc(bl)) || !(sce = sc->data[type]) ) {
		ShowDebug("status_change_timer: Null pointer id: %d data: %d bl-type: %d\n",id,data,bl->type);
		return 0;
	}

	if( sce->timer != tid ) {
		ShowError("status_change_timer: Mismatch for type %d: %d != %d (bl id %d)\n",type,tid,sce->timer,bl->id);
		return 0;
	}

	sce->timer = INVALID_TIMER;
	sd = BL_CAST(BL_PC,bl);
	status = status_get_status_data(bl);

//Set the next timer of the sce (don't assume the status still exists)
#define sc_timer_next(t,f,i,d) \
	if( (sce = sc->data[type]) ) \
		sce->timer = add_timer(t,f,i,d); \
	else \
		ShowError("status_change_timer: Unexpected NULL status change id: %d data: %d\n",id,data)

	switch( type ) {
		case SC_MAXIMIZEPOWER:
		case SC_CLOAKING:
			if( !status_charge(bl,0,1) )
				break; //Not enough SP to continue
			sc_timer_next(sce->val2 + tick,status_change_timer,bl->id,data);
			return 0;

		case SC_CHASEWALK:
			if( !status_charge(bl,0,sce->val4) )
				break;
			if( !sc->data[SC_CHASEWALK2] ) {
				sc_start(bl,bl,SC_CHASEWALK2,100,1<<(sce->val1 - 1),
				(sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_ROGUE ? 10 : 1) * //SL bonus -> x10 duration
				skill_get_time2(status_sc2skill(type),sce->val1));
			}
			sc_timer_next(sce->val2 + tick,status_change_timer,bl->id,data);
			return 0;

		case SC_HIDING:
			if( --(sce->val2) >= 0 ) {
				if( !(sce->val2%sce->val4) && !status_charge(bl,0,1) )
					break; //Fail if it's time to substract SP and there isn't
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_SIGHT:
		case SC_RUWACH:
		case SC_SIGHTBLASTER:
			if( type == SC_SIGHTBLASTER ) {
				if( sce->val4%2 ) //Restore trap immunity
					sce->val4--;
				map_foreachinallrange(status_change_timer_sub,bl,sce->val3,BL_CHAR|BL_SKILL,bl,sce,type,tick);
			} else {
				map_foreachinallrange(status_change_timer_sub,bl,sce->val3,BL_CHAR,bl,sce,type,tick);
				skill_reveal_trap_inarea(bl,sce->val3,bl->x,bl->y);
			}
			if( --(sce->val2) >= 0 ) {
				sce->val4 += 20; //Use for Shadow Form 2 seconds checking
				sc_timer_next(20 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_PROVOKE:
			if( sce->val2 ) { //Auto-provoke (it is ended in status_heal)
				sc_timer_next(60000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_STONE:
			if( sc->opt1 == OPT1_STONEWAIT && sce->val4 ) {
				sce->val3 = 0; //Incubation time used up
				unit_stop_attack(bl);
				if( sc->data[SC_DANCING] ) {
					unit_stop_walking(bl,USW_FIXPOS);
					status_change_end(bl,SC_DANCING,INVALID_TIMER);
				}
				status_change_end(bl,SC_AETERNA,INVALID_TIMER);
				sc->opt1 = OPT1_STONE;
				clif_changeoption(bl);
				sc_timer_next(min(sce->val4,interval) + tick,status_change_timer,bl->id,data);
				sce->val4 -= interval; //Remaining time
				status_calc_bl(bl,StatusChangeFlagTable[type]);
				return 0;
			}
			if( sce->val4 >= 0 && !sce->val3 && status->hp > status->max_hp / 4 )
				status_percent_damage(NULL,bl,1,0,false);
			break;

		case SC_POISON:
		case SC_DPOISON:
			if( sce->val4 >= 0 && !sc->data[SC_SLOWPOISON] ) {
				int64 damage = 0;

				if( sd )
					damage = (type == SC_DPOISON) ? 2 + status->max_hp / 50 : 2 + 3 * status->max_hp / 200;
				else
					damage = (type == SC_DPOISON) ? 2 + status->max_hp / 100 : 2 + status->max_hp / 200;
				if( status->hp > max(status->max_hp / 4,damage) ) //Stop damaging after 25% HP left
					status_zap(bl,damage,0);
			}
			break;

		case SC_BLEEDING:
			if( sce->val4 >= 0 ) {
				int64 damage = rnd()%600 + 200;

				if( !sd && damage >= status->hp ) { //No deadly damage for monsters
#ifdef RENEWAL //Leave 50 HP (fixed) for mob [exneval]
					damage = status->hp - 50;
#else
					damage = status->hp - 1;
#endif
				}
				map_freeblock_lock();
				dounlock = true;
				status_zap(bl,damage,0);
			}
			break;

		case SC_BURNING:
			if( sce->val4 >= 0 ) {
				int64 damage = 1000 + 3 * status->max_hp / 100; //Deals fixed (1000 + 3% MaxHP)

				map_freeblock_lock();
				dounlock = true;
				status_fix_damage(bl,bl,damage,clif_damage(bl,bl,tick,0,1,damage,1,DMG_NORMAL,0,false));
				unit_skillcastcancel(bl,2);
			}
			break;

		case SC_TOXIN:
			if( sce->val4 >= 0 ) { //Damage is every 10 seconds including 3% MaxSP drain
				map_freeblock_lock();
				dounlock = true;
				status_damage(bl,bl,1,3 * status->max_sp / 100,clif_damage(bl,bl,tick,status->amotion,status->dmotion + 500,1,1,DMG_NORMAL,0,false),0);
				unit_skillcastcancel(bl,0);
			}
			break;

		case SC_MAGICMUSHROOM:
			if( sce->val4 >= 0 ) {
				int64 damage = 3 * status->max_hp / 100;

				if( damage >= status->hp )
					damage = status->hp - 1; //Cannot kill
				if( damage > 0 ) { //3% damage each 4 seconds
					map_freeblock_lock();
					status_zap(bl,damage,0);
					map_freeblock_unlock();
				}
				if( sc->data[type] ) { //Random skill cast
					map_freeblock_lock();
					dounlock = true;
					if( sd && !pc_issit(sd) ) { // Can't cast if sit
						int mushroom_skill_id = 0, checked = 0, checked_max = MAX_SKILL_MAGICMUSHROOM_DB * 3;

						unit_stop_attack(bl);
						unit_skillcastcancel(bl,0);
						do {
							int i = rnd()%MAX_SKILL_MAGICMUSHROOM_DB;

							mushroom_skill_id = skill_magicmushroom_db[i].skill_id;
						} while( checked++ < checked_max && !mushroom_skill_id );
						if( !skill_get_index(mushroom_skill_id) )
							break;
						switch( skill_get_casttype(mushroom_skill_id) ) { //Magic Mushroom skills are buffs or area damage
							case CAST_GROUND:
								skill_castend_pos2(bl,bl->x,bl->y,mushroom_skill_id,1,tick,0);
								break;
							case CAST_NODAMAGE:
								skill_castend_nodamage_id(bl,bl,mushroom_skill_id,1,tick,0);
								break;
							case CAST_DAMAGE:
								skill_castend_damage_id(bl,bl,mushroom_skill_id,1,tick,0);
								break;
						}
					}
					clif_emotion(bl,E_HEH);
				}
			}
			break;

		case SC_PYREXIA:
			if( sce->val4 >= 0 ) {
				map_freeblock_lock();
				dounlock = true;
				status_fix_damage(bl,bl,100,clif_damage(bl,bl,tick,status->amotion,status->dmotion + 500,100,1,DMG_NORMAL,0,false));
				unit_skillcastcancel(bl,2);
			}
			break;

		case SC_LEECHESEND:
			if( sce->val4 >= 0 ) {
				//{Target's VIT x (New Poison Research Skill Level - 3)} + (Target's HP / 100)
				int64 damage = status->vit * (sce->val1 - 3) + (int)status->max_hp / 100;

				map_freeblock_lock();
				dounlock = true;
				status_fix_damage(bl,bl,damage,clif_damage(bl,bl,tick,status->amotion,status->dmotion + 500,damage,1,DMG_NORMAL,0,false));
				unit_skillcastcancel(bl,2);
			}
			break;

		case SC_TENSIONRELAX:
			if( --(sce->val3) >= 0 && status->max_hp > status->hp ) { //Decrease at 10 secs intervals
				sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_KNOWLEDGE:
			if( !sd )
				break;
			if( bl->m == sd->feel_map[0].m ||
				bl->m == sd->feel_map[1].m ||
				bl->m == sd->feel_map[2].m )
			{ //Timeout will be handled by pc_setpos
				sce->timer = INVALID_TIMER;
				return 0;
			}
			break;

		case SC_S_LIFEPOTION:
		case SC_L_LIFEPOTION:
			if( --(sce->val4) >= 0 ) { //val1 < 0 = per max% | val1 > 0 = exact amount
				int hp = 0;

				if( status->hp < status->max_hp )
					hp = (sce->val1 < 0) ? (int)(status->max_hp * -1 * sce->val1 / 100.) : sce->val1 ;
				status_heal(bl,hp,0,2);
				sc_timer_next(sce->val2 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_BOSSMAPINFO:
			if( --(sce->val4) >= 0 && sd ) {
				struct mob_data *boss_md = map_id2boss(sce->val1);

				if( boss_md ) {
					if( sd->bl.m != boss_md->bl.m ) //Not on same map anymore
						return 0;
					else if( boss_md->bl.prev ) { //Boss is alive - Update X, Y on minimap
						sce->val2 = 0;
						clif_bossmapinfo(sd,boss_md,BOSS_INFO_ALIVE);
					} else if( boss_md->spawn_timer != INVALID_TIMER && !sce->val2 ) { //Boss is dead
						sce->val2 = 1;
						clif_bossmapinfo(sd,boss_md,BOSS_INFO_DEAD);
					}
				}
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_DANCING: { //SP consumption by time of dancing skills
				int s = 0;
				int sp = 1;

				if( --(sce->val3) <= 0 )
					break;
				switch( sce->val1&0xFFFF ) {
					case BD_RICHMANKIM:
					case BD_DRUMBATTLEFIELD:
					case BD_RINGNIBELUNGEN:
					case BD_SIEGFRIED:
					case BA_DISSONANCE:
					case BA_ASSASSINCROSS:
					case DC_UGLYDANCE:
						s = 3;
						break;
					case BD_LULLABY:
					case BD_ETERNALCHAOS:
					case BD_ROKISWEIL:
					case DC_FORTUNEKISS:
						s = 4;
						break;
					case CG_HERMODE:
					case BD_INTOABYSS:
					case BA_WHISTLE:
					case DC_HUMMING:
					case BA_POEMBRAGI:
					case DC_SERVICEFORYOU:
						s = 5;
						break;
					case BA_APPLEIDUN:
#ifdef RENEWAL
							s = 5;
#else
							s = 6;
#endif
						break;
					case CG_MOONLIT:
						sp = 4 * (sce->val1>>16); //Moonlit's SP cost is 4*skill_lv [Skotlex]
					//Fall through
					case DC_DONTFORGETME:
						s = 10;
						break;
				}
				if( s && !(sce->val3%s) ) {
					if( sc->data[SC_LONGING] )
						sp *= 3;
					if( !status_charge(bl,0,sp) )
						break;
				}
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_BERSERK:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,sce->val2,0) )
					break;
				sc_timer_next(sce->val3 + tick,status_change_timer,bl->id,data); //5% MaxHP drain every 15 seconds
				return 0;
			}
			break;

		case SC_NOCHAT:
			if( sd ) {
				sd->status.manner++;
				clif_changestatus(sd,SP_MANNER,sd->status.manner);
				clif_updatestatus(sd,SP_MANNER);
				if( sd->status.manner < 0 ) { //Every 60 seconds your manner goes up by 1 until it gets back to 0
					sc_timer_next(60000 + tick,status_change_timer,bl->id,data);
					return 0;
				}
			}
			break;

		case SC_MARIONETTE:
		case SC_MARIONETTE2:
			{
				struct block_list *pbl = map_id2bl(sce->val1);

				if( pbl && check_distance_bl(bl,pbl,7) ) {
					sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
					return 0;
				}
			}
			break;

		case SC_GOSPEL:
			if( --(sce->val2) >= 0 && sce->val4 == BCT_SELF ) {
				int hp, sp;

				hp = (sce->val1 > 5 ? 45 : 30);
				sp = (sce->val1 > 5 ? 35 : 20);
				if( !status_charge(bl,hp,sp) )
					break;
				sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_JAILED:
			if( --(sce->val1) >= 0 || sce->val1 == INT_MAX ) {
				sc_timer_next(60000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_BLIND:
			if( sc->data[SC_FOGWALL] ) { //Blind lasts forever while you are standing on the fog
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_ABUNDANCE:
			if( --(sce->val4) >= 0 ) {
				status_heal(bl,0,60,0);
				sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_OBLIVIONCURSE:
			if( --(sce->val4) >= 0 ) {
				clif_emotion(bl,E_WHAT);
				sc_timer_next(3000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_WEAPONBLOCKING:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,3) )
					break;
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_CLOAKINGEXCEED:
			if( !status_charge(bl,0,10 - sce->val1) )
				break;
			sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
			return 0;

		case SC_EPICLESIS:
			++sce->val3;
			if( !(sce->val3%3) ) { //Recovers players every 3 seconds
				int hp, sp;

				switch( sce->val1 ) {
					case 1: case 2: hp = 3; sp = 2; break;
					case 3: case 4: hp = 4; sp = 3; break;
					case 5: default: hp = 5; sp = 4; break;
				}
				hp = status->max_hp * hp / 100;
				sp = status->max_sp * sp / 100;
				status_heal(bl,hp,sp,2);
			}
			if( !(sce->val3%5) ) { //Un-hides players every 5 seconds
				status_change_end(bl,SC_HIDING,INVALID_TIMER);
				status_change_end(bl,SC_CLOAKING,INVALID_TIMER);
				status_change_end(bl,SC_CAMOUFLAGE,INVALID_TIMER);
				status_change_end(bl,SC_CLOAKINGEXCEED,INVALID_TIMER);
				if( sc->data[SC__SHADOWFORM] && rnd()%100 < 100 - sc->data[SC__SHADOWFORM]->val1 * 10 )
					status_change_end(bl,SC__SHADOWFORM,INVALID_TIMER);
			}
			sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
			return 0;

		case SC_RENOVATIO:
			if( --(sce->val4) >= 0 ) {
				int heal = status->max_hp * 5 / 100;

				if( sc->data[SC_AKAITSUKI] && heal )
					skill_akaitsuki_damage(bl,bl,heal,status_sc2skill(type),sce->val1,tick);
				else
					status_heal(bl,heal,0,3);
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_SPHERE_1:
		case SC_SPHERE_2:
		case SC_SPHERE_3:
		case SC_SPHERE_4:
		case SC_SPHERE_5:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,1) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_FREEZE_SP:
			if( !status_charge(bl,0,sce->val2) ) {
				int i;

				for( i = SC_SPELLBOOK1; i <= SC_MAXSPELLBOOK; i++ ) //Also remove stored spell as well
					status_change_end(bl,(sc_type)i,INVALID_TIMER);
				break;
			}
			sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
			return 0;

		case SC_ELECTRICSHOCKER:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,5 * sce->val1 * status->max_sp / 100) )
					status_zap(bl,0,status->sp);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_CAMOUFLAGE:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,7 - sce->val1) )
					break;
				sce->val3++; //Value from duration
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC__REPRODUCE:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,9 - (1 + sce->val1) / 2) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC__SHADOWFORM:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,11 - sce->val1) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC__INVISIBILITY:
			if( !status_charge(bl,0,status->max_sp * (12 - 2 * sce->val1) / 100) )
				break;
			sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
			return 0;

		case SC_CLOUD_KILL:
			if( --(sce->val4) >= 0 ) {
				struct block_list *src = map_id2bl(sce->val2), *unit_bl = map_id2bl(sce->val3);

				if( !src || status_isdead(src) || src->m != bl->m )
					break;
				map_freeblock_lock();
				if( unit_bl )
					skill_attack(BF_MAGIC,src,unit_bl,bl,status_sc2skill(type),sce->val1,tick,0);
				if( sc->data[type] ) {
					sc_timer_next(500 + tick,status_change_timer,bl->id,data);
				}
				map_freeblock_unlock();
				return 0;
			}
			break;

		case SC_STRIKING:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,sce->val3) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_WARMER:
			if( --(sce->val4) >= 0 ) {
				struct status_change *ssc = status_get_sc(map_id2bl(sce->val2));
				int heal = status->max_hp * sce->val1 / 100;

				if( ssc && ssc->data[SC_HEATER_OPTION] )
					heal *= 3;
				if( sc->data[SC_AKAITSUKI] && heal )
					skill_akaitsuki_damage(bl,bl,heal,status_sc2skill(type),sce->val1,tick);
				else
					status_heal(bl,heal,0,1);
				sc_timer_next(3000 + tick,status_change_timer,bl->id,data);
			}
			return 0;

		case SC_VACUUM_EXTREME:
			if( sce->val4 ) {
				int x = sce->val3>>16;
				int y = sce->val3&0xFFFF;
				int range = skill_get_unit_range(status_sc2skill(type),sce->val1);

				if( distance_xy(x,y,bl->x,bl->y) <= range && unit_movepos(bl,x,y,0,false) ) {
					clif_slide(bl,x,y);
					clif_fixpos(bl);
				}
				sc_timer_next(sce->val4 + tick,status_change_timer,bl->id,data);
				sce->val4 = 0;
			}
			break;

		case SC_FIRE_INSIGNIA:
			if( --(sce->val4) >= 0 ) {
				if( status->def_ele == ELE_FIRE )
					status_heal(bl,status->max_hp / 100,0,1);
				else if( status->def_ele == ELE_EARTH )
					status_zap(bl,status->max_hp / 100,0);
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_WATER_INSIGNIA:
			if( --(sce->val4) >= 0 ) {
				if( status->def_ele == ELE_WATER )
					status_heal(bl,status->max_hp / 100,0,1);
				else if( status->def_ele == ELE_FIRE )
					status_zap(bl,status->max_hp / 100,0);
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_WIND_INSIGNIA:
			if( --(sce->val4) >= 0 ) {
				if( status->def_ele == ELE_WIND )
					status_heal(bl,status->max_hp / 100,0,1);
				else if( status->def_ele == ELE_WATER )
					status_zap(bl,status->max_hp / 100,0);
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_EARTH_INSIGNIA:
			if( --(sce->val4) >= 0 ) {
				if( status->def_ele == ELE_EARTH )
					status_heal(bl,status->max_hp / 100,0,1);
				else if( status->def_ele == ELE_WIND )
					status_zap(bl,status->max_hp / 100,0);
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_THORNSTRAP:
			if( --(sce->val4) >= 0 ) {
				struct block_list *src = map_id2bl(sce->val2), *unit_bl = map_id2bl(sce->val3);

				if( !src || status_isdead(src) || src->m != bl->m )
					break;
				map_freeblock_lock();
				if( unit_bl )
					skill_attack(BF_MISC,src,unit_bl,bl,status_sc2skill(type),sce->val1,tick,0);
				if( sc->data[type] ) {
					sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				}
				map_freeblock_unlock();
				return 0;
			}
			break;

		case SC_BLOODSUCKER:
			if( --(sce->val4) >= 0 ) {
				struct block_list *src = map_id2bl(sce->val2);

				if( !src || status_isdead(src) || src->m != bl->m || distance_bl(src,bl) >= 12 )
					break;
				map_freeblock_lock();
				skill_attack(BF_MISC,src,src,bl,status_sc2skill(type),sce->val1,tick,SD_LEVEL|SD_ANIMATION);
				if( sc->data[type] ) {
					sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				}
				map_freeblock_unlock();
				return 0;
			}
			break;

		case SC_DEMONIC_FIRE:
			if( --(sce->val4) >= 0 ) {
				struct block_list *src = map_id2bl(sce->val2), *unit_bl = map_id2bl(sce->val3);

				if( !src || status_isdead(src) || src->m != bl->m )
					break;
				map_freeblock_lock();
				if( unit_bl )
					skill_attack(BF_MAGIC,src,unit_bl,bl,status_sc2skill(type),sce->val1,tick,0);
				if( sc->data[type] ) {
					sc_timer_next(2000 + tick,status_change_timer,bl->id,data);
				}
				map_freeblock_unlock();
				return 0;
			}
			break;

		case SC_TEARGAS:
			if( --(sce->val4) >= 0 ) {
				struct block_list *src = map_id2bl(sce->val3);
				int damage = status->max_hp * 5 / 100; //Drain 5% HP

				if( damage >= status->hp )
					damage = status->hp - 1;
				map_freeblock_lock();
				status_fix_damage(src,bl,damage,1);
				if( sc->data[type] ) {
					sc_timer_next(2000 + tick,status_change_timer,bl->id,data);
				}
				map_freeblock_unlock();
				return 0;
			}
			break;

		case SC_TEARGAS_SOB:
			if( --(sce->val4) >= 0 ) {
				clif_emotion(bl,E_SOB); //Cry emotion
				sc_timer_next(3000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_WINKCHARM:
		case SC_VOICEOFSIREN:
			if( --(sce->val4) >= 0 ) {
				clif_emotion(bl,E_LV);
				sc_timer_next(2000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_DEEPSLEEP:
			if( --(sce->val4) >= 0 ) { //Recovers 3% of the player's MaxHP/MaxSP every 2 seconds
				status_heal(bl,status->max_hp * 3 / 100,status->max_sp * 3 / 100,0);
				sc_timer_next(2000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_SIRCLEOFNATURE:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,sce->val3) )
					break;
				status_heal(bl,sce->val2,0,1);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_SONGOFMANA:
			if( --(sce->val4) >= 0 ) {
				status_heal(bl,0,sce->val3,3);
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_SATURDAYNIGHTFEVER:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,sce->val2,status->max_sp / 100) )
					break;
				sc_timer_next(sce->val3 + tick,status_change_timer,bl->id,data); //1% MaxHP/MaxSP drain every val3 seconds [Jobbie]
				return 0;
			}
			break;

		case SC_MELODYOFSINK:
			if( --(sce->val4) >= 0 ) {
				status_charge(bl,0,status->max_sp * (2 * sce->val1 + min(2 * sce->val2,20)) / 100);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_CRYSTALIZE:
			if( --(sce->val4) >= 0 ) { //2% HP lost per second
				map_freeblock_lock();
				status_zap(bl,status->max_hp * 2 / 100,0);
				map_freeblock_unlock();
				if( sc->data[type] ) {
					if( (bl->type&BL_CONSUME) && !status_charge(bl,0,status->max_sp / 100) )
						break; //1% SP lost per second, status ends if less then 1% remains
					sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				}
				return 0;
			}
			break;

		case SC_FORCEOFVANGUARD:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,24 - 4 * sce->val1) )
					break;
				sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_BANDING:
			if( --(sce->val3) >= 0 ) {
				if( !status_charge(bl,0,7 - sce->val1) )
					break;
				sce->val2 = (sd ? skill_banding_count(sd) : 1); //Recheck around you to update the banding count
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_REFLECTDAMAGE:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,10) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_OVERHEAT: {
				int damage = status->max_hp / 100; //1% each second

				if( damage >= status->hp )
					damage = status->hp - 1; //Do not kill, just keep you with 1 HP minimum
				map_freeblock_lock();
				status_zap(bl,damage,0);
				if( sc->data[type] ) {
					sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				}
				map_freeblock_unlock();
				return 0;
			}
			break;

		case SC_OVERHEAT_LIMITPOINT:
			if( --(sce->val1) >= 0 ) {
				int16 limit[] = { 150,200,280,360,450 };
				uint16 lv = (sd ? pc_checkskill(sd,NC_MAINFRAME) : 0);

				if( sc && sc->data[SC_OVERHEAT] )
					status_change_end(bl,SC_OVERHEAT,INVALID_TIMER);
				if( sce->val1 > limit[lv] )
					sc_start(bl,bl,SC_OVERHEAT,100,sce->val1,1000);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_MAGNETICFIELD:
			if( --(sce->val4) >= 0 ) {
				struct block_list *src = map_id2bl(sce->val2);

				if( !src || status_isdead(src) || src->m != bl->m )
					break;
				map_freeblock_lock();
				if( !status_charge(bl,0,50) )
					status_zap(bl,0,status->sp);
				if( sc->data[type] ) {
					sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				}
				map_freeblock_unlock();
				return 0;
			}
			break;

		case SC_STEALTHFIELD_MASTER:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,status->max_sp * 3 / 100) )
					break;
				sc_timer_next(sce->val3 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_INSPIRATION:
			if( --(sce->val4) >= 0 ) {
				int hp = status->max_hp * (7 / 2 - (1 / 2 * sce->val1)) / 100;
				int sp = status->max_sp * (9 / 2 - (1 / 2 * sce->val1)) / 100;

				if( status->sp < sp )
					break;
				status_zap(bl,hp,sp);
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_LIGHTNINGWALK:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,sce->val3) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_RAISINGDRAGON:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,status->max_hp / 100,0) ) //Officially tested its 1% HP drain [Jobbie]
					break;
				sc_timer_next(5000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_TROPIC:
		case SC_CHILLY_AIR:
		case SC_WILD_STORM:
		case SC_UPHEAVAL:
		case SC_HEATER:
		case SC_COOLER:
		case SC_BLAST:
		case SC_CURSED_SOIL:
		case SC_PYROTECHNIC:
		case SC_AQUAPLAY:
		case SC_GUST:
		case SC_PETROLOGY:
		case SC_CIRCLE_OF_FIRE:
		case SC_WATER_SCREEN:
		case SC_WIND_STEP:
		case SC_SOLID_SKIN:
		case SC_FIRE_CLOAK:
		case SC_WATER_DROP:
		case SC_WIND_CURTAIN:
		case SC_STONE_SHIELD:
			if( status_charge(bl,0,sce->val2) && (sce->val4 == -1 || (sce->val4 -= sce->val3) >= 0) ) {
				sc_timer_next(sce->val3 + tick,status_change_timer,bl->id,data);
				return 0;
			} else if( bl->type == BL_ELEM )
				sc_start(bl,bl,SC_EL_WAIT,100,EL_MODE_PASSIVE,-1);
			break;

		case SC_WATER_SCREEN_OPTION:
			status_heal(bl,1000,0,2);
			sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
			return 0;

		case SC_STOMACHACHE:
			if( --(sce->val4) >= 0 ) {
				status_charge(bl,0,sce->val2); //Reduce 8 SP every 10 seconds
				if( sd && !pc_issit(sd) ) { //Force to sit every 10 seconds
					pc_setsit(sd);
					skill_sit(sd,true);
					clif_sitting(bl);
				}
				sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_LEADERSHIP:
		case SC_GLORYWOUNDS:
		case SC_SOULCOLD:
		case SC_HAWKEYES:
			//They only end by status_change_end
			sc_timer_next(600000 + tick,status_change_timer,bl->id,data);
			return 0;

		case SC_MEIKYOUSISUI:
			if( --(sce->val4) >= 0 ) {
				status_percent_heal(bl,2 * sce->val1,sce->val1);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_KAGEMUSYA:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,1) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_ANGRIFFS_MODUS:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,100,20) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_CBC:
			if( --(sce->val4) >= 0 ) { //val1 is used to count the number of seconds that passed
				sce->val1++;
				if( !(sce->val1%2) ) { //Lose HP every 2 seconds
					if( sce->val3 ) { //SPdamage value higher then 0 signals target is not a monster
						if( !status_charge(bl,sce->val2,0) )
							break;
					} else if( !status_damage(NULL,bl,sce->val2,0,0,3) )
						break; //If it's a monster then use this to remove HP since status_charge won't work
				}
				if( !(sce->val1%3) ) //Lose SP every 3 seconds
					status_charge(bl,0,sce->val3);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_FULL_THROTTLE:
			if( --(sce->val4) >= 0 ) {
				if( !status_charge(bl,0,status->max_sp * sce->val2 / 100) )
					break;
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_REBOUND:
			if( --(sce->val4) >= 0 ) {
				clif_emotion(bl,E_SWT);
				sc_timer_next(2000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_KINGS_GRACE:
			if( --(sce->val4) >= 0 ) {
				status_percent_heal(bl,sce->val2,0);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_FRIGG_SONG:
			if( --(sce->val4) >= 0 ) {
				status_heal(bl,sce->val3,0,2);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_C_MARKER:
			if( --(sce->val4) >= 0 ) {
				struct map_session_data *tsd = map_id2sd(sce->val2);

				if( !tsd || tsd->bl.m != bl->m ) //End status if caster isn't in same map
					break;
				clif_crimson_marker(tsd,bl,0); //Update target position
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_SV_ROOTTWIST:
			if( sce->val4 >= 0 ) {
				struct block_list *src = map_id2bl(sce->val2);

				if( !src || status_isdead(src) || src->m != bl->m || distance_bl(src,bl) >= 32 )
					break;
				map_freeblock_lock();
				dounlock = true;
				skill_attack(BF_MISC,src,src,bl,SU_SV_ROOTTWIST_ATK,sce->val1,tick,SD_LEVEL|SD_ANIMATION);
			}
			break;

		case SC_BITESCAR:
			if( --(sce->val4) >= 0 ) {
				status_percent_damage(NULL,bl,-sce->val2,0,0);
				sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_FRESHSHRIMP:
			if( --(sce->val4) >= 0 ) {
				status_heal(bl,sce->val2,0,3);
				sc_timer_next(sce->val3 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_DORAM_BUF_01:
			if( --(sce->val4) >= 0 ) {
				if( status->hp < status->max_hp )
					status_heal(bl,10,0,2);
				sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_DORAM_BUF_02:
			if( --(sce->val4) >= 0 ) {
				if( status->sp < status->max_sp )
					status_heal(bl,0,5,2);
				sc_timer_next(10000 + tick,status_change_timer,bl->id,data);
				return 0;
			}
			break;

		case SC_BURNT:
			if( --(sce->val4) >= 0 ) {
				int damage = 2000;

				if( damage >= status->hp )
					damage = status->hp - 1;
				map_freeblock_lock();
				status_zap(bl,damage,0);
				if( sc->data[type] ) {
					sc_timer_next(1000 + tick,status_change_timer,bl->id,data);
				}
				map_freeblock_unlock();
				return 0;
			}
			break;
	}

	//If status has an interval and there is at least 100ms remaining time, wait for next interval
	if( interval > 0 && sc->data[type] && sce->val4 >= 100 ) {
		sc_timer_next(min(sce->val4,interval) + tick,status_change_timer,bl->id,data);
		sce->val4 -= interval;
		if( dounlock )
			map_freeblock_unlock();
		return 0;
	}

	if( dounlock )
		map_freeblock_unlock();

	//Default for all non-handled control paths is to end the status
	return status_change_end(bl,type,tid);
#undef sc_timer_next
}

/*==========================================
 * Foreach iteration of repetitive status
 *------------------------------------------*/
int status_change_timer_sub(struct block_list *bl, va_list ap) {
	struct status_change *tsc;
	struct block_list *src = va_arg(ap, struct block_list *);
	struct status_change_entry *sce = va_arg(ap, struct status_change_entry *);
	enum sc_type type = (sc_type)va_arg(ap, int); //gcc: enum args get promoted to int
	unsigned int tick = va_arg(ap, unsigned int);

	if( status_isdead(bl) )
		return 0;

	tsc = status_get_sc(bl);

	switch( type ) {
		case SC_SIGHT: //Un-hides targets on 7*7 range
			if( tsc && tsc->data[SC__SHADOWFORM] && (sce && sce->val4 > 0 && !(sce->val4%2000)) && //For every 2 seconds do the checking
				rnd()%100 < 100 - tsc->data[SC__SHADOWFORM]->val1 * 10 ) //[100 - (Skill Level x 10)] %
				status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
		//Fall through
		case SC_CONCENTRATE:
			status_change_end(bl, SC_HIDING, INVALID_TIMER);
			status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
			status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
			status_change_end(bl, SC_CAMOUFLAGE, INVALID_TIMER);
			if( type == SC_CONCENTRATE && tsc && tsc->data[SC__SHADOWFORM] && rnd()%100 < 100 - tsc->data[SC__SHADOWFORM]->val1 * 10 )
				status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
			break;
		case SC_RUWACH: //Un-hides targets on 5*5 range and deals little damages
			if( tsc ) {
				if( tsc->data[SC_HIDING] || tsc->data[SC_CLOAKING] || tsc->data[SC_CLOAKINGEXCEED] || tsc->data[SC_CAMOUFLAGE] ) {
					status_change_end(bl, SC_HIDING, INVALID_TIMER);
					status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
					status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
					status_change_end(bl, SC_CAMOUFLAGE, INVALID_TIMER);
					if( battle_check_target(src, bl, BCT_ENEMY) > 0 )
						skill_attack(BF_MAGIC, src, src, bl, status_sc2skill(type), 1, tick, 0);
				}
				if( tsc->data[SC__SHADOWFORM] && (sce && sce->val4 > 0 && !(sce->val4%2000)) &&
					rnd()%100 < 100 - tsc->data[SC__SHADOWFORM]->val1 * 10 )
					status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
			}
			break;
		case SC_SIGHTBLASTER:
			if( battle_check_target(src, bl, BCT_ENEMY) > 0 && status_check_skilluse(src, bl, WZ_SIGHTBLASTER, 2) ) {
				if( sce ) {
					struct skill_unit *su = NULL;

					if( bl->type == BL_SKILL )
						su = (struct skill_unit *)bl;
					if( skill_attack(BF_MAGIC, src, src, bl, status_sc2skill(type), sce->val1, tick, SD_LEVEL) &&
						(!su || !su->group || !(skill_get_inf2(su->group->skill_id)&INF2_TRAP)) ) //The hit is not counted if it's against a trap
						sce->val2 = 0; //This signals it to end
					else if( (bl->type&BL_SKILL) && !(sce->val4%2) )
						sce->val4++;
				}
			}
			break;
		case SC_CLOSECONFINE:
			//Locked char has released the hold on everyone
			if( tsc && tsc->data[SC_CLOSECONFINE2] && tsc->data[SC_CLOSECONFINE2]->val2 == src->id ) {
				tsc->data[SC_CLOSECONFINE2]->val2 = 0;
				status_change_end(bl, SC_CLOSECONFINE2, INVALID_TIMER);
			}
			break;
		case SC_CURSEDCIRCLE_TARGET:
			if( tsc && tsc->data[SC_CURSEDCIRCLE_TARGET] && tsc->data[SC_CURSEDCIRCLE_TARGET]->val2 == src->id )
				status_change_end(bl, type, INVALID_TIMER);
			break;
	}
	return 0;
}

/**
 * Clears buffs/debuffs or bonus_script on an object
 * @param bl: Object to clear [PC|MOB|HOM|MER|ELEM]
 * @param type: Type to remove
 *  SCCB_BUFFS: Clear Buffs
 *  SCCB_DEBUFFS: Clear Debuffs
 *  SCCB_CHEM_PROTECT: Clear AM_CP_ARMOR/HELM/SHIELD/WEAPON
 *  SCCB_REFRESH: Clear specific debuffs through RK_REFRESH
 *  SCCB_LUXANIMA: Bonus Script removed through RK_LUXANIMA
 * @param val1: Additional value
 */
void status_change_clear_buffs(struct block_list *bl, uint8 type, uint16 val1)
{
	int i;
	struct status_change *sc = status_get_sc(bl);

	if( !sc || !sc->count )
		return;

	map_freeblock_lock();

	if( type&(SCCB_DEBUFFS|SCCB_REFRESH) ) { //Debuffs and spesific debuffs with a RK_REFRESH
		for( i = SC_COMMON_MIN; i <= SC_COMMON_MAX; i++ )
			status_change_end(bl, (sc_type)i, INVALID_TIMER);
	}

	for( i = SC_COMMON_MAX + 1; i < SC_MAX; i++ ) {
		if( !sc->data[i] )
			continue;
		if( status_get_sc_type(i)&(SC_NO_REM_DEATH|SC_NO_CLEAR) )
			continue; //Stuff that cannot be removed
		switch( i ) {
			case SC_CP_WEAPON:
			case SC_CP_SHIELD:
			case SC_CP_ARMOR:
			case SC_CP_HELM:
				if( !(type&SCCB_CHEM_PROTECT) )
					continue; //Chemical Protection is only removed by some skills
				break;
			case SC_FEAR:
			case SC_BURNING:
			case SC_FREEZING:
			case SC_WHITEIMPRISON:
			case SC_MARSHOFABYSS:
			case SC_TOXIN:
			case SC_PARALYSE:
			case SC_VENOMBLEED:
			case SC_MAGICMUSHROOM:
			case SC_DEATHHURT:
			case SC_PYREXIA:
			case SC_OBLIVIONCURSE:
			case SC_LEECHESEND:
			case SC_DEEPSLEEP:
			case SC_CRYSTALIZE:
			case SC_MANDRAGORA:
				if( !(type&SCCB_REFRESH) )
					continue; //RK_REFRESH specific debuffs
				break;
			default:
				//Debuffs that can be removed
				if( (type&SCCB_DEBUFFS) && !(status_get_sc_type(i)&SC_DEBUFF) )
					continue;
				//The rest are buffs that can be removed
				if( !(type&(SCCB_BUFFS)) )
					continue;
				if( i == SC_BERSERK || i == SC_SATURDAYNIGHTFEVER )
					sc->data[i]->val2 = 0;
				break;
		}
		status_change_end(bl, (sc_type)i, INVALID_TIMER);
	}

	if( bl->type == BL_PC ) { //Removes bonus_script
		i = 0;
		if( type&SCCB_BUFFS )
			i |= BSF_REM_BUFF;
		if( type&SCCB_DEBUFFS )
			i |= BSF_REM_DEBUFF;
		if( type&SCCB_REFRESH )
			i |= BSF_REM_ON_REFRESH;
		if( type&SCCB_LUXANIMA )
			i |= BSF_REM_ON_LUXANIMA;
		pc_bonus_script_clear(BL_CAST(BL_PC, bl), i);
	}

	//Cleaning all extras vars
	sc->pos_x = 0;
	sc->pos_y = 0;
#ifndef RENEWAL
	sc->sg_counter = 0;
#endif
	sc->bs_counter = 0;

	map_freeblock_unlock();
}
/**
 * Infect a user with status effects (SC_DEADLYINFECT)
 * @param src: Object initiating change on bl [PC|MOB|HOM|MER|ELEM]
 * @param bl: Object to change
 * @param type: false - Shadow Chaser attacking, true - Shadow Chaser being attacked
 * @return 1: Success 0: Fail
 */
int status_change_spread(struct block_list *src, struct block_list *bl, bool type) {
	int i, flag = 0;
	struct status_change *sc = status_get_sc(src);
	const struct TimerData *timer = NULL;
	unsigned int tick;
	struct status_change_data data;

	if( !sc || !sc->count )
		return 0;

	tick = gettick();

	//Status Immunity resistance
	if( status_bl_has_mode(src,MD_STATUS_IMMUNE) || status_bl_has_mode(bl,MD_STATUS_IMMUNE) )
		return 0;

	for( i = SC_COMMON_MIN; i < SC_MAX; i++ ) {
		if( !sc->data[i] || i == SC_COMMON_MAX )
			continue;
		if( sc->data[i]->timer != INVALID_TIMER ) {
			timer = get_timer(sc->data[i]->timer);
			if( !timer || timer->func != status_change_timer || DIFF_TICK(timer->tick,tick) < 0 )
				continue;
		}
		switch( i ) {
			//Buffs that can be spreaded through Deadly Infect
			//NOTE: We'll add/delete SCs when we are able to confirm it
			//First we list the common status's that can be spreaded
			case SC_DEATHHURT:
			case SC_PARALYSE:
				if( type )
					continue;
			//Fall through
			//case SC_STUN:
			case SC_CURSE:
			case SC_SILENCE:
			case SC_CONFUSION:
			case SC_BLIND:
			//case SC_NOCHAT:
			case SC_HALLUCINATION:
			case SC_SIGNUMCRUCIS:
			case SC_DECREASEAGI:
			//case SC_SLOWDOWN:
			//case SC_MINDBREAKER:
			//case SC_WINKCHARM:
			//case SC_STOP:
			case SC_ORCISH:
			//case SC_STRIPWEAPON: //Omg I got infected and had the urge to strip myself physically
			//case SC_STRIPSHIELD: //No this is stupid and shouldnt be spreadable at all
			//case SC_STRIPARMOR: //Disabled until I can confirm if it does or not [Rytech]
			//case SC_STRIPHELM:
			//case SC__STRIPACCESSORY:
			//case SC_BITE:
			//Additional Commons
			case SC_FEAR:
			case SC_FREEZING:
			case SC_VENOMBLEED:
				if( sc->data[i]->timer != INVALID_TIMER )
					data.tick = DIFF_TICK(timer->tick,tick);
				else
					data.tick = INVALID_TIMER;
				break;
			//Special cases
			case SC_TOXIN:
			case SC_MAGICMUSHROOM:
			case SC_PYREXIA:
			case SC_LEECHESEND:
				if( type )
					continue;
			//Fall through
			case SC_POISON:
			case SC_DPOISON:
			case SC_BLEEDING:
			case SC_BURNING:
				if( sc->data[i]->timer != INVALID_TIMER )
					data.tick = DIFF_TICK(timer->tick,tick) + sc->data[i]->val4;
				else
					data.tick = INVALID_TIMER;
				break;
			default:
				continue;
		}
		if( i ) {
			data.val1 = sc->data[i]->val1;
			data.val2 = sc->data[i]->val2;
			data.val3 = sc->data[i]->val3;
			data.val4 = sc->data[i]->val4;
			status_change_start(src,bl,(sc_type)i,10000,data.val1,data.val2,data.val3,data.val4,data.tick,SCFLAG_NOAVOID|SCFLAG_FIXEDTICK|SCFLAG_FIXEDRATE);
			flag = 1;
		}
	}

	return flag;
}

//Natural regen related stuff
static unsigned int natural_heal_prev_tick,natural_heal_diff_tick;
static int status_natural_heal(struct block_list *bl, va_list args)
{
	struct regen_data *regen;
	struct status_data *status;
	struct status_change *sc;
	struct unit_data *ud;
	struct view_data *vd = NULL;
	struct regen_data_sub *sregen;
	struct map_session_data *sd;
	int rate, multi = 1, flag;

	regen = status_get_regen_data(bl);
	if (!regen)
		return 0;
	status = status_get_status_data(bl);
	sc = status_get_sc(bl);
	if (sc && !sc->count)
		sc = NULL;
	sd = BL_CAST(BL_PC,bl);
	flag = regen->flag;
	if (flag&RGN_HP && (status->hp >= status->max_hp || regen->state.block&1))
		flag &= ~(RGN_HP|RGN_SHP);
	if (flag&RGN_SP && (status->sp >= status->max_sp || regen->state.block&2))
		flag &= ~(RGN_SP|RGN_SSP);
	if (flag && (status_isdead(bl) || (sd && pc_ishiding(sd))))
		flag = RGN_NONE;
	if (sd) {
		if (sd->hp_loss.value || sd->sp_loss.value)
			pc_bleeding(sd, natural_heal_diff_tick);
		if (sd->hp_regen.value || sd->sp_regen.value)
			pc_regen(sd, natural_heal_diff_tick);
	}
	//Apply sitting regen bonus
	if (flag&(RGN_SHP|RGN_SSP) && regen->ssregen && (vd = status_get_viewdata(bl)) && vd->dead_sit == 2) {
		sregen = regen->ssregen;
		if (flag&RGN_SHP) { //Sitting HP regen
			rate = (int)(natural_heal_diff_tick * sregen->rate.hp / 100.);
			if (regen->state.overweight)
				rate /= 2; //Half as fast when overweight
			sregen->tick.hp += rate;
			while (sregen->tick.hp >= (unsigned int)battle_config.natural_heal_skill_interval) {
				sregen->tick.hp -= battle_config.natural_heal_skill_interval;
				if (status_heal(bl, sregen->hp, 0, 3) < sregen->hp) { //Full
					flag &= ~(RGN_HP|RGN_SHP);
					break;
				}
			}
		}
		if (flag&RGN_SSP) { //Sitting SP regen
			rate = (int)(natural_heal_diff_tick * sregen->rate.sp / 100.);
			if (regen->state.overweight)
				rate /= 2;
			sregen->tick.sp += rate;
			while (sregen->tick.sp >= (unsigned int)battle_config.natural_heal_skill_interval) {
				sregen->tick.sp -= battle_config.natural_heal_skill_interval;
				if (status_heal(bl, 0, sregen->sp, 3) < sregen->sp) {
					flag &= ~(RGN_SP|RGN_SSP);
					break;
				}
			}
		}
	}
	if (flag && regen->state.overweight)
		flag = RGN_NONE;
	ud = unit_bl2ud(bl);
	if (flag&(RGN_HP|RGN_SHP|RGN_SSP) && ud && ud->walktimer != INVALID_TIMER) {
		flag &= ~(RGN_SHP|RGN_SSP);
		if (!regen->state.walk)
			flag &= ~RGN_HP;
	}
	if (!flag)
		return 0;
	if (flag&(RGN_HP|RGN_SP)) {
		if (!vd)
			vd = status_get_viewdata(bl);
		if (vd && vd->dead_sit == 2)
			multi += 1; //This causes the interval to be halved
		if (regen->state.gc)
			multi += 1;
	}
	//Natural HP regen
	if (flag&RGN_HP) {
		rate = (int)(natural_heal_diff_tick * (regen->rate.hp / 100. * multi));
		if (ud && ud->walktimer != INVALID_TIMER && !(sc && sc->data[SC_GT_REVITALIZE]))
			rate /= 2;
		//Homun HP regen fix (they should regen as if they were sitting (twice as fast)
		if (bl->type == BL_HOM)
			rate *= 2;
		regen->tick.hp += rate;
		if (regen->tick.hp >= (unsigned int)battle_config.natural_healhp_interval) {
			int val = 0;

			do {
				val += regen->hp;
				regen->tick.hp -= battle_config.natural_healhp_interval;
			} while (regen->tick.hp >= (unsigned int)battle_config.natural_healhp_interval);
			if (status_heal(bl, val, 0, 1) < val)
				flag &= ~RGN_SHP; //Full
		}
	}
	//Natural SP regen
	if (flag&RGN_SP) {
		rate = (int)(natural_heal_diff_tick * (regen->rate.sp / 100. * multi));
		//Homun SP regen fix (they should regen as if they were sitting (twice as fast)
		if (bl->type == BL_HOM)
			rate *= 2;
#ifdef RENEWAL
		if (bl->type == BL_PC && (((TBL_PC *)bl)->class_&MAPID_UPPERMASK) == MAPID_MONK &&
			sc && sc->data[SC_EXPLOSIONSPIRITS] && (!sc->data[SC_SPIRIT] || sc->data[SC_SPIRIT]->val2 != SL_MONK))
			rate /= 2; //Tick doubled in Fury state
#endif
		regen->tick.sp += rate;
		if (regen->tick.sp >= (unsigned int)battle_config.natural_healsp_interval) {
			int val = 0;

			do {
				val += regen->sp;
				regen->tick.sp -= battle_config.natural_healsp_interval;
			} while (regen->tick.sp >= (unsigned int)battle_config.natural_healsp_interval);
			if (status_heal(bl, 0, val, 1) < val)
				flag &= ~RGN_SSP; //Full
		}
	}
	if (!regen->sregen)
		return flag;
	//Skill regen
	sregen = regen->sregen;
	if (flag&RGN_SHP) { //Skill HP regen
		sregen->tick.hp += (int)(natural_heal_diff_tick * sregen->rate.hp / 100.);
		while (sregen->tick.hp >= (unsigned int)battle_config.natural_heal_skill_interval) {
			sregen->tick.hp -= battle_config.natural_heal_skill_interval;
			if (status_heal(bl, sregen->hp, 0, 3) < sregen->hp)
				break; //Full
		}
	}
	if (flag&RGN_SSP) { //Skill SP regen
		sregen->tick.sp += (int)(natural_heal_diff_tick * sregen->rate.sp / 100.);
		while (sregen->tick.sp >= (unsigned int)battle_config.natural_heal_skill_interval) {
			int val = sregen->sp;

			if (sd && sd->state.doridori) {
				val *= 2;
				sd->state.doridori = 0;
				if ((rate = pc_checkskill(sd,TK_SPTIME)))
					sc_start(bl, bl, status_skill2sc(TK_SPTIME), 100, rate, skill_get_time(TK_SPTIME, rate));
				if ((sd->class_&MAPID_UPPERMASK) == MAPID_STAR_GLADIATOR &&
					rnd()%10000 < battle_config.sg_angel_skill_ratio) { //Angel of the Sun/Moon/Star
					clif_feel_hate_reset(sd);
					pc_resethate(sd);
					pc_resetfeel(sd);
				}
			}
			sregen->tick.sp -= battle_config.natural_heal_skill_interval;
			if (status_heal(bl, 0, val, 3) < val)
				break; //Full
		}
	}
	return flag;
}

//Natural heal main timer
static TIMER_FUNC(status_natural_heal_timer)
{
	natural_heal_diff_tick = DIFF_TICK(tick,natural_heal_prev_tick);
	map_foreachregen(status_natural_heal);
	natural_heal_prev_tick = tick;
	return 0;
}

/**
 * Get the chance to upgrade a piece of equipment.
 * @param wlv The weapon type of the item to refine (see see enum e_refine_type)
 * @param refine The target refine level
 * @param is_enriched Check if the item to refine is using enriched chance
 * @return The chance to refine the item, in percent (0~100)
 */
int status_get_refine_chance(enum e_refine_type wlv, int refine, bool is_enriched)
{
	enum e_refine_chance_type type;

	if (refine < 0 || refine >= MAX_REFINE)
		return 0;

	if (battle_config.event_refine_chance)
		type = (is_enriched ? REFINE_CHANCE_TYPE_E_ENRICHED : REFINE_CHANCE_TYPE_E_NORMAL);
	else
		type = (is_enriched ? REFINE_CHANCE_TYPE_ENRICHED : REFINE_CHANCE_TYPE_NORMAL);

	return refine_info[wlv].chance[type][refine];
}

/**
 * Get Blacksmith Blessing requirement for refining
 * @param bs Pointer to store the value
 * @param wlv Armor or weapon level (see enum e_refine_type)
 * @param refine Current refine level
 * @return True if has valid value, false otherwise.
 */
bool status_get_refine_blacksmithBlessing(struct s_refine_bs_blessing *bs, enum e_refine_type wlv, int refine)
{
	if (refine < 0 || refine >= MAX_REFINE)
		return false;

 	if (wlv < REFINE_TYPE_ARMOR || wlv >= REFINE_TYPE_MAX)
		return false;

 	memcpy(bs, &refine_info[wlv].bs_blessing[refine], sizeof(struct s_refine_bs_blessing));
	return true;
}

/**
 * Returns refine cost (zeny or item) for a weapon level.
 * @param wlv The weapon type of the item to refine (see enum e_refine_type)
 * @param type Refine cost type (see enum e_refine_cost_type)
 * @param info Refine cost info (see enum e_refine_info)
 * @return Refine cost for refining item
 */
int status_get_refine_cost(enum e_refine_type wlv, int type, enum e_refine_info info) {
	switch (info) {
		case REFINE_INFO_MATERIAL_ID:
			return refine_info[wlv].cost[type].nameid;
		case REFINE_INFO_ZENY:
			return refine_info[wlv].cost[type].zeny;
		case REFINE_INFO_DOWN_REFINE_CHANCE:
			return refine_info[wlv].cost[type].downrefine_chance;
		case REFINE_INFO_DOWN_REFINE_NUM:
			return refine_info[wlv].cost[type].downrefine_num;
	}
	return 0;
}

struct s_refine_cost *status_refine_cost(enum e_refine_type wlv, int type) {
	if (wlv < REFINE_TYPE_ARMOR || wlv >= REFINE_TYPE_MAX)
		return NULL;
	if (type < REFINE_COST_TYPE_NORMAL || type >= REFINE_COST_TYPE_MAX)
		return NULL;
	return &refine_info[wlv].cost[type];
}

/**
 * Check if status is disabled on a map
 * @param type: Status Change data
 * @param mapIsVS: If the map is a map_flag_vs type
 * @param mapisPVP: If the map is a PvP type
 * @param mapIsGVG: If the map is a map_flag_gvg type
 * @param mapIsBG: If the map is a Battleground type
 * @param mapZone: Map Zone type
 * @param mapIsTE: If the map us WOE TE
 * @return True - SC disabled on map; False - SC not disabled on map/Invalid SC
 */
static bool status_change_isDisabledOnMap_(sc_type type, bool mapIsVS, bool mapIsPVP, bool mapIsGVG, bool mapIsBG, unsigned int mapZone, bool mapIsTE)
{
	if (type <= SC_NONE || type >= SC_MAX)
		return false;
	if ((!mapIsVS && SCDisabled[type]&1) ||
		(mapIsPVP && SCDisabled[type]&2) ||
		(mapIsGVG && SCDisabled[type]&4) ||
		(mapIsBG && SCDisabled[type]&8) ||
		(mapIsTE && SCDisabled[type]&16) ||
		(SCDisabled[type]&(mapZone)))
		return true;

	return false;
}

/**
 * Clear a status if it is disabled on a map
 * @param bl: Block list data
 * @param sc: Status Change data
 */
void status_change_clear_onChangeMap(struct block_list *bl, struct status_change *sc)
{
	nullpo_retv(bl);

	if (sc && sc->count) {
		unsigned short i;
		bool mapIsVS = map_flag_vs2(bl->m);
		bool mapIsPVP = map[bl->m].flag.pvp;
		bool mapIsGVG = map_flag_gvg2_no_te(bl->m);
		bool mapIsBG = map[bl->m].flag.battleground;
		bool mapIsTE = map_flag_gvg2_te(bl->m);
		unsigned int mapZone = map[bl->m].zone<<3;

		for (i = 0; i < SC_MAX; i++) {
			if (!sc->data[i] || !SCDisabled[i])
				continue;
			if (status_change_isDisabledOnMap_((sc_type)i, mapIsVS, mapIsPVP, mapIsGVG, mapIsBG, mapZone, mapIsTE))
				status_change_end(bl, (sc_type)i, INVALID_TIMER);
		}
	}
}

/**
 * Read status_disabled.txt file
 * @param str: Fields passed from sv_readdb
 * @param columns: Columns passed from sv_readdb function call
 * @param current: Current row being read into SCDisabled array
 * @return True - Successfully stored, False - Invalid SC
 */
static bool status_readdb_status_disabled(char **str, int columns, int current)
{
	int type = SC_NONE;

	if (ISDIGIT(str[0][0]))
		type = atoi(str[0]);
	else {
		if (!script_get_constant(str[0], &type))
			type = SC_NONE;
	}
	if (type <= SC_NONE || type >= SC_MAX) {
		ShowError("status_readdb_status_disabled: Invalid SC with type %s.\n", str[0]);
		return false;
	}
	SCDisabled[type] = (unsigned int)atol(str[1]);
	return true;
}

static bool status_readdb_sizefix(char *fields[], int columns, int current)
{
	unsigned int i;

	for (i = 0; i < MAX_WEAPON_TYPE; i++)
		atkmods[current][i] = atoi(fields[i]);
	return true;
}

/**
 * Processes a refine_db.conf entry.
 *
 * @param *r	  Libconfig setting entry. It is expected to be valid and it
 *                won't be freed (it is care of the caller to do so if
 *                necessary)
 * @param n       Ordinal number of the entry, to be displayed in case of
 *                validation errors.
 * @param *source Source of the entry (file name), to be displayed in case of
 *                validation errors.
 * @return # of the validated entry, or 0 in case of failure.
 */
static int status_readdb_refine_libconfig_sub(struct config_setting_t *r, const char *name, const char *source)
{
	struct config_setting_t *rate = NULL, *cost = NULL;
	int type = REFINE_TYPE_ARMOR, bonus_per_level = 0, rnd_bonus_v = 0, rnd_bonus_lv = 0, refine_v = 0;
	char lv[4];

	nullpo_ret(r);
	nullpo_ret(name);
	nullpo_ret(source);

	if (!strncmp(name, "Armors", 6)) {
		type = REFINE_TYPE_ARMOR;
	} else if (strncmp(name, "WeaponLevel", 11) || !strspn(&name[strlen(name) - 1], "0123456789") || (type = atoi(strncpy(lv, name + 11, 2))) == REFINE_TYPE_ARMOR) {
		ShowError("status_readdb_refine_libconfig_sub: Invalid key name for entry '%s' in \"%s\".\n", name, source);
		return 0;
	}
	if (type < REFINE_TYPE_ARMOR || type >= REFINE_TYPE_MAX) {
		ShowError("status_readdb_refine_libconfig_sub: Out of range refine type for entry '%s' in \"%s\".\n", name, source);
		return 0;
	}
	if (!config_setting_lookup_int(r, "StatsPerLevel", &bonus_per_level)) {
		ShowWarning("status_readdb_refine_libconfig_sub: Missing StatsPerLevel for entry '%s' in \"%s\".\n", name, source);
		return 0;
	}
	if (!config_setting_lookup_int(r, "RandomBonusStartLevel", &rnd_bonus_lv)) {
		ShowWarning("status_readdb_refine_libconfig_sub: Missing RandomBonusStartLevel for entry '%s' in \"%s\".\n", name, source);
		return 0;
	}
	if (!config_setting_lookup_int(r, "RandomBonusValue", &rnd_bonus_v)) {
		ShowWarning("status_readdb_refine_libconfig_sub: Missing RandomBonusValue for entry '%s' in \"%s\".\n", name, source);
		return 0;
	}
	if ((cost = config_setting_get_member(r, "Costs")) && config_setting_is_group(cost)) {
		struct config_setting_t *t = NULL;
		bool duplicate[REFINE_COST_TYPE_MAX];
		int i = 0;

		memset(&refine_info[type].cost, 0, sizeof(struct s_refine_cost) * REFINE_COST_TYPE_MAX);
		memset(&duplicate, 0, sizeof(duplicate));

		while ((t = config_setting_get_elem(cost, i++)) && config_setting_is_group(t)) {
			int idx = REFINE_COST_TYPE_NORMAL, material = 0, price = 0, downrefine_chance = 0, downrefine_num = 0, i32;
			char *refine_cost_const = config_setting_name(t);
			struct item_data *id = NULL;

			if (!script_get_constant(refine_cost_const, &idx)) {
				ShowError("status_readdb_refine_libconfig_sub: Unknown refine cost type '%s' for entry %s in \"%s\".\n", refine_cost_const, name, source);
				return 0;
			}
			if (idx < REFINE_COST_TYPE_NORMAL || idx >= REFINE_COST_TYPE_MAX) {
				ShowError("status_readdb_refine_libconfig_sub: Out of range refine cost type '%s' for entry %s in \"%s\".\n", refine_cost_const, name, source);
				return 0;
			}
			if (duplicate[idx])
				ShowWarning("status_readdb_refine_libconfig_sub: Duplicate refine cost type '%s' for entry %s in \"%s\", overwriting previous entry...\n", refine_cost_const, name, source);
			else
				duplicate[idx] = true;
			if (!config_setting_lookup_int(t, "Material", &i32)) {
				ShowWarning("status_readdb_refine_libconfig_sub: Missing Material for entry '%s' in \"%s\".\n", name, source);
				return 0;
			}
			material = i32;
			if (!(id = itemdb_exists(material))) {
				ShowWarning("status_readdb_refine_libconfig_sub: Required Material %d is not found for entry '%s' in \"%s\".\n", material, name, source);
				return 0;
			}
			if (!config_setting_lookup_int(t, "Price", &i32)) {
				ShowWarning("status_readdb_refine_libconfig_sub: Missing Price for entry '%s' in \"%s\".\n", name, source);
				return 0;
			}
			if (i32 <= 0) {
				ShowWarning("status_readdb_refine_libconfig_sub: Invalid Price for entry '%s' in \"%s\".\n", name, source);
				return 0;
			}
			price = i32;
			if (config_setting_lookup_int(t, "DownRefineChance", &i32))
				downrefine_chance = i32;
			if (config_setting_lookup_int(t, "DownRefineNum", &i32) && downrefine_chance)
				downrefine_num = i32;
			refine_info[type].cost[idx].nameid = material;
			refine_info[type].cost[idx].zeny = price;
			refine_info[type].cost[idx].downrefine_chance = downrefine_chance;
			refine_info[type].cost[idx].downrefine_num = downrefine_num;
		}
	}
	if ((rate = config_setting_get_member(r, "Rates")) && config_setting_is_group(rate)) {
		struct config_setting_t *t = NULL;
		bool duplicate[MAX_REFINE];
		int bonus[MAX_REFINE], rnd_bonus[MAX_REFINE], bsItemID[MAX_REFINE], bsCount[MAX_REFINE];
		int chance[REFINE_CHANCE_TYPE_MAX][MAX_REFINE];
		int i, j;

		memset(&refine_info[type].bs_blessing, 0, sizeof(struct s_refine_bs_blessing) * MAX_REFINE);
		memset(&duplicate, 0, sizeof(duplicate));
		memset(&bonus, 0, sizeof(bonus));
		memset(&rnd_bonus, 0, sizeof(rnd_bonus));

		for (i = 0; i < REFINE_CHANCE_TYPE_MAX; i++) {
			for (j = 0; j < MAX_REFINE; j++)
				chance[i][j] = 100; //Default value for all rates
		}
		i = 0;
		while ((t = config_setting_get_elem(rate, i++)) && config_setting_is_group(t)) {
			int level = 0, i32;
			char *rlvl = config_setting_name(t);

			memset(&lv, 0, sizeof(lv));

			if (!strspn(&rlvl[strlen(rlvl) - 1], "0123456789") || (level = atoi(strncpy(lv, rlvl + 2, 3))) <= 0) {
				ShowError("status_readdb_refine_libconfig_sub: Invalid refine level format '%s' for entry %s in \"%s\".\n", rlvl, name, source);
				return 0;
			}
			if (level <= 0 || level > MAX_REFINE) {
				ShowError("status_readdb_refine_libconfig_sub: Out of range refine level '%s' for entry %s in \"%s\".\n", rlvl, name, source);
				return 0;
			}
			level--;
			if (duplicate[level])
				ShowWarning("status_readdb_refine_libconfig_sub: Duplicate rate '%s' for entry %s in \"%s\", overwriting previous entry...\n", rlvl, name, source);
			else
				duplicate[level] = true;
			if (config_setting_lookup_int(t, "NormalChance", &i32))
				chance[REFINE_CHANCE_TYPE_NORMAL][level] = i32;
			if (config_setting_lookup_int(t, "EnrichedChance", &i32))
				chance[REFINE_CHANCE_TYPE_ENRICHED][level] = i32;
			if (config_setting_lookup_int(t, "EventNormalChance", &i32))
				chance[REFINE_CHANCE_TYPE_E_NORMAL][level] = i32;
			if (config_setting_lookup_int(t, "EventEnrichedChance", &i32))
				chance[REFINE_CHANCE_TYPE_E_ENRICHED][level] = i32;
			if (config_setting_lookup_int(t, "Bonus", &i32))
				bonus[level] = i32;
			else
				bonus[level] = 0;
			if (config_setting_lookup_int(t, "BSItemID", &i32))
				bsItemID[level] = i32;
			else
				bsItemID[level] = 0;
			if (config_setting_lookup_int(t, "BSCount", &i32))
				bsCount[level] = i32;
			else
				bsCount[level] = 0;
		}
		for (j = 0; j < MAX_REFINE; j++) {
			refine_info[type].chance[REFINE_CHANCE_TYPE_NORMAL][j] = chance[REFINE_CHANCE_TYPE_NORMAL][j];
			refine_info[type].chance[REFINE_CHANCE_TYPE_ENRICHED][j] = chance[REFINE_CHANCE_TYPE_ENRICHED][j];
			refine_info[type].chance[REFINE_CHANCE_TYPE_E_NORMAL][j] = chance[REFINE_CHANCE_TYPE_E_NORMAL][j];
			refine_info[type].chance[REFINE_CHANCE_TYPE_E_ENRICHED][j] = chance[REFINE_CHANCE_TYPE_E_ENRICHED][j];
			if (rnd_bonus_lv > 0 && j >= rnd_bonus_lv - 1) {
				rnd_bonus[j] = rnd_bonus_v * (j - rnd_bonus_lv + 2);
				refine_info[type].randombonus_max[j] = rnd_bonus[j];
			}
			refine_v += bonus_per_level + bonus[j];
			refine_info[type].bonus[j] = refine_v;
			refine_info[type].bs_blessing[j].nameid = bsItemID[j];
			refine_info[type].bs_blessing[j].count = bsCount[j];
		}
	} else {
		ShowWarning("status_readdb_refine_libconfig_sub: Missing refine rates for entry '%s' in \"%s\".\n", name, source);
		return 0;
	}
	return type + 1;
}

/**
 * Reads from a libconfig-formatted refine_db.conf file.
 *
 * @param *filename File name, relative to the database path.
 * @return The number of found entries.
 */
static void status_readdb_refine_libconfig(const char *filename)
{
	bool duplicate[REFINE_TYPE_MAX];
	struct config_t refine_db_conf;
	struct config_setting_t *r;
	char filepath[256];
	int i = 0, count = 0, type = 0;

	memset(&duplicate, 0, sizeof(duplicate));

	safesnprintf(filepath, sizeof(filepath), "%s/%s", db_path, filename);
	if (conf_read_file(&refine_db_conf, filepath))
		return;
	while ((r = config_setting_get_elem(refine_db_conf.root, i++))) {
		char *name = config_setting_name(r);

		if ((type = status_readdb_refine_libconfig_sub(r, name, filename))) {
			if (duplicate[type - 1])
				ShowWarning("status_readdb_refine_libconfig: duplicate entry for %s in \"%s\", overwriting previous entry...\n", name, filepath);
			else
				duplicate[type - 1] = true;
			count++;
		}
	}
	config_destroy(&refine_db_conf);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, filepath);
}

/**
 * Read status_config.txt file
 * @param fields: Fields passed from sv_readdb
 * @param columns: Columns passed from sv_readdb function call
 * @param current: Current row being read into sc_conf array
 * @return True - Successfully stored, False - Invalid SC
 */
static bool status_readdb_status_config(char **str, int columns, int current)
{
	int type = SC_NONE;

	if (ISDIGIT(str[0][0]))
		type = atoi(str[0]);
	else {
		if (!script_get_constant(str[0], &type))
			type = SC_NONE;
	}
	if (type <= SC_NONE || type >= SC_MAX) {
		ShowError("status_readdb_status_config: Invalid SC with type %s.\n", str[0]);
		return false;
	}
	StatusConfig[type] = (unsigned int)atol(str[1]);
	return true;
}

/*------------------------------------------
 * DB reading.
 * size_fix.txt		- size adjustment table for weapons
 * refine_db.conf	- refining data table
 *------------------------------------------*/
void status_readdb(void)
{
	int i, j;

	//Initialize databases to default
	memset(SCDisabled, 0, sizeof(SCDisabled));

	//size_fix.txt
	for (i = 0; i < ARRAYLENGTH(atkmods); i++) {
		for (j = 0; j < MAX_WEAPON_TYPE; j++)
			atkmods[i][j] = 100;
	}

	//Read databases
	//path,filename,separator,mincol,maxcol,maxrow,func_parsor
	sv_readdb(db_path, "status_disabled.txt", ',', 2, 2, -1, &status_readdb_status_disabled);
	sv_readdb(db_path, "status_config.txt", ',', 2, 2, -1, &status_readdb_status_config);
	sv_readdb(db_path, DBPATH"size_fix.txt", ',', MAX_WEAPON_TYPE, MAX_WEAPON_TYPE, ARRAYLENGTH(atkmods), &status_readdb_sizefix);
	status_readdb_refine_libconfig(DBPATH"refine_db.conf");
}

/*==========================================
 * Status db init and destroy.
 *------------------------------------------*/
void do_init_status(void)
{
	add_timer_func_list(status_change_timer,"status_change_timer");
	add_timer_func_list(status_natural_heal_timer,"status_natural_heal_timer");
	initChangeTables();
	initDummyData();
	status_readdb();
	natural_heal_prev_tick = gettick();
	sc_data_ers = ers_new(sizeof(struct status_change_entry),"status.c::sc_data_ers",ERS_OPT_NONE);
	add_timer_interval(natural_heal_prev_tick + NATURAL_HEAL_INTERVAL,status_natural_heal_timer,0,0,NATURAL_HEAL_INTERVAL);
}

void do_final_status(void)
{
	ers_destroy(sc_data_ers);
}
