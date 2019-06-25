// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "mmo.h"
#include "showmsg.h"
#include "malloc.h"
#include "core.h"
#include "strlib.h"
#ifndef MINICORE
#include "db.h"
#include "socket.h"
#include "timer.h"
#include "sql.h"
#include "cbasetypes.h"
#include "msg_conf.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include "../common/winapi.h" // Console close event handling
#include <direct.h>
#endif


/// Called when a terminate signal is received.
void (*shutdown_callback)(void) = NULL;

#if defined(BUILDBOT)
	int buildbotflag = 0;
#endif

int runflag = CORE_ST_RUN;
int arg_c = 0;
char **arg_v = NULL;

char *SERVER_NAME = NULL;
char SERVER_TYPE = ATHENA_SERVER_NONE;

#ifndef MINICORE	// minimalist Core
// Added by Gabuzomeu
//
// This is an implementation of signal() using sigaction() for portability.
// (sigaction() is POSIX; signal() is not.)  Taken from Stevens' _Advanced
// Programming in the UNIX Environment_.
//
#ifdef WIN32	// windows don't have SIGPIPE
#define SIGPIPE SIGINT
#endif

#ifndef POSIX
#define compat_signal(signo, func) signal(signo, func)
#else
sigfunc *compat_signal(int signo, sigfunc *func) {
	struct sigaction sact, oact;

	sact.sa_handler = func;
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = 0;
#ifdef SA_INTERRUPT
	sact.sa_flags |= SA_INTERRUPT;	/* SunOS */
#endif

	if (sigaction(signo, &sact, &oact) < 0)
		return (SIG_ERR);

	return (oact.sa_handler);
}
#endif

/*======================================
 *	CORE : Console events for Windows
 *--------------------------------------*/
#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD c_event) {
    switch(c_event) {
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			if( shutdown_callback != NULL )
				shutdown_callback();
			else
				runflag = CORE_ST_STOP;// auto-shutdown
			break;
		default:
			return FALSE;
		}
    return TRUE;
}

static void cevents_init() {
	if (SetConsoleCtrlHandler(console_handler,TRUE)==FALSE)
		ShowWarning ("Unable to install the console handler!\n");
}
#endif

/*======================================
 *	CORE : Signal Sub Function
 *--------------------------------------*/
static void sig_proc(int sn) {
	static int is_called = 0;

	switch (sn) {
		case SIGINT:
		case SIGTERM:
			if (++is_called > 3)
				exit(EXIT_SUCCESS);
			if( shutdown_callback != NULL )
				shutdown_callback();
			else
				runflag = CORE_ST_STOP;// auto-shutdown
			break;
		case SIGSEGV:
		case SIGFPE:
			do_abort();
			// Pass the signal to the system's default handler
			compat_signal(sn, SIG_DFL);
			raise(sn);
			break;
#ifndef _WIN32
		case SIGXFSZ:
			// ignore and allow it to set errno to EFBIG
			ShowWarning ("Max file size reached!\n");
			//run_flag = 0;	// should we quit?
			break;
		case SIGPIPE:
			//ShowInfo ("Broken pipe found... closing socket\n");	// set to eof in socket.c
			break;	// does nothing here
#endif
	}
}

void signals_init (void) {
	compat_signal(SIGTERM, sig_proc);
	compat_signal(SIGINT, sig_proc);
#ifndef _DEBUG // need unhandled exceptions to debug on Windows
	compat_signal(SIGSEGV, sig_proc);
	compat_signal(SIGFPE, sig_proc);
#endif
#ifndef _WIN32
	compat_signal(SIGILL, SIG_DFL);
	compat_signal(SIGXFSZ, sig_proc);
	compat_signal(SIGPIPE, sig_proc);
	compat_signal(SIGBUS, SIG_DFL);
	compat_signal(SIGTRAP, SIG_DFL);
#endif
}
#endif

