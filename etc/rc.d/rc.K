#! /bin/sh
#
# rc.K		This file is executed by init(8) when the system is being
#		brought down to single user mode. It reverses things
#		set up by rc.M, such as unmounting file systems.
#
# Version:	@(#)/etc/rc.d/rc.S	1.01	03/10/93
#
# Author:	Miquel van Smoorenburg, <miquels@drinkel.nl.mugnet.org>
#

  # Stop swapping
  /etc/swapoff -a

  # Unmount all file systems
  /etc/umount -a

