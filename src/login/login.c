// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/core.h"
#include "../common/db.h"
#include "../common/malloc.h"
#include "../common/md5calc.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/timer.h"
#include "../common/cli.h"
#include "../common/ers.h"
#include "../common/utils.h"
#include "../common/mmo.h"
#include "../common/msg_conf.h"
#include "account.h"
#include "ipban.h"
#include "login.h"
#include "loginlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGIN_MAX_MSG 30 // Max number predefined in msg_conf
static char *msg_table[LOGIN_MAX_MSG]; // Login Server messages_conf
struct Login_Config login_config; // Configuration of login-serv

int login_fd; // Login server socket
struct mmo_char_server ch_server[MAX_SERVERS]; // Char server data

// Account engines available
static struct{
	AccountDB *(*constructor)(void);
	AccountDB *db;
} account_engines[] = {
	{account_db_sql, NULL},
#ifdef ACCOUNTDB_ENGINE_0
	{ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_0), NULL},
#endif
#ifdef ACCOUNTDB_ENGINE_1
	{ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_1), NULL},
#endif
#ifdef ACCOUNTDB_ENGINE_2
	{ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_2), NULL},
#endif
#ifdef ACCOUNTDB_ENGINE_3
	{ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_3), NULL},
#endif
#ifdef ACCOUNTDB_ENGINE_4
	{ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_4), NULL},
#endif
	// end of structure
	{NULL, NULL}
};
// account database
AccountDB *accounts = NULL;

//Account registration flood protection [Kevin]
int allowed_regs = 1;
int time_allowed = 10; //in seconds

// Advanced subnet check [LuzZza]
struct s_subnet {
	uint32 mask;
	uint32 char_ip;
	uint32 map_ip;
} subnet[16];

int subnet_count = 0;

int mmo_auth_new(const char *userid, const char *pass, const char sex, const char *last_ip);

//-----------------------------------------------------
// Auth database
//-----------------------------------------------------
#define AUTH_TIMEOUT 30000
struct auth_node {
	int account_id;
	uint32 login_id1;
	uint32 login_id2;
	uint32 ip;
	char sex;
	uint8 clienttype;
	int group_id;
};
static DBMap *auth_db; // int account_id -> struct auth_node*


//-----------------------------------------------------
// Online User Database [Wizputer]
//-----------------------------------------------------
struct online_login_data {
	int account_id;
	int waiting_disconnect;
	int char_server;
};
static DBMap *online_db; // int account_id -> struct online_login_data*

static TIMER_FUNC(waiting_disconnect_timer);

/**
 * @see DBCreateData
 * Create an online_login_data struct and add it into online db
 */
static DBData create_online_user(DBKey key, va_list args) {
	struct online_login_data *p;
	CREATE(p, struct online_login_data, 1);
	p->account_id = key.i;
	p->char_server = -1;
	p->waiting_disconnect = INVALID_TIMER;
	return db_ptr2data(p);
}

/**
 * Receive info from char-serv that this user is online
 * This function will start a timer to recheck if that user still online
 * @param char_server : Serv id where account_id is connected
 * @param account_id : aid connected
 * @return the new online_login_data for that user
 */
struct online_login_data *add_online_user(int char_server, int account_id)
{
	struct online_login_data *p;

	p = idb_ensure(online_db, account_id, create_online_user);
	p->char_server = char_server;
	if( p->waiting_disconnect != INVALID_TIMER ) {
		delete_timer(p->waiting_disconnect, waiting_disconnect_timer);
		p->waiting_disconnect = INVALID_TIMER;
	}
	return p;
}

/**
 * Received info from char serv that the account_id is now offline
 * remove the user from online_db
 * @param account_id : aid to remove from db
 */
void remove_online_user(int account_id)
{
	struct online_login_data *p;

	p = (struct online_login_data *)idb_get(online_db, account_id);
	if( p == NULL )
		return;
	if( p->waiting_disconnect != INVALID_TIMER )
		delete_timer(p->waiting_disconnect, waiting_disconnect_timer);

	idb_remove(online_db, account_id);
}

/**
 * Timered fonction to check if the user still connected
 * @param tid
 * @param tick
 * @param id
 * @param data
 * @return 
 */
static TIMER_FUNC(waiting_disconnect_timer)
{
	struct online_login_data *p = (struct online_login_data *)idb_get(online_db, id);

	if( p != NULL && p->waiting_disconnect == tid && p->account_id == id ) {
		p->waiting_disconnect = INVALID_TIMER;
		remove_online_user(id);
		idb_remove(auth_db, id);
	}
	return 0;
}

/**
 * @see DBApply
 */
static int online_db_setoffline(DBKey key, DBData *data, va_list ap)
{
	struct online_login_data *p = db_data2ptr(data);
	int server = va_arg(ap, int);

	if( server == -1 ) {
		p->char_server = -1;
		if( p->waiting_disconnect != INVALID_TIMER ) {
			delete_timer(p->waiting_disconnect, waiting_disconnect_timer);
			p->waiting_disconnect = INVALID_TIMER;
		}
	} else if( p->char_server == server )
		p->char_server = -2; //Char server disconnected.
	return 0;
}

/**
 * @see DBApply
 */
static int online_data_cleanup_sub(DBKey key, DBData *data, va_list ap)
{
	struct online_login_data *character = db_data2ptr(data);

	if( character->char_server == -2 ) //Unknown server.. set them offline
		remove_online_user(character->account_id);
	return 0;
}

static TIMER_FUNC(online_data_cleanup)
{
	online_db->foreach(online_db, online_data_cleanup_sub);
	return 0;
}


//--------------------------------------------------------------------
// Packet send to all char-servers, except one (wos: without our self)
//--------------------------------------------------------------------
int charif_sendallwos(int sfd, uint8 *buf, size_t len)
{
	int i, c;

	for( i = 0, c = 0; i < ARRAYLENGTH(ch_server); ++i ) {
		int fd = ch_server[i].fd;

		if( session_isValid(fd) && fd != sfd ) {
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0), buf, len);
			WFIFOSET(fd,len);
			++c;
		}
	}

	return c;
}


/// Initializes a server structure.
void chrif_server_init(int id)
{
	memset(&ch_server[id], 0, sizeof(ch_server[id]));
	ch_server[id].fd = -1;
}


/// Destroys a server structure.
void chrif_server_destroy(int id)
{
	if( ch_server[id].fd != -1 )
	{
		do_close(ch_server[id].fd);
		ch_server[id].fd = -1;
	}
}


/// Resets all the data related to a server.
void chrif_server_reset(int id)
{
	online_db->foreach(online_db, online_db_setoffline, id); //Set all chars from this char server to offline.
	chrif_server_destroy(id);
	chrif_server_init(id);
}


/// Called when the connection to Char Server is disconnected.
void chrif_on_disconnect(int id)
{
	ShowStatus("Char-server '%s' has disconnected.\n", ch_server[id].name);
	chrif_server_reset(id);
}


//-----------------------------------------------------
// periodic ip address synchronization
//-----------------------------------------------------
static TIMER_FUNC(sync_ip_addresses)
{
	uint8 buf[2];
	ShowInfo("IP Sync in progress...\n");
	WBUFW(buf,0) = 0x2735;
	charif_sendallwos(-1, buf, 2);
	return 0;
}


//-----------------------------------------------------
// encrypted/unencrypted password check (from eApp)
//-----------------------------------------------------
bool check_encrypted(const char *str1, const char *str2, const char *passwd)
{
	char tmpstr[64 + 1], md5str[32 + 1];

	safesnprintf(tmpstr, sizeof(tmpstr), "%s%s", str1, str2);
	MD5_String(tmpstr, md5str);

	return (0 == strcmp(passwd, md5str));
}

bool check_password(const char *md5key, int passwdenc, const char *passwd, const char *refpass)
{
	if(passwdenc == 0)
		return (0 == strcmp(passwd, refpass));
	else {
		// password mode set to 1 -> md5(md5key, refpass) enable with <passwordencrypt></passwordencrypt>
		// password mode set to 2 -> md5(refpass, md5key) enable with <passwordencrypt2></passwordencrypt2>
		return ((passwdenc&0x01) && check_encrypted(md5key, refpass, passwd)) ||
		       ((passwdenc&0x02) && check_encrypted(refpass, md5key, passwd));
	}
}

//--------------------------------------------
// Test to know if an IP come from LAN or WAN.
//--------------------------------------------
int lan_subnetcheck(uint32 ip)
{
	int i;

	ARR_FIND(0, subnet_count, i, (subnet[i].char_ip & subnet[i].mask) == (ip & subnet[i].mask));
	return (i < subnet_count) ? subnet[i].char_ip : 0;
}

//----------------------------------
// Reading Lan Support configuration
//----------------------------------
int login_lan_config_read(const char *lancfgName)
{
	FILE *fp;
	int line_num = 0;
	char line[1024], w1[64], w2[64], w3[64], w4[64];

	if((fp = fopen(lancfgName, "r")) == NULL) {
		ShowWarning("LAN Support configuration file is not found: %s\n", lancfgName);
		return 1;
	}

	while(fgets(line, sizeof(line), fp)) {
		line_num++;
		if((line[0] == '/' && line[1] == '/') || line[0] == '\n' || line[1] == '\n')
			continue;

		if(sscanf(line,"%63[^:]: %63[^:]:%63[^:]:%63[^\r\n]", w1, w2, w3, w4) != 4) {
			ShowWarning("Error syntax of configuration file %s in line %d.\n", lancfgName, line_num);
			continue;
		}

		if(strcmpi(w1, "subnet") == 0) {
			subnet[subnet_count].mask = str2ip(w2);
			subnet[subnet_count].char_ip = str2ip(w3);
			subnet[subnet_count].map_ip = str2ip(w4);

			if( (subnet[subnet_count].char_ip & subnet[subnet_count].mask) != (subnet[subnet_count].map_ip & subnet[subnet_count].mask) ) {
				ShowError("%s: Configuration Error: The char server (%s) and map server (%s) belong to different subnetworks!\n", lancfgName, w3, w4);
				continue;
			}

			subnet_count++;
		}
	}

	if(subnet_count > 1) /* only useful if there is more than 1 available */
		ShowStatus("Read information about %d subnetworks.\n", subnet_count);

	fclose(fp);
	return 0;
}