// Grabs the hash from the last time the user updated their working copy (last pull)
const char *get_git_hash(void) {
	static char GitHash[41] = ""; //Sha(40) + 1
	FILE *fp;

	if (GitHash[0] != '\0')
		return GitHash;

	if ((fp = fopen(".git/refs/remotes/origin/master", "r")) != NULL || //Already pulled once
		(fp = fopen(".git/refs/heads/master", "r")) != NULL) { //Cloned only
		char line[64];
		char *rev = (char *)malloc(sizeof(char) * 50);

		if (fgets(line, sizeof(line), fp) && sscanf(line, "%40s", rev))
			snprintf(GitHash, sizeof(GitHash), "%s", rev);
		free(rev);
		fclose(fp);
	} else
		GitHash[0] = UNKNOWN_VERSION;

	if (!(*GitHash))
		GitHash[0] = UNKNOWN_VERSION;

	return GitHash;
}

/*======================================
 *	CORE : Display title
 *  ASCII By CalciumKid 1/12/2011
 *--------------------------------------*/
static void display_title(void) {
	const char *git = get_git_hash();

	ShowMessage("\n");
	ShowMessage(""CL_PASS"        "CL_BOLD"                                                             "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_BT_WHITE"                    Mempersembahkan                      "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_BOLD"       _      _     ___   __  __                             "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_BOLD"      |_| ___| |   /   | / /_/ /_  ___  ____  ____ _         "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_BOLD"      | ||  _  |  / /| |/ __/ __ \\/ _ \\/ __ \\/ __ `/      "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_BOLD"      | || |_| | / ___ / /_/ / / /  __/ / / / /_/ /          "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_BOLD"      |_||____/ /_/  |_\\__/_/ /_/\\___/_/ /_/\\__,_/        "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_BOLD"                                                             "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_GREEN"            http://github.com/idathena/trunk/               "CL_PASS""CL_CLL""CL_NORMAL"\n");
	ShowMessage(""CL_PASS"        "CL_BOLD"                                                             "CL_PASS""CL_CLL""CL_NORMAL"\n");

	if (git[0] != UNKNOWN_VERSION)
		ShowInfo("Git Hash: '"CL_WHITE"%s"CL_RESET"'\n", git);
}

// Warning if executed as superuser (root)
void usercheck(void)
{
#ifndef _WIN32
    if( geteuid() == 0 )
		ShowWarning ("You are running idAthena with root privileges, it is not necessary.\n");
#endif
}

/*======================================
 *	CORE : MAINROUTINE
 *--------------------------------------*/
int main(int argc, char **argv)
{
	{ // Initialize program arguments
		char *p1 = SERVER_NAME = argv[0];

		if( (p1 = strrchr(argv[0], '/')) != NULL || (p1 = strrchr(argv[0], '\\')) != NULL ) {
			char *pwd = NULL; // Path working directory
			int n = 0;

			SERVER_NAME = ++p1;
			n = p1 - argv[0]; // Calc dir name len
			pwd = safestrncpy((char *)malloc(n + 1), argv[0], n);
			if( chdir(pwd) != 0 )
				ShowError("Couldn't change working directory to %s for %s, runtime will probably fail", pwd, SERVER_NAME);
			free(pwd);
		}

		arg_c = argc;
		arg_v = argv;

	}

	malloc_init(); // Needed for Show* in display_title() [FlavioJS]

#ifdef MINICORE // Minimalist Core
	display_title();
	usercheck();
	do_init(argc,argv);
	do_final();
#else// not MINICORE
	set_server_type();
	display_title();
	usercheck();

	Sql_init();
	db_init();
	signals_init();

#ifdef _WIN32
	cevents_init();
#endif

	timer_init();
	socket_init();

	do_init(argc,argv);

	// Main runtime cycle
	while (runflag != CORE_ST_STOP) {
		int next = do_timer(gettick_nocache());

		do_sockets(next);
	}

	do_final();

	timer_final();
	socket_final();
	db_final();
#endif

	malloc_final();

	return 0;
}
