// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef	_TIMER_H_
#define	_TIMER_H_

#include "../common/cbasetypes.h"
#include <time.h>

#define DIFF_TICK(a,b) ((int)((a)-(b)))

#define INVALID_TIMER -1
#define CLIF_WALK_TIMER -2

// timer flags
enum {
	TIMER_ONCE_AUTODEL = 0x01,
	TIMER_INTERVAL = 0x02,
	TIMER_REMOVE_HEAP = 0x10,
};

#define TIMER_FUNC(x) int x(int tid, unsigned int tick, int id, intptr_t data)

// Struct declaration
typedef TIMER_FUNC((*TimerFunc));

struct TimerData {
	unsigned int tick;
	TimerFunc func;
	unsigned char type;
	int interval;

	// general-purpose storage
	int id; 
	intptr_t data;
};

// Function prototype declaration

unsigned int gettick(void);
unsigned int gettick_nocache(void);

int add_timer(unsigned int tick, TimerFunc func, int id, intptr_t data);
int add_timer_interval(unsigned int tick, TimerFunc func, int id, intptr_t data, int interval);
const struct TimerData *get_timer(int tid);
int delete_timer(int tid, TimerFunc func);

int addtick_timer(int tid, unsigned int tick);
int settick_timer(int tid, unsigned int tick);

int add_timer_func_list(TimerFunc func, char *name);

unsigned long get_uptime(void);

const char *timestamp2string(char *str, size_t size, time_t timestamp, const char *format);
void split_time(int time, int *year, int *month, int *day, int *hour, int *minute, int *second);
double solve_time(char *modif_p);

int do_timer(unsigned int tick);
void timer_init(void);
void timer_final(void);

#endif /* _TIMER_H_ */
