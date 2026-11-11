#!/bin/bash

DOCKER=${DOCKER:-docker}
BUILD=${BUILD:-1}
LOGS=${LOGS:-/tmp/allocations}

CONNECTIONS=${CONNECTIONS:-256}
PACKETS=${PACKETS:-1024}
POLL_MAX_ALLOCATIONS=${POLL_MAX_ALLOCATIONS:-4}
THREADS_MAX_ALLOCATIONS=${THREADS_MAX_ALLOCATIONS:-5}
HANDLERS_MAX_ALLOCATIONS=${HANDLERS_MAX_ALLOCATIONS:-0}

PORT=7400
START_SECONDS=60
RUN_SECONDS=600

SERVER=./test/bin/cerver/connections
CLIENT=./test/bin/client/connections

if [ ! -f /.dockerenv ]; then
	IMAGE=${IMAGE:-ermiry/cerver:test}

	if [ "$BUILD" -ne 0 ]; then
		$DOCKER build -t "$IMAGE" -f Dockerfile.test . || exit 1
	fi

	exec $DOCKER run --rm \
		-e CONNECTIONS="$CONNECTIONS" \
		-e PACKETS="$PACKETS" \
		-e POLL_MAX_ALLOCATIONS="$POLL_MAX_ALLOCATIONS" \
		-e THREADS_MAX_ALLOCATIONS="$THREADS_MAX_ALLOCATIONS" \
		-e HANDLERS_MAX_ALLOCATIONS="$HANDLERS_MAX_ALLOCATIONS" \
		"$IMAGE" bash -c "apt-get update > /dev/null && apt-get install -y valgrind > /dev/null && bash test/allocations.sh"
fi

source test/process.sh

mkdir -p "$LOGS"

# the total allocations of the server after the client sent its requests
count_allocations () {
	local mode="$1" count="$2" client_mode="$3"
	local log="$LOGS/$mode-$count.log"

	valgrind --trace-children=no "$SERVER" "$mode" > "$log" 2>&1 &
	local pid=$!

	if ! wait_until $START_SECONDS is_listening $PORT; then
		echo "the $mode server did not start" >&2
		kill -KILL $pid 2> /dev/null
		tail -n 32 "$log" >&2
		return 1
	fi

	if ! timeout $RUN_SECONDS "$CLIENT" "$count" $client_mode > "$LOGS/client.log" 2>&1; then
		echo "the client failed with $count $mode requests" >&2
		tail -n 32 "$LOGS/client.log" >&2
		kill -KILL $pid 2> /dev/null
		return 1
	fi

	kill -TERM $pid
	wait $pid

	grep "total heap usage" "$log" | sed -E 's/.*usage: ([0-9,]+) allocs.*/\1/' | tr -d ','
}

# the allocations of each unit (a connection or a packet) that the second run adds
allocations_per_unit () {
	local mode="$1" count="$2" client_mode="$3"

	local single=$(count_allocations "$mode" "$count" $client_mode) || return 1
	local double=$(count_allocations "$mode" $(( count * 2 )) $client_mode) || return 1

	echo $(( (double - single) / count ))
}

FAILED=0

check_mode () {
	local mode="$1" unit="$2" count="$3" max_allocations="$4" client_mode="$5"

	local per_unit=$(allocations_per_unit "$mode" "$count" $client_mode)
	if [ -z "$per_unit" ]; then
		return 1
	fi

	echo "$mode: $per_unit allocations per $unit"

	if [ "$per_unit" -gt "$max_allocations" ]; then
		echo "$mode: more than $max_allocations allocations per $unit"
		return 1
	fi
}

check_mode poll connection "$CONNECTIONS" "$POLL_MAX_ALLOCATIONS" || FAILED=1
check_mode threads connection "$CONNECTIONS" "$THREADS_MAX_ALLOCATIONS" || FAILED=1
check_mode handlers packet "$PACKETS" "$HANDLERS_MAX_ALLOCATIONS" packets || FAILED=1

exit $FAILED
