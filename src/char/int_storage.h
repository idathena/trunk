// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _INT_STORAGE_SQL_H_
#define _INT_STORAGE_SQL_H_

struct s_storage;

void inter_storage_sql_init(void);
void inter_storage_sql_final(void);

bool inter_premiumStorage_exists(uint8 id);
int inter_premiumStorage_getMax(uint8 id);
const char *inter_premiumStorage_getTableName(uint8 id);
const char *inter_premiumStorage_getPrintableName(uint8 id);

bool inter_storage_parse_frommap(int fd);

#endif /* _INT_STORAGE_SQL_H_ */
