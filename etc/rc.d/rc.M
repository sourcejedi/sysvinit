#! /bin/sh
#
# rc.M		This file is executed by init(8) when the system is being
#		initialized for one of the "multi user" run levels (i.e.
#		levels 1 through 6).  It usually does mounting of file
#		systems et al.
#
# Version:	@(#)/etc/rc.d/rc.M	2.02	02/26/93
#
# Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
#

  # Tell the viewers what's going to happen...
  echo "Going multiuser..."

  # Do any file system checking necessary...
  if [ ! -f /etc/fastboot ]
  then
	echo -n "Checking file systems... "
	/etc/fsck -A -a
	echo
  fi
  rm -f /etc/fastboot

  # OK.  Enable system swapping, if any.  Use this for swapping to disks.
  #/etc/swapon -a

  # Mount the file systems.
  /etc/mount -a -t nonfs
  /bin/chmod 644 /etc/mtab
  /bin/chown bin.bin /etc/mtab

  # OK.  Enable system swapping, if any.  Use this for swapping to files.
  /etc/swapon -a

  # Fire up the usual daemons.
  echo -n "Starting daemons update"
  /etc/update &
  echo " cron"
  /usr/bin/cron &

  # Initialize the NET subsystem.
  if [ -s /etc/NETWORKING -a "`/bin/cat /etc/NETWORKING`" = "YES" ]
  then
	if [ -f /etc/rc.d/rc.inet ]
	then
		/etc/rc.d/rc.inet
	fi
  fi 

  # Start the local setup procedure.
  . /etc/rc.d/rc.local

  # All done.
