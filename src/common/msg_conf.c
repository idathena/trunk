#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "malloc.h"
#include "msg_conf.h"
#include "showmsg.h"
#include "strlib.h"

/**
 * Return the message string of the specified number by [Yor]
 * (read in table msg_table, with specified lenght table in size)
 */
const char *_msg_txt(int msg_number, int size, char **msg_table)
{
    if (msg_number >= 0 && msg_number < size &&
	    msg_table[msg_number] != NULL && msg_table[msg_number][0] != '\0')
	return msg_table[msg_number];

    return "??";
}

/**
 * Read txt file and store them into msg_table
 */
int _msg_config_read(const char *cfgName, int size, char **msg_table)
{
	int msg_number;
	uint16 msg_count = 0, line_num = 0;
	char line[1024], w1[8], w2[512];
	FILE *fp;
	static int called = 1;

	if ((fp = fopen(cfgName, "r")) == NULL) {
		ShowError("Messages file not found: %s\n", cfgName);
		return -1;
	}

	if ((--called) == 0)
		memset(msg_table, 0, sizeof (msg_table[0]) * size);

	while (fgets(line, sizeof (line), fp)) {
		line_num++;
		if (line[0] == '/' && line[1] == '/')
			continue;
		if (sscanf(line, "%7[^:]: %511[^\r\n]", w1, w2) != 2)
			continue;

		if (strcmpi(w1, "import") == 0)
			_msg_config_read(w2,size,msg_table);
		else {
			msg_number = atoi(w1);
			if (msg_number >= 0 && msg_number < size) {
				size_t len = strnlen(w2, sizeof(w2)) + 1;

				if (msg_table[msg_number] != NULL)
					aFree(msg_table[msg_number]);
				msg_table[msg_number] = (char *)aMalloc(len * sizeof(char));
				safestrncpy(msg_table[msg_number], w2, len);
				msg_count++;
			} else
				ShowWarning("Invalid message ID '%s' at line %d from '%s' file.\n", w1, line_num, cfgName);
		}
	}

	fclose(fp);
	ShowInfo("Done reading "CL_WHITE"'%d'"CL_RESET" messages in "CL_WHITE"'%s'"CL_RESET".\n", msg_count, cfgName);

	return 0;
}

/**
 * Destroy msg_table (freeup mem)
 */
void _do_final_msg(int size, char **msg_table)
{
	int i;

	for (i = 0; i < size; i++)
		aFree(msg_table[i]);
}

/**
 * Lookup a langtype string into his associate langtype number
 * return -1 if not found
 */
int msg_langstr2langtype(char *langtype)
{
	int lang = -1;

	if (!strncmpi(langtype, "eng", 2))
		lang = 0;
	else if (!strncmpi(langtype, "idn", 2))
		lang = 1;
	else if (!strncmpi(langtype, "spn", 2))
		lang = 2;

	return lang;
}

const char *msg_langtype2langstr(int langtype)
{
	switch (langtype) {
		case 0: return "English (ENG)";
		case 1: return "Bahasa Indonesia (IDN)";
		case 2: return "Spanish (SPN)";
		default: return "??";
	}
}

/**
 * Verify that the choosen langtype is enable
 * return
 *  1 : langage enable
 * -1 : false range
 * -2 : disable
 */
int msg_checklangtype(int lang, bool display)
{
	uint16 test = (1<<(lang - 1));

	if (!lang)
		return 1; //Default English
	else if (lang < 0 || test > LANG_MAX)
		return -1; //False range
	else if (LANG_ENABLE&test)
		return 1;
	else if (display)
		ShowDebug("msg_checklangtype: Unsupported langtype '%d'.\n", lang);

	return -2;
}