//-----------------------
// Console Command Parser [Wizputer]
//-----------------------
int parse_console(const char *buf) {
	char type[64];
	char command[64];
	int n = 0;

	if( ( n = sscanf(buf, "%127[^:]:%255[^\n\r]", type, command) ) < 2 ) {
		if((n = sscanf(buf, "%63[^\n]", type))<1) return -1; //nothing to do no arg
	}
	if( n != 2 ) { //end string
		ShowNotice("Type: '%s'\n",type);
		command[0] = '\0';
	} else
		ShowNotice("Type of command: '%s' || Command: '%s'\n",type,command);

	if( n == 2 ) {
		if(strcmpi("server", type) == 0 ) {
			if( strcmpi("shutdown", command) == 0 || strcmpi("exit", command) == 0 || strcmpi("quit", command) == 0 ) {
				runflag = 0;
			} else if( strcmpi("alive", command) == 0 || strcmpi("status", command) == 0 )
				ShowInfo(CL_CYAN"Console: "CL_BOLD"I'm Alive."CL_RESET"\n");
		}
		if( strcmpi("create",type) == 0 ) {
			char username[NAME_LENGTH], password[NAME_LENGTH], md5password[32 + 1], sex; //23 + 1 plaintext, 32 + 1 md5
			bool md5 = 0;
			if( sscanf(command, "%23s %23s %c", username, password, &sex) < 3 || strnlen(username, sizeof(username)) < 4 || strnlen(password, sizeof(password)) < 1 ) {
				ShowWarning("Console: Invalid parameters for '%s'. Usage: %s <username> <password> <sex:F/M>\n", type, type);
				return 0;
			}
			if( login_config.use_md5_passwds ) {
				MD5_String(password,md5password);
				md5 = 1;
			}
			if( mmo_auth_new(username,(md5?md5password:password), TOUPPER(sex), "0.0.0.0") != -1 ) {
				ShowError("Console: Account creation failed.\n");
				return 0;
			}
			ShowStatus("Console: Account '%s' created successfully.\n", username);
		}
	} else if( strcmpi("ers_report", type) == 0 ) {
		ers_report();
	} else if( strcmpi("help", type) == 0 ) {
		ShowInfo("Available commands:\n");
		ShowInfo("\t server:shutdown => Stops the server.\n");
		ShowInfo("\t server:alive => Checks if the server is running.\n");
		ShowInfo("\t ers_report => Displays database usage.\n");
		ShowInfo("\t create:<username> <password> <sex:M|F> => Creates a new account.\n");
	} else { // commands with parameters

	}

	return 0;
}

int chrif_send_accdata(int fd, uint32 aid) {
	struct mmo_account acc;
	time_t expiration_time = 0;
	char email[40] = "";
	int group_id = 0;
	char birthdate[10 + 1] = "";
	char pincode[PINCODE_LENGTH + 1];
	char isvip = false;
	uint8 char_slots = MIN_CHARS, char_vip = 0, char_billing = 0;

	memset(pincode,0,PINCODE_LENGTH + 1);
	if( !accounts->load_num(accounts, &acc, aid) )
		return -1;
	else {
		safestrncpy(email, acc.email, sizeof(email));
		expiration_time = acc.expiration_time;
		group_id = acc.group_id;

		safestrncpy(birthdate, acc.birthdate, sizeof(birthdate));
		safestrncpy(pincode, acc.pincode, sizeof(pincode));
#ifdef VIP_ENABLE
		char_vip = login_config.vip_sys.char_increase;
		if( acc.vip_time > time(NULL) ) {
			isvip = true;
			char_slots = login_config.char_per_account + char_vip;
		} else
			char_slots = login_config.char_per_account;
		char_billing = MAX_CHAR_BILLING; //@TODO: Create a config for this
#endif
	}

	WFIFOHEAD(fd,75);
	WFIFOW(fd,0) = 0x2717;
	WFIFOL(fd,2) = aid;
	safestrncpy((char *)WFIFOP(fd,6), email, 40);
	WFIFOL(fd,46) = (uint32)expiration_time;
	WFIFOB(fd,50) = (unsigned char)group_id;
	WFIFOB(fd,51) = char_slots;
	safestrncpy((char *)WFIFOP(fd,52), birthdate, 10 + 1);
	safestrncpy((char *)WFIFOP(fd,63), pincode, 4 + 1);
	WFIFOL(fd,68) = (uint32)acc.pincode_change;
	WFIFOB(fd,72) = isvip;
	WFIFOB(fd,73) = char_vip;
	WFIFOB(fd,74) = char_billing;
	WFIFOSET(fd,75);
	return 0;
}

int chrif_parse_reqaccdata(int fd, int cid, char *ip) {
	if( RFIFOREST(fd) < 6 )
		return 0;
	else {
		uint32 aid = RFIFOL(fd,2);

		RFIFOSKIP(fd,6);
		if( chrif_send_accdata(fd,aid) < 0 )
			ShowNotice("Char-server '%s': account %d NOT found (ip: %s).\n", ch_server[cid].name, aid, ip);
	}
	return 0;
}

/**
 * Transmit vip specific data to char-serv (will be transfered to mapserv)
 * @param fd
 * @param acc
 * @param flag 0x1: VIP, 0x2: GM, 0x4: Show rates on player
 * @param mapfd
 */
int chrif_sendvipdata(int fd, struct mmo_account acc, uint8 flag, int mapfd) {
#ifdef VIP_ENABLE
	WFIFOHEAD(fd,19);
	WFIFOW(fd,0) = 0x2743;
	WFIFOL(fd,2) = acc.account_id;
	WFIFOL(fd,6) = (uint32)acc.vip_time;
	WFIFOB(fd,10) = flag;
	WFIFOL(fd,11) = acc.group_id; //New group id
	WFIFOL(fd,15) = mapfd; //Link to mapserv
	WFIFOSET(fd,19);
	chrif_send_accdata(fd, acc.account_id); //Refresh char with new setting
#endif
	return 1;
}

/**
 * Received a vip data request from char
 * flag is the query to perform
 *  0x1 : Select info and update old_groupid
 *  0x2 : VIP duration is changed by atcommand or script
 *  0x8 : First request on player login
 * @param fd link to charserv
 * @return 0 missing data, 1 succeed
 */
int chrif_parse_reqvipdata(int fd) {
#ifdef VIP_ENABLE
	if( RFIFOREST(fd) < 15 )
		return 0;
	else { //Request vip info
		struct mmo_account acc;
		uint32 aid = RFIFOL(fd,2);
		uint8 flag = RFIFOB(fd,6);
		int32 timediff = RFIFOL(fd,7);
		int mapfd = RFIFOL(fd,11);

		RFIFOSKIP(fd,15);
		if( accounts->load_num(accounts, &acc, aid) ) {
			time_t now = time(NULL);
			time_t vip_time = acc.vip_time;
			bool isvip = false;

			if( acc.group_id > login_config.vip_sys.group_id ) { //Don't change group if it's higher
				chrif_sendvipdata(fd, acc, 0x2|((flag&0x8) ? 0x4 : 0), mapfd);
				return 1;
			}
			if( flag&2 ) {
				if( !vip_time )
					vip_time = now; //New entry
				vip_time += timediff; //Set new duration
			}
			if( now < vip_time ) { //isvip
				if( acc.group_id != login_config.vip_sys.group_id ) //Only upadate this if we're not vip already
					acc.old_group = acc.group_id;
				acc.group_id = login_config.vip_sys.group_id;
				acc.char_slots = login_config.char_per_account + login_config.vip_sys.char_increase;
				isvip = true;
			} else { //Expired or @vip -xx
				vip_time = 0;
				if( acc.group_id == login_config.vip_sys.group_id ) //Prevent alteration in case account wasn't registered as vip yet
					acc.group_id = acc.old_group;
				acc.old_group = 0;
				acc.char_slots = login_config.char_per_account;
			}
			acc.vip_time = vip_time;
			accounts->save(accounts, &acc);
			if( flag&1 )
				chrif_sendvipdata(fd, acc, (isvip ? 0x1 : 0)|((flag&0x8) ? 0x4 : 0), mapfd);
		}
	}
#endif
	return 1;
}

