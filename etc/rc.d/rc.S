#! /bin/sh
#
# rc.S		This file is executed by init(8) when the system is being
#		initialized.  It takes care of the system just prior to
#		making it available for single-user (or multi-user) mode.
#		This includes setting up the basic networking interfaces
#		(to allow for remote mounting), initializing device dri-
#		vers, and that sort of things.
#
# Version:	@(#)/etc/rc.d/rc.S	2.02	02/26/93
#
# Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
#

  # Clear the mount table.
  rm -f /etc/mtab~		# remove any lockfile...
  rm -f /etc/mtab		# remove the mount table itself

  # Set up networking if this has been configured.
  if [ -s /etc/NETWORKING -a "`/bin/cat /etc/NETWORKING`" = "YES" ]
  then
	MYNAME=""

	# HACK: disable any old-style DNS lookups.
	if [ -f /etc/resolv.conf ]
	then
		/bin/mv /etc/resolv.conf /etc/_resolv.conf
	fi

	# Fetch our current host name, and set it up.
	/bin/hostname -Sf
	MYNAME="`/bin/hostname`"

	# Attach the loopback device.
	/etc/ifconfig lo $MYNAME up
	/etc/ifconfig lo localhost up

	# Attach the primary Ethernet channel as "we0".
	#/etc/ifconfig wd0 $MYNAME net alone up

	# HACK: enable any old-style DNS lookups.
	if [ -f /etc/_resolv.conf ]
	then
		/bin/mv /etc/_resolv.conf /etc/resolv.conf
	fi
	unset MYNAME
  fi

  # All done.
