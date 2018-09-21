// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/mmo.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/core.h"
#include "../common/showmsg.h"
#include "../common/malloc.h"
#include "../common/random.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/conf.h"

#include "map.h"
#include "atcommand.h"
#include "battle.h"
#include "chat.h"
#include "channel.h"
#include "clif.h"
#include "chrif.h"
#include "duel.h"
#include "instance.h"
#include "intif.h"
#include "itemdb.h"
#include "log.h"
#include "pc.h"
#include "pc_groups.h" // groupid2name
#include "status.h"
#include "skill.h"
#include "mob.h"
#include "npc.h"
#include "pet.h"
#include "homunculus.h"
#include "mail.h"
#include "mercenary.h"
#include "elemental.h"
#include "party.h"
#include "guild.h"
#include "script.h"
#include "storage.h"
#include "trade.h"
#include "unit.h"
#include "mapreg.h"
#include "quest.h"
#include "achievement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


#define ATCOMMAND_LENGTH 50
#define ACMD_FUNC(x) static int atcommand_ ## x (const int fd, struct map_session_data *sd, const char *command, const char *message)

typedef struct AtCommandInfo AtCommandInfo;
typedef struct AliasInfo AliasInfo;

int atcmd_binding_count = 0;

//Atcommand restriction usage
enum e_atcmd_restict {
	ATCMD_NOCONSOLE    = 0x1, //Cannot be used via console (is_atcommand type 2)
	ATCMD_NOSCRIPT     = 0x2, //Cannot be used via script command 'atcommand' or 'useatcmd' (is_atcommand type 0 and 3)
	ATCMD_NOAUTOTRADE  = 0x4, //Like ATCMD_NOSCRIPT, but if the player is autotrader. Example: atcommand "@kick "+strcharinfo(0);
};

struct AtCommandInfo {
	char command[ATCOMMAND_LENGTH];
	AtCommandFunc func;
	char *at_groups; //Quick @commands "can-use" lookup
	char *char_groups; //Quick @charcommands "can-use" lookup
	uint8 restriction; //Restrictions see enum e_restict
};

struct AliasInfo {
	AtCommandInfo *command;
	char alias[ATCOMMAND_LENGTH];
};


char atcommand_symbol = '@'; //First char of the commands
char charcommand_symbol = '#';

static DBMap *atcommand_db = NULL; //Name -> AtCommandInfo
static DBMap *atcommand_alias_db = NULL; //alias -> AtCommandInfo
static config_t atcommand_config;

static char atcmd_output[CHAT_SIZE_MAX];
static char atcmd_player_name[NAME_LENGTH];
const char *parent_cmd;

struct atcmd_binding_data **atcmd_binding;

static AtCommandInfo *get_atcommandinfo_byname(const char *name); //@help
static const char *atcommand_checkalias(const char *aliasname); //@help
static void atcommand_get_suggestions(struct map_session_data *sd, const char *name, bool atcommand); //@help
static void warp_get_suggestions(struct map_session_data *sd, const char *name); //@rura, @warp, @mapmove

//@commands (script-based)
struct atcmd_binding_data *get_atcommandbind_byname(const char *name) {
	int i = 0;
	
	if( *name == atcommand_symbol || *name == charcommand_symbol )
		name++; //For backwards compatibility

	ARR_FIND(0, atcmd_binding_count, i, strcmpi(atcmd_binding[i]->command, name) == 0);

	return (i < atcmd_binding_count) ? atcmd_binding[i] : NULL;
}

/**
 * Retrieves the help string associated with a given command.
 *
 * @param name the name of the command to retrieve help information for
 * @return the string associated with the command, or NULL
 */
static const char *atcommand_help_string(const char *command)
{
	const char *str = NULL;
	struct config_setting_t *info;

	if( *command == atcommand_symbol || *command == charcommand_symbol ) {
		// Remove the prefix symbol for the raw name of the command
		command++;
	}

	// Convert alias to the real command name
	command = atcommand_checkalias(command);

	// Attempt to find the first default help command
	info = config_lookup(&atcommand_config, "help");

	if( info == NULL ) // Failed to find the help property in the configuration file
		return NULL;

	if( !config_setting_lookup_string(info, command, &str) ) // Failed to find the matching help string
		return NULL;

	// Push the result from the method
	return str;
}

/*==========================================
 * @send (used for testing packet sends from the client)
 *------------------------------------------*/
ACMD_FUNC(send)
{
	int len = 0, type;
	long num;

	// Read message type as hex number (without the 0x)
	if(!message || !*message || !((sscanf(message, "len %x", &type) == 1 && (len = 1)) ||
		sscanf(message, "%x", &type) == 1) ) {
		int i;

		for(i = 900; i <= 903; ++i)
			clif_displaymessage(fd, msg_txt(i));
		return -1;
	}

#define PARSE_ERROR(error,p) \
	{\
		clif_displaymessage(fd, (error));\
		sprintf(atcmd_output, ">%s", (p));\
		clif_displaymessage(fd, atcmd_output);\
	}

#define CHECK_EOS(p) \
	if(*(p) == 0){\
		clif_displaymessage(fd, "Unexpected end of string");\
		return -1;\
	}

#define SKIP_VALUE(p) \
	{\
		while(*(p) && !ISSPACE(*(p))) ++(p); /* Non-space */\
		while(*(p) && ISSPACE(*(p)))  ++(p); /* Space */\
	}

#define GET_VALUE(p,num) \
	{\
		if(sscanf((p), "x%lx", &(num)) < 1 && sscanf((p), "%ld ", &(num)) < 1){\
			PARSE_ERROR("Invalid number in:",(p));\
			return -1;\
		}\
	}

	if(type > 0 && type < MAX_PACKET_DB) {
		int off, end;

		if(len) { // Show packet length
			sprintf(atcmd_output, msg_txt(904), type, packet_db[type].len); // Packet 0x%x length: %d
			clif_displaymessage(fd, atcmd_output);
			return 0;
		}

		len = packet_db[type].len;
		off = 2;
		if(len == 0) { // Unknown packet - ERROR
			sprintf(atcmd_output, msg_txt(905), type); // Unknown packet: 0x%x
			clif_displaymessage(fd, atcmd_output);
			return -1;
		} else if(len == -1) { // Dynamic packet
			len = SHRT_MAX - 4; // Maximum length
			off = 4;
		}
		WFIFOHEAD(sd->fd,len);
		WFIFOW(sd->fd,0) = TOW(type);

		// Parse packet contents
		SKIP_VALUE(message);
		while(*message != 0 && off < len) {
			if(ISDIGIT(*message) || *message == '-' || *message == '+') { // Default (byte)
				GET_VALUE(message,num);
				WFIFOB(sd->fd,off) = TOB(num);
				++off;
			} else if(TOUPPER(*message) == 'B') { // Byte
				++message;
				GET_VALUE(message,num);
				WFIFOB(sd->fd,off) = TOB(num);
				++off;
			} else if(TOUPPER(*message) == 'W') { // Word (2 bytes)
				++message;
				GET_VALUE(message,num);
				WFIFOW(sd->fd,off) = TOW(num);
				off += 2;
			} else if(TOUPPER(*message) == 'L') { // Long word (4 bytes)
				++message;
				GET_VALUE(message,num);
				WFIFOL(sd->fd,off) = TOL(num);
				off += 4;
			} else if(TOUPPER(*message) == 'S') { // String - escapes are valid
				// Get string length - num <= 0 means not fixed length (default)
				++message;
				if(*message == '"') {
					num = 0;
				} else {
					GET_VALUE(message,num);
					while(*message != '"') { // Find start of string
						if(*message == 0 || ISSPACE(*message)) {
							PARSE_ERROR(msg_txt(906),message); // Not a string:
							return -1;
						}
						++message;
					}
				}

				// Parse string
				++message;
				CHECK_EOS(message);
				end = (num <= 0 ? 0 : min(off + ((int)num),len));
				for(; *message != '"' && (off < end || end == 0); ++off) {
					if(*message == '\\') {
						++message;
						CHECK_EOS(message);
						switch(*message) {
							case 'a': num = 0x07; break; // Bell
							case 'b': num = 0x08; break; // Backspace
							case 't': num = 0x09; break; // Horizontal tab
							case 'n': num = 0x0A; break; // Line feed
							case 'v': num = 0x0B; break; // Vertical tab
							case 'f': num = 0x0C; break; // Form feed
							case 'r': num = 0x0D; break; // Carriage return
							case 'e': num = 0x1B; break; // Escape
							default:  num =* message; break;
							case 'x': // Hexadecimal
								{
									++message;
									CHECK_EOS(message);
									if(!ISXDIGIT(*message)) {
										PARSE_ERROR(msg_txt(907),message); // Not a hexadecimal digit:
										return -1;
									}
									num = (ISDIGIT(*message) ? *message - '0' : TOLOWER(*message) - 'a' + 10);
									if(ISXDIGIT(*message)) {
										++message;
										CHECK_EOS(message);
										num <<= 8;
										num += (ISDIGIT(*message) ? *message - '0' : TOLOWER(*message) - 'a' + 10);
									}
									WFIFOB(sd->fd,off) = TOB(num);
									++message;
									CHECK_EOS(message);
									continue;
								}
							case '0':
							case '1':
							case '2':
							case '3':
							case '4':
							case '5':
							case '6':
							case '7': // Octal
								{
									num = *message - '0'; // 1st octal digit
									++message;
									CHECK_EOS(message);
									if(ISDIGIT(*message) && *message < '8') {
										num <<= 3;
										num += *message - '0'; // 2nd octal digit
										++message;
										CHECK_EOS(message);
										if(ISDIGIT(*message) && *message < '8') {
											num <<= 3;
											num += *message - '0'; // 3rd octal digit
											++message;
											CHECK_EOS(message);
										}
									}
									WFIFOB(sd->fd,off) = TOB(num);
									continue;
								}
						}
					} else
						num = *message;
					WFIFOB(sd->fd,off) = TOB(num);
					++message;
					CHECK_EOS(message);
				} // For
				while(*message != '"') { // Ignore extra characters
					++message;
					CHECK_EOS(message);
				}

				// Terminate the string
				if(off < end) { // Fill the rest with 0's
					memset(WFIFOP(sd->fd,off),0,end - off);
					off = end;
				}
			} else { // Unknown
				PARSE_ERROR(msg_txt(908),message); // Unknown type of value in:
				return -1;
			}
			SKIP_VALUE(message);
		}

		if(packet_db[type].len == -1) { // Send dynamic packet
			WFIFOW(sd->fd,2) = TOW(off);
			WFIFOSET(sd->fd,off);
		} else { // Send static packet
			if(off < len)
				memset(WFIFOP(sd->fd,off),0,len - off);
			WFIFOSET(sd->fd,len);
		}
	} else {
		clif_displaymessage(fd, msg_txt(259)); // Invalid packet
		return -1;
	}
	sprintf(atcmd_output, msg_txt(258), type, type); // Sent packet 0x%x (%d)
	clif_displaymessage(fd, atcmd_output);
	return 0;
#undef PARSE_ERROR
#undef CHECK_EOS
#undef SKIP_VALUE
#undef GET_VALUE
}

/**
 * Retrieves map name suggestions for a given string.
 * This will first check if any map names contain the given string, and will
 *   print out MAX_SUGGESTIONS results if any maps are found.
 * Otherwise, suggestions will be calculated through Levenshtein distance,
 *   and up to 5 of the closest matches will be printed.
 *
 * @author Euphy
 */
static void warp_get_suggestions(struct map_session_data *sd, const char *name) {
	char buffer[512];
	int i, count = 0;

	if (strlen(name) < 2)
		return;

	// Build the suggestion string
	strcpy(buffer, msg_txt(205)); // Maybe you meant:
	strcat(buffer, "\n");

	// Check for maps that contain string
	for (i = 0; i < MAX_MAP_PER_SERVER; i++) {
		if (count < MAX_SUGGESTIONS && strstr(map[i].name, name)) {
			strcat(buffer, map[i].name);
			strcat(buffer, " ");
			if (++count >= MAX_SUGGESTIONS)
				break;
		}
	}

	// If no maps found, search by edit distance
	if (!count) {
		unsigned int distance[MAX_MAP_PER_SERVER][2];
		int j;

		// Calculate Levenshtein distance for all maps
		for (i = 0; i < MAX_MAP_PER_SERVER; i++) {
			if (strlen(map[i].name) < 4) // Invalid map name?
				distance[i][0] = INT_MAX;
			else {
				distance[i][0] = levenshtein(map[i].name, name);
				distance[i][1] = i;
			}
		}

		// Selection sort elements as needed
		count = min(MAX_SUGGESTIONS, 5); // Results past 5 aren't worth showing
		for (i = 0; i < count; i++) {
			int min = i;

			for (j = i + 1; j < MAX_MAP_PER_SERVER; j++) {
				if (distance[j][0] < distance[min][0])
					min = j;
			}

			// Print map name
			if (distance[min][0] > 4) { // Awful results, don't bother
				if (!i)
					return;
				break;
			}
			strcat(buffer, map[distance[min][1]].name);
			strcat(buffer, " ");

			// Swap elements
			swap(distance[i][0], distance[min][0]);
			swap(distance[i][1], distance[min][1]);
		}
	}

	clif_displaymessage(sd->fd, buffer);
}

/*==========================================
 * @rura, @warp, @mapmove
 *------------------------------------------*/
ACMD_FUNC(mapmove)
{
	char map_name[MAP_NAME_LENGTH_EXT];
	unsigned short mapindex;
	short x = 0, y = 0;
	int16 m = -1;

	nullpo_retr(-1, sd);

	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message ||
		(sscanf(message, "%15s %hd %hd", map_name, &x, &y) < 3 &&
		 sscanf(message, "%15[^,],%hd,%hd", map_name, &x, &y) < 1)) {
		clif_displaymessage(fd, msg_txt(909)); // Please enter a map (usage: @warp/@rura/@mapmove <mapname> <x> <y>).
		return -1;
	}

	mapindex = mapindex_name2id(map_name);
	if (mapindex)
		m = map_mapindex2mapid(mapindex);

	if (!mapindex) { // m < 0 means on different server! [Kevin]
		clif_displaymessage(fd, msg_txt(1)); // Map not found.
		if (battle_config.warp_suggestions_enabled)
			warp_get_suggestions(sd, map_name);
		return -1;
	}

	if (sd->bl.m == m && sd->bl.x == x && sd->bl.y == y) {
		clif_displaymessage(fd, msg_txt(253)); // You already are at your destination!
		return -1;
	}

	//This is to prevent the pc_setpos call from printing an error
	if ((x || y) && map_getcell(m, x, y, CELL_CHKNOPASS) && pc_get_group_level(sd) < battle_config.gm_ignore_warpable_area) {
		clif_displaymessage(fd, msg_txt(2)); // Invalid coordinates, using random target cell.
		if (!map_search_freecell(NULL, m, &x, &y, 10, 10, 1))
			x = y = 0; //Invalid cell, use random spot
	}
	if ((map[m].flag.nowarpto && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) || !pc_job_can_entermap((enum e_job)sd->status.class_, m, sd->group_level)) {
		clif_displaymessage(fd, msg_txt(247)); // You are not authorized to warp to this map.
		return -1;
	}
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(248)); // You are not authorized to warp from your current map.
		return -1;
	}
	if (pc_setpos(sd, mapindex, x, y, CLR_TELEPORT) != SETPOS_OK) {
		clif_displaymessage(fd, msg_txt(1)); // Map not found.
		return -1;
	}

	clif_displaymessage(fd, msg_txt(0)); // Warped.
	return 0;
}

/*==========================================
 * Displays where a character is. Corrected version by Silent. [Skotlex]
 *------------------------------------------*/
ACMD_FUNC(where)
{
	struct map_session_data *pl_sd;

	nullpo_retr(-1, sd);
	memset(atcmd_player_name, '\0', sizeof atcmd_player_name);

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(910)); // Please enter a player name (usage: @where <char name>).
		return -1;
	}

	pl_sd = map_nick2sd(atcmd_player_name);
	if (pl_sd == NULL ||
	    strncmp(pl_sd->status.name, atcmd_player_name, NAME_LENGTH) != 0 ||
	    (pc_has_permission(pl_sd, PC_PERM_HIDE_SESSION) && pc_get_group_level(pl_sd) > pc_get_group_level(sd) && !pc_has_permission(sd, PC_PERM_WHO_DISPLAY_AID))
	) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	snprintf(atcmd_output, sizeof atcmd_output, "%s %s %d %d", pl_sd->status.name, mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(jumpto)
{
	struct map_session_data *pl_sd = NULL;

	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(911)); // Please enter a player name (usage: @jumpto/@warpto/@goto <char name/ID>).
		return -1;
	}

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(248)); // You are not authorized to warp from your current map.
		return -1;
	}

	if( pc_isdead(sd) ) {
		clif_displaymessage(fd, msg_txt(664)); // You cannot use this command when dead.
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL && (pl_sd = map_charid2sd(atoi(atcmd_player_name))) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}
	
	if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarpto && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(247));	// You are not authorized to warp to this map.
		return -1;
	}

	if (pl_sd->bl.m == sd->bl.m && pl_sd->bl.x == sd->bl.x && pl_sd->bl.y == sd->bl.y) {
		clif_displaymessage(fd, msg_txt(253)); // You already are at your destination!
		return -1;
	}

	pc_setpos(sd, pl_sd->mapindex, pl_sd->bl.x, pl_sd->bl.y, CLR_TELEPORT);
	sprintf(atcmd_output, msg_txt(4), pl_sd->status.name); // Jumped to %s
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(jump)
{
	short x = 0, y = 0;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	sscanf(message, "%hd %hd", &x, &y);

	if (map[sd->bl.m].flag.noteleport && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(248)); // You are not authorized to warp from your current map.
		return -1;
	}

	if (pc_isdead(sd)) {
		clif_displaymessage(fd, msg_txt(664)); // You cannot use this command when dead.
		return -1;
	}

	if ((x || y) && map_getcell(sd->bl.m, x, y, CELL_CHKNOPASS)) {
		//This is to prevent the pc_setpos call from printing an error.
		clif_displaymessage(fd, msg_txt(2)); // Invalid coordinates, using random target cell.
		if (!map_search_freecell(NULL, sd->bl.m, &x, &y, 10, 10, 1))
			x = y = 0; //Invalid cell, use random spot.
	}

	if (x && y && sd->bl.x == x && sd->bl.y == y) {
		clif_displaymessage(fd, msg_txt(253)); // You already are at your destination!
		return -1;
	}

	pc_setpos(sd, sd->mapindex, x, y, CLR_TELEPORT);
	sprintf(atcmd_output, msg_txt(5), sd->bl.x, sd->bl.y); // Jumped to %d %d
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * Display list of online characters with
 * various info.
 *------------------------------------------*/
ACMD_FUNC(who)
{
	struct map_session_data *pl_sd = NULL;
	struct s_mapiterator *iter = NULL;
	char player_name[NAME_LENGTH] = "";
	int count = 0;
	int level = 0;
	StringBuf buf;
	/**
	 * 1 = @who  : Player name, [Title], [Party name], [Guild name]
	 * 2 = @who2 : Player name, [Title], BLvl, JLvl, Job
	 * 3 = @who3 : [CID/AID] Player name [Title], Map, X, Y
	 */
	int display_type = 1;
	int map_id = -1;

	nullpo_retr(-1, sd);

	if (strstr(command, "map")) {
		char map_name[MAP_NAME_LENGTH_EXT] = "";

		if (sscanf(message, "%15s %23s", map_name, player_name) < 1 || (map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	} else
		sscanf(message, "%23s", player_name);

	if (strstr(command, "2"))
		display_type = 2;
	else if (strstr(command, "3"))
		display_type = 3;

	level = pc_get_group_level(sd);
	StringBuf_Init(&buf);

	iter = mapit_getallusers();
	for (pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter))	{
		if (!((pc_has_permission(pl_sd, PC_PERM_HIDE_SESSION) || pc_isinvisible(pl_sd)) && pc_get_group_level(pl_sd) > level)) { // You can look only lower or same level
			if (!stristr(pl_sd->status.name, player_name) || // Search with no case sensitive
				(map_id >= 0 && pl_sd->bl.m != map_id))
				continue;
			switch (display_type) {
				case 2:
					StringBuf_Printf(&buf, msg_txt(343), pl_sd->status.name); // "Name: %s "
					if (pc_get_group_id(pl_sd) > 0) // Player title, if exists
						StringBuf_Printf(&buf, msg_txt(344), pc_group_id2name(pc_get_group_id(pl_sd))); // "(%s) "
					StringBuf_Printf(&buf, msg_txt(347), pl_sd->status.base_level, pl_sd->status.job_level,
						job_name(pl_sd->status.class_)); // "| Lv:%d/%d | Job: %s"
					break;
				case 3:
					if (pc_has_permission(sd, PC_PERM_WHO_DISPLAY_AID))
						StringBuf_Printf(&buf, msg_txt(912), pl_sd->status.char_id, pl_sd->status.account_id);	// "(CID:%d/AID:%d) "
					StringBuf_Printf(&buf, msg_txt(343), pl_sd->status.name); // "Name: %s "
					if (pc_get_group_id(pl_sd) > 0) // Player title, if exists
						StringBuf_Printf(&buf, msg_txt(344), pc_group_id2name(pc_get_group_id(pl_sd))); // "(%s) "
					StringBuf_Printf(&buf, msg_txt(348), mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y); // "| Location: %s %d %d"
					break;
				default: {
						struct party_data *p = party_search(pl_sd->status.party_id);
						struct guild *g = pl_sd->guild;

						StringBuf_Printf(&buf, msg_txt(343), pl_sd->status.name); // "Name: %s "
						if (pc_get_group_id(pl_sd) > 0) // Player title, if exists
							StringBuf_Printf(&buf, msg_txt(344), pc_group_id2name(pc_get_group_id(pl_sd))); // "(%s) "
						if (p)
							StringBuf_Printf(&buf, msg_txt(345), p->party.name); // " | Party: '%s'"
						if (g)
							StringBuf_Printf(&buf, msg_txt(346), g->name); // " | Guild: '%s'"
					}
					break;
			}
			clif_displaymessage(fd, StringBuf_Value(&buf));
			StringBuf_Clear(&buf);
			count++;
		}
	}
	mapit_free(iter);

	if (map_id < 0) {
		if (!count)
			StringBuf_Printf(&buf, msg_txt(28)); // No player found.
		else if (count == 1)
			StringBuf_Printf(&buf, msg_txt(29)); // 1 player found.
		else
			StringBuf_Printf(&buf, msg_txt(30), count); // %d players found.
	} else {
		if (!count)
			StringBuf_Printf(&buf, msg_txt(54), map[map_id].name); // No player found in map '%s'.
		else if (count == 1)
			StringBuf_Printf(&buf, msg_txt(55), map[map_id].name); // 1 player found in map '%s'.
		else
			StringBuf_Printf(&buf, msg_txt(56), count, map[map_id].name); // %d players found in map '%s'.
	}
	clif_displaymessage(fd, StringBuf_Value(&buf));
	StringBuf_Destroy(&buf);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(whogm)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
	int j, count;
	int level;
	char match_text[CHAT_SIZE_MAX];
	char player_name[NAME_LENGTH];
	struct guild *g;
	struct party_data *p;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%255[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	level = pc_get_group_level(sd);

	iter = mapit_getallusers();
	for (pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter)) {
		int pl_level = pc_get_group_level(pl_sd);

		if (!pl_level)
			continue;

		if (match_text[0]) {
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
			// Search with no case sensitive
			if (strstr(player_name, match_text) == NULL)
				continue;
		}
		if (pl_level > level) {
			if (pc_isinvisible(pl_sd))
				continue;
			sprintf(atcmd_output, msg_txt(913), pl_sd->status.name); // Name: %s (GM)
			clif_displaymessage(fd, atcmd_output);
			count++;
			continue;
		}

		sprintf(atcmd_output, msg_txt(914), // Name: %s (GM:%d) | Location: %s %d %d
			pl_sd->status.name, pl_level,
			mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
		clif_displaymessage(fd, atcmd_output);

		sprintf(atcmd_output, msg_txt(915), // BLvl: %d | Job: %s (Lvl: %d)
			pl_sd->status.base_level,
			job_name(pl_sd->status.class_), pl_sd->status.job_level);
		clif_displaymessage(fd, atcmd_output);

		p = party_search(pl_sd->status.party_id);
		g = pl_sd->guild;

		sprintf(atcmd_output,msg_txt(916),	// Party: '%s' | Guild: '%s'
			(p ? p->party.name : msg_txt(917)), (g ? g->name : msg_txt(917)));	// None.

		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		clif_displaymessage(fd, msg_txt(150)); // No GM found.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(151)); // 1 GM found.
	else {
		sprintf(atcmd_output, msg_txt(152), count); // %d GMs found.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(save)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].instance_id) {
		clif_displaymessage(fd, msg_txt(383)); // You cannot create a savepoint in an instance.
		return 1;
	}

	pc_setsavepoint(sd, sd->mapindex, sd->bl.x, sd->bl.y);
	if (sd->status.pet_id > 0 && sd->pd)
		intif_save_petdata(sd->status.account_id, &sd->pd->pet);

	chrif_save(sd, CSAVE_NORMAL);

	clif_displaymessage(fd, msg_txt(6)); // Your save point has been changed.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(load)
{
	int16 m;

	nullpo_retr(-1, sd);

	m = map_mapindex2mapid(sd->status.save_point.map);
	if (m >= 0 && map[m].flag.nowarpto && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(249));	// You are not authorized to warp to your save map.
		return -1;
	}
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(248));	// You are not authorized to warp from your current map.
		return -1;
	}

	pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, CLR_OUTSIGHT);
	clif_displaymessage(fd, msg_txt(7)); // Warping to save point..

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(speed)
{
	int speed;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &speed) < 1) {
		sprintf(atcmd_output, msg_txt(918), MIN_WALK_SPEED, MAX_WALK_SPEED); // Please enter a speed value (usage: @speed <%d-%d>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	sd->state.permanent_speed = 0;

	if (speed < 0)
		sd->base_status.speed = DEFAULT_WALK_SPEED;
	else
		sd->base_status.speed = cap_value(speed, MIN_WALK_SPEED, MAX_WALK_SPEED);

	if (sd->base_status.speed != DEFAULT_WALK_SPEED) {
		sd->state.permanent_speed = 1; // Set lock when set to non-default speed.
		clif_displaymessage(fd, msg_txt(8)); // Speed changed.
	} else
		clif_displaymessage(fd, msg_txt(172)); //Speed returned to normal.

	status_calc_bl(&sd->bl, SCB_SPEED);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(storage)
{
	nullpo_retr(-1, sd);

	if (sd->npc_id || sd->state.vending || sd->state.buyingstore || sd->state.trading || sd->state.storage_flag)
		return -1;

	if (storage_storageopen(sd) == 1) { //Already open.
		clif_displaymessage(fd, msg_txt(250)); // You have already opened your storage. Close it first.
		return -1;
	}

	clif_displaymessage(fd, msg_txt(919)); // Storage opened.

	return 0;
}


/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(guildstorage)
{
	nullpo_retr(-1, sd);

	if (sd->npc_id || sd->state.vending || sd->state.buyingstore || sd->state.trading)
		return -1;

	switch (storage_guild_storageopen(sd)) {
		case GSTORAGE_OPEN:
			clif_displaymessage(fd, msg_txt(920)); // Guild storage opened.
			break;
		case GSTORAGE_STORAGE_ALREADY_OPEN:
			clif_displaymessage(fd, msg_txt(250)); // You have already opened your storage. Close it first.
			return -1;
		case GSTORAGE_ALREADY_OPEN:
			clif_displaymessage(fd, msg_txt(251)); // You have already opened your guild storage. Close it first.
			return -1;
		case GSTORAGE_NO_GUILD:
			clif_displaymessage(fd, msg_txt(252)); // You are not in a guild.
			return -1;
		case GSTORAGE_NO_STORAGE:
			clif_displaymessage(fd, msg_txt(284)); // The guild does not have a guild storage.
			return -1;
		case GSTORAGE_NO_PERMISSION:
			clif_displaymessage(fd, msg_txt(285)); // You do not have permission to use the guild storage.
			return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(option)
{
	int param1 = 0, param2 = 0, param3 = 0;
	nullpo_retr(-1, sd);

	// Failed to match the parameters so inform the user of the options
	if (!message || !*message || sscanf(message, "%d %d %d", &param1, &param2, &param3) < 1 || param1 < 0 || param2 < 0 || param3 < 0) {
		const char *text;

		// Attempt to find the setting information for this command
		text = atcommand_help_string(command);

		// Notify the user of the requirement to enter an option
		clif_displaymessage(fd, msg_txt(921)); // Please enter at least one option.

		if (text) // Send the help text associated with this command
			clif_displaymessage(fd, text);

		return -1;
	}

	sd->sc.opt1 = param1;
	sd->sc.opt2 = param2;
	pc_setoption(sd, param3);
	
	clif_displaymessage(fd, msg_txt(9)); // Options changed.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(hide)
{
	nullpo_retr(-1, sd);

	if (pc_isinvisible(sd)) {
		sd->sc.option &= ~OPTION_INVISIBLE;
		if (sd->disguise)
			status_set_viewdata(&sd->bl, sd->disguise);
		else
			status_set_viewdata(&sd->bl, sd->status.class_);
		clif_displaymessage(fd, msg_txt(10)); // Invisible: Off

		// Increment the number of pvp players on the map
		map[sd->bl.m].users_pvp++;

		// Register the player for ranking calculations
		if( !battle_config.pk_mode && map[sd->bl.m].flag.pvp && !map[sd->bl.m].flag.pvp_nocalcrank )
			sd->pvp_timer = add_timer(gettick() + 200, pc_calc_pvprank_timer, sd->bl.id, 0);

		// bugreport:2266
		map_foreachinmovearea(clif_insight, &sd->bl, AREA_SIZE, sd->bl.x, sd->bl.y, BL_ALL, &sd->bl);
	} else {
		sd->sc.option |= OPTION_INVISIBLE;
		sd->vd.class_ = JT_INVISIBLE;
		clif_displaymessage(fd, msg_txt(11)); // Invisible: On

		// Decrement the number of pvp players on the map
		map[sd->bl.m].users_pvp--;

		// Unregister the player for ranking
		if( map[sd->bl.m].flag.pvp && !map[sd->bl.m].flag.pvp_nocalcrank && sd->pvp_timer != INVALID_TIMER ) {
			delete_timer(sd->pvp_timer, pc_calc_pvprank_timer);
			sd->pvp_timer = INVALID_TIMER;
		}
	}
	clif_changeoption(&sd->bl);

	return 0;
}

/*==========================================
 * Changes a character's class
 *------------------------------------------*/
ACMD_FUNC(jobchange)
{
	int job = 0, upper = 0;
	const char *text;

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d", &job, &upper) < 1) {
		upper = 0;

		if (message) {
			int i;
			bool found = false;

			// Normal Jobs
			for (i = JOB_NOVICE; i < JOB_MAX_BASIC && !found; i++) {
				if (strncmpi(message, job_name(i), 16) == 0) {
					job = i;
					found = true;
				}
			}

			// High Jobs, Babys and Third
			for (i = JOB_NOVICE_HIGH; i < JOB_MAX && !found; i++) {
				if (strncmpi(message, job_name(i), 16) == 0) {
					job = i;
					found = true;
				}
			}

			if (!found) {
				text = atcommand_help_string(command);
				if (text)
					clif_displaymessage(fd, text);
				return -1;
			}
		}
	}

	if (job == JOB_KNIGHT2 || job == JOB_CRUSADER2 || job == JOB_WEDDING || job == JOB_XMAS || job == JOB_SUMMER || job == JOB_HANBOK || job == JOB_OKTOBERFEST || job == JOB_SUMMER2 ||
		job == JOB_LORD_KNIGHT2 || job == JOB_PALADIN2 || job == JOB_BABY_KNIGHT2 || job == JOB_BABY_CRUSADER2 || job == JOB_STAR_GLADIATOR2 ||
		(job >= JOB_RUNE_KNIGHT2 && job <= JOB_MECHANIC_T2) || (job >= JOB_BABY_RUNE2 && job <= JOB_BABY_MECHANIC2) || job == JOB_BABY_STAR_GLADIATOR2)
	{ // Deny direct transformation into dummy jobs
		clif_displaymessage(fd, msg_txt(923)); //"You can not change to this job by command."
		return 0;
	}

	if (pcdb_checkid(job)) {
		if (pc_jobchange(sd, job, upper))
			clif_displaymessage(fd, msg_txt(12)); // Your job has been changed.
		else {
			clif_displaymessage(fd, msg_txt(155)); // You are unable to change your job.
			return -1;
		}
	} else {
		text = atcommand_help_string(command);
		if (text)
			clif_displaymessage(fd, text);
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(kill)
{
	nullpo_retr(-1, sd);
	status_kill(&sd->bl);
	clif_displaymessage(sd->fd, msg_txt(13)); // A pity! You've died.
	if (fd != sd->fd)
		clif_displaymessage(fd, msg_txt(14)); // Character killed.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(alive)
{
	nullpo_retr(-1, sd);
	if (!status_revive(&sd->bl, 100, 100)) {
		clif_displaymessage(fd, msg_txt(667)); // You're not dead.
		return -1;
	}
	clif_skill_nodamage(&sd->bl,&sd->bl,ALL_RESURRECTION,4,1);
	clif_displaymessage(fd, msg_txt(16)); // You've been revived! It's a miracle!
	return 0;
}

/*==========================================
 * +kamic [LuzZza]
 *------------------------------------------*/
ACMD_FUNC(kami)
{
	unsigned long color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (*(command + 5) != 'c' && *(command + 5) != 'C') {
		if (!message || !*message) {
			clif_displaymessage(fd, msg_txt(980)); // Please enter a message (usage: @kami <message>).
			return -1;
		}
		sscanf(message, "%255[^\n]", atcmd_output);
		if (strstr(command, "l"))
			clif_broadcast(&sd->bl, atcmd_output, strlen(atcmd_output) + 1, BC_DEFAULT, ALL_SAMEMAP);
		else
			intif_broadcast(atcmd_output, strlen(atcmd_output) + 1, (*(command + 5) == 'b' || *(command + 5) == 'B') ? BC_BLUE : BC_YELLOW);
	} else {
		if (!message || !*message || (sscanf(message, "%lx %255[^\n]", &color, atcmd_output) < 2)) {
			clif_displaymessage(fd, msg_txt(981)); // Please enter color and message (usage: @kamic <color> <message>).
			return -1;
		}
		if (color > 0xFFFFFF) {
			clif_displaymessage(fd, msg_txt(982)); // Invalid color.
			return -1;
		}
		intif_broadcast2(atcmd_output, strlen(atcmd_output) + 1, color, 0x190, 12, 0, 0);
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(heal)
{
	int hp = 0, sp = 0; // [Valaris] thanks to fov
	nullpo_retr(-1, sd);

	sscanf(message, "%d %d", &hp, &sp);

	// some overflow checks
	if( hp == INT_MIN ) hp++;
	if( sp == INT_MIN ) sp++;

	if ( hp == 0 && sp == 0 ) {
		if (!status_percent_heal(&sd->bl, 100, 100))
			clif_displaymessage(fd, msg_txt(157)); // HP and SP have already been recovered.
		else
			clif_displaymessage(fd, msg_txt(17)); // HP, SP recovered.
		return 0;
	}

	if ( hp > 0 && sp >= 0 ) {
		if(!status_heal(&sd->bl, hp, sp, 0))
			clif_displaymessage(fd, msg_txt(157)); // HP and SP are already with the good value.
		else
			clif_displaymessage(fd, msg_txt(17)); // HP, SP recovered.
		return 0;
	}

	if ( hp < 0 && sp <= 0 ) {
		status_damage(NULL, &sd->bl, -hp, -sp, 0, 0);
		clif_damage(&sd->bl, &sd->bl, gettick(), 0, 0, -hp, 0, DMG_ENDURE, 0, false);
		clif_displaymessage(fd, msg_txt(156)); // HP or/and SP modified.
		return 0;
	}

	//Opposing signs.
	if ( hp ) {
		if (hp > 0)
			status_heal(&sd->bl, hp, 0, 0);
		else {
			status_damage(NULL, &sd->bl, -hp, 0, 0, 0);
			clif_damage(&sd->bl, &sd->bl, gettick(), 0, 0, -hp, 0, DMG_ENDURE, 0, false);
		}
	}

	if ( sp ) {
		if (sp > 0)
			status_heal(&sd->bl, 0, sp, 0);
		else
			status_damage(NULL, &sd->bl, 0, -sp, 0, 0);
	}

	clif_displaymessage(fd, msg_txt(156)); // HP or/and SP modified.
	return 0;
}

/*==========================================
 * @item command (usage: @item <itemdid1:itemid2:itemname:..> <quantity>) (modified by [Yor] for pet_egg)
 * @itembound command (usage: @itembound <name/id_of_item> <quantity> <bound_type>)
 *------------------------------------------*/
ACMD_FUNC(item)
{
	char item_name[100];
	int number = 0, bound = BOUND_NONE;
	char flag = 0;
	struct item_data *item_data[10];
	int get_count, i, j = 0;
	char *itemlist;

	nullpo_retr(-1, sd);
	memset(item_name, '\0', sizeof(item_name));

	parent_cmd = atcommand_checkalias(command + 1);

	if (!strcmpi(parent_cmd, "itembound")) {
		if (!message || !*message || (
			sscanf(message, "\"%99[^\"]\" %d %d", item_name, &number, &bound) < 3 &&
			sscanf(message, "%99s %d %d", item_name, &number, &bound) < 3))
		{
			clif_displaymessage(fd, msg_txt(295)); // Please enter an item name or ID (usage: @itembound <item name/ID> <quantity> <bound_type>).
			clif_displaymessage(fd, msg_txt(298)); // Invalid bound type.
			return -1;
		}
		if (bound <= BOUND_NONE || bound >= BOUND_MAX) {
			clif_displaymessage(fd, msg_txt(298)); // Invalid bound type.
			return -1;
		}
	} else if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d", item_name, &number) < 1 &&
		sscanf(message, "%99s %d", item_name, &number) < 1))
	{
		clif_displaymessage(fd, msg_txt(983)); //Please enter an item name or ID (usage: @item <item name/ID> <quantity>).
		return -1;
	}

	itemlist = strtok(item_name, ":");
	while (itemlist && j < 10) {
		if (!(item_data[j] = itemdb_searchname(itemlist)) && !(item_data[j] = itemdb_exists(atoi(itemlist)))) {
			clif_displaymessage(fd, msg_txt(19)); //Invalid item ID or name.
			return -1;
		}
		itemlist = strtok(NULL, ":"); //Next itemline
		j++;
	}

	get_count = number = max(number, 1);

	for (j--; j >= 0; j--) { //Produce items in list
		unsigned short nameid = item_data[j]->nameid;

		//Check if it's stackable
		if (!itemdb_isstackable2(item_data[j]) || item_data[j]->flag.guid)
			get_count = 1;
		for (i = 0; i < number; i += get_count) {
			if (!pet_create_egg(sd, nameid)) { //If not pet egg
				struct item item_tmp;

				memset(&item_tmp, 0, sizeof(item_tmp));
				item_tmp.nameid = nameid;
				item_tmp.identify = 1;
				item_tmp.bound = bound;
				if ((flag = pc_additem(sd, &item_tmp, get_count, LOG_TYPE_COMMAND)))
					clif_additem(sd, 0, 0, flag);
			}
		}
	}

	if (!flag)
		clif_displaymessage(fd, msg_txt(18)); //Item created.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(item2)
{
	struct item item_tmp;
	struct item_data *item_data;
	char item_name[100];
	unsigned short nameid;
	int number = 0, bound = BOUND_NONE;
	int identify = 0, refine = 0, attr = 0;
	int c1 = 0, c2 = 0, c3 = 0, c4 = 0;

	nullpo_retr(-1, sd);
	memset(item_name, '\0', sizeof(item_name));

	parent_cmd = atcommand_checkalias(command + 1);

	if (!strcmpi(parent_cmd, "itembound2")) {
		if (!message || !*message || (
			sscanf(message, "\"%99[^\"]\" %d %d %d %d %d %d %d %d %d", item_name, &number, &identify, &refine, &attr, &c1, &c2, &c3, &c4, &bound) < 10 &&
			sscanf(message, "%99s %d %d %d %d %d %d %d %d %d", item_name, &number, &identify, &refine, &attr, &c1, &c2, &c3, &c4, &bound) < 10))
		{
			clif_displaymessage(fd, msg_txt(296)); // Please enter all parameters (usage: @itembound2 <item name/ID> <quantity>
			clif_displaymessage(fd, msg_txt(297)); //   <identify_flag> <refine> <attribute> <card1> <card2> <card3> <card4> <bound_type>).
			clif_displaymessage(fd, msg_txt(298)); // Invalid bound type
			return -1;
		}
		if (bound <= BOUND_NONE || bound >= BOUND_MAX) {
			clif_displaymessage(fd, msg_txt(298)); // Invalid bound type
			return -1;
		}
	} else if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d %d %d %d %d %d %d %d", item_name, &number, &identify, &refine, &attr, &c1, &c2, &c3, &c4) < 9 &&
		sscanf(message, "%99s %d %d %d %d %d %d %d %d", item_name, &number, &identify, &refine, &attr, &c1, &c2, &c3, &c4) < 9))
	{
		clif_displaymessage(fd, msg_txt(984)); // Please enter all parameters (usage: @item2 <item name/ID> <quantity>
		clif_displaymessage(fd, msg_txt(985)); //   <identify_flag> <refine> <attribute> <card1> <card2> <card3> <card4>).
		return -1;
	}

	nameid = 0;
	if ((item_data = itemdb_searchname(item_name)) || (item_data = itemdb_exists(atoi(item_name))))
		nameid = item_data->nameid;

	number = max(number, 1);
	if (nameid > 500) {
		int loop, get_count, i;
		char flag = 0;

		//Check if it's stackable
		if (!itemdb_isstackable2(item_data) || item_data->flag.guid) {
			loop = number;
			get_count = 1;
		} else {
			loop = 1;
			get_count = number;
		}

		if (itemdb_isequip2(item_data))
			refine = cap_value(refine, 0, MAX_REFINE);
		else { //All other items cannot be refined and are always identified
			identify = 1;
			refine = attr = 0;
		}

		for (i = 0; i < loop; i++) {
			if (!pet_create_egg(sd, nameid)) { //If not pet egg
				memset(&item_tmp, 0, sizeof(item_tmp));
				item_tmp.nameid = nameid;
				item_tmp.identify = identify;
				item_tmp.refine = refine;
				item_tmp.attribute = attr;
				item_tmp.card[0] = c1;
				item_tmp.card[1] = c2;
				item_tmp.card[2] = c3;
				item_tmp.card[3] = c4;
				item_tmp.bound = bound;
				if ((flag = pc_additem(sd, &item_tmp, get_count, LOG_TYPE_COMMAND)))
					clif_additem(sd, 0, 0, flag);
			}
		}
		if (!flag)
			clif_displaymessage(fd, msg_txt(18)); // Item created.
	} else {
		clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(itemreset)
{
	int i;
	nullpo_retr(-1, sd);

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].amount && !sd->inventory.u.items_inventory[i].equip)
			pc_delitem(sd, i, sd->inventory.u.items_inventory[i].amount, 0, 0, LOG_TYPE_COMMAND);
	}
	clif_displaymessage(fd, msg_txt(20)); // All of your items have been removed.

	return 0;
}

/*==========================================
 * Atcommand @lvlup
 *------------------------------------------*/
ACMD_FUNC(baselevelup)
{
	int level = 0, i = 0, status_point = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || !(level = atoi(message))) {
		clif_displaymessage(fd, msg_txt(986)); // Please enter a level adjustment (usage: @lvup/@blevel/@baselvlup <number of levels>).
		return -1;
	}

	if (level > 0) {
		if (sd->status.base_level >= pc_maxbaselv(sd)) { // Check for max level by Valaris
			clif_displaymessage(fd, msg_txt(47)); // Base level can't go any higher.
			return -1;
		} // End Addition
		if ((unsigned int)level > pc_maxbaselv(sd) || (unsigned int)level > pc_maxbaselv(sd) - sd->status.base_level) // Fix positive overflow
			level = pc_maxbaselv(sd) - sd->status.base_level;
		for (i = 0; i < level; i++)
			status_point += pc_gets_status_point(sd->status.base_level + i);

		sd->status.status_point += status_point;
		sd->status.base_level += (unsigned int)level;
		status_calc_pc(sd, SCO_FORCE);
		status_percent_heal(&sd->bl, 100, 100);
		clif_misceffect(&sd->bl, 0);
		achievement_update_objective(sd, AG_GOAL_LEVEL, 1, sd->status.base_level);
		achievement_update_objective(sd, AG_GOAL_STATUS, 2, sd->status.base_level, sd->status.class_);
		clif_displaymessage(fd, msg_txt(21)); // Base level raised.
	} else {
		if (sd->status.base_level == 1) {
			clif_displaymessage(fd, msg_txt(158)); // Base level can't go any lower.
			return -1;
		}
		level *= -1;
		if ((unsigned int)level >= sd->status.base_level)
			level = sd->status.base_level - 1;
		for (i = 0; i > -level; i--)
			status_point += pc_gets_status_point(sd->status.base_level + i - 1);
		if (sd->status.status_point < status_point)
			pc_resetstate(sd);
		if (sd->status.status_point < status_point)
			sd->status.status_point = 0;
		else
			sd->status.status_point -= status_point;
		sd->status.base_level -= (unsigned int)level;
		clif_displaymessage(fd, msg_txt(22)); // Base level lowered.
		status_calc_pc(sd, SCO_FORCE);
		level *= -1;
	}
	sd->status.base_exp = 0;
	clif_updatestatus(sd, SP_STATUSPOINT);
	clif_updatestatus(sd, SP_BASELEVEL);
	clif_updatestatus(sd, SP_BASEEXP);
	clif_updatestatus(sd, SP_NEXTBASEEXP);
	pc_baselevelchanged(sd);
	if(sd->status.party_id)
		party_send_levelup(sd);
	if(level > 0 && battle_config.atcommand_levelup_events)
		npc_script_event(sd, NPCE_BASELVUP);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(joblevelup)
{
	int level = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || !(level = atoi(message))) {
		clif_displaymessage(fd, msg_txt(987)); // Please enter a level adjustment (usage: @joblvup/@jlevel/@joblvlup <number of levels>).
		return -1;
	}
	if (level > 0) {
		if (sd->status.job_level >= pc_maxjoblv(sd)) {
			clif_displaymessage(fd, msg_txt(23)); // Job level can't go any higher.
			return -1;
		}
		if ((unsigned int)level > pc_maxjoblv(sd) || (unsigned int)level > pc_maxjoblv(sd) - sd->status.job_level) // Fix positive overflow
			level = pc_maxjoblv(sd) - sd->status.job_level;
		sd->status.job_level += (unsigned int)level;
		sd->status.skill_point += level;
		clif_misceffect(&sd->bl, 1);
		achievement_update_objective(sd, AG_GOAL_LEVEL, 1, sd->status.job_level);
		clif_displaymessage(fd, msg_txt(24)); // Job level raised.
	} else {
		if (sd->status.job_level == 1) {
			clif_displaymessage(fd, msg_txt(159)); // Job level can't go any lower.
			return -1;
		}
		level *= -1;
		if ((unsigned int)level >= sd->status.job_level) // Fix negative overflow
			level = sd->status.job_level - 1;
		sd->status.job_level -= (unsigned int)level;
		if (sd->status.skill_point < level)
			pc_resetskill(sd,0); //Reset skills since we need to subtract more points
		if (sd->status.skill_point < level)
			sd->status.skill_point = 0;
		else
			sd->status.skill_point -= level;
		clif_displaymessage(fd, msg_txt(25)); // Job level lowered.
		level *= -1;
	}
	sd->status.job_exp = 0;
	clif_updatestatus(sd, SP_JOBLEVEL);
	clif_updatestatus(sd, SP_JOBEXP);
	clif_updatestatus(sd, SP_NEXTJOBEXP);
	clif_updatestatus(sd, SP_SKILLPOINT);
	status_calc_pc(sd, SCO_FORCE);
	if(level > 0 && battle_config.atcommand_levelup_events)
		npc_script_event(sd, NPCE_JOBLVUP);

	return 0;
}

/*==========================================
 * @help
 *------------------------------------------*/
ACMD_FUNC(help)
{
	struct config_setting_t *help;
	const char *text = NULL;
	const char *command_name = NULL;
	char *default_command = "help";

	nullpo_retr(-1, sd);

	help = config_lookup(&atcommand_config, "help");
	if (!help) {
		clif_displaymessage(fd, msg_txt(27)); // "Commands help is not available."
		return -1;
	}

	if (!message || !*message) {
		command_name = default_command; // If no command_name specified, display help for @help.
	} else {
		if (*message == atcommand_symbol || *message == charcommand_symbol)
			++message;
		command_name = atcommand_checkalias(message);
	}

	if (!pc_can_use_command(sd, command_name, COMMAND_ATCOMMAND)) {
		sprintf(atcmd_output, msg_txt(153), message); // "%s is Unknown Command"
		clif_displaymessage(fd, atcmd_output);
		atcommand_get_suggestions(sd, command_name, true);
		return -1;
	}
	
	if (!config_setting_lookup_string(help, command_name, &text)) {
		sprintf(atcmd_output, msg_txt(988), atcommand_symbol, command_name); // There is no help for %c%s.
		clif_displaymessage(fd, atcmd_output);
		atcommand_get_suggestions(sd, command_name, true);
		return -1;
	}

	sprintf(atcmd_output, msg_txt(989), atcommand_symbol, command_name); // Help for command %c%s:
	clif_displaymessage(fd, atcmd_output);

	{   // Display aliases
		DBIterator *iter;
		AtCommandInfo *command_info;
		AliasInfo *alias_info = NULL;
		StringBuf buf;
		bool has_aliases = false;

		StringBuf_Init(&buf);
		StringBuf_AppendStr(&buf, msg_txt(990)); // Available aliases:
		command_info = get_atcommandinfo_byname(command_name);
		iter = db_iterator(atcommand_alias_db);
		for (alias_info = dbi_first(iter); dbi_exists(iter); alias_info = dbi_next(iter)) {
			if (alias_info->command == command_info) {
				StringBuf_Printf(&buf, " %s", alias_info->alias);
				has_aliases = true;
			}
		}
		dbi_destroy(iter);
		if (has_aliases)
			clif_displaymessage(fd, StringBuf_Value(&buf));
		StringBuf_Destroy(&buf);
	}

	// Display help contents
	clif_displaymessage(fd, text);
	return 0;
}

/**
 * PvP timer handling (stopping)
 * @param bl: Player block object
 * @param ap: func* with va_list values
 * @return 0
 */
static int atcomamnd_mapflag_pvp_stop_sub(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd = map_id2sd(bl->id);

	clif_pvpset(sd, 0, 0, 2);
	if (sd->pvp_timer != INVALID_TIMER) {
		delete_timer(sd->pvp_timer, pc_calc_pvprank_timer);
		sd->pvp_timer = INVALID_TIMER;
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(pvpoff)
{
	nullpo_retr(-1, sd);

	if (!map[sd->bl.m].flag.pvp) {
		clif_displaymessage(fd, msg_txt(160)); // PvP is already Off.
		return -1;
	}

	map[sd->bl.m].flag.pvp = 0;
	if (!battle_config.pk_mode)
		clif_map_property_mapall(sd->bl.m, MAPPROPERTY_NOTHING);
	map_foreachinmap(atcomamnd_mapflag_pvp_stop_sub, sd->bl.m, BL_PC);
	map_foreachinmap(unit_stopattack, sd->bl.m, BL_CHAR, 0);
	clif_displaymessage(fd, msg_txt(31)); // PvP: Off.
	return 0;
}

/**
 * PvP timer handling (starting)
 * @param bl: Player block object
 * @param ap: func* with va_list values
 * @return 0
 */
static void atcommand_mapflag_pvp_start_sub(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd = map_id2sd(bl->id);

	nullpo_retv(sd);

	if (sd->pvp_timer == INVALID_TIMER) {
		sd->pvp_timer = add_timer(gettick() + 200, pc_calc_pvprank_timer, sd->bl.id, 0);
		sd->pvp_rank = 0;
		sd->pvp_lastusers = 0;
		sd->pvp_point = 5;
		sd->pvp_won = 0;
		sd->pvp_lost = 0;
	}

	clif_map_property(&sd->bl, MAPPROPERTY_FREEPVPZONE, SELF);
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(pvpon)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.pvp) {
		clif_displaymessage(fd, msg_txt(161)); // PvP is already On.
		return -1;
	}

	map[sd->bl.m].flag.pvp = 1;

	if (!battle_config.pk_mode) //Display pvp circle and rank
		map_foreachinmap(atcommand_mapflag_pvp_start_sub, sd->bl.m, BL_PC);

	clif_displaymessage(fd, msg_txt(32)); // PvP: On.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(gvgoff)
{
	nullpo_retr(-1, sd);

	if (!map[sd->bl.m].flag.gvg) {
		clif_displaymessage(fd, msg_txt(162)); // GvG is already Off.
		return -1;
	}

	map[sd->bl.m].flag.gvg = 0;
	clif_map_property_mapall(sd->bl.m, MAPPROPERTY_NOTHING);
	map_foreachinmap(unit_stopattack, sd->bl.m, BL_CHAR, 0);
	clif_displaymessage(fd, msg_txt(33)); // GvG: Off.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(gvgon)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.gvg) {
		clif_displaymessage(fd, msg_txt(163)); // GvG is already On.
		return -1;
	}

	map[sd->bl.m].flag.gvg = 1;
	clif_map_property_mapall(sd->bl.m, MAPPROPERTY_AGITZONE);
	clif_displaymessage(fd, msg_txt(34)); // GvG: On.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(model)
{
	int hair_style = 0, hair_color = 0, cloth_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d %d %d", &hair_style, &hair_color, &cloth_color) < 1) {
		sprintf(atcmd_output, msg_txt(991), // Please enter at least one value (usage: @model <hair ID: %d-%d> <hair color: %d-%d> <clothes color: %d-%d>).
			MIN_HAIR_STYLE, MAX_HAIR_STYLE, MIN_HAIR_COLOR, MAX_HAIR_COLOR, MIN_CLOTH_COLOR, MAX_CLOTH_COLOR);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_style >= MIN_HAIR_STYLE && hair_style <= MAX_HAIR_STYLE &&
		hair_color >= MIN_HAIR_COLOR && hair_color <= MAX_HAIR_COLOR &&
		cloth_color >= MIN_CLOTH_COLOR && cloth_color <= MAX_CLOTH_COLOR)
	{
		pc_changelook(sd, LOOK_HAIR, hair_style);
		pc_changelook(sd, LOOK_HAIR_COLOR, hair_color);
		pc_changelook(sd, LOOK_CLOTHES_COLOR, cloth_color);
		clif_displaymessage(fd, msg_txt(36)); // Appearance changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @bodystyle [Rytech]
 *------------------------------------------*/
ACMD_FUNC(bodystyle)
{
	int body_style = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!pc_has_second_costume(sd)) {
		clif_displaymessage(fd, msg_txt(727)); // This job has no alternate body styles.
		return -1;
	}

	if (!message || !*message || sscanf(message, "%d", &body_style) < 1) {
		sprintf(atcmd_output, msg_txt(726), MIN_BODY_STYLE, MAX_BODY_STYLE); // Please enter a body style (usage: @bodystyle <body ID: %d-%d>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (body_style >= MIN_BODY_STYLE && body_style <= MAX_BODY_STYLE) {
		pc_changelook(sd, LOOK_BODY2, body_style);
		clif_displaymessage(fd, msg_txt(36)); // Appearence changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @dye && @ccolor
 *------------------------------------------*/
ACMD_FUNC(dye)
{
	int cloth_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &cloth_color) < 1) {
		sprintf(atcmd_output, msg_txt(992), MIN_CLOTH_COLOR, MAX_CLOTH_COLOR); // Please enter a clothes color (usage: @dye/@ccolor <clothes color: %d-%d>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (cloth_color >= MIN_CLOTH_COLOR && cloth_color <= MAX_CLOTH_COLOR) {
		pc_changelook(sd, LOOK_CLOTHES_COLOR, cloth_color);
		clif_displaymessage(fd, msg_txt(36)); // Appearance changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @hairstyle && @hstyle
 *------------------------------------------*/
ACMD_FUNC(hair_style)
{
	int hair_style = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &hair_style) < 1) {
		sprintf(atcmd_output, msg_txt(993), MIN_HAIR_STYLE, MAX_HAIR_STYLE); // Please enter a hair style (usage: @hairstyle/@hstyle <hair ID: %d-%d>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_style >= MIN_HAIR_STYLE && hair_style <= MAX_HAIR_STYLE) {
			pc_changelook(sd, LOOK_HAIR, hair_style);
			clif_displaymessage(fd, msg_txt(36)); // Appearance changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @haircolor && @hcolor
 *------------------------------------------*/
ACMD_FUNC(hair_color)
{
	int hair_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &hair_color) < 1) {
		sprintf(atcmd_output, msg_txt(994), MIN_HAIR_COLOR, MAX_HAIR_COLOR); // Please enter a hair color (usage: @haircolor/@hcolor <hair color: %d-%d>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_color >= MIN_HAIR_COLOR && hair_color <= MAX_HAIR_COLOR) {
			pc_changelook(sd, LOOK_HAIR_COLOR, hair_color);
			clif_displaymessage(fd, msg_txt(36)); // Appearance changed.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}

	return 0;
}

/*==========================================
 * @go [city_number or city_name] - Updated by Harbin
 *------------------------------------------*/
ACMD_FUNC(go)
{
	int i;
	int town = INT_MAX; // Initialized to INT_MAX instead of -1 to avoid conflicts with those who map [-3:-1] to @memo locations.
	char map_name[MAP_NAME_LENGTH];
 
	const struct {
		char map[MAP_NAME_LENGTH];
		int x, y;
		int min_match; // Minimum string length to match
	} data[] = {
		{ MAP_PRONTERA,    156, 191, 3 }, //  0 = Prontera
		{ MAP_MORROC,      156,  93, 4 }, //  1 = Morroc
		{ MAP_GEFFEN,      119,  59, 3 }, //  2 = Geffen
		{ MAP_PAYON,       162, 233, 3 }, //  3 = Payon
		{ MAP_ALBERTA,     192, 147, 3 }, //  4 = Alberta
#ifdef RENEWAL
		{ MAP_IZLUDE,      128, 146, 3 }, //  5 = Izlude (Renewal)
#else
		{ MAP_IZLUDE,      128, 114, 3 }, //  5 = Izlude
#endif
		{ MAP_ALDEBARAN,   140, 131, 3 }, //  6 = Aldebaran
		{ MAP_LUTIE,       147, 134, 3 }, //  7 = Lutie
		{ MAP_COMODO,      209, 143, 3 }, //  8 = Comodo
		{ MAP_YUNO,        157,  51, 3 }, //  9 = Juno
		{ MAP_AMATSU,      198,  84, 3 }, // 10 = Amatsu
		{ MAP_GONRYUN,     160, 120, 3 }, // 11 = Kunlun
		{ MAP_UMBALA,       89, 157, 3 }, // 12 = Umbala
		{ MAP_NIFLHEIM,     21, 153, 3 }, // 13 = Niflheim
		{ MAP_LOUYANG,     217,  40, 3 }, // 14 = Luoyang
#ifdef RENEWAL
		{ MAP_NOVICE,       97,  90, 3 }, // 15 = Training Grounds (Renewal)
#else
		{ MAP_NOVICE,       53, 111, 3 }, // 15 = Training Grounds
#endif
		{ MAP_JAIL,         23,  61, 3 }, // 16 = Prison
		{ MAP_JAWAII,      249, 127, 3 }, // 17 = Jawaii
		{ MAP_AYOTHAYA,    151, 117, 3 }, // 18 = Ayothaya
		{ MAP_EINBROCH,     64, 200, 5 }, // 19 = Einbroch
		{ MAP_LIGHTHALZEN, 158,  92, 3 }, // 20 = Lighthalzen
		{ MAP_EINBECH,      70,  95, 5 }, // 21 = Einbech
		{ MAP_HUGEL,        96, 145, 3 }, // 22 = Hugel
		{ MAP_RACHEL,      130, 110, 3 }, // 23 = Rachel
		{ MAP_VEINS,       216, 123, 3 }, // 24 = Veins
		{ MAP_MOSCOVIA,    223, 184, 3 }, // 25 = Moscovia
		{ MAP_MIDCAMP,     180, 240, 3 }, // 26 = Midgard Camp
		{ MAP_MANUK,       282, 138, 3 }, // 27 = Manuk
		{ MAP_SPLENDIDE,   197, 176, 3 }, // 28 = Splendide
		{ MAP_BRASILIS,    182, 239, 3 }, // 29 = Brasilis
		{ MAP_DICASTES,    198, 187, 3 }, // 30 = El Dicastes
		{ MAP_MORA,         44, 151, 4 }, // 31 = Mora
		{ MAP_DEWATA,      200, 180, 3 }, // 32 = Dewata
		{ MAP_MALANGDO,    140, 114, 5 }, // 33 = Malangdo Island
		{ MAP_MALAYA,      242, 211, 5 }, // 34 = Malaya Port
		{ MAP_ECLAGE,      110,  39, 3 }, // 35 = Eclage
		{ MAP_LASAGNA,     193, 182, 2 }, // 36 = Lasagna
	};
 
	nullpo_retr(-1, sd);
 
	if (map[sd->bl.m].flag.nogo && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(sd->fd,msg_txt(995)); // You cannot use @go on this map.
		return 0;
	}
 
	memset(map_name, '\0', sizeof(map_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));
 
	if (!message || !*message || sscanf(message, "%11s", map_name) < 1) { // No value matched so send the list of locations
		const char *text;

		// Attempt to find the text help string
		text = atcommand_help_string(command);

		clif_displaymessage(fd, msg_txt(38)); // Invalid location number, or name.

		if (text) // Send the text to the client
			clif_displaymessage(fd, text);

		return -1;
	}

	// Numeric entry
	if (ISDIGIT(*message) || (message[0] == '-' && ISDIGIT(message[1])))
		town = atoi(message);

	if (town < 0 || town >= ARRAYLENGTH(data)) {
		map_name[MAP_NAME_LENGTH - 1] = '\0';

		// Match maps on the list
		for (i = 0; i < ARRAYLENGTH(data); i++) {
			if (strncmpi(map_name, data[i].map, data[i].min_match) == 0) {
				town = i;
				break;
			}
		}
	}

	if (town < 0 || town >= ARRAYLENGTH(data)) { // Alternate spellings
		if (strncmpi(map_name, "morroc", 4) == 0) // Correct town name for 'morocc'
			town = 1;
		else if (strncmpi(map_name, "lutie", 3) == 0) // Correct town name for 'xmas'
			town = 7;
		else if (strncmpi(map_name, "juno", 3) == 0) // Correct town name for 'yuno'
			town = 9;
		else if (strncmpi(map_name, "kunlun", 3) == 0) // Original town name for 'gonryun'
			town = 11;
		else if (strncmpi(map_name, "luoyang", 3) == 0) // Original town name for 'louyang'
			town = 14;
		else if (strncmpi(map_name, "startpoint", 3) == 0 || // Easy to remember alternatives to 'new_1-1'
			strncmpi(map_name, "beginning", 3) == 0)
			town = 15;
		else if (strncmpi(map_name, "prison", 3) == 0 || // Easy to remember alternatives to 'sec_pri'
			strncmpi(map_name, "jail", 3) == 0)
			town = 16;
		else if (strncmpi(map_name, "rael", 3) == 0) // Original town name for 'rachel'
			town = 23;
	}

	if (town >= 0 && town < ARRAYLENGTH(data)) {
		int16 m = map_mapname2mapid(data[town].map);

		if (m >= 0 && map[m].flag.nowarpto && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
			clif_displaymessage(fd, msg_txt(247)); // You are not authorized to warp to this map.
			return -1;
		}
		if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
			clif_displaymessage(fd, msg_txt(248)); // You are not authorized to warp from your current map.
			return -1;
		}
		if (pc_setpos(sd, mapindex_name2id(data[town].map), data[town].x, data[town].y, CLR_TELEPORT) == SETPOS_OK)
			clif_displaymessage(fd, msg_txt(0)); // Warped.
		else {
			clif_displaymessage(fd, msg_txt(1)); // Map not found.
			return -1;
		}
	} else {
		clif_displaymessage(fd, msg_txt(38)); // Invalid location number or name.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(monster)
{
	char name[NAME_LENGTH];
	char monster[NAME_LENGTH];
	char eventname[EVENT_NAME_LENGTH] = "";
	int mob_id;
	int number = 0;
	int count;
	int i, range;
	short mx, my;
	unsigned int size;
	nullpo_retr(-1, sd);

	memset(name, '\0', sizeof(name));
	memset(monster, '\0', sizeof(monster));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(80)); // Give the display name or monster name/id please.
		return -1;
	}
	if (sscanf(message, "\"%23[^\"]\" %23s %d", name, monster, &number) > 1 ||
		sscanf(message, "%23s \"%23[^\"]\" %d", monster, name, &number) > 1) {
		//All data can be left as it is.
	} else if ((count = sscanf(message, "%23s %d %23s", monster, &number, name)) > 1) {
		//Here, it is possible name was not given and we are using monster for it.
		if (count < 3) //Blank mob's name.
			name[0] = '\0';
	} else if (sscanf(message, "%23s %23s %d", name, monster, &number) > 1) {
		//All data can be left as it is.
	} else if (sscanf(message, "%23s", monster) > 0) {
		//As before, name may be already filled.
		name[0] = '\0';
	} else {
		clif_displaymessage(fd, msg_txt(80)); // Give a display name and monster name/id please.
		return -1;
	}

	if ((mob_id = mobdb_searchname(monster)) == 0) // Check name first (to avoid possible name beginning by a number)
		mob_id = mobdb_checkid(atoi(monster));

	if (mob_id == 0) {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	if (number <= 0)
		number = 1;

	if (!name[0])
		strcpy(name, "--ja--");

	// If value of atcommand_spawn_quantity_limit directive is greater than or equal to 1 and quantity of monsters is greater than value of the directive
	if (battle_config.atc_spawn_quantity_limit && number > battle_config.atc_spawn_quantity_limit)
		number = battle_config.atc_spawn_quantity_limit;

	parent_cmd = atcommand_checkalias(command + 1);

	if (strcmp(parent_cmd, "monstersmall") == 0)
		size = SZ_MEDIUM; // This is just gorgeous [mkbu95]
	else if (strcmp(parent_cmd, "monsterbig") == 0)
		size = SZ_BIG;
	else
		size = SZ_SMALL;

	if (battle_config.etc_log)
		ShowInfo("%s monster='%s' name='%s' id=%d count=%d (%d,%d)\n", command, monster, name, mob_id, number, sd->bl.x, sd->bl.y);

	count = 0;
	range = (int)sqrt((float)number) + 2; // Calculation of an odd number (+4 area around)
	for (i = 0; i < number; i++) {
		int k;

		map_search_freecell(&sd->bl, 0, &mx,  &my, range, range, 0);
		k = mob_once_spawn(sd, sd->bl.m, mx, my, name, mob_id, 1, eventname, size, AI_NONE|(mob_id == MOBID_EMPERIUM ? 0x200 : 0x0));
		if (k) {
			//mapreg_setreg(reference_uid(add_str("$@mobid"), i),k); // Retain created mobid in array uncomment if needed
			count++;
		}
	}

	if (count != 0)
		if (number == count)
			clif_displaymessage(fd, msg_txt(39)); // All monster summoned!
		else {
			sprintf(atcmd_output, msg_txt(240), count); // %d monster(s) summoned!
			clif_displaymessage(fd, atcmd_output);
		}
	else {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static int atkillmonster_sub(struct block_list *bl, va_list ap)
{
	struct mob_data *md;
	struct block_list *src;
	int flag;
	nullpo_ret(md = (struct mob_data *)bl);

	flag = va_arg(ap, int);
	src = va_arg(ap, struct block_list *);

	if (md->guardian_data)
		return 0; //Do not touch WoE mobs!

	if (flag)
		status_zap(bl, md->status.hp, 0);
	else
		status_percent_damage(src, bl, 100, 0, true);
	return 1;
}

ACMD_FUNC(killmonster)
{
	int char_id, map_id, drop_flag;
	struct block_list *src;
	char map_name[MAP_NAME_LENGTH_EXT];
	nullpo_retr(-1, sd);

	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message || sscanf(message, "%15s %d", map_name, &char_id) < 1)
		map_id = sd->bl.m;
	else {
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	if (char_id) {
		struct map_session_data *c_sd = map_charid2sd(char_id);

		src = &c_sd->bl;
	} else
		src = NULL;

	parent_cmd = atcommand_checkalias(command + 1);

	drop_flag = strcmp(parent_cmd, "killmonster2");

	map_foreachinmap(atkillmonster_sub, map_id, BL_MOB, -drop_flag, src);

	clif_displaymessage(fd, msg_txt(165)); // All monsters killed!

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(refine)
{
	int j, position = 0, refine = 0, current_position, final_refine;
	int count;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d %d", &position, &refine) < 2) {
		clif_displaymessage(fd, msg_txt(996)); // Please enter a position and an amount (usage: @refine <equip position> <+/-amount>).
		sprintf(atcmd_output, msg_txt(997), EQP_HEAD_LOW); // %d: Lower Headgear
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(998), EQP_HAND_R); // %d: Right Hand
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(999), EQP_GARMENT); // %d: Garment
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(1000), EQP_ACC_L); // %d: Left Accessory
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(1001), EQP_ARMOR); // %d: Body Armor
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(1002), EQP_HAND_L); // %d: Left Hand
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(1003), EQP_SHOES); // %d: Shoes
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(1004), EQP_ACC_R); // %d: Right Accessory
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(1005), EQP_HEAD_TOP); // %d: Top Headgear
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, msg_txt(1006), EQP_HEAD_MID); // %d: Mid Headgear
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	refine = cap_value(refine, -MAX_REFINE, MAX_REFINE);

	count = 0;
	for (j = 0; j < EQI_MAX; j++) {
		short i;

		if ((i = sd->equip_index[j]) < 0)
			continue;

		if (j == EQI_AMMO)
			continue;

		if (pc_is_same_equip_index((enum equip_index)j, sd->equip_index, i))
			continue;

		if (position && !(sd->inventory.u.items_inventory[i].equip&position))
			continue;

		final_refine = cap_value(sd->inventory.u.items_inventory[i].refine + refine, 0, MAX_REFINE);
		if (sd->inventory.u.items_inventory[i].refine != final_refine) {
			sd->inventory.u.items_inventory[i].refine = final_refine;
			current_position = sd->inventory.u.items_inventory[i].equip;
			pc_unequipitem(sd, i, 1|2);
			clif_delitem(sd, i, 1, 3);
			clif_inventorylist(sd);
			clif_refine(fd, 0, i, sd->inventory.u.items_inventory[i].refine);
			pc_equipitem(sd, i, current_position);
			clif_misceffect(&sd->bl, 3);
			achievement_update_objective(sd, AG_REFINE_SUCCESS, 2, sd->inventory_data[i]->wlv, sd->inventory.u.items_inventory[i].refine);
			count++;
		}
	}

	if (count == 0)
		clif_displaymessage(fd, msg_txt(166)); // No item has been refined.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(167)); // 1 item has been refined.
	else {
		sprintf(atcmd_output, msg_txt(168), count); // %d items have been refined.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(produce)
{
	char item_name[100];
	unsigned short item_id;
	int attribute = 0, star = 0;
	struct item_data *item_data;
	struct item tmp_item;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(item_name, '\0', sizeof(item_name));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d %d", item_name, &attribute, &star) < 1 &&
		sscanf(message, "%99s %d %d", item_name, &attribute, &star) < 1
	)) {
		clif_displaymessage(fd, msg_txt(1007)); // Please enter at least one item name/ID (usage: @produce <equip name/ID> <element> <# of very's>).
		return -1;
	}

	if ((item_data = itemdb_searchname(item_name)) == NULL &&
		 (item_data = itemdb_exists(atoi(item_name))) == NULL) {
		clif_displaymessage(fd, msg_txt(170)); //This item is not an equipment.
		return -1;
	}

	item_id = item_data->nameid;

	if (itemdb_isequip2(item_data)) {
		char flag = 0;

		if (attribute < MIN_ATTRIBUTE || attribute > MAX_ATTRIBUTE)
			attribute = ATTRIBUTE_NORMAL;
		if (star < MIN_STAR || star > MAX_STAR)
			star = 0;
		memset(&tmp_item, 0, sizeof tmp_item);
		tmp_item.nameid = item_id;
		tmp_item.amount = 1;
		tmp_item.identify = 1;
		tmp_item.card[0] = CARD0_FORGE;
		tmp_item.card[1] = (item_data->type == IT_WEAPON ?
			((star * 5)<<8) + attribute : 0);
		tmp_item.card[2] = GetWord(sd->status.char_id, 0);
		tmp_item.card[3] = GetWord(sd->status.char_id, 1);
		clif_produceeffect(sd, 0, item_id);
		clif_misceffect(&sd->bl, 3);

		if ((flag = pc_additem(sd, &tmp_item, 1, LOG_TYPE_COMMAND)))
			clif_additem(sd, 0, 0, flag);
	} else {
		sprintf(atcmd_output, msg_txt(169), item_id, item_data->name); // The item (%hu: '%s') is not equipable.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(memo)
{
	int position = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if( !message || !*message || sscanf(message, "%d", &position) < 1 ) {
		int i;
		clif_displaymessage(sd->fd,  msg_txt(668)); // Your actual memo positions are:
		for( i = 0; i < MAX_MEMOPOINTS; i++ ) {
			if( sd->status.memo_point[i].map )
				sprintf(atcmd_output, "%d - %s (%d,%d)", i, mapindex_id2name(sd->status.memo_point[i].map), sd->status.memo_point[i].x, sd->status.memo_point[i].y);
			else
				sprintf(atcmd_output, msg_txt(171), i); // %d - void
			clif_displaymessage(sd->fd, atcmd_output);
		}
		return 0;
	}

	if( position < 0 || position >= MAX_MEMOPOINTS ) {
		sprintf(atcmd_output, msg_txt(1008), 0, MAX_MEMOPOINTS - 1); // Please enter a valid position (usage: @memo <memo_position:%d-%d>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	return !pc_memo(sd, position);
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(gat)
{
	int y;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	for (y = 2; y >= -2; y--) {
		sprintf(atcmd_output, "%s (x= %d, y= %d) %02X %02X %02X %02X %02X",
			map[sd->bl.m].name,   sd->bl.x - 2, sd->bl.y + y,
			map_getcell(sd->bl.m, sd->bl.x - 2, sd->bl.y + y, CELL_GETTYPE),
			map_getcell(sd->bl.m, sd->bl.x - 1, sd->bl.y + y, CELL_GETTYPE),
			map_getcell(sd->bl.m, sd->bl.x,     sd->bl.y + y, CELL_GETTYPE),
			map_getcell(sd->bl.m, sd->bl.x + 1, sd->bl.y + y, CELL_GETTYPE),
			map_getcell(sd->bl.m, sd->bl.x + 2, sd->bl.y + y, CELL_GETTYPE));

		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(displaystatus)
{
	int i, type, flag, tick, val1 = 0, val2 = 0, val3 = 0;
	nullpo_retr(-1, sd);
	
	if (!message || !*message || (i = sscanf(message, "%d %d %d %d %d %d", &type, &flag, &tick, &val1, &val2, &val3)) < 1) {
		clif_displaymessage(fd, msg_txt(1009)); // Please enter a status type/flag (usage: @displaystatus <status type> <flag> <tick> {<val1> {<val2> {<val3>}}}).
		return -1;
	}

	if (i < 2) flag = 1;
	if (i < 3) tick = 0;
	if (i < 4) val1 = 0;
	if (i < 5) val2 = 0;
	if (i < 6) val3 = 0;

	clif_status_change(&sd->bl, type, flag, tick, val1, val2, val3);

	return 0;
}

/*==========================================
 * @stpoint (Rewritten by [Yor])
 *------------------------------------------*/
ACMD_FUNC(statuspoint)
{
	int point;
	unsigned int new_status_point;

	if (!message || !*message || (point = atoi(message)) == 0) {
		clif_displaymessage(fd, msg_txt(1010)); // Please enter a number (usage: @stpoint <number of points>).
		return -1;
	}

	if (point < 0) {
		if (sd->status.status_point < (unsigned int)(-point))
			new_status_point = 0;
		else
			new_status_point = sd->status.status_point + point;
	} else if (UINT_MAX - sd->status.status_point < (unsigned int)point)
		new_status_point = UINT_MAX;
	else
		new_status_point = sd->status.status_point + point;

	if (new_status_point != sd->status.status_point) {
		sd->status.status_point = new_status_point;
		clif_updatestatus(sd, SP_STATUSPOINT);
		clif_displaymessage(fd, msg_txt(174)); // Number of status points changed.
	} else {
		if (point < 0)
			clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		else
			clif_displaymessage(fd, msg_txt(149)); // Unable to increase the number/value.
		return -1;
	}

	return 0;
}

/*==========================================
 * @skpoint (Rewritten by [Yor])
 *------------------------------------------*/
ACMD_FUNC(skillpoint)
{
	int point;
	unsigned int new_skill_point;
	nullpo_retr(-1, sd);

	if (!message || !*message || (point = atoi(message)) == 0) {
		clif_displaymessage(fd, msg_txt(1011)); // Please enter a number (usage: @skpoint <number of points>).
		return -1;
	}

	if (point < 0) {
		if(sd->status.skill_point < (unsigned int)(-point))
			new_skill_point = 0;
		else
			new_skill_point = sd->status.skill_point + point;
	} else if(UINT_MAX - sd->status.skill_point < (unsigned int)point)
		new_skill_point = UINT_MAX;
	else
		new_skill_point = sd->status.skill_point + point;

	if (new_skill_point != sd->status.skill_point) {
		sd->status.skill_point = new_skill_point;
		clif_updatestatus(sd, SP_SKILLPOINT);
		clif_displaymessage(fd, msg_txt(175)); // Number of skill points changed.
	} else {
		if (point < 0)
			clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		else
			clif_displaymessage(fd, msg_txt(149)); // Unable to increase the number/value.
		return -1;
	}

	return 0;
}

/*==========================================
 * @zeny
 *------------------------------------------*/
ACMD_FUNC(zeny)
{
	int zeny = 0, ret = -1;
	nullpo_retr(-1, sd);

	if (!message || !*message || (zeny = atoi(message)) == 0) {
		clif_displaymessage(fd, msg_txt(1012)); // Please enter an amount (usage: @zeny <amount>).
		return -1;
	}

	if (zeny > 0) {
		if ((ret = pc_getzeny(sd, zeny, LOG_TYPE_COMMAND, NULL)) == 1)
			clif_displaymessage(fd, msg_txt(149)); // Unable to increase the number/value.
	} else {
		if (sd->status.zeny < -zeny )
			zeny = -sd->status.zeny;
		if ((ret = pc_payzeny(sd, -zeny, LOG_TYPE_COMMAND, NULL)) == 1)
			clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
	}
	if (ret) //ret != 0 means cmd failure
		return -1;

	clif_displaymessage(fd, msg_txt(176));
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(param)
{
	uint8 i;
	int value = 0;
	const char *param[] = { "str", "agi", "vit", "int", "dex", "luk" };
	short new_value, *status[6], max_status[6];
	// We don't use direct initialization because it isn't part of the c standard.
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &value) < 1 || value == 0) {
		clif_displaymessage(fd, msg_txt(1013)); // Please enter a valid value (usage: @str/@agi/@vit/@int/@dex/@luk <+/-adjustment>).
		return -1;
	}

	ARR_FIND(0, ARRAYLENGTH(param), i, strcmpi(command + 1, param[i]) == 0);

	if (i == ARRAYLENGTH(param) || i > MAX_STATUS_TYPE) { // Normally impossible.
		clif_displaymessage(fd, msg_txt(1013)); // Please enter a valid value (usage: @str/@agi/@vit/@int/@dex/@luk <+/-adjustment>).
		return -1;
	}

	status[0] = &sd->status.str;
	status[1] = &sd->status.agi;
	status[2] = &sd->status.vit;
	status[3] = &sd->status.int_;
	status[4] = &sd->status.dex;
	status[5] = &sd->status.luk;

	if (pc_has_permission(sd,PC_PERM_BYPASS_MAX_STAT))
		max_status[0] = max_status[1] = max_status[2] = max_status[3] = max_status[4] = max_status[5] = SHRT_MAX;
	else {
		max_status[0] = pc_maxparameter(sd,PARAM_STR);
		max_status[1] = pc_maxparameter(sd,PARAM_AGI);
		max_status[2] = pc_maxparameter(sd,PARAM_VIT);
		max_status[3] = pc_maxparameter(sd,PARAM_INT);
		max_status[4] = pc_maxparameter(sd,PARAM_DEX);
		max_status[5] = pc_maxparameter(sd,PARAM_LUK);
	}

	if (value > 0  && *status[i] + value >= max_status[i])
		new_value = max_status[i];
	else if (value < 0 && *status[i] <= -value)
		new_value = 1;
	else
		new_value = *status[i] + value;

	if (new_value != *status[i]) {
		*status[i] = new_value;
		clif_updatestatus(sd, SP_STR + i);
		clif_updatestatus(sd, SP_USTR + i);
		status_calc_pc(sd, SCO_FORCE);
		clif_displaymessage(fd, msg_txt(42)); // Stat changed.
		achievement_update_objective(sd, AG_GOAL_STATUS, 1, new_value);
	} else {
		if (value < 0)
			clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		else
			clif_displaymessage(fd, msg_txt(149)); // Unable to increase the number/value.
		return -1;
	}

	return 0;
}

/*==========================================
 * Stat all by fritz (rewritten by [Yor])
 *------------------------------------------*/
ACMD_FUNC(stat_all)
{
	int value = 0;
	uint8 count, i;
	short *status[PARAM_MAX], max_status[PARAM_MAX];
	// We don't use direct initialization because it isn't part of the c standard.
	nullpo_retr(-1, sd);

	status[0] = &sd->status.str;
	status[1] = &sd->status.agi;
	status[2] = &sd->status.vit;
	status[3] = &sd->status.int_;
	status[4] = &sd->status.dex;
	status[5] = &sd->status.luk;

	if (!message || !*message || sscanf(message, "%d", &value) < 1 || value == 0) {
		max_status[0] = pc_maxparameter(sd,PARAM_STR);
		max_status[1] = pc_maxparameter(sd,PARAM_AGI);
		max_status[2] = pc_maxparameter(sd,PARAM_VIT);
		max_status[3] = pc_maxparameter(sd,PARAM_INT);
		max_status[4] = pc_maxparameter(sd,PARAM_DEX);
		max_status[5] = pc_maxparameter(sd,PARAM_LUK);
		value = SHRT_MAX;
	} else {
		if (pc_has_permission(sd,PC_PERM_BYPASS_MAX_STAT))
			max_status[0] = max_status[1] = max_status[2] = max_status[3] = max_status[4] = max_status[5] = SHRT_MAX;
		else {
			max_status[0] = pc_maxparameter(sd,PARAM_STR);
			max_status[1] = pc_maxparameter(sd,PARAM_AGI);
			max_status[2] = pc_maxparameter(sd,PARAM_VIT);
			max_status[3] = pc_maxparameter(sd,PARAM_INT);
			max_status[4] = pc_maxparameter(sd,PARAM_DEX);
			max_status[5] = pc_maxparameter(sd,PARAM_LUK);
		}
	}

	count = 0;
	for (i = 0; i < ARRAYLENGTH(status); i++) {
		short new_value;

		if (value > 0 && *status[i] + value >= max_status[i])
			new_value = max_status[i];
		else if (value < 0 && *status[i] <= -value)
			new_value = 1;
		else
			new_value = *status[i] + value;

		if (new_value != *status[i]) {
			*status[i] = new_value;
			clif_updatestatus(sd, SP_STR + i);
			clif_updatestatus(sd, SP_USTR + i);
			achievement_update_objective(sd, AG_GOAL_STATUS, 1, new_value);
			count++;
		}
	}

	if (count > 0) { // If at least 1 stat modified
		status_calc_pc(sd, SCO_FORCE);
		clif_displaymessage(fd, msg_txt(84)); // All stats changed!
	} else {
		if (value < 0)
			clif_displaymessage(fd, msg_txt(177)); // You cannot decrease that stat anymore.
		else
			clif_displaymessage(fd, msg_txt(178)); // You cannot increase that stat anymore.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(guildlevelup) {
	int level = 0;
	short added_level;
	struct guild *guild_info;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d", &level) < 1 || level == 0) {
		clif_displaymessage(fd, msg_txt(1014)); // Please enter a valid level (usage: @guildlvup/@guildlvlup <# of levels>).
		return -1;
	}

	if (sd->status.guild_id <= 0 || (guild_info = sd->guild) == NULL) {
		clif_displaymessage(fd, msg_txt(43)); // You're not in a guild.
		return -1;
	}
	//if (strcmp(sd->status.name, guild_info->master) != 0) {
	//	clif_displaymessage(fd, msg_txt(44)); // You're not the master of your guild.
	//	return -1;
	//}

	added_level = (short)level;
	if (level > 0 && (level > MAX_GUILDLEVEL || added_level > ((short)MAX_GUILDLEVEL - guild_info->guild_lv))) // Fix positive overflow
		added_level = (short)MAX_GUILDLEVEL - guild_info->guild_lv;
	else if (level < 0 && (level < -MAX_GUILDLEVEL || added_level < (1 - guild_info->guild_lv))) // Fix negative overflow
		added_level = 1 - guild_info->guild_lv;

	if (added_level != 0) {
		intif_guild_change_basicinfo(guild_info->guild_id, GBI_GUILDLV, &added_level, sizeof(added_level));
		clif_displaymessage(fd, msg_txt(179)); // Guild level changed.
	} else {
		clif_displaymessage(fd, msg_txt(45)); // Guild level change failed.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(makeegg) {
	struct item_data *item_data;
	int id, pet_id;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1015)); // Please enter a monster/egg name/ID (usage: @makeegg <pet>).
		return -1;
	}

	if ((item_data = itemdb_searchname(message)) != NULL) // For egg name
		id = item_data->nameid;
	else if ((id = mobdb_searchname(message)) != 0) // For monster name
		;
	else
		id = atoi(message);

	pet_id = search_petDB_index(id, PET_CLASS);
	if (pet_id < 0)
		pet_id = search_petDB_index(id, PET_EGG);
	if (pet_id >= 0) {
		sd->catch_target_class = pet_db[pet_id].class_;
		intif_create_pet(sd->status.account_id, sd->status.char_id, pet_db[pet_id].class_, mob_db(pet_db[pet_id].class_)->lv, pet_db[pet_id].EggID, 0, pet_db[pet_id].intimate, 100, 0, 1, pet_db[pet_id].jname);
	} else {
		clif_displaymessage(fd, msg_txt(180)); // The monster/egg name/id doesn't exist.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(hatch) {
	nullpo_retr(-1, sd);
	if (sd->status.pet_id <= 0)
		clif_sendegg(sd);
	else {
		clif_displaymessage(fd, msg_txt(181)); // You already have a pet.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(petfriendly) {
	int friendly;
	struct pet_data *pd;
	nullpo_retr(-1, sd);

	if (!message || !*message || (friendly = atoi(message)) < 0) {
		clif_displaymessage(fd, msg_txt(1016)); // Please enter a valid value (usage: @petfriendly <0-1000>).
		return -1;
	}

	pd = sd->pd;
	if (!pd) {
		clif_displaymessage(fd, msg_txt(184)); // Sorry, but you have no pet.
		return -1;
	}
	
	if (friendly < 0 || friendly > 1000) {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}
	
	if (friendly == pd->pet.intimate) {
		clif_displaymessage(fd, msg_txt(183)); // Pet intimacy is already at maximum.
		return -1;
	}

	pet_set_intimate(pd, friendly);
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(182)); // Pet intimacy changed.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(pethungry)
{
	int hungry;
	struct pet_data *pd;
	nullpo_retr(-1, sd);

	if (!message || !*message || (hungry = atoi(message)) < 0) {
		clif_displaymessage(fd, msg_txt(1017)); // Please enter a valid number (usage: @pethungry <0-100>).
		return -1;
	}

	pd = sd->pd;
	if (!sd->status.pet_id || !pd) {
		clif_displaymessage(fd, msg_txt(184)); // Sorry, but you have no pet.
		return -1;
	}
	if (hungry < 0 || hungry > 100) {
		clif_displaymessage(fd, msg_txt(37)); // An invalid number was specified.
		return -1;
	}
	if (hungry == pd->pet.hungry) {
		clif_displaymessage(fd, msg_txt(186)); // Pet hunger is already at maximum.
		return -1;
	}

	pd->pet.hungry = hungry;
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(185)); // Pet hunger changed.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(petrename)
{
	struct pet_data *pd;
	nullpo_retr(-1, sd);
	if (!sd->status.pet_id || !sd->pd) {
		clif_displaymessage(fd, msg_txt(184)); // Sorry, but you have no pet.
		return -1;
	}
	pd = sd->pd;
	if (!pd->pet.rename_flag) {
		clif_displaymessage(fd, msg_txt(188)); // You can already rename your pet.
		return -1;
	}

	pd->pet.rename_flag = 0;
	intif_save_petdata(sd->status.account_id, &pd->pet);
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(187)); // You can now rename your pet.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(recall) {
	struct map_session_data *pl_sd = NULL;

	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(1018)); // Please enter a player name (usage: @recall <char name/ID>).
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL && (pl_sd = map_charid2sd(atoi(atcmd_player_name))) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) {
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
		return -1;
	}

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(1019)); // You are not authorized to warp someone to this map.
		return -1;
	}

	if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(1020)); // You are not authorized to warp this player from their map.
		return -1;
	}

	if (pl_sd->bl.m == sd->bl.m && pl_sd->bl.x == sd->bl.x && pl_sd->bl.y == sd->bl.y)
		return -1;

	if (pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN) == SETPOS_AUTOTRADE) {
		clif_displaymessage(fd, msg_txt(544)); // The player cannot be recalled, because he is in autotrading state.
		return -1;
	}

	sprintf(atcmd_output, msg_txt(46), pl_sd->status.name); // %s recalled!
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * charblock command (usage: block <player_name>)
 * This command do a definitiv ban on a player
 *------------------------------------------*/
ACMD_FUNC(char_block)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		sprintf(atcmd_output, msg_txt(1021), command); // Please enter a player name (usage: %s <char name>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	chrif_req_login_operation(sd->status.account_id, atcmd_player_name, CHRIF_OP_LOGIN_BLOCK, 0, 0);
	sprintf(atcmd_output, msg_txt(88), "login"); // Sending request to %s server...
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * accountban command (usage: ban <%time> <player_name>)
 * charban command (usage: charban <%time> <player_name>)
 * %time see common/timer.c::solve_time()
 *------------------------------------------*/
ACMD_FUNC(char_ban)
{
	char *modif_p, output[CHAT_SIZE_MAX];
	int32 timediff = 0; // Don't set this as uint as we may want to decrease banned time
	enum chrif_req_op bantype;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	parent_cmd = atcommand_checkalias(command + 1);

	if (!strcmpi(parent_cmd, "charban"))
		bantype = CHRIF_OP_BAN;
	else if (!strcmpi(parent_cmd, "ban"))
		bantype = CHRIF_OP_LOGIN_BAN;
	else
		return -1;

	if (!message || !*message || sscanf(message, "%255s %23[^\n]", atcmd_output, atcmd_player_name) < 2) {
		sprintf(output, msg_txt(1022), command); // Please enter ban time and a player name (usage: %s <time> <char name>).
		clif_displaymessage(fd, output);
		return -1;
	}

	atcmd_output[sizeof(atcmd_output) - 1] = '\0';

	modif_p = atcmd_output;
	timediff = (int32)solve_time(modif_p); // Discard seconds

	if (!timediff) { // Allow negative?
		safesnprintf(output, sizeof(output), msg_txt(85), command, timediff); // Invalid time for %s command (time=%d)
		clif_displaymessage(fd, output);
		clif_displaymessage(fd, msg_txt(702)); // Time parameter format is <+/-value> to alter. y/a = Year, m = Month, d/j = Day, h = Hour, n/mn = Minute, s = Second.
		return -1;
	}

	if (timediff < 0 && ((bantype == CHRIF_OP_BAN && !pc_can_use_command(sd, "charunban", COMMAND_ATCOMMAND)) ||
		(bantype == CHRIF_OP_LOGIN_BAN && !pc_can_use_command(sd, "unban", COMMAND_ATCOMMAND)))) {
		clif_displaymessage(fd, msg_txt(1023)); // You are not allowed to alter the time of a ban.
		return -1;
	}

	if (bantype == CHRIF_OP_BAN)
		chrif_req_charban(sd->status.account_id, atcmd_player_name, timediff);
	else
		chrif_req_login_operation(sd->status.account_id, atcmd_player_name, bantype, timediff, 0);

	safesnprintf(output, sizeof(output), msg_txt(88), (bantype == CHRIF_OP_BAN ? "char" : "login")); // Sending request to %s server...
	clif_displaymessage(fd, output);

	return 0;
}

/*==========================================
 * charunblock command (usage: unblock <player_name>)
 *------------------------------------------*/
ACMD_FUNC(char_unblock)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		sprintf(atcmd_output, msg_txt(1021), command); // Please enter a player name (usage: %s <char name>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	// Send answer to login server via char-server
	chrif_req_login_operation(sd->status.account_id, atcmd_player_name, CHRIF_OP_LOGIN_UNBLOCK, 0, 0);
	sprintf(atcmd_output, msg_txt(88), "login"); // Sending request to %s server...
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * acc unban command (usage: unban <player_name>)
 * char unban command (usage: charunban <player_name>)
 *------------------------------------------*/
ACMD_FUNC(char_unban) {
	enum chrif_req_op unbantype;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	parent_cmd = atcommand_checkalias(command + 1);

	if (strcmpi(parent_cmd, "charunban") == 0)
		unbantype = CHRIF_OP_UNBAN;
	else if (strcmpi(parent_cmd, "unban") == 0)
		unbantype = CHRIF_OP_LOGIN_UNBAN;
	else
		return -1;

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		sprintf(atcmd_output, msg_txt(435), command); // Please enter a player name (usage: %s <char name>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (unbantype == CHRIF_OP_UNBAN)
		chrif_req_charunban(sd->status.account_id, atcmd_player_name);
	else
		chrif_req_login_operation(sd->status.account_id, atcmd_player_name, unbantype, 0, 0);

	sprintf(atcmd_output, msg_txt(88), (unbantype == CHRIF_OP_UNBAN ? "char" : "login")); // Sending request to %s server...
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(night)
{
	nullpo_retr(-1, sd);

	if (night_flag != 1) {
		map_night_timer(night_timer_tid, 0, 0, 1);
	} else {
		clif_displaymessage(fd, msg_txt(89)); // Night mode is already enabled.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(day)
{
	nullpo_retr(-1, sd);

	if (night_flag != 0) {
		map_day_timer(day_timer_tid, 0, 0, 1);
	} else {
		clif_displaymessage(fd, msg_txt(90)); // Day mode is already enabled.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(doom)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
		if (pl_sd->fd != fd && pc_get_group_level(sd) >= pc_get_group_level(pl_sd)) {
			status_kill(&pl_sd->bl);
			clif_specialeffect(&pl_sd->bl,EF_GRANDCROSS2,AREA);
			clif_displaymessage(pl_sd->fd, msg_txt(61)); // The holy messenger has given judgement.
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(62)); // Judgement was made.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(doommap)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
		if (pl_sd->fd != fd && sd->bl.m == pl_sd->bl.m && pc_get_group_level(sd) >= pc_get_group_level(pl_sd)) {
			status_kill(&pl_sd->bl);
			clif_specialeffect(&pl_sd->bl,EF_GRANDCROSS2,AREA);
			clif_displaymessage(pl_sd->fd, msg_txt(61)); // The holy messenger has given judgement.
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(62)); // Judgement was made.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static void atcommand_raise_sub(struct map_session_data *sd) {
	
	status_revive(&sd->bl, 100, 100);

	clif_skill_nodamage(&sd->bl,&sd->bl,ALL_RESURRECTION,4,1);
	clif_displaymessage(sd->fd, msg_txt(63)); // Mercy has been shown.
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(raise)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
		
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
		if( pc_isdead(pl_sd) )
			atcommand_raise_sub(pl_sd);
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(64)); // Mercy has been granted.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(raisemap)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) )
		if (sd->bl.m == pl_sd->bl.m && pc_isdead(pl_sd) )
			atcommand_raise_sub(pl_sd);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(64)); // Mercy has been granted.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(kick)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(1026)); // Please enter a player name (usage: @kick <char name/ID>).
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL && (pl_sd = map_charid2sd(atoi(atcmd_player_name))) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) {
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
		return -1;
	}

	clif_GM_kick(sd, pl_sd);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(kickall)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
		if (pc_get_group_level(sd) >= pc_get_group_level(pl_sd)) // You can kick only lower or same gm level
			if (sd->status.account_id != pl_sd->status.account_id)
				clif_GM_kick(NULL, pl_sd);
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(195)); // All players have been kicked!

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(allskill)
{
	nullpo_retr(-1, sd);
	pc_allskillup(sd); // all skills
	sd->status.skill_point = 0; // 0 skill points
	clif_updatestatus(sd, SP_SKILLPOINT); // update
	clif_displaymessage(fd, msg_txt(76)); // All skills have been added to your skill tree.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(questskill)
{
	uint16 skill_id;
	nullpo_retr(-1, sd);

	if (!message || !*message || (skill_id = atoi(message)) <= 0) {
		// Also send a list of skills applicable to this command
		const char *text;

		// Attempt to find the text corresponding to this command
		text = atcommand_help_string(command);

		// Send the error message as always
		clif_displaymessage(fd, msg_txt(1027)); // Please enter a quest skill number.

		if (text) // Send the skill ID list associated with this command
			clif_displaymessage(fd, text);

		return -1;
	}
	if (skill_id >= MAX_SKILL_DB) {
		clif_displaymessage(fd, msg_txt(198)); // This skill number doesn't exist.
		return -1;
	}
	if (!(skill_get_inf2(skill_id)&INF2_QUEST_SKILL)) {
		clif_displaymessage(fd, msg_txt(197)); // This skill number doesn't exist or isn't a quest skill.
		return -1;
	}
	if (pc_checkskill(sd, skill_id) > 0) {
		clif_displaymessage(fd, msg_txt(196)); // You already have this quest skill.
		return -1;
	}

	pc_skill(sd, skill_id, 1, 0);
	clif_displaymessage(fd, msg_txt(70)); // You have learned the skill.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(lostskill)
{
	uint16 skill_id;
	nullpo_retr(-1, sd);

	if (!message || !*message || (skill_id = atoi(message)) <= 0) { // also send a list of skills applicable to this command
		const char *text;

		// Attempt to find the text corresponding to this command
		text = atcommand_help_string(command);
		
		// Send the error message as always
		clif_displaymessage(fd, msg_txt(1027)); // Please enter a quest skill number.

		if (text) // Send the skill ID list associated with this command
			clif_displaymessage(fd, text);

		return -1;
	}
	if (skill_id >= MAX_SKILL) {
		clif_displaymessage(fd, msg_txt(198)); // This skill number doesn't exist.
		return -1;
	}
	if (!(skill_get_inf2(skill_id)&INF2_QUEST_SKILL)) {
		clif_displaymessage(fd, msg_txt(197)); // This skill number doesn't exist or isn't a quest skill.
		return -1;
	}
	if (!pc_checkskill(sd, skill_id)) {
		clif_displaymessage(fd, msg_txt(201)); // You don't have this quest skill.
		return -1;
	}

	sd->status.skill[skill_id].lv = 0;
	sd->status.skill[skill_id].flag = SKILL_FLAG_PERMANENT;
	clif_deleteskill(sd,skill_id);
	clif_displaymessage(fd, msg_txt(71)); // You have forgotten the skill.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(spiritball)
{
	uint32 max_spiritballs;
	int number;
	nullpo_retr(-1, sd);

	max_spiritballs = zmin(ARRAYLENGTH(sd->spiritball_timer), 0x7FFF);

	if( !message || !*message || (number = atoi(message)) < 0 || number > max_spiritballs ) {
		char msg[CHAT_SIZE_MAX];

		safesnprintf(msg, sizeof(msg), msg_txt(1028), max_spiritballs); // Please enter a number (usage: @spiritball <number: 0-%d>).
		clif_displaymessage(fd, msg);
		return -1;
	}

	if( sd->spiritball > 0 )
		pc_delspiritball(sd, sd->spiritball, 1);
	sd->spiritball = number;
	clif_spiritball(&sd->bl);
	// No message, player can look the difference

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(shieldball)
{
	uint32 max_shieldballs;
	int number, health;
	nullpo_retr(-1, sd);

	max_shieldballs = zmin(ARRAYLENGTH(sd->shieldball_timer), 0x7FFF);

	if( !message || !*message || sscanf(message, "%d %d", &number, &health) < 1 ||
		number > max_shieldballs || health < 1 || health > 1000000000 ) {
		char msg[CHAT_SIZE_MAX];

		safesnprintf(msg, sizeof(msg), msg_txt(1057), max_shieldballs); // Please enter a number (usage: @shieldball <number: 0-%d> <health: 1-1000000000>).
		clif_displaymessage(fd, msg);
		return -1;
	}

	if( sd->shieldball > 0 )
		pc_delshieldball(sd, sd->shieldball, 1);
	sd->shieldball = number;
	sd->shieldball_health = sd->shieldball_set_health = health;
	if( sd->shieldball > 0 )
		sc_start(&sd->bl, &sd->bl, SC_MILLENNIUMSHIELD, 100, 0, -1);
	clif_millenniumshield(&sd->bl, sd->shieldball);
	// No message, player can look the difference

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(rageball)
{
	uint32 max_rageballs;
	int number;
	nullpo_retr(-1, sd);

	max_rageballs = zmin(ARRAYLENGTH(sd->rageball_timer), 0x7FFF);

	if( !message || !*message || (number = atoi(message)) < 0 || number > max_rageballs ) {
		char msg[CHAT_SIZE_MAX];

		safesnprintf(msg, sizeof(msg), msg_txt(1025), max_rageballs); // Please enter a number (usage: @rageball <number: 0-%d>).
		clif_displaymessage(fd, msg);
		return -1;
	}

	if( sd->rageball > 0 )
		pc_delrageball(sd, sd->rageball, 1);
	sd->rageball = number;
	clif_millenniumshield(&sd->bl, sd->rageball);
	// No message, player can look the difference

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(charmball)
{
	uint32 max_charmballs;
	int number, type;
	nullpo_retr(-1, sd);

	max_charmballs = zmin(ARRAYLENGTH(sd->charmball_timer), 0x7FFF);

	if( !message || !*message || sscanf(message, "%d %d", &number, &type) < 1 ||
		number > max_charmballs || type < CHARM_TYPE_WATER || type > CHARM_TYPE_WIND ) {
		char msg[CHAT_SIZE_MAX];

		safesnprintf(msg, sizeof(msg), msg_txt(1055), max_charmballs); // Please enter a number (usage: @charmball <number: 0-%d> <charm type>).
		clif_displaymessage(fd, msg);
		clif_displaymessage(fd, msg_txt(1056)); // Charm Types: 1: Water, 2: Earth, 3: Fire, 4: Wind
		return -1;
	}

	if( sd->charmball > 0 )
		pc_delcharmball(sd, sd->charmball, sd->charmball_type);
	sd->charmball = number;
	sd->charmball_type = type;
	clif_charmball(sd);
	// No message, player can look the difference

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(party)
{
	char party[NAME_LENGTH];
	nullpo_retr(-1, sd);

	memset(party, '\0', sizeof(party));

	if (!message || !*message || sscanf(message, "%23[^\n]", party) < 1) {
		clif_displaymessage(fd, msg_txt(1029)); // Please enter a party name (usage: @party <party_name>).
		return -1;
	}

	party_create(sd, party, 0, 0);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(guild)
{
	char guild[NAME_LENGTH];
	int prev;
	nullpo_retr(-1, sd);

	memset(guild, '\0', sizeof(guild));

	if (sd->clan) {
		clif_displaymessage(fd, msg_txt(733)); // You cannot create a guild because you are in a clan.
		return -1;
	}

	if (!message || !*message || sscanf(message, "%23[^\n]", guild) < 1) {
		clif_displaymessage(fd, msg_txt(1030)); // Please enter a guild name (usage: @guild <guild_name>).
		return -1;
	}

	prev = battle_config.guild_emperium_check;
	battle_config.guild_emperium_check = 0;
	guild_create(sd, guild);
	battle_config.guild_emperium_check = prev;

	return 0;
}

ACMD_FUNC(breakguild)
{
	nullpo_retr(-1, sd);

	if (sd->status.guild_id) { // Check if the player has a guild
		struct guild *g = sd->guild; // Search the guild

		if (g) { // Check if guild was found
			if (sd->state.gmaster_flag) { // Check if player is guild master
				int ret = guild_break(sd, g->name); // Break guild

				if (ret) // Check if anything went wrong
					return 0; // Guild was broken
				else
					return -1; // Something went wrong
			} else { // Not guild master
				clif_displaymessage(fd, msg_txt(1181)); // You need to be a Guild Master to use this command.
				return -1;
			}
		} else { // Guild was not found. HOW?
			clif_displaymessage(fd, msg_txt(252)); // You are not in a guild.
			return -1;
		}
	} else { // Player does not have a guild
		clif_displaymessage(fd, msg_txt(252)); // You are not in a guild.
		return -1;
	}
}

/**
 * Start WoE:FE
 */
ACMD_FUNC(agitstart)
{
	nullpo_retr(-1, sd);

	if (guild_agit_start()) {
		clif_displaymessage(fd, msg_txt(72)); // War of Emperium has been initiated.
		return 0;
	} else {
		clif_displaymessage(fd, msg_txt(73)); // War of Emperium is currently in progress.
		return -1;
	}
}

/**
 * Start WoE:SE
 */
ACMD_FUNC(agitstart2)
{
	nullpo_retr(-1, sd);

	if (guild_agit2_start()) {
		clif_displaymessage(fd, msg_txt(403)); // War of Emperium SE has been initiated.
		return 0;
	} else {
		clif_displaymessage(fd, msg_txt(404)); // War of Emperium SE is currently in progress.
		return -1;
	}
}

/**
 * Start WoE:TE
 */
ACMD_FUNC(agitstart3)
{
	nullpo_retr(-1, sd);

	if (guild_agit3_start()) {
		clif_displaymessage(fd, msg_txt(741)); // War of Emperium TE has been initiated.
		return 0;
	} else {
		clif_displaymessage(fd, msg_txt(742)); // War of Emperium TE is currently in progress.
		return -1;
	}
}

/**
 * End WoE:FE
 */
ACMD_FUNC(agitend)
{
	nullpo_retr(-1, sd);

	if (guild_agit_end()) {
		clif_displaymessage(fd, msg_txt(74)); // War of Emperium has been ended.
		return 0;
	} else {
		clif_displaymessage(fd, msg_txt(75)); // War of Emperium is currently not in progress.
		return -1;
	}
}

/**
 * End WoE:SE
 */
ACMD_FUNC(agitend2)
{
	nullpo_retr(-1, sd);

	if (guild_agit2_end()) {
		clif_displaymessage(fd, msg_txt(405)); // War of Emperium SE has been ended.
		return 0;
	} else {
		clif_displaymessage(fd, msg_txt(406)); // War of Emperium SE is currently not in progress.
		return -1;
	}
}

/**
 * End WoE:TE
 */
ACMD_FUNC(agitend3)
{
	nullpo_retr(-1, sd);

	if (guild_agit3_end()) {
		clif_displaymessage(fd, msg_txt(743)); // War of Emperium TE has been ended.
		return 0;
	} else {
		clif_displaymessage(fd, msg_txt(744)); // War of Emperium TE is currently not in progress.
		return -1;
	}
}

/*==========================================
 * @mapexit - shuts down the map server
 *------------------------------------------*/
ACMD_FUNC(mapexit)
{
	nullpo_retr(-1, sd);

	do_shutdown();
	return 0;
}

/*==========================================
 * idsearch <part_of_name>: revrited by [Yor]
 *------------------------------------------*/
ACMD_FUNC(idsearch)
{
	char item_name[100];
	unsigned int i, match;
	struct item_data *item_array[MAX_SEARCH];
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%99s", item_name) < 0) {
		clif_displaymessage(fd, msg_txt(1031)); // Please enter part of an item name (usage: @idsearch <part_of_item_name>).
		return -1;
	}

	sprintf(atcmd_output, msg_txt(77), item_name); // The reference result of '%s' (name: id):
	clif_displaymessage(fd, atcmd_output);
	match = itemdb_searchname_array(item_array, MAX_SEARCH, item_name);
	if (match == MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH); // Displaying first %d matches
		clif_displaymessage(fd, atcmd_output);
	}
	for(i = 0; i < match; i++) {
		sprintf(atcmd_output, msg_txt(78), item_array[i]->jname, item_array[i]->nameid); // %s: %d
		clif_displaymessage(fd, atcmd_output);
	}
	sprintf(atcmd_output, msg_txt(79), match); // It is %d affair above.
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * Recall All Characters Online To Your Location
 *------------------------------------------*/
ACMD_FUNC(recallall)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
	int count;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(1032)); // You are not authorized to warp someone to your current map.
		return -1;
	}

	count = 0;
	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
		if (sd->status.account_id != pl_sd->status.account_id && pc_get_group_level(sd) >= pc_get_group_level(pl_sd)) {
			if (pl_sd->bl.m == sd->bl.m && pl_sd->bl.x == sd->bl.x && pl_sd->bl.y == sd->bl.y)
				continue; // Don't waste time warping the character to the same place.
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE))
				count++;
			else if (pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN) == SETPOS_AUTOTRADE)
				count++;
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(92)); // All characters recalled!
	if (count) {
		sprintf(atcmd_output, msg_txt(1033), count); // Because you are not authorized to warp from some maps, %d player(s) have not been recalled.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Recall online characters of a guild to your location
 *------------------------------------------*/
ACMD_FUNC(guildrecall)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
	int count;
	char guild_name[NAME_LENGTH];
	struct guild *g;
	nullpo_retr(-1, sd);

	memset(guild_name, '\0', sizeof(guild_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%23[^\n]", guild_name) < 1) {
		clif_displaymessage(fd, msg_txt(1034)); // Please enter a guild name/ID (usage: @guildrecall <guild_name/ID>).
		return -1;
	}

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(1032)); // You are not authorized to warp someone to your current map.
		return -1;
	}

	if (!(g = guild_searchname(guild_name)) && // Name first to avoid error when name begin with a number
	    !(g = guild_search(atoi(message))))
	{
		clif_displaymessage(fd, msg_txt(94)); // Incorrect name/ID, or no one from the guild is online.
		return -1;
	}

	count = 0;

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
		if (sd->status.account_id != pl_sd->status.account_id && pl_sd->status.guild_id == g->guild_id) {
			if (pc_get_group_level(pl_sd) > pc_get_group_level(sd) || (pl_sd->bl.m == sd->bl.m && pl_sd->bl.x == sd->bl.x && pl_sd->bl.y == sd->bl.y))
				continue; //Skip GMs greater than you.
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE))
				count++;
			else if (pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN) == SETPOS_AUTOTRADE)
				count++;
		}
	}
	mapit_free(iter);

	sprintf(atcmd_output, msg_txt(93), g->name); // All online characters of the %s guild have been recalled to your position.
	clif_displaymessage(fd, atcmd_output);
	if (count) {
		sprintf(atcmd_output, msg_txt(1033), count); // Because you are not authorized to warp from some maps, %d player(s) have not been recalled.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Recall online characters of a party to your location
 *------------------------------------------*/
ACMD_FUNC(partyrecall)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
	char party_name[NAME_LENGTH];
	struct party_data *p;
	int count;
	nullpo_retr(-1, sd);

	memset(party_name, '\0', sizeof(party_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%23[^\n]", party_name) < 1) {
		clif_displaymessage(fd, msg_txt(1035)); // Please enter a party name/ID (usage: @partyrecall <party_name/ID>).
		return -1;
	}

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE)) {
		clif_displaymessage(fd, msg_txt(1032)); // You are not authorized to warp someone to your current map.
		return -1;
	}

	if (!(p = party_searchname(party_name)) && // Name first to avoid error when name begin with a number
	    !(p = party_search(atoi(message))))
	{
		clif_displaymessage(fd, msg_txt(96)); // Incorrect name or ID, or no one from the party is online.
		return -1;
	}

	count = 0;

	iter = mapit_getallusers();
	for (pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter)) {
		if (sd->status.account_id != pl_sd->status.account_id && pl_sd->status.party_id == p->party.party_id) {
			if (pc_get_group_level(pl_sd) > pc_get_group_level(sd) || (pl_sd->bl.m == sd->bl.m && pl_sd->bl.x == sd->bl.x && pl_sd->bl.y == sd->bl.y))
				continue; //Skip GMs greater than you.
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && !pc_has_permission(sd, PC_PERM_WARP_ANYWHERE))
				count++;
			else if (pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN) == SETPOS_AUTOTRADE)
				count++;
		}
	}
	mapit_free(iter);

	sprintf(atcmd_output, msg_txt(95), p->party.name); // All online characters of the %s party have been recalled to your position.
	clif_displaymessage(fd, atcmd_output);
	if (count) {
		sprintf(atcmd_output, msg_txt(1033), count); // Because you are not authorized to warp from some maps, %d player(s) have not been recalled.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
void atcommand_doload();
ACMD_FUNC(reload)
{

	nullpo_retr(-1, sd);

	if ((strlen(command) < 8) && (!message || !*message)) {
		const char *text;

		text = atcommand_help_string(command);
		if (text)
			clif_displaymessage(fd, text);
		return -1;
	}

	if (strstr(command, "itemdb") || strncmp(message, "itemdb", 4) == 0) {
		itemdb_reload();
		clif_displaymessage(fd, msg_txt(97)); // Item database has been reloaded.
	} else if (strstr(command, "mobdb") || strncmp(message, "mobdb", 3) == 0) {
		mob_reload();
		pet_readdb();
		hom_reload();
		mercenary_readdb();
		mercenary_read_skilldb();
		reload_elementaldb();
		clif_displaymessage(fd, msg_txt(98)); // Monster database has been reloaded.
	} else if (strstr(command, "skilldb") || strncmp(message, "skilldb", 4) == 0) {
		skill_reload();
		hom_reload_skill();
		reload_elemental_skilldb();
		mercenary_read_skilldb();
		clif_displaymessage(fd, msg_txt(99)); // Skill database has been reloaded.
	} else if (strstr(command, "atcommand") || strncmp(message, "atcommand", 4) == 0) {
		struct config_t run_test;

		if (conf_read_file(&run_test, "conf/groups.conf")) {
			clif_displaymessage(fd, msg_txt(1036)); // Error reading groups.conf, reload failed.
			return -1;
		}

		config_destroy(&run_test);

		if (conf_read_file(&run_test, ATCOMMAND_CONF_FILENAME)) {
			clif_displaymessage(fd, msg_txt(1037)); // Error reading atcommand_athena.conf, reload failed.
			return -1;
		}

		config_destroy(&run_test);

		atcommand_doload();
		pc_groups_reload();
		clif_displaymessage(fd, msg_txt(254)); // GM command configuration has been reloaded.
	} else if (strstr(command, "battleconf") || strncmp(message, "battleconf", 3) == 0) {
		struct Battle_Config prev_config;
		memcpy(&prev_config, &battle_config, sizeof(prev_config));

		battle_config_read(BATTLE_CONF_FILENAME);

		if( prev_config.item_rate_mvp          != battle_config.item_rate_mvp
		||  prev_config.item_rate_common       != battle_config.item_rate_common
		||  prev_config.item_rate_common_boss  != battle_config.item_rate_common_boss
		||  prev_config.item_rate_common_mvp   != battle_config.item_rate_common_mvp
		||  prev_config.item_rate_card         != battle_config.item_rate_card
		||  prev_config.item_rate_card_boss    != battle_config.item_rate_card_boss
		||  prev_config.item_rate_card_mvp     != battle_config.item_rate_card_mvp
		||  prev_config.item_rate_equip        != battle_config.item_rate_equip
		||  prev_config.item_rate_equip_boss   != battle_config.item_rate_equip_boss
		||  prev_config.item_rate_equip_mvp    != battle_config.item_rate_equip_mvp
		||  prev_config.item_rate_heal         != battle_config.item_rate_heal
		||  prev_config.item_rate_heal_boss    != battle_config.item_rate_heal_boss
		||  prev_config.item_rate_heal_mvp     != battle_config.item_rate_heal_mvp
		||  prev_config.item_rate_use          != battle_config.item_rate_use
		||  prev_config.item_rate_use_boss     != battle_config.item_rate_use_boss
		||  prev_config.item_rate_use_mvp      != battle_config.item_rate_use_mvp
		||  prev_config.item_rate_treasure     != battle_config.item_rate_treasure
		||  prev_config.item_rate_adddrop      != battle_config.item_rate_adddrop
		||  prev_config.logarithmic_drops      != battle_config.logarithmic_drops
		||  prev_config.item_drop_common_min   != battle_config.item_drop_common_min
		||  prev_config.item_drop_common_max   != battle_config.item_drop_common_max
		||  prev_config.item_drop_card_min     != battle_config.item_drop_card_min
		||  prev_config.item_drop_card_max     != battle_config.item_drop_card_max
		||  prev_config.item_drop_equip_min    != battle_config.item_drop_equip_min
		||  prev_config.item_drop_equip_max    != battle_config.item_drop_equip_max
		||  prev_config.item_drop_mvp_min      != battle_config.item_drop_mvp_min
		||  prev_config.item_drop_mvp_max      != battle_config.item_drop_mvp_max
		||  prev_config.item_drop_heal_min     != battle_config.item_drop_heal_min
		||  prev_config.item_drop_heal_max     != battle_config.item_drop_heal_max
		||  prev_config.item_drop_use_min      != battle_config.item_drop_use_min
		||  prev_config.item_drop_use_max      != battle_config.item_drop_use_max
		||  prev_config.item_drop_treasure_min != battle_config.item_drop_treasure_min
		||  prev_config.item_drop_treasure_max != battle_config.item_drop_treasure_max
		||  prev_config.base_exp_rate          != battle_config.base_exp_rate
		||  prev_config.job_exp_rate           != battle_config.job_exp_rate
		)
		{	// Exp or Drop rates changed.
			mob_reload(); //Needed as well so rate changes take effect.
			chrif_ragsrvinfo(battle_config.base_exp_rate, battle_config.job_exp_rate, battle_config.item_rate_common);
		}
		clif_displaymessage(fd, msg_txt(255)); // Battle configuration has been reloaded.
	} else if (strstr(command, "statusdb") || strncmp(message, "statusdb", 3) == 0) {
		status_readdb();
		clif_displaymessage(fd, msg_txt(256)); // Status database has been reloaded.
	} else if (strstr(command, "pcdb") || strncmp(message, "pcdb", 2) == 0) {
		pc_readdb();
		clif_displaymessage(fd, msg_txt(257)); // Player database has been reloaded.
	} else if (strstr(command, "motd") || strncmp(message, "motd", 4) == 0) {
		pc_read_motd();
		clif_displaymessage(fd, msg_txt(268)); // Reloaded the Message of the Day.
	} else if (strstr(command, "script") || strncmp(message, "script", 3) == 0) {
		struct s_mapiterator *iter;
		struct map_session_data *pl_sd;
		//atcommand_broadcast( fd, sd, "@broadcast", "Server is reloading scripts..." );
		//atcommand_broadcast( fd, sd, "@broadcast", "You will feel a bit of lag at this point !" );

		iter = mapit_getallusers();
		for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) )
			pc_close_npc(pl_sd,2);
		mapit_free(iter);

		flush_fifos();
		map_reloadnpc(true); // reload config files seeking for npcs
		script_reload();
		npc_reload();

		clif_displaymessage(fd, msg_txt(100)); // Scripts have been reloaded.
	} else if (strstr(command, "questdb") || strncmp(message, "questdb", 3) == 0) {
		do_reload_quest();
		clif_displaymessage(fd, msg_txt(1377)); // Quest database has been reloaded.
	} else if (strstr(command, "instancedb") || strncmp(message, "instancedb", 4) == 0) {
		instance_reload();
		clif_displaymessage(fd, msg_txt(516)); // Instance database has been reloaded.
	} else if (strstr(command, "achievementdb") || strncmp(message, "achievementdb", 4) == 0) {
		achievement_db_reload();
		clif_displaymessage(fd, msg_txt(1024)); // Achievement database has been reloaded.
	}

	return 0;
}

/*==========================================
 * @partysharelvl <share_range> [Akinari]
 * Updates char server party share level range in runtime
 * Temporary - Permanent update in inter_athena.conf
 *------------------------------------------*/
ACMD_FUNC(partysharelvl) {
	unsigned int share_lvl;

	nullpo_retr(-1, sd);

	if(!message || !*message) {
		clif_displaymessage(fd, msg_txt(1322)); // Please enter an amount.
		return -1;
	} else
		share_lvl = min(abs(atoi(message)),MAX_LEVEL);

	if(intif_party_sharelvlupdate(share_lvl)) // Successfully updated
		clif_displaymessage(fd, msg_txt(1479)); // Party share level range has been changed successfully.
	else // Char server offline
		clif_displaymessage(fd, msg_txt(1480)); // Failed updating configuration. Character server offline.

	return 0;
}

/*==========================================
 * @mapinfo [0-3] <map name> by MC_Cameri
 * => Shows information about the map [map name]
 * 0 = no additional information
 * 1 = Show users in that map and their location
 * 2 = Shows NPCs in that map
 * 3 = Shows the chats in that map
 *------------------------------------------*/
ACMD_FUNC(mapinfo)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
	struct chat_data *cd = NULL;
	char direction[12];
	int i, m_id, chat_num = 0, list = 0, vend_num = 0;
	unsigned short m_index;
	char mapname[24];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(mapname, '\0', sizeof(mapname));
	memset(direction, '\0', sizeof(direction));

	sscanf(message, "%d %23[^\n]", &list, mapname);

	if (list < 0 || list > 3) {
		clif_displaymessage(fd, msg_txt(1038)); // Please enter at least one valid list number (usage: @mapinfo <0-3> <map>).
		return -1;
	}

	if (mapname[0] == '\0') {
		safestrncpy(mapname, mapindex_id2name(sd->mapindex), MAP_NAME_LENGTH);
		m_id =  map_mapindex2mapid(sd->mapindex);
	} else
		m_id = map_mapname2mapid(mapname);

	if (m_id < 0) {
		clif_displaymessage(fd, msg_txt(1)); // Map not found.
		return -1;
	}
	m_index = mapindex_name2id(mapname); // This one shouldn't fail since the previous seek did not.
	
	clif_displaymessage(fd, msg_txt(1039)); // ------ Map Info ------

	// Count chats (for initial message)
	chat_num = 0;
	iter = mapit_getallusers();
	for (pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter)) {
		if (pl_sd->mapindex == m_index) {
			if (pl_sd->state.vending)
				vend_num++;
			else if ((cd = (struct chat_data *)map_id2bl(pl_sd->chatID)) != NULL && cd->usersd[0] == pl_sd)
				chat_num++;
		}
	}
	mapit_free(iter);

	sprintf(atcmd_output, msg_txt(1040), mapname, map[m_id].users, map[m_id].npc_num, chat_num, vend_num); // Map: %s | Players: %d | NPCs: %d | Chats: %d | Vendings: %d
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, msg_txt(1041)); // ------ Map Flags ------
	if (map[m_id].flag.town)
		clif_displaymessage(fd, msg_txt(1042)); // Town Map
	if (map[m_id].flag.restricted) {
		sprintf(atcmd_output, "Restricted (zone %d)",map[m_id].zone);
		clif_displaymessage(fd, atcmd_output);
	}

	if (battle_config.autotrade_mapflag == map[m_id].flag.autotrade)
		clif_displaymessage(fd, msg_txt(1043)); // Autotrade Enabled
	else
		clif_displaymessage(fd, msg_txt(1044)); // Autotrade Disabled

	if (map[m_id].flag.battleground) {
		sprintf(atcmd_output, msg_txt(1045), map[m_id].flag.battleground); // Battlegrounds ON (type %d)
		clif_displaymessage(fd, atcmd_output);
	}

	// Skill damage adjustment info [Cydh]
#ifdef ADJUST_SKILL_DAMAGE
	if (map[m_id].flag.skill_damage) {
		clif_displaymessage(fd,msg_txt(1052)); // Skill Damage Adjustments:
		sprintf(atcmd_output," > [Map] %d%%, %d%%, %d%%, %d%% | Caster:%d"
			,map[m_id].adjust.damage.pc
			,map[m_id].adjust.damage.mob
			,map[m_id].adjust.damage.boss
			,map[m_id].adjust.damage.other
			,map[m_id].adjust.damage.caster);
		clif_displaymessage(fd, atcmd_output);
		if (map[m_id].skill_damage.count) {
			uint8 j;

			clif_displaymessage(fd," > [Map Skill] Name : Player, Monster, Boss Monster, Other | Caster");
			for (j = 0; j < map[m_id].skill_damage.count; j++) {
				sprintf(atcmd_output,"     %d. %s : %d%%, %d%%, %d%%, %d%% | %d"
					,j + 1
					,skill_get_name(map[m_id].skill_damage.entries[j]->skill_id)
					,map[m_id].skill_damage.entries[j]->pc
					,map[m_id].skill_damage.entries[j]->mob
					,map[m_id].skill_damage.entries[j]->boss
					,map[m_id].skill_damage.entries[j]->other
					,map[m_id].skill_damage.entries[j]->caster);
				clif_displaymessage(fd,atcmd_output);
			}
		}
	}
#endif

	strcpy(atcmd_output,msg_txt(1046)); // PvP Flags:
	if (map[m_id].flag.pvp)
		strcat(atcmd_output, " Pvp ON |");
	if (map[m_id].flag.pvp_noguild)
		strcat(atcmd_output, " NoGuild |");
	if (map[m_id].flag.pvp_noparty)
		strcat(atcmd_output, " NoParty |");
	if (map[m_id].flag.pvp_nightmaredrop)
		strcat(atcmd_output, " NightmareDrop |");
	if (map[m_id].flag.pvp_nocalcrank)
		strcat(atcmd_output, " NoCalcRank |");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,msg_txt(1047)); // GvG Flags:
	if (map[m_id].flag.gvg)
		strcat(atcmd_output, " GvG ON |");
	if (map[m_id].flag.gvg_dungeon)
		strcat(atcmd_output, " GvG Dungeon |");
	if (map[m_id].flag.gvg_castle)
		strcat(atcmd_output, " GvG Castle |");
	if (map[m_id].flag.gvg_te)
		strcat(atcmd_output, " GvG TE |");
	if (map[m_id].flag.gvg_te_castle)
		strcat(atcmd_output, " GvG TE Castle |");
	if (map[m_id].flag.gvg_noparty)
		strcat(atcmd_output, " NoParty |");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,msg_txt(1048)); // Teleport Flags:
	if (map[m_id].flag.noteleport)
		strcat(atcmd_output, " NoTeleport |");
	if (map[m_id].flag.monster_noteleport)
		strcat(atcmd_output, " Monster NoTeleport |");
	if (map[m_id].flag.nowarp)
		strcat(atcmd_output, " NoWarp |");
	if (map[m_id].flag.nowarpto)
		strcat(atcmd_output, " NoWarpTo |");
	if (map[m_id].flag.noreturn)
		strcat(atcmd_output, " NoReturn |");
	if (map[m_id].flag.nogo)
		strcat(atcmd_output, " NoGo |");
	if (map[m_id].flag.nomemo)
		strcat(atcmd_output, "  NoMemo |");
	clif_displaymessage(fd, atcmd_output);

	sprintf(atcmd_output, msg_txt(1065),  // No Exp Penalty: %s | No Zeny Penalty: %s
		(map[m_id].flag.noexppenalty) ? msg_txt(1066) : msg_txt(1067), (map[m_id].flag.nozenypenalty) ? msg_txt(1066) : msg_txt(1067)); // On / Off
	clif_displaymessage(fd, atcmd_output);

	if (map[m_id].flag.nosave) {
		if (!map[m_id].save.map)
			clif_displaymessage(fd, msg_txt(1068)); // No Save (Return to last Save Point)
		else if (map[m_id].save.x == -1 || map[m_id].save.y == -1 ) {
			sprintf(atcmd_output, msg_txt(1069), mapindex_id2name(map[m_id].save.map)); // No Save, Save Point: %s,Random
			clif_displaymessage(fd, atcmd_output);
		} else {
			sprintf(atcmd_output, msg_txt(1070), // No Save, Save Point: %s,%d,%d
				mapindex_id2name(map[m_id].save.map),map[m_id].save.x,map[m_id].save.y);
			clif_displaymessage(fd, atcmd_output);
		}
	}

	strcpy(atcmd_output,msg_txt(1049)); // Weather Flags:
	if (map[m_id].flag.snow)
		strcat(atcmd_output, " Snow |");
	if (map[m_id].flag.fog)
		strcat(atcmd_output, " Fog |");
	if (map[m_id].flag.sakura)
		strcat(atcmd_output, " Sakura |");
	if (map[m_id].flag.clouds)
		strcat(atcmd_output, " Clouds |");
	if (map[m_id].flag.clouds2)
		strcat(atcmd_output, "  Clouds2 |");
	if (map[m_id].flag.fireworks)
		strcat(atcmd_output, " Fireworks |");
	if (map[m_id].flag.leaves)
		strcat(atcmd_output, "  Leaves |");
	if (map[m_id].flag.nightenabled)
		strcat(atcmd_output, "  Displays Night |");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,msg_txt(1050)); // Other Flags:
	if (map[m_id].flag.nobranch)
		strcat(atcmd_output, " NoBranch |");
	if (map[m_id].flag.notrade)
		strcat(atcmd_output, " NoTrade |");
	if (map[m_id].flag.novending)
		strcat(atcmd_output, " NoVending |");
	if (map[m_id].flag.nodrop)
		strcat(atcmd_output, " NoDrop |");
	if (map[m_id].flag.noskill)
		strcat(atcmd_output, " NoSkill |");
	if (map[m_id].flag.noicewall)
		strcat(atcmd_output, " NoIcewall |");
	if (map[m_id].flag.allowks)
		strcat(atcmd_output, " AllowKS |");
	if (map[m_id].flag.reset)
		strcat(atcmd_output, " Reset |");
	if (map[m_id].flag.hidemobhpbar)
		strcat(atcmd_output, " HideMobHPBar |");
	if (map[m_id].flag.privateairship_source)
		strcat(atcmd_output, " PrivateAirshipSource |");
	if (map[m_id].flag.privateairship_destination)
		strcat(atcmd_output, " PrivateAirshipDestination |");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,msg_txt(1051)); // Other Flags:
	if (map[m_id].nocommand)
		strcat(atcmd_output, " NoCommand |");
	if (map[m_id].flag.nobaseexp)
		strcat(atcmd_output, " NoBaseEXP |");
	if (map[m_id].flag.nojobexp)
		strcat(atcmd_output, " NoJobEXP |");
	if (map[m_id].flag.nomobloot)
		strcat(atcmd_output, " NoMobLoot |");
	if (map[m_id].flag.nomvploot)
		strcat(atcmd_output, " NoMVPLoot |");
	if (map[m_id].flag.partylock)
		strcat(atcmd_output, " PartyLock |");
	if (map[m_id].flag.guildlock)
		strcat(atcmd_output, " GuildLock |");
	if (map[m_id].flag.loadevent)
		strcat(atcmd_output, " Loadevent |");
	if (map[m_id].flag.nochmautojoin)
		strcat(atcmd_output, " NoChmAutoJoin |");
	if (map[m_id].flag.nousecart)
		strcat(atcmd_output, " NoUsecart |");
	if (map[m_id].flag.noitemconsumption)
		strcat(atcmd_output, " NoItemConsumption |");
	if (map[m_id].flag.nosumstarmiracle)
		strcat(atcmd_output, " NoSumStarMiracle |");
	if (map[m_id].flag.nomineeffect)
		strcat(atcmd_output, " NoMineEffect |");
	if (map[m_id].flag.nolockon)
		strcat(atcmd_output, " NoLockOn |");
	if (map[m_id].flag.notomb)
		strcat(atcmd_output, " NoTomb |");
	if (map[m_id].flag.nocashshop)
		strcat(atcmd_output, " NoCashShop |");
	if (map[m_id].flag.nobanking)
		strcat(atcmd_output, " NoBanking |");
	if (map[m_id].flag.nocostume)
		strcat(atcmd_output, " NoCostume |");
	clif_displaymessage(fd, atcmd_output);

	switch (list) {
		case 0:
			// Do nothing. It's list 0, no additional display.
			break;
		case 1:
			clif_displaymessage(fd, msg_txt(480)); // ----- Players in Map -----
			iter = mapit_getallusers();
			for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
				if (pl_sd->mapindex == m_index) {
					sprintf(atcmd_output, msg_txt(481), // Player '%s' (session #%d) | Location: %d,%d
							pl_sd->status.name, pl_sd->fd, pl_sd->bl.x, pl_sd->bl.y);
					clif_displaymessage(fd, atcmd_output);
				}
			}
			mapit_free(iter);
			break;
		case 2:
			clif_displaymessage(fd, msg_txt(482)); // ----- NPCs in Map -----
			for (i = 0; i < map[m_id].npc_num;) {
				struct npc_data *nd = map[m_id].npc[i];

				switch(nd->ud.dir) {
					case DIR_NORTH:      strcpy(direction, msg_txt(491)); break;
					case DIR_NORTHWEST:  strcpy(direction, msg_txt(492)); break;
					case DIR_WEST:       strcpy(direction, msg_txt(493)); break;
					case DIR_SOUTHWEST:  strcpy(direction, msg_txt(494)); break;
					case DIR_SOUTH:      strcpy(direction, msg_txt(495)); break;
					case DIR_SOUTHEAST:  strcpy(direction, msg_txt(496)); break;
					case DIR_EAST:       strcpy(direction, msg_txt(497)); break;
					case DIR_NORTHEAST:  strcpy(direction, msg_txt(498)); break;
					default:             strcpy(direction, msg_txt(499)); break; // Unknown
				}
				if(strcmp(nd->name,nd->exname) == 0)
					sprintf(atcmd_output, msg_txt(490), // NPC %d: %s | Direction: %s | Sprite: %d | Location: %d %d
					++i, nd->name, direction, nd->class_, nd->bl.x, nd->bl.y);
				else
					sprintf(atcmd_output, msg_txt(489), // NPC %d: %s::%s | Direction: %s | Sprite: %d | Location: %d %d
					++i, nd->name, nd->exname, direction, nd->class_, nd->bl.x, nd->bl.y);
				clif_displaymessage(fd, atcmd_output);
			}
			break;
		case 3:
			clif_displaymessage(fd, msg_txt(483)); // ----- Chats in Map -----
			iter = mapit_getallusers();
			for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
				if ((cd = (struct chat_data *)map_id2bl(pl_sd->chatID)) != NULL &&
					pl_sd->mapindex == m_index &&
					cd->usersd[0] == pl_sd)
				{
					sprintf(atcmd_output, msg_txt(484), // Chat: %s | Player: %s | Location: %d %d
					cd->title, pl_sd->status.name, cd->bl.x, cd->bl.y);
					clif_displaymessage(fd, atcmd_output);
					sprintf(atcmd_output, msg_txt(485), // Users: %d/%d | Password: %s | Public: %s
					cd->users, cd->limit, cd->pass, (cd->pub) ? msg_txt(486) : msg_txt(487)); // Yes / No
					clif_displaymessage(fd, atcmd_output);
				}
			}
			mapit_free(iter);
			break;
		default: // Normally impossible to arrive here
			clif_displaymessage(fd, msg_txt(488)); // Please enter at least one valid list number (usage: @mapinfo <0-3> <map>).
			return -1;
			break;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(mount_peco)
{
	nullpo_retr(-1, sd);

	if( sd->disguise ) {
		clif_displaymessage(fd, msg_txt(212)); // Cannot mount while in disguise.
		return -1;
	}

	if( (sd->class_&MAPID_THIRDMASK) == MAPID_RUNE_KNIGHT ) {
		if( !pc_checkskill(sd, RK_DRAGONTRAINING) ) {
			sprintf(atcmd_output, msg_txt(213), skill_get_desc(RK_DRAGONTRAINING)); // You need %s to mount!
			clif_displaymessage(fd, atcmd_output);
			return -1;
		}
		if( !(sd->sc.option&OPTION_DRAGON) ) {
			unsigned int option = OPTION_DRAGON1;

			if( message[0] ) {
				int color = atoi(message);

				option = (color == 2 ? OPTION_DRAGON2 :
				           color == 3 ? OPTION_DRAGON3 :
				           color == 4 ? OPTION_DRAGON4 :
				           color == 5 ? OPTION_DRAGON5 :
				                        OPTION_DRAGON1);
			}
			clif_displaymessage(fd, msg_txt(1119)); // You have mounted your Dragon.
			pc_setoption(sd, sd->sc.option|option);
		} else {
			clif_displaymessage(fd, msg_txt(1120)); // You have released your Dragon.
			pc_setoption(sd, sd->sc.option&~OPTION_DRAGON);
		}
		return 0;
	}
	if( (sd->class_&MAPID_THIRDMASK) == MAPID_RANGER ) {
		if( !pc_checkskill(sd, RA_WUGRIDER) ) {
			sprintf(atcmd_output, msg_txt(213), skill_get_desc(RA_WUGRIDER)); // You need %s to mount!
			clif_displaymessage(fd, atcmd_output);
			return -1;
		}
		if( (pc_isfalcon(sd) && !battle_config.warg_can_falcon) ) {
			clif_displaymessage(fd, msg_txt(216)); // Please remove your Falcon.
			return -1;
		}
		if( !pc_isridingwug(sd) ) {
			clif_displaymessage(fd, msg_txt(1121)); // You have mounted your Warg.
			pc_setoption(sd, sd->sc.option|OPTION_WUGRIDER);
		} else {
			clif_displaymessage(fd, msg_txt(1122)); // You have released your Warg.
			pc_setoption(sd, sd->sc.option&~OPTION_WUGRIDER);
		}
		return 0;
	}
	if( (sd->class_&MAPID_THIRDMASK) == MAPID_MECHANIC ) {
		if( !pc_ismadogear(sd) ) {
			if( !pc_checkskill(sd, NC_MADOLICENCE) ) {
				sprintf(atcmd_output, msg_txt(213), skill_get_desc(NC_MADOLICENCE)); // You need %s to mount!
				clif_displaymessage(fd, atcmd_output);
				return -1;
			}
			clif_displaymessage(fd, msg_txt(1123)); // You have mounted your Mado Gear.
			pc_setoption(sd, sd->sc.option|OPTION_MADOGEAR);
		} else {
			clif_displaymessage(fd, msg_txt(1124)); // You have released your Mado Gear.
			pc_setoption(sd, sd->sc.option&~OPTION_MADOGEAR);
		}
		return 0;
	}
	if( sd->class_&MAPID_SWORDMAN && sd->class_&JOBL_2 ) {
		if( !pc_isriding(sd) ) { // If actually no peco
			if( !pc_checkskill(sd, KN_RIDING) ) {
				sprintf(atcmd_output, msg_txt(213), skill_get_desc(KN_RIDING)); // You need %s to mount!
				clif_displaymessage(fd, atcmd_output);
				return -1;
			}
			clif_displaymessage(fd, msg_txt(102)); // You have mounted a Peco Peco.
			pc_setoption(sd, sd->sc.option|OPTION_RIDING);
		} else { // Dismount
			clif_displaymessage(fd, msg_txt(214)); // You have released your Peco Peco.
			pc_setoption(sd, sd->sc.option&~OPTION_RIDING);
		}
		return 0;
	}

	clif_displaymessage(fd, msg_txt(215)); // Your class can't mount!
	return -1;
}

/*==========================================
 *Spy Commands by Syrus22
 *------------------------------------------*/
ACMD_FUNC(guildspy)
{
	char guild_name[NAME_LENGTH];
	struct guild *g;
	nullpo_retr(-1, sd);

	memset(guild_name, '\0', sizeof(guild_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!enable_spy) {
		clif_displaymessage(fd, msg_txt(1125)); // The mapserver has spy command support disabled.
		return -1;
	}
	if (!message || !*message || sscanf(message, "%23[^\n]", guild_name) < 1) {
		clif_displaymessage(fd, msg_txt(1126)); // Please enter a guild name/ID (usage: @guildspy <guild_name/ID>).
		return -1;
	}

	if ((g = guild_searchname(guild_name)) || // Name first to avoid error when name begin with a number
	    (g = guild_search(atoi(message)))) {
		if (sd->guildspy == g->guild_id) {
			sd->guildspy = 0;
			sprintf(atcmd_output, msg_txt(103), g->name); // No longer spying on the %s guild.
			clif_displaymessage(fd, atcmd_output);
		} else {
			sd->guildspy = g->guild_id;
			sprintf(atcmd_output, msg_txt(104), g->name); // Spying on the %s guild.
			clif_displaymessage(fd, atcmd_output);
		}
	} else {
		clif_displaymessage(fd, msg_txt(94)); // Incorrect name/ID, or no one from the specified guild is online.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(partyspy)
{
	char party_name[NAME_LENGTH];
	struct party_data *p;
	nullpo_retr(-1, sd);

	memset(party_name, '\0', sizeof(party_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!enable_spy) {
		clif_displaymessage(fd, msg_txt(1125)); // The mapserver has spy command support disabled.
		return -1;
	}

	if (!message || !*message || sscanf(message, "%23[^\n]", party_name) < 1) {
		clif_displaymessage(fd, msg_txt(1127)); // Please enter a party name/ID (usage: @partyspy <party_name/ID>).
		return -1;
	}

	if ((p = party_searchname(party_name)) || // Name first to avoid error when name begin with a number
	    (p = party_search(atoi(message)))) {
		if (sd->partyspy == p->party.party_id) {
			sd->partyspy = 0;
			sprintf(atcmd_output, msg_txt(105), p->party.name); // No longer spying on the %s party.
			clif_displaymessage(fd, atcmd_output);
		} else {
			sd->partyspy = p->party.party_id;
			sprintf(atcmd_output, msg_txt(106), p->party.name); // Spying on the %s party.
			clif_displaymessage(fd, atcmd_output);
		}
	} else {
		clif_displaymessage(fd, msg_txt(96)); // Incorrect name/ID, or no one from the specified party is online.
		return -1;
	}

	return 0;
}

ACMD_FUNC(clanspy)
{
	char clan_name[NAME_LENGTH];
	struct clan *c;
	nullpo_retr(-1, sd);

	memset(clan_name, '\0', sizeof(clan_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!enable_spy) {
		clif_displaymessage(fd, msg_txt(1125)); // The mapserver has spy command support disabled.
		return -1;
	}

	if (!message || !*message || sscanf(message, "%23[^\n]", clan_name) < 1) {
		clif_displaymessage(fd, msg_txt(734)); // Please enter a clan name/ID (usage: @clanspy <clan_name/ID>).
		return -1;
	}

	if ((c = clan_searchname(clan_name)) || // Name first to avoid error when name begin with a number
		(c = clan_search(atoi(message)))) {
		if (sd->clanspy == c->id) {
			sd->clanspy = 0;
			sprintf(atcmd_output, msg_txt(735), c->name); // No longer spying on the %s clan.
			clif_displaymessage(fd, atcmd_output);
		} else {
			sd->clanspy = c->id;
			sprintf(atcmd_output, msg_txt(736), c->name); // Spying on the %s clan.
			clif_displaymessage(fd, atcmd_output);
		}
	} else {
		clif_displaymessage(fd, msg_txt(737)); // Incorrect clan name/ID.
		return -1;
	}

	return 0;
}

/*==========================================
 * @repairall [Valaris]
 *------------------------------------------*/
ACMD_FUNC(repairall)
{
	int count, i;
	nullpo_retr(-1, sd);

	count = 0;
	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].card[0] == CARD0_PET)
			continue;
		if (sd->inventory.u.items_inventory[i].nameid && sd->inventory.u.items_inventory[i].attribute) {
			sd->inventory.u.items_inventory[i].attribute = 0;
			clif_produceeffect(sd, 0, sd->inventory.u.items_inventory[i].nameid);
			count++;
		}
	}

	if (count > 0) {
		clif_misceffect(&sd->bl, 3);
		clif_equiplist(sd);
		clif_displaymessage(fd, msg_txt(107)); // All items have been repaired.
	} else {
		clif_displaymessage(fd, msg_txt(108)); // No item need to be repaired.
		return -1;
	}

	return 0;
}

/*==========================================
 * @nuke [Valaris]
 *------------------------------------------*/
ACMD_FUNC(nuke)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(1128)); // Please enter a player name (usage: @nuke <char name>).
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) != NULL) {
		if (pc_get_group_level(sd) >= pc_get_group_level(pl_sd)) { // you can kill only lower or same GM level
			skill_castend_nodamage_id(&pl_sd->bl, &pl_sd->bl, NPC_SELFDESTRUCTION, 99, gettick(), 0);
			clif_displaymessage(fd, msg_txt(109)); // Player has been nuked!
		} else {
			clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
			return -1;
		}
	} else {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	return 0;
}

/*==========================================
 * @tonpc
 *------------------------------------------*/
ACMD_FUNC(tonpc)
{
	char npcname[NAME_LENGTH + 1];
	struct npc_data *nd;

	nullpo_retr(-1, sd);

	memset(npcname, 0, sizeof(npcname));

	if (!message || !*message || sscanf(message, "%23[^\n]", npcname) < 1) {
		clif_displaymessage(fd, msg_txt(1129)); // Please enter a NPC name (usage: @tonpc <NPC_name>).
		return -1;
	}

	if ((nd = npc_name2id(npcname)) != NULL) {
		if (nd->bl.m != -1 && pc_setpos(sd, map_id2index(nd->bl.m), nd->bl.x, nd->bl.y, CLR_TELEPORT) == SETPOS_OK)
			clif_displaymessage(fd, msg_txt(0)); // Warped.
		else
			return -1;
	} else {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(shownpc)
{
	char NPCname[NAME_LENGTH+1];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%23[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, msg_txt(1130)); // Please enter a NPC name (usage: @enablenpc <NPC_name>).
		return -1;
	}

	if (npc_name2id(NPCname) != NULL) {
		npc_enable(NPCname, 1);
		clif_displaymessage(fd, msg_txt(110)); // Npc Enabled.
	} else {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(hidenpc)
{
	char NPCname[NAME_LENGTH+1];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%23[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, msg_txt(1131)); // Please enter a NPC name (usage: @hidenpc <NPC_name>).
		return -1;
	}

	if (npc_name2id(NPCname) == NULL) {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	npc_enable(NPCname, 0);
	clif_displaymessage(fd, msg_txt(112)); // Npc Disabled.
	return 0;
}

ACMD_FUNC(loadnpc)
{
	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1132)); // Please enter a script file name (usage: @loadnpc <file name>).
		return -1;
	}

	if (!npc_addsrcfile(message, true)) {
		clif_displaymessage(fd, msg_txt(261)); // Script could not be loaded.
		return -1;
	}

	npc_read_event_script();

	clif_displaymessage(fd, msg_txt(262)); // Script loaded.
	return 0;
}

ACMD_FUNC(unloadnpc)
{
	struct npc_data *nd;
	char NPCname[NAME_LENGTH];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%23[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, msg_txt(1133)); // Please enter a NPC name (usage: @unloadnpc <NPC_name>).
		return -1;
	}

	if (!(nd = npc_name2id(NPCname))) {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	npc_unload_duplicates(nd);
	npc_unload(nd,true);
	npc_read_event_script();
	clif_displaymessage(fd, msg_txt(112)); // Npc Disabled.
	return 0;
}

/*==========================================
 * time in txt for time command (by [Yor])
 *------------------------------------------*/
char *txt_time(unsigned int duration)
{
	int days, hours, minutes, seconds;
	static char temp1[CHAT_SIZE_MAX];
	int tlen = 0;

	memset(temp1, '\0', sizeof(temp1));

	days = duration / (60 * 60 * 24);
	duration = duration - (60 * 60 * 24 * days);
	hours = duration / (60 * 60);
	duration = duration - (60 * 60 * hours);
	minutes = duration / 60;
	seconds = duration - (60 * minutes);

	if (days == 1)
		tlen += sprintf(tlen + temp1, msg_txt(219), days); // %d day
	else if (days > 1)
		tlen += sprintf(tlen + temp1, msg_txt(220), days); // %d days
	if (hours == 1)
		tlen += sprintf(tlen + temp1, msg_txt(221), hours); // %d hour
	else if (hours > 1)
		tlen += sprintf(tlen + temp1, msg_txt(222), hours); // %d hours
	if (minutes < 2)
		tlen += sprintf(tlen + temp1, msg_txt(223), minutes); // %d minute
	else
		tlen += sprintf(tlen + temp1, msg_txt(224), minutes); // %d minutes
	if (seconds == 1)
		tlen += sprintf(tlen + temp1, msg_txt(225), seconds); // and %d second
	else if (seconds > 1)
		tlen += sprintf(tlen + temp1, msg_txt(226), seconds); // and %d seconds

	return temp1;
}

/*==========================================
 * @time/@date/@serverdate/@servertime: Display the date/time of the server (by [Yor]
 * Calculation management of GM modification (@day/@night GM commands) is done
 *------------------------------------------*/
ACMD_FUNC(servertime)
{
	time_t time_server;  // Variable for number of seconds (used with time() function)
	struct tm *datetime; // Variable for time in structure ->tm_mday, ->tm_sec, ...
	char temp[CHAT_SIZE_MAX];
	nullpo_retr(-1, sd);

	memset(temp, '\0', sizeof(temp));

	time(&time_server);  // Get time in seconds since 1/1/1970
	datetime = localtime(&time_server); // Convert seconds in structure
	// Like sprintf, but only for date/time (Sunday, November 02 2003 15:12:52)
	strftime(temp, sizeof(temp) - 1, msg_txt(230), datetime); // Server time (normal time): %A, %B %d %Y %X.
	clif_displaymessage(fd, temp);

	if (day_timer_tid != INVALID_TIMER && night_timer_tid != INVALID_TIMER) {
		const struct TimerData *timer_data = get_timer(night_timer_tid);
		const struct TimerData *timer_data2 = get_timer(day_timer_tid);

		if (night_flag == 0) {
			sprintf(temp, msg_txt(235), txt_time(DIFF_TICK(timer_data->tick,gettick()) / 1000)); // Game time: The game is in daylight for %s.
			clif_displaymessage(fd, temp);
			if (DIFF_TICK(timer_data->tick, timer_data2->tick) > 0)
				sprintf(temp, msg_txt(237), txt_time(DIFF_TICK(timer_data->interval,DIFF_TICK(timer_data->tick,timer_data2->tick)) / 1000)); // Game time: After, the game will be in night for %s.
			else
				sprintf(temp, msg_txt(237), txt_time(DIFF_TICK(timer_data2->tick,timer_data->tick) / 1000)); // Game time: After, the game will be in night for %s.
			clif_displaymessage(fd, temp);
		} else {
			sprintf(temp, msg_txt(233), txt_time(DIFF_TICK(timer_data2->tick,gettick()) / 1000)); // Game time: The game is in night for %s.
			clif_displaymessage(fd, temp);
			if (DIFF_TICK(timer_data2->tick,timer_data->tick) > 0)
				sprintf(temp, msg_txt(239), txt_time((timer_data2->interval - DIFF_TICK(timer_data2->tick, timer_data->tick)) / 1000)); // Game time: After, the game will be in daylight for %s.
			else
				sprintf(temp, msg_txt(239), txt_time(DIFF_TICK(timer_data->tick, timer_data2->tick) / 1000)); // Game time: After, the game will be in daylight for %s.
			clif_displaymessage(fd, temp);
		}
		sprintf(temp, msg_txt(238), txt_time(timer_data2->interval / 1000)); // Game time: A day cycle has a normal duration of %s.
		clif_displaymessage(fd, temp);
	} else {
		if (night_flag == 0)
			clif_displaymessage(fd, msg_txt(231)); // Game time: The game is in permanent daylight.
		else
			clif_displaymessage(fd, msg_txt(232)); // Game time: The game is in permanent night.
	}

	return 0;
}

/*==========================================
 * @jail <char_name> by [Yor]
 * Special warp! No check with nowarp and nowarpto flag
 *------------------------------------------*/
ACMD_FUNC(jail)
{
	struct map_session_data *pl_sd;
	int x, y;
	unsigned short m_index;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(1134)); // Please enter a player name (usage: @jail <char_name>).
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) { // You can jail only lower or same GM
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
		return -1;
	}

	if (pl_sd->sc.data[SC_JAILED]) {
		clif_displaymessage(fd, msg_txt(118)); // Player warped in jails.
		return -1;
	}

	switch(rnd()%2) { // Jail Locations
		case 0:
			m_index = mapindex_name2id(MAP_JAIL);
			x = 24;
			y = 75;
			break;
		default:
			m_index = mapindex_name2id(MAP_JAIL);
			x = 49;
			y = 75;
			break;
	}

	// Duration of INT_MAX to specify infinity.
	sc_start4(NULL, &pl_sd->bl, SC_JAILED, 100, INT_MAX, m_index, x, y, 1000);
	clif_displaymessage(pl_sd->fd, msg_txt(117)); // GM has send you in jails.
	clif_displaymessage(fd, msg_txt(118)); // Player warped in jails.
	return 0;
}

/*==========================================
 * @unjail/@discharge <char_name> by [Yor]
 * Special warp! No check with nowarp and nowarpto flag
 *------------------------------------------*/
ACMD_FUNC(unjail)
{
	struct map_session_data *pl_sd;

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(1135)); // Please enter a player name (usage: @unjail/@discharge <char_name>).
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) { // You can jail only lower or same GM
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
		return -1;
	}

	if (!pl_sd->sc.data[SC_JAILED]) {
		clif_displaymessage(fd, msg_txt(119)); // This player is not in jails.
		return -1;
	}

	//Reset jail time to 1 sec.
	sc_start(NULL, &pl_sd->bl, SC_JAILED, 100, 1, 1000);
	clif_displaymessage(pl_sd->fd, msg_txt(120)); // A GM has discharged you from jail.
	clif_displaymessage(fd, msg_txt(121)); // Player unjailed.
	return 0;
}

ACMD_FUNC(jailfor) {
	struct map_session_data *pl_sd = NULL;
	char *modif_p;
	int jailtime = 0, x, y;
	short m_index = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%255s %23[^\n]",atcmd_output,atcmd_player_name) < 2) {
		clif_displaymessage(fd, msg_txt(400));	//Usage: @jailfor <time> <character name>
		return -1;
	}

	atcmd_output[sizeof(atcmd_output) - 1] = '\0';

	modif_p = atcmd_output;
	jailtime = (int)solve_time(modif_p) / 60; // Change to minutes

	if (!jailtime) {
		clif_displaymessage(fd, msg_txt(1136)); // Invalid time for jail command.
		clif_displaymessage(fd, msg_txt(702)); // Time parameter format is <+/-value> to alter. y/a = Year, m = Month, d/j = Day, h = Hour, n/mn = Minute, s = Second.
		return -1;
	}

	if (!(pl_sd = map_nick2sd(atcmd_player_name))) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) {
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
		return -1;
	}

	// Added by Coltaro
	if (pl_sd->sc.data[SC_JAILED] && pl_sd->sc.data[SC_JAILED]->val1 != INT_MAX) { // Update the player's jail time
		jailtime += pl_sd->sc.data[SC_JAILED]->val1;
		if (jailtime <= 0) {
			jailtime = 0;
			clif_displaymessage(pl_sd->fd, msg_txt(120)); // GM has discharge you.
			clif_displaymessage(fd, msg_txt(121)); // Player unjailed.
		} else {
			int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
			char timestr[21];
			time_t now = time(NULL);

			split_time(jailtime * 60, &year, &month, &day, &hour, &minute, &second);
			sprintf(atcmd_output, msg_txt(402), msg_txt(1137), year, month, day, hour, minute); // %s in jail for %d years, %d months, %d days, %d hours and %d minutes
			clif_displaymessage(pl_sd->fd, atcmd_output);
			sprintf(atcmd_output, msg_txt(402), msg_txt(1138), year, month, day, hour, minute); // This player is now in jail for %d years, %d months, %d days, %d hours and %d minutes
			clif_displaymessage(fd, atcmd_output);
			timestamp2string(timestr, 20, now + jailtime * 60, "%Y-%m-%d %H:%M");
			sprintf(atcmd_output, "Release date is: %s", timestr);
			clif_displaymessage(pl_sd->fd, atcmd_output);
			clif_displaymessage(fd, atcmd_output);
		}
	} else if (jailtime < 0) {
		clif_displaymessage(fd, msg_txt(1136)); // Invalid time for jail command.
		return -1;
	}

	switch(rnd()%2) { // Jail locations, add more as you wish.
		case 1: // Jail #1
			m_index = mapindex_name2id(MAP_JAIL);
			x = 49; y = 75;
			break;
		default: // Default Jail
			m_index = mapindex_name2id(MAP_JAIL);
			x = 24; y = 75;
			break;
	}

	//jailtime = 0: Time was reset to 0. Wait 1 second to warp player out (since it's done in status_change_timer).
	sc_start4(NULL, &pl_sd->bl, SC_JAILED, 100, jailtime, m_index, x, y, (jailtime ? 60000 : 1000));
	return 0;
}

//By Coltaro
ACMD_FUNC(jailtime) {
	int year, month, day, hour, minute, second;
	char timestr[21];
	time_t now = time(NULL);

	nullpo_retr(-1, sd);

	if (!sd->sc.data[SC_JAILED]) {
		clif_displaymessage(fd, msg_txt(1139)); // You are not in jail.
		return -1;
	}

	if (sd->sc.data[SC_JAILED]->val1 == INT_MAX) {
		clif_displaymessage(fd, msg_txt(1140)); // You have been jailed indefinitely.
		return 0;
	}

	if (sd->sc.data[SC_JAILED]->val1 <= 0) { // Was not jailed with @jailfor (maybe @jail? or warped there? or got recalled?)
		clif_displaymessage(fd, msg_txt(1141)); // You have been jailed for an unknown amount of time.
		return -1;
	}

	// Get remaining jail time
	split_time(sd->sc.data[SC_JAILED]->val1 * 60, &year, &month, &day, &hour, &minute, &second);
	sprintf(atcmd_output, msg_txt(402), msg_txt(1142), year, month, day, hour, minute); // You will remain in jail for %d years, %d months, %d days, %d hours and %d minutes
	clif_displaymessage(fd, atcmd_output);
	timestamp2string(timestr, 20, now + sd->sc.data[SC_JAILED]->val1 * 60, "%Y-%m-%d %H:%M");
	sprintf(atcmd_output, "Release date is: %s", timestr);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * @disguise <mob_id> by [Valaris] (simplified by [Yor])
 *------------------------------------------*/
ACMD_FUNC(disguise)
{
	int id = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1143)); // Please enter a Monster/NPC name/ID (usage: @disguise <name/ID>).
		return -1;
	}

	if ((id = atoi(message)) > 0) { // Acquired an ID
		if (!mobdb_checkid(id) && !npcdb_checkid(id))
			id = 0; // Invalid id for either mobs or npcs
	} else { // Acquired a Name
		if ((id = mobdb_searchname(message)) == 0) {
			struct npc_data *nd = npc_name2id(message);

			if (nd != NULL)
				id = nd->class_;
		}
	}

	if (id == 0) {
		clif_displaymessage(fd, msg_txt(123));	// Invalid Monster/NPC name/ID specified.
		return -1;
	}

	if (pc_isriding(sd)) {
		clif_displaymessage(fd, msg_txt(1144)); // Character cannot be disguised while mounted.
		return -1;
	}

	if (sd->sc.data[SC_MONSTER_TRANSFORM] || sd->sc.data[SC_ACTIVE_MONSTER_TRANSFORM]) {
		clif_displaymessage(fd, msg_txt(1492)); // Character cannot be disguised while in monster form.
		return -1;
	}

	pc_disguise(sd, id);
	clif_displaymessage(fd, msg_txt(122)); // Disguise applied.

	return 0;
}

/*==========================================
 * DisguiseAll
 *------------------------------------------*/
ACMD_FUNC(disguiseall)
{
	int mob_id = 0;
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1145)); // Please enter a Monster/NPC name/ID (usage: @disguiseall <name/ID>).
		return -1;
	}

	if ((mob_id = mobdb_searchname(message)) == 0) // Check name first (to avoid possible name beginning by a number)
		mob_id = atoi(message);

	if (!mobdb_checkid(mob_id) && !npcdb_checkid(mob_id)) { //If mob or npc.
		clif_displaymessage(fd, msg_txt(123)); // Monster/NPC name/id not found.
		return -1;
	}

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) )
		pc_disguise(pl_sd, mob_id);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(122)); // Disguise applied.
	return 0;
}

/*==========================================
 * DisguiseGuild
 *------------------------------------------*/
ACMD_FUNC(disguiseguild)
{
	int id = 0, i;
	char monster[NAME_LENGTH], guild[NAME_LENGTH];
	struct guild *g;

	memset(monster, '\0', sizeof(monster));
	memset(guild, '\0', sizeof(guild));

	if( !message || !*message || sscanf(message, "%23[^,], %23[^\r\n]", monster, guild) < 2 ) {
		clif_displaymessage(fd, msg_txt(1146)); // Please enter a mob name/ID and guild name/ID (usage: @disguiseguild <mob name/ID>, <guild name/ID>).
		return -1;
	}

	if( (id = atoi(monster)) > 0 ) {
		if( !mobdb_checkid(id) && !npcdb_checkid(id) )
			id = 0;
	} else {
		if( (id = mobdb_searchname(monster)) == 0 ) {
			struct npc_data *nd = npc_name2id(monster);
			if( nd != NULL )
				id = nd->class_;
		}
	}

	if( id == 0 ) {
		clif_displaymessage(fd, msg_txt(123));	// Monster/NPC name/id hasn't been found.
		return -1;
	}

	if( (g = guild_searchname(guild)) == NULL && (g = guild_search(atoi(guild))) == NULL ) {
		clif_displaymessage(fd, msg_txt(94)); // Incorrect name/ID, or no one from the guild is online.
		return -1;
	}

	for( i = 0; i < g->max_member; i++ ) {
		struct map_session_data *pl_sd;

		if( (pl_sd = g->member[i].sd) && !pc_isriding(pl_sd) )
			pc_disguise(pl_sd, id);
	}

	clif_displaymessage(fd, msg_txt(122)); // Disguise applied.
	return 0;
}


/*==========================================
 * @undisguise by [Yor]
 *------------------------------------------*/
ACMD_FUNC(undisguise)
{
	nullpo_retr(-1, sd);

	if (sd->disguise) {
		pc_disguise(sd, 0);
		clif_displaymessage(fd, msg_txt(124)); // Undisguise applied.
	} else {
		clif_displaymessage(fd, msg_txt(125)); // You're not disguised.
		return -1;
	}

	return 0;
}

/*==========================================
 * UndisguiseAll
 *------------------------------------------*/
ACMD_FUNC(undisguiseall)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) )
		if( pl_sd->disguise )
			pc_disguise(pl_sd, 0);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(124)); // Undisguise applied.

	return 0;
}

/*==========================================
 * UndisguiseGuild
 *------------------------------------------*/
ACMD_FUNC(undisguiseguild)
{
	char guild_name[NAME_LENGTH];
	struct guild *g;
	int i;
	nullpo_retr(-1, sd);

	memset(guild_name, '\0', sizeof(guild_name));

	if(!message || !*message || sscanf(message, "%23[^\n]", guild_name) < 1) {
		clif_displaymessage(fd, msg_txt(1147)); // Please enter guild name/ID (usage: @undisguiseguild <guild name/ID>).
		return -1;
	}

	if( (g = guild_searchname(guild_name)) == NULL && (g = guild_search(atoi(message))) == NULL ) {
		clif_displaymessage(fd, msg_txt(94)); // Incorrect name/ID, or no one from the guild is online.
		return -1;
	}

	for(i = 0; i < g->max_member; i++) {
		struct map_session_data *pl_sd;

		if( (pl_sd = g->member[i].sd) && pl_sd->disguise )
			pc_disguise(pl_sd, 0);
	}

	clif_displaymessage(fd, msg_txt(124)); // Undisguise applied.

	return 0;
}

/*==========================================
 * @exp by [Skotlex]
 *------------------------------------------*/
ACMD_FUNC(exp)
{
	char output[CHAT_SIZE_MAX];
	double nextb, nextj;
	nullpo_retr(-1, sd);
	memset(output, '\0', sizeof(output));

	nextb = pc_nextbaseexp(sd);
	if (nextb)
		nextb = sd->status.base_exp*100.0/nextb;

	nextj = pc_nextjobexp(sd);
	if (nextj)
		nextj = sd->status.job_exp*100.0/nextj;

	sprintf(output, msg_txt(1148), sd->status.base_level, nextb, sd->status.job_level, nextj); // Base Level: %d (%.3f%%) | Job Level: %d (%.3f%%)
	clif_displaymessage(fd, output);
	return 0;
}


/*==========================================
 * @broadcast by [Valaris]
 *------------------------------------------*/
ACMD_FUNC(broadcast)
{
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1149)); // Please enter a message (usage: @broadcast <message>).
		return -1;
	}

	sprintf(atcmd_output, "%s: %s", sd->status.name, message);
	intif_broadcast(atcmd_output, strlen(atcmd_output) + 1, BC_DEFAULT);

	return 0;
}

/*==========================================
 * @localbroadcast by [Valaris]
 *------------------------------------------*/
ACMD_FUNC(localbroadcast)
{
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1150)); // Please enter a message (usage: @localbroadcast <message>).
		return -1;
	}

	sprintf(atcmd_output, "%s: %s", sd->status.name, message);

	clif_broadcast(&sd->bl, atcmd_output, strlen(atcmd_output) + 1, BC_DEFAULT, ALL_SAMEMAP);

	return 0;
}

/*==========================================
 * @email <actual@email> <new@email> by [Yor]
 *------------------------------------------*/
ACMD_FUNC(email)
{
	char actual_email[100];
	char new_email[100];
	nullpo_retr(-1, sd);

	memset(actual_email, '\0', sizeof(actual_email));
	memset(new_email, '\0', sizeof(new_email));

	if (!message || !*message || sscanf(message, "%99s %99s", actual_email, new_email) < 2) {
		clif_displaymessage(fd, msg_txt(1151)); // Please enter 2 emails (usage: @email <actual@email> <new@email>).
		return -1;
	}

	if (e_mail_check(actual_email) == 0) {
		clif_displaymessage(fd, msg_txt(144)); // Invalid actual email. If you have default e-mail, give a@a.com.
		return -1;
	} else if (e_mail_check(new_email) == 0) {
		clif_displaymessage(fd, msg_txt(145)); // Invalid new email. Please enter a real e-mail.
		return -1;
	} else if (strcmpi(new_email, "a@a.com") == 0) {
		clif_displaymessage(fd, msg_txt(146)); // New email must be a real e-mail.
		return -1;
	} else if (strcmpi(actual_email, new_email) == 0) {
		clif_displaymessage(fd, msg_txt(147)); // New email must be different of the actual e-mail.
		return -1;
	}

	chrif_changeemail(sd->status.account_id, actual_email, new_email);
	clif_displaymessage(fd, msg_txt(148)); // Information sent to login-server via char-server.
	return 0;
}

/*==========================================
 *@effect
 *------------------------------------------*/
ACMD_FUNC(effect)
{
	int type = EF_NONE;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d", &type) < 1) {
		clif_displaymessage(fd, msg_txt(1152)); // Please enter an effect number (usage: @effect <effect number>).
		return -1;
	}

	if (type <= EF_NONE || type >= EF_MAX) {
		sprintf(atcmd_output, msg_txt(699), EF_NONE + 1, EF_MAX - 1); // Please enter a valid effect id in the range from %d to %d.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	clif_specialeffect(&sd->bl, type, ALL_CLIENT);
	clif_displaymessage(fd, msg_txt(229)); // Your effect has changed.
	return 0;
}

/*==========================================
 * @killer by MouseJstr
 * enable killing players even when not in pvp
 *------------------------------------------*/
ACMD_FUNC(killer)
{
	nullpo_retr(-1, sd);
	sd->state.killer = !sd->state.killer;

	if(sd->state.killer)
		clif_displaymessage(fd, msg_txt(241)); // You can now attack and kill players freely.
	else {
		clif_displaymessage(fd, msg_txt(292)); // Killer state reset.
		pc_stop_attack(sd);
	}
	return 0;
}

/*==========================================
 * @killable by MouseJstr
 * enable other people killing you
 *------------------------------------------*/
ACMD_FUNC(killable)
{
	nullpo_retr(-1, sd);
	sd->state.killable = !sd->state.killable;

	if(sd->state.killable)
		clif_displaymessage(fd, msg_txt(242)); // You can now be attacked and killed by players.
	else {
		clif_displaymessage(fd, msg_txt(288)); // You are no longer killable.
		map_foreachinallrange(unit_stopattack, &sd->bl, AREA_SIZE, BL_CHAR, sd->bl.id);
	}
	return 0;
}

/*==========================================
 * @skillon by MouseJstr
 * turn skills on for the map
 *------------------------------------------*/
ACMD_FUNC(skillon)
{
	nullpo_retr(-1, sd);
	map[sd->bl.m].flag.noskill = 0;
	clif_displaymessage(fd, msg_txt(244)); // Skills have been enabled on this map.
	return 0;
}

/*==========================================
 * @skilloff by MouseJstr
 * Turn skills off on the map
 *------------------------------------------*/
ACMD_FUNC(skilloff)
{
	nullpo_retr(-1, sd);
	map[sd->bl.m].flag.noskill = 1;
	clif_displaymessage(fd, msg_txt(243)); // Skills have been disabled on this map.
	return 0;
}

/*==========================================
 * @npcmove by MouseJstr
 * move a npc
 *------------------------------------------*/
ACMD_FUNC(npcmove)
{
	int x = 0, y = 0, m;
	struct npc_data *nd = 0;
	nullpo_retr(-1, sd);
	memset(atcmd_player_name, '\0', sizeof atcmd_player_name);

	if (!message || !*message || sscanf(message, "%d %d %23[^\n]", &x, &y, atcmd_player_name) < 3) {
		clif_displaymessage(fd, msg_txt(1153)); // Usage: @npcmove <X> <Y> <npc_name>
		return -1;
	}

	if ((nd = npc_name2id(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	if ((m = nd->bl.m) < 0 || nd->bl.prev == NULL) {
		clif_displaymessage(fd, msg_txt(1154)); // NPC is not on this map.
		return -1;	//Not on a map.
	}

	x = cap_value(x, 0, map[m].xs-1);
	y = cap_value(y, 0, map[m].ys-1);
	map_foreachinallrange(clif_outsight, &nd->bl, AREA_SIZE, BL_PC, &nd->bl);
	map_moveblock(&nd->bl, x, y, gettick());
	map_foreachinallrange(clif_insight, &nd->bl, AREA_SIZE, BL_PC, &nd->bl);
	clif_displaymessage(fd, msg_txt(1155)); // NPC moved.

	return 0;
}

/*==========================================
 * @addwarp by MouseJstr
 * Create a new static warp point.
 *------------------------------------------*/
ACMD_FUNC(addwarp)
{
	char mapname[32], warpname[NAME_LENGTH+1];
	int x,y;
	unsigned short m;
	struct npc_data *nd;

	nullpo_retr(-1, sd);
	memset(warpname, '\0', sizeof(warpname));
	
	if (!message || !*message || sscanf(message, "%31s %d %d %23[^\n]", mapname, &x, &y, warpname) < 4) {
		clif_displaymessage(fd, msg_txt(1156)); // Usage: @addwarp <mapname> <X> <Y> <npc name>
		return -1;
	}

	m = mapindex_name2id(mapname);
	if (m == 0) {
		sprintf(atcmd_output, msg_txt(1157), mapname); // Unknown map '%s'.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	nd = npc_add_warp(warpname, sd->bl.m, sd->bl.x, sd->bl.y, 2, 2, m, x, y);
	if (nd == NULL)
		return -1;

	sprintf(atcmd_output, msg_txt(1158), nd->exname); // New warp NPC '%s' created.
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * @follow by [MouseJstr]
 * Follow a player .. staying no more then 5 spaces away
 *------------------------------------------*/
ACMD_FUNC(follow)
{
	struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		if (sd->followtarget == -1)
			return -1;

		pc_stop_following (sd);
		clif_displaymessage(fd, msg_txt(1159)); // Follow mode OFF.
		return 0;
	}
	
	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (sd->followtarget == pl_sd->bl.id) {
		pc_stop_following (sd);
		clif_displaymessage(fd, msg_txt(1159)); // Follow mode OFF.
	} else {
		pc_follow(sd, pl_sd->bl.id);
		clif_displaymessage(fd, msg_txt(1160)); // Follow mode ON.
	}
	
	return 0;
}


/*==========================================
 * @dropall by [MouseJstr] and [Xantara]
 * Drops all your possession on the ground based on item type
 *------------------------------------------*/
ACMD_FUNC(dropall)
{
	int8 type = -1;
	uint16 i, count = 0, count2 = 0;
	struct item_data *item_data = NULL;

	nullpo_retr(-1, sd);

	if (message[0]) {
		type = atoi(message);
		if (type != -1 && type != IT_HEALING && type != IT_USABLE && type != IT_ETC && type != IT_WEAPON &&
			type != IT_ARMOR && type != IT_CARD && type != IT_PETEGG && type != IT_PETARMOR && type != IT_AMMO)
		{
			clif_displaymessage(fd, msg_txt(1506)); // Usage: @dropall {<type>}
			clif_displaymessage(fd, msg_txt(1507)); // Type List: (default) all = -1, healing = 0, usable = 2, etc = 3, armor = 4, weapon = 5, card = 6, petegg = 7, petarmor = 8, ammo = 10
			return -1;
		}
	}

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].amount) {
			if (!(item_data = itemdb_exists(sd->inventory.u.items_inventory[i].nameid))) {
				ShowDebug("Non-existant item %d on dropall list (account_id: %d, char_id: %d)\n", sd->inventory.u.items_inventory[i].nameid, sd->status.account_id, sd->status.char_id);
				continue;
			}
			if (type == -1 || type == (uint8)item_data->type) {
				if (sd->inventory.u.items_inventory[i].equip != 0)
					pc_unequipitem(sd, i, 1|2);
				count += sd->inventory.u.items_inventory[i].amount;
				if (!pc_dropitem(sd, i, sd->inventory.u.items_inventory[i].amount))
					count2 += sd->inventory.u.items_inventory[i].amount;
			}
		}
	}
	count -= count2;
	sprintf(atcmd_output, msg_txt(1508), count, count2); // %d items are dropped (%d skipped)!
	clif_displaymessage(fd, atcmd_output); 
	return 0;
}

/*==========================================
 * @storeall by [MouseJstr]
 * Put everything into storage
 *------------------------------------------*/
ACMD_FUNC(storeall)
{
	int i;
	nullpo_retr(-1, sd);

	if (sd->state.storage_flag != 1) { //Open storage.
		if( storage_storageopen(sd) == 1 ) {
			clif_displaymessage(fd, msg_txt(1161)); // You currently cannot open your storage.
			return -1;
		}
	}

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].amount) {
			if(sd->inventory.u.items_inventory[i].equip != 0)
				pc_unequipitem(sd, i, 1|2);
			storage_storageadd(sd, &sd->storage, i, sd->inventory.u.items_inventory[i].amount);
		}
	}
	storage_storageclose(sd);

	clif_displaymessage(fd, msg_txt(1162)); // All items stored.
	return 0;
}

ACMD_FUNC(clearstorage)
{
	int i, j;
	nullpo_retr(-1, sd);

	if (sd->state.storage_flag == 1) {
		clif_displaymessage(fd, msg_txt(250)); // You have already opened your storage. Close it first.
		return -1;
	}

	if (sd->state.storage_flag == 3) {
		clif_displaymessage(fd, msg_txt(250)); // You have already opened your storage. Close it first.
		return -1;
	}

	j = sd->storage.amount;
	for (i = 0; i < j; ++i) {
		storage_delitem(sd, &sd->storage, i, sd->storage.u.items_storage[i].amount);
	}
	sd->state.storage_flag = 1;
	storage_storageclose(sd);

	clif_displaymessage(fd, msg_txt(1394)); // Your storage was cleaned.
	return 0;
}

ACMD_FUNC(cleargstorage)
{
	int i, j;
	struct guild *g;
	struct s_storage *gstorage;
	nullpo_retr(-1, sd);

	g = sd->guild;

	if (g == NULL) {
		clif_displaymessage(fd, msg_txt(43)); // You're not in a guild.
		return -1;
	}

	if (sd->state.storage_flag == 1) {
		clif_displaymessage(fd, msg_txt(250)); // You have already opened your storage. Close it first.
		return -1;
	}

	if (sd->state.storage_flag == 2) {
		clif_displaymessage(fd, msg_txt(251)); // You have already opened your guild storage. Close it first.
		return -1;
	}

	if (sd->state.storage_flag == 3) {
		clif_displaymessage(fd, msg_txt(250)); // You have already opened your storage. Close it first.
		return -1;
	}

	gstorage = guild2storage2(sd->status.guild_id);
	if (gstorage == NULL) // Doesn't have opened @gstorage yet, so we skip the deletion since *shouldn't* have any item there.
		return -1;

	j = gstorage->amount;
	gstorage->lock = true; // Lock @gstorage: do not allow any item to be retrieved or stored from any guild member
	for (i = 0; i < j; ++i)
		storage_guild_delitem(sd, gstorage, i, gstorage->u.items_guild[i].amount);
	storage_guild_storageclose(sd);
	gstorage->lock = false; // Cleaning done, release lock

	clif_displaymessage(fd, msg_txt(1395)); // Your guild storage was cleaned.
	return 0;
}

ACMD_FUNC(clearcart)
{
	int i;

	nullpo_retr(-1, sd);

	if (!pc_iscarton(sd)) {
		clif_displaymessage(fd, msg_txt(1396)); // You do not have a cart to be cleaned.
		return -1;
	}

	if (sd->state.vending == 1) {
		clif_displaymessage(fd, msg_txt(547)); // You can't clean a cart while vending!
		return -1;
	}

	 for (i = 0; i < MAX_CART; i++) {
		if (sd->cart.u.items_cart[i].nameid > 0)
			pc_cart_delitem(sd, i, sd->cart.u.items_cart[i].amount, 1, LOG_TYPE_OTHER);
	 }

	clif_clearcart(fd);
	clif_updatestatus(sd,SP_CARTINFO);

	clif_displaymessage(fd, msg_txt(1397)); // Your cart was cleaned.
	return 0;
}

/*==========================================
 * @skillid by [MouseJstr]
 * lookup a skill by name
 *------------------------------------------*/
#define MAX_SKILLID_PARTIAL_RESULTS 5
#define MAX_SKILLID_PARTIAL_RESULTS_LEN 74 /* "skill " (6) + "%d:" (up to 5) + "%s" (up to 30) + " (%s)" (up to 33) */
ACMD_FUNC(skillid)
{
	int skillen, i, found = 0;
	DBIterator *iter;
	DBKey key;
	DBData *data;
	char partials[MAX_SKILLID_PARTIAL_RESULTS][MAX_SKILLID_PARTIAL_RESULTS_LEN];

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1163)); // Please enter a skill name to look up (usage: @skillid <skill name>).
		return -1;
	}

	skillen = strlen(message);

	iter = db_iterator(skilldb_name2id);

	for (data = iter->first(iter,&key); iter->exists(iter); data = iter->next(iter,&key)) {
		uint16 idx = skill_get_index(db_data2i(data));

		if (strnicmp(key.str, message, skillen) == 0 || strnicmp(skill_db[idx].desc, message, skillen) == 0) {
			sprintf(atcmd_output, msg_txt(1164), db_data2i(data), skill_db[idx].desc, key.str); // skill %d: %s (%s)
			clif_displaymessage(fd, atcmd_output);
		} else if (found < MAX_SKILLID_PARTIAL_RESULTS && (stristr(key.str,message) || stristr(skill_db[idx].desc,message))) {
			snprintf(partials[found], MAX_SKILLID_PARTIAL_RESULTS_LEN, msg_txt(1164), db_data2i(data), skill_db[idx].desc, key.str); // skill %d: %s (%s)
			found++;
		}
	}

	dbi_destroy(iter);

	if (found) {
		sprintf(atcmd_output, msg_txt(1398), found); // -- Displaying first %d partial matches
		clif_displaymessage(fd, atcmd_output);
	}

	for (i = 0; i < found; i++) /* Partials */
		clif_displaymessage(fd, partials[i]);

	return 0;
}

/*==========================================
 * @useskill by [MouseJstr]
 * A way of using skills without having to find them in the skills menu
 *------------------------------------------*/
ACMD_FUNC(useskill)
{
	struct map_session_data *pl_sd = NULL;
	struct block_list *bl;
	uint16 skill_id;
	uint16 skill_lv;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%6hu %6hu %23[^\n]", &skill_id, &skill_lv, atcmd_player_name) != 3) {
		clif_displaymessage(fd, msg_txt(1165)); // Usage: @useskill <skill ID> <skill level> <char name>
		return -1;
	}

	if (!strcmp(atcmd_player_name,"self"))
		pl_sd = sd; // Quick keyword
	else if (!(pl_sd = map_nick2sd(atcmd_player_name))) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) {
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
		return -1;
	}

	if (skill_id >= HM_SKILLBASE && skill_id < HM_SKILLBASE + MAX_HOMUNSKILL && hom_is_active(sd->hd)) // (If used with @useskill, put the homunc as dest)
		bl = &sd->hd->bl;
	else
		bl = &sd->bl;

	pc_delinvincibletimer(sd);

	if (skill_get_inf(skill_id)&INF_GROUND_SKILL)
		unit_skilluse_pos(bl, pl_sd->bl.x, pl_sd->bl.y, skill_id, skill_lv);
	else
		unit_skilluse_id(bl, pl_sd->bl.id, skill_id, skill_lv);

	return 0;
}

/*==========================================
 * @displayskill by [Skotlex]
 *  Debug command to locate new skill IDs. It sends the
 *  three possible skill-effect packets to the area.
 *------------------------------------------*/
ACMD_FUNC(displayskill)
{
	struct status_data *status;
	unsigned int tick;
	uint16 skill_id;
	uint16 skill_lv = 1;
	short type;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%hu %hu %hd", &skill_id, &skill_lv, &type) < 1) {
		clif_displaymessage(fd, msg_txt(1166)); // Usage: @displayskill <skill ID> {<skill level>} {<type>}
		return -1;
	}
	status = status_get_status_data(&sd->bl);
	tick = gettick();
	switch (type) {
		case 1:
			clif_skill_damage(&sd->bl, &sd->bl, tick, status->amotion, status->dmotion, 1, 1, skill_id, skill_lv, DMG_SPLASH);
			break;
		case 2:
			clif_skill_poseffect(&sd->bl, skill_id, skill_lv, sd->bl.x, sd->bl.y, tick);
			break;
		default:
			clif_skill_nodamage(&sd->bl, &sd->bl, skill_id, skill_lv, 1);
			break;
	}
	return 0;
}

/*==========================================
 * @skilltree by [MouseJstr]
 * prints the skill tree for a player required to get to a skill
 *------------------------------------------*/
ACMD_FUNC(skilltree)
{
	struct map_session_data *pl_sd = NULL;
	uint16 skill_id;
	int meets, j, c = 0;
	struct skill_tree_entry *ent;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%6hu %23[^\n]", &skill_id, atcmd_player_name) != 2) {
		clif_displaymessage(fd, msg_txt(1167)); // Usage: @skilltree <skill ID> <char name>
		return -1;
	}

	if (!(pl_sd = map_nick2sd(atcmd_player_name))) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	c = pc_calc_skilltree_normalize_job(pl_sd);
	c = pc_mapid2jobid(c, pl_sd->status.sex);

	sprintf(atcmd_output, msg_txt(1168), job_name(c), pc_checkskill(pl_sd, NV_BASIC)); // Player is using %s skill tree (%d basic points).
	clif_displaymessage(fd, atcmd_output);

	ARR_FIND(0, MAX_SKILL_TREE, j, (!skill_tree[c][j].id || skill_tree[c][j].id == skill_id));
	if (j == MAX_SKILL_TREE || !skill_tree[c][j].id) {
		clif_displaymessage(fd, msg_txt(1169)); // The player cannot use that skill.
		return 0;
	}

	ent = &skill_tree[c][j];

	meets = 1;
	for (j = 0; j < MAX_PC_SKILL_REQUIRE; j++) {
		if (ent->need[j].id && pc_checkskill(sd,ent->need[j].id) < ent->need[j].lv) {
			sprintf(atcmd_output, msg_txt(1170), ent->need[j].lv, skill_db[ent->need[j].id].desc); // Player requires level %d of skill %s.
			clif_displaymessage(fd, atcmd_output);
			meets = 0;
		}
	}
	if (meets == 1)
		clif_displaymessage(fd, msg_txt(1171)); // The player meets all the requirements for that skill.

	return 0;
}

// Hand a ring with partners name on it to this char
void getring(struct map_session_data *sd)
{
	char flag = 0;
	unsigned short item_id;
	struct item item_tmp;

	item_id = (sd->status.sex ? WEDDING_RING_M : WEDDING_RING_F);
	memset(&item_tmp, 0, sizeof(item_tmp));
	item_tmp.nameid = item_id;
	item_tmp.identify = 1;
	item_tmp.card[0] = 255;
	item_tmp.card[2] = sd->status.partner_id;
	item_tmp.card[3] = sd->status.partner_id>>16;

	if ((flag = pc_additem(sd,&item_tmp,1,LOG_TYPE_COMMAND))) {
		clif_additem(sd,0,0,flag);
		map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,4,0);
	}
}

/*==========================================
 * @marry by [MouseJstr], fixed by Lupus
 * Marry two players
 *------------------------------------------*/
ACMD_FUNC(marry)
{
	struct map_session_data *pl_sd = NULL;

	nullpo_retr(-1, sd);
	
	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(1172)); // Usage: @marry <char name>
		return -1;
	}

	if (!(pl_sd = map_nick2sd(atcmd_player_name))) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_marriage(sd, pl_sd)) {
		clif_displaymessage(fd, msg_txt(1173)); // They are married... wish them well.
		clif_wedding_effect(&pl_sd->bl); //wedding effect and music [Lupus]
		if (pl_sd->bl.m != sd->bl.m)
			clif_wedding_effect(&sd->bl);
		getring(sd); // Auto-give named rings (Aru)
		getring(pl_sd);
		return 0;
	}

	clif_displaymessage(fd, msg_txt(1174)); // The two cannot wed because one is either a baby or already married.
	return -1;
}

/*==========================================
 * @divorce by [MouseJstr], fixed by [Lupus]
 * divorce two players
 *------------------------------------------*/
ACMD_FUNC(divorce)
{
	nullpo_retr(-1, sd);

	if (!pc_divorce(sd)) {
		sprintf(atcmd_output, msg_txt(1175), sd->status.name); // '%s' is not married.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}
	
	sprintf(atcmd_output, msg_txt(1176), sd->status.name); // '%s' and his/her partner are now divorced.
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * @changelook by [Celest]
 * Recoded by [Rytech]
 *------------------------------------------*/
ACMD_FUNC(changelook)
{
	int type = 0, value = 0; //p = Position, v = Value
	int pos[7] = { LOOK_HEAD_TOP,LOOK_HEAD_MID,LOOK_HEAD_BOTTOM,LOOK_WEAPON,LOOK_SHIELD,LOOK_ROBE,LOOK_BODY2 };

	if( sscanf(message, "%d %d", &type, &value) != 2 || type < 1 || type > 7 || value < 0 ) {
		clif_displaymessage(fd, msg_txt(1177)); // Usage: @changelook {<position>} <view id>
		clif_displaymessage(fd, msg_txt(533));  // Position must be a number between 1 - 6 and view id must be 0 or higher.
		clif_displaymessage(fd, msg_txt(1178)); // Position: 1-Top 2-Middle 3-Bottom 4-Weapon 5-Shield 6-Robe 7-Body
		return -1;
	}
	//If the check passes, display the requested result on the character.
	clif_changelook(&sd->bl, pos[type - 1], value);
	return 0;
}

/*==========================================
 * @autotrade by durf [Lupus] [Paradox924X]
 * Turns on/off Autotrade for a specific player
 *------------------------------------------*/
ACMD_FUNC(autotrade)
{
	nullpo_retr(-1, sd);

	if( map[sd->bl.m].flag.autotrade != battle_config.autotrade_mapflag ) {
		clif_displaymessage(fd, msg_txt(1179)); // Autotrade is not allowed on this map.
		return -1;
	}

	if( pc_isdead(sd) ) {
		clif_displaymessage(fd, msg_txt(1180)); // You cannot autotrade when dead.
		return -1;
	}

	if( !sd->state.vending && !sd->state.buyingstore ) { // Check if player is vending or buying
		clif_displaymessage(fd, msg_txt(549)); // "You should have a shop open to use @autotrade."
		return -1;
	}

	sd->state.autotrade = 1;

	if( battle_config.autotrade_monsterignore )
		sd->state.monster_ignore = 1;

	if( sd->state.vending ) {
		if( Sql_Query(mmysql_handle, "UPDATE `%s` SET `autotrade` = 1 WHERE `id` = %d;", vendings_db, sd->vender_id) != SQL_SUCCESS )
			Sql_ShowDebug(mmysql_handle);
	} else if( sd->state.buyingstore ) {
		if( Sql_Query(mmysql_handle, "UPDATE `%s` SET `autotrade` = 1 WHERE `id` = %d;", buyingstores_db, sd->buyer_id) != SQL_SUCCESS )
			Sql_ShowDebug(mmysql_handle);
	}

	if( battle_config.at_timeout ) {
		int timeout = atoi(message);

		sc_start(NULL, &sd->bl, SC_AUTOTRADE, 100, 0, (timeout > 0 ? min(timeout,battle_config.at_timeout) : battle_config.at_timeout) * 60000);
	}

	channel_pcquit(sd, 0xF); // Leave all channels.
	clif_authfail_fd(sd->fd, 15);
	chrif_save(sd, CSAVE_AUTOTRADE);

	return 0;
}

/*==========================================
 * @changegm by durf (changed by Lupus)
 * Changes Master of your Guild to a specified guild member
 *------------------------------------------*/
ACMD_FUNC(changegm)
{
	struct guild *g;
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if( !sd->status.guild_id || !(g = sd->guild) || strcmp(g->master, sd->status.name) ) {
		clif_displaymessage(fd, msg_txt(1181)); // You need to be a Guild Master to use this command.
		return -1;
	}

	if( map[sd->bl.m].flag.guildlock || map[sd->bl.m].flag.gvg_castle ) {
		clif_displaymessage(fd, msg_txt(1182)); // You cannot change guild leaders on this map.
		return -1;
	}

	if( !message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1 ) {
		clif_displaymessage(fd, msg_txt(1183)); // Usage: @changegm <guild_member_name>
		return -1;
	}

	if( !(pl_sd = map_nick2sd(atcmd_player_name)) || pl_sd->status.guild_id != sd->status.guild_id ) {
		clif_displaymessage(fd, msg_txt(1184)); // Target character must be online and be a guild member.
		return -1;
	}

	guild_gm_change(sd->status.guild_id, pl_sd->status.char_id);
	return 0;
}

/*==========================================
 * @changeleader by Skotlex
 * Changes the leader of a party.
 *------------------------------------------*/
ACMD_FUNC(changeleader)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if( !message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1 ) {
		clif_displaymessage(fd, msg_txt(1185)); // Usage: @changeleader <party_member_name>
		return -1;
	}

	party_changeleader(sd, map_nick2sd(atcmd_player_name), NULL);
	return 0;
}

/*==========================================
 * @partyoption by Skotlex
 * Used to change the item share setting of a party.
 *------------------------------------------*/
ACMD_FUNC(partyoption)
{
	struct party_data *p;
	int mi, option;
	char w1[16], w2[16];
	nullpo_retr(-1, sd);

	if (sd->status.party_id == 0 || (p = party_search(sd->status.party_id)) == NULL) {
		clif_displaymessage(fd, msg_txt(282)); // You need to be a party leader to use this command.
		return -1;
	}

	ARR_FIND(0, MAX_PARTY, mi, p->data[mi].sd == sd);
	if (mi == MAX_PARTY)
		return -1; //Shouldn't happen

	if (!p->party.member[mi].leader) {
		clif_displaymessage(fd, msg_txt(282)); // You need to be a party leader to use this command.
		return -1;
	}

	if(!message || !*message || sscanf(message, "%15s %15s", w1, w2) < 2) {
		clif_displaymessage(fd, msg_txt(1186)); // Usage: @partyoption <pickup share: yes/no> <item distribution: yes/no>
		return -1;
	}
	
	option = (config_switch(w1) ? 1 : 0)|(config_switch(w2) ? 2 : 0);

	//Change item share type.
	if (option != p->party.item)
		party_changeoption(sd, p->party.exp, option);
	else
		clif_displaymessage(fd, msg_txt(286)); // There's been no change in the setting.

	return 0;
}

/*==========================================
 * @autoloot by Upa-Kun
 * Turns on/off AutoLoot for a specific player
 *------------------------------------------*/
ACMD_FUNC(autoloot)
{
	int rate;

	nullpo_retr(-1, sd);

	// Autoloot command without value
	if (!message || !*message) {
		if (sd->state.autoloot)
			rate = 0;
		else
			rate = 10000;
	} else {
		double drate;

		drate = atof(message);
		rate = (int)(drate * 100);
	}
	if (rate < 0)
		rate = 0;
	if (rate > 10000)
		rate = 10000;
	sd->state.autoloot = rate;
	if (sd->state.autoloot) {
		snprintf(atcmd_output, sizeof atcmd_output, msg_txt(1187),((double)sd->state.autoloot) / 100.); // Autolooting items with drop rates of %0.02f%% and below.
		clif_displaymessage(fd, atcmd_output);
	} else
		clif_displaymessage(fd, msg_txt(1188)); // Autoloot is now off.

	return 0;
}

/*==========================================
 * @alootid
 *------------------------------------------*/
ACMD_FUNC(autolootitem)
{
	struct item_data *item_data = NULL;
	int i;
	int action = 3; // 1 = add, 2 = remove, 3 = help + list (default), 4 = reset

	nullpo_retr(-1, sd);

	if (message && *message) {
		if (message[0] == '+') {
			message++;
			action = 1;
		} else if (message[0] == '-') {
			message++;
			action = 2;
		} else if (!strcmp(message, "reset"))
			action = 4;
	}

	if (action < 3) { // Add or remove
		if ((item_data = itemdb_exists(atoi(message))) == NULL)
			item_data = itemdb_searchname(message);
		if (!item_data) {
			// No items founds in the DB with Id or Name
			clif_displaymessage(fd, msg_txt(1189)); // Item not found.
			return -1;
		}
	}

	switch (action) {
		case 1:
			ARR_FIND(0, AUTOLOOTITEM_SIZE, i, sd->state.autolootid[i] == item_data->nameid);
			if (i != AUTOLOOTITEM_SIZE) {
				clif_displaymessage(fd, msg_txt(1190)); // You're already autolooting this item.
				return -1;
			}
			ARR_FIND(0, AUTOLOOTITEM_SIZE, i, sd->state.autolootid[i] == 0);
			if (i == AUTOLOOTITEM_SIZE) {
				clif_displaymessage(fd, msg_txt(1191)); // Your autolootitem list is full. Remove some items first with @autolootid -<item name or ID>.
				return -1;
			}
			sd->state.autolootid[i] = item_data->nameid; // Autoloot Activated
			sprintf(atcmd_output, msg_txt(1192), item_data->name, item_data->jname, item_data->nameid); // Autolooting item: '%s'/'%s' {%d}
			clif_displaymessage(fd, atcmd_output);
			sd->state.autolooting = 1;
			break;
		case 2:
			ARR_FIND(0, AUTOLOOTITEM_SIZE, i, sd->state.autolootid[i] == item_data->nameid);
			if (i == AUTOLOOTITEM_SIZE) {
				clif_displaymessage(fd, msg_txt(1193)); // You're currently not autolooting this item.
				return -1;
			}
			sd->state.autolootid[i] = 0;
			sprintf(atcmd_output, msg_txt(1194), item_data->name, item_data->jname, item_data->nameid); // Removed item: '%s'/'%s' {%d} from your autolootitem list.
			clif_displaymessage(fd, atcmd_output);
			ARR_FIND(0, AUTOLOOTITEM_SIZE, i, sd->state.autolootid[i] != 0);
			if (i == AUTOLOOTITEM_SIZE) {
				sd->state.autolooting = 0;
			}
			break;
		case 3:
			sprintf(atcmd_output, msg_txt(1195), AUTOLOOTITEM_SIZE); // You can have %d items on your autolootitem list.
			clif_displaymessage(fd, atcmd_output);
			clif_displaymessage(fd, msg_txt(1196)); // To add an item to the list, use "@alootid +<item name or ID>". To remove an item, use "@alootid -<item name or ID>".
			clif_displaymessage(fd, msg_txt(1197)); // "@alootid reset" will clear your autolootitem list.
			ARR_FIND(0, AUTOLOOTITEM_SIZE, i, sd->state.autolootid[i] != 0);
			if (i == AUTOLOOTITEM_SIZE) {
				clif_displaymessage(fd, msg_txt(1198)); // Your autolootitem list is empty.
			} else {
				clif_displaymessage(fd, msg_txt(1199)); // Items on your autolootitem list:
				for (i = 0; i < AUTOLOOTITEM_SIZE; i++) {
					if (sd->state.autolootid[i] == 0)
						continue;
					if (!(item_data = itemdb_exists(sd->state.autolootid[i]))) {
						ShowDebug("Non-existant item %d on autolootitem list (account_id: %d, char_id: %d)", sd->state.autolootid[i], sd->status.account_id, sd->status.char_id);
						continue;
					}
					sprintf(atcmd_output, "'%s'/'%s' {%hu}", item_data->name, item_data->jname, item_data->nameid);
					clif_displaymessage(fd, atcmd_output);
				}
			}
			break;
		case 4:
			memset(sd->state.autolootid, 0, sizeof(sd->state.autolootid));
			clif_displaymessage(fd, msg_txt(1200)); // Your autolootitem list has been reset.
			sd->state.autolooting = 0;
			break;
	}
	return 0;
}

/*==========================================
 * @autoloottype
 * Flags:
 * 1:   IT_HEALING,  2:   IT_UNKNOWN,  4:    IT_USABLE, 8:    IT_ETC,
 * 16:  IT_ARMOR,    32:  IT_WEAPON,   64:   IT_CARD,   128:  IT_PETEGG,
 * 256: IT_PETARMOR, 512: IT_UNKNOWN2, 1024: IT_AMMO,   2048: IT_RESTRICTEDCONSUME
 * 262144: IT_CASH
 *------------------------------------------
 * Credits:
 *    chriser
 *    Aleos
 *------------------------------------------*/
ACMD_FUNC(autoloottype)
{
	uint8 i = 0, action = 3; // 1 = add, 2 = remove, 3 = help + list (default), 4 = reset
	enum item_types type = -1;
	int ITEM_NONE = 0, ITEM_MAX = 1533;

	nullpo_retr(-1, sd);

	if (message && *message) {
		if (message[0] == '+') {
			message++;
			action = 1;
		} else if (message[0] == '-') {
			message++;
			action = 2;
		} else if (!strcmp(message, "reset"))
			action = 4;
	}

	if (action < 3) { // Add or remove
		if ((strncmp(message, "healing", 3) == 0) || (atoi(message) == 0))
			type = IT_HEALING;
		else if ((strncmp(message, "usable", 3) == 0) || (atoi(message) == 2))
			type = IT_USABLE;
		else if ((strncmp(message, "etc", 3) == 0) || (atoi(message) == 3))
			type = IT_ETC;
		else if ((strncmp(message, "armor", 3) == 0) || (atoi(message) == 4))
			type = IT_ARMOR;
		else if ((strncmp(message, "weapon", 3) == 0) || (atoi(message) == 5))
			type = IT_WEAPON;
		else if ((strncmp(message, "card", 3) == 0) || (atoi(message) == 6))
			type = IT_CARD;
		else if ((strncmp(message, "petegg", 4) == 0) || (atoi(message) == 7))
			type = IT_PETEGG;
		else if ((strncmp(message, "petarmor", 4) == 0) || (atoi(message) == 8))
			type = IT_PETARMOR;
		else if ((strncmp(message, "ammo", 3) == 0) || (atoi(message) == 10))
			type = IT_AMMO;
		else {
			clif_displaymessage(fd, msg_txt(1494)); // Item type not found.
			return -1;
		}
	}

	switch (action) {
		case 1:
			if (sd->state.autoloottype&(1<<type)) {
				clif_displaymessage(fd, msg_txt(1495)); // You're already autolooting this item type.
				return -1;
			}
			if (sd->state.autoloottype == ITEM_MAX) {
				clif_displaymessage(fd, msg_txt(1496)); // Your autoloottype list has all item types. You can remove some items with @autoloottype -<type name or ID>.
				return -1;
			}
			sd->state.autolootingtype = 1; // Autoloot Activated
			sd->state.autoloottype |= (1<<type); // Stores the type
			sprintf(atcmd_output, msg_txt(1497), itemdb_typename(type), type); // Autolooting item type: '%s' {%d}
			clif_displaymessage(fd, atcmd_output);
			break;
		case 2:
			if (!(sd->state.autoloottype&(1<<type))) {
				clif_displaymessage(fd, msg_txt(1498)); // You're currently not autolooting this item type.
				return -1;
			}
			sd->state.autoloottype &= ~(1<<type);
			sprintf(atcmd_output, msg_txt(1499), itemdb_typename(type), type); // Removed item type: '%s' {%d} from your autoloottype list.
			clif_displaymessage(fd, atcmd_output);
			if (sd->state.autoloottype == ITEM_NONE)
				sd->state.autolootingtype = 0;
			break;
		case 3:
			clif_displaymessage(fd, msg_txt(1500)); // To add an item type to the list, use "@aloottype +<type name or ID>". To remove an item type, use "@aloottype -<type name or ID>".
			clif_displaymessage(fd, msg_txt(1501)); // Type List: healing = 0, usable = 2, etc = 3, armor = 4, weapon = 5, card = 6, petegg = 7, petarmor = 8, ammo = 10
			clif_displaymessage(fd, msg_txt(1502)); // "@aloottype reset" will clear your autoloottype list.
			if (sd->state.autoloottype == ITEM_NONE)
				clif_displaymessage(fd, msg_txt(1503)); // Your autoloottype list is empty.
			else {
				clif_displaymessage(fd, msg_txt(1504)); // Item types on your autoloottype list:
				while (i < IT_MAX) {
					if (sd->state.autoloottype&(1<<i)) {
						sprintf(atcmd_output, "  '%s' {%d}", itemdb_typename(i), i);
						clif_displaymessage(fd, atcmd_output);
					}
					i++;
				}
			}
			break;
		case 4:
			sd->state.autoloottype = ITEM_NONE;
			sd->state.autolootingtype = 0;
			clif_displaymessage(fd, msg_txt(1505)); // Your autoloottype list has been reset.
			break;
	}
	return 0;
}

/**
 * No longer available, keeping here just in case it's back someday. [Ind]
 */
/*==========================================
 * It is made to rain.
 *------------------------------------------*/
//ACMD_FUNC(rain)
//{
//	nullpo_retr(-1, sd);
//	if (map[sd->bl.m].flag.rain) {
//		map[sd->bl.m].flag.rain = 0;
//		clif_weather(sd->bl.m);
//		clif_displaymessage(fd, msg_txt(1201)); // The rain has stopped.
//	} else {
//		map[sd->bl.m].flag.rain = 1;
//		clif_weather(sd->bl.m);
//		clif_displaymessage(fd, msg_txt(1202)); // It has started to rain.
//	}
//	return 0;
//}

/*==========================================
 * It is made to snow.
 *------------------------------------------*/
ACMD_FUNC(snow)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.snow) {
		map[sd->bl.m].flag.snow = 0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1203)); // Snow has stopped falling.
	} else {
		map[sd->bl.m].flag.snow = 1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1204)); // It has started to snow.
	}

	return 0;
}

/*==========================================
 * Cherry tree snowstorm is made to fall. (Sakura)
 *------------------------------------------*/
ACMD_FUNC(sakura)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.sakura) {
		map[sd->bl.m].flag.sakura = 0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1205)); // Cherry tree leaves no longer fall.
	} else {
		map[sd->bl.m].flag.sakura = 1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1206)); // Cherry tree leaves have begun to fall.
	}

	return 0;
}

/*==========================================
 * Clouds appear.
 *------------------------------------------*/
ACMD_FUNC(clouds)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.clouds) {
		map[sd->bl.m].flag.clouds = 0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1207)); // The clouds has disappear.
	} else {
		map[sd->bl.m].flag.clouds = 1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1208)); // Clouds appear.
	}

	return 0;
}

/*==========================================
 * Different type of clouds using effect 516
 *------------------------------------------*/
ACMD_FUNC(clouds2)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.clouds2) {
		map[sd->bl.m].flag.clouds2 = 0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1209)); // The alternative clouds disappear.
	} else {
		map[sd->bl.m].flag.clouds2 = 1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1210)); // Alternative clouds appear.
	}

	return 0;
}

/*==========================================
 * Fog hangs over.
 *------------------------------------------*/
ACMD_FUNC(fog)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.fog) {
		map[sd->bl.m].flag.fog = 0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1211)); // The fog has gone.
	} else {
		map[sd->bl.m].flag.fog = 1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1212)); // Fog hangs over.
	}

	return 0;
}

/*==========================================
 * Fallen leaves fall.
 *------------------------------------------*/
ACMD_FUNC(leaves)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.leaves) {
		map[sd->bl.m].flag.leaves = 0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1213)); // Leaves no longer fall.
	} else {
		map[sd->bl.m].flag.leaves = 1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1214)); // Fallen leaves fall.
	}

	return 0;
}

/*==========================================
 * Fireworks appear.
 *------------------------------------------*/
ACMD_FUNC(fireworks)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.fireworks) {
		map[sd->bl.m].flag.fireworks = 0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1215)); // Fireworks have ended.
	} else {
		map[sd->bl.m].flag.fireworks = 1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, msg_txt(1216)); // Fireworks have launched.
	}

	return 0;
}

/*==========================================
 * Clearing Weather Effects by Dexity
 *------------------------------------------*/
ACMD_FUNC(clearweather)
{
	nullpo_retr(-1, sd);

	map[sd->bl.m].flag.snow = 0;
	map[sd->bl.m].flag.sakura = 0;
	map[sd->bl.m].flag.clouds = 0;
	map[sd->bl.m].flag.clouds2 = 0;
	map[sd->bl.m].flag.fog = 0;
	map[sd->bl.m].flag.fireworks = 0;
	map[sd->bl.m].flag.leaves = 0;
	clif_weather(sd->bl.m);
	clif_displaymessage(fd, msg_txt(291)); // Weather effects will dispell on warp/refresh

	return 0;
}

/*===============================================================
 * Sound Command - plays a sound for everyone around! [Codemaster]
 *---------------------------------------------------------------*/
ACMD_FUNC(sound)
{
	char sound_file[100];

	memset(sound_file, '\0', sizeof(sound_file));

		if(!message || !*message || sscanf(message, "%99[^\n]", sound_file) < 1) {
		clif_displaymessage(fd, msg_txt(1217)); // Please enter a sound filename (usage: @sound <filename>).
		return -1;
	}

	if(strstr(sound_file, ".wav") == NULL)
		strcat(sound_file, ".wav");

	clif_soundeffectall(&sd->bl, sound_file, 0, AREA);

	return 0;
}

/*==========================================
 *	MOB Search
 *------------------------------------------*/
ACMD_FUNC(mobsearch)
{
	char mob_name[100];
	int mob_id;
	int number = 0;
	struct s_mapiterator *it;

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%99[^\n]", mob_name) < 1) {
		clif_displaymessage(fd, msg_txt(1218)); // Please enter a monster name (usage: @mobsearch <monster name>).
		return -1;
	}

	if (!(mob_id = atoi(mob_name)))
		mob_id = mobdb_searchname(mob_name);

	if (!mobdb_checkid(mob_id)) {
		snprintf(atcmd_output, sizeof atcmd_output, msg_txt(1219),mob_name); // Invalid mob ID %s!
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	strcpy(mob_name,mob_db(mob_id)->jname);	// --ja--
//	strcpy(mob_name,mob_db(mob_id)->name);	// --en--

	snprintf(atcmd_output, sizeof atcmd_output, msg_txt(1220), mob_name, mapindex_id2name(sd->mapindex)); // Mob Search... %s %s
	clif_displaymessage(fd, atcmd_output);

	it = mapit_geteachmob();
	for (;;) {
		TBL_MOB *md = (TBL_MOB *)mapit_next(it);

		if (!md)
			break; //No more mobs
		if (md->bl.m != sd->bl.m)
			continue;
		if (md->mob_id != mob_id)
			continue;
		++number;
		if (md->spawn_timer == INVALID_TIMER)
			snprintf(atcmd_output, sizeof(atcmd_output), "%2d[%3d:%3d] %s", number, md->bl.x, md->bl.y, md->name);
		else
			snprintf(atcmd_output, sizeof(atcmd_output), "%2d[%s] %s", number, "dead", md->name);
		clif_displaymessage(fd, atcmd_output);
	}
	mapit_free(it);

	return 0;
}

/*==========================================
 * @cleanmap - cleans items on the ground
 * @cleanarea - cleans items on the ground within an specified area
 *------------------------------------------*/
static int atcommand_cleanfloor_sub(struct block_list *bl, va_list ap)
{
	nullpo_ret(bl);
	map_clearflooritem(bl);

	return 0;
}

ACMD_FUNC(cleanmap)
{
	map_foreachinmap(atcommand_cleanfloor_sub, sd->bl.m, BL_ITEM);
	clif_displaymessage(fd, msg_txt(1221)); // All dropped items have been cleaned up.
	return 0;
}

ACMD_FUNC(cleanarea)
{
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	
	if (!message || !*message || sscanf(message, "%d %d %d %d", &x0, &y0, &x1, &y1) < 1) {
		map_foreachinallarea(atcommand_cleanfloor_sub, sd->bl.m, sd->bl.x - (AREA_SIZE * 2), sd->bl.y - (AREA_SIZE * 2), sd->bl.x + (AREA_SIZE * 2), sd->bl.y + (AREA_SIZE * 2), BL_ITEM);
	}
	else if (sscanf(message, "%d %d %d %d", &x0, &y0, &x1, &y1) == 1) {
		map_foreachinallarea(atcommand_cleanfloor_sub, sd->bl.m, sd->bl.x - x0, sd->bl.y - x0, sd->bl.x + x0, sd->bl.y + x0, BL_ITEM);
	}
	else if (sscanf(message, "%d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
		map_foreachinallarea(atcommand_cleanfloor_sub, sd->bl.m, x0, y0, x1, y1, BL_ITEM);
	}
	
	clif_displaymessage(fd, msg_txt(1221)); // All dropped items have been cleaned up.
	return 0;
}

/*==========================================
 * make a NPC/PET talk
 * @npctalkc [SnakeDrak]
 *------------------------------------------*/
ACMD_FUNC(npctalk)
{
	char name[NAME_LENGTH],mes[100],temp[100];
	struct npc_data *nd;
	bool ifcolor = (*(command + 8) != 'c' && *(command + 8) != 'C') ? 0 : 1;
	unsigned long color = 0;

	if (sd->sc.cant.chat)
		return -1; // No "chatting" while muted

	if (!ifcolor) {
		if (!message || !*message || sscanf(message, "%23[^,], %99[^\n]", name, mes) < 2) {
			clif_displaymessage(fd, msg_txt(1222)); // Please enter the correct parameters (usage: @npctalk <npc name>, <message>).
			return -1;
		}
	} else {
		if (!message || !*message || sscanf(message, "%lx %23[^,], %99[^\n]", &color, name, mes) < 3) {
			clif_displaymessage(fd, msg_txt(1223)); // Please enter the correct parameters (usage: @npctalkc <color> <npc name>, <message>).
			return -1;
		}
	}

	if (!(nd = npc_name2id(name))) {
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist
		return -1;
	}

	strtok(name, "#"); // Discard extra name identifier if present
	snprintf(temp, sizeof(temp), "%s : %s", name, mes);

	if (ifcolor) clif_messagecolor(&nd->bl, color, temp, true, AREA_CHAT_WOC);
	else clif_disp_overhead(&nd->bl, temp);

	return 0;
}

ACMD_FUNC(pettalk)
{
	char mes[100],temp[100];
	struct pet_data *pd;

	nullpo_retr(-1, sd);

	if ( battle_config.min_chat_delay ) {
		if( DIFF_TICK(sd->cantalk_tick, gettick()) > 0 )
			return 0;
		sd->cantalk_tick = gettick() + battle_config.min_chat_delay;
	}

	if(!sd->status.pet_id || !(pd = sd->pd)) {
		clif_displaymessage(fd, msg_txt(184)); // Sorry, but you have no pet.
		return -1;
	}

	if (sd->sc.cant.chat)
		return -1; //no "chatting" while muted.

	if (!message || !*message || sscanf(message, "%99[^\n]", mes) < 1) {
		clif_displaymessage(fd, msg_txt(1224)); // Please enter a message (usage: @pettalk <message>).
		return -1;
	}

	if (message[0] == '/') { // pet emotion processing
		const char *emo[] = {
			"/!", "/?", "/ho", "/lv", "/swt", "/ic", "/an", "/ag", "/$", "/...",
			"/scissors", "/rock", "/paper", "/korea", "/lv2", "/thx", "/wah", "/sry", "/heh", "/swt2",
			"/hmm", "/no1", "/??", "/omg", "/O", "/X", "/hlp", "/go", "/sob", "/gg",
			"/kis", "/kis2", "/pif", "/ok", "-?-", "/indonesia", "/bzz", "/rice", "/awsm", "/meh",
			"/shy", "/pat", "/mp", "/slur", "/com", "/yawn", "/grat", "/hp", "/philippines", "/malaysia",
			"/singapore", "/brazil", "/fsh", "/spin", "/sigh", "/dum", "/crwd", "/desp", "/dice", "-dice2",
			"-dice3", "-dice4", "-dice5", "-dice6", "/india", "/love", "/russia", "-?-", "/mobile", "/mail",
			"/chinese", "/antenna1", "/antenna2", "/antenna3", "/hum", "/abs", "/oops", "/spit", "/ene", "/panic",
			"/whisp"
		};
		int i;

		ARR_FIND(0, ARRAYLENGTH(emo), i, stricmp(message, emo[i]) == 0);
		if( i == E_DICE1 )
			i = rnd()%6 + E_DICE1; // Randomize /dice
		if( i < ARRAYLENGTH(emo) ) {
			if (sd->emotionlasttime + 1 >= time(NULL)) { // Not more than 1 per second
					sd->emotionlasttime = time(NULL);
					return 0;
			}
			sd->emotionlasttime = time(NULL);
			
			clif_emotion(&pd->bl, i);
			return 0;
		}
	}

	snprintf(temp, sizeof temp ,"%s : %s", pd->pet.name, mes);
	clif_disp_overhead(&pd->bl, temp);

	return 0;
}

/// @users - displays the number of players present on each map (and percentage)
/// #users displays on the target user instead of self
ACMD_FUNC(users)
{
	char buf[CHAT_SIZE_MAX];
	int i;
	int users[MAX_MAPINDEX];
	int users_all;
	struct s_mapiterator *iter;

	memset(users, 0, sizeof(users));
	users_all = 0;

	// Count users on each map
	iter = mapit_getallusers();
	for( ;; ) {
		struct map_session_data *sd2 = (struct map_session_data *)mapit_next(iter);

		if( sd2 == NULL )
			break; // No more users

		if( sd2->mapindex >= MAX_MAPINDEX )
			continue; // Invalid mapindex

		if( users[sd2->mapindex] < INT_MAX ) ++users[sd2->mapindex];
		if( users_all < INT_MAX ) ++users_all;
	}
	mapit_free(iter);

	// Display results for each map
	for( i = 0; i < MAX_MAPINDEX; ++i ) {
		if( users[i] == 0 )
			continue; // Empty

		safesnprintf(buf, sizeof(buf), "%s: %d (%.2f%%)", mapindex_id2name(i), users[i], (float)(100.0f * users[i] / users_all));
		clif_displaymessage(sd->fd, buf);
	}

	// display overall count
	safesnprintf(buf, sizeof(buf), "all: %d", users_all);
	clif_displaymessage(sd->fd, buf);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(reset)
{
	pc_resetstate(sd);
	pc_resetskill(sd,1);
	sprintf(atcmd_output, msg_txt(208), sd->status.name); // '%s' skill and stats points reseted!
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(summon)
{
	char name[NAME_LENGTH];
	int mob_id = 0;
	int duration = 0;
	struct mob_data *md;
	unsigned int tick = gettick();

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%23s %d", name, &duration) < 1) {
		clif_displaymessage(fd, msg_txt(1225)); // Please enter a monster name (usage: @summon <monster name> {duration}).
		return -1;
	}

	if (duration < 1)
		duration = 1;
	else if (duration > 60)
		duration = 60;

	if ((mob_id = atoi(name)) == 0)
		mob_id = mobdb_searchname(name);
	if (mob_id == 0 || mobdb_checkid(mob_id) == 0) {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	md = mob_once_spawn_sub(&sd->bl, sd->bl.m, -1, -1, "--ja--", mob_id, "", SZ_SMALL, AI_NONE);

	if (!md)
		return -1;

	md->master_id = sd->bl.id;
	md->special_state.ai = AI_ATTACK;
	md->deletetimer = add_timer(tick + duration * 60000, mob_timer_delete, md->bl.id, 0);
	clif_specialeffect(&md->bl, EF_ENTRY2, AREA);
	mob_spawn(md);
	sc_start4(NULL, &md->bl, SC_MODECHANGE, 100, 1, 0, MD_AGGRESSIVE, 0, 60000);
	clif_skill_poseffect(&sd->bl, AM_CALLHOMUN, 1, md->bl.x, md->bl.y, tick);
	clif_displaymessage(fd, msg_txt(39)); // All monster summoned!

	return 0;
}

/*==========================================
 * @adjgroup
 * Temporarily move player to another group
 * Useful during beta testing to allow players to use GM commands for short periods of time
 *------------------------------------------*/
ACMD_FUNC(adjgroup)
{
	int new_group = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d", &new_group) != 1) {
		clif_displaymessage(fd, msg_txt(1226)); // Usage: @adjgroup <group_id>
		return -1;
	}

	if (!pc_group_exists(new_group)) {
		clif_displaymessage(fd, msg_txt(1227)); // Specified group does not exist.
		return -1;
	}

	sd->group_id = new_group;
	pc_group_pc_load(sd); // Update cache
	clif_displaymessage(fd, msg_txt(1228)); // Group changed successfully.
	clif_displaymessage(sd->fd, msg_txt(1229)); // Your group has changed.
	return 0;
}

/*==========================================
 * @trade by [MouseJstr]
 * Open a trade window with a remote player
 *------------------------------------------*/
ACMD_FUNC(trade)
{
    struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(1230)); // Please enter a player name (usage: @trade <char name>).
		return -1;
	}

	if ( (pl_sd = map_nick2sd(atcmd_player_name)) == NULL ) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	trade_traderequest(sd, pl_sd);
	return 0;
}

/*==========================================
 * @setbattleflag by [MouseJstr]
 * set a battle_config flag without having to reboot
 *------------------------------------------*/
ACMD_FUNC(setbattleflag)
{
	char flag[128], value[128];
	int reload = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%127s %127s %11d", flag, value, &reload) != 2) {
		clif_displaymessage(fd, msg_txt(1231)); // Usage: @setbattleflag <flag> <value> {<reload>}
		return -1;
	}

	if (battle_set_value(flag, value) == 0) {
		clif_displaymessage(fd, msg_txt(1232)); // Unknown battle_config flag.
		return -1;
	}

	clif_displaymessage(fd, msg_txt(1233)); // Set battle_config as requested.

	if (reload)
		mob_reload();

	return 0;
}

/*==========================================
 * @unmute [Valaris]
 *------------------------------------------*/
ACMD_FUNC(unmute)
{
	struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(1234)); // Please enter a player name (usage: @unmute <char name>).
		return -1;
	}

	if ( (pl_sd = map_nick2sd(atcmd_player_name)) == NULL ) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if(!pl_sd->sc.data[SC_NOCHAT]) {
		clif_displaymessage(sd->fd,msg_txt(1235)); // Player is not muted.
		return -1;
	}

	pl_sd->status.manner = 0;
	status_change_end(&pl_sd->bl, SC_NOCHAT, INVALID_TIMER);
	clif_displaymessage(sd->fd,msg_txt(1236)); // Player unmuted.
	
	return 0;
}

/*==========================================
 * @uptime by MC Cameri
 *------------------------------------------*/
ACMD_FUNC(uptime)
{
	unsigned long seconds = 0, day = 24 * 60 * 60, hour = 60 * 60,
		minute = 60, days = 0, hours = 0, minutes = 0;
	nullpo_retr(-1, sd);

	seconds = get_uptime();
	days = seconds / day;
	seconds -= (seconds / day > 0) ? (seconds / day) * day : 0;
	hours = seconds / hour;
	seconds -= (seconds / hour > 0) ? (seconds / hour) * hour : 0;
	minutes = seconds / minute;
	seconds -= (seconds / minute > 0) ? (seconds / minute) * minute : 0;

	snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(245), days, hours, minutes, seconds); // Server Uptime: %ld days, %ld hours, %ld minutes, %ld seconds.
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * @changesex
 * => Changes one's account sex. Switch from male to female or visversa
 *------------------------------------------*/
ACMD_FUNC(changesex)
{
	int i;
	nullpo_retr(-1, sd);

	pc_resetskill(sd,4);
	// To avoid any problem with equipment and invalid sex, equipment is unequiped.
	for( i = 0; i < EQI_MAX; i++ ) {
		if( sd->equip_index[i] >= 0 )
			pc_unequipitem(sd, sd->equip_index[i], 1|2);
	}
	chrif_changesex(sd, true);

	return 0;
}

/*==========================================
 * @changecharsex
 * => Changes one's character sex. Switch from male to female or visversa.
 *------------------------------------------*/
ACMD_FUNC(changecharsex)
{
	int i;
	nullpo_retr(-1, sd);

	pc_resetskill(sd,4);
	// To avoid any problem with equipment and invalid sex, equipment is unequiped.
	for( i = 0; i < EQI_MAX; i++ ) {
		if( sd->equip_index[i] >= 0 )
			pc_unequipitem(sd, sd->equip_index[i], 1|2);
	}
	chrif_changesex(sd, false);

	return 0;
}

/*================================================
 * @mute - Mutes a player for a set amount of time
 *------------------------------------------------*/
ACMD_FUNC(mute)
{
	struct map_session_data *pl_sd = NULL;
	int manner;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if( !message || !*message || sscanf(message, "%d %23[^\n]", &manner, atcmd_player_name) < 1 ) {
		clif_displaymessage(fd, msg_txt(1237)); // Usage: @mute <time> <char name>
		return -1;
	}

	if( (pl_sd = map_nick2sd(atcmd_player_name)) == NULL ) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if( pc_get_group_level(sd) < pc_get_group_level(pl_sd) ) {
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
		return -1;
	}

	clif_manner_message(sd, 0);
	clif_manner_message(pl_sd, 5);

	if( pl_sd->status.manner < manner ) {
		pl_sd->status.manner -= manner;
		sc_start(NULL, &pl_sd->bl, SC_NOCHAT, 100, 0, 0);
	} else {
		pl_sd->status.manner = 0;
		status_change_end(&pl_sd->bl, SC_NOCHAT, INVALID_TIMER);
	}

	clif_GM_silence(sd, pl_sd, (manner > 0 ? 1 : 0));

	return 0;
}

/*==========================================
 * @refresh (like @jumpto <<yourself>>)
 *------------------------------------------*/
ACMD_FUNC(refresh)
{
	nullpo_retr(-1, sd);
	clif_refresh(sd);
	return 0;
}

ACMD_FUNC(refreshall)
{
	struct map_session_data *iter_sd;
	struct s_mapiterator *iter;
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for (iter_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); iter_sd = (TBL_PC *)mapit_next(iter))
		clif_refresh(iter_sd);
	mapit_free(iter);
	return 0;
}

/*==========================================
 * @identify
 * => GM's magnifier.
 *------------------------------------------*/
ACMD_FUNC(identify)
{
	int i, num;

	nullpo_retr(-1, sd);

	for (i = num = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].nameid > 0 && sd->inventory.u.items_inventory[i].identify != 1)
			num++;
	}
	if (num > 0)
		clif_item_identify_list(sd);
	else
		clif_displaymessage(fd, msg_txt(1238)); // There are no items to appraise.
	return 0;
}

/*===============================================
 * @identifyall
 * => Indentify all items in inventory - Akinari
 *-----------------------------------------------*/
ACMD_FUNC(identifyall)
{
	nullpo_retr(-1, sd);

	pc_identifyall(sd, true);
	return 0;
}

/*==========================================
 * @gmotd (Global MOTD)
 * by davidsiaw :P
 *------------------------------------------*/
ACMD_FUNC(gmotd)
{
	FILE *fp;

	if ((fp = fopen(motd_txt, "r"))) {
		char buf[CHAT_SIZE_MAX];
		size_t len;

		while (fgets(buf, sizeof(buf), fp)) {
			if (buf[0] == '/' && buf[1] == '/')
				continue;

			len = strlen(buf);

			while (len && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) // Strip trailing EOL characters
				len--;

			if (len) {
				buf[len] = 0;
				intif_broadcast(buf, len + 1, 0);
			}
		}
		fclose(fp);
	}
	return 0;
}

ACMD_FUNC(misceffect)
{
	int effect = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message)
		return -1;
	if (sscanf(message, "%d", &effect) < 1)
		return -1;
	clif_misceffect(&sd->bl, effect);

	return 0;
}

/*==========================================
 * MAIL SYSTEM
 *------------------------------------------*/
ACMD_FUNC(mail)
{
	nullpo_ret(sd);
	mail_openmail(sd);
	return 0;
}

/*==========================================
 * Show Monster DB Info v1.0
 * originally by [Lupus]
 *------------------------------------------*/
ACMD_FUNC(mobinfo)
{
	unsigned char msize[SZ_ALL][7] = { "Small","Medium","Large" };
	unsigned char mrace[RC_ALL][11] = { "Formless","Undead","Beast","Plant","Insect","Fish","Demon","Demi-Human","Angel","Dragon" };
	unsigned char mclass[CLASS_ALL][9] = { "Normal","Boss","Guardian", };
	unsigned char melement[ELE_ALL][8] = { "Neutral","Water","Earth","Fire","Wind","Poison","Holy","Dark","Ghost","Undead" };
	char atcmd_output2[CHAT_SIZE_MAX];
	struct item_data *item_data;
	struct mob_db *mob, *mob_array[MAX_SEARCH];
	int count;
	int i, k;

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(atcmd_output2, '\0', sizeof(atcmd_output2));

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1239)); // Please enter a monster name/ID (usage: @mobinfo <monster_name_or_monster_ID>).
		return -1;
	}

	// If monster identifier/name argument is a name
	if ((i = mobdb_checkid(atoi(message)))) {
		mob_array[0] = mob_db(i);
		count = 1;
	} else
		count = mobdb_searchname_array(mob_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	if (count >= MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH); // Displaying first %d matches
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}

	for (k = 0; k < count; k++) {
		unsigned int j, base_exp, job_exp;

		mob = mob_array[k];
		base_exp = mob->base_exp;
		job_exp = mob->job_exp;

		if (pc_isvip(sd)) { // Display EXP rate increase for VIP.
			base_exp += base_exp * battle_config.vip_base_exp_increase / 100;
			job_exp += base_exp * battle_config.vip_job_exp_increase / 100;
		}

#ifdef RENEWAL_EXP
		if (battle_config.atcommand_mobinfo_type) {
			base_exp = base_exp * pc_level_penalty_mod(mob->lv - sd->status.base_level, mob->status.class_, mob->status.mode, 1) / 100;
			job_exp = job_exp * pc_level_penalty_mod(mob->lv - sd->status.base_level, mob->status.class_, mob->status.mode, 1) / 100;
		}
#endif

		// Stats
		if (mob->mexp)
			sprintf(atcmd_output, msg_txt(1240), mob->name, mob->jname, mob->sprite, mob->vd.class_); // MVP Monster: '%s'/'%s'/'%s' (%d)
		else
			sprintf(atcmd_output, msg_txt(1241), mob->name, mob->jname, mob->sprite, mob->vd.class_); // Monster: '%s'/'%s'/'%s' (%d)
		clif_displaymessage(fd, atcmd_output);

		sprintf(atcmd_output, msg_txt(1242), mob->lv, mob->status.max_hp, base_exp, job_exp, MOB_HIT(mob), MOB_FLEE(mob)); // Lv:%d  HP:%d  Base EXP:%u  Job EXP:%u  HIT:%d  FLEE:%d
		clif_displaymessage(fd, atcmd_output);

		sprintf(atcmd_output, msg_txt(1243), // DEF:%d  MDEF:%d  STR:%d  AGI:%d  VIT:%d  INT:%d  DEX:%d  LUK:%d
			mob->status.def, mob->status.mdef, mob->status.str, mob->status.agi,
			mob->status.vit, mob->status.int_, mob->status.dex, mob->status.luk);
		clif_displaymessage(fd, atcmd_output);

#ifdef RENEWAL
		sprintf(atcmd_output, msg_txt(534), // ATK:%d~%d MATK:%d~%d Range:%d~%d~%d Size:%s Race:%s Class:%s Element:%s (Lv:%d)
			MOB_ATK1(mob), MOB_ATK2(mob), MOB_MATK1(mob), MOB_MATK2(mob), mob->status.rhw.range,
#else
		sprintf(atcmd_output, msg_txt(1244), // ATK:%d~%d Range:%d~%d~%d Size:%s Race:%s Class:%s Element:%s (Lv:%d)
			mob->status.rhw.atk, mob->status.rhw.atk2, mob->status.rhw.range,
#endif
			mob->range2 , mob->range3, msize[mob->status.size],
			mrace[mob->status.race], mclass[mob->status.class_], melement[mob->status.def_ele], mob->status.ele_lv);
		clif_displaymessage(fd, atcmd_output);

		// Drops
		clif_displaymessage(fd, msg_txt(1245)); // Drops:
		strcpy(atcmd_output, " ");
		j = 0;
		for (i = 0; i < MAX_MOB_DROP_TOTAL; i++) {
			float droprate;

			if (mob->dropitem[i].nameid <= 0 || mob->dropitem[i].p < 1 || !(item_data = itemdb_exists(mob->dropitem[i].nameid)))
				continue;
			droprate = (float)mob->dropitem[i].p;
#ifdef RENEWAL_DROP
			if (battle_config.atcommand_mobinfo_type) {
				droprate = droprate * pc_level_penalty_mod(mob->lv - sd->status.base_level, mob->status.class_, mob->status.mode, 2) / 100;

				if (droprate <= 0 && !battle_config.drop_rate0item)
					droprate = 1;
			}
#endif
			if (pc_isvip(sd)) { // Display item rate increase for VIP.
				droprate += droprate * battle_config.vip_drop_increase / 100;
				droprate = min(droprate, 10000); // Cap it to 100%
			}
			if (item_data->slot)
				sprintf(atcmd_output2, " - %s[%d]  %02.02f%%", item_data->jname, item_data->slot, droprate / 100);
			else
				sprintf(atcmd_output2, " - %s  %02.02f%%", item_data->jname, droprate / 100);
			strcat(atcmd_output, atcmd_output2);
			if (!(++j%3)) {
				clif_displaymessage(fd, atcmd_output);
				strcpy(atcmd_output, " ");
			}
		}

		if (!j)
			clif_displaymessage(fd, msg_txt(1246)); // This monster has no drops.
		else if (j%3)
			clif_displaymessage(fd, atcmd_output);

		// Mvp
		if (mob->mexp) {
			float mvppercent, mvpremain;

			sprintf(atcmd_output, msg_txt(1247), mob->mexp); // MVP Bonus EXP:%u
			clif_displaymessage(fd, atcmd_output);
			strcpy(atcmd_output, msg_txt(1248)); // MVP Items:
			mvpremain = 100.0; // Remaining drop chance for official mvp drop mode
			j = 0;
			for (i = 0; i < MAX_MVP_DROP_TOTAL; i++) {
				if (mob->mvpitem[i].nameid <= 0 || !(item_data = itemdb_exists(mob->mvpitem[i].nameid)))
					continue;
				// Because if there are 3 MVP drops at 50%, the first has a chance of 50%, the second 25% and the third 12.5%
				mvppercent = (float)mob->mvpitem[i].p * mvpremain / 10000;
				if (!battle_config.item_drop_mvp_mode)
					mvpremain -= mvppercent;
				if (mvppercent > 0) {
					j++;
					if (j == 1) {
						if (item_data->slot)
							sprintf(atcmd_output2, " %s[%d]  %02.02f%%", item_data->jname, item_data->slot, mvppercent);
						else
							sprintf(atcmd_output2, " %s  %02.02f%%", item_data->jname, mvppercent);
					} else {
						if (item_data->slot)
							sprintf(atcmd_output2, " - %s[%d]  %02.02f%%", item_data->jname, item_data->slot, mvppercent);
						else
							sprintf(atcmd_output2, " - %s  %02.02f%%", item_data->jname, mvppercent);
					}
					strcat(atcmd_output, atcmd_output2);
				}
			}
			if (!j)
				clif_displaymessage(fd, msg_txt(1249)); // This monster has no MVP prizes.
			else
				clif_displaymessage(fd, atcmd_output);
		}
	}
	return 0;
}

/*=========================================
* @showmobs by KarLaeda
* => For 15 sec displays the mobs on minimap
*------------------------------------------*/
ACMD_FUNC(showmobs)
{
	char mob_name[100];
	int mob_id;
	int number = 0;
	struct s_mapiterator *it;

	nullpo_retr(-1, sd);

	if (sscanf(message, "%99[^\n]", mob_name) < 0) {
		clif_displaymessage(fd, msg_txt(545)); // Please enter mob name/ID (usage: @showmobs <mob name/ID>)
		return -1;
	}

	if ((mob_id = atoi(mob_name)) == 0)
		mob_id = mobdb_searchname(mob_name);

	if (mob_id == 0) {
		snprintf(atcmd_output, sizeof atcmd_output, msg_txt(546), mob_name); // Invalid mob name %s!
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (mob_id > 0 && mobdb_checkid(mob_id) == 0) {
		snprintf(atcmd_output, sizeof atcmd_output, msg_txt(1250), mob_name); // Invalid mob id %s!
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	// If player group does not have access to boss mobs.
	if (status_has_mode(&mob_db(mob_id)->status, MD_STATUS_IMMUNE) && !pc_has_permission(sd, PC_PERM_SHOW_BOSS)) {
		clif_displaymessage(fd, msg_txt(1251)); // Can't show boss mobs!
		return -1;
	}

	if (mob_id == atoi(mob_name))
		strcpy(mob_name, mob_db(mob_id)->jname); // --ja--
	//strcpy(mob_name, mob_db(mob_id)->name); // --en--

	snprintf(atcmd_output, sizeof atcmd_output, msg_txt(1252),  // Mob Search... %s %s
		mob_name, mapindex_id2name(sd->mapindex));
	clif_displaymessage(fd, atcmd_output);

	it = mapit_geteachmob();
	for (;;) {
		TBL_MOB *md = (TBL_MOB *)mapit_next(it);

		if (md == NULL)
			break; // No more mobs
		if (md->bl.m != sd->bl.m)
			continue;
		if (mob_id != -1 && md->mob_id != mob_id)
			continue;
		if (md->special_state.ai || md->master_id)
			continue; // Hide slaves and player summoned mobs
		if (md->spawn_timer != INVALID_TIMER)
			continue; // Hide mobs waiting for respawn

		++number;
		clif_viewpoint(sd, 1, 0, md->bl.x, md->bl.y, number, 0xFFFFFF);
	}
	mapit_free(it);

	return 0;
}

/*==========================================
 * homunculus level up [orn]
 *------------------------------------------*/
ACMD_FUNC(homlevel)
{
	TBL_HOM *hd;
	int level = 0, m_class;

	nullpo_retr(-1, sd);

	if (!message || !*message || (level = atoi(message)) < 1) {
		clif_displaymessage(fd, msg_txt(1253)); // Please enter a level adjustment (usage: @homlevel <number of levels>).
		return -1;
	}

	if (!hom_is_active(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1254)); // You do not have a homunculus.
		return -1;
	}

	hd = sd->hd;
	m_class = hom_class2mapid(hd->homunculus.class_);

	if (((m_class&HOM_REG) && battle_config.hom_max_level <= hd->homunculus.level) ||
		((m_class&HOM_S) && battle_config.hom_S_max_level <= hd->homunculus.level)) {
		clif_displaymessage(fd, "Homunculus already reached its maximum level.");
		return 0;
	}

	do {
		hd->homunculus.exp += hd->exp_next;
	} while (hd->homunculus.level < level && hom_levelup(hd));

	status_calc_homunculus(hd, SCO_NONE);
	status_percent_heal(&hd->bl, 100, 100);
	clif_specialeffect(&hd->bl, EF_HO_UP, AREA);

	return 0;
}

/*==========================================
 * homunculus evolution H [orn]
 *------------------------------------------*/
ACMD_FUNC(homevolution)
{
	nullpo_retr(-1, sd);

	if (!hom_is_active(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1254)); // You do not have a homunculus.
		return -1;
	}

	if (!hom_evolution(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1255)); // Your homunculus doesn't evolve.
		return -1;
	}
	clif_homskillinfoblock(sd);
	return 0;
}

ACMD_FUNC(hommutate)
{
	int homun_id, m_class = 0, m_id;
	nullpo_retr(-1, sd);

	if (!hom_is_active(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1254)); // You do not have a homunculus.
		return -1;
	}

	if (!message || !*message) {
		const char *text;

		text = atcommand_help_string(command);
		clif_displaymessage(fd, msg_txt(1054)); // Please enter a proper mutation ID.
		if (text)
			clif_displaymessage(fd, text);
		return -1;
	} else
		homun_id = atoi(message);

	m_class = hom_class2mapid(sd->hd->homunculus.class_);
	m_id	= hom_class2mapid(homun_id);

	if (m_class&HOM_EVO && m_id&HOM_S && sd->hd->homunculus.level >= 99)
		hom_mutate(sd->hd, homun_id);
	else
		clif_emotion(&sd->hd->bl, E_SWT);
	return 0;
}

/*==========================================
 * call choosen homunculus [orn]
 *------------------------------------------*/
ACMD_FUNC(makehomun)
{
	int homunid;
	nullpo_retr(-1, sd);

	if (sd->status.hom_id) {
		clif_displaymessage(fd, msg_txt(450)); // You already have a homunculus
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1256)); // Please enter a homunculus ID (usage: @makehomun <homunculus id>).
		return -1;
	}

	homunid = atoi(message);
	if (homunid < HM_CLASS_BASE || homunid > HM_CLASS_BASE + MAX_HOMUNCULUS_CLASS - 1) {
		clif_displaymessage(fd, msg_txt(1257)); // Invalid Homunculus ID.
		return -1;
	}

	hom_create_request(sd,homunid);
	return 0;
}

/*==========================================
 * modify homunculus intimacy [orn]
 *------------------------------------------*/
ACMD_FUNC(homfriendly)
{
	int friendly = 0;
	nullpo_retr(-1, sd);

	if (!hom_is_active(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1254)); // You do not have a homunculus.
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1258)); // Please enter a friendly value (usage: @homfriendly <friendly value [0-1000]>).
		return -1;
	}

	friendly = atoi(message);
	friendly = cap_value(friendly, 0, 1000);

	sd->hd->homunculus.intimacy = friendly * 100 ;
	clif_send_homdata(sd,SP_INTIMATE,friendly);
	return 0;
}

/*==========================================
 * modify homunculus hunger [orn]
 *------------------------------------------*/
ACMD_FUNC(homhungry)
{
	int hungry = 0;
	nullpo_retr(-1, sd);

	if (!hom_is_active(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1254)); // You do not have a homunculus.
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1259)); // Please enter a hunger value (usage: @homhungry <hunger value [0-100]>).
		return -1;
	}

	hungry = atoi(message);
	hungry = cap_value(hungry, 0, 100);

	sd->hd->homunculus.hunger = hungry;
	clif_send_homdata(sd,SP_HUNGRY,hungry);
	return 0;
}

/*==========================================
 * make the homunculus speak [orn]
 *------------------------------------------*/
ACMD_FUNC(homtalk)
{
	char mes[100],temp[100];
	nullpo_retr(-1, sd);

	if (battle_config.min_chat_delay) {
		if (DIFF_TICK(sd->cantalk_tick, gettick()) > 0)
			return 0;
		sd->cantalk_tick = gettick() + battle_config.min_chat_delay;
	}

	if (sd->sc.cant.chat)
		return -1; // No "chatting" while muted.

	if (!hom_is_active(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1254)); // You do not have a homunculus.
		return -1;
	}

	if (!message || !*message || sscanf(message, "%99[^\n]", mes) < 1) {
		clif_displaymessage(fd, msg_txt(1260)); // Please enter a message (usage: @homtalk <message>).
		return -1;
	}

	snprintf(temp, sizeof temp ,"%s : %s", sd->hd->homunculus.name, mes);
	clif_disp_overhead(&sd->hd->bl, temp);

	return 0;
}

/*==========================================
 * Show homunculus stats
 *------------------------------------------*/
ACMD_FUNC(hominfo)
{
	struct homun_data *hd;
	struct status_data *status;
	nullpo_retr(-1, sd);

	if (!hom_is_active(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1254)); // You do not have a homunculus.
		return -1;
	}

	hd = sd->hd;
	status = status_get_status_data(&hd->bl);
	clif_displaymessage(fd, msg_txt(1261)); // Homunculus stats:

	snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(1262), // HP: %d/%d - SP: %d/%d
		status->hp, status->max_hp, status->sp, status->max_sp);
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(1263), // ATK: %d - MATK: %d~%d
		status->rhw.atk2 + status->batk, status->matk_min, status->matk_max);
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(1264), // Hungry: %d - Intimacy: %u
		hd->homunculus.hunger, hd->homunculus.intimacy / 100);
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output),
		msg_txt(1265), // Stats: Str %d / Agi %d / Vit %d / Int %d / Dex %d / Luk %d
		status->str, status->agi, status->vit,
		status->int_, status->dex, status->luk);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

ACMD_FUNC(homstats)
{
	struct homun_data *hd;
	struct s_homunculus_db *db;
	struct s_homunculus *hom;
	int lv, min, max, evo;
	nullpo_retr(-1, sd);

	if (!hom_is_active(sd->hd)) {
		clif_displaymessage(fd, msg_txt(1254)); // You do not have a homunculus.
		return -1;
	}

	hd = sd->hd;

	hom = &hd->homunculus;
	db = hd->homunculusDB;
	lv = hom->level;

	snprintf(atcmd_output, sizeof(atcmd_output) ,
		msg_txt(1266), lv, db->name); // Homunculus growth stats (Lv %d %s):
	clif_displaymessage(fd, atcmd_output);
	lv--; //Since the first increase is at level 2.

	evo = (hom->class_ == db->evo_class);
	min = db->base.HP +lv*db->gmin.HP +(evo?db->emin.HP:0);
	max = db->base.HP +lv*db->gmax.HP +(evo?db->emax.HP:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,msg_txt(1267), hom->max_hp, min, max); // Max HP: %d (%d~%d)
	clif_displaymessage(fd, atcmd_output);

	min = db->base.SP +lv*db->gmin.SP +(evo?db->emin.SP:0);
	max = db->base.SP +lv*db->gmax.SP +(evo?db->emax.SP:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,msg_txt(1268), hom->max_sp, min, max); // Max SP: %d (%d~%d)
	clif_displaymessage(fd, atcmd_output);

	min = db->base.str +lv*(db->gmin.str/10) +(evo?db->emin.str:0);
	max = db->base.str +lv*(db->gmax.str/10) +(evo?db->emax.str:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,msg_txt(1269), hom->str/10, min, max); // Str: %d (%d~%d)
	clif_displaymessage(fd, atcmd_output);

	min = db->base.agi +lv*(db->gmin.agi/10) +(evo?db->emin.agi:0);
	max = db->base.agi +lv*(db->gmax.agi/10) +(evo?db->emax.agi:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,msg_txt(1270), hom->agi/10, min, max); // Agi: %d (%d~%d)
	clif_displaymessage(fd, atcmd_output);

	min = db->base.vit +lv*(db->gmin.vit/10) +(evo?db->emin.vit:0);
	max = db->base.vit +lv*(db->gmax.vit/10) +(evo?db->emax.vit:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,msg_txt(1271), hom->vit/10, min, max); // Vit: %d (%d~%d)
	clif_displaymessage(fd, atcmd_output);

	min = db->base.int_ +lv*(db->gmin.int_/10) +(evo?db->emin.int_:0);
	max = db->base.int_ +lv*(db->gmax.int_/10) +(evo?db->emax.int_:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,msg_txt(1272), hom->int_/10, min, max); // Int: %d (%d~%d)
	clif_displaymessage(fd, atcmd_output);

	min = db->base.dex +lv*(db->gmin.dex/10) +(evo?db->emin.dex:0);
	max = db->base.dex +lv*(db->gmax.dex/10) +(evo?db->emax.dex:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,msg_txt(1273), hom->dex/10, min, max); // Dex: %d (%d~%d)
	clif_displaymessage(fd, atcmd_output);

	min = db->base.luk +lv*(db->gmin.luk/10) +(evo?db->emin.luk:0);
	max = db->base.luk +lv*(db->gmax.luk/10) +(evo?db->emax.luk:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,msg_txt(1274), hom->luk/10, min, max); // Luk: %d (%d~%d)
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

ACMD_FUNC(homshuffle)
{
	nullpo_retr(-1, sd);

	if(!sd->hd)
		return -1; // Nothing to do

	if(!hom_shuffle(sd->hd))
		return -1;

	clif_displaymessage(sd->fd, msg_txt(1275)); // Homunculus stats altered.
	atcommand_homstats(fd, sd, command, message); // Print out the new stats
	return 0;
}

ACMD_FUNC(hommax)
{
	nullpo_retr(-1, sd);

	if(!sd->hd)
		return -1; // Nothing to do

	if(!hom_max(sd->hd))
		return -1;

	clif_displaymessage(sd->fd, msg_txt(1053)); // [Homunculus MaxHP, MaxSP, And Stats maxed]
	atcommand_homstats(fd, sd, command, message);
	return 0;
}

/*==========================================
 * Show Items DB Info   v 1.0
 * originally by [Lupus]
 *------------------------------------------*/
ACMD_FUNC(iteminfo)
{
	struct item_data *item_array[MAX_SEARCH];
	int i, count = 1;

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1276)); // Please enter an item name/ID (usage: @ii/@iteminfo <item name/ID>).
		return -1;
	}
	if (!(item_array[0] = itemdb_exists(atoi(message))))
		count = itemdb_searchname_array(item_array, MAX_SEARCH, message);
	if (!count) {
		clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
		return -1;
	}
	if (count == MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH); // Displaying first %d matches
		clif_displaymessage(fd, atcmd_output);
	}
	for (i = 0; i < count; i++) {
		struct item_data *item_data = item_array[i];

		sprintf(atcmd_output, msg_txt(1277), // Item: '%s'/'%s'[%d] (%hu) Type: %s | Extra Effect: %s
			item_data->name,item_data->jname,item_data->slot,item_data->nameid,
			(item_data->type == IT_AMMO) ? itemdb_typename_ammo((enum e_item_ammo)item_data->look) : itemdb_typename((enum item_types)item_data->type),
			(!item_data->script ? msg_txt(1278) : msg_txt(1279)) // None / With script
		);
		clif_displaymessage(fd, atcmd_output);

		sprintf(atcmd_output, msg_txt(1280), item_data->value_buy, item_data->value_sell, item_data->weight / 10.); // NPC Buy:%dz, Sell:%dz | Weight: %.1f
		clif_displaymessage(fd, atcmd_output);

		if (item_data->maxchance == -1) {
			strcpy(atcmd_output, msg_txt(1281)); // - Available in the shops only.
			clif_displaymessage(fd, atcmd_output);
		} else if (!battle_config.atcommand_mobinfo_type) {
			if (item_data->maxchance)
				sprintf(atcmd_output, msg_txt(1282), (float)item_data->maxchance / 100); // - Maximal monsters drop chance: %02.02f%%
			else
				strcpy(atcmd_output, msg_txt(1283)); //  - Monsters don't drop this item.
			clif_displaymessage(fd, atcmd_output);
		}
	}
	return 0;
}

/*==========================================
 * Show who drops the item.
 *------------------------------------------*/
ACMD_FUNC(whodrops)
{
	struct item_data *item_data, *item_array[MAX_SEARCH];
	int i, j, count = 1;

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1284)); // Please enter item name/ID (usage: @whodrops <item name/ID>).
		return -1;
	}

	if (!(item_array[0] = itemdb_exists(atoi(message))))
		count = itemdb_searchname_array(item_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
		return -1;
	}

	if (count == MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH); // Displaying first %d matches
		clif_displaymessage(fd, atcmd_output);
	}

	for (i = 0; i < count; i++) {
		item_data = item_array[i];
		sprintf(atcmd_output, msg_txt(1285), item_data->jname, item_data->slot, item_data->nameid); // Item: '%s'[%d] (ID: %hu)
		clif_displaymessage(fd, atcmd_output);
		if (!item_data->mob[0].chance) {
			strcpy(atcmd_output, msg_txt(1286)); // - Item is not dropped by mobs.
			clif_displaymessage(fd, atcmd_output);
		} else {
			sprintf(atcmd_output, msg_txt(1287), MAX_SEARCH); // - Common mobs with highest drop chance (only max %d are listed):
			clif_displaymessage(fd, atcmd_output);
			for (j = 0; j < MAX_SEARCH && item_data->mob[j].chance > 0; j++) {
				float dropchance = (float)item_data->mob[j].chance;

#ifdef RENEWAL_DROP
				if (battle_config.atcommand_mobinfo_type)
					dropchance = dropchance * pc_level_penalty_mod(mob_db(item_data->mob[j].id)->lv - sd->status.base_level, mob_db(item_data->mob[j].id)->status.class_, mob_db(item_data->mob[j].id)->status.mode, 2) / 100;
#endif
				if (pc_isvip(sd)) // Display item rate increase for VIP.
					dropchance += dropchance * battle_config.vip_drop_increase / 100;
				sprintf(atcmd_output, "- %s (%d): %02.02f%%", mob_db(item_data->mob[j].id)->jname, item_data->mob[j].id, dropchance / 100);
				clif_displaymessage(fd, atcmd_output);
			}
		}
	}
	return 0;
}

ACMD_FUNC(whereis)
{
	struct mob_db *mob_array[MAX_SEARCH];
	int count;
	int i, j, k;

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1288)); // Please enter a monster name/ID (usage: @whereis <monster_name_or_monster_ID>).
		return -1;
	}

	// If monster identifier/name argument is a name
	if ((i = mobdb_checkid(atoi(message)))) {
		mob_array[0] = mob_db(i);
		count = 1;
	} else
		count = mobdb_searchname_array(mob_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(40)); // Invalid monster ID or name.
		return -1;
	}

	if (count >= MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH); // Displaying first %d matches
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}

	for (k = 0; k < count; k++) {
		struct mob_db *mob = mob_array[k];

		snprintf(atcmd_output, sizeof atcmd_output, msg_txt(1289), mob->jname); // %s spawns in:
		clif_displaymessage(fd, atcmd_output);
		for (i = 0; i < ARRAYLENGTH(mob->spawn) && mob->spawn[i].qty; i++) {
			j = map_mapindex2mapid(mob->spawn[i].mapindex);
			if (j < 0)
				continue;
			snprintf(atcmd_output, sizeof atcmd_output, "%s (%d)", map[j].name, mob->spawn[i].qty);
			clif_displaymessage(fd, atcmd_output);
		}
		if (i == 0)
			clif_displaymessage(fd, msg_txt(1290)); // This monster does not spawn normally.
	}

	return 0;
}

ACMD_FUNC(version)
{
	pc_show_version(sd);
	return 0;
}

/*==========================================
 * @mutearea by MouseJstr
 *------------------------------------------*/
static int atcommand_mutearea_sub(struct block_list *bl,va_list ap)
{	
	int time, id;
	struct map_session_data *pl_sd = (struct map_session_data *)bl;

	if (pl_sd == NULL)
		return 0;

	id = va_arg(ap, int);
	time = va_arg(ap, int);

	if (id != bl->id && !pc_get_group_level(pl_sd)) {
		pl_sd->status.manner -= time;
		if (pl_sd->status.manner < 0)
			sc_start(NULL, &pl_sd->bl, SC_NOCHAT, 100, 0, 0);
		else
			status_change_end(&pl_sd->bl, SC_NOCHAT, INVALID_TIMER);
	}
	return 0;
}

ACMD_FUNC(mutearea)
{
	int time;

	nullpo_ret(sd);

	if (!message || !*message) {
		clif_displaymessage(fd, msg_txt(1297)); // Please enter a time in minutes (usage: @mutearea/@stfu <time in minutes>).
		return -1;
	}

	time = atoi(message);

	map_foreachinallarea(atcommand_mutearea_sub,sd->bl.m,
		sd->bl.x-AREA_SIZE, sd->bl.y-AREA_SIZE,
		sd->bl.x+AREA_SIZE, sd->bl.y+AREA_SIZE, BL_PC, sd->bl.id, time);

	return 0;
}


ACMD_FUNC(rates)
{
	char buf[CHAT_SIZE_MAX];

	nullpo_ret(sd);
	memset(buf, '\0', sizeof(buf));

	snprintf(buf, CHAT_SIZE_MAX, msg_txt(1298), // Experience rates: Base %.2fx / Job %.2fx
		(battle_config.base_exp_rate + (pc_isvip(sd) ? battle_config.vip_base_exp_increase * battle_config.base_exp_rate / 100 : 0)) / 100.,
		(battle_config.job_exp_rate + (pc_isvip(sd) ? battle_config.vip_job_exp_increase * battle_config.job_exp_rate / 100 : 0)) / 100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, msg_txt(1299), // Normal Drop Rates: Common %.2fx / Healing %.2fx / Usable %.2fx / Equipment %.2fx / Card %.2fx
		(battle_config.item_rate_common + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_common / 100 : 0)) / 100.,
		(battle_config.item_rate_heal + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_heal / 100 : 0)) / 100.,
		(battle_config.item_rate_use + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_use / 100 : 0)) / 100.,
		(battle_config.item_rate_equip + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_equip / 100 : 0)) / 100.,
		(battle_config.item_rate_card + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_card / 100 : 0)) / 100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, msg_txt(1300), // Boss Drop Rates: Common %.2fx / Healing %.2fx / Usable %.2fx / Equipment %.2fx / Card %.2fx
		(battle_config.item_rate_common_boss + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_common_boss / 100 : 0)) / 100.,
		(battle_config.item_rate_heal_boss + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_heal_boss / 100 : 0)) / 100.,
		(battle_config.item_rate_use_boss + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_use_boss / 100 : 0)) / 100.,
		(battle_config.item_rate_equip_boss + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_equip_boss / 100 : 0)) / 100.,
		(battle_config.item_rate_card_boss + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_card_boss / 100 : 0)) / 100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, msg_txt(381), // MVP Drop Rates: Common %.2fx / Healing %.2fx / Usable %.2fx / Equipment %.2fx / Card %.2fx
		(battle_config.item_rate_common_mvp + (pc_isvip(sd) ? (battle_config.vip_drop_increase * battle_config.item_rate_common_mvp) / 100 : 0)) / 100.,
		(battle_config.item_rate_heal_mvp + (pc_isvip(sd) ? (battle_config.vip_drop_increase * battle_config.item_rate_heal_mvp) / 100 : 0)) / 100.,
		(battle_config.item_rate_use_mvp + (pc_isvip(sd) ? (battle_config.vip_drop_increase * battle_config.item_rate_use_mvp) / 100 : 0)) / 100.,
		(battle_config.item_rate_equip_mvp + (pc_isvip(sd) ? (battle_config.vip_drop_increase * battle_config.item_rate_equip_mvp) / 100 : 0)) / 100.,
		(battle_config.item_rate_card_mvp + (pc_isvip(sd) ? (battle_config.vip_drop_increase * battle_config.item_rate_card_mvp) / 100 : 0)) / 100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, msg_txt(1301), // Other Drop Rates: MvP %.2fx / Card-Based %.2fx / Treasure %.2fx
		(battle_config.item_rate_mvp + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_mvp / 100 : 0)) / 100.,
		(battle_config.item_rate_adddrop + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_adddrop / 100 : 0)) / 100.,
		(battle_config.item_rate_treasure + (pc_isvip(sd) ? battle_config.vip_drop_increase * battle_config.item_rate_treasure / 100 : 0)) / 100.);
	clif_displaymessage(fd, buf);

	return 0;
}

/*==========================================
 * @me by lordalfa
 * => Displays the OUTPUT string on top of the Visible players Heads.
 *------------------------------------------*/
ACMD_FUNC(me)
{
	char tempmes[CHAT_SIZE_MAX];
	nullpo_retr(-1, sd);

	memset(tempmes, '\0', sizeof(tempmes));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (sd->sc.cant.chat)
		return -1; //no "chatting" while muted.

	if (!message || !*message || sscanf(message, "%255[^\n]", tempmes) < 0) {
		clif_displaymessage(fd, msg_txt(1302)); // Please enter a message (usage: @me <message>).
		return -1;
	}
	
	sprintf(atcmd_output, msg_txt(270), sd->status.name, tempmes);	// *%s %s*
	clif_disp_overhead(&sd->bl, atcmd_output);
	
	return 0;
	
}

/*==========================================
 * @size
 * => Resize your character sprite. [Valaris]
 *------------------------------------------*/
ACMD_FUNC(size)
{
	int size = 0;
	nullpo_retr(-1, sd);

	size = cap_value(atoi(message),SZ_SMALL,SZ_BIG);

	if(sd->state.size) {
		sd->state.size = SZ_SMALL;
		pc_setpos(sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_TELEPORT);
	}

	sd->state.size = size;
	if( size == SZ_MEDIUM )
		clif_specialeffect(&sd->bl,EF_BABYBODY,AREA);
	else if( size == SZ_BIG )
		clif_specialeffect(&sd->bl,EF_GIANTBODY,AREA);

	clif_displaymessage(fd, msg_txt(1303)); // Size change applied.
	return 0;
}

ACMD_FUNC(sizeall)
{
	int size;
	struct map_session_data *pl_sd;
	struct s_mapiterator *iter;

	size = atoi(message);
	size = cap_value(size,0,2);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC *)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC *)mapit_next(iter) ) {
		if( pl_sd->state.size != size ) {
			if( pl_sd->state.size ) {
				pl_sd->state.size = SZ_SMALL;
				pc_setpos(pl_sd, pl_sd->mapindex, pl_sd->bl.x, pl_sd->bl.y, CLR_TELEPORT);
			}

			pl_sd->state.size = size;
			if( size == SZ_MEDIUM )
				clif_specialeffect(&pl_sd->bl,EF_BABYBODY,AREA);
			else if( size == SZ_BIG )
				clif_specialeffect(&pl_sd->bl,EF_GIANTBODY,AREA);
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(1303)); // Size change applied.
	return 0;
}

ACMD_FUNC(sizeguild)
{
	int size = 0, i;
	char guild[NAME_LENGTH];
	struct map_session_data *pl_sd;
	struct guild *g;
	nullpo_retr(-1, sd);
	
	memset(guild, '\0', sizeof(guild));
	
	if( !message || !*message || sscanf(message, "%d %23[^\n]", &size, guild) < 2 ) {
		clif_displaymessage(fd, msg_txt(1304)); // Please enter guild name/ID (usage: @sizeguild <size> <guild name/ID>).
		return -1;
	}

	if( (g = guild_searchname(guild)) == NULL && (g = guild_search(atoi(guild))) == NULL ) {
		clif_displaymessage(fd, msg_txt(94)); // Incorrect name/ID, or no one from the guild is online.
		return -1;
	}
	
	size = cap_value(size,SZ_SMALL,SZ_BIG);
	
	for( i = 0; i < g->max_member; i++ ) {
		if( (pl_sd = g->member[i].sd) && pl_sd->state.size != size ) {
			if( pl_sd->state.size ) {
				pl_sd->state.size = SZ_SMALL;
				pc_setpos(pl_sd, pl_sd->mapindex, pl_sd->bl.x, pl_sd->bl.y, CLR_TELEPORT);
			}

			pl_sd->state.size = size;
			if( size == SZ_MEDIUM )
				clif_specialeffect(&pl_sd->bl,EF_BABYBODY,AREA);
			else if( size == SZ_BIG )
				clif_specialeffect(&pl_sd->bl,EF_GIANTBODY,AREA);
		}
	}

	clif_displaymessage(fd, msg_txt(1303)); // Size change applied.
	return 0;
}

/*==========================================
 * @monsterignore
 * => Makes monsters ignore you. [Valaris]
 *------------------------------------------*/
ACMD_FUNC(monsterignore)
{
	nullpo_retr(-1, sd);

	if (!sd->state.monster_ignore) {
		sd->state.monster_ignore = 1;
		clif_displaymessage(sd->fd, msg_txt(1305)); // You are now immune to attacks.
	} else {
		sd->state.monster_ignore = 0;
		clif_displaymessage(sd->fd, msg_txt(1306)); // Returned to normal state.
	}

	return 0;
}
/*==========================================
 * @fakename
 * => Gives your character a fake name. [Valaris]
 *------------------------------------------*/
ACMD_FUNC(fakename)
{
	nullpo_retr(-1, sd);

	if( !message || !*message ) {
		if( sd->fakename[0] ) {
			sd->fakename[0] = '\0';
			clif_name_area(&sd->bl);
			if( sd->disguise )
				clif_name_self(&sd->bl);
			clif_displaymessage(sd->fd, msg_txt(1307)); // Returned to real name.
			return 0;
		}

		clif_displaymessage(sd->fd, msg_txt(1308)); // You must enter a name.
		return -1;
	}

	if( strlen(message) < 2 ) {
		clif_displaymessage(sd->fd, msg_txt(1309)); // Fake name must be at least two characters.
		return -1;
	}

	safestrncpy(sd->fakename, message, sizeof(sd->fakename));
	clif_name_area(&sd->bl);
	if( sd->disguise ) // Another packet should be sent so the client updates the name for sd
		clif_name_self(&sd->bl);
	clif_displaymessage(sd->fd, msg_txt(1310)); // Fake name enabled.

	return 0;
}

/*==========================================
 * Ragnarok Resources
 *------------------------------------------*/
ACMD_FUNC(mapflag) {
#define checkflag( cmd ) if ( map[ sd->bl.m ].flag.cmd ) clif_displaymessage(sd->fd,#cmd)
#define setflag( cmd ) \
	if ( strcmp( flag_name , #cmd ) == 0 ){\
		map[ sd->bl.m ].flag.cmd = flag;\
		sprintf(atcmd_output,"[ @mapflag ] %s flag has been set to %s value = %hd",#cmd,flag?"On":"Off",flag);\
		clif_displaymessage(sd->fd,atcmd_output);\
		return 0;\
	}
	char flag_name[100];
	short flag = 0,i;

	nullpo_retr(-1, sd);

	memset(flag_name, '\0', sizeof(flag_name));

	if (!message || !*message || (sscanf(message, "%99s %hd", flag_name, &flag) < 1)) {
		clif_displaymessage(sd->fd,msg_txt(1311)); // Enabled Mapflags in this map:
		clif_displaymessage(sd->fd,"----------------------------------");
		checkflag(town);
		checkflag(autotrade);		checkflag(allowks);		checkflag(nomemo);		checkflag(noteleport);
		checkflag(noreturn);		checkflag(monster_noteleport);	checkflag(nosave);		checkflag(nobranch);
		checkflag(noexppenalty);	checkflag(pvp);			checkflag(pvp_noparty);		checkflag(pvp_noguild);
		checkflag(pvp_nightmaredrop);	checkflag(pvp_nocalcrank);	checkflag(gvg_castle);		checkflag(gvg);
		checkflag(gvg_dungeon);		checkflag(gvg_noparty);		checkflag(battleground);	checkflag(nozenypenalty);
		checkflag(notrade);		checkflag(noskill);		checkflag(nowarp);		checkflag(nowarpto);
		checkflag(noicewall);		checkflag(snow);		checkflag(clouds);		checkflag(clouds2);
		checkflag(fog);			checkflag(fireworks);		checkflag(sakura);		checkflag(leaves);
		checkflag(nogo);		checkflag(nobaseexp);
		checkflag(nojobexp);		checkflag(nomobloot);		checkflag(nomvploot);		checkflag(nightenabled);
		checkflag(restricted);		checkflag(nodrop);		checkflag(novending);		checkflag(loadevent);
		checkflag(nochat);		checkflag(partylock);		checkflag(guildlock);		checkflag(reset);
		checkflag(nochmautojoin);	checkflag(nousecart);		checkflag(noitemconsumption);	checkflag(nosumstarmiracle);
		checkflag(nomineeffect);	checkflag(nolockon);		checkflag(notomb);		checkflag(nocashshop);
		checkflag(nobanking);		checkflag(gvg_te);		checkflag(gvg_te_castle);	checkflag(nocostume);
		checkflag(hidemobhpbar);
		checkflag(privateairship_source);
		checkflag(privateairship_destination);
#ifdef ADJUST_SKILL_DAMAGE
		checkflag(skill_damage);
#endif
		clif_displaymessage(sd->fd," ");
		clif_displaymessage(sd->fd,msg_txt(1312)); // Usage: "@mapflag monster_noteleport 1" (0=Off | 1=On)
		clif_displaymessage(sd->fd,msg_txt(1313)); // Type "@mapflag available" to list the available mapflags.
		return 1;
	}
	for (i = 0; flag_name[i]; i++)
		flag_name[i] = (char)tolower(flag_name[i]); // Lowercase

	setflag(town);
	setflag(autotrade);		setflag(allowks);		setflag(nomemo);		setflag(noteleport);
	setflag(noreturn);		setflag(monster_noteleport);	setflag(nosave);		setflag(nobranch);
	setflag(noexppenalty);		setflag(pvp);			setflag(pvp_noparty);		setflag(pvp_noguild);
	setflag(pvp_nightmaredrop);	setflag(pvp_nocalcrank);	setflag(gvg_castle);		setflag(gvg);
	setflag(gvg_dungeon);		setflag(gvg_noparty);		setflag(battleground);		setflag(nozenypenalty);
	setflag(notrade);		setflag(noskill);		setflag(nowarp);		setflag(nowarpto);
	setflag(noicewall);		setflag(snow);			setflag(clouds);		setflag(clouds2);
	setflag(fog);			setflag(fireworks);		setflag(sakura);		setflag(leaves);
	setflag(nogo);			setflag(nobaseexp);
	setflag(nojobexp);		setflag(nomobloot);		setflag(nomvploot);		setflag(nightenabled);
	setflag(restricted);		setflag(nodrop);		setflag(novending);		setflag(loadevent);
	setflag(nochat);		setflag(partylock);		setflag(guildlock);		setflag(reset);
	setflag(nochmautojoin);		setflag(nousecart);		setflag(noitemconsumption);	setflag(nosumstarmiracle);
	setflag(nomineeffect);		setflag(nolockon);		setflag(notomb);		setflag(nocashshop);
	setflag(nobanking);		setflag(gvg_te);		setflag(gvg_te_castle);		setflag(nocostume);
	setflag(hidemobhpbar);
	setflag(privateairship_source);
	setflag(privateairship_destination)
#ifdef ADJUST_SKILL_DAMAGE
	setflag(skill_damage);
#endif

	clif_displaymessage(sd->fd,msg_txt(1314)); // Invalid flag name or flag.
	clif_displaymessage(sd->fd,msg_txt(1312)); // Usage: "@mapflag monster_noteleport 1" (0=Off | 1=On)
	clif_displaymessage(sd->fd,msg_txt(1315)); // Available Flags:
	clif_displaymessage(sd->fd,"----------------------------------");
	clif_displaymessage(sd->fd,"town, autotrade, allowks, nomemo, noteleport, noreturn, monster_noteleport, nosave,");
	clif_displaymessage(sd->fd,"nobranch, noexppenalty, pvp, pvp_noparty, pvp_noguild, pvp_nightmaredrop,");
	clif_displaymessage(sd->fd,"pvp_nocalcrank, gvg_castle, gvg, gvg_dungeon, gvg_noparty, battleground,");
	clif_displaymessage(sd->fd,"nozenypenalty, notrade, noskill, nowarp, nowarpto, noicewall, snow, clouds, clouds2,");
	clif_displaymessage(sd->fd,"fog, fireworks, sakura, leaves, nogo, nobaseexp, nojobexp, nomobloot, nomvploot,");
	clif_displaymessage(sd->fd,"nightenabled, restricted, nodrop, novending, loadevent, nochat, partylock, guildlock,");
	clif_displaymessage(sd->fd,"reset, nochmautojoin, nousecart, noitemconsumption, nosumstarmiracle, nolockon, notomb,");
	clif_displaymessage(sd->fd,"nocashshop, nobanking, gvg_te, gvg_te_castle, nocostume, hidemobhpbar, privateairship_source,");
	clif_displaymessage(sd->fd,"privateairship_destination,");
#ifdef ADJUST_SKILL_DAMAGE
	clif_displaymessage(sd->fd,"skill_damage");
#endif

#undef checkflag
#undef setflag

	return 0;
}

/*===================================
 * Remove some messages
 *-----------------------------------*/
ACMD_FUNC(showexp)
{
	if (sd->state.showexp) {
		sd->state.showexp = 0;
		clif_displaymessage(fd, msg_txt(1316)); // Gained exp will not be shown.
		return 0;
	}

	sd->state.showexp = 1;
	clif_displaymessage(fd, msg_txt(1317)); // Gained exp is now shown.
	return 0;
}

ACMD_FUNC(showzeny)
{
	if (sd->state.showzeny) {
		sd->state.showzeny = 0;
		clif_displaymessage(fd, msg_txt(1318)); // Gained zeny will not be shown.
		return 0;
	}

	sd->state.showzeny = 1;
	clif_displaymessage(fd, msg_txt(1319)); // Gained zeny is now shown.
	return 0;
}

ACMD_FUNC(showdelay)
{
	if (sd->state.showdelay) {
		sd->state.showdelay = 0;
		clif_displaymessage(fd, msg_txt(1320)); // Skill delay failures will not be shown.
		return 0;
	}
	
	sd->state.showdelay = 1;
	clif_displaymessage(fd, msg_txt(1321)); // Skill delay failures are now shown.
	return 0;
}

/*==========================================
 * Duel organizing functions [LuzZza]
 *
 * @duel [limit|nick] - create a duel
 * @invite <nick> - invite player
 * @accept - accept invitation
 * @reject - reject invitation
 * @leave - leave duel
 *------------------------------------------*/
ACMD_FUNC(invite)
{
	unsigned int did = sd->duel_group;
	struct map_session_data *target_sd = NULL;

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if(!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		sprintf(atcmd_output, msg_txt(435), command); // Please enter a player name (usage: %s <char name>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if(did == 0) {
		clif_displaymessage(fd, msg_txt(350)); // "Duel: @invite without @duel."
		return 0;
	}

	if(duel_list[did].max_players_limit > 0 &&
		duel_list[did].members_count >= duel_list[did].max_players_limit) {
		clif_displaymessage(fd, msg_txt(351)); // "Duel: Limit of players is reached."
		return 0;
	}

	if((target_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(352)); // "Duel: Player not found."
		return 0;
	}

	if(target_sd->duel_group > 0 || target_sd->duel_invite > 0) {
		clif_displaymessage(fd, msg_txt(353)); // "Duel: Player already in duel."
		return 0;
	}

	if(battle_config.duel_only_on_same_map && target_sd->bl.m != sd->bl.m) {
		sprintf(atcmd_output, msg_txt(364), atcmd_player_name); // Duel: You can't invite %s because he/she isn't on the same map.
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}

	duel_invite(did, sd, target_sd);
	clif_displaymessage(fd, msg_txt(354)); // "Duel: Invitation has been sent."
	return 0;
}

ACMD_FUNC(duel)
{
	unsigned int maxpl = 0;

	if(sd->duel_group > 0) {
		duel_showinfo(sd->duel_group, sd);
		return 0;
	}

	if(sd->duel_invite > 0) {
		clif_displaymessage(fd, msg_txt(355)); // "Duel: @duel without @reject."
		return 0;
	}

	if(!duel_checktime(sd)) {
		char output[CHAT_SIZE_MAX];

		sprintf(output, msg_txt(356), battle_config.duel_time_interval); // "Duel: You can take part in duel only one time per %d minutes."
		clif_displaymessage(fd, output);
		return 0;
	}

	if(message[0]) {
		if(sscanf(message, "%u", &maxpl) >= 1) {
			if(maxpl < 2 || maxpl > 65535) {
				clif_displaymessage(fd, msg_txt(357)); // "Duel: Invalid value."
				return 0;
			}
			duel_create(sd, maxpl);
		} else {
			struct map_session_data *target_sd = NULL;

			memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));
			memset(atcmd_output, '\0', sizeof(atcmd_output));

			if(sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
				sprintf(atcmd_output, msg_txt(435), command); // Please enter a player name (usage: %s <char name>).
				clif_displaymessage(fd, atcmd_output);
				return -1;
			}

			if((target_sd = map_nick2sd(atcmd_player_name)) != NULL) {
				unsigned int newduel;

				if((newduel = duel_create(sd, 2)) != -1) {
					if(target_sd->duel_group > 0 ||	target_sd->duel_invite > 0) {
						clif_displaymessage(fd, msg_txt(353)); // "Duel: Player already in duel."
						return 0;
					}
					duel_invite(newduel, sd, target_sd);
					clif_displaymessage(fd, msg_txt(354)); // "Duel: Invitation has been sent."
				}
			} else {
				clif_displaymessage(fd, msg_txt(352)); // "Duel: Player not found."
				return 0;
			}
		}
	} else
		duel_create(sd, 0);
	return 0;
}


ACMD_FUNC(leave)
{
	if(sd->duel_group <= 0) {
		clif_displaymessage(fd, msg_txt(358)); // "Duel: @leave without @duel."
		return 0;
	}

	duel_leave(sd->duel_group, sd);
	clif_displaymessage(fd, msg_txt(359)); // "Duel: You left the duel."
	return 0;
}

ACMD_FUNC(accept)
{
	if(!duel_checktime(sd)) {
		char output[CHAT_SIZE_MAX];

		sprintf(output, msg_txt(356), battle_config.duel_time_interval); // "Duel: You can take part in duel only one time per %d minutes."
		clif_displaymessage(fd, output);
		return 0;
	}

	if(sd->duel_invite <= 0) {
		clif_displaymessage(fd, msg_txt(360)); // "Duel: @accept without invititation."
		return 0;
	}

	if(duel_list[sd->duel_invite].max_players_limit > 0 && duel_list[sd->duel_invite].members_count >= duel_list[sd->duel_invite].max_players_limit) {
		clif_displaymessage(fd, msg_txt(351)); // "Duel: Limit of players is reached."
		return 0;
	}

	duel_accept(sd->duel_invite, sd);
	clif_displaymessage(fd, msg_txt(361)); // "Duel: Invitation has been accepted."
	return 0;
}

ACMD_FUNC(reject)
{
	if(sd->duel_invite <= 0) {
		clif_displaymessage(fd, msg_txt(362)); // "Duel: @reject without invititation."
		return 0;
	}

	duel_reject(sd->duel_invite, sd);
	clif_displaymessage(fd, msg_txt(363)); // "Duel: Invitation has been rejected."
	return 0;
}

/*===================================
 * Cash Points
 *-----------------------------------*/
ACMD_FUNC(cash)
{
	char output[128];
	int value;
	int ret = 0;

	nullpo_retr(-1, sd);

	if( sd->state.cashshop_open ) { // Since there is no cashpoint update packet we need to force updating like this
		clif_displaymessage(fd, msg_txt(725)); // Please close the cashshop before using this command.
		return -1;
	}

	if( !message || !*message || !(value = atoi(message)) ) {
		clif_displaymessage(fd, msg_txt(1322)); // Please enter an amount.
		return -1;
	}

	parent_cmd = atcommand_checkalias(command + 1);

	if( !strcmpi(parent_cmd, "cash") ) {
		if( value > 0 ) {
			if( (ret = pc_getcash(sd, value, 0, LOG_TYPE_COMMAND)) >= 0 ) {
				// If this option is set, the message is already sent by pc function
				if( !battle_config.cashshop_show_points ) {
					sprintf(output, msg_txt(505), ret, sd->cashPoints); // Gained %d cash points. Total %d points.
					clif_messagecolor(&sd->bl, color_table[COLOR_LIGHT_GREEN], output, false, SELF);
				}
			} else
				clif_displaymessage(fd, msg_txt(149)); // Impossible to increase the number/value.
		} else {
			if( -value > sd->cashPoints ) //By command, if cash < value, force it to remove all
				value = -sd->cashPoints;
			if( (ret = pc_paycash(sd, -value, 0, LOG_TYPE_COMMAND)) >= 0 ) {
				// If this option is set, the message is already sent by pc function
				if( !battle_config.cashshop_show_points ) {
					sprintf(output, msg_txt(410), ret, sd->cashPoints); // Removed %d cash points. Total %d points.
					clif_messagecolor(&sd->bl, color_table[COLOR_LIGHT_GREEN], output, false, SELF);
				}
			} else
				clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		}
	} else { // @points
		if( value > 0 ) {
			if( (ret = pc_getcash(sd, 0, value, LOG_TYPE_COMMAND)) >= 0 ) {
				sprintf(output, msg_txt(506), ret, sd->kafraPoints); // Gained %d kafra points. Total %d points.
				clif_messagecolor(&sd->bl, color_table[COLOR_LIGHT_GREEN], output, false, SELF);
			} else
				clif_displaymessage(fd, msg_txt(149)); // Impossible to increase the number/value.
		} else {
			if( -value > sd->kafraPoints ) //By command, if point < value, force it to remove all
				value = -sd->kafraPoints;
			if( (ret = pc_paycash(sd, 0, -value, LOG_TYPE_COMMAND)) >= 0 ) {
				sprintf(output, msg_txt(411), ret, sd->kafraPoints); // Removed %d kafra points. Total %d points.
				clif_messagecolor(&sd->bl, color_table[COLOR_LIGHT_GREEN], output, false, SELF);
			} else
				clif_displaymessage(fd, msg_txt(41)); // Unable to decrease the number/value.
		}
	}

	return 0;
}

// @clone/@slaveclone/@evilclone <playername> [Valaris]
ACMD_FUNC(clone)
{
	int x = 0, y = 0, flag = 0, master = 0, i = 0;
	struct map_session_data *pl_sd = NULL;

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(sd->fd,msg_txt(1323)); // You must enter a player name or ID.
		return 0;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL && (pl_sd = map_charid2sd(atoi(atcmd_player_name))) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return 0;
	}

	if (pc_get_group_level(pl_sd) > pc_get_group_level(sd)) {
		clif_displaymessage(fd, msg_txt(126)); // Cannot clone a player of higher GM level than yourself.
		return 0;
	}

	parent_cmd = atcommand_checkalias(command + 1);

	if (strcmpi(parent_cmd, "clone") == 0)
		flag = 1;
	else if (strcmpi(parent_cmd, "slaveclone") == 0) {
		flag = 2;
		if (pc_isdead(sd)) {
			clif_displaymessage(fd, msg_txt(129 + flag * 2));
			return 0;
		}
		master = sd->bl.id;
		if (battle_config.atc_slave_clone_limit &&
			mob_countslave(&sd->bl) >= battle_config.atc_slave_clone_limit) {
			clif_displaymessage(fd, msg_txt(127)); // You've reached your slave clones limit.
			return 0;
		}
	}

	do {
		x = sd->bl.x + (rnd() % 10 - 5);
		y = sd->bl.y + (rnd() % 10 - 5);
	} while (map_getcell(sd->bl.m,x,y,CELL_CHKNOPASS) && i++ < 10);

	if (i >= 10) {
		x = sd->bl.x;
		y = sd->bl.y;
	}

	if ((x = mob_clone_spawn(pl_sd, sd->bl.m, x, y, "", master, 0, (flag ? 1 : 0), 0)) > 0) {
		clif_displaymessage(fd, msg_txt(128 + flag * 2)); // Evil Clone spawned. Clone spawned. Slave clone spawned.
		return 0;
	}
	clif_displaymessage(fd, msg_txt(129 + flag * 2)); // Unable to spawn evil clone. Unable to spawn clone. Unable to spawn slave clone.
	return 0;
}

/*=====================================
 * Autorejecting Invites/Deals [LuzZza]
 * Usage: @noask
 *-------------------------------------*/
ACMD_FUNC(noask)
{
	if (sd->state.noask) {
		clif_displaymessage(fd, msg_txt(391)); // Autorejecting is deactivated.
		sd->state.noask = 0;
	} else {
		clif_displaymessage(fd, msg_txt(390)); // Autorejecting is activated.
		sd->state.noask = 1;
	}
	
	return 0;
}

/*=====================================
 * Send a @request message to all GMs of lowest_gm_level.
 * Usage: @request <petition>
 *-------------------------------------*/
ACMD_FUNC(request)
{
	if (!message || !*message) {
		clif_displaymessage(sd->fd,msg_txt(277)); // Usage: @request <petition/message to online GMs>.
		return -1;
	}

	sprintf(atcmd_output, msg_txt(278), message); // (@request): %s
	intif_wis_message_to_gm(sd->status.name, PC_PERM_RECEIVE_REQUESTS, atcmd_output);
	clif_messagecolor(&sd->bl, color_table[COLOR_LIGHT_GREEN], atcmd_output, false, SELF);
	clif_displaymessage(sd->fd, msg_txt(279)); // @request sent.
	return 0;
}

/*==========================================
 * Feel (SG save map) Reset [HiddenDragon]
 *------------------------------------------*/
ACMD_FUNC(feelreset)
{
	pc_resetfeel(sd);
	clif_displaymessage(fd, msg_txt(1324)); // Reset 'Feeling' maps.

	return 0;
}

/*==========================================
 * AUCTION SYSTEM
 *------------------------------------------*/
ACMD_FUNC(auction)
{
	nullpo_ret(sd);

	if (!battle_config.feature_auction) {
		clif_messagecolor(&sd->bl, color_table[COLOR_RED], msg_txt(1489), false, SELF); // Auction system isn't available.
		return 0;
	}

	clif_Auction_openwindow(sd);

	return 0;
}

/*==========================================
 * Kill Steal Protection
 *------------------------------------------*/
ACMD_FUNC(ksprotection)
{
	nullpo_retr(-1,sd);

	if( sd->state.noks ) {
		sd->state.noks = 0;
		clif_displaymessage(fd, msg_txt(1325)); // [ K.S Protection Inactive ]
	} else {
		if( !message || !*message || !strcmpi(message, "party") ) { // Default is Party
			sd->state.noks = 2;
			clif_displaymessage(fd, msg_txt(1326)); // [ K.S Protection Active - Option: Party ]
		} else if( !strcmpi(message, "self") ) {
			sd->state.noks = 1;
			clif_displaymessage(fd, msg_txt(1327)); // [ K.S Protection Active - Option: Self ]
		} else if( !strcmpi(message, "guild") ) {
			sd->state.noks = 3;
			clif_displaymessage(fd, msg_txt(1328)); // [ K.S Protection Active - Option: Guild ]
		} else
			clif_displaymessage(fd, msg_txt(1329)); // Usage: @noks <self|party|guild>
	}
	return 0;
}
/*==========================================
 * Map Kill Steal Protection Setting
 *------------------------------------------*/
ACMD_FUNC(allowks)
{
	nullpo_retr(-1,sd);

	if( map[sd->bl.m].flag.allowks ) {
		map[sd->bl.m].flag.allowks = 0;
		clif_displaymessage(fd, msg_txt(1330)); // [ Map K.S Protection Active ]
	} else {
		map[sd->bl.m].flag.allowks = 1;
		clif_displaymessage(fd, msg_txt(1331)); // [ Map K.S Protection Inactive ]
	}
	return 0;
}

ACMD_FUNC(resetstat)
{
	nullpo_retr(-1, sd);
	
	pc_resetstate(sd);
	sprintf(atcmd_output, msg_txt(207), sd->status.name); // '%s' stats points reset.
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

ACMD_FUNC(resetskill)
{
	nullpo_retr(-1,sd);
	
	pc_resetskill(sd,1);
	sprintf(atcmd_output, msg_txt(206), sd->status.name); // '%s' skill points reset.
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * #storagelist: Displays the items list of a player's storage.
 * #cartlist: Displays contents of target's cart.
 * #itemlist: Displays contents of target's inventory.
 *------------------------------------------*/
ACMD_FUNC(itemlist)
{
	int i, j, count, counter;
	const char *location;
	const struct item *items;
	int size;
	StringBuf buf;

	nullpo_retr(-1, sd);

	parent_cmd = atcommand_checkalias(command + 1);

	if( !strcmp(parent_cmd, "storagelist") ) {
		location = "Storage";
		items = sd->storage.u.items_storage;
		size = sd->storage.max_amount;
	} else if( !strcmp(parent_cmd, "cartlist") ) {
		location = "Cart";
		items = sd->cart.u.items_cart;
		size = MAX_CART;
	} else if( !strcmp(parent_cmd, "itemlist") ) {
		location = "Inventory";
		items = sd->inventory.u.items_inventory;
		size = MAX_INVENTORY;
	} else
		return 1;

	StringBuf_Init(&buf);

	count = 0; // Total slots occupied
	counter = 0; // Total items found
	for( i = 0; i < size; ++i ) {
		const struct item *it = &items[i];
		struct item_data *itd;

		if( it->nameid == 0 || (itd = itemdb_exists(it->nameid)) == NULL )
			continue;

		counter += it->amount;
		count++;

		if( count == 1 ) {
			StringBuf_Printf(&buf, msg_txt(1332), location, sd->status.name); // ------ %s Items List of '%s' ------
			clif_displaymessage(fd, StringBuf_Value(&buf));
			StringBuf_Clear(&buf);
		}

		if( it->refine )
			StringBuf_Printf(&buf, "%d %s %+d (%s, id: %d)", it->amount, itd->jname, it->refine, itd->name, it->nameid);
		else
			StringBuf_Printf(&buf, "%d %s (%s, id: %d)", it->amount, itd->jname, itd->name, it->nameid);

		if( it->equip ) {
			char equipstr[CHAT_SIZE_MAX];

			strcpy(equipstr, msg_txt(1333)); // | Equipped:
			if( it->equip&EQP_GARMENT )
				strcat(equipstr, msg_txt(1334)); // Robe,
			if( it->equip&EQP_ACC_L )
				strcat(equipstr, msg_txt(1335)); // Left Accessory,
			if( it->equip&EQP_ARMOR )
				strcat(equipstr, msg_txt(1336)); // Body,
			if( (it->equip&EQP_ARMS) == EQP_HAND_R )
				strcat(equipstr, msg_txt(1337)); // Right Hand,
			if( (it->equip&EQP_ARMS) == EQP_HAND_L )
				strcat(equipstr, msg_txt(1338)); // Left Hand,
			if( (it->equip&EQP_ARMS) == EQP_ARMS )
				strcat(equipstr, msg_txt(1339)); // Both Hands,
			if( it->equip&EQP_SHOES )
				strcat(equipstr, msg_txt(1340)); // Shoes,
			if( it->equip&EQP_ACC_R )
				strcat(equipstr, msg_txt(1341)); // Right Accessory,
			if( (it->equip&EQP_HELM) == EQP_HEAD_LOW )
				strcat(equipstr, msg_txt(1342)); // Lower Head,
			if( (it->equip&EQP_HELM) == EQP_HEAD_TOP )
				strcat(equipstr, msg_txt(1343)); // Top Head,
			if( (it->equip&EQP_HELM) == (EQP_HEAD_LOW|EQP_HEAD_TOP) )
				strcat(equipstr, msg_txt(1344)); // Top/Lower Head,
			if( (it->equip&EQP_HELM) == EQP_HEAD_MID )
				strcat(equipstr, msg_txt(1345)); // Mid Head,
			if( (it->equip&EQP_HELM) == (EQP_HEAD_LOW|EQP_HEAD_MID) )
				strcat(equipstr, msg_txt(1346)); // Mid/Lower Head,
			if( (it->equip&EQP_HELM) == EQP_HELM )
				strcat(equipstr, msg_txt(1347)); // Top/Mid/Lower Head,
			if( (it->equip&EQP_COSTUME_HELM) == EQP_COSTUME_HEAD_LOW )
				strcat(equipstr, msg_txt(517)); // Lower Costume Head,
			if( (it->equip&EQP_COSTUME_HELM) == EQP_COSTUME_HEAD_TOP )
				strcat(equipstr, msg_txt(518)); // Top Costume Head,
			if( (it->equip&EQP_COSTUME_HELM) == (EQP_COSTUME_HEAD_LOW|EQP_COSTUME_HEAD_TOP) )
				strcat(equipstr, msg_txt(519)); // Top/Lower Costume Head,
			if( (it->equip&EQP_COSTUME_HELM) == EQP_COSTUME_HEAD_MID )
				strcat(equipstr, msg_txt(520)); // Mid Costume Head,
			if( (it->equip&EQP_COSTUME_HELM) == (EQP_COSTUME_HEAD_LOW|EQP_COSTUME_HEAD_MID) )
				strcat(equipstr, msg_txt(521)); // Mid/Lower Costume Head,
			if( (it->equip&EQP_COSTUME_HELM) == EQP_COSTUME_HELM )
				strcat(equipstr, msg_txt(522)); // Top/Mid/Lower Costume Head,
			if( it->equip&EQP_COSTUME_GARMENT )
				strcat(equipstr, msg_txt(523)); // Costume Robe,
			if( it->equip&EQP_AMMO )
				strcat(equipstr, msg_txt(525)); // Ammo,
			if( it->equip&EQP_SHADOW_ARMOR )
				strcat(equipstr, msg_txt(526)); // Shadow Body,
			if( (it->equip&EQP_SHADOW_ARMS) == EQP_SHADOW_WEAPON )
				strcat(equipstr, msg_txt(527)); // Shadow Right Hand,
			if( (it->equip&EQP_SHADOW_ARMS) == EQP_SHADOW_SHIELD )
				strcat(equipstr, msg_txt(528)); // Shadow Left Hand,
			if( (it->equip&EQP_SHADOW_ARMS) == EQP_SHADOW_ARMS )
				strcat(equipstr, msg_txt(529)); // Shadow Both Hands,
			if( it->equip&EQP_SHADOW_SHOES )
				strcat(equipstr, msg_txt(530)); // Shadow Shoes,
			if( it->equip&EQP_SHADOW_ACC_R )
				strcat(equipstr, msg_txt(531)); // Shadow Right Accessory,
			if( it->equip&EQP_SHADOW_ACC_L )
				strcat(equipstr, msg_txt(532)); // Shadow Left Accessory,
			// Remove final ', '
			equipstr[strlen(equipstr) - 2] = '\0';
			StringBuf_AppendStr(&buf, equipstr);
		}

		clif_displaymessage(fd, StringBuf_Value(&buf));
		StringBuf_Clear(&buf);

		if( it->card[0] == CARD0_PET ) { // Pet Egg
			if (it->card[3])
				StringBuf_Printf(&buf, msg_txt(1348), (unsigned int)MakeDWord(it->card[1], it->card[2])); // -> (Pet Egg, Pet ID: %u, Named)
			else
				StringBuf_Printf(&buf, msg_txt(1349), (unsigned int)MakeDWord(it->card[1], it->card[2])); // -> (Pet Egg, Pet ID: %u, Unnamed)
		} else if(it->card[0] == CARD0_FORGE) { // Forged Item
			StringBuf_Printf(&buf, msg_txt(1350), (unsigned int)MakeDWord(it->card[2], it->card[3]), it->card[1]>>8, it->card[1]&0x0f); // -> (Crafted item, Creator ID: %u, Star Crumbs %d, Element %d)
		} else if(it->card[0] == CARD0_CREATE) { // Created Item
			StringBuf_Printf(&buf, msg_txt(1351), (unsigned int)MakeDWord(it->card[2], it->card[3])); // -> (Produced Item, Creator ID: %u)
		} else { // Normal item
			int counter2 = 0;

			for( j = 0; j < itd->slot; ++j ) {
				struct item_data *card;

				if( it->card[j] == 0 || (card = itemdb_exists(it->card[j])) == NULL )
					continue;

				counter2++;

				if( counter2 == 1 )
					StringBuf_AppendStr(&buf, msg_txt(1352)); //  -> (Card(s):

				if( counter2 != 1 )
					StringBuf_AppendStr(&buf, ", ");

				StringBuf_Printf(&buf, "#%d %s (id: %d)", counter2, card->jname, card->nameid);
			}

			if( counter2 > 0 )
				StringBuf_AppendStr(&buf, ")");
		}

		if( StringBuf_Length(&buf) > 0 )
			clif_displaymessage(fd, StringBuf_Value(&buf));

		StringBuf_Clear(&buf);
	}

	if( count == 0 )
		StringBuf_Printf(&buf, msg_txt(1353), location); // No item found in this player's %s.
	else
		StringBuf_Printf(&buf, msg_txt(1354), counter, count, location); // %d item(s) found in %d %s slots.

	clif_displaymessage(fd, StringBuf_Value(&buf));

	StringBuf_Destroy(&buf);

	return 0;
}

ACMD_FUNC(stats)
{
	char job_jobname[100];
	char output[CHAT_SIZE_MAX];
	int i;
	struct {
		const char *format;
		int value;
	} output_table[] = {
		{ "Base Level - %d", 0 },
		{ NULL, 0 },
		{ "Hp - %d", 0 },
		{ "MaxHp - %d", 0 },
		{ "Sp - %d", 0 },
		{ "MaxSp - %d", 0 },
		{ "Str - %3d", 0 },
		{ "Agi - %3d", 0 },
		{ "Vit - %3d", 0 },
		{ "Int - %3d", 0 },
		{ "Dex - %3d", 0 },
		{ "Luk - %3d", 0 },
		{ "Zeny - %d", 0 },
		{ "Free SK Points - %d", 0 },
		{ "JobChangeLvl (2nd) - %d", 0 },
		{ "JobChangeLvl (3rd) - %d", 0 },
		{ NULL, 0 }
	};

	memset(job_jobname, '\0', sizeof(job_jobname));
	memset(output, '\0', sizeof(output));

	//direct array initialization with variables is not standard C compliant.
	output_table[0].value = sd->status.base_level;
	output_table[1].format = job_jobname;
	output_table[1].value = sd->status.job_level;
	output_table[2].value = sd->status.hp;
	output_table[3].value = sd->status.max_hp;
	output_table[4].value = sd->status.sp;
	output_table[5].value = sd->status.max_sp;
	output_table[6].value = sd->status.str;
	output_table[7].value = sd->status.agi;
	output_table[8].value = sd->status.vit;
	output_table[9].value = sd->status.int_;
	output_table[10].value = sd->status.dex;
	output_table[11].value = sd->status.luk;
	output_table[12].value = sd->status.zeny;
	output_table[13].value = sd->status.skill_point;
	output_table[14].value = sd->change_level_2nd;
	output_table[15].value = sd->change_level_3rd;

	sprintf(job_jobname, "Job - %s %s", job_name(sd->status.class_), "(level %d)");
	sprintf(output, msg_txt(53), sd->status.name); // '%s' stats:

	clif_displaymessage(fd, output);
	
	for (i = 0; output_table[i].format != NULL; i++) {
		sprintf(output, output_table[i].format, output_table[i].value);
		clif_displaymessage(fd, output);
	}

	return 0;
}

ACMD_FUNC(delitem)
{
	char item_name[100];
	unsigned short nameid;
	int amount = 0, total, idx;
	struct item_data *id;

	nullpo_retr(-1, sd);

	if( !message || !*message || ( sscanf(message, "\"%99[^\"]\" %d", item_name, &amount) < 2 && sscanf(message, "%99s %d", item_name, &amount) < 2 ) || amount < 1 )
	{
		clif_displaymessage(fd, msg_txt(1355)); // Please enter an item name/ID, a quantity, and a player name (usage: #delitem <player> <item_name_or_ID> <quantity>).
		return -1;
	}

	if( (id = itemdb_searchname(item_name)) || (id = itemdb_exists(atoi(item_name))) )
		nameid = id->nameid;
	else {
		clif_displaymessage(fd, msg_txt(19)); // Invalid item ID or name.
		return -1;
	}

	total = amount;

	// Delete items
	while( amount && (idx = pc_search_inventory(sd, nameid)) != INDEX_NOT_FOUND ) {
		int delamount = (amount < sd->inventory.u.items_inventory[idx].amount) ? amount : sd->inventory.u.items_inventory[idx].amount;

		if( sd->inventory_data[idx]->type == IT_PETEGG && sd->inventory.u.items_inventory[idx].card[0] == CARD0_PET && CheckForCharServer() )
			intif_delete_petdata(MakeDWord(sd->inventory.u.items_inventory[idx].card[1], sd->inventory.u.items_inventory[idx].card[2])); // Delete pet
		pc_delitem(sd, idx, delamount, 0, 0, LOG_TYPE_COMMAND);

		amount-= delamount;
	}

	// Notify target
	sprintf(atcmd_output, msg_txt(113), total - amount); // %d item(s) removed by a GM.
	clif_displaymessage(sd->fd, atcmd_output);

	// Notify source
	if( amount == total ) {
		clif_displaymessage(fd, msg_txt(116)); // Character does not have the item.
	} else if( amount ) {
		sprintf(atcmd_output, msg_txt(115), total-amount, total-amount, total); // %d item(s) removed. Player had only %d on %d items.
		clif_displaymessage(fd, atcmd_output);
	} else {
		sprintf(atcmd_output, msg_txt(114), total); // %d item(s) removed from the player.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Custom Fonts
 *------------------------------------------*/
ACMD_FUNC(font)
{
	int font_id;
	nullpo_retr(-1, sd);

	font_id = atoi(message);
	if( font_id == 0 ) {
		if( sd->status.font ) {
			sd->status.font = 0;
			clif_displaymessage(fd, msg_txt(1356)); // Returning to normal font.
			clif_font(sd);
		} else {
			clif_displaymessage(fd, msg_txt(1357)); // Use @font <1-9> to change your message font.
			clif_displaymessage(fd, msg_txt(1358)); // Use 0 or no parameter to return to normal font.
		}
	} else if( font_id < 0 || font_id > 9 )
		clif_displaymessage(fd, msg_txt(1359)); // Invalid font. Use a value from 0 to 9.
	else if( font_id != sd->status.font ) {
		sd->status.font = font_id;
		clif_font(sd);
		clif_displaymessage(fd, msg_txt(1360)); // Font changed.
	} else
		clif_displaymessage(fd, msg_txt(1361)); // Already using this font.

	return 0;
}

/*==========================================
 * type: 1 = commands (@), 2 = charcommands (#)
 *------------------------------------------*/
static void atcommand_commands_sub(struct map_session_data *sd, const int fd, AtCommandType type)
{
	char line_buff[CHATBOX_SIZE];
	char *cur = line_buff;
	AtCommandInfo *cmd;
	DBIterator *iter = db_iterator(atcommand_db);
	int count = 0;

	memset(line_buff, ' ', CHATBOX_SIZE);
	line_buff[CHATBOX_SIZE - 1] = 0;

	clif_displaymessage(fd, msg_txt(273)); // "Commands available:"

	for( cmd = dbi_first(iter); dbi_exists(iter); cmd = dbi_next(iter) ) {
		unsigned int slen = 0;

		switch( type ) {
			case COMMAND_CHARCOMMAND:
				if( cmd->char_groups[sd->group_pos] == 0 )
					continue;
				break;
			case COMMAND_ATCOMMAND:
				if( cmd->at_groups[sd->group_pos] == 0 )
					continue;
				break;
			default:
				continue;
		}

		slen = strlen(cmd->command);

		// Flush the text buffer if this command won't fit into it
		if( slen + cur - line_buff >= CHATBOX_SIZE ) {
			clif_displaymessage(fd, line_buff);
			cur = line_buff;
			memset(line_buff, ' ', CHATBOX_SIZE);
			line_buff[CHATBOX_SIZE - 1] = 0;
		}

		memcpy(cur, cmd->command, slen);
		cur += slen + (10 - slen%10);

		count++;
	}
	dbi_destroy(iter);
	clif_displaymessage(fd,line_buff);

	if( atcmd_binding_count ) {
		int i, count_bind, gm_lvl = pc_get_group_level(sd);

		for( i = count_bind = 0; i < atcmd_binding_count; i++ ) {
			if( gm_lvl >= ((type - 1) ? atcmd_binding[i]->level2 : atcmd_binding[i]->level) ) {
				unsigned int slen = strlen(atcmd_binding[i]->command);

				if( count_bind == 0 ) {
					cur = line_buff;
					memset(line_buff, ' ', CHATBOX_SIZE);
					line_buff[CHATBOX_SIZE - 1] = 0;
					clif_displaymessage(fd, "-----------------");
					clif_displaymessage(fd, msg_txt(509)); // Script-bound commands:
				}
				if( slen + cur - line_buff >= CHATBOX_SIZE ) {
					clif_displaymessage(fd, line_buff);
					cur = line_buff;
					memset(line_buff, ' ', CHATBOX_SIZE);
					line_buff[CHATBOX_SIZE - 1] = 0;
				}
				memcpy(cur, atcmd_binding[i]->command, slen);
				cur += slen + (10 - slen%10);
				count_bind++;
			}
		}
		if( count_bind )
			clif_displaymessage(fd, line_buff); // Last one
		count += count_bind;
		
	}

	sprintf(atcmd_output, msg_txt(274), count); // "%d commands found."
	clif_displaymessage(fd, atcmd_output);

	return;
}

/*==========================================
 * @commands Lists available @ commands to you
 *------------------------------------------*/
ACMD_FUNC(commands)
{
	atcommand_commands_sub(sd, fd, COMMAND_ATCOMMAND);
	return 0;
}

/*==========================================
 * @charcommands Lists available # commands to you
 *------------------------------------------*/
ACMD_FUNC(charcommands)
{
	atcommand_commands_sub(sd, fd, COMMAND_CHARCOMMAND);
	return 0;
}

//For new mounts
ACMD_FUNC(mount2) {
	clif_displaymessage(sd->fd,msg_txt(1362)); // NOTICE: If you crash with mount your LUA is outdated.
	if( !sd->sc.data[SC_ALL_RIDING] ) {
		clif_displaymessage(sd->fd, msg_txt(1363)); // You have mounted.
		sc_start(&sd->bl, &sd->bl, SC_ALL_RIDING, 100, 0, -1);
	} else {
		clif_displaymessage(sd->fd, msg_txt(1364)); // You have released your mount.
		status_change_end(&sd->bl, SC_ALL_RIDING, INVALID_TIMER);
	}
	return 0;
}

ACMD_FUNC(accinfo) {
	char query[NAME_LENGTH];

	if (!message || !*message || strlen(message) > NAME_LENGTH ) {
		clif_displaymessage(fd, msg_txt(1365)); // Usage: @accinfo/@accountinfo <account_id/char name>
		clif_displaymessage(fd, msg_txt(1366)); // You may search partial name by making use of '%' in the search, ex. "@accinfo %Mario%" lists all characters whose name contains "Mario".
		return -1;
	}
	// Remove const type
	safestrncpy(query, message, NAME_LENGTH);
	intif_request_accinfo(sd->fd, sd->bl.id, pc_get_group_level(sd), query);
	return 0;
}

// [Ind]
ACMD_FUNC(set) {
	char reg[32], val[128];
	struct script_data *data;
	int toset = 0, len;
	bool is_str = false;

	if( !message || !*message || (toset = sscanf(message, "%31s %128[^\n]s", reg, val)) < 1  ) {
		clif_displaymessage(fd, msg_txt(1367)); // Usage: @set <variable name> <value>
		clif_displaymessage(fd, msg_txt(1368)); // Usage: ex. "@set PoringCharVar 50"
		clif_displaymessage(fd, msg_txt(1369)); // Usage: ex. "@set PoringCharVarSTR$ Super Duper String"
		clif_displaymessage(fd, msg_txt(1370)); // Usage: ex. "@set PoringCharVarSTR$" outputs its value, Super Duper String.
		return -1;
	}

	// Disabled variable types (they require a proper script state to function, so allowing them would crash the server)
	if( reg[0] == '.' ) {
		clif_displaymessage(fd, msg_txt(1371)); // NPC variables may not be used with @set.
		return -1;
	} else if( reg[0] == '\'' ) {
		clif_displaymessage(fd, msg_txt(1372)); // Instance variables may not be used with @set.
		return -1;
	}

	is_str = (reg[strlen(reg) - 1] == '$' ? true : false);

	if( (len = strlen(val)) > 1 ) {
		if( val[0] == '"' && val[len - 1] == '"') {
			val[len - 1] = '\0'; // Strip quotes.
			memmove(val, val + 1, len - 1);
		}
	}

	if( toset >= 2 ) { // We only set the var if there is an val, otherwise we only output the value
		if( is_str )
			set_var(sd, reg, (void *)val);
		else
			set_var(sd, reg, (void *)__64BPRTSIZE((atoi(val))));
	}

	CREATE(data, struct script_data, 1);

	if( is_str ) { // String variable
		switch( reg[0] ) {
			case '@':
				data->u.str = pc_readregstr(sd, add_str(reg));
				break;
			case '$':
				data->u.str = mapreg_readregstr(add_str(reg));
				break;
			case '#':
				if( reg[1] == '#' )
					data->u.str = pc_readaccountreg2str(sd, reg); // Global
				else
					data->u.str = pc_readaccountregstr(sd, reg); // Local
				break;
			default:
				data->u.str = pc_readglobalreg_str(sd, reg);
				break;
		}
		if( !data->u.str || data->u.str[0] == '\0' ) { // Empty string
			data->type = C_CONSTSTR;
			data->u.str = "";
		} else { // Duplicate string
			data->type = C_STR;
			data->u.str = aStrdup(data->u.str);
		}
	} else { // Integer variable
		data->type = C_INT;
		switch( reg[0] ) {
			case '@':
				data->u.num = pc_readreg(sd, add_str(reg));
				break;
			case '$':
				data->u.num = mapreg_readreg(add_str(reg));
				break;
			case '#':
				if( reg[1] == '#' )
					data->u.num = pc_readaccountreg2(sd, reg); // Global
				else
					data->u.num = pc_readaccountreg(sd, reg); // Local
				break;
			default:
				data->u.num = pc_readglobalreg(sd, reg);
				break;
		}
	}

	switch( data->type ) {
		case C_INT:
			sprintf(atcmd_output, msg_txt(1373), reg, data->u.num); // %s value is now :%d
			break;
		case C_STR:
			sprintf(atcmd_output, msg_txt(1374), reg, data->u.str); // %s value is now :%s
			break;
		case C_CONSTSTR:
			sprintf(atcmd_output, msg_txt(1375), reg); // %s is empty
			break;
		default:
			sprintf(atcmd_output, msg_txt(1376), reg, data->type); // %s data type is not supported :%u
			break;
	}

	clif_displaymessage(fd, atcmd_output);

	if( is_str && data->u.str )
		aFree(data->u.str);
	aFree(data);

	return 0;
}

ACMD_FUNC(addperm) {
	int perm_size = ARRAYLENGTH(pc_g_permission_name);
	bool add;
	int i;

	parent_cmd = atcommand_checkalias(command + 1);

	add = (strcmpi(parent_cmd, "addperm") == 0 ? true : false);

	if( !message || !*message ) {
		sprintf(atcmd_output,  msg_txt(1378),command); // Usage: %s <permission_name>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1379)); // -- Permission List
		for( i = 0; i < perm_size; i++ ) {
			sprintf(atcmd_output,"- %s",pc_g_permission_name[i].name);
			clif_displaymessage(fd, atcmd_output);
		}
		return -1;
	}

	ARR_FIND(0, perm_size, i, strcmpi(pc_g_permission_name[i].name, message) == 0);

	if( i == perm_size ) {
		sprintf(atcmd_output,msg_txt(1380),message); // '%s' is not a known permission.
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1379)); // -- Permission List
		for( i = 0; i < perm_size; i++ ) {
			sprintf(atcmd_output,"- %s",pc_g_permission_name[i].name);
			clif_displaymessage(fd, atcmd_output);
		}
		return -1;
	}

	if( add && (sd->permissions&pc_g_permission_name[i].permission) ) {
		sprintf(atcmd_output,  msg_txt(1381),sd->status.name,pc_g_permission_name[i].name); // User '%s' already possesses the '%s' permission.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	} else if ( !add && !(sd->permissions&pc_g_permission_name[i].permission) ) {
		sprintf(atcmd_output,  msg_txt(1382),sd->status.name,pc_g_permission_name[i].name); // User '%s' doesn't possess the '%s' permission.
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output,msg_txt(1383),sd->status.name); // -- User '%s' Permissions
		clif_displaymessage(fd, atcmd_output);
		for( i = 0; i < perm_size; i++ ) {
			if( sd->permissions&pc_g_permission_name[i].permission ) {
				sprintf(atcmd_output,"- %s",pc_g_permission_name[i].name);
				clif_displaymessage(fd, atcmd_output);
			}
		}
		
		return -1;
	}

	if( add )
		sd->permissions |= pc_g_permission_name[i].permission;
	else
		sd->permissions &=~ pc_g_permission_name[i].permission;

	sprintf(atcmd_output, msg_txt(1384),sd->status.name); // User '%s' permissions updated successfully. The changes are temporary.
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

ACMD_FUNC(unloadnpcfile) {

	if( !message || !*message ) {
		clif_displaymessage(fd, msg_txt(1385)); // Usage: @unloadnpcfile <file name>
		return -1;
	}

	if( npc_unloadfile(message) )
		clif_displaymessage(fd, msg_txt(1386)); // File unloaded. Be aware that mapflags and monsters spawned directly are not removed.
	else {
		clif_displaymessage(fd, msg_txt(1387)); // File not found.
		return -1;
	}

	return 0;
}

ACMD_FUNC(reloadnpcfile) {
	if( !message || !*message ) {
		clif_displaymessage(fd, msg_txt(543)); // Please enter a NPC file name (usage: @reloadnpcfile <file name>).
		return -1;
	}

	if( npc_unloadfile(message) )
		clif_displaymessage(fd, msg_txt(1386)); // File unloaded. Be aware that mapflags and monsters spawned directly are not removed.

	if( !npc_addsrcfile(message, true) ) {
		clif_displaymessage(fd, msg_txt(261)); // Script could not be loaded.
		return -1;
	}

	npc_read_event_script();

	clif_displaymessage(fd, msg_txt(262)); // Script loaded.
	return 0;
}

ACMD_FUNC(cart) {
#define MC_CART_MDFY(x) \
	sd->status.skill[MC_PUSHCART].id = x ? MC_PUSHCART : 0; \
	sd->status.skill[MC_PUSHCART].lv = x ? 1 : 0; \
	sd->status.skill[MC_PUSHCART].flag = x ? SKILL_FLAG_TEMPORARY : SKILL_FLAG_PERMANENT;

	int val;
	bool lv = (pc_checkskill(sd, MC_PUSHCART) ? false : true);

	if( message )
		val = atoi(message);
	if( !message || !*message || val < 0 || val > MAX_CARTS ) {
		sprintf(atcmd_output, msg_txt(1390), command, MAX_CARTS); // Unknown Cart (usage: %s <0-%d>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if( val == 0 && !pc_iscarton(sd) ) {
		clif_displaymessage(fd, msg_txt(1391)); // You do not possess a cart to be removed
		return -1;
	}

	if( lv )
		MC_CART_MDFY(1);

	if( !pc_setcart(sd, val) ) {
		if( lv )
			MC_CART_MDFY(0);
		return -1; // @cart failed
	}

	if( lv )
		MC_CART_MDFY(0);

	clif_displaymessage(fd, msg_txt(1392)); // Cart Added

	return 0;
#undef MC_CART_MDFY
}

/* Channel System [Ind] */
ACMD_FUNC(join) {
	char chname[CHAN_NAME_LENGTH], pass[CHAN_NAME_LENGTH];

	if( !message || !*message || sscanf(message, "%19s %19s", chname, pass) < 1 ) {
		sprintf(atcmd_output, msg_txt(1399),command); // Unknown channel (usage: %s <#channel_name>)
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	return channel_pcjoin(sd, chname, pass);
}
/**
 * Display available option for @channel command
 * @command : the name of used command (for alias case)
 */
static inline void atcmd_channel_help(struct map_session_data *sd, const char *command) {
	int fd = sd->fd;
	bool can_delete = pc_has_permission(sd, PC_PERM_CHANNEL_ADMIN);
	bool can_create = (can_delete || channel_config.private_channel.allow);
	clif_displaymessage(fd, msg_txt(1414)); // ---- Available options:

	//Option create
	if( can_create ) {
		sprintf(atcmd_output, msg_txt(1415),command); // * %s create <#channel_name> <channel_password>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1416)); // -- Creates a new channel.
	}

	//Option delete
	if( can_delete ) {
		sprintf(atcmd_output, msg_txt(1469),command); // * %s delete <channel_name>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1470)); // -- Destroys the specified channel.
	}

	//Option list
	sprintf(atcmd_output, msg_txt(1417),command); // * %s list
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, msg_txt(1418)); // -- Lists all public channels.
	sprintf(atcmd_output, msg_txt(1471),command); // * %s list mine
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, msg_txt(1472)); // -- Lists all channels you have joined.
	if( can_create ) {
		sprintf(atcmd_output, msg_txt(1419),command); // * %s list colors
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1420)); // -- Lists all available colors for custom channels.
	}

	//Option setcolor
	if( can_create ) {
		sprintf(atcmd_output, msg_txt(1421),command); // * %s setcolor <#channel_name> <color_name>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1422)); // -- Changes channel text to the specified color (channel owners only).
	}

	//Option join
	sprintf(atcmd_output, msg_txt(1473),command); // * %s join <#channel_name> <channel_password>
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, msg_txt(1474)); // -- Joins the specified channel.

	//Option leave
	sprintf(atcmd_output, msg_txt(1423),command); // * %s leave <#channel_name>
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, msg_txt(1424)); // -- Leaves the specified channel.

	//Option bindto
	sprintf(atcmd_output, msg_txt(1427),command); // * %s bindto <#channel_name>
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, msg_txt(1428)); // -- Binds your global chat to the specified channel, sending all global messages to that channel.

	//Option unbind
	sprintf(atcmd_output, msg_txt(1429),command); // * %s unbind
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, msg_txt(1430)); // -- Unbinds your global chat from the attached channel, if any.

	//Option ban/unban/banlist
	if( can_create ) {
		sprintf(atcmd_output, msg_txt(1456),command); // * %s ban <#channel_name> <player>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1457)); // -- Bans the specified player from the channel.
		sprintf(atcmd_output, msg_txt(1458),command); // * %s banlist <#channel_name>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1459)); // -- Lists all players banned from the specified channel.
		sprintf(atcmd_output, msg_txt(1460),command); // * %s unban <#channel_name> <player>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1461)); // -- Unbans the specified player from the channel.
		sprintf(atcmd_output, msg_txt(1467),command); // * %s unbanall <#channel_name>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1468)); // -- Clears all bans from the specified channel.
	}

	//Option setopt
	if( can_create ) {
		sprintf(atcmd_output, msg_txt(1462),command); // * %s setopt <#channel_name> <option> <value>
		clif_displaymessage(fd, atcmd_output);
		clif_displaymessage(fd, msg_txt(1463)); // -- Sets an option and value for the specified channel.
	}

	sprintf(atcmd_output, msg_txt(1404),command); // %s failed.
	clif_displaymessage(fd, atcmd_output);
}

ACMD_FUNC(channel) {
	char key[NAME_LENGTH], sub1[CHAN_NAME_LENGTH], sub2[64];
	sub1[0] = sub2[0] = '\0';

	if( !message || !*message || sscanf(message, "%23s %19s %63[^\n]", key, sub1, sub2) < 1 ) {
		atcmd_channel_help(sd,command);
		return 0;
	}

	if( strcmpi(key, "delete") == 0 && pc_has_permission(sd, PC_PERM_CHANNEL_ADMIN) )
		return channel_pcdelete(sd, sub1);
	else if( strcmpi(key, "list") == 0 )
		return channel_display_list(sd, sub1);
	else if( strcmpi(key, "setcolor") == 0 )
		return channel_pccolor(sd, sub1, sub2);
	else if( strcmpi(key, "join") == 0 )
		return channel_pcjoin(sd, sub1, sub2);
	else if( strcmpi(key, "leave") == 0 )
		return channel_pcleave(sd, sub1);
	else if( strcmpi(key, "bindto") == 0 )
		return channel_pcbind(sd, sub1);
	else if( strcmpi(key, "unbind") == 0 )
		return channel_pcunbind(sd);
	else if( strcmpi(key, "ban") == 0 )
		return channel_pcban(sd, sub1, sub2, 0);
	else if( strcmpi(key, "kick") == 0 )
		return channel_pckick(sd, sub1, sub2);
	else if( strcmpi(key, "banlist") == 0 )
		return channel_pcban(sd, sub1, NULL, 3);
	else if( strcmpi(key, "unban") == 0 )
		return channel_pcban(sd, sub1, sub2, 1);
	else if( strcmpi(key, "unbanall") == 0 )
		return channel_pcban(sd, sub1, NULL, 2);
	else {
		char sub3[CHAN_NAME_LENGTH], sub4[CHAN_NAME_LENGTH];

		sub3[0] = sub4[0] = '\0';
		sscanf(sub2, "%19s %19s", sub3, sub4);
		if( strcmpi(key, "create") == 0 && (channel_config.private_channel.allow || pc_has_permission(sd, PC_PERM_CHANNEL_ADMIN)) ) {
			if( sub4[0] != '\0' ) {
				clif_displaymessage(fd, msg_txt(1408)); // Channel password may not contain spaces.
				return -1;
			}
			return channel_pccreate(sd, sub1, sub3);
		} else if( strcmpi(key, "setopt") == 0 )
			return channel_pcsetopt(sd, sub1, sub3, sub4);
		atcmd_channel_help(sd, command);
	}

	return 0;
}

ACMD_FUNC(fontcolor)
{
	if( !message || !*message ) {
		channel_display_list(sd, "colors");
		return -1;
	}

	if( strcmpi(message, "Normal") == 0 ) {
		sd->fontcolor = 0;
	} else {
		unsigned char k;

		ARR_FIND(0, channel_config.colors_count,k, (strcmpi(message, channel_config.colors_name[k]) == 0));
		if( k == channel_config.colors_count ) {
			sprintf(atcmd_output, msg_txt(1411), message); // Unknown color '%s'.
			clif_displaymessage(fd, atcmd_output);
			return -1;
		}
		sd->fontcolor = k;
	}

	sprintf(atcmd_output, msg_txt(1454), message); // Color set to '%s'.
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

ACMD_FUNC(costume) {
	const char *names[] = {
		"Wedding",
		"Xmas",
		"Summer",
		"Summer2",
		"Hanbok",
		"Oktoberfest",
	};
	const int name2id[] = {
		SC_WEDDING,
		SC_XMAS,
		SC_SUMMER,
		SC_DRESSUP,
		SC_HANBOK,
		SC_OKTOBERFEST
	};
	unsigned short k = 0, len = ARRAYLENGTH(names);

	if( !message || !*message ) {
		for( k = 0; k < len; k++ ) {
			if( sd->sc.data[name2id[k]] ) {
				sprintf(atcmd_output, msg_txt(1488), names[k]); // '%s' costume removed.
				clif_displaymessage(sd->fd, atcmd_output);
				status_change_end(&sd->bl, (sc_type)name2id[k], INVALID_TIMER);
				return 0;
			}
		}

		clif_displaymessage(sd->fd, msg_txt(1487)); // Available Costumes
		for( k = 0; k < len; k++ ) {
			sprintf(atcmd_output, msg_txt(1486), names[k]); // -- %s
			clif_displaymessage(sd->fd, atcmd_output);
		}
		return -1;
	}

	for( k = 0; k < len; k++ ) {
		if( sd->sc.data[name2id[k]] ) {
			sprintf(atcmd_output, msg_txt(1485), names[k]); // You're already with a '%s' costume, type '@costume' to remove it.
			clif_displaymessage(sd->fd, atcmd_output);
			return -1;
		}
	}

	for( k = 0; k < len; k++ )
		if( strcmpi(message, names[k]) == 0 )
			break;

	if( k == len ) {
		sprintf(atcmd_output, msg_txt(1484), message); // '%s' is an unknown costume.
		clif_displaymessage(sd->fd, atcmd_output);
		return -1;
	}

	sc_start(&sd->bl, &sd->bl, (sc_type)name2id[k], 100, (name2id[k] == SC_DRESSUP ? 1 : 0), -1);

	return 0;
}

#ifdef VIP_ENABLE
ACMD_FUNC(vip) {
	struct map_session_data *pl_sd = NULL;
	char *modif_p;
	int32 vipdifftime = 0;
	time_t now = time(NULL);
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%255s %23[^\n]",atcmd_output,atcmd_player_name) < 2) {
		clif_displaymessage(fd, msg_txt(700)); // Usage: @vip <+/-time> <player name>
		return -1;
	}

	atcmd_output[sizeof(atcmd_output) - 1] = '\0';

	modif_p = atcmd_output;
	vipdifftime = (int32)solve_time(modif_p);

	if (!vipdifftime) {
		clif_displaymessage(fd, msg_txt(701)); // Invalid time for vip command.
		clif_displaymessage(fd, msg_txt(702)); // Time parameter format is <+/-value> to alter. y/a = Year, m = Month, d/j = Day, h = Hour, n/mn = Minute, s = Second.
		return -1;
	}

	if (!(pl_sd = map_nick2sd(atcmd_player_name))) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) {
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to do this action on this player.
		return -1;
	}

	if (!pl_sd->vip.time)
		pl_sd->vip.time = now;

	pl_sd->vip.time += vipdifftime; // Increase or reduce VIP duration

	if (pl_sd->vip.time <= now) {
		clif_displaymessage(pl_sd->fd, msg_txt(703)); // GM has removed your VIP time.
		clif_displaymessage(fd, msg_txt(704)); // Player is no longer VIP.
	} else {
		int year, month, day, hour, minute, second;
		char timestr[21];

		split_time((int)(pl_sd->vip.time - now), &year, &month, &day, &hour, &minute, &second);
		sprintf(atcmd_output, msg_txt(705), year, month, day, hour, minute); // Your VIP status is valid for %d years, %d months, %d days, %d hours and %d minutes.
		clif_displaymessage(pl_sd->fd, atcmd_output);
		timestamp2string(timestr, 20, pl_sd->vip.time, "%Y-%m-%d %H:%M");
		sprintf(atcmd_output, msg_txt(707), timestr); // You are VIP until : %s
		clif_displaymessage(pl_sd->fd, atcmd_output);
		if (pl_sd != sd) {
			sprintf(atcmd_output, msg_txt(706), pl_sd->status.name, year, month, day, hour, minute); // Player '%s' is now VIP for %d years, %d months, %d days, %d hours and %d minutes.
			clif_displaymessage(fd, atcmd_output);
			sprintf(atcmd_output, msg_txt(708), timestr); // The player is now VIP until : %s
			clif_displaymessage(fd, atcmd_output);
		}
	}

	chrif_req_login_operation(pl_sd->status.account_id, pl_sd->status.name, CHRIF_OP_LOGIN_VIP, vipdifftime, 0x1|0x2|0x4);

	return 0;
}

/**
 * Enable/disable rate info
 */
ACMD_FUNC(showrate) {
	nullpo_retr(-1,sd);

	if (!sd->vip.disableshowrate) {
		sprintf(atcmd_output, msg_txt(718)); //Personal rate information is not displayed now.
		sd->vip.disableshowrate = 1;
	} else {
		sprintf(atcmd_output, msg_txt(719)); //Personal rate information will be shown.
		sd->vip.disableshowrate = 0;
	}

	clif_displaymessage(fd, atcmd_output);

	return 0;
}
#endif

ACMD_FUNC(fullstrip) {
	int i;
	TBL_PC *tsd;

	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, msg_txt(349)); // Please enter a player name (usage: @fullstrip/@warpto/@goto <char name/ID>).
		return -1;
	}

	if ((tsd = map_nick2sd(atcmd_player_name)) == NULL && (tsd = map_id2sd(atoi(atcmd_player_name))) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	for (i = 0; i < EQI_MAX; i++) {
		if (tsd->equip_index[i] >= 0)
			pc_unequipitem(tsd, tsd->equip_index[i], 2);
	}

	return 0;
}

ACMD_FUNC(changedress) {
	enum sc_type name2id[] = {
		SC_WEDDING,
		SC_XMAS,
		SC_SUMMER,
		SC_DRESSUP,
		SC_HANBOK,
		SC_OKTOBERFEST
	};
	uint8 i;

	for( i = 0; i < ARRAYLENGTH(name2id); i++ ) {
		if( sd->sc.data[name2id[i]] ) {
			status_change_end(&sd->bl, name2id[i], INVALID_TIMER);
			// You should only be able to have one - so we cancel here
			return 0;
		}
	}

	return -1;
}

/**
 * Clone other player's equipments
 * Usage: @cloneequip <char name/ID>
 * http://rathena.org/board/topic/95076-new-atcommands-suggestion/
 * @author [Cydh], [Antares]
 */
ACMD_FUNC(cloneequip) {
	struct map_session_data *pl_sd;

	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		sprintf(atcmd_output, msg_txt(721), command); // Usage: %s <char name/ID>
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (!(pl_sd = map_nick2sd(atcmd_player_name)) && !(pl_sd = map_charid2sd(atoi(atcmd_player_name)))) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (sd == pl_sd) {
		memset(atcmd_output, '\0', sizeof(atcmd_output));
		sprintf(atcmd_output, msg_txt(720), "equip"); // Cannot clone your own %.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) {
		memset(atcmd_output, '\0', sizeof(atcmd_output));
		sprintf(atcmd_output, msg_txt(722), "equip"); // Cannot clone %s from this player.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	} else {
		int8 i;

		for (i = 0; i < EQI_MAX; i++) {
			short idx;
			char flag = 0;
			struct item tmp_item;

			if ((idx = pl_sd->equip_index[i]) < 0)
				continue;
			if (i == EQI_AMMO)
				continue;
			if (pc_is_same_equip_index((enum equip_index) i, pl_sd->equip_index, idx))
				continue;
			tmp_item = pl_sd->inventory.u.items_inventory[idx];
			if (itemdb_isspecial(tmp_item.card[0]))
				memset(tmp_item.card, 0, sizeof(tmp_item.card));
			tmp_item.bound = 0;
			tmp_item.expire_time = 0;
			tmp_item.unique_id = 0;
			tmp_item.favorite = 0;
			tmp_item.amount = 1;
			if ((flag = pc_additem(sd, &tmp_item, 1, LOG_TYPE_COMMAND)))
				clif_additem(sd, 0, 0, flag);
			else
				pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(tmp_item.nameid));
		}
	}
	memset(atcmd_output, '\0', sizeof(atcmd_output));
	sprintf(atcmd_output, msg_txt(724), "equip"); // Clone '%s' is done.
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/**
 * Clone other player's statuses/parameters using method same like ACMD_FUNC(param), doesn't use stat point
 * Usage: @clonestat <char name/ID>
 * http://rathena.org/board/topic/95076-new-atcommands-suggestion/
 * @author [Cydh], [Antares]
 */
ACMD_FUNC(clonestat) {
	struct map_session_data *pl_sd;

	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		sprintf(atcmd_output, msg_txt(721), command); // Usage: %s <char name/ID>
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (!(pl_sd = map_nick2sd(atcmd_player_name)) && !(pl_sd = map_charid2sd(atoi(atcmd_player_name)))) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	if (sd == pl_sd) {
		memset(atcmd_output, '\0', sizeof(atcmd_output));
		sprintf(atcmd_output, msg_txt(720), "status"); // Cannot clone your own %.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (pc_get_group_level(sd) < pc_get_group_level(pl_sd)) {
		memset(atcmd_output, '\0', sizeof(atcmd_output));
		sprintf(atcmd_output, msg_txt(722), "status"); // Cannot clone %s from this player.
		clif_displaymessage(fd, atcmd_output);
		return -1;
	} else {
		uint8 i;
		short max_status[6];

		pc_resetstate(sd);
		if (pc_has_permission(sd, PC_PERM_BYPASS_STAT_ONCLONE))
			max_status[0] = max_status[1] = max_status[2] = max_status[3] = max_status[4] = max_status[5] = SHRT_MAX;
		else {
			max_status[0] = pc_maxparameter(sd, PARAM_STR);
			max_status[1] = pc_maxparameter(sd, PARAM_AGI);
			max_status[2] = pc_maxparameter(sd, PARAM_VIT);
			max_status[3] = pc_maxparameter(sd, PARAM_INT);
			max_status[4] = pc_maxparameter(sd, PARAM_DEX);
			max_status[5] = pc_maxparameter(sd, PARAM_LUK);
		}

#define clonestat_check(cmd,stat)\
		{\
			memset(atcmd_output, '\0', sizeof(atcmd_output));\
			if (pl_sd->status.cmd > max_status[(stat)]) {\
				sprintf(atcmd_output, msg_txt(723), #cmd, pl_sd->status.cmd, max_status[(stat)]);\
				clif_displaymessage(fd, atcmd_output);\
				sd->status.cmd = max_status[(stat)];\
			}\
			else\
				sd->status.cmd = pl_sd->status.cmd;\
		}

		clonestat_check(str, PARAM_STR);
		clonestat_check(agi, PARAM_AGI);
		clonestat_check(vit, PARAM_VIT);
		clonestat_check(int_, PARAM_INT);
		clonestat_check(dex, PARAM_DEX);
		clonestat_check(luk, PARAM_LUK);

		for (i = 0; i < PARAM_MAX; i++) {
			clif_updatestatus(sd, SP_STR + i);
			clif_updatestatus(sd, SP_USTR + i);
		}
		status_calc_pc(sd, SCO_FORCE);
	}
	memset(atcmd_output, '\0', sizeof(atcmd_output));
	sprintf(atcmd_output, msg_txt(724), "status"); // Clone '%s' is done.
	clif_displaymessage(fd, atcmd_output);

#undef clonestat_check
	return 0;
}

/**
 * Adopt a character.
 * Usage: @adopt <char name>
 * https://rathena.org/board/topic/104014-suggestion-add-adopt-or-etc/
 */
ACMD_FUNC(adopt)
{
	TBL_PC *b_sd;
	enum adopt_responses response;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		sprintf(atcmd_output, msg_txt(435), command); // Please enter a player name (usage: %s <char name>).
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (!(b_sd = map_nick2sd(atcmd_player_name))) {
		clif_displaymessage(fd, msg_txt(3)); // Character not found.
		return -1;
	}

	response = pc_try_adopt(sd, map_charid2sd(sd->status.partner_id), b_sd);

	if (response == ADOPT_ALLOWED) {
		TBL_PC *p_sd = map_charid2sd(sd->status.partner_id);

		b_sd->adopt_invite = sd->status.account_id;
		clif_Adopt_request(b_sd, sd, p_sd->status.account_id);
		return 0;
	}

	if (response < ADOPT_MORE_CHILDREN) // No displaymessage for client-type responses
		clif_displaymessage(fd, msg_txt(728 + response - 1));
	return -1;
}

/**
 * Opens the limited sale window.
 * Usage: @limitedsale or client command /limitedsale on supported clients
 */
ACMD_FUNC(limitedsale) {
	nullpo_retr(-1, sd);

	clif_sale_open(sd);
	return 0;
}

/**
 * Opens the refineUI
 * Usage: @refineui
 */
ACMD_FUNC(refineui)
{
	nullpo_retr(-1, sd);

#if PACKETVER < 20161012
	clif_displaymessage(fd, msg_txt(769)); // This command requires packet version 2016-10-12 or newer.
	return -1;
#else
	if (!battle_config.feature_refineui) {
		clif_displaymessage(fd, msg_txt(770)); // This command is disabled via configuration.
		return -1;
	}

	if (sd->state.refineui_open) {
		clif_displaymessage(fd, msg_txt(771)); // You have already opened the refine UI.
		return -1;
	}

	clif_refineui_open(sd);
	return 0;
#endif
}

#include "../custom/atcommand.inc"

/**
 * Fills the reference of available commands in atcommand DBMap
 */
#define ACMD_DEF(x) { #x, atcommand_ ## x, NULL, NULL, 0 }
#define ACMD_DEF2(x2, x) { x2, atcommand_ ## x, NULL, NULL, 0 }
//Define with restriction
#define ACMD_DEFR(x, r) { #x, atcommand_ ## x, NULL, NULL, r }
#define ACMD_DEF2R(x2, x, r) { x2, atcommand_ ## x, NULL, NULL, r }
void atcommand_basecommands(void) {
	/**
	 * Command reference list, place the base of your commands here
	 * @TODO: List all commands that causing crash
	 */
	AtCommandInfo atcommand_base[] = {
#include "../custom/atcommand_def.inc"
		ACMD_DEF2R("warp", mapmove, ATCMD_NOCONSOLE),
		ACMD_DEF(where),
		ACMD_DEF(jumpto),
		ACMD_DEF(jump),
		ACMD_DEF(who),
		ACMD_DEF2("who2", who),
		ACMD_DEF2("who3", who),
		ACMD_DEF2("whomap", who),
		ACMD_DEF2("whomap2", who),
		ACMD_DEF2("whomap3", who),
		ACMD_DEF(whogm),
		ACMD_DEF(save),
		ACMD_DEF(load),
		ACMD_DEF(speed),
		ACMD_DEF(storage),
		ACMD_DEF(guildstorage),
		ACMD_DEF(option),
		ACMD_DEF(hide), //+ /hide
		ACMD_DEFR(jobchange, ATCMD_NOCONSOLE),
		ACMD_DEF(kill),
		ACMD_DEF(alive),
		ACMD_DEF(kami),
		ACMD_DEF2("kamib", kami),
		ACMD_DEF2("kamic", kami),
		ACMD_DEF2("lkami", kami),
		ACMD_DEF(heal),
		ACMD_DEF(item),
		ACMD_DEF(item2),
		ACMD_DEF2("itembound",item),
		ACMD_DEF2("itembound2",item2),
		ACMD_DEF(itemreset),
		ACMD_DEF(clearstorage),
		ACMD_DEF(cleargstorage),
		ACMD_DEF(clearcart),
		ACMD_DEF2R("blvl", baselevelup, ATCMD_NOCONSOLE),
		ACMD_DEF2("jlvl", joblevelup),
		ACMD_DEF(help),
		ACMD_DEF(pvpoff),
		ACMD_DEF(pvpon),
		ACMD_DEF(gvgoff),
		ACMD_DEF(gvgon),
		ACMD_DEF(model),
		ACMD_DEFR(go, ATCMD_NOCONSOLE),
		ACMD_DEF(monster),
		ACMD_DEF2("monstersmall", monster),
		ACMD_DEF2("monsterbig", monster),
		ACMD_DEF(killmonster),
		ACMD_DEF2("killmonster2", killmonster),
		ACMD_DEF(refine),
		ACMD_DEF(produce),
		ACMD_DEF(memo),
		ACMD_DEF(gat),
		ACMD_DEF(displaystatus),
		ACMD_DEF2("stpoint", statuspoint),
		ACMD_DEF2("skpoint", skillpoint),
		ACMD_DEF(zeny),
		ACMD_DEF2("str", param),
		ACMD_DEF2("agi", param),
		ACMD_DEF2("vit", param),
		ACMD_DEF2("int", param),
		ACMD_DEF2("dex", param),
		ACMD_DEF2("luk", param),
		ACMD_DEF2("glvl", guildlevelup),
		ACMD_DEF(makeegg),
		ACMD_DEF(hatch),
		ACMD_DEF(petfriendly),
		ACMD_DEF(pethungry),
		ACMD_DEF(petrename),
		ACMD_DEF(recall), // + /recall
		ACMD_DEF(night),
		ACMD_DEF(day),
		ACMD_DEF(doom),
		ACMD_DEF(doommap),
		ACMD_DEF(raise),
		ACMD_DEF(raisemap),
		ACMD_DEFR(kick, ATCMD_NOAUTOTRADE), // + right click menu for GM "(name) force to quit"
		ACMD_DEF(kickall),
		ACMD_DEF(allskill),
		ACMD_DEF(questskill),
		ACMD_DEF(lostskill),
		ACMD_DEF(spiritball),
		ACMD_DEF(shieldball),
		ACMD_DEF(rageball),
		ACMD_DEF(charmball),
		ACMD_DEF(party),
		ACMD_DEF(guild),
		ACMD_DEF(breakguild),
		ACMD_DEF(agitstart),
		ACMD_DEF(agitend),
		ACMD_DEF(mapexit),
		ACMD_DEF(idsearch),
		ACMD_DEF(broadcast), // + /b and /nb
		ACMD_DEF(localbroadcast), // + /lb and /nlb
		ACMD_DEF(recallall),
		ACMD_DEFR(reload, ATCMD_NOSCRIPT),
		ACMD_DEF2("reloaditemdb", reload),
		ACMD_DEF2("reloadmobdb", reload),
		ACMD_DEF2("reloadskilldb", reload),
		ACMD_DEF2R("reloadscript", reload, ATCMD_NOSCRIPT),
		ACMD_DEF2("reloadatcommand", reload),
		ACMD_DEF2("reloadbattleconf", reload),
		ACMD_DEF2("reloadstatusdb", reload),
		ACMD_DEF2("reloadpcdb", reload),
		ACMD_DEF2("reloadmotd", reload),
		ACMD_DEF2("reloadquestdb", reload),
		ACMD_DEF2("reloadinstancedb", reload),
		ACMD_DEF2("reloadachievementdb", reload),
		ACMD_DEF(partysharelvl),
		ACMD_DEF(mapinfo),
		ACMD_DEF(dye),
		ACMD_DEF2("hairstyle", hair_style),
		ACMD_DEF2("haircolor", hair_color),
		ACMD_DEF2("allstats", stat_all),
		ACMD_DEF2("block", char_block),
		ACMD_DEF2("ban", char_ban),
		ACMD_DEF2("charban", char_ban), //Char-specific ban time
		ACMD_DEF2("unblock", char_unblock),
		ACMD_DEF2("unban", char_unban),
		ACMD_DEF2("charunban", char_unban), //Char-specific ban time
		ACMD_DEF2("mount", mount_peco),
		ACMD_DEF(guildspy),
		ACMD_DEF(partyspy),
		ACMD_DEF(clanspy),
		ACMD_DEF(repairall),
		ACMD_DEF(guildrecall),
		ACMD_DEF(partyrecall),
		ACMD_DEF(nuke),
		ACMD_DEF(shownpc),
		ACMD_DEF(hidenpc),
		ACMD_DEF(loadnpc),
		ACMD_DEF(unloadnpc),
		ACMD_DEF2("time", servertime),
		ACMD_DEF(jail),
		ACMD_DEF(unjail),
		ACMD_DEF(jailfor),
		ACMD_DEF(jailtime),
		ACMD_DEF(disguise),
		ACMD_DEF(undisguise),
		ACMD_DEF(email),
		ACMD_DEF(effect),
		ACMD_DEF(follow),
		ACMD_DEF(addwarp),
		ACMD_DEF(skillon),
		ACMD_DEF(skilloff),
		ACMD_DEF(killer),
		ACMD_DEF(npcmove),
		ACMD_DEF(killable),
		ACMD_DEF(dropall),
		ACMD_DEF(storeall),
		ACMD_DEF(skillid),
		ACMD_DEF(useskill),
		ACMD_DEF(displayskill),
		ACMD_DEF(snow),
		ACMD_DEF(sakura),
		ACMD_DEF(clouds),
		ACMD_DEF(clouds2),
		ACMD_DEF(fog),
		ACMD_DEF(fireworks),
		ACMD_DEF(leaves),
		ACMD_DEF(summon),
		ACMD_DEF(adjgroup),
		ACMD_DEF(trade),
		ACMD_DEF(send),
		ACMD_DEF(setbattleflag),
		ACMD_DEF(unmute),
		ACMD_DEF(clearweather),
		ACMD_DEF(uptime),
		ACMD_DEF(changesex),
		ACMD_DEF(changecharsex),
		ACMD_DEF(mute),
		ACMD_DEF(refresh),
		ACMD_DEF(refreshall),
		ACMD_DEF(identify),
		ACMD_DEF(identifyall),
		ACMD_DEF(gmotd),
		ACMD_DEF(misceffect),
		ACMD_DEF(mobsearch),
		ACMD_DEF(cleanmap),
		ACMD_DEF(cleanarea),
		ACMD_DEF(npctalk),
		ACMD_DEF(pettalk),
		ACMD_DEF(users),
		ACMD_DEF(reset),
		ACMD_DEF(skilltree),
		ACMD_DEF(marry),
		ACMD_DEF(divorce),
		ACMD_DEF(sound),
		ACMD_DEF(undisguiseall),
		ACMD_DEF(disguiseall),
		ACMD_DEF(changelook),
		ACMD_DEF(autoloot),
		ACMD_DEF2("alootid", autolootitem),
		ACMD_DEF(autoloottype),
		ACMD_DEF(mobinfo),
		ACMD_DEF(exp),
		ACMD_DEF(version),
		ACMD_DEF(mutearea),
		ACMD_DEF(rates),
		ACMD_DEF(iteminfo),
		ACMD_DEF(whodrops),
		ACMD_DEF(whereis),
		ACMD_DEF(mapflag),
		ACMD_DEF(me),
		ACMD_DEF(monsterignore),
		ACMD_DEF(fakename),
		ACMD_DEF(size),
		ACMD_DEF(showexp),
		ACMD_DEF(showzeny),
		ACMD_DEF(showdelay),
		ACMD_DEF(autotrade),
		ACMD_DEF(changegm),
		ACMD_DEF(changeleader),
		ACMD_DEF(partyoption),
		ACMD_DEF(invite),
		ACMD_DEF(duel),
		ACMD_DEF(leave),
		ACMD_DEF(accept),
		ACMD_DEF(reject),
		ACMD_DEF(clone),
		ACMD_DEF2("slaveclone", clone),
		ACMD_DEF2("evilclone", clone),
		ACMD_DEF(tonpc),
		ACMD_DEF(commands),
		ACMD_DEF(noask),
		ACMD_DEF(request),
		ACMD_DEF(homlevel),
		ACMD_DEF(homevolution),
		ACMD_DEF(hommutate),
		ACMD_DEF(makehomun),
		ACMD_DEF(homfriendly),
		ACMD_DEF(homhungry),
		ACMD_DEF(homtalk),
		ACMD_DEF(hominfo),
		ACMD_DEF(homstats),
		ACMD_DEF(homshuffle),
		ACMD_DEF(hommax),
		ACMD_DEF(showmobs),
		ACMD_DEF(feelreset),
		ACMD_DEF(auction),
		ACMD_DEF(mail),
		ACMD_DEF2("noks", ksprotection),
		ACMD_DEF(allowks),
		ACMD_DEF(cash),
		ACMD_DEF2("points", cash),
		ACMD_DEF(agitstart2),
		ACMD_DEF(agitend2),
		ACMD_DEF(resetskill),
		ACMD_DEF(resetstat),
		ACMD_DEF2("storagelist", itemlist),
		ACMD_DEF2("cartlist", itemlist),
		ACMD_DEF2("itemlist", itemlist),
		ACMD_DEF(stats),
		ACMD_DEF(delitem),
		ACMD_DEF(charcommands),
		ACMD_DEF(font),
		ACMD_DEF(accinfo),
		ACMD_DEF(set),
		ACMD_DEF(undisguiseguild),
		ACMD_DEF(disguiseguild),
		ACMD_DEF(sizeall),
		ACMD_DEF(sizeguild),
		ACMD_DEF(addperm),
		ACMD_DEF2("rmvperm", addperm),
		ACMD_DEF(unloadnpcfile),
		ACMD_DEF(reloadnpcfile),
		ACMD_DEF(cart),
		ACMD_DEF(mount2),
		ACMD_DEF(join),
		ACMD_DEFR(channel, ATCMD_NOSCRIPT),
		ACMD_DEF(fontcolor),
		ACMD_DEF(costume),
#ifdef VIP_ENABLE
		ACMD_DEF(vip),
		ACMD_DEF(showrate),
#endif
		ACMD_DEF(fullstrip),
		ACMD_DEF(cloneequip),
		ACMD_DEF(clonestat),
		ACMD_DEF(bodystyle),
		ACMD_DEF(adopt),
		ACMD_DEF(agitstart3),
		ACMD_DEF(agitend3),
		ACMD_DEFR(limitedsale, ATCMD_NOCONSOLE|ATCMD_NOAUTOTRADE),
		ACMD_DEFR(changedress, ATCMD_NOCONSOLE|ATCMD_NOAUTOTRADE),
		ACMD_DEF(refineui),
	};
	AtCommandInfo *atcommand;
	int i;

	for( i = 0; i < ARRAYLENGTH(atcommand_base); i++ ) {
		if(atcommand_exists(atcommand_base[i].command)) { // Should not happen if atcommand_base[] array is OK
			ShowDebug("atcommand_basecommands: duplicate ACMD_DEF for '%s'.\n", atcommand_base[i].command);
			continue;
		}
		CREATE(atcommand, AtCommandInfo, 1);
		safestrncpy(atcommand->command, atcommand_base[i].command, sizeof(atcommand->command));
		atcommand->func = atcommand_base[i].func;
		atcommand->restriction = atcommand_base[i].restriction;
		strdb_put(atcommand_db, atcommand->command, atcommand);
	}
	return;
}

/*==========================================
 * Command lookup functions
 *------------------------------------------*/
bool atcommand_exists(const char *name)
{
	return strdb_exists(atcommand_db, name);
}

static AtCommandInfo *get_atcommandinfo_byname(const char *name)
{
	if (strdb_exists(atcommand_db, name))
		return (AtCommandInfo *)strdb_get(atcommand_db, name);
	return NULL;
}

static const char *atcommand_checkalias(const char *aliasname)
{
	AliasInfo *alias_info = NULL;
	if ((alias_info = (AliasInfo *)strdb_get(atcommand_alias_db, aliasname)) != NULL)
		return alias_info->command->command;
	return aliasname;
}

/// AtCommand suggestion
static void atcommand_get_suggestions(struct map_session_data *sd, const char *name, bool atcommand) {
	DBIterator *atcommand_iter;
	DBIterator *alias_iter;
	AtCommandInfo *command_info = NULL;
	AliasInfo *alias_info = NULL;
	AtCommandType type = (atcommand ? COMMAND_ATCOMMAND : COMMAND_CHARCOMMAND);
	char *full_match[MAX_SUGGESTIONS];
	char *suggestions[MAX_SUGGESTIONS];
	char *match;
	int prefix_count = 0, full_count = 0;
	bool can_use;

	if (!battle_config.atcommand_suggestions_enabled)
		return;

	atcommand_iter = db_iterator(atcommand_db);
	alias_iter = db_iterator(atcommand_alias_db);

	// Build the matches
	for (command_info = dbi_first(atcommand_iter); dbi_exists(atcommand_iter); command_info = dbi_next(atcommand_iter)) {
		match = strstr(command_info->command, name);
		can_use = pc_can_use_command(sd, command_info->command, type);
		if (prefix_count < MAX_SUGGESTIONS && match == command_info->command && can_use) {
			suggestions[prefix_count] = command_info->command;
			++prefix_count;
		}
		if (full_count < MAX_SUGGESTIONS && match != NULL && match != command_info->command && can_use) {
			full_match[full_count] = command_info->command;
			++full_count;
		}
	}

	for (alias_info = dbi_first(alias_iter); dbi_exists(alias_iter); alias_info = dbi_next(alias_iter)) {
		match = strstr(alias_info->alias, name);
		can_use = pc_can_use_command(sd, alias_info->command->command, type);
		if (prefix_count < MAX_SUGGESTIONS && match == alias_info->alias && can_use) {
			suggestions[prefix_count] = alias_info->alias;
			++prefix_count;
		}
		if (full_count < MAX_SUGGESTIONS && match != NULL && match != alias_info->alias && can_use) {
			full_match[full_count] = alias_info->alias;
			++full_count;
		}
	}

	if ((full_count+prefix_count) > 0) {
		char buffer[512];
		int i;

		// Merge full match and prefix match results
		if (prefix_count < MAX_SUGGESTIONS) {
			memmove(&suggestions[prefix_count], full_match, sizeof(char *) * (MAX_SUGGESTIONS - prefix_count));
			prefix_count = min(prefix_count + full_count, MAX_SUGGESTIONS);
		}

		// Build the suggestion string
		strcpy(buffer, msg_txt(205)); // Maybe you meant:
		strcat(buffer, "\n");

		for (i = 0; i < prefix_count; ++i) {
			strcat(buffer, suggestions[i]);
			strcat(buffer, " ");
		}

		clif_displaymessage(sd->fd, buffer);
	}

	dbi_destroy(atcommand_iter);
	dbi_destroy(alias_iter);
}

/**
 * Executes an at-command
 * @param fd
 * @param sd
 * @param message
 * @param type
 *  0 : script call (atcommand)
 *  1 : normal player @atcommand
 *  2 : console (admin:@atcommand)
 *  3 : script call (useatcmd)
 */
bool is_atcommand(const int fd, struct map_session_data *sd, const char *message, int type)
{
	char command[CHAT_SIZE_MAX], params[CHAT_SIZE_MAX];
	char output[CHAT_SIZE_MAX];

	// Reconstructed message
	char atcmd_msg[CHAT_SIZE_MAX];

	TBL_PC *ssd = NULL; //sd for target
	AtCommandInfo *info;

	bool is_atcommand = true; // false if it's a charcommand

	nullpo_retr(false, sd);

	// Shouldn't happen
	if (!message || !*message)
		return false;

	// If cannot use atcomamnd while talking with NPC [Kichi]
	if (type == 1 && sd->npc_id && sd->state.disable_atcommand_on_npc)
		return false;

	// Block NOCHAT but do not display it as a normal message
	if (sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCOMMAND)
		return true;

	// Skip 10/11-langtype's codepage indicator, if detected
	if (message[0] == '|' && strlen(message) >= 4 && (message[3] == atcommand_symbol || message[3] == charcommand_symbol))
		message += 3;

	// Should display as a normal message
	if (*message != atcommand_symbol && *message != charcommand_symbol)
		return false;

	// Type value 0|2 = script|console invoked: bypass restrictions
	if (type == 1 || type == 3) {
		// Commands are disabled on maps flagged as 'nocommand'
		if (map[sd->bl.m].nocommand && pc_get_group_level(sd) < map[sd->bl.m].nocommand) {
			clif_displaymessage(fd, msg_txt(143));  // Commands are disabled on this map.
			return false;
		}
	}

	if (*message == charcommand_symbol)
		is_atcommand = false;

	if (is_atcommand) { // @command
		sprintf(atcmd_msg, "%s", message);
		ssd = sd;
	} else { // #command
		char charname[NAME_LENGTH];
		int n;

		// Checks to see if #command has a name or a name + parameters.
		if ((n = sscanf(message, "%255s \"%23[^\"]\" %255[^\n]", command, charname, params)) < 2 &&
			(n = sscanf(message, "%255s %23s %255[^\n]", command, charname, params)) < 2) {
			if (pc_get_group_level(sd) == 0) {
				if (n < 1)
					return false; // No command found. Display as normal message.
				info = get_atcommandinfo_byname(atcommand_checkalias(command + 1));
				if (!info || !info->char_groups[sd->group_pos]) // If we can't use or doesn't exist: don't even display the command failed message
					return false;
			}
			sprintf(output, msg_txt(1388), charcommand_symbol); // Charcommand failed (usage: %c<command> <char name> <parameters>).
			clif_displaymessage(fd, output);
			return true;
		}
		if (!(ssd = map_nick2sd(charname))) {
			sprintf(output, msg_txt(1389), command); // %s failed. Player not found.
			clif_displaymessage(fd, output);
			return true;
		}
		if (n > 2)
			sprintf(atcmd_msg, "%s %s", command, params);
		else
			sprintf(atcmd_msg, "%s", command);
	}

	// Clearing these to be used once more
	memset(command, '\0', sizeof(command));
	memset(params, '\0', sizeof(params));

	// Check to see if any params exist within this command
	if (sscanf(atcmd_msg, "%255s %255[^\n]", command, params) < 2)
		params[0] = '\0';

	// @commands (script based)
	if ((type == 1 || type == 3) && atcmd_binding_count > 0) {
		struct atcmd_binding_data *binding = get_atcommandbind_byname(command);

		// Check if the binding isn't NULL and there is a NPC event, level of usage met, et cetera
		if (binding && binding->npc_event[0] &&
			((is_atcommand && pc_get_group_level(sd) >= binding->level) ||
			(!is_atcommand && pc_get_group_level(sd) >= binding->level2)))
		{
			// Check if self or character invoking; if self == character invoked, then self invoke
			npc_do_atcmd_event(ssd, command, params, binding->npc_event);
			return true;
		}
	}

	// Grab the command information and check for the proper GM level required to use it or if the command exists
	info = get_atcommandinfo_byname(atcommand_checkalias(command + 1));
	if (!info) {
		if (!pc_get_group_level(sd)) // @TODO: remove or replace with proper permission
			return false;
		sprintf(output, msg_txt(153), command); // "%s is Unknown Command."
		clif_displaymessage(fd, output);
		atcommand_get_suggestions(sd, command + 1, is_atcommand);
		return true;
	}

	// Check restriction
	if (info->restriction) {
		if (info->restriction&ATCMD_NOCONSOLE && type == 2) // Console prevent
			return true;
		if (info->restriction&ATCMD_NOSCRIPT && (!type || type == 3)) // Scripts prevent
			return true;
		if (info->restriction&ATCMD_NOAUTOTRADE && (!type || type == 3) &&
			((is_atcommand && sd && sd->state.autotrade) || (ssd && ssd->state.autotrade)))
			return true;
	}

	// Type 1 : player invoked
	if (type == 1) {
		if ((is_atcommand && !info->at_groups[sd->group_pos]) ||
			(!is_atcommand && !info->char_groups[sd->group_pos]))
			return false;
		if (pc_isdead(sd) && pc_has_permission(sd,PC_PERM_DISABLE_CMD_DEAD)) {
			clif_displaymessage(fd, msg_txt(1393)); // You can't use commands while dead
			return true;
		}
	}

	// Attempt to use the command
	if ((info->func(fd, ssd, command, params))) {
		sprintf(output,msg_txt(154), command); // %s failed.
		clif_displaymessage(fd, output);
		return true;
	}

	// Log only if successful
	log_atcommand(sd, (is_atcommand ? atcmd_msg : message));

	return true;
}

/*==========================================
 *
 *------------------------------------------*/
static void atcommand_config_read(const char *config_filename)
{
	struct config_setting_t *aliases = NULL, *help = NULL;
	const char *symbol = NULL;
	int num_aliases = 0;

	if (conf_read_file(&atcommand_config, config_filename))
		return;

	// Command symbols
	if (config_lookup_string(&atcommand_config, "atcommand_symbol", &symbol)) {
		if (ISPRINT(*symbol) && // no control characters
			*symbol != '/' && // symbol of client commands
			*symbol != '%' && // symbol of party chat
			*symbol != '$' && // symbol of guild chat
			*symbol != charcommand_symbol)
			atcommand_symbol = *symbol;
	}

	if (config_lookup_string(&atcommand_config, "charcommand_symbol", &symbol)) {
		if (ISPRINT(*symbol) && // no control characters
			*symbol != '/' && // symbol of client commands
			*symbol != '%' && // symbol of party chat
			*symbol != '$' && // symbol of guild chat
			*symbol != atcommand_symbol)
			charcommand_symbol = *symbol;
	}

	// Command aliases
	aliases = config_lookup(&atcommand_config, "aliases");
	if (aliases != NULL) {
		int i = 0;
		int count = config_setting_length(aliases);

		for (i = 0; i < count; ++i) {
			struct config_setting_t *command;
			const char *commandname = NULL;
			int j = 0, alias_count = 0;
			AtCommandInfo *commandinfo = NULL;

			command = config_setting_get_elem(aliases, i);
			if (config_setting_type(command) != CONFIG_TYPE_ARRAY)
				continue;
			commandname = config_setting_name(command);
			if (!atcommand_exists(commandname)) {
				ShowConfigWarning(command, "atcommand_config_read: can not set alias for non-existent command %s", commandname);
				continue;
			}
			commandinfo = get_atcommandinfo_byname(commandname);
			alias_count = config_setting_length(command);
			for (j = 0; j < alias_count; ++j) {
				const char *alias = config_setting_get_string_elem(command, j);
				if (alias != NULL) {
					AliasInfo *alias_info;
					if (strdb_exists(atcommand_alias_db, alias)) {
						ShowConfigWarning(command, "atcommand_config_read: alias %s already exists", alias);
						continue;
					}
					CREATE(alias_info, AliasInfo, 1);
					alias_info->command = commandinfo;
					safestrncpy(alias_info->alias, alias, sizeof(alias_info->alias));
					strdb_put(atcommand_alias_db, alias, alias_info);
					++num_aliases;
				}
			}
		}
	}

	// Commands help
	// We only check if all commands exist
	help = config_lookup(&atcommand_config, "help");
	if (help != NULL) {
		int count = config_setting_length(help);
		int i;

		for (i = 0; i < count; ++i) {
			struct config_setting_t *command;
			const char *commandname;

			command = config_setting_get_elem(help, i);
			commandname = config_setting_name(command);
			if (!atcommand_exists(commandname))
				ShowConfigWarning(command, "atcommand_config_read: command %s does not exist", commandname);
		}
	}

	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' command aliases in '"CL_WHITE"%s"CL_RESET"'.\n", num_aliases, config_filename);
	return;
}
void atcommand_db_load_groups(int *group_ids) {
	DBIterator *iter = db_iterator(atcommand_db);
	AtCommandInfo *cmd;
	int i;
	
	for (cmd = dbi_first(iter); dbi_exists(iter); cmd = dbi_next(iter)) {
		cmd->at_groups = aMalloc( pc_group_max * sizeof(char) );
		cmd->char_groups = aMalloc( pc_group_max * sizeof(char) );
		for(i = 0; i < pc_group_max; i++) {
			if( pc_group_can_use_command(group_ids[i], cmd->command, COMMAND_ATCOMMAND ) )
			   cmd->at_groups[i] = 1;
			else
			   cmd->at_groups[i] = 0;
		   if( pc_group_can_use_command(group_ids[i], cmd->command, COMMAND_CHARCOMMAND ) )
			  cmd->char_groups[i] = 1;
			else
			  cmd->char_groups[i] = 0;
		}
	}
	
	dbi_destroy(iter);
	
	return;
}
void atcommand_db_clear(void) {
	
	if (atcommand_db != NULL) {
		DBIterator *iter = db_iterator(atcommand_db);
		AtCommandInfo *cmd;
		
		for (cmd = dbi_first(iter); dbi_exists(iter); cmd = dbi_next(iter)) {
			aFree(cmd->at_groups);
			aFree(cmd->char_groups);
		}
		
		dbi_destroy(iter);

		db_destroy(atcommand_db);
	}
	if (atcommand_alias_db != NULL)
		db_destroy(atcommand_alias_db);
	
	config_destroy(&atcommand_config);
}

void atcommand_doload(void) {
	atcommand_db_clear();
	atcommand_db = stridb_alloc(DB_OPT_DUP_KEY|DB_OPT_RELEASE_DATA, ATCOMMAND_LENGTH);
	atcommand_alias_db = stridb_alloc(DB_OPT_DUP_KEY|DB_OPT_RELEASE_DATA, ATCOMMAND_LENGTH);
	atcommand_basecommands(); //fills initial atcommand_db with known commands
	atcommand_config_read(ATCOMMAND_CONF_FILENAME);
}

void do_init_atcommand(void) {
	atcommand_doload();
}

void do_final_atcommand(void) {
	atcommand_db_clear();
}