//--------------------------------
// Packet parsing for char-servers
//--------------------------------
int parse_fromchar(int fd) {
	int j, id;
	uint32 ipl;
	char ip[16];

	ARR_FIND(0, ARRAYLENGTH(ch_server), id, ch_server[id].fd == fd);
	if( id == ARRAYLENGTH(ch_server) ) { //Not a char server
		ShowDebug("parse_fromchar: Disconnecting invalid session #%d (is not a char-server)\n", fd);
		set_eof(fd);
		do_close(fd);
		return 0;
	}

	if( session[fd]->flag.eof ) {
		do_close(fd);
		ch_server[id].fd = -1;
		chrif_on_disconnect(id);
		return 0;
	}

	ipl = ch_server[id].ip;
	ip2str(ipl, ip);

	while( RFIFOREST(fd) >= 2 ) {
		uint16 command = RFIFOW(fd,0);

		switch( command ) {
			case 0x2712: //Request from char-server to authenticate an account
				if( RFIFOREST(fd) < 23 )
					return 0;
				else {
					struct auth_node *node;
					int account_id = RFIFOL(fd,2);
					uint32 login_id1 = RFIFOL(fd,6);
					uint32 login_id2 = RFIFOL(fd,10);
					uint8 sex = RFIFOB(fd,14);
					//uint32 ip_ = ntohl(RFIFOL(fd,15));
					int request_id = RFIFOL(fd,19);
					RFIFOSKIP(fd,23);

					node = (struct auth_node *)idb_get(auth_db, account_id);
					if( runflag == LOGINSERVER_ST_RUNNING &&
						node != NULL &&
						node->account_id == account_id &&
						node->login_id1  == login_id1 &&
						node->login_id2  == login_id2 &&
						node->sex        == sex_num2str(sex) /*&&
						node->ip         == ip_*/ ){// found
						//ShowStatus("Char-server '%s': authentication of the account %d accepted (ip: %s).\n", ch_server[id].name, account_id, ip);

						//Send ack
						WFIFOHEAD(fd,25);
						WFIFOW(fd,0) = 0x2713;
						WFIFOL(fd,2) = account_id;
						WFIFOL(fd,6) = login_id1;
						WFIFOL(fd,10) = login_id2;
						WFIFOB(fd,14) = sex;
						WFIFOB(fd,15) = 0;// ok
						WFIFOL(fd,16) = request_id;
						WFIFOB(fd,20) = node->clienttype;
						WFIFOL(fd,21) = node->group_id;
						WFIFOSET(fd,25);

						//Each auth entry can only be used once
						idb_remove(auth_db, account_id);
					} else { //Authentication not found
						ShowStatus("Char-server '%s': authentication of the account %d REFUSED (ip: %s).\n", ch_server[id].name, account_id, ip);
						WFIFOHEAD(fd,25);
						WFIFOW(fd,0) = 0x2713;
						WFIFOL(fd,2) = account_id;
						WFIFOL(fd,6) = login_id1;
						WFIFOL(fd,10) = login_id2;
						WFIFOB(fd,14) = sex;
						WFIFOB(fd,15) = 1; //Auth failed
						WFIFOL(fd,16) = request_id;
						WFIFOB(fd,20) = 0;
						WFIFOL(fd,21) = 0;
						WFIFOSET(fd,25);
					}
				}
				break;

			case 0x2714:
				if( RFIFOREST(fd) < 6 )
					return 0;
				else {
					int users = RFIFOL(fd,2);

					RFIFOSKIP(fd,6);
					//How many users on world? (update)
					if( ch_server[id].users != users ) {
						ShowStatus("set users %s : %d\n", ch_server[id].name, users);

						ch_server[id].users = users;
					}
				}
				break;

			case 0x2715: //Request from char server to change e-email from default "a@a.com"
				if (RFIFOREST(fd) < 46)
					return 0;
				else {
					struct mmo_account acc;
					char email[40];
					int account_id = RFIFOL(fd,2);

					safestrncpy(email, (char *)RFIFOP(fd,6), 40); remove_control_chars(email);
					RFIFOSKIP(fd,46);
					if( e_mail_check(email) == 0 )
						ShowNotice("Char-server '%s': Attempt to create an e-mail on an account with a default e-mail REFUSED - e-mail is invalid (account: %d, ip: %s)\n", ch_server[id].name, account_id, ip);
					else if( !accounts->load_num(accounts, &acc, account_id) || strcmp(acc.email, "a@a.com") == 0 || acc.email[0] == '\0' )
						ShowNotice("Char-server '%s': Attempt to create an e-mail on an account with a default e-mail REFUSED - account doesn't exist or e-mail of account isn't default e-mail (account: %d, ip: %s).\n", ch_server[id].name, account_id, ip);
					else {
						memcpy(acc.email, email, 40);
						ShowNotice("Char-server '%s': Create an e-mail on an account with a default e-mail (account: %d, new e-mail: %s, ip: %s).\n", ch_server[id].name, account_id, email, ip);
						//Save
						accounts->save(accounts, &acc);
					}
				}
				break;

			case 0x2716: chrif_parse_reqaccdata(fd,id,ip); break; //Request account data

			case 0x2719: //Ping request from charserver
				RFIFOSKIP(fd,2);
				WFIFOHEAD(fd,2);
				WFIFOW(fd,0) = 0x2718;
				WFIFOSET(fd,2);
				break;

			//Map server send information to change an email of an account via char-server
			case 0x2722: // 0x2722 <account_id>.L <actual_e-mail>.40B <new_e-mail>.40B
				if (RFIFOREST(fd) < 86)
					return 0;
				else {
					struct mmo_account acc;
					char actual_email[40];
					char new_email[40];

					int account_id = RFIFOL(fd,2);
					safestrncpy(actual_email, (char *)RFIFOP(fd,6), 40);
					safestrncpy(new_email, (char *)RFIFOP(fd,46), 40);
					RFIFOSKIP(fd, 86);

					if( e_mail_check(actual_email) == 0 )
						ShowNotice("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command), but actual email is invalid (account: %d, ip: %s)\n", ch_server[id].name, account_id, ip);
					else if( e_mail_check(new_email) == 0 )
						ShowNotice("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command) with a invalid new e-mail (account: %d, ip: %s)\n", ch_server[id].name, account_id, ip);
					else if( strcmpi(new_email, "a@a.com") == 0 )
						ShowNotice("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command) with a default e-mail (account: %d, ip: %s)\n", ch_server[id].name, account_id, ip);
					else if( !accounts->load_num(accounts, &acc, account_id) )
						ShowNotice("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command), but account doesn't exist (account: %d, ip: %s).\n", ch_server[id].name, account_id, ip);
					else if( strcmpi(acc.email, actual_email) != 0 )
						ShowNotice("Char-server '%s': Attempt to modify an e-mail on an account (@email GM command), but actual e-mail is incorrect (account: %d (%s), actual e-mail: %s, proposed e-mail: %s, ip: %s).\n", ch_server[id].name, account_id, acc.userid, acc.email, actual_email, ip);
					else {
						safestrncpy(acc.email, new_email, 40);
						ShowNotice("Char-server '%s': Modify an e-mail on an account (@email GM command) (account: %d (%s), new e-mail: %s, ip: %s).\n", ch_server[id].name, account_id, acc.userid, new_email, ip);
						//Save
						accounts->save(accounts, &acc);
					}
				}
				break;

			case 0x2724: //Receiving an account state update request from a map-server (relayed via char-server)
				if( RFIFOREST(fd) < 10 )
					return 0;
				else {
					struct mmo_account acc;
					int account_id = RFIFOL(fd,2);
					unsigned int state = RFIFOL(fd,6);

					RFIFOSKIP(fd,10);
					if( !accounts->load_num(accounts, &acc, account_id) )
						ShowNotice("Char-server '%s': Error of Status change (account: %d not found, suggested status %d, ip: %s).\n", ch_server[id].name, account_id, state, ip);
					else if( acc.state == state )
						ShowNotice("Char-server '%s':  Error of Status change - actual status is already the good status (account: %d, status %d, ip: %s).\n", ch_server[id].name, account_id, state, ip);
					else {
						ShowNotice("Char-server '%s': Status change (account: %d, new status %d, ip: %s).\n", ch_server[id].name, account_id, state, ip);

						acc.state = state;
						//Save
						accounts->save(accounts, &acc);

						//Notify other servers
						if( state != 0 ) {
							uint8 buf[11];
							WBUFW(buf,0) = 0x2731;
							WBUFL(buf,2) = account_id;
							WBUFB(buf,6) = 0; // 0: Change of state, 1: Ban
							WBUFL(buf,7) = state; // Status or final date of a banishment
							charif_sendallwos(-1, buf, 11);
						}
					}
				}
				break;

			case 0x2725: //Receiving of map-server via char-server a ban request
				if( RFIFOREST(fd) < 10 )
					return 0;
				else {
					struct mmo_account acc;
					int account_id = RFIFOL(fd,2);
					int timediff = RFIFOL(fd,6);

					RFIFOSKIP(fd,10);
					if( !accounts->load_num(accounts, &acc, account_id) )
						ShowNotice("Char-server '%s': Error of ban request (account: %d not found, ip: %s).\n", ch_server[id].name, account_id, ip);
					else {
						time_t timestamp;

						if( acc.unban_time == 0 || acc.unban_time < time(NULL) )
							timestamp = time(NULL); // New ban
						else
							timestamp = acc.unban_time; // Add to existing ban
						timestamp += timediff;
						if( timestamp == -1 )
							ShowNotice("Char-server '%s': Error of ban request (account: %d, invalid date, ip: %s).\n", ch_server[id].name, account_id, ip);
						else if( timestamp <= time(NULL) || timestamp == 0 )
							ShowNotice("Char-server '%s': Error of ban request (account: %d, new date unbans the account, ip: %s).\n", ch_server[id].name, account_id, ip);
						else {
							uint8 buf[11];
							char tmpstr[24];

							timestamp2string(tmpstr, sizeof(tmpstr), timestamp, login_config.date_format);
							ShowNotice("Char-server '%s': Ban request (account: %d, new final date of banishment: %d (%s), ip: %s).\n", ch_server[id].name, account_id, timestamp, tmpstr, ip);

							acc.unban_time = timestamp;

							//Save
							accounts->save(accounts, &acc);

							WBUFW(buf,0) = 0x2731;
							WBUFL(buf,2) = account_id;
							WBUFB(buf,6) = 1; //0: Change of status, 1: Ban
							WBUFL(buf,7) = (uint32)timestamp; //Status or final date of a banishment
							charif_sendallwos(-1, buf, 11);
						}
					}
				}
				break;

			case 0x2727: //Change of sex (sex is reversed)
				if( RFIFOREST(fd) < 6 )
					return 0;
				else {
					struct mmo_account acc;

					int account_id = RFIFOL(fd,2);
					RFIFOSKIP(fd,6);

					if( !accounts->load_num(accounts, &acc, account_id) )
						ShowNotice("Char-server '%s': Error of sex change (account: %d not found, ip: %s).\n", ch_server[id].name, account_id, ip);
					else if( acc.sex == 'S' )
						ShowNotice("Char-server '%s': Error of sex change - account to change is a Server account (account: %d, ip: %s).\n", ch_server[id].name, account_id, ip);
					else {
						unsigned char buf[7];
						char sex = ( acc.sex == 'M' ) ? 'F' : 'M'; //Change gender

						ShowNotice("Char-server '%s': Sex change (account: %d, new sex %c, ip: %s).\n", ch_server[id].name, account_id, sex, ip);

						acc.sex = sex;
						//Save
						accounts->save(accounts, &acc);

						//Announce to other servers
						WBUFW(buf,0) = 0x2723;
						WBUFL(buf,2) = account_id;
						WBUFB(buf,6) = sex_str2num(sex);
						charif_sendallwos(-1, buf, 7);
					}
				}
				break;

			case 0x2728: //We receive account_reg2 from a char-server, and we send them to other map-servers.
				if( RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2) )
					return 0;
				else {
					struct mmo_account acc;
					int account_id = RFIFOL(fd,4);

					if( !accounts->load_num(accounts, &acc, account_id) )
						ShowStatus("Char-server '%s': receiving (from the char-server) of account_reg2 (account: %d not found, ip: %s).\n", ch_server[id].name, account_id, ip);
					else {
						int len;
						int p;

						ShowNotice("char-server '%s': receiving (from the char-server) of account_reg2 (account: %d, ip: %s).\n", ch_server[id].name, account_id, ip);
						for( j = 0, p = 13; j < ACCOUNT_REG2_NUM && p < RFIFOW(fd,2); ++j ){
							sscanf((char *)RFIFOP(fd,p), "%31c%n", acc.account_reg2[j].str, &len);
							acc.account_reg2[j].str[len]='\0';
							p += len + 1; //+1 to skip the '\0' between strings.
							sscanf((char *)RFIFOP(fd,p), "%255c%n", acc.account_reg2[j].value, &len);
							acc.account_reg2[j].value[len]='\0';
							p += len + 1;
							remove_control_chars(acc.account_reg2[j].str);
							remove_control_chars(acc.account_reg2[j].value);
						}
						acc.account_reg2_num = j;

						//Save
						accounts->save(accounts, &acc);

						//Sending information towards the other char-servers.
						RFIFOW(fd,0) = 0x2729; //Reusing read buffer
						charif_sendallwos(fd, RFIFOP(fd,0), RFIFOW(fd,2));
					}
					RFIFOSKIP(fd,RFIFOW(fd,2));
				}
				break;

			case 0x272a: //Receiving of map-server via char-server an unban request
				if( RFIFOREST(fd) < 6 )
					return 0;
				else {
					struct mmo_account acc;
					int account_id = RFIFOL(fd,2);

					RFIFOSKIP(fd,6);
					if( !accounts->load_num(accounts, &acc, account_id) )
						ShowNotice("Char-server '%s': Error of UnBan request (account: %d not found, ip: %s).\n", ch_server[id].name, account_id, ip);
					else if( acc.unban_time == 0 )
						ShowNotice("Char-server '%s': Error of UnBan request (account: %d, no change for unban date, ip: %s).\n", ch_server[id].name, account_id, ip);
					else {
						ShowNotice("Char-server '%s': UnBan request (account: %d, ip: %s).\n", ch_server[id].name, account_id, ip);
						acc.unban_time = 0;
						accounts->save(accounts, &acc);
					}
				}
				break;

			case 0x272b: //Set account_id to online [Wizputer]
				if( RFIFOREST(fd) < 6 )
					return 0;
				add_online_user(id, RFIFOL(fd,2));
				RFIFOSKIP(fd,6);
				break;

			case 0x272c: //Set account_id to offline [Wizputer]
				if( RFIFOREST(fd) < 6 )
					return 0;
				remove_online_user(RFIFOL(fd,2));
				RFIFOSKIP(fd,6);
				break;

			case 0x272d: //Receive list of all online accounts. [Skotlex]
				if( RFIFOREST(fd) < 4 || RFIFOREST(fd) < RFIFOW(fd,2) )
					return 0;
				else {
					uint32 i, users;

					online_db->foreach(online_db, online_db_setoffline, id); //Set all chars from this char-server offline first
					users = RFIFOW(fd,4);
					for( i = 0; i < users; i++ ) {
						int aid = RFIFOL(fd,6 + i * 4);
						struct online_login_data *p = idb_ensure(online_db, aid, create_online_user);

						p->char_server = id;
						if( p->waiting_disconnect != INVALID_TIMER ) {
							delete_timer(p->waiting_disconnect, waiting_disconnect_timer);
							p->waiting_disconnect = INVALID_TIMER;
						}
					}
					RFIFOSKIP(fd,RFIFOW(fd,2));
				}
				break;

			case 0x272e: //Request account_reg2 for a character.
				if (RFIFOREST(fd) < 10)
					return 0;
				else {
					struct mmo_account acc;
					size_t off;
					int account_id = RFIFOL(fd,2);
					int char_id = RFIFOL(fd,6);

					RFIFOSKIP(fd,10);
					WFIFOHEAD(fd,ACCOUNT_REG2_NUM*sizeof(struct global_reg));
					WFIFOW(fd,0) = 0x2729;
					WFIFOL(fd,4) = account_id;
					WFIFOL(fd,8) = char_id;
					WFIFOB(fd,12) = 1; //Type 1 for Account2 registry
					off = 13;
					if( accounts->load_num(accounts, &acc, account_id) ) {
						for( j = 0; j < acc.account_reg2_num; j++ ) {
							if( acc.account_reg2[j].str[0] != '\0' ) {
								off += sprintf((char *)WFIFOP(fd,off), "%s", acc.account_reg2[j].str)+1; //We add 1 to consider the '\0' in place.
								off += sprintf((char *)WFIFOP(fd,off), "%s", acc.account_reg2[j].value)+1;
							}
						}
					}
					WFIFOW(fd,2) = (uint16)off;
					WFIFOSET(fd,WFIFOW(fd,2));
				}
				break;

			case 0x2736: //WAN IP update from char-server
				if( RFIFOREST(fd) < 6 )
					return 0;
				ch_server[id].ip = ntohl(RFIFOL(fd,2));
				ShowInfo("Updated IP of Server #%d to %d.%d.%d.%d.\n",id, CONVIP(ch_server[id].ip));
				RFIFOSKIP(fd,6);
				break;

			case 0x2737: //Request to set all offline
				ShowInfo("Setting accounts from char-server %d offline.\n", id);
				online_db->foreach(online_db, online_db_setoffline, id);
				RFIFOSKIP(fd,2);
				break;

#if PACKETVER_SUPPORTS_PINCODE
			case 0x2738: //Change PIN Code for a account
				if( RFIFOREST(fd) < 8 + PINCODE_LENGTH + 1 )
					return 0;
				else {
					struct mmo_account acc;

					if( accounts->load_num(accounts, &acc, RFIFOL(fd,4) ) ) {
						strncpy(acc.pincode, (char *)RFIFOP(fd,8), PINCODE_LENGTH + 1);
						acc.pincode_change = time(NULL);
						accounts->save(accounts, &acc);
					}
					RFIFOSKIP(fd,8 + PINCODE_LENGTH + 1);
				}
				break;

			case 0x2739: //PIN Code was entered wrong too often
				if( RFIFOREST(fd) < 6 )
					return 0;
				else {
					struct mmo_account acc;

					if( accounts->load_num(accounts, &acc, RFIFOL(fd,2) ) ) {
						struct online_login_data *ld;

						ld = (struct online_login_data *)idb_get(online_db,acc.account_id);
						if( ld == NULL )
							return 0;
						login_log(host2ip(acc.last_ip), acc.userid, 100, "PIN Code check failed");
					}
					remove_online_user(acc.account_id);
					RFIFOSKIP(fd,6);
				}
				break;
#endif

			case 0x2742: chrif_parse_reqvipdata(fd); break; //Vip sys

			case 0x2744: // Accinfo request forwarded by charserver on mapserver's account
				if( RFIFOREST(fd) < 22 )
					return 0;
				else {
					struct mmo_account acc;
					int account_id = RFIFOL(fd, 2), u_fd = RFIFOL(fd, 6), u_aid = RFIFOL(fd, 10), u_group = RFIFOL(fd, 14), map_fd = RFIFOL(fd, 18);

					if( accounts->load_num(accounts, &acc, account_id) ) {
						WFIFOHEAD(fd,183);
						WFIFOW(fd,0) = 0x2738;
						safestrncpy((char *)WFIFOP(fd,2), acc.userid, NAME_LENGTH);
						if( u_group >= acc.group_id )
							safestrncpy((char *)WFIFOP(fd,26), acc.pass, 33);
						else
							memset(WFIFOP(fd,26), '\0', 33);
						safestrncpy((char *)WFIFOP(fd,59), acc.email, 40);
						safestrncpy((char *)WFIFOP(fd,99), acc.last_ip, 16);
						WFIFOL(fd,115) = acc.group_id;
						safestrncpy((char *)WFIFOP(fd,119), acc.lastlogin, 24);
						WFIFOL(fd,143) = acc.logincount;
						WFIFOL(fd,147) = acc.state;
						if( u_group >= acc.group_id )
							safestrncpy((char *)WFIFOP(fd,151), acc.pincode, 5);
						else
							memset(WFIFOP(fd,151), '\0', 5);
						safestrncpy((char *)WFIFOP(fd,156), acc.birthdate, 11);
						WFIFOL(fd,167) = map_fd;
						WFIFOL(fd,171) = u_fd;
						WFIFOL(fd,175) = u_aid;
						WFIFOL(fd,179) = account_id;
						WFIFOSET(fd,183);
					} else {
						WFIFOHEAD(fd,18);
						WFIFOW(fd,0) = 0x2737;
						WFIFOL(fd,2) = map_fd;
						WFIFOL(fd,6) = u_fd;
						WFIFOL(fd,10) = u_aid;
						WFIFOL(fd,14) = account_id;
						WFIFOSET(fd,18);
					}
					RFIFOSKIP(fd,22);
				}
				break;

			default:
				ShowError("parse_fromchar: Unknown packet 0x%x from a char-server! Disconnecting!\n", command);
				set_eof(fd);
				return 0;
		} //Switch
	} //While

	return 0;
}


