#!/bin/sh
# #(@) Install.sh 1.0 MvS
#
# Installation script for the new init.
# This script installs init inittab reboot halt shutdown wall & man pages

#
# The system installs the binaries into these places:
#
Reboot=/etc/reboot
Halt=/etc/halt
Init=/etc/init
Powerd=/etc/powerd

#
# These binaries are installed into the place they already were
# (the install script tries to find out)
#
Shutdown=/etc/shutdown
Wall=/bin/wall
Last=/bin/last
Lastb=/bin/lastb
Mesg=/usr/bin/mesg
Telinit=/bin/telinit
#
# Some flags
#
ConvertInittab=n

umask 022
PATH=/bin:/usr/bin:/etc:/sbin

echo "SystemV init installation script".
echo
echo "One moment, checking out your system..."
#
# See what kind of inittab this system has...
#
if [ ! -f /etc/inittab ]
then
  echo "Well, I'm sorry. Your system is ancient: you don't have an /etc/inittab"
  echo "at all. Configure the new one, place it in /etc/and try again."
else
  if grep ^tty1 /etc/inittab > /dev/null
  then
	ConvertInittab=y
  fi
fi
#
# Find the place of the binaries...
#
[ -x /bin/shutdown ] && Shutdown=/bin/shutdown
[ -x /sbin/shutdown ] && Shutdown=/sbin/shutdown
[ -x /sbin/wall ] && Wall=/sbin/wall
[ -x /usr/bin/wall ] && Wall=/usr/bin/wall
# Lastb goes to wherever last is..
[ -x /usr/bin/last ] && Lastb=/usr/bin/lastb
[ -x /usr/bin/last ] && Last=/usr/bin/last
[ -x /bin/mesg ] && Mesg=/bin/mesg
[ -x /etc/telinit ] && Telinit=/etc/telinit
[ -x /sbin/telinit ] && Telinit=/sbin/telinit
#
# Tell the user what we're gonna do.
#
echo
if [ $ConvertInittab = y ]
then
  echo "I'm going to convert your old inittab into the new format."
  echo "Your old inittab will be renamed to \"/etc/inittab.old\"."
  sleep 3
  grep -v '#' /etc/inittab | (
    echo "#"
    echo "# System generated inittab"
    echo "#"
    echo "id:4:initdefault:"
    echo "rc::bootwait:/etc/rc"
    echo "#"
    IFS=:
    while read line term command
    do
	echo "${line##tty}:1234:respawn:+$command"
    done
    echo "#"
    echo "ca::ctrlaltdel:/etc/shutdown -t3 -rf now"
    # Be sure /etc/rc is executable
    chmod 755 /etc/rc 1>&2
  ) | tee inittab.new
  echo
else
  echo "Oh! You already have a new style inittab.. I won't touch it."
fi

echo
echo "These files are going to be installed:"
echo
echo "reboot   as $Reboot"
echo "halt     as $Halt"
echo "init     as $Init"
echo "telinit  as $Telinit"
echo "shutdown as $Shutdown"
echo "wall     as $Wall"
echo "last     as $Last"
echo "lastb    as $Lastb"
echo "mesg     as $Mesg"
echo
echo "And the appropriate man pages in /usr/man/man1, man5 and man8"
echo
echo -n "Enter \"yes\" to install: "
read yes
if [ "$yes" != yes ]
then
  echo "Bad luck, blue eyes, goodbye..."
  exit 1
fi

echo "Installing."
#
# Dispose of old files.
#
rm -f $Reboot $Halt /etc/shutdown /bin/shutdown /sbin/shutdown /bin/reboot
rm -f $Wall $Last /bin/lastb /usr/bin/lastb $Mesg $Telinit
mv /etc/init /etc/init.old
if [ $ConvertInittab = y ]
then
  mv /etc/inittab /etc/inittab.old
  cp inittab.new /etc/inittab
  cp brc /etc/brc ; chmod 755 /etc/brc
fi
#
# Install new files
#
cp init $Init;	chmod 755 $Init ; ln -s $Init $Telinit
cp halt $Halt;	chmod 755 $Halt
ln $Halt $Reboot
cp shutdown $Shutdown; chmod 755 $Shutdown
ln -s $Shutdown /etc/shutdown > /dev/null 2>&1
cp wall $Wall;  chmod 755 $Wall
cp last $Last;  chmod 755 $Last
ln -s $Last $Lastb
cp mesg $Mesg;  chmod 755 $Mesg
chown bin.bin $Init $Halt $Shutdown $Wall $last $Lastb $Mesg

#
# And install the man pages.
#
if [ -d /usr/man ]
then
  echo "Installing man pages..."
  umask 002
  for i in 1 5 8
  do
	[ ! -d /usr/man/man$i ] && {
		mkdir /usr/man/man$i
		chown bin.bin /usr/man/man$i
	}
	[ ! -d /usr/man/cat$i ] && {
		mkdir /usr/man/cat$i
		chown bin.bin /usr/man/cat$i
	}
	FILES=`echo *.$i`
	cp *.$i /usr/man/man$i
	( cd /usr/man/man$i ; chown bin.bin $FILES )
  done
fi

echo "Installation complete. Please check everything carefully, and"
echo "then reboot. Succes!"
