#!/bin/bash
#chkconfig:2345 88 77
#description: mudisd

. /etc/rc.d/init.d/functions

prog="mudisd"
exec=/usr/bin/$prog
OPTS="-d -p /var/run/$prog.pid -c /etc/mudis.yaml"

lockfile=/var/lock/subsys/$prog

start() {
	[ -x $exec ] || exit 5
	echo -n $"Starting $prog: "
	#daemon $exec $OPTS > /dev/null	&& action
	$exec $OPTS -s start > /dev/null && action
	retval=$?
	[ $retval -eq 0 ] && touch $lockfile
}

stop() {
	echo -n $"Stopping $prog: "
	if [ -n "`pidfileofproc $exec`" ] ; then
		killproc $exec
		RETVAL=3
	else
		failure $"Stopping $prog"
	fi
	retval=$?
	echo
	[ $retval -eq 0 ] && rm -f $lockfile
}

restart() {
	stop
	start
}

reload() {
	[ -x $exec ] || exit 5
	echo -n $"Reloading $prog: "
	$exec $OPTS -s reload > /dev/null && action
}

force_reload() {
	restart
}

rh_status() {
	# run checks to determine if the service is running or use generic status
	status $prog
}

rh_status_q() {
	rh_status >/dev/null 2>&1
}


case "$1" in
	start)
		rh_status_q && exit 0
		$1
	;;
	stop)
		rh_status_q || exit 0
		$1
	;;
	restart)
		$1
	;;
	reload)
		rh_status_q || exit 7
		$1
	;;
	force-reload)
		force_reload
	;;
	status)
		rh_status
	;;
	condrestart|try-restart)
		rh_status_q || exit 0
		restart
	;;
	*)
	echo $"Usage: $0 {start|stop|status|restart|condrestart|try-restart|reload|force-reload}"
	exit 2
esac
exit $?