//-------------------------------------
// Make new account
//-------------------------------------
int mmo_auth_new(const char *userid, const char *pass, const char sex, const char *last_ip) {
	static int num_regs = 0; // registration counter
	static unsigned int new_reg_tick = 0;
	unsigned int tick = gettick();
	struct mmo_account acc;

	//Account Registration Flood Protection by [Kevin]
	if( new_reg_tick == 0 )
		new_reg_tick = gettick();
	if( DIFF_TICK(tick, new_reg_tick) < 0 && num_regs >= allowed_regs ) {
		ShowNotice("Account registration denied (registration limit exceeded)\n");
		return 3;
	}

	if( login_config.new_acc_length_limit && (strlen(userid) < 4 || strlen(pass) < 4) )
		return 1;

	//Check for invalid inputs
	if( sex != 'M' && sex != 'F' )
		return 0; // 0 = Unregistered ID

	//Check if the account doesn't exist already
	if( accounts->load_str(accounts, &acc, userid) ) {
		ShowNotice("Attempt of creation of an already existant account (account: %s, sex: %c)\n", userid, sex);
		return 1; // 1 = Incorrect Password
	}

	memset(&acc, '\0', sizeof(acc));
	acc.account_id = -1; // assigned by account db
	safestrncpy(acc.userid, userid, sizeof(acc.userid));
	safestrncpy(acc.pass, pass, sizeof(acc.pass));
	acc.sex = sex;
	safestrncpy(acc.email, "a@a.com", sizeof(acc.email));
	acc.expiration_time = ( login_config.start_limited_time != -1 ) ? time(NULL) + login_config.start_limited_time : 0;
	safestrncpy(acc.lastlogin, "0000-00-00 00:00:00", sizeof(acc.lastlogin));
	safestrncpy(acc.last_ip, last_ip, sizeof(acc.last_ip));
	safestrncpy(acc.birthdate, "0000-00-00", sizeof(acc.birthdate));
	safestrncpy(acc.pincode, "", sizeof(acc.pincode));
	acc.pincode_change = 0;
	acc.char_slots = MIN_CHARS;
#ifdef VIP_ENABLE
	acc.vip_time = 0;
	acc.old_group = 0;
#endif

	if( !accounts->create(accounts, &acc) )
		return 0;

	ShowNotice("Account creation (account %s, id: %d, sex: %c)\n", acc.userid, acc.account_id, acc.sex);

	if( DIFF_TICK(tick, new_reg_tick) > 0 ) {// Update the registration check.
		num_regs = 0;
		new_reg_tick = tick + time_allowed*1000;
	}
	++num_regs;

	return -1;
}

