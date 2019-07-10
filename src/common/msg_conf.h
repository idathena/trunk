// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef MSG_CONF_H
#define	MSG_CONF_H

#ifdef	__cplusplus
extern "C" {
#endif

enum e_lang_types {
	LANG_IDN = 0x1,
	LANG_SPN = 0x2,
	LANG_MAX
};

// Multilanguage System.
// Define which languages to enable (bitmask).
// 0xF will enable all, while 0x0 will enable English only.
#define LANG_ENABLE 0x3

// Read msg in table
const char *_msg_txt(int msg_number, int size, char **msg_table);
// Store msg from txtfile into msg_table
int _msg_config_read(const char *cfgName, int size, char **msg_table);
// Clear msg_table
void _do_final_msg(int size, char **msg_table);
int msg_langstr2langtype(char *langtype);
const char *msg_langtype2langstr(int langtype);
// Verify that the choosen langtype is enabled.
int msg_checklangtype(int lang, bool display);

#ifdef	__cplusplus
}
#endif

#endif	/* MSG_CONF_H */
