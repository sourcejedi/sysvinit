/*
 * shutdown.c	- Shut down the system.
 *
 * Usage:	  shutdown [-krhnf] time [warning message]
 *		  -k: don't really shutdown, only warn.
 *		  -r: reboot after shutdown.
 *		  -h: halt after shutdown.
 *		  -n: don't sync before reboot or halt.
 *		  -f: do a 'fast' reboot.
 *		  -c: cancel an already running shutdown.
 *		  -t secs: delay between SIGTERM and SIGKILL
 *
 * Author:	  Miquel van Smoorenburg, miquels@drinkel.nl.mugnet.org
 *
 * Version:	- 0.01 Long time ago
 *		- 1.1  07-12-1992
 *		- 1.2  15-12-1992
 *		- 1.3  24-03-1993
 *		- 1.4  14-05-1993
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h> 
#include <signal.h>
#include <fcntl.h>

char *Version = "@(#) shutdown 1.4 14-05-1993 MvS";

/* Some pathnames */
#define NOLOGIN		"/etc/nologin"
#define FASTBOOT	"/etc/fastboot"
#define INIT		"/etc/init"
#define REBOOT		"/etc/reboot"
#define HALT		"/etc/halt"
#define SDPID		"/etc/shutdownpid"

int dontshut = 0;	/* Don't shutdown, only warn	*/
int do_reboot = 0;	/* Reboot after shutdown	*/
int halt = 0;		/* Halt after shutdown		*/
int dosync = 1;		/* Sync before reboot or halt	*/
int quick = 0;		/* Don't send processes SIGTERM */
int fastboot = 0;	/* Do a 'fast' reboot		*/
char reason[256];	/* Warning message		*/
char *sltime = 0;	/* Sleep time			*/

/* From "wall.c" */
extern void wall();

/*
 * Break off an already running shutdown.
 */
void stopit()
{
  unlink(NOLOGIN);
  unlink(FASTBOOT);
  unlink(SDPID);
  printf("\nShutdown cancelled.\n");
  exit(0);
}

/*
 * Show usage message.
 */
void usage()
{
 fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
	"Usage:\t  shutdown [-krhnf] time [warning message]",
	"\t\t  -k:      don't really shutdown, only warn.",
	"\t\t  -r:      reboot after shutdown.",
	"\t\t  -h:      halt after shutdown.",
	"\t\t  -n:      don't sync before reboot or halt.",
	"\t\t  -f:      do a 'fast' reboot.",
	"\t\t  -c:      cancel a running shutdown.",
	"\t\t  -t secs: delay between warning and kill signal.");
  exit(1);
}

/*
 * Tell everyone the system is going down in 'mins' minutes.
 */
void warn(mins)
int mins;
{
  char buf[512];
  int len;

  strcpy(buf, reason);
  len = strlen(buf);

  if (mins == 0)
	sprintf(buf + len, "\rThe system is going down NOW !!\r\n");
  else
  	sprintf(buf + len,
		"\rThe system is going DOWN in %d minutes !!\r\n", mins);
  wall(buf);
}

/*
 * Create the /etc/nologin file.
 */
void donologin()
{
  FILE *fp;
  time_t t;

  time(&t);
  t += 300;

  unlink(NOLOGIN);
  if ((fp = fopen(NOLOGIN, "w")) != NULL) {
  	fprintf(fp, "\rThe system is going down on %s\r\n", ctime(&t));
  	if (reason[0]) fputs(reason, fp);
  	fclose(fp);
  }
}

/*
 * Execute /etc/reboot or /etc/halt.
 */
void shutdown()
{
  char *prog = (char *)0;
  char *args[6];
  int argp = 0;

  warn(0);
  sleep(2); /* Give telnetd etc the chance to show this message */
  if (dontshut) stopit();

  if (do_reboot) prog = REBOOT;
  if (halt) prog = HALT;

  args[argp++] = prog;
  if (!dosync) args[argp++] = "-n";
  if (quick)   args[argp++] = "-q";
  if (sltime) {
	args[argp++] = "-t";
	args[argp++] = sltime;
  }
  args[argp]   = (char *)NULL;

  unlink(NOLOGIN);
  unlink(SDPID);

  if (prog) {
  	execv(prog, args);
  	fprintf(stderr, "shutdown: cannot execute %s\n", prog);
	unlink("/etc/fastboot");
	exit(1);
  }
  if (dosync) sync();
  if (sltime)
	execl(INIT, "telinit", "-t", sltime, "s", NULL);
  else
	execl(INIT, "telinit", "s", NULL);
  fprintf(stderr, "shutdown: cannot get into single user mode\n");
  unlink(FASTBOOT);	
  exit(1);
}