//-----------------------------------------------------
// Check/authentication of a connection
//-----------------------------------------------------
int mmo_auth(struct login_session_data* sd, bool isServer) {
	struct mmo_account acc;
	int len;

	char ip[16];
	ip2str(session[sd->fd]->client_addr, ip);

	// DNS Blacklist check
	if( login_config.use_dnsbl ) {
		char r_ip[16];
		char ip_dnsbl[256];
		char *dnsbl_serv;
		uint8 *sin_addr = (uint8 *)&session[sd->fd]->client_addr;

		sprintf(r_ip, "%u.%u.%u.%u", sin_addr[0], sin_addr[1], sin_addr[2], sin_addr[3]);

		for( dnsbl_serv = strtok(login_config.dnsbl_servs,","); dnsbl_serv != NULL; dnsbl_serv = strtok(NULL,",") ) {
			sprintf(ip_dnsbl, "%s.%s", r_ip, trim(dnsbl_serv));
			if( host2ip(ip_dnsbl) ) {
				ShowInfo("DNSBL: (%s) Blacklisted. User Kicked.\n", r_ip);
				return 3;
			}
		}

	}

	len = strnlen(sd->userid, NAME_LENGTH);

	// Account creation with _M/_F
	if( login_config.new_account_flag ) {
		if( len > 2 && strnlen(sd->passwd, NAME_LENGTH) > 0 && // Valid user and password lengths
			sd->passwdenc == 0 && // Unencoded password
			sd->userid[len-2] == '_' && memchr("FfMm", sd->userid[len-1], 4) ) // _M/_F suffix
		{
			int result;

			// Remove the _M/_F suffix
			len -= 2;
			sd->userid[len] = '\0';

			result = mmo_auth_new(sd->userid, sd->passwd, TOUPPER(sd->userid[len+1]), ip);
			if( result != -1 )
				return result; // Failed to make account. [Skotlex].
		}
	}

	if( len <= 0 ) { // A empty password is fine, a userid is not.
		ShowNotice("Empty userid (received pass: '%s', ip: %s)\n", sd->passwd, ip);
		return 0; // 0 = Unregistered ID
	}

	if( !accounts->load_str(accounts, &acc, sd->userid) ) {
		ShowNotice("Unknown account (account: %s, ip: %s)\n", sd->userid, ip);
		return 0; // 0 = Unregistered ID
	}

	if( !check_password(sd->md5key, sd->passwdenc, sd->passwd, acc.pass) ) {
		ShowNotice("Invalid password (account: '%s', ip: %s)\n", sd->userid, ip);
		return 1; // 1 = Incorrect Password
	}

	if( acc.expiration_time != 0 && acc.expiration_time < time(NULL) ) {
		ShowNotice("Connection refused (account: %s, expired ID, ip: %s)\n", sd->userid, ip);
		return 2; // 2 = This ID is expired
	}

	if( acc.unban_time != 0 && acc.unban_time > time(NULL) ) {
		char tmpstr[24];

		timestamp2string(tmpstr, sizeof(tmpstr), acc.unban_time, login_config.date_format);
		ShowNotice("Connection refused (account: %s, banned until %s, ip: %s)\n", sd->userid, tmpstr, ip);
		return 6; // 6 = Your are Prohibited to log in until %s
	}

	if( acc.state != 0 ) {
		ShowNotice("Connection refused (account: %s, state: %d, ip: %s)\n", sd->userid, acc.state, ip);
		return acc.state - 1;
	}

	if( login_config.client_hash_check && !isServer ) {
		struct client_hash_node *node = NULL;
		bool match = false;

		for( node = login_config.client_hash_nodes; node; node = node->next ) {
			if( acc.group_id < node->group_id )
				continue;
			if( *node->hash == '\0' || // Allowed to login without hash
				(sd->has_client_hash && memcmp(node->hash, sd->client_hash, 16) == 0) ) // Correct hash
			{
				match = true;
				break;
			}
		}

		if( !match ) {
			char smd5[33];
			int i;

			if( !sd->has_client_hash ) {
				ShowNotice("Client didn't send client hash (account: %s, ip: %s)\n", sd->userid, ip);
				return 5;
			}

			for( i = 0; i < 16; i++ )
				sprintf(&smd5[i * 2], "%02x", sd->client_hash[i]);
			smd5[32] = '\0';

			ShowNotice("Invalid client hash (account: %s, sent md5: %s, ip: %s)\n", sd->userid, smd5, ip);
			return 5;
		}
	}

	ShowNotice("Authentication accepted (account: %s, id: %d, ip: %s)\n", sd->userid, acc.account_id, ip);

	// Update session data
	sd->account_id = acc.account_id;
	sd->login_id1 = rnd() + 1;
	sd->login_id2 = rnd() + 1;
	safestrncpy(sd->lastlogin, acc.lastlogin, sizeof(sd->lastlogin));
	sd->sex = acc.sex;
	sd->group_id = acc.group_id;

	// Update account data
	timestamp2string(acc.lastlogin, sizeof(acc.lastlogin), time(NULL), "%Y-%m-%d %H:%M:%S");
	safestrncpy(acc.last_ip, ip, sizeof(acc.last_ip));
	acc.unban_time = 0;
	acc.logincount++;

	accounts->save(accounts, &acc);

	if( sd->sex != 'S' && sd->account_id < START_ACCOUNT_NUM )
		ShowWarning("Account %s has account id %d! Account IDs must be over %d to work properly!\n", sd->userid, sd->account_id, START_ACCOUNT_NUM);

	return -1; // Account OK
}

void login_auth_ok(struct login_session_data* sd)
{
	int fd = sd->fd;
	uint32 ip = session[fd]->client_addr;

	uint8 server_num, n;
	uint32 subnet_char_ip;
	struct auth_node *node;
	int i;

#if PACKETVER < 20170315
	int cmd = 0x69; // AC_ACCEPT_LOGIN
	int header = 47;
	int size = 32;
#else
	int cmd = 0xac4; // AC_ACCEPT_LOGIN3
	int header = 64;
	int size = 160;
#endif

	if( runflag != LOGINSERVER_ST_RUNNING ) {
		// players can only login while running
		WFIFOHEAD(fd,3);
		WFIFOW(fd,0) = 0x81;
		WFIFOB(fd,2) = 1;// server closed
		WFIFOSET(fd,3);
		return;
	}

	if( login_config.group_id_to_connect >= 0 && sd->group_id != login_config.group_id_to_connect ) {
		ShowStatus("Connection refused: the required group id for connection is %d (account: %s, group: %d).\n", login_config.group_id_to_connect, sd->userid, sd->group_id);
		WFIFOHEAD(fd,3);
		WFIFOW(fd,0) = 0x81;
		WFIFOB(fd,2) = 1; // 01 = Server closed
		WFIFOSET(fd,3);
		return;
	} else if( login_config.min_group_id_to_connect >= 0 && login_config.group_id_to_connect == -1 && sd->group_id < login_config.min_group_id_to_connect ) {
		ShowStatus("Connection refused: the minimum group id required for connection is %d (account: %s, group: %d).\n", login_config.min_group_id_to_connect, sd->userid, sd->group_id);
		WFIFOHEAD(fd,3);
		WFIFOW(fd,0) = 0x81;
		WFIFOB(fd,2) = 1; // 01 = Server closed
		WFIFOSET(fd,3);
		return;
	}

	server_num = 0;
	for( i = 0; i < ARRAYLENGTH(ch_server); ++i )
		if( session_isActive(ch_server[i].fd) )
			server_num++;

	if( server_num == 0 ) { // if no char-server, don't send void list of servers, just disconnect the player with proper message
		ShowStatus("Connection refused: there is no char-server online (account: %s).\n", sd->userid);
		WFIFOHEAD(fd,3);
		WFIFOW(fd,0) = 0x81;
		WFIFOB(fd,2) = 1; // 01 = Server closed
		WFIFOSET(fd,3);
		return;
	}

	{
		struct online_login_data *data = (struct online_login_data *)idb_get(online_db, sd->account_id);
		if( data ) { // account is already marked as online!
			if( data->char_server > -1 ) { // Request char servers to kick this account out. [Skotlex]
				uint8 buf[6];
				ShowNotice("User '%s' is already online - Rejected.\n", sd->userid);
				WBUFW(buf,0) = 0x2734;
				WBUFL(buf,2) = sd->account_id;
				charif_sendallwos(-1, buf, 6);
				if( data->waiting_disconnect == INVALID_TIMER )
					data->waiting_disconnect = add_timer(gettick()+AUTH_TIMEOUT, waiting_disconnect_timer, sd->account_id, 0);

				WFIFOHEAD(fd,3);
				WFIFOW(fd,0) = 0x81;
				WFIFOB(fd,2) = 8; // 08 = Server still recognizes your last login
				WFIFOSET(fd,3);
				return;
			} else if( data->char_server == -1 ) { // client has authed but did not access char-server yet
				// wipe previous session
				idb_remove(auth_db, sd->account_id);
				remove_online_user(sd->account_id);
				data = NULL;
			}
		}
	}

	login_log(ip, sd->userid, 100, "login ok");
	ShowStatus("Connection of the account '%s' accepted.\n", sd->userid);

	WFIFOHEAD(fd,header + size * server_num);
	WFIFOW(fd,0) = cmd;
	WFIFOW(fd,2) = header + size * server_num;
	WFIFOL(fd,4) = sd->login_id1;
	WFIFOL(fd,8) = sd->account_id;
	WFIFOL(fd,12) = sd->login_id2;
	WFIFOL(fd,16) = 0; // In old version, that was for ip (not more used)
	//memcpy(WFIFOP(fd,20), sd->lastlogin, 24); // In old version, that was for name (not more used)
	memset(WFIFOP(fd,20), 0, 24);
	WFIFOW(fd,44) = 0; // Unknown
	WFIFOB(fd,46) = sex_str2num(sd->sex);
#if PACKETVER >= 20170315
	memset(WFIFOP(fd,47), 0, 17); // Unknown
#endif
	for( i = 0, n = 0; i < ARRAYLENGTH(ch_server); ++i ) {
		if( !session_isValid(ch_server[i].fd) )
			continue;
		subnet_char_ip = lan_subnetcheck(ip); // Advanced subnet check [LuzZza]
		WFIFOL(fd,header + n * size) = htonl((subnet_char_ip) ? subnet_char_ip : ch_server[i].ip);
		WFIFOW(fd,header + n * size + 4) = ntows(htons(ch_server[i].port)); // [!] LE byte order here [!]
		memcpy(WFIFOP(fd,header + n * size + 6), ch_server[i].name, 20);
		WFIFOW(fd,header + n * size + 26) = ch_server[i].users;
		WFIFOW(fd,header + n * size + 28) = ch_server[i].type;
		WFIFOW(fd,header + n * size + 30) = ch_server[i].new_;
#if PACKETVER >= 20170315
		memset(WFIFOP(fd,header + n * size + 32), 0, 128); // Unknown
#endif
		n++;
	}
	WFIFOSET(fd,header + size * server_num);

	// Create temporary auth entry
	CREATE(node, struct auth_node, 1);
	node->account_id = sd->account_id;
	node->login_id1 = sd->login_id1;
	node->login_id2 = sd->login_id2;
	node->sex = sd->sex;
	node->ip = ip;
	node->clienttype = sd->clienttype;
	node->group_id = sd->group_id;
	idb_put(auth_db, sd->account_id, node);

	{
		struct online_login_data *data;

		// Mark client as 'online'
		data = add_online_user(-1, sd->account_id);

		// Schedule deletion of this node
		data->waiting_disconnect = add_timer(gettick()+AUTH_TIMEOUT, waiting_disconnect_timer, sd->account_id, 0);
	}
}

