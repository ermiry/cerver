#!/bin/bash

DOCKER=${DOCKER:-docker}
BUILD=${BUILD:-1}
LOGS=${LOGS:-/tmp/performance}

CONNECTIONS=${CONNECTIONS:-256}
POLL_MAX_ALLOCATIONS=${POLL_MAX_ALLOCATIONS:-14}
THREADS_MAX_ALLOCATIONS=${THREADS_MAX_ALLOCATIONS:-15}

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
		-e POLL_MAX_ALLOCATIONS="$POLL_MAX_ALLOCATIONS" \
		-e THREADS_MAX_ALLOCATIONS="$THREADS_MAX_ALLOCATIONS" \
		"$IMAGE" bash -c "apt-get update > /dev/null && apt-get install -y valgrind > /dev/null && bash test/performance.sh"
fi

source test/process.sh

mkdir -p "$LOGS"

# the total allocations of the server after the connections
count_allocations () {
	local handler="$1" connections="$2"
	local log="$LOGS/$handler-$connections.log"

	valgrind --trace-children=no "$SERVER" "$handler" > "$log" 2>&1 &
	local pid=$!

	if ! wait_until $START_SECONDS is_listening $PORT; then
		echo "the $handler server did not start" >&2
		kill -KILL $pid 2> /dev/null
		tail -n 32 "$log" >&2
		return 1
	fi

	if ! timeout $RUN_SECONDS "$CLIENT" "$connections" > "$LOGS/client.log" 2>&1; then
		echo "the client failed with $connections connections" >&2
		tail -n 32 "$LOGS/client.log" >&2
		kill -KILL $pid 2> /dev/null
		return 1
	fi

	kill -TERM $pid
	wait $pid

	grep "total heap usage" "$log" | sed -E 's/.*usage: ([0-9,]+) allocs.*/\1/' | tr -d ','
}

# the allocations of the connections that the second run adds
allocations_per_connection () {
	local handler="$1"

	local single=$(count_allocations "$handler" "$CONNECTIONS") || return 1
	local double=$(count_allocations "$handler" $(( CONNECTIONS * 2 ))) || return 1

	echo $(( (double - single) / CONNECTIONS ))
}

FAILED=0

check_handler () {
	local handler="$1" max_allocations="$2"

	local per_connection=$(allocations_per_connection "$handler")
	if [ -z "$per_connection" ]; then
		return 1
	fi

	echo "$handler: $per_connection allocations per connection"

	if [ "$per_connection" -gt "$max_allocations" ]; then
		echo "$handler: more than $max_allocations allocations per connection"
		return 1
	fi
}

check_handler poll "$POLL_MAX_ALLOCATIONS" || FAILED=1
check_handler threads "$THREADS_MAX_ALLOCATIONS" || FAILED=1

exit $FAILED
