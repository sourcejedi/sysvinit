/*
 * Init		A System-V Init Clone.
 *
 * Usage:	/etc/init
 *		     init [0123456SsQqAaBbCc]
 *		  telinit [0123456SsQqAaBbCc]
 *
 * Author:	Written by Miquel van Smoorenburg, Februari 1992.
 * 		Conforms to all standards I know of.
 *
 * Copyright:	(C) Miquel van Smoorenburg, miquels@drinkel.nl.mugnet.org
 *
 *		Permission for redistribution granted, if
 *		  1) You do not pretend you have written it
 *		  2) You keep this header intact.
 *		  3) You do not sell it or make any profit of it.
 *
 * Version:	1.0, 01-Feb-92
 *		- Initial version.
 *
 *		[ ... ]
 *
 *		2.4, 15-May-93
 *		See the file 'History' for changes.
 */

/* Standard configuration */
#define SYSLOG	   1				/* Use syslog for messages */
#define SHELL	   "/bin/sh"			/* Shell to run commands */
#define SOME_USER  0				/* 2 for password in SU mode */
#define SU	   "/bin/su"			/* Single-user shell */

/* Debug and test modes */
#define DEBUG	   0				/* Debug code off */
#define TEST	   0				/* Test mode off */
#define CLEAN_UTMP 1				/* Do garbage collect in utmp*/
#undef  ROOTFS	   "/dev/hda1"			/* Root fs to use */

/* Some constants */
#define INITLVL	   "/etc/initrunlvl"		/* Used with telinit */
#define INITPID	   1				/* pid of first process */
#define CONSOLE    "/dev/console"		/* For diagnostics & input */
#define SUTTY	   "/dev/tty1"			/* Terminal for SU shell */
#define INITTAB    "/etc/inittab"		/* the inittab to use */
#define PWRSTAT    "/etc/powerstatus"		/* Why did we get SIGPWR? */

/* Failsafe configuration */
#define MAXSPAWN   10				/* Max times respawned in.. */
#define TESTTIME   120				/* this much seconds */
#define SLEEPTIME  300				/* Disable time */

#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <utmp.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#if SYSLOG
#  include <sys/syslog.h>
#endif

/* To keep this thing less system dependant, check some things */
#ifndef UTMP
#  ifdef UTMP_FILE
#    define UTMP UTMP_FILE		/* The real name */
#    define WTMP WTMP_FILE
#  else
#    define UTMP "/etc/utmp"
#    define WTMP "/usr/adm/wtmp"
#  endif
#endif
#ifndef NO_PROCESS
#  define NO_PROCESS 0
#endif
#ifndef _NSIG
#  define _NSIG NSIG
#endif

/* Linux does not have SIGPWR (yet) so let's define it here. */
#ifndef SIGPWR
#  define SIGPWR 30
#endif

#if TEST
#  undef UTMP
#  undef WTMP
#  undef INITLVL
#  undef CONSOLE
#  undef INITTAB
#  undef INITPID
#  undef SUTTY
#  define UTMP		"/project/init/utmp"
#  define WTMP		"/project/init/wtmp"
#  define INITLVL 	"/project/init/initrunlvl"
#  define CONSOLE	"/dev/tty5"
#  define INITTAB	"/project/init/inittab.tst"
#  define INITPID	"/project/init/initpid"
#  define SUTTY		"/dev/tty5"
#endif

/* Version information */
char *Version = "@(#) init 2.4 22-04-93 MvS";
char *bootmsg = "version 2.4 booting";

/* Information about a process in the in-core inittab */
typedef struct _child_ {
  int flags;			/* Status of this entry */
  int exstat;			/* Exit status of process */
  int pid;			/* Pid of this process */
  time_t tm;			/* When respawned last */
  int count;			/* Times respawned in the last 2 minutes */
  char id[4];			/* Inittab id (must be unique) */
  char rlevel[12];		/* run levels */
  int action;			/* what to do (see list below) */
  char process[128];		/* The command line */
  struct _child_ *new;		/* New entry (after inittab re-read) */
  struct _child_ *next;		/* For the linked list */
} CHILD;

CHILD *family = NULL;		/* The linked list of all entries */
CHILD *newFamily = NULL;	/* The list after inittab re-read */

char runlevel = 'S';		/* The current run level */
char lastlevel = 0;		/* The previous run level */
int got_hup = 0;		/* Set if we received the SIGHUP signal */
int got_alrm = 0;		/* Set if we received the SIGALRM signal */
int got_pwr = 0;		/* Set if we received the SIGPWR signal */
int got_int = 0;		/* Set if we received the SIGINT signal */
int got_chld = 0;		/* Set if we received the SIGCHLD signal */
int got_cont = 0;		/* Set if we received the SIGCONT signal */
int wroteReboot	= 1;		/* Set when we wrote the reboot record */
int sltime = 20;		/* Sleep time between TERM and KILL */

/* Two forward declarations */
void Wtmp(char *user, char *id, int pid, int type);
void WtmpOnly(char *user, char *id, int pid, int type, char *line);

/* Actions to be taken by init */
#define RESPAWN			1
#define WAIT			2
#define ONCE			3
#define	BOOT			4
#define BOOTWAIT		5
#define POWERFAIL		6
#define POWERWAIT		7
#define POWEROKWAIT		8
#define CTRLALTDEL		9
#define OFF		       10
#define	ONDEMAND	       11
#define	INITDEFAULT	       12
#define SYSINIT		       13

/* Macro to see if this is a special action */
#define ISPOWER(i) ((i) == POWERWAIT || (i) == POWERFAIL || \
		    (i) == POWEROKWAIT || (i) == CTRLALTDEL)

/* Values for the 'flags' field */
#define RUNNING			2	/* Process is still running */
#define KILLME			4	/* Kill this process */
#define DEMAND			8	/* "runlevels" a b c */
#define FAILING			16	/* process respawns rapidly */
#define WAITING			32	/* We're waiting for this process */
#define ZOMBIE			64	/* This process is already dead */
#define EXECUTED		128	/* Set if spawned once or more times */