/* Log the result of a failed connection attempt by sd
 * result: nb (msg define in conf)
    0 = Unregistered ID
    1 = Incorrect Password
    2 = This ID is expired
    3 = Rejected from Server
    4 = You have been blocked by the GM Team
    5 = Your Game's EXE file is not the latest version
    6 = Your are Prohibited to log in until %s
    7 = Server is jammed due to over populated
    8 = No more accounts may be connected from this company
    9 = MSI_REFUSE_BAN_BY_DBA
    10 = MSI_REFUSE_EMAIL_NOT_CONFIRMED
    11 = MSI_REFUSE_BAN_BY_GM
    12 = MSI_REFUSE_TEMP_BAN_FOR_DBWORK
    13 = MSI_REFUSE_SELF_LOCK
    14 = MSI_REFUSE_NOT_PERMITTED_GROUP
    15 = MSI_REFUSE_NOT_PERMITTED_GROUP
    99 = This ID has been totally erased
    100 = Login information remains at %s
    101 = Account has been locked for a hacking investigation. Please contact the GM Team for more information
    102 = This account has been temporarily prohibited from login due to a bug-related investigation
    103 = This character is being deleted. Login is temporarily unavailable for the time being
    104 = This character is being deleted. Login is temporarily unavailable for the time being
     default = Unknown Error.
 */

void login_auth_failed(struct login_session_data* sd, int result)
{
	int fd = sd->fd;
	uint32 ip = session[fd]->client_addr;

	if( login_config.log_login ) {
		if( result >= 0 && result <= 15 )
		    login_log(ip, sd->userid, result, msg_txt(result));
		else if( result >= 99 && result <= 104 )
		    login_log(ip, sd->userid, result, msg_txt(result - 83)); // -83 offset.
		else
		    login_log(ip, sd->userid, result, msg_txt(22)); // Unknown error.
	}

	if( (result == 0 || result == 1) && login_config.dynamic_pass_failure_ban )
		ipban_log(ip); // Log failed password attempt

#if PACKETVER >= 20120000 // Not sure when this started
	WFIFOHEAD(fd,26);
	WFIFOW(fd,0) = 0x83e;
	WFIFOL(fd,2) = result;
	if( result != 6 )
		memset(WFIFOP(fd,6), '\0', 20);
	else { // 6 = You are prohibited to log in until %s
		struct mmo_account acc;
		time_t unban_time = ( accounts->load_str(accounts, &acc, sd->userid) ) ? acc.unban_time : 0;

		timestamp2string((char *)WFIFOP(fd,6), 20, unban_time, login_config.date_format);
	}
	WFIFOSET(fd,26);
#else
	WFIFOHEAD(fd,23);
	WFIFOW(fd,0) = 0x6a;
	WFIFOB(fd,2) = (uint8)result;
	if( result != 6 )
		memset(WFIFOP(fd,3), '\0', 20);
	else { // 6 = You are prohibited to log in until %s
		struct mmo_account acc;
		time_t unban_time = ( accounts->load_str(accounts, &acc, sd->userid) ) ? acc.unban_time : 0;

		timestamp2string((char *)WFIFOP(fd,3), 20, unban_time, login_config.date_format);
	}
	WFIFOSET(fd,23);
#endif
}


