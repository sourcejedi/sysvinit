/*
 * Halt		Stop the system running.
 *		Halt stops init, and then kills all processes.
 *		After that, the program (shell script) /etc/rc.d/rc.0
 *		is executed. If that can't be found, /etc/brc is used.
 *
 * Usage:	halt [-n][-q][-t secs]
 *		-n: don't sync before halting the system
 *		-q: quick reboot, don't send processes a warning signal.
 *		-t secs: delay between SIGTERM and SIGKILL
 *
 * Reboot	Identical to halt, but after rc.0 has been
 *		executed, the system is automatically rebooted.
 *
 *		Reboot and halt are both this program. Reboot
 *		is just a link to halt.
 *
 * Author:	Miquel van Smoorenburg, miquels@drinkel.nl.mugnet.org
 *
 * Version:	1.3,  24-03-1993
 *
 */

#include <sys/types.h>
#include <stdlib.h>
#include <signal.h>
#include <utmp.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <stdio.h>

char *Version = "@(#) halt 1.3 24-03-1993 MvS";

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

#define INITPID		1	/* PID of init				*/
#define DOWN1 "/etc/rc.d/rc.0"	/* Script to be run if system is down	*/
#define DOWN2 "/etc/brc"	/* Second choise */
#define NO_KILL_ALL	0	/* Linux can kill all processes simult. */
extern int getopt();		/* For getopt (3) 			*/
extern int optind;		/* Getopt too				*/
int nosync = 0;			/* Don't sync before reboot or halt	*/
int quick = 0;			/* Quick kill of all processes		*/
char *progname;			/* Invoked as 'halt' or 'reboot' ?	*/
int sltime = 20;		/* Sleeptime between TERM and KILL	*/

/*
 * Sleep 't' seconds. Print a dot every second.
 */
void dotdot(t)
int t;
{
  int f;
  
  for(f = 0; f < t; f++) {
  	sleep(1);
  	putchar('.');
  	fflush(stdout);
  }
  printf("\r\n");
}

#if NO_KILL_ALL
/*
 * Kill all processes spawned by init by reading /etc/utmp.
 */
void killall()
{
  struct utmp utmp;
  int fd;
  int pid = getpid();

  if ((fd = open(UTMP, O_RDONLY)) < 0) {
  	fprintf(stderr, "halt: cannot open %s\n", UTMP);
  	exit(1);
  }
  if (!quick) {
	/* Send all processes the SIGTERM signal */
	printf("\r%s: sending all processes the TERM signal", progname);
	while(read(fd, &utmp, sizeof(utmp)) == sizeof(utmp)) {
  		if (utmp.ut_type && utmp.ut_pid && utmp.ut_pid != pid)
  			kill(utmp.ut_pid, SIGTERM);
	}
	dotdot(sltime); /* Give processes the time to clean up */
	lseek(fd, 0L, SEEK_SET);
  }

  printf("%s: sending all processes the KILL signal..\r\n", progname);
  /* Send all processes the SIGKILL signal */
  while(read(fd, &utmp, sizeof(utmp)) == sizeof(utmp)) {
  	if (utmp.ut_type && utmp.ut_pid && utmp.ut_pid != pid)
  		kill(utmp.ut_pid, SIGKILL);
  }
  close(fd);
}
#else
/*
 * Kill all processes with the special argument '-1' to kill().
 */
void killall()
{
  if (!quick) {
	printf("\r%s: sending all processes the TERM signal", progname);
	kill(-1, SIGTERM);
	dotdot(sltime);
  }	
  printf("%s: sending all processes the KILL signal..\r\n", progname);
  kill(-1, SIGKILL);
}
#endif

/*
 * Send usage message.
 */
void usage()
{
  fprintf(stderr, "usage: %s [-qn]\n", progname);
  exit(1);
}

/*
 * Main program.
 * Kill all processes, write a wtmp entry and reboot cq. halt.
 */
int main(argc, argv)
int argc;
char **argv;
{
  struct utmp wtmp;
  int c, pid, st, fd, f;
  time_t t;
  int do_reboot = 0;

  /* Ignore all signals */
  for(f = 1; f < _NSIG; f++) {
#ifdef SIGCHLD
  if (f == SIGCHLD) continue;
#endif
#ifdef SIGCLD
  if (f == SIGCLD)  continue;
#endif
	signal(f, SIG_IGN);
  }

  /* Find out who we are */
  if ((progname = strrchr(argv[0], '/')) != NULL)
  	progname++;
  else
  	progname = argv[0];
  	
  if (!strcmp(progname, "reboot")) do_reboot = 1;

  /* Get flags */
  while((c = getopt(argc, argv, "nqt:")) != EOF) {
  	switch(c) {
  		case 'n':
  			nosync = 1;
  			break;
  		case 'q':
  			quick = 1;
  			break;
		case 't':
			sltime = atoi(optarg);
			break;
  		default:
  			usage();
  	}
  }
  if (optind < argc) usage();
  
  /* Tell init to stop */
  if (kill(INITPID, SIGTSTP) != 0) {
  	fprintf(stderr, "%s: can't idle init\n", progname);
  	exit(1);
  }

  /* Kill all processes */
  killall();
  
  if (!nosync) sync();

  /* Record the fact that's we're going down */
  if ((fd = open(WTMP, O_WRONLY|O_APPEND)) >= 0) {
  	time(&t);
  	strcpy(wtmp.ut_user, "shutdown");
  	strcpy(wtmp.ut_line, "~");
  	strcpy(wtmp.ut_id,  "~~");
  	wtmp.ut_pid = 0;
  	wtmp.ut_type = RUN_LVL;
  	wtmp.ut_time = t;
  	write(fd, (char *)&wtmp, sizeof(wtmp));
  	close(fd);
  }

  /* Now we're alone, execute /etc/brc */
  if ((pid = fork()) == 0) {
	execlp(DOWN1, DOWN1, (char *)NULL);
	execlp(DOWN2, DOWN2, (char *)NULL);
	exit(1);
  }
  if (pid > 0)
	wait(&st);
  else
	fprintf(stderr, "Warning: cannot fork: %s not executed\n",
	    	DOWN1);

  if (!nosync) sync();
  if (do_reboot) {
	if (!nosync) sleep(2); /* For sync */
	reboot(0xfee1dead, 672274793, 0x01234567);
  } else
	printf("\r\nThe system is halted\r\n");
  exit(0);
}
