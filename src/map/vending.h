// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef	_VENDING_H_
#define	_VENDING_H_

#include "../common/cbasetypes.h"
#include "buyingstore.h"
//#include "map.h"

struct map_session_data;
struct s_search_store_search;

struct s_vending {
	short index; //Cart index (return item data)
	short amount; //Amout of the item for vending
	unsigned int value; //At wich price
};

DBMap *vending_db; //Db holder the vender : charid -> map_session_data

void vending_reopen(struct map_session_data *sd);
void vending_closevending(struct map_session_data *sd);
int8 vending_openvending(struct map_session_data *sd, const char *message, const uint8 *data, int count, struct s_autotrader *at);
void vending_vendinglistreq(struct map_session_data *sd, int id);
void vending_purchasereq(struct map_session_data *sd, int aid, int uid, const uint8 *data, int count);
bool vending_search(struct map_session_data *sd, unsigned short nameid);
bool vending_searchall(struct map_session_data *sd, const struct s_search_store_search *s);

void do_final_vending(void);
void do_init_vending(void);
void do_init_vending_autotrade(void);

#endif /* _VENDING_H_ */