//----------------------------------------------------------------------------------------
// Default packet parsing (normal players or char-server connection requests)
//----------------------------------------------------------------------------------------
int parse_login(int fd)
{
	struct login_session_data* sd = (struct login_session_data*)session[fd]->session_data;
	int result;

	char ip[16];
	uint32 ipl = session[fd]->client_addr;
	ip2str(ipl, ip);

	if( session[fd]->flag.eof ) {
		ShowInfo("Closed connection from '"CL_WHITE"%s"CL_RESET"'.\n", ip);
		do_close(fd);
		return 0;
	}

	if( sd == NULL ) {
		// Perform ip-ban check
		if( login_config.ipban && ipban_check(ipl) ) {
			ShowStatus("Connection refused: IP isn't authorized (deny/allow, ip: %s).\n", ip);
			login_log(ipl, "unknown", -3, "ip banned");
			WFIFOHEAD(fd,23);
			WFIFOW(fd,0) = 0x6a;
			WFIFOB(fd,2) = 3; // 3 = Rejected from Server
			WFIFOSET(fd,23);
			set_eof(fd);
			return 0;
		}

		// Create a session for this new connection
		CREATE(session[fd]->session_data, struct login_session_data, 1);
		sd = (struct login_session_data*)session[fd]->session_data;
		sd->fd = fd;
	}

	while( RFIFOREST(fd) >= 2 ) {
		uint16 command = RFIFOW(fd,0);

		switch( command ) {

			case 0x0200: // New alive packet: structure: 0x200 <account.userid>.24B. used to verify if client is always alive.
				if (RFIFOREST(fd) < 26)
					return 0;
				RFIFOSKIP(fd,26);
				break;

			// Client md5 hash (binary)
			case 0x0204: // S 0204 <md5 hash>.16B (kRO 2004-05-31aSakexe langtype 0 and 6)
				if (RFIFOREST(fd) < 18)
					return 0;

				sd->has_client_hash = 1;
				memcpy(sd->client_hash, RFIFOP(fd, 2), 16);

				RFIFOSKIP(fd,18);
				break;

			// Request client login (raw password)
			case 0x0064: // S 0064 <version>.L <username>.24B <password>.24B <clienttype>.B
			case 0x0277: // S 0277 <version>.L <username>.24B <password>.24B <clienttype>.B <ip address>.16B <adapter address>.13B
			case 0x02b0: // S 02b0 <version>.L <username>.24B <password>.24B <clienttype>.B <ip address>.16B <adapter address>.13B <g_isGravityID>.B
			// Request client login (md5-hashed password)
			case 0x01dd: // S 01dd <version>.L <username>.24B <password hash>.16B <clienttype>.B
			case 0x01fa: // S 01fa <version>.L <username>.24B <password hash>.16B <clienttype>.B <?>.B(index of the connection in the clientinfo file (+10 if the command-line contains "pc"))
			case 0x027c: // S 027c <version>.L <username>.24B <password hash>.16B <clienttype>.B <?>.13B(junk)
			case 0x0825: // S 0825 <packetsize>.W <version>.L <clienttype>.B <userid>.24B <password>.27B <mac>.17B <ip>.15B <token>.(packetsize - 0x5C)B
				{
					size_t packet_len = RFIFOREST(fd);

					if( (command == 0x0064 && packet_len < 55)
					||  (command == 0x0277 && packet_len < 84)
					||  (command == 0x02b0 && packet_len < 85)
					||  (command == 0x01dd && packet_len < 47)
					||  (command == 0x01fa && packet_len < 48)
					||  (command == 0x027c && packet_len < 60)
					||  (command == 0x0825 && (packet_len < 4 || packet_len < RFIFOW(fd, 2))) )
						return 0;
				}
				{
					char username[NAME_LENGTH];
					char password[NAME_LENGTH];
					unsigned char passhash[16];
					uint8 clienttype;
					bool israwpass = (command==0x0064 || command==0x0277 || command==0x02b0 || command == 0x0825);

					// Shinryo: For the time being, just use token as password.
					if(command == 0x0825) {
						char *accname = (char *)RFIFOP(fd, 9);
						char *token = (char *)RFIFOP(fd, 0x5C);
						size_t uAccLen = strlen(accname);
						size_t uTokenLen = RFIFOREST(fd) - 0x5C;

						if(uAccLen > NAME_LENGTH - 1 || uAccLen == 0 || uTokenLen > NAME_LENGTH - 1  || uTokenLen == 0) {
							login_auth_failed(sd, 3);
							return 0;
						}
						safestrncpy(username, accname, uAccLen + 1);
						safestrncpy(password, token, uTokenLen + 1);
						clienttype = RFIFOB(fd, 8);
					} else {
						safestrncpy(username, (const char *)RFIFOP(fd,6), NAME_LENGTH);
						if( israwpass ) {
							safestrncpy(password, (const char *)RFIFOP(fd,30), NAME_LENGTH);
							clienttype = RFIFOB(fd,54);
						} else {
							memcpy(passhash, RFIFOP(fd,30), 16);
							clienttype = RFIFOB(fd,46);
						}
					}
					RFIFOSKIP(fd,RFIFOREST(fd)); // Assume no other packet was sent

					sd->clienttype = clienttype;
					safestrncpy(sd->userid, username, NAME_LENGTH);
					if( israwpass ) {
						ShowStatus("Request for connection of %s (ip: %s)\n", sd->userid, ip);
						safestrncpy(sd->passwd, password, NAME_LENGTH);
						if( login_config.use_md5_passwds )
							MD5_String(sd->passwd, sd->passwd);
						sd->passwdenc = 0;
					} else {
						ShowStatus("Request for connection (passwdenc mode) of %s (ip: %s)\n", sd->userid, ip);
						bin2hex(sd->passwd, passhash, 16); // raw binary data here!
						sd->passwdenc = PASSWORDENC;
					}

					if( sd->passwdenc != 0 && login_config.use_md5_passwds ) {
						login_auth_failed(sd, 3); // Send "rejected from server"
						return 0;
					}

					result = mmo_auth(sd, false);

					if( result == -1 )
						login_auth_ok(sd);
					else
						login_auth_failed(sd, result);
				}
				break;

			case 0x01db: // Sending request of the coding key
				RFIFOSKIP(fd,2);
				{
					memset(sd->md5key, '\0', sizeof(sd->md5key));
					sd->md5keylen = (uint16)(12 + rnd() % 4);
					MD5_Salt(sd->md5keylen, sd->md5key);

					WFIFOHEAD(fd,4 + sd->md5keylen);
					WFIFOW(fd,0) = 0x01dc;
					WFIFOW(fd,2) = 4 + sd->md5keylen;
					memcpy(WFIFOP(fd,4), sd->md5key, sd->md5keylen);
					WFIFOSET(fd,WFIFOW(fd,2));
				}
				break;

			case 0x2710: // Connection request of a char-server
				if (RFIFOREST(fd) < 86)
					return 0;
				{
					char server_name[20];
					char message[256];
					uint32 server_ip;
					uint16 server_port;
					uint16 type;
					uint16 new_;

					safestrncpy(sd->userid, (char *)RFIFOP(fd,2), NAME_LENGTH);
					safestrncpy(sd->passwd, (char *)RFIFOP(fd,26), NAME_LENGTH);
					if( login_config.use_md5_passwds )
						MD5_String(sd->passwd, sd->passwd);
					sd->passwdenc = 0;
					server_ip = ntohl(RFIFOL(fd,54));
					server_port = ntohs(RFIFOW(fd,58));
					safestrncpy(server_name, (char *)RFIFOP(fd,60), 20);
					type = RFIFOW(fd,82);
					new_ = RFIFOW(fd,84);
					RFIFOSKIP(fd,86);

					ShowInfo("Connection request of the char-server '%s' @ %u.%u.%u.%u:%u (account: '%s', ip: '%s')\n", server_name, CONVIP(server_ip), server_port, sd->userid, ip);
					sprintf(message, "charserver - %s@%u.%u.%u.%u:%u", server_name, CONVIP(server_ip), server_port);
					login_log(session[fd]->client_addr, sd->userid, 100, message);

					result = mmo_auth(sd, true);
					if( runflag == LOGINSERVER_ST_RUNNING &&
						result == -1 &&
						sd->sex == 'S' &&
						sd->account_id < ARRAYLENGTH(ch_server) &&
						!session_isValid(ch_server[sd->account_id].fd) )
					{
						ShowStatus("Connection of the char-server '%s' accepted.\n", server_name);
						safestrncpy(ch_server[sd->account_id].name, server_name, sizeof(ch_server[sd->account_id].name));
						ch_server[sd->account_id].fd = fd;
						ch_server[sd->account_id].ip = server_ip;
						ch_server[sd->account_id].port = server_port;
						ch_server[sd->account_id].users = 0;
						ch_server[sd->account_id].type = type;
						ch_server[sd->account_id].new_ = new_;

						session[fd]->func_parse = parse_fromchar;
						session[fd]->flag.server = 1;
						realloc_fifo(fd, FIFOSIZE_SERVERLINK, FIFOSIZE_SERVERLINK);

						// Send connection success
						WFIFOHEAD(fd,3);
						WFIFOW(fd,0) = 0x2711;
						WFIFOB(fd,2) = 0;
						WFIFOSET(fd,3);
					} else {
						ShowNotice("Connection of the char-server '%s' REFUSED.\n", server_name);
						WFIFOHEAD(fd,3);
						WFIFOW(fd,0) = 0x2711;
						WFIFOB(fd,2) = 3;
						WFIFOSET(fd,3);
					}
				}
				return 0; // Processing will continue elsewhere

			default:
				ShowNotice("Abnormal end of connection (ip: %s): Unknown packet 0x%x\n", ip, command);
				set_eof(fd);
				return 0;
		}
	}

	return 0;
}


void login_set_defaults() {
	login_config.login_ip = INADDR_ANY;
	login_config.login_port = 6900;
	login_config.ipban_cleanup_interval = 60;
	login_config.ip_sync_interval = 0;
	login_config.log_login = true;
	safestrncpy(login_config.date_format, "%Y-%m-%d %H:%M:%S", sizeof(login_config.date_format));
	login_config.console = false;
	login_config.new_account_flag = true;
	login_config.new_acc_length_limit = true;
	login_config.use_md5_passwds = false;
	login_config.group_id_to_connect = -1;
	login_config.min_group_id_to_connect = -1;

	login_config.ipban = true;
	login_config.dynamic_pass_failure_ban = true;
	login_config.dynamic_pass_failure_ban_interval = 5;
	login_config.dynamic_pass_failure_ban_limit = 7;
	login_config.dynamic_pass_failure_ban_duration = 5;
	login_config.use_dnsbl = false;
	safestrncpy(login_config.dnsbl_servs, "", sizeof(login_config.dnsbl_servs));
	safestrncpy(login_config.account_engine, "auto", sizeof(login_config.account_engine));

	login_config.client_hash_check = 0;
	login_config.client_hash_nodes = NULL;
	login_config.char_per_account = MAX_CHARS - MAX_CHAR_VIP - MAX_CHAR_BILLING;
#ifdef VIP_ENABLE
	login_config.vip_sys.char_increase = MAX_CHAR_VIP;
	login_config.vip_sys.group_id = 5;
#endif
}

//-----------------------------------
// Reading main configuration file
//-----------------------------------
int login_config_read(const char *cfgName)
{
	char line[1024], w1[32], w2[1024];
	FILE *fp = fopen(cfgName, "r");

	if(!fp) {
		ShowError("Configuration file (%s) not found.\n", cfgName);
		return 1;
	}
	while(fgets(line, sizeof(line), fp)) {
		if(line[0] == '/' && line[1] == '/')
			continue;
		if(sscanf(line, "%31[^:]: %1023[^\r\n]", w1, w2) < 2)
			continue;
		if(!strcmpi(w1, "timestamp_format"))
			safestrncpy(timestamp_format, w2, 20);
		else if(!strcmpi(w1, "stdout_with_ansisequence"))
			stdout_with_ansisequence = config_switch(w2);
		else if(!strcmpi(w1, "console_silent")) {
			msg_silent = atoi(w2);
			if(msg_silent) /* Only bother if we actually have this enabled */
				ShowInfo("Console Silent Setting: %d\n", atoi(w2));
		} else if(!strcmpi(w1, "console_msg_log"))
			console_msg_log = atoi(w2);
		else if(!strcmpi(w1, "console_log_filepath"))
			safestrncpy(console_log_filepath, w2, sizeof(console_log_filepath));
		else if(!strcmpi(w1, "bind_ip")) {
			login_config.login_ip = host2ip(w2);
			if(login_config.login_ip) {
				char ip_str[16];

				ShowStatus("Login server binding IP address : %s -> %s\n", w2, ip2str(login_config.login_ip, ip_str));
			}
		} else if(!strcmpi(w1, "login_port"))
			login_config.login_port = (uint16)atoi(w2);
		else if(!strcmpi(w1, "log_login"))
			login_config.log_login = (bool)config_switch(w2);
		else if(!strcmpi(w1, "new_account"))
			login_config.new_account_flag = (bool)config_switch(w2);
		else if(!strcmpi(w1, "new_acc_length_limit"))
			login_config.new_acc_length_limit = (bool)config_switch(w2);
		else if(!strcmpi(w1, "start_limited_time"))
			login_config.start_limited_time = atoi(w2);
		else if(!strcmpi(w1, "use_MD5_passwords"))
			login_config.use_md5_passwds = (bool)config_switch(w2);
		else if(!strcmpi(w1, "group_id_to_connect"))
			login_config.group_id_to_connect = atoi(w2);
		else if(!strcmpi(w1, "min_group_id_to_connect"))
			login_config.min_group_id_to_connect = atoi(w2);
		else if(!strcmpi(w1, "date_format"))
			safestrncpy(login_config.date_format, w2, sizeof(login_config.date_format));
		else if(!strcmpi(w1, "console"))
			login_config.console = (bool)config_switch(w2);
		else if(!strcmpi(w1, "allowed_regs")) //Account flood protection system
			allowed_regs = atoi(w2);
		else if(!strcmpi(w1, "time_allowed"))
			time_allowed = atoi(w2);
		else if(!strcmpi(w1, "use_dnsbl"))
			login_config.use_dnsbl = (bool)config_switch(w2);
		else if(!strcmpi(w1, "dnsbl_servers"))
			safestrncpy(login_config.dnsbl_servs, w2, sizeof(login_config.dnsbl_servs));
		else if(!strcmpi(w1, "ipban_cleanup_interval"))
			login_config.ipban_cleanup_interval = (unsigned int)atoi(w2);
		else if(!strcmpi(w1, "ip_sync_interval"))
			login_config.ip_sync_interval = (unsigned int)1000 * 60 * atoi(w2); //w2 comes in minutes.
		else if(!strcmpi(w1, "client_hash_check"))
			login_config.client_hash_check = config_switch(w2);
		else if(!strcmpi(w1, "client_hash")) {
			int group = 0;
			char md5[33];

			if(sscanf(w2, "%3d, %32s", &group, md5) == 2) {
				struct client_hash_node *nnode;

				CREATE(nnode, struct client_hash_node, 1);
				if(!strcmpi(md5, "disabled"))
					nnode->hash[0] = '\0';
				else {
					int i;

					for(i = 0; i < 32; i += 2) {
						char buf[3];
						unsigned int byte;

						memcpy(buf, &md5[i], 2);
						buf[2] = 0;
						sscanf(buf, "%2x", &byte);
						nnode->hash[i / 2] = (uint8)(byte&0xFF);
					}
				}
				nnode->group_id = group;
				nnode->next = login_config.client_hash_nodes;
				login_config.client_hash_nodes = nnode;
			}
		} else if(!strcmpi(w1, "chars_per_account")) { //Max chars per account [Sirius]
			login_config.char_per_account = atoi(w2);
			if(login_config.char_per_account <= 0 || login_config.char_per_account > MAX_CHARS) {
				if(login_config.char_per_account > MAX_CHARS) {
					ShowWarning("Max chars per account '%d' exceeded limit. Defaulting to '%d'.\n", login_config.char_per_account, MAX_CHARS);
					login_config.char_per_account = MAX_CHARS;
				}
				login_config.char_per_account = MIN_CHARS;
			}
		}
#ifdef VIP_ENABLE
		else if(!strcmpi(w1, "vip_group"))
			login_config.vip_sys.group_id = cap_value(atoi(w2), 0, 99);
		else if(!strcmpi(w1, "vip_char_increase")) {
			if(atoi(w2) == -1)
				login_config.vip_sys.char_increase = MAX_CHAR_VIP;
			else
				login_config.vip_sys.char_increase = atoi(w2);
			if(login_config.vip_sys.char_increase > (unsigned int)MAX_CHARS - login_config.char_per_account) {
				ShowWarning("vip_char_increase too high, can only go up to %d, according to your char_per_account config %d\n",
					MAX_CHARS - login_config.char_per_account, login_config.char_per_account);
				login_config.vip_sys.char_increase = MAX_CHARS - login_config.char_per_account;
			}
		}
#endif
		else if(!strcmpi(w1, "import"))
			login_config_read(w2);
		else if(!strcmpi(w1, "account.engine"))
			safestrncpy(login_config.account_engine, w2, sizeof(login_config.account_engine));
		else { //Try the account engines
			int i;

			for( i = 0; account_engines[i].constructor; ++i ) {
				AccountDB *db = account_engines[i].db;
				if( db && db->set_property(db, w1, w2) )
					break;
			}
			//Try others
			ipban_config_read(w1, w2);
			loginlog_config_read(w1, w2);
		}
	}
	fclose(fp);
	ShowInfo("Finished reading %s.\n", cfgName);
	return 0;
}

