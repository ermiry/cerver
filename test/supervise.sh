#!/bin/bash

# usage: test/supervise.sh <restart seconds> <command...>
# runs the command again restart seconds after it ends, until a SIGINT or a SIGTERM stops it,
# the signal is passed to the command and its exit code is the one of the last run,
# a run that failed by itself makes it fail too

RESTART_SECONDS="$1"
shift

STOPPING=0
CHILD=0
FAILED=0

stop () {
	STOPPING=1

	if [ $CHILD -ne 0 ]; then
		kill -INT $CHILD 2> /dev/null
	fi
}

trap stop INT TERM

# a wait that a signal interrupts returns before the command ended
wait_child () {
	local code=0

	wait $CHILD
	code=$?

	while kill -0 $CHILD 2> /dev/null; do
		wait $CHILD
		code=$?
	done

	return $code
}

while [ $STOPPING -eq 0 ]; do
	"$@" &
	CHILD=$!

	wait_child
	code=$?
	CHILD=0

	if [ $STOPPING -ne 0 ]; then
		[ $code -ne 0 ] && FAILED=1
		break
	fi

	if [ $code -ne 0 ]; then
		echo "supervise: $1 failed with $code, it is started again"
		FAILED=1
	fi

	sleep "$RESTART_SECONDS" &
	wait $!
done

exit $FAILED
