#
# Makefile for init, shutdown, halt and wall.
# All written by Miquel van Smoorenburg, <miquels@drinkel.nl.mugnet.org>.
# 

NAME  = SysVinit-2.4

FILES =	Defines History Makefile Readme Propaganda brc dowall.c \
	halt.8 halt.c install.sh init.8 init.c inittab.tst \
	inittab.5 last.1 last.c mesg.1 mesg.c powerd.c \
	shutdown.8 shutdown.c wall.1 wall.c dump.c etc

CC	= cc
CFLAGS	= -Wall -O6
LDFLAGS	= -s

all:		init halt last mesg shutdown wall dump powerd

init:		init.c

halt:		halt.c

last:		last.c

mesg:		mesg.c

dump:		dump.c

powerd:		powerd.c

wall:		dowall.o wall.o

shutdown:	dowall.o shutdown.o

dowall.o:	dowall.c

wall.o:		wall.c

shutdown.o:	shutdown.c

install:	all
		@sh install.sh

clean:
		rm -f *.o *.s *.bak init shutdown halt reboot wall \
		last mesg powerd inittab.new *.tar.Z *.tar.z *.lzh

tar:
		@echo Creating tar archive...
		@tar cvf $(NAME).tar $(FILES)
		@echo Compressing...
		@compress $(NAME).tar
		@echo Tar archive created

lzh:
		@echo Creating lzh archive...
		@lha a $(NAME).lzh $(FILES)
		@echo Lzh archive created

gzip:
		@echo Creating tar archive
		@tar cvf $(NAME).tar $(FILES)
		@echo Gzipping...
		@gzip $(NAME).tar
		@echo Tar archive created