/// Get the engine selected in the config settings.
/// Updates the config setting with the selected engine if 'auto'.
static AccountDB *get_account_engine(void)
{
	int i;
	bool get_first = (strcmp(login_config.account_engine,"auto") == 0);

	for( i = 0; account_engines[i].constructor; ++i )
	{
		char name[sizeof(login_config.account_engine)];
		AccountDB *db = account_engines[i].db;
		if( db && db->get_property(db, "engine.name", name, sizeof(name)) &&
			(get_first || strcmp(name, login_config.account_engine) == 0) )
		{
			if( get_first )
				safestrncpy(login_config.account_engine, name, sizeof(login_config.account_engine));
			return db;
		}
	}
	return NULL;
}

//--------------------------------------
// Function called at exit of the server
//--------------------------------------
void do_final(void)
{
	int i;
	struct client_hash_node *hn = login_config.client_hash_nodes;

	while (hn) {
		struct client_hash_node *tmp = hn;
		hn = hn->next;
		aFree(tmp);
	}

	login_log(0, "login server", 100, "login server shutdown");
	ShowStatus("Terminating...\n");

	if( login_config.log_login )
		loginlog_final();

	do_final_msg();
	ipban_final();

	for( i = 0; account_engines[i].constructor; ++i ) { // destroy all account engines
		AccountDB *db = account_engines[i].db;
		if( db ) {
			db->destroy(db);
			account_engines[i].db = NULL;
		}
	}
	accounts = NULL; // destroyed in account_engines
	online_db->destroy(online_db, NULL);
	auth_db->destroy(auth_db, NULL);

	for( i = 0; i < ARRAYLENGTH(ch_server); ++i )
		chrif_server_destroy(i);

	if( login_fd != -1 ) {
		do_close(login_fd);
		login_fd = -1;
	}

	ShowStatus("Finished.\n");
}

//------------------------------
// Function called when the server
// has received a crash signal.
//------------------------------
void do_abort(void)
{
}

void set_server_type(void)
{
	SERVER_TYPE = ATHENA_SERVER_LOGIN;
}


/// Called when a terminate signal is received.
void do_shutdown(void)
{
	if( runflag != LOGINSERVER_ST_SHUTDOWN ) {
		int id;

		runflag = LOGINSERVER_ST_SHUTDOWN;
		ShowStatus("Shutting down...\n");
		//@TODO: Proper shutdown procedure; kick all characters, wait for acks, ...  [FlavioJS]
		for( id = 0; id < ARRAYLENGTH(ch_server); ++id )
			chrif_server_reset(id);
		flush_fifos();
		runflag = CORE_ST_STOP;
	}
}


//------------------------------
// Login server initialization
//------------------------------
int do_init(int argc, char **argv)
{
	int i;

	runflag = LOGINSERVER_ST_STARTING;

	// Intialize engines (to accept config settings)
	for( i = 0; account_engines[i].constructor; ++i )
		account_engines[i].db = account_engines[i].constructor();

	// Read login-server configuration
	login_set_defaults();

	// Init default value
	LOGIN_CONF_NAME = "conf/login_athena.conf";
	LAN_CONF_NAME = "conf/subnet_athena.conf";
	MSG_CONF_NAME = "conf/msg_conf/login_msg.conf";
	safestrncpy(console_log_filepath, "./log/login-msg_log.log", sizeof(console_log_filepath));

	cli_get_options(argc,argv);

	msg_config_read(MSG_CONF_NAME);
	login_config_read(LOGIN_CONF_NAME);
	login_lan_config_read(LAN_CONF_NAME);

	rnd_init();

	for( i = 0; i < ARRAYLENGTH(ch_server); ++i )
		chrif_server_init(i);

	// Initialize logging
	if( login_config.log_login )
		loginlog_init();

	// Initialize static and dynamic ipban system
	ipban_init();

	// Online user database init
	online_db = idb_alloc(DB_OPT_RELEASE_DATA);
	add_timer_func_list(waiting_disconnect_timer, "waiting_disconnect_timer");

	// Interserver auth init
	auth_db = idb_alloc(DB_OPT_RELEASE_DATA);

	// Set default parser as parse_login function
	set_defaultparse(parse_login);

	// Every 10 minutes cleanup online account db.
	add_timer_func_list(online_data_cleanup, "online_data_cleanup");
	add_timer_interval(gettick() + 600*1000, online_data_cleanup, 0, 0, 600*1000);

	// Add timer to detect ip address change and perform update
	if (login_config.ip_sync_interval) {
		add_timer_func_list(sync_ip_addresses, "sync_ip_addresses");
		add_timer_interval(gettick() + login_config.ip_sync_interval, sync_ip_addresses, 0, 0, login_config.ip_sync_interval);
	}

	// Account database init
	accounts = get_account_engine();
	if( accounts == NULL ) {
		ShowFatalError("do_init: account engine '%s' not found.\n", login_config.account_engine);
		exit(EXIT_FAILURE);
	} else {

		if(!accounts->init(accounts)) {
			ShowFatalError("do_init: Failed to initialize account engine '%s'.\n", login_config.account_engine);
			exit(EXIT_FAILURE);
		}
	}

	// Server port open & binding
	if( (login_fd = make_listen_bind(login_config.login_ip,login_config.login_port)) == -1 ) {
		ShowFatalError("Failed to bind to port '"CL_WHITE"%d"CL_RESET"'\n",login_config.login_port);
		exit(EXIT_FAILURE);
	}

	if( runflag != CORE_ST_STOP ) {
		shutdown_callback = do_shutdown;
		runflag = LOGINSERVER_ST_RUNNING;
	}

	account_db_sql_up(accounts);

	ShowStatus("The login-server is "CL_GREEN"ready"CL_RESET" (Server is listening on the port %u).\n\n", login_config.login_port);
	login_log(0, "login server", 100, "login server started");

	if( login_config.console ) {
		add_timer_func_list(parse_console_timer, "parse_console_timer");
		add_timer_interval(gettick()+1000, parse_console_timer, 0, 0, 1000); //start in 1s each 1sec
	}

	return 0;
}

int login_msg_config_read(char *cfgName) {
	return _msg_config_read(cfgName,LOGIN_MAX_MSG,msg_table);
}
const char *login_msg_txt(int msg_number) {
	return _msg_txt(msg_number,LOGIN_MAX_MSG,msg_table);
}
void login_do_final_msg(void) {
	_do_final_msg(LOGIN_MAX_MSG,msg_table);
}

/*======================================================
 * Login-Server help option info
 *------------------------------------------------------*/
void display_helpscreen(bool do_exit)
{
	ShowInfo("Usage: %s [options]\n", SERVER_NAME);
	ShowInfo("\n");
	ShowInfo("Options:\n");
	ShowInfo("  -?, -h [--help]\t\tDisplays this help screen.\n");
	ShowInfo("  -v [--version]\t\tDisplays the server's version.\n");
	ShowInfo("  --run-once\t\t\tCloses server after loading (testing).\n");
	ShowInfo("  --login-config <file>\t\tAlternative login-server configuration.\n");
	ShowInfo("  --lan-config <file>\t\tAlternative lag configuration.\n");
	ShowInfo("  --msg-config <file>\t\tAlternative message configuration.\n");
	if( do_exit )
		exit(EXIT_SUCCESS);
}
