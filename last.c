/*
 * last.c	Re-implementation of the 'last' command, this time
 *		for Linux. Yes I know there is BSD last, but I
 *		just felt like writing this. No thanks :-).
 *
 * Author:	Miquel van Smoorenburg, miquels@drinkel.nl.mugnet.org
 *
 * Version:	1.0  08-12-1992
 *		- Initial version
 *
 *		1.1  15-12-1992
 *		- Fixed some bugs.
 *
 *		1.2  31-01-1993
 *		- Some little changes. (lastb, begintime)
 *		- Added hack for the bad wtmp logging that the
 *		  new telnetd suffers from (only logs ut_line and ut_time).
 *		
 *		1.3  24-03-1993
 *		- Added detection of re-login on a terminal
 *		  without logging out (by exec login newuser).
 *
 *		1.4 26-04-1993
 *		- Changed main engine to use BSD style wtmp file.
 *		  This can still read wtmp files made by my SysV init,
 *		  because those records are both SysV and BSD compatible.
 *		  The SysV style code is still in (but #ifdeffed out)
 *		  to show how it _should_ be done.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <utmp.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

char *Version = "@(#) last 1.3 24-03-93 MvS";

#define BSD	1	/* Also read BSD style WTMP files. */

/* Double linked list of struct utmp's */
struct utmplist {
  struct utmp ut;
  struct utmplist *next;
  struct utmplist *prev;
};
struct utmplist *utmplist = NULL;

/* Types of listing */
#define R_CRASH		1 /* No logout record, system boot in between */
#define R_DOWN		2 /* System brought down in decent way */
#define R_NORMAL	3 /* Normal */
#define R_NOW		4 /* Still logged in */

/* Global variables */
int maxrecs = 0;	/* Maximum number of records to list. */
int recsdone = 0;	/* Number of records listed */
int showhost = 0;	/* Show hostname too? */
char **show = NULL;	/* What do they want us to show */
char *ufile;		/* Filename of this file */
FILE *fp;		/* Filepointer of wtmp file */
static char lastdate[32]; /* Last date we've seen */

/* Try to be smart about the location of the BTMP file */
#ifndef BTMP_FILE
#define BTMP_FILE getbtmp()
char *getbtmp()
{
  static char btmp[128];
  char *p;

  strcpy(btmp, WTMP_FILE);
  if ((p = strrchr(btmp, '/')) == NULL)
	p = btmp;
  else
	p++;
  *p = 0;
  strcat(btmp, "btmp");
  return(btmp);
}
#endif

/* SIGINT handler */
void int_handler()
{
  printf("Interrupted %s\n", lastdate);
  exit(1);
}

/* SIGQUIT handler */
void quit_handler()
{
  printf("Interrupted %s\n", lastdate);
  signal(SIGQUIT, quit_handler);
}

/* Get the basename of a filename */
char *basename(char *s)
{
  char *p;

  if ((p = strrchr(s, '/')) != NULL)
	p++;
  else
	p = s;
  return(p);
}


/* Show one line of information on screen */
int list(struct utmp *p, time_t t, int what)
{
  char logintime[32];
  char logouttime[32];
  char length[32];
  time_t secs;
  int mins, hours, days;
  char **walk;

  /* uucp and ftp have special-type entries */
  if (strncmp(p->ut_line, "ftp", 3) == 0)  p->ut_line[3] = 0;
  if (strncmp(p->ut_line, "uucp", 4) == 0) p->ut_line[4] = 0;

  /* Is this something we wanna show? */
  if (show) {
	for(walk = show; *walk; walk++) {
		if (strncmp(p->ut_name, *walk, 8) == 0 ||
		    strncmp(p->ut_line, *walk, 12) == 0 ||
		    strncmp(p->ut_line + 3, *walk, 9) == 0) break;
	}
	if (*walk == NULL) return(0);
  }

  /* Calculate times */
  strcpy(logintime, ctime(&p->ut_time));
  logintime[16] = 0;
  strcpy(lastdate, logintime);
  strcpy(logouttime, ctime(&t) + 11);
  logouttime[5] = 0;
  secs = t - p->ut_time;
  mins   = (secs / 60) % 60;
  hours = (secs / 3600) % 24;
  days  = secs / 86400;
  if (days)
	sprintf(length, "(%d+%02d:%02d)", days, hours, mins);
  else
	sprintf(length, " (%02d:%02d)", hours, mins);

  switch(what) {
	case R_CRASH:
		sprintf(logouttime, "crash");
		break;
	case R_DOWN:
		sprintf(logouttime, "down ");
		break;
	case R_NOW:
		sprintf(length, "(still logged in)");
		break;
	case R_NORMAL:
		break;
  }

  if (showhost)
	printf("%-8.8s %-12.12s %-14.14s %s - %s %s\n", p->ut_name, p->ut_line,
		p->ut_host, logintime, logouttime, length);
  else
	printf("%-8.8s %-12.12s %s - %s %s\n", p->ut_name, p->ut_line,
		logintime, logouttime, length);

  recsdone++;
  if (maxrecs && recsdone >= maxrecs) return(1);
  return(0);
}