/* ascii values for the `action' field. */
struct actions {
  char *name;
  int act;
} actions[] = {
  { "respawn", 	   RESPAWN	},
  { "wait",	   WAIT		},
  { "once",	   ONCE		},
  { "boot",	   BOOT		},
  { "bootwait",	   BOOTWAIT	},
  { "powerfail",   POWERFAIL	},
  { "powerwait",   POWERWAIT	},
  { "powerokwait", POWEROKWAIT	},
  { "ctrlaltdel",  CTRLALTDEL	},
  { "off",	   OFF		},
  { "ondemand",	   ONDEMAND	},
  { "initdefault", INITDEFAULT	},
  { "sysinit",	   SYSINIT	},
  { NULL,	   0		},
};

/*
 * The SIGHUP handler
 */
void hup_handler()
{
  signal(SIGHUP, hup_handler);
  got_hup = 1;
}

#ifdef SIGPWR
/*
 * The SIGPWR handler
 */
void pwr_handler()
{
  signal(SIGPWR, pwr_handler);
  got_pwr = 1;
}
#endif

/*
 * The SIGINT handler
 */
void int_handler()
{
  signal(SIGINT, int_handler);
  got_int = 1;
}

/*
 * The SIGCHLD handler
 */
void chld_handler()
{
  CHILD *ch;
  int pid, st;

  /* Find out which process(es) this was (were) */
#if 0
  while((pid = wait4(WAIT_ANY, &st, WNOHANG, NULL)) > 0) {
#else
  while((pid = waitpid(-1, &st, WNOHANG)) > 0) {
#endif
	for( ch = family; ch; ch = ch->next )
		if ( ch->pid == pid && (ch->flags & RUNNING) ) {
			got_chld = 1;
			ch->exstat = st;
			ch->flags |= ZOMBIE;
			break;
		}
  }
  signal(SIGCHLD, chld_handler);
}

/*
 * The SIGALRM handler
 */
void alrm_handler()
{
  signal(SIGALRM, alrm_handler);
  got_alrm = 1;
}

/*
 * Linux ignores all signals sent to init when the
 * SIG_DFL handler is installed. Therefore we must catch SIGTSTP
 * and SIGCONT, or else they won't work....
 *
 * The SIGCONT handler
 */
void cont_handler()
{
  signal(SIGCONT, cont_handler);
  got_cont = 1;
}

/*
 * The SIGSTOP & SIGTSTP handler
 */
void stop_handler()
{
  got_cont = 0;
  while(!got_cont) pause();
  got_cont = 0;
  signal(SIGSTOP, stop_handler);
  signal(SIGTSTP, stop_handler);
}

/* Set terminal settings to reasonable defaults */
void SetTerm(int fd)
{
  struct termios tty;

  /* Get old settings */
  ioctl(fd, TCGETS, &tty);

  /* Set pre and post processing */
  tty.c_iflag = IGNPAR|ICRNL|IXON|IXANY;
  tty.c_oflag = OPOST|ONLCR;
  tty.c_lflag = ISIG|ICANON|ECHO|ECHOCTL|ECHOPRT|ECHOKE;
  tty.c_cflag = HUPCL|CS8|B9600;
  /* FIXME: should we set C_LINE here? */

  /* Set the most important characters */
  tty.c_cc[VINTR]  = 3;
  tty.c_cc[VQUIT]  = 28;
  tty.c_cc[VERASE] = 127;
  tty.c_cc[VKILL]  = 24;
  tty.c_cc[VEOF]   = 4;
  tty.c_cc[VTIME]  = 0;
  tty.c_cc[VMIN]   = 1;
  tty.c_cc[VSTART] = 17;
  tty.c_cc[VSTOP]  = 19;
  tty.c_cc[VSUSP]  = 26;

  ioctl(fd, TCSETS, &tty);
}

/* Print to the system console */
void print(char *s, va_list va_alist)
{
  char buf[256];
  int fd;

  vsprintf(buf, s, va_alist);
  if ((fd = open(CONSOLE, O_WRONLY|O_NOCTTY|O_NDELAY)) >= 0) {
	write(fd, buf, strlen(buf));
	close(fd);
  }
}

/* Sleep a number of seconds */
void VerboseSleep(int sec, int verbose)
{
  int oldAlrm;			/* Previous value of timer */
  int f;			/* Counter */
  int ga = got_alrm;		/* Remember got_alrm flag */

#if SYSLOG
  verbose = 0;
#endif

  /* Save old alarm time */
  oldAlrm = alarm(0);

  /* Wait 'sec' seconds */
  if (verbose) print("\r", 0);
  for(f = 0; f < sec; f++) {
	if (verbose) {
		print(".", 0);
	}
	got_alrm = 0;
	alarm(1);
	while(got_alrm == 0) pause();
  }
  if (verbose) print("\r\n", 0);

  /* Reset old values of got_alrm flag and the timer */
  got_alrm = ga;
  if (oldAlrm) alarm(oldAlrm - sec > 0 ? oldAlrm - sec : 1);
}

/* Say something to the user */
void Say(char *s, ...)
{
  va_list va_alist;
  char buf[256];

  va_start(va_alist, s);

#if SYSLOG
  vsprintf(buf, s, va_alist);
  /*
   * Yeach! We have to call closelog() and openlog() every time, since
   * a change of runlevels could have killed syslogd...
   */
  openlog("init", LOG_PERROR|LOG_PID|LOG_CONS|LOG_NOWAIT, LOG_DAEMON);
  syslog(LOG_INFO, buf);
  closelog();
#else
  sprintf(buf, "\rINIT: %s\r\n", s);
  print(buf, va_alist);
#endif
  va_end(va_alist);
}

/* Warn the user */
void Warning(char *s, ...)
{
  va_list va_alist;
  char buf[256];

  va_start(va_alist, s);
#if SYSLOG
  openlog("init", LOG_PERROR|LOG_PID|LOG_CONS|LOG_NOWAIT, LOG_DAEMON);
  vsprintf(buf, s, va_alist);
  syslog(LOG_INFO, buf);
  closelog();
#else
  sprintf(buf, "\rINIT: Warning: %s\r\n", s);
  print(buf, va_alist);
#endif
  va_end(va_alist);
}

#if DEBUG
/* Warn the user */
void Panic(char *s, ...)
{
  va_list va_alist;
  char buf[256];

  va_start(va_alist, s);
#if SYSLOG
  openlog("init", LOG_PERROR|LOG_PID|LOG_CONS|LOG_NOWAIT, LOG_DAEMON);
  vsprintf(buf, s, va_alist);
  syslog(LOG_ALERT, buf);
  closelog();
#else
  sprintf(buf, "\rINIT: PANIC: %s\r\n", s);
  print(buf, va_alist);
#endif
  va_end(va_alist);
  exit(1);
}
#endif

/* See if one character of s2 is in s1 */
int any(char *s1, char *s2)
{
  while(*s2)
	if (strchr(s1, *s2++) != NULL) return(1);
  return(0);
}

/*
 * Fork and execute.
 */
int Spawn(CHILD *ch)
{
  char *args[16];		/* Argv array */
  char buf[136];		/* Line buffer */
  int f, pid;			/* Scratch variables */
  char *ptr;			/* Ditto */
  time_t t;			/* System time */
  int oldAlarm;			/* Previous alarm value */
  char *proc = ch->process;	/* Command line */

  /* Skip '+' if it's there */
  if (proc[0] == '+') proc++;

  ch->flags |= EXECUTED;

  if (ch->action == RESPAWN || ch->action == ONDEMAND) {
	/* Is the date stamp from less than 2 minutes ago? */
	time(&t);
	if (ch->tm + TESTTIME > t)
		ch->count++;
	else
		ch->count = 0;
	ch->tm = t;

	/* Do we try to respawn too fast? */
	if (ch->count >= MAXSPAWN) {

	  Warning("Id \"%s\" respawning too fast: disabled for %d minutes",
			ch->id, SLEEPTIME / 60);
	  ch->flags &= ~RUNNING;
	  ch->flags |= FAILING;

	  /* Remember the time we stopped */
	  ch->tm = t;

	  /* Try again in 5 minutes */
	  oldAlarm = alarm(0);
	  if (oldAlarm > SLEEPTIME || oldAlarm <= 0) oldAlarm = SLEEPTIME;
	  alarm(oldAlarm);
	  return(-1);
	}
  }

  /* See if we need to fire off a shell for this command */
  if (any(proc, "~`!$^&*()=|\\{}[];\"'<>?")) {
  	/* Give command line to shell */
  	args[1] = SHELL;
  	args[2] = "-c";
  	strcpy(buf, "exec ");
  	strcat(buf, proc);
  	args[3] = buf;
  	args[4] = NULL;
  } else {
	/* Split up command line arguments */
  	strcpy(buf, proc);
  	ptr = buf;
  	for(f = 1; f < 15; f++) {
  		/* Skip white space */
  		while(*ptr == ' ' || *ptr == '\t') ptr++;
  		args[f] = ptr;
  		
  		/* Skip this `word' */
  		while(*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '#')
  			ptr++;
  		
  		/* If end-of-line, break */	
  		if (*ptr == '#' || *ptr == 0) {
  			f++;
  			*ptr = 0;
  			break;
  		}
  		/* End word with \0 and continue */
  		*ptr++ = 0;
  	}
  	args[f] = NULL;
  }
  args[0] = args[1];
  while(1) {
	if ((pid = fork()) == 0) {
#if DEBUG && !SYSLOG
		pid = open("/dev/null", O_RDONLY);
		close(pid);
		if (pid != 0)
			Warning("some fd is still open (%d)", pid);
#endif
  		/* The single user entry needs to talk to the console */
  		if (strcmp(ch->id, "~~") == 0) {
			setsid();
			f = open(SUTTY, O_RDWR);
  			dup(f);
  			dup(f);
  			setuid(SOME_USER); /* Force su to ask for a password */
			/* Set ioctl settings to default ones */
			SetTerm(0);
  		} else {
			f = open(CONSOLE, O_RDWR|O_NOCTTY);
			dup(f);
			dup(f);
			setsid();
		}
  		/* Reset all the signals */
  		for(f = 1; f < _NSIG; f++) signal(f, SIG_DFL);
  		execvp(args[1], args + 1);
		/* Is this a bug in execvp? It does _not_ execute shell
		 * scripts (/etc/rc !), so we try again with 'sh -c exec ...'
		 */
		if (errno == ENOEXEC) {
  			args[1] = SHELL;
  			args[2] = "-c";
  			strcpy(buf, "exec ");
  			strcat(buf, proc);
  			args[3] = buf;
  			args[4] = NULL;
			execvp(args[1], args + 1);
		}
  		Warning("cannot execute \"%s\"", args[1]);
  		exit(1);
  	}
	if (pid == -1) {
		Warning("cannot fork, retry..", NULL, NULL);
		VerboseSleep(5, 1);
		continue;
	}
	return(pid);
  }
}

/*
 * Start a child running!
 */
void StartUp(ch)
CHILD *ch;
{
#if DEBUG
  Say("Starting id %s", ch->id);
#endif

  /* See if it's disabled */
  if (ch->flags & FAILING) return;

  switch(ch->action) {

	case SYSINIT:
  	case BOOTWAIT:
	case WAIT:
	case POWERWAIT:
	case POWEROKWAIT:
	case CTRLALTDEL:
		if (!(ch->flags & EXECUTED)) ch->flags |= WAITING;
	case ONCE:
	case BOOT:
	case POWERFAIL:
		if (ch->flags & EXECUTED) break;
	case ONDEMAND:
	case RESPAWN:
  		ch->flags |= RUNNING;
  		if ((ch->pid = Spawn(ch)) < 0) break;
		/* Do NOT log if process field starts with '+' */
  		if (ch->process[0] != '+')
			Wtmp("", ch->id, ch->pid, INIT_PROCESS);
  		break;
  }
}

/* My version of strtok(3) */
char *getPart(char *str, int tok)
{
  static char *s;
  char *p, *q;

  if (str != NULL) s = str;
  if (s == NULL || *s == 0) return(NULL);
  q = p = s;
  while(*p != tok && *p) p++;
  if (*p == tok) *p++ = 0;
  s = p;
  return(q);
}

/* Read the inittab file. */
void ReadItab(void)
{
  FILE *fp;			/* The INITTAB file */
  char buf[256];		/* Line buffer */
  char *id, *rlevel,
       *action, *process;	/* Fields of a line */
  CHILD *ch, *old, *i;		/* Pointers to CHILD structure */
  CHILD *head = NULL;		/* Head of linked list */
  int lineNo = 0;		/* Line number in INITTAB file */
  int actionNo;			/* Decoded action field */
  int f;			/* Counter */
  int round;			/* round 0 for SIGTERM, round 1 for SIGKILL */
  int foundOne = 0;		/* No killing no sleep */
  int talk;			/* Talk to the user */
  int done = 0;			/* Ready yet? */
  int killEmAll = 0;		/* Metallica's first album :-) */

#if DEBUG
  if (newFamily != NULL) Panic("newFamily != NULL");
#endif

  /* Open INITTAB */
  if ((fp = fopen(INITTAB, "r")) == NULL)
	Warning("No inittab file found");

  /* Read INITTAB line by line */
  while(!done) {
	/* Add single user shell entry as last one. */
	if (fp == NULL || fgets(buf, 255, fp) == NULL) {
		sprintf(buf, "~~:S:wait:%s\n", SU);
		done = 1;
	}
	lineNo++;
	/* Skip comments and empty lines */
	if (buf[0] == '#' || buf[0] == '\n') continue;

	/* Decode the fields */
	id =      getPart(buf,  ':');
	rlevel =  getPart(NULL, ':');
	action =  getPart(NULL, ':');
	process = getPart(NULL, '\n');
	if (!id || !rlevel || !action || !process ||
	    strlen(id) > 2 || strlen(rlevel) > 11 ||
	    strlen(process) > 127) {
		Warning("line %d of %s is incorrect", lineNo, INITTAB);
#if DEBUG
		Say("%s:%s:%s:%s", id, rlevel, action, process);
#endif
		continue;
	}
  
	/* Decode the "action" field */
	actionNo = -1;
	for(f = 0; actions[f].name; f++)
		if (strcasecmp(action, actions[f].name) == 0) {
			actionNo = actions[f].act;
			break;
		}
	if (actionNo == -1) {
		Warning("Invalid action field in line %d of %s",
			lineNo, INITTAB);
		continue;
	}

	/* See if the id field is unique */
	for(old = newFamily; old; old = old->next) {
		if(strcmp(old->id, id) == 0 && strcmp(id, "~~")) {
			Warning("dupicate ID field on line %d in %s",
				lineNo, INITTAB);
			break;
		}
	}
	if (old) continue;

	/* Allocate a CHILD structure */
	if ((ch = malloc(sizeof(CHILD))) == NULL) {
		Warning("out of memory");
		continue;
	}
	memset(ch, 0, sizeof(CHILD));

	/* And fill it in. */
	ch->action = actionNo;
	strncpy(ch->id, id, 3);
	strncpy(ch->process, process, 127);
	if (rlevel[0]) {
		for(f = 0; f < 11 && rlevel[f]; f++) {
			ch->rlevel[f] = rlevel[f];
			if (ch->rlevel[f] == 's') ch->rlevel[f] = 'S';
		}
		strncpy(ch->rlevel, rlevel, 11);
	} else {
		strcpy(ch->rlevel, "0123456789");
		if (ISPOWER(ch->action))
			strcpy(ch->rlevel, "S0123456789");
	}
	/*
	 * We have the fake runlevel '#' for SYSINIT  and
	 * '*' for BOOT and BOOTWAIT.
	 */
	if (ch->action == SYSINIT) strcpy(ch->rlevel, "#");
	if (ch->action == BOOT || ch->action == BOOTWAIT)
		strcpy(ch->rlevel, "*");

	/* Now add it to the linked list. Special for powerfail. */
	if (ISPOWER(ch->action)) {

		/* Disable by default */
		ch->flags |= EXECUTED;

		/* Tricky: insert at the front of the list.. */
		old = NULL;
		for(i = newFamily; i; i = i->next) {
			if (!ISPOWER(i->action)) break;
			old = i;
		}
		/* Now add after entry "old" */
		if (old) {
			ch->next = i;
			old->next = ch;
			if (i == NULL) head = ch;
		} else {
			ch->next = newFamily;
			newFamily = ch;
			if (ch->next == NULL) head = ch;
		}
	} else {
		/* Just add at end of the list */
		ch->next = NULL;
		if (head)
			head->next = ch;
		else
			newFamily = ch;
		head = ch;
	}

	/* Walk through the old list comparing id fields */
	for(old = family; old; old = old->next)
		if (strcmp(old->id, ch->id) == 0) {
			old->new = ch;
			break;
		}
  }
  /* We're done. */
  if (fp ) fclose(fp);

  /*
   * Loop through the list of children, and see if they need to
   * be killed. 
   */

#if !TEST
  if (isdigit(lastlevel) && runlevel == 'S') killEmAll++;
#endif

  for(round = 0; round < 2; round++) {
    talk = 1;
    for(ch = family; ch; ch = ch->next) {
	ch->flags &= ~KILLME;

	/* Is this line deleted? */
	if (ch->new == NULL) ch->flags |= KILLME;

	/* See if the entry changed. Yes: kill anyway */
	if (ch->new && (strcmp(ch->process, ch->new->process) ||
		ch->action != ch->new->action)) ch->flags |= KILLME;

	/* Now see if this entry belongs in this level */
	if (!killEmAll && runlevel != 'S' && (ch->flags & DEMAND)) break;

	/* Only BOOT processes may live in all levels */
	if (ch->action != BOOT &&
	    strchr(ch->rlevel, runlevel) == NULL)
		ch->flags |= KILLME;

	/* Now, if this process may live note so in the new list */
	if ((ch->flags & KILLME) == 0) {
		ch->new->flags  = ch->flags;
		ch->new->pid    = ch->pid;
		ch->new->exstat = ch->exstat;
		continue;
	}


	/* Is this process still around? */
	if ((ch->flags & RUNNING) == 0) {
		ch->flags &= ~KILLME;
		if (!killEmAll) continue;
	}
#if DEBUG
	Say("Killing \"%s\"", ch->process);
#endif
	switch(round) {
		case 0: /* Send TERM signal */
			if (talk)
				Say("Sending processes the TERM signal");
			if (killEmAll)
				kill(-1, SIGTERM);
			else
				kill(-(ch->pid), SIGTERM);
			foundOne = 1;
			break;
		case 1: /* Send KILL signal and collect status */
			if (talk)
				Say("Sending processes the KILL signal");
			if (killEmAll)
				kill(-1, SIGKILL);
			else
				kill(-(ch->pid), SIGKILL);
			break;
	}
	talk = 0;
	
	/* And process the next member of our family */
    }
    /* See if we have to wait 20 seconds */
    if (foundOne && round == 0) VerboseSleep(sltime, 1);
  }

  /* Now give all processes the chance to die and collect exit statuses */
  /* FIXME: how to give away time slice?? */
  if (foundOne) VerboseSleep(1, 0);
  for(ch = family; ch; ch = ch->next)
	if (ch->flags & KILLME) {
		if (!(ch->flags & ZOMBIE))
		    Warning("Pid %d [id %s] seems to hang", ch->pid,
				ch->id);
		else {
		    if (ch->process[0] != '+')
		    	Wtmp("", ch->id, ch->pid, DEAD_PROCESS);
		    ch->flags &= ~RUNNING;
		}
	}

  /* Both rounds done; clean up the list. */
  signal(SIGCHLD, SIG_DFL);
  for(ch = family; ch; ch = ch->next) free(ch);
  family = newFamily;
  for(ch = family; ch; ch = ch->next) ch->new = NULL;
  newFamily = NULL;
  chld_handler();
  /* Delete the INITLVL file (if it exists) */
  unlink(INITLVL);
}

/*
 * Walk through the family list and start up children.
 * The entries that do not belong here at all are removed
 * from the list.
 */
void StartEmIfNeeded(void)
{
  CHILD *ch;		/* Pointer to child */
  int delete;		/* Delete this entry from list? */

  for(ch = family; ch; ch = ch->next) {

	/* Are we waiting for this process? Then quit here. */
	if (ch->flags & WAITING) break;

	/* Already running? OK, don't touch it */
	if (ch->flags & RUNNING) continue;

	/* See if we have to start it up */
	delete = 1;
	if (strchr(ch->rlevel, runlevel) ||
	    ((ch->flags & DEMAND) && !strchr("#*Ss", runlevel))) {
		StartUp(ch);
		delete = 0;
	}

	if (delete) {
		/* FIXME: is this OK? */
		ch->flags &= ~(RUNNING|WAITING);
		if (!ISPOWER(ch->action))
			ch->flags &= ~EXECUTED;
		ch->pid = 0;
	} else
		/* Do we have to wait for this process? */
		if (ch->flags & WAITING) break;
  }
  /* Done. */
}

/*
 * Ask the user on the console for a runlevel
 */
int AskRunLevel()
{
  int lvl = -1;
  char buf[8];
  int fd;


  fd = open(CONSOLE, O_RDWR|O_NOCTTY/*|O_NDELAY*/);
	/* FIXME as soon as O_NDELAY != O_NONBLOCK */
  if (fd < 0) return('S');
  /* Reset terminal settings */
  SetTerm(fd);

  while(!strchr("0123456789S", lvl)) {
  	write(fd, "\nEnter runlevel: ", 17);
	buf[0] = 0;
  	read(fd, buf, 8);
  	if (buf[0] != 0 && (buf[1] == '\r' || buf[1] == '\n')) lvl = buf[0];
	if (islower(lvl)) lvl = toupper(lvl);
  }
  close(fd);
  return(lvl);
}

/*
 * Search the INITTAB file for the 'initdefault' field, with the default
 * runlevel. If this fails, ask the user to supply a runlevel.
 */
int GetInitDefault(void)
{
  CHILD *ch;
  int lvl = -1;
  char *p;

  for(ch = family; ch; ch = ch->next)
	if (ch->action == INITDEFAULT) {
		p = ch->rlevel;
		while(*p) {
			if (*p > lvl) lvl = *p;
			p++;
		}
		break;
	}
  if (lvl > 0) {
	if (islower(lvl)) lvl = toupper(lvl);
	if (strchr("0123456789S", lvl)) return(lvl);
	Warning("Initdefault level '%c' is invalid", lvl);
  }
  return(AskRunLevel());
}


/*
 * We got signaled: read the new level from INITLVL
 */
int ReadLevel(void)
{
  unsigned char foo = 'X';		/* Contents of INITLVL */
  int st;				/* Sleep time */
  CHILD *ch;				/* Walk through list */
  FILE *fp;				/* File pointer for INITLVL */
  int ok = 0;

  if ((fp = fopen(INITLVL, "r")) == NULL) {
  	Warning("cannot open %s", INITLVL);
  	return(runlevel);
  }
  ok = fscanf(fp, "%c %d", &foo, &st);
  fclose(fp);

  if (islower(foo)) foo = toupper(foo);

  if (ok < 1 || ok > 2 || strchr("QS0123456789ABC", foo) == NULL) {
  	Warning("bad runlevel: %c", foo);
  	return(runlevel);
  }
  if (ok == 2) sltime = st;

  /* Be verbose 'bout it all */
  switch(foo) {
	case 'S':
  		Say("Going single user");
		break;
	case 'Q':
		Say("Re-reading inittab");
		break;
	case 'A':
	case 'B':
	case 'C':
		Say("Activating demand-procedures for '%c'", foo);
		break;
	default:
	  	Say("Going to runlevel: %c", foo);
  }

  if (foo == 'Q') return(runlevel);

  /* Check if this is a runlevel a, b or c */
  if (strchr("ABC", foo)) {
	if (runlevel == 'S') return(runlevel);

  	/* Start up those special tasks */
	for(ch = family; ch; ch = ch->next)
		if (strchr(ch->rlevel, foo) != NULL ||
		    strchr(ch->rlevel, tolower(foo)) != NULL) {
			ch->flags |= DEMAND;
#if DEBUG
			Say("Marking (%s) as ondemand, flags %d",
				ch->id, ch->flags);
#endif
		}
  	return(runlevel);
  }

  WtmpOnly("runlevel", "~~", runlevel, RUN_LVL, "~");
  return(foo);
}


/*
 * Log an event ONLY in the wtmp file (reboot, runlevel)
 */
void WtmpOnly(user, id, pid, type, line)
char *user;			/* name of user */
char *id;			/* inittab ID */
int pid;			/* PID of process */
int type;			/* TYPE of entry */
char *line;			/* Which line is this */
{
  int fd;
  struct utmp utmp;
  time_t t;

  if ((fd = open(WTMP, O_WRONLY|O_APPEND)) < 0) return;

  /* See if we need to write a boot record */
  if (wroteReboot == 0 && type != BOOT_TIME) {
  	WtmpOnly("reboot", "~~", 0, BOOT_TIME, "~");
	wroteReboot++;
  }

  /* Zero the fields */
  memset(&utmp, 0, sizeof(utmp));

  /* Enter new fields */
  time(&t);
  utmp.ut_time = t;
  utmp.ut_pid  = pid;
  utmp.ut_type = type;
  strncpy(utmp.ut_name, user, sizeof(utmp.ut_name));
  strncpy(utmp.ut_id  , id  , sizeof(utmp.ut_id  ));
  strncpy(utmp.ut_line, line, sizeof(utmp.ut_line));

  write(fd, (char *)&utmp, sizeof(utmp));
  close(fd);
}

/*
 * Log an event into the WTMP and UTMP files.
 */
void Wtmp(user, id, pid, type)
char *user;			/* name of user */
char *id;			/* inittab ID */
int pid;			/* PID of process */
int type;			/* TYPE of entry */
{
  struct utmp utmp;		/* UTMP/WTMP User Accounting */
  struct utmp tmp;		/* Scratch */
  struct utmp empty;		/* Empty utmp struct */
  int fd = -1;			/* File Descriptor for UTMP */
  int fd2;			/* File Descriptor for WTMP */
  int found = 0;		/* Was the record found in UTMP */
  int freeEntry = -1;		/* Was a free entry found during UTMP scan? */
  int lineno;			/* Offset into UTMP file */
  time_t t;			/* What's the time? */

  /* Zero the empty structure */
  memset(&empty, 0, sizeof(struct utmp));
  empty.ut_type = NO_PROCESS;

  /* First read the utmp entry for this process */
  if ((fd = open(UTMP, O_RDWR)) >= 0) {
	lineno = 0;
        while (read(fd, (char *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
		if (tmp.ut_pid == pid && tmp.ut_type != NO_PROCESS) {
			found = 1;
			memcpy(&utmp, &tmp, sizeof(utmp));
			if (type == DEAD_PROCESS) {
				/* Zero all entries with this pid */
				(void) lseek(fd, (long) lineno, SEEK_SET);
				write(fd, (char *)&empty, sizeof(struct utmp));
			} else
				break;
  		}
#if CLEAN_UTMP
		/* Let's check if this process still exists. If it
		 * doesn't, clear it's utmp entry.
		 * By putting a 'cu::respawn:/bin/sleep 60' in the
		 * inittab file /etc/utmp will be checked and
		 * cleared every minute.
		 */
		else if (tmp.ut_type != NO_PROCESS && tmp.ut_pid > 0) {
			if (kill(tmp.ut_pid, 0) < 0) {
				/* Zero the entry with this pid */
				(void) lseek(fd, (long) lineno, SEEK_SET);
				write(fd, (char *)&empty, sizeof(struct utmp));
			}
		}
#endif
		/* See if this is a free entry, save it for later */
		if (tmp.ut_pid == 0 || tmp.ut_type == 0)
			if (freeEntry < 0) freeEntry = lineno;
		lineno += sizeof(tmp);
	}
  }
  if (!found) { /* Enter some defaults */
	/* Zero the fields */
	memset(&utmp, 0, sizeof(utmp));

	/* Enter new fields */
	utmp.ut_pid  = pid;
	strncpy(utmp.ut_name, user, sizeof(utmp.ut_name));
	strncpy(utmp.ut_id  , id  , sizeof(utmp.ut_id  ));
	strcpy (utmp.ut_line, "");

	/* Where to write new utmp record */
	if (freeEntry >= 0)
		lseek(fd, (long) freeEntry, SEEK_SET);
  }

  /* Change the values of some fields */
  time(&t);
  utmp.ut_type = type;
  utmp.ut_time = t;

  /* Write the utmp record, if needed */
  if (fd >= 0)  {
  	/* DEAD_PROCESS has already been zeroed */
  	if (utmp.ut_type != DEAD_PROCESS)
		(void) write(fd, (char *) &utmp, sizeof(struct utmp));
	(void) close(fd);
  }

  /* Write the wtmp record */
  if ((fd2 = open(WTMP, O_WRONLY|O_APPEND)) >= 0) {
  	/* See if we need to write a boot record */
	if (wroteReboot == 0 && type != BOOT_TIME) {
  		WtmpOnly("reboot", "~~", 0, BOOT_TIME, "~");
		wroteReboot++;
	}
	/* Set ut_user to 0 if this is a logout record */
	if (utmp.ut_type == DEAD_PROCESS) utmp.ut_name[0] = 0;
	(void) write(fd2, (char *) &utmp, sizeof(struct utmp));
	(void) close(fd2);
  }
}

/*
 * This procedure is called after every signal (SIGHUP, SIGALRM..)
 *
 * Only clear the 'failing' flag if the process is sleeping
 * longer than 5 minutes, or inittab was read again due
 * to user interaction.
 */
void FailCheck()
{
  time_t t;		/* System time */
  CHILD *ch;		/* Pointer to child structure */
  time_t nxtAlrm = 0;	/* When to set next alarm */

  time(&t);

  for(ch = family; ch; ch = ch->next) {

	if (ch->flags & FAILING) {
		/* Can we free this sucker? */
		if (ch->tm + SLEEPTIME < t) {
			ch->flags &= ~FAILING;
			ch->count = 0;
			ch->tm = 0;
		} else {
			/* No, we'll look again later */
			if (nxtAlrm == 0 || ch->tm + SLEEPTIME > nxtAlrm)
				nxtAlrm = ch->tm + SLEEPTIME;
		}
	}
  }
  if (nxtAlrm) {
	nxtAlrm -= t;
	if (nxtAlrm < 1) nxtAlrm = 1;
	alarm(nxtAlrm);
  }
}

/* Set all 'Fail' timers to 0 */
void FailCancel(void)
{
  CHILD *ch;

  for(ch = family; ch; ch = ch->next) {
	ch->count = 0;
	ch->tm = 0;
	ch->flags &= ~FAILING;
  }
}


/*
 * The main loop
 */ 
int InitMain(int dflLevel)
{
  int f;
  int did_boot = 0;
  CHILD *ch;
  int loglevel;
  int newlevel = 0;
  int fd;
  char c;
  int pwrstat;

#ifdef ROOTFS
  if (mount(ROOTFS, "/root", "minix", 0, 0) < 0) {
	Warning("Cannot mount root file system.");
	while(1) pause();
  }

  if (chroot("/root") < 0) {
	Warning("Cannot chroot to root file system.");
	while(1) pause();
  }
  chdir("/");
#endif

  /* Tell the kernel to send us SIGINT when CTRL-ALT-DEL is pressed */
  reboot(0xfee1dead, 672274793, 0);

  /* Set up signals */
  for(f = 1; f <= _NSIG; f++)
  	signal(f, SIG_IGN);

  signal(SIGALRM, alrm_handler);
  signal(SIGHUP,  hup_handler);
  signal(SIGSTOP, stop_handler);
  signal(SIGTSTP, stop_handler);
  signal(SIGCONT, cont_handler);
  signal(SIGCHLD, chld_handler);
  signal(SIGINT,  int_handler);
#ifdef SIGPWR
  signal(SIGPWR, pwr_handler);
#endif  

  /* Close whatever files are open, and initialize the console. */
  close(0);
  close(1);
  close(2);
  f = open(CONSOLE, O_RDWR|O_NOCTTY);
  SetTerm(f);
  close(f);
  setsid();

  /* Initialize /etc/utmp */
  close(open(UTMP, O_WRONLY|O_CREAT|O_TRUNC, 0644));

  /* Say hello to the world */
  Say(bootmsg);
  runlevel = '#';
  ReadItab();
  StartEmIfNeeded();

  while(1) {

#if DEBUG
     Say("InitMain: waiting..");
#endif

     /* Check if there is something to wait for! */
     for( ch = family; ch; ch = ch->next )
	if ((ch->flags & RUNNING) && ch->action != BOOT) break;
     
     if (ch == NULL) {
	/* No processes left in this level, proceed to next level. */
	loglevel = -1;
	switch(runlevel) {
		case '#': /* SYSINIT -> BOOT */
#if DEBUG
			Say("SYSINIT -> BOOT");
#endif
			/* Write a boot record. */
			wroteReboot = 0;
			WtmpOnly("reboot", "~~", 0, BOOT_TIME, "~");

  			/* Get our run level */
  			newlevel = dflLevel ? dflLevel : GetInitDefault();
			if (newlevel == 'S')
				runlevel = newlevel;
			else
				runlevel = '*';
			break;
		case '*': /* BOOT -> NORMAL */
#if DEBUG
			Say("BOOT -> NORMAL");
#endif
			if (runlevel != newlevel) loglevel = newlevel;
			runlevel = newlevel;
			did_boot = 1;
			break;
		case 'S': /* Ended SU mode */
		case 's':
#if DEBUG
			Say("END SU MODE");
#endif
			newlevel = AskRunLevel();
			if (!did_boot && newlevel != 'S')
				runlevel = '*';
			else {
				if (runlevel != newlevel) loglevel = newlevel;
				runlevel = newlevel;
			}
			for(ch = family; ch; ch = ch->next)
			    if (strcmp(ch->rlevel, "S") == 0)
				ch->flags &= ~(FAILING|WAITING|EXECUTED);
			break;
		default: /* Normal runlevel, out of processes */
#if DEBUG
			Say("NORMAL (out of processes)");
#endif
			Warning("No more processes left in this runlevel");
			break;
	}
	if (loglevel > 0) {
		Say("Entering runlevel: %c", runlevel);
		WtmpOnly("runlevel", "~~", runlevel, RUN_LVL, "~");
	}
     }

     /* Wait until we get hit by some signal. */
     if (ch != NULL &&
	 (got_int | got_pwr | got_chld | got_alrm | got_hup) == 0) pause();

     /* Check the 'failing' flags */
     FailCheck();

     if (got_pwr) {
#if DEBUG
	Say("got_pwr");
#endif
	/* See _what_ kind of SIGPWR this is. */
	pwrstat = 0;
	if ((fd = open(PWRSTAT, O_RDONLY)) >= 0) {
		c = 0;
		read(fd, &c, 1);
		if (c == 'O') pwrstat = 1;
		close(fd);
		unlink(PWRSTAT);
	}

	/* Tell powerwait & powerfail entries to start up */
	for(ch = family; ch; ch = ch->next)
		if (pwrstat) {
			/* The power is good again. */
			if (ch->action == POWEROKWAIT)
				ch->flags &= ~EXECUTED;
		} else {
			/* Power is failing */
			if (ch->action == POWERFAIL || ch->action == POWERWAIT)
				ch->flags &= ~EXECUTED;
		}
	got_pwr = 0;
     }

     if (got_int) {
#if DEBUG
	Say("got_int");
#endif
	/* Tell ctrlaltdel entry to start up */
	for(ch = family; ch; ch = ch->next)
		if (ch->action == CTRLALTDEL)
			ch->flags &= ~EXECUTED;
	got_int = 0;
     }

     if (got_alrm) {
#if DEBUG
	Say("got_alrm");
#endif
	/* The timer went off: check it out */
	got_alrm = 0;
     }

     if (got_hup) {
#if DEBUG
	Say("got_hup");
#endif
	/* We need to go into a new runlevel */
	lastlevel = runlevel;
	runlevel = ReadLevel();
	FailCancel();
	ReadItab();
	got_hup = 0;
     }

     if (got_chld) {
#if DEBUG
	Say("got_chld");
#endif
	/* See which child this was */
	for(ch = family; ch; ch = ch->next)
	    if (ch->flags & ZOMBIE) {
#if DEBUG
		Say("Child died, PID= %d", ch->pid);
#endif
		ch->flags &= ~(RUNNING|ZOMBIE|WAITING);
		if (ch->process[0] != '+')
			Wtmp("", ch->id, ch->pid, DEAD_PROCESS);
	    }
	got_chld = 0;

     }

     /* See what we need to start up (again) */
     StartEmIfNeeded();
     sync();
  }

}

/*
 * Tell the user about the syntax we expect.
 */
void Usage(s)
char *s;
{
  fprintf(stderr, "Usage: %s 0123456SsQqAaBbCc\n", s);
  exit(1);
}

/*
 * Main entry for init and telinit.
 */
int main(int argc, char **argv)
{
  char *p;
  FILE *fp;
  struct stat st;
  int f, dflLevel = 0;
  extern int getopt(), optind;
  extern char *optarg;

#if TEST
  int pid;
#endif

  /* Get my own name */
  if ((p = strrchr(argv[0], '/')) != NULL)
  	p++;
  else
  	p = argv[0];
  	

  /* See if I want to become "father of all processes" */
#if TEST
  if (strcmp(p, "init") == 0 && (argc == 1 || !strcmp(argv[1], "single")) ) {

  	/* Check command line arguments */
  	for(f = 1; f < argc; f++)
		if (!strcmp(argv[f], "single"))
			dflLevel = 'S';
		else if (!strcmp(argv[f], "-a") || !strcmp(argv[f], "auto"))
			putenv("AUTOBOOT=YES");
		else
			Warning("unrecognized option \"%s\"", argv[f]);

	if ((fp = fopen(INITPID, "w")) != NULL) {
		fprintf(fp, "%d", getpid());
		fclose(fp);
	} else {
		fprintf(stderr, "%s: cannot open %s\n", p, INITPID);
		exit(1);
	}
	InitMain(dflLevel);
  }
#else
  if (getpid() == INITPID) {
  	/* Check command line arguments */
  	for(f = 1; f < argc; f++)
		if (!strcmp(argv[f], "single"))
			dflLevel = 'S';
		else if (!strcmp(argv[f], "-a") || !strcmp(argv[f], "auto"))
			putenv("AUTOBOOT=YES");
		else if (strchr("0123456789sS", argv[f][0])
			&& strlen(argv[f]) == 1)
			dflLevel = argv[f][0];
		else
			Warning("unrecognized option \"%s\"", argv[f]);

		InitMain(dflLevel);
  }
#endif

  /* Nope, this is a change-run-level init */
  while((f = getopt(argc, argv, "t:")) != EOF) {
	switch(f) {
		case 't':
			sltime = atoi(optarg);
			break;
		default:
			Usage(p);
			break;
	}
  }

  if (argc - optind != 1 || strlen(argv[optind]) != 1) Usage(p);
  if (!strchr("0123456789SsQqAaBbCc", argv[optind][0])) Usage(p);

#if TEST
  if ((fp = fopen(INITPID, "r")) == NULL) {
	fprintf(stderr, "%s: cannot open %s\n", p, INITPID);
	exit(1);
  }
  fscanf(fp, "%d", &pid);
  fclose(fp);
#endif
  umask(066);
  if ((fp = fopen(INITLVL, "w")) == NULL) {
	fprintf(stderr, "%s: cannot create %s\n", p, INITLVL);
	exit(1);
  }
  fprintf(fp, "%s %d", argv[optind], sltime);
  fclose(fp);
  signal(SIGTERM, SIG_IGN);
#if TEST
  if (kill(pid, SIGHUP) < 0) perror(p);
#else
  if (kill(INITPID, SIGHUP) < 0) perror(p);
#endif
  /* Now wait until the INITLVL file is gone */
  while(stat(INITLVL, &st) == 0) sleep(1);

  exit(0);
}
