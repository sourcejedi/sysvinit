#
# A sample of the new, SYSV compatible inittab.
#
# (This one runs on my machine every day)
#
# Level to run in. Set to 5 or 6 to allow serial port logins.
# If you comment this out, the system will ask you for a runlevel
# when it is booted.
#
#id:4:initdefault:
#
# Sysinit: takes place only once, right after system boot, *before*
# possibly going single-user.
#
si::sysinit:echo "sysinit stage is executing"
#
# boot & bootwait take place once too, but *after* possibly going
# single user.
#
rc::bootwait:echo "Reading rc in bootwait"
#
# No cron today.
#
#cr::boot:/usr/bin/cron
#
# Normal levels: 1-4 = virtual consoles 5,6 = COM ports.
# Ofcourse you can change this to suit your taste.
#
ts:S:wait:echo "Going single user!"
tt:4:respawn:sh -c "sleep 1; date"
a1:4:wait:sh -c "echo this should execute once...01 ; sleep 5"
a2:4:wait:sh -c "echo this should execute once...02 ; sleep 5"
c1:123456:respawn:/etc/getty 9600 tty6
c2:23456:respawn:/etc/getty 9600 tty7
c3:3456:respawn:/etc/getty 9600 tty8
c4:4:once:/tmp/test.sh
en:345:wait:echo Entering level 3, 4 and 5 : hello.
#
# The routines that take care of a graceful shutdown.
# Someone presented us with the Three Finger Salute.
#
ca:S0123456:ctrlaltdel:sh -c "echo PANIC!! ; sleep 2 ; echo NO PANIC."
