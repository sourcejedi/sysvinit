/*
 * dump		Simple program to dump UTMP and WTMP files in
 *		raw format, so they can be examined.
 *
 * Author:	Miquel van Smoorenburg, <miquels@drinkel.nl.mugnet.org>
 * Date:	27-Apr-1993
 * Version:	1.0
 */

#include <stdio.h>
#include <utmp.h>
#include <time.h>

void dump(fp)
FILE *fp;
{
  struct utmp ut;
  int f;

  while (fread(&ut, sizeof(struct utmp), 1, fp) == 1) {
	for(f = 0; f < 12; f++) if (ut.ut_line[f] == ' ') ut.ut_line[f] = '_';
	for(f = 0; f <  8; f++) if (ut.ut_name[f] == ' ') ut.ut_name[f] = '_';
	printf("[%d] [%05d] [%-2.2s] %-8.8s %-12.12s %-15.15s\n",
		ut.ut_type, ut.ut_pid, ut.ut_id, ut.ut_user,
		ut.ut_line, 4 + ctime(&ut.ut_time));
  }
}

int main(argc, argv)
int argc;
char **argv;
{
  int f;
  FILE *fp;

  if (argc == 1) {
	printf("Utmp dump of stdin\n");
	dump(stdin);
  } else {
	for(f = 1; f < argc; f++) {
		printf("Utmp dump of %s\n", argv[f]);
		if ((fp = fopen(argv[f], "r")) != NULL) {
			dump(fp);
			fclose(fp);
		}
	}
  }
  return(0);
}