/*
 * Main program.
 * Process the options and do the final countdown.
 */
int main(argc, argv)
int argc;
char **argv;
{
  extern int getopt();
  extern int optind; 
  int c, wt, hours, mins;
  struct tm *lt;
  time_t t;
  char *n;
  int didnolog = 0;
  int cancel = 0;
  int pid = 0;
  FILE *fp;

  /* We can be installed setuid root (executable for a special group) */
  setuid(geteuid());

  if (getuid() != 0) {
  	fprintf(stderr, "shutdown: must be root.\n");
  	exit(1);
  }

  while((c = getopt(argc, argv, "cqkrhnft:")) != EOF) {
  	switch(c) {
		case 'c': /* Cancel an already running shutdown. */
			cancel = 1;
			break;
		case 'q': /* Do a quick reboot */
			quick = 1;
			break;
  		case 'k': /* Don't really shutdown, only warn.*/
  			dontshut = 1;
  			break;
  		case 'r': /* Automatic reboot */
  			do_reboot = 1;
  			halt = 0;
  			break;
  		case 'h': /* Halt after shutdown */
  			halt = 1;
  			do_reboot = 0;
  			break;
  		case 'n': /* Don't sync after shutdown */
  			dosync = 0;
  			break;
  		case 'f': /* Don't perform fsck after next boot */
  			fastboot = 1;
  			break;
		case 't': /* Delay between TERM and KILL */
			sltime = optarg;
			break;
  		default:
  			usage();
  			break;	
  	}
  }
  /* Read pid of running shutdown from a file */
  if ((fp = fopen(SDPID, "r")) != NULL) {
	fscanf(fp, "%d", &pid);
	fclose(fp);
  }

  /* Read remaining words */
  reason[0] = 0;
  for(c = optind + (cancel == 0); c < argc; c++) {
  	strcat(reason, argv[c]);
  	strcat(reason, " ");
  }
  if (reason[0]) strcat(reason, "\r\n");

  /* See if we want to run or cancel. */
  if (cancel) {
	if (pid <= 0) {
		fprintf(stderr, "shutdown: cannot find pid of running shutdown.\n");
		exit(1);
	}
	if (kill(pid, SIGINT) < 0) {
		fprintf(stderr, "shutdown: not running.\n");
		exit(1);
	}
	if (reason[0]) wall(reason);
	exit(0);
  }
  
  /* Check syntax. */
  if (optind == argc) usage();
  n = argv[optind++];
  
  /* See if we are already running. */
  if (pid > 0 && kill(pid, 0) == 0) {
	fprintf(stderr, "shutdown: already running.\n");
	exit(1);
  }

  /* Create a new PID file. */
  unlink(SDPID);
  umask(022);
  if ((fp = fopen(SDPID, "w")) != NULL) {
	fprintf(fp, "%d", getpid());
	fclose(fp);
  } else
	fprintf(stderr, "shutdown: warning: cannot open %s\n", SDPID);

  for(c = 1; c < _NSIG; c++) signal(c, SIG_IGN);
  signal(SIGINT, stopit);

  /* Go to the root directory */
  chdir("/");
  if (fastboot) close(open(FASTBOOT, O_CREAT | O_RDWR, 0644));

  if (!strcmp(n, "now") || !strcmp(n, "+0")) shutdown();

  if (n[0] == '+') {
  	wt = atoi(n + 1);
  	if (wt == 0) usage();
  } else {
  	if (sscanf(n, "%d:%2d", &hours, &mins) != 2) usage();
  	if (hours > 23 || mins > 59) usage();
  	time(&t);
  	lt = localtime(&t);
  	wt = (60*hours + mins) - (60*lt->tm_hour + lt->tm_min);
  	if (wt < 0) wt += 1440;
  }
  if (wt < 15 && wt!= 10 && wt != 5 && wt != 1) warn(wt);

  while(wt) {
  	if (wt <= 5 && !didnolog) {
  		donologin();
  		didnolog++;
  	}
  	if (wt == 15 || wt == 10 || wt == 5 || wt == 1) warn(wt);
  	sleep(60);
  	wt--;
  }
  shutdown();
  return(0); /* Never happens */
}
