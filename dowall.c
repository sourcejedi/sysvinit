/*
 * dowall.c	- Write to all users on the system.
 *
 * Author:	  Miquel van Smoorenburg, miquels@drinkel.nl.mugnet.org
 * 
 * Version:	  1.01  18-11-1992
 *		  - initial version.
 *
 *		  1.1   31-01-1993
 *		  - Made the open() non blocking, so that false utmp
 *		    entries will not block wall.
 *
 *		  1.2   13-05-1993
 *		  - Added some more code to prevent 'hanging' on
 *		    open()'s or write()'s.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <utmp.h>
#include <pwd.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>

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

static jmp_buf jbuf;

/* Alarm handler */
/*ARGSUSED*/
static void handler(arg)
int arg;
{
  signal(SIGALRM, handler);
  longjmp(jbuf, 1);
}

/*
 * Wall function.
 */
void wall(text)
char *text;
{
  FILE *fp, *tp;
  char line[81];
  char term[32];
  static char *user, ttynm[16], *date;
  static int fd, init = 0;
  struct passwd *pwd;
  char *tty, *p;
  time_t t;
  struct utmp utmp;
  
  if ((fp = fopen(UTMP, "r")) == (FILE *)0) return;
  
  if (init == 0) {
  	if ((pwd = getpwuid(getuid())) == (struct passwd *)0) {
  		fprintf(stderr, "You don't exist. Go away.\n");
  		exit(1);
  	}
  	user = pwd->pw_name;
  	if ((p = ttyname(0)) != (char *)0) {
  		if ((tty = strrchr(p, '/')) != NULL)
  			tty++;
  		else
  			tty = p;
  		sprintf(ttynm, "(%s) ", tty);	
  	} else
  		ttynm[0] = 0;
  	init++;
	signal(SIGALRM, handler);
  }
  
  /* Get the time */
  time(&t);
  date = ctime(&t);
  for(p = date; *p && *p != '\n'; p++)
  	;
  *p = 0;

  sprintf(line, "\007\r\nBroadcast message from %s %s%s...\r\n\r\n", user,
  	ttynm, date);

  while(fread(&utmp, sizeof(struct utmp), 1, fp) == 1) {
  	if(utmp.ut_type != USER_PROCESS ||
	   utmp.ut_user[0] == 0) continue;
  	sprintf(term, "/dev/%s", utmp.ut_line);

	alarm(2); /* Sometimes the open/write hangs in spite of the O_NDELAY */
#ifdef O_NDELAY
	/* Open it non-delay */
	if (setjmp(jbuf) == 0 && (fd = open(term, O_WRONLY | O_NDELAY )) > 0) {
  		if ((tp = fdopen(fd, "w")) != NULL) {
  			fputs(line, tp);
  			fputs(text, tp);
  			fclose(tp);
  		} else
			close(fd);
		fd = -1;
		alarm(0);
	}
	if (fd >= 0) close(fd);
#else
  	if (setjmp(jbuf) == 0 && (tp = fopen(term, "w")) != NULL) {
  		fputs(line, tp);
  		fputs(text, tp);
		alarm(0);
  		fclose(tp);
		tp = NULL;
  	}
	if (tp != NULL) fclose(tp);
#endif
	alarm(0);
  }
  fclose(fp);
}

