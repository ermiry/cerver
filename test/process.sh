#!/bin/bash

POLL_SECONDS=0.1

is_alive () {
	local state=$(sed 's/.*) //' "/proc/$1/stat" 2> /dev/null | cut -d ' ' -f 1)
	[ -n "$state" ] && [ "$state" != "Z" ]
}

is_listening () {
	local port_hex=$(printf ':%04X' "$1")
	cat /proc/net/tcp /proc/net/tcp6 2> /dev/null | awk '{ print $2, $4 }' | grep -q "$port_hex 0A"
}

is_finished () {
	! is_alive "$1"
}

wait_until () {
	local seconds="$1"
	shift

	local deadline=$(( $(date +%s) + seconds ))
	while [ "$(date +%s)" -lt "$deadline" ]; do
		"$@" && return 0
		sleep $POLL_SECONDS
	done

	return 1
}

stacks () {
	local pid="$1" label="$2"

	if is_alive "$pid" && command -v gdb > /dev/null; then
		echo "---- $label stacks ----"
		gdb -batch -p "$pid" -ex "thread apply all bt" 2> /dev/null
	fi
}
