/*
 * powerd	Monitor the DCD line of a serial port connected to
 *		an UPS. If the power goes down, notify init.
 *		If the power comes up again, notify init again.
 *		As long as the power is OK, the DCD line should be
 *		"HIGH". When the power fails, DCD should go "LOW".
 *		Powerd keeps DTR high so that you can connect
 *		DCD and DTR with a resistor of 10 Kilo Ohm and let the
 *		UPS or some relais pull the DCD line to ground.
 *
 * Usage:	powerd /dev/cua4 (or any other serial device).
 *
 * Author:	Miquel van Smoorenburg, <miquels@drinkel.nl.mugnet.org>.
 *
 * Version:	1.0,  15-05-1993.
 *
 *		This program was originally written for my employer,
 *			** Cistron Electronics **
 *		who has given kind permission to release this program
 *		for general puppose.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#define PWRSTAT		"/etc/powerstatus"

/* Linux does not have SIGPWR (yet!) */
#ifndef SIGPWR
#  define SIGPWR	30
#endif

/* Tell init the power has either gone or is back. */
void powerfail(ok)
int ok;
{
  int fd;

  /* Create an info file for init. */
  unlink(PWRSTAT);
  if ((fd = open(PWRSTAT, O_CREAT|O_WRONLY, 0644)) >= 0) {
	if (ok)
		write(fd, "OK\n", 3);
	else
		write(fd, "FAIL\n", 5);
	close(fd);
  }
  kill(1, SIGPWR);
}

/* Main program. */
int main(int argc, char **argv)
{
  int fd;
  int dtr_bit = TIOCM_DTR;
  int flags;
  int status, oldstat = -1;
  int count = 0;

  if (argc < 2) {
	fprintf(stderr, "Usage: powerd <device>\n");
	exit(1);
  }

  /* Open monitor device. */
  if ((fd = open(argv[1], O_RDWR | O_NDELAY)) < 0) {
	fprintf(stderr, "powerd: %s: %s\n", argv[1], sys_errlist[errno]);
	exit(1);
  }

  /* Line is opened, so DTR is high. Force it anyway to be sure. */
  ioctl(fd, TIOCMBIS, &dtr_bit);

  /* Check it. */
  ioctl(fd, TIOCMGET, &flags);
  if ((flags & TIOCM_CAR) == 0) {
	fprintf(stderr, "powerd: UPS connection error, exiting.\n");
	exit(1);
  }

  /* Daemonize. */
  switch(fork()) {
	case 0: /* Child */
		setsid();
		break;
	case -1: /* Error */
		fprintf(stderr, "powerd: can't fork.\n");
		exit(1);
	default: /* Parent */
		exit(0);
  }

  /* Now sample the DCD line. */
  while(1) {
	ioctl(fd, TIOCMGET, &flags);
	status = (flags & TIOCM_CAR);
	/* Did DCD drop to zero? Then the power has failed. */
	if (oldstat > 0 && status == 0) {
		count++;
		if (count > 3)
			powerfail(0);
		else {
			sleep(1);
			continue;
		}
	}
	/* Did DCD come up again? Then the power is back. */
	if (oldstat == 0 && status > 0) {
		count++;
		if (count > 3)
			powerfail(1);
		else {
			sleep(1);
			continue;
		}
	}
	/* Reset count, remember status and sleep 2 seconds. */
	count = 0;
	oldstat = status;
	sleep(2);
  }
  /* Never happens */
  return(0);
}