/* show usage */
void usage(char *s)
{
  fprintf(stderr, "Usage: %s [-num] [-R] [username..] [tty..]\n", s);
  exit(1);
}

int main(int argc, char **argv)
{
  struct utmp ut;	/* Current utmp entry */
  struct utmplist *p;	/* Pointer into utmplist */
  time_t lastboot = 0;  /* Last boottime */
  time_t lastdown;	/* Last downtime */
  time_t begintime;	/* When wtmp begins */
  int whydown = 0;	/* Why we went down: crash or shutdown */
  int x, y;		/* Scratch */
  char *progname;	/* Name of this program */
  struct stat st;	/* To stat the [uw]tmp file */
  int quit = 0;		/* Flag */
  int lastb = 0;	/* Is this 'lastb' ? */
#if BSD
  int down;		/* Down flag */
#endif

  progname = basename(argv[0]);

  /* Check the arguments */
  for(y = 1; y < argc; y++) {
	if (argv[y][0] == '-') {
		if (argv[y][1] == 0) usage(argv[0]);
		if ((x = atoi(argv[y] + 1)) <= 0) {
		    if (argv[y][2] != 0) usage(progname);
		    switch(argv[y][1]) {
			case 'R':
				showhost++;
				break;
			default:
				usage(progname);
				break;
		    }
		} else
			maxrecs = x;
	} else
		break;
  }
  if (y < argc) show = argv + y;

  /* Which file do we want to read? */
  if (strcmp(progname, "lastb") == 0) {
	ufile = BTMP_FILE;
	lastb = 1;
  } else
	ufile = WTMP_FILE;
  time(&lastdown);

  /* Fill in 'lastdate' */
  strcpy(lastdate, ctime(&lastdown));
  lastdate[16] = 0;

  /* Install signal handlers */
  signal(SIGINT, int_handler);
  signal(SIGQUIT, quit_handler);

  /* Open the utmp file */
  if ((fp = fopen(ufile, "r")) == NULL) {
	fprintf(stderr, "%s: %s: %s\n", progname, ufile, sys_errlist[errno]);
	exit(1);
  }
  
  /* Read first struture to capture the time field */
  if (fread(&ut, sizeof(struct utmp), 1, fp) == 1)
	begintime = ut.ut_time;
  else {
  	fstat(fileno(fp), &st);
	begintime = st.st_ctime;
  }

  /* Go to end of file minus one structure */
  fseek(fp, -1L * sizeof(struct utmp), SEEK_END);

  /* Read struct after struct */
  while(!quit && fread(&ut, sizeof(struct utmp), 1, fp)) {
	if (lastb)
		quit = list(&ut, ut.ut_time, R_NORMAL);
	else {
#if BSD
	  /* BSD does things quite different. Try to be
	   * smart and accept both BSD and SysV records.
	   */
	  down = 0;
	  if (strncmp(ut.ut_line, "~", 1) == 0) {
		/* Halt/reboot/shutdown etc */
		if (strncmp(ut.ut_user, "shutdown", 8) == 0) {
			lastdown = ut.ut_time;
			down = 1;
		}
		if (strncmp(ut.ut_user, "reboot", 6) == 0) {
			strcpy(ut.ut_line, "system boot");
			quit = list(&ut, lastdown, R_NORMAL);
			lastdown = ut.ut_time;
			down = 1;
		}
		if (down) {
			lastboot = ut.ut_time;
			whydown = (ut.ut_type == RUN_LVL) ? R_DOWN : R_CRASH;

			/* Delete the list; it's meaningless now */
			for(p = utmplist; p; p = p->next) free(p);
			utmplist = NULL;
		}
	  } else {
		/* Allright, is this a login or a logout? */
		if (ut.ut_name[0] != 0 && strcmp(ut.ut_name, "LOGIN")) {
			/* This is a login */
			/* Walk through list to see when logged out */
			for(p = utmplist; p; p = p->next)
				if (strncmp(p->ut.ut_line, ut.ut_line, 12) == 0)
				{
					/* Show it */
					quit = list(&ut, p->ut.ut_time,
							R_NORMAL);
					/* Delete it from the list */
					if (p->next) p->next->prev = p->prev;
					if (p->prev)
						p->prev->next = p->next;
					else
						utmplist = p->next;
					free( p );
					break;
				}
			/* Not found? Then crashed, down or still logged in */
			if (p == NULL)
				if (lastboot == 0)
					quit = list(&ut, time(NULL), R_NOW);
				else
					quit = list(&ut, lastboot, whydown);
		}
		/* Get some memory */
		if ((p = malloc(sizeof(struct utmplist))) == NULL) {
			fprintf(stderr, "%s: out of memory\n",
				progname);
			exit(1);
		}
		/* Fill in structure */
		memcpy(&p->ut, &ut, sizeof(struct utmp));
		p->next  = utmplist;
		p->prev  = NULL;
		if (utmplist) utmplist->prev = p;
		utmplist = p;
	  }
#else
	  /* This is the SysV way (the _right_ way!) */
	  switch (ut.ut_type) {
		case RUN_LVL:   /* Might be a shutdown record */
			if (strncmp(ut.ut_user, "shutdown", 8) != 0)
				break;
			lastdown = ut.ut_time;
		case BOOT_TIME: /* Rebooted, processes might have crashed */
			if (ut.ut_type == BOOT_TIME) {
				sprintf(ut.ut_user, "reboot");
				sprintf(ut.ut_line, "system boot");
				quit = list(&ut, lastdown, R_NORMAL);
				lastdown = ut.ut_time;
			}
			lastboot = ut.ut_time;
			whydown = (ut.ut_type == RUN_LVL) ? R_DOWN : R_CRASH;

			/* Delete the list; it's meaningless now */
			for(p = utmplist; p; p = p->next) free(p);
			utmplist = NULL;
			break;
		case USER_PROCESS: /* Someone logged in */
			/* Walk through list to see when logged out */
			for(p = utmplist; p; p = p->next)
				if ((p->ut.ut_pid == ut.ut_pid &&
				    p->ut.ut_type == DEAD_PROCESS)
				{
					/* Show it */
					quit = list(&ut, p->ut.ut_time,
							R_NORMAL);
					/* Delete it from the list */
					if (p->next) p->next->prev = p->prev;
					if (p->prev)
						p->prev->next = p->next;
					else
						utmplist = p->next;
					free( p );
					break;
				}
			/* Not found? Then crashed, down or still logged in */
			if (p == NULL)
				if (lastboot == 0)
					quit = list(&ut, time(NULL), R_NOW);
				else
					quit = list(&ut, lastboot, whydown);
			break;
		case DEAD_PROCESS: /* Process died; remember it */
#if DEAD_PROCESS != 0
		case 0: /* Telnetd just sets to 0 */
#endif
			/* Get some memory */
			if ((p = malloc(sizeof(struct utmplist))) == NULL) {
				fprintf(stderr, "%s: out of memory\n",
					progname);
				exit(1);
			}
			/* Fill in structure */
			memcpy(&p->ut, &ut, sizeof(struct utmp));
			p->next  = utmplist;
			p->prev  = NULL;
			if (utmplist) utmplist->prev = p;
			utmplist = p;
			break;
		default:
			break;
	}
#endif /* BSD or SysV */
      }
      /* Position the file pointer 2 structures back */
      if (fseek(fp, -2 * sizeof(struct utmp), SEEK_CUR) < 0) break;
  }
  printf("\n%s begins %s", basename(ufile), ctime(&begintime));

  fclose(fp);
  /* Should we free memory here? Nah. */
  return(0);
}
