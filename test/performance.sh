#!/bin/bash

PORT=8099
THREADS=4
READY_SECONDS=16
VALGRIND_READY_SECONDS=120
STOP_SECONDS=32

ALLOCATION_REQUESTS=1024
ALLOCATION_BASE_REQUESTS=256
ALLOCATION_SLACK=0
ALLOCATION_CONCURRENCY=8
GET_MAX_ALLOCATIONS=0
POST_MAX_ALLOCATIONS=14

LOAD_REQUESTS=16384
CONCURRENCIES=(8 64)
MINIMUM_PERCENT=50

TREE_ROUNDS=1024
TREE_MINIMUM_SPEEDUP=2

BASELINE=benchmarks/baseline.txt
ROUTES=benchmarks/routes
LOGS=${LOGS:-/tmp/performance}
BUILD=${BUILD:-1}

GET_PATH=/bench
POST_PATH=/bench/json
JSON_BODY='{"name": "cerver", "values": [1, 2, 3]}'

WEB_SOCKET_PATH=/ws/echo
WEB_SOCKET_SIZE=64
WEB_SOCKET_ALLOCATION_CONNECTIONS=8
WEB_SOCKET_ALLOCATION_MESSAGES=1024
WEB_SOCKET_MAX_ALLOCATIONS=0.01
WEB_SOCKET_LOAD_MESSAGES=1048576

source test/process.sh

export LD_LIBRARY_PATH=./bin

ARCHITECTURE=$(uname -m)
FAILED=()

mkdir -p "$LOGS"

if [ "$BUILD" -ne 0 ]; then
	make clean > /dev/null
	{ make TYPE=production -j4 && make TYPE=production load tree; } > "$LOGS/build.log" 2>&1 || { tail -n 32 "$LOGS/build.log"; exit 1; }
fi

fail () {
	echo "FAILED: $1"
	FAILED+=("$1")
}

server_start () {
	local log="$1" ready="$2"
	shift 2

	"$@" ./benchmarks/bin/server $PORT $THREADS > "$log" 2>&1 &
	SERVER_PID=$!

	wait_until "$ready" is_listening $PORT || { echo "the bench server did not start"; exit 1; }
}

# stops the server, a server that had to be killed or that ended with an error fails its check
server_stop () {
	kill -INT $SERVER_PID
	wait_until $STOP_SECONDS is_finished $SERVER_PID || kill -KILL $SERVER_PID
	wait $SERVER_PID
}

# the requests of a load that the server answered with 200, all of them or the load failed
load_succeeded () {
	local result="$1" requests="$2"

	[ "$(field "$result" 200)" = "$requests" ] && [ "$(field "$result" failed)" = "0" ]
}

# the total allocations of the server after the requests,
# nothing when the load or the server failed
count_allocations () {
	local requests="$1" method="$2" path="$3" body="$4"
	local log="$LOGS/allocations.log"

	server_start "$log" $VALGRIND_READY_SECONDS valgrind --tool=memcheck --leak-check=no

	local result=$(./benchmarks/bin/load $PORT $ALLOCATION_CONCURRENCY "$requests" $method "$path" "$body")
	local loaded=0
	load_succeeded "$result" "$requests" || loaded=1

	server_stop || return 1
	[ $loaded -eq 0 ] || { echo "the load of $requests requests failed: $result" >&2; return 1; }

	grep "total heap usage" "$log" | sed -E 's/.*usage: ([0-9,]+) allocs.*/\1/' | tr -d ','
}

# the same server with a few requests and with many, so only the requests are counted:
# the first requests of each thread create what it keeps between requests
check_allocations () {
	local method="$1" path="$2" body="$3" maximum="$4"

	local base total
	base=$(count_allocations $ALLOCATION_BASE_REQUESTS "$method" "$path" "$body")
	total=$(count_allocations $(( ALLOCATION_BASE_REQUESTS + ALLOCATION_REQUESTS )) "$method" "$path" "$body")

	if ! [[ "$base" =~ ^[0-9]+$ ]] || ! [[ "$total" =~ ^[0-9]+$ ]]; then
		fail "$method $path allocations were not counted"
		return
	fi

	local allocations=$(( total - base ))
	local per_request=$(awk -v allocations="$allocations" -v requests=$ALLOCATION_REQUESTS 'BEGIN { printf "%.3f", allocations / requests }')

	echo "$method $path - $per_request allocations per request ($allocations for $ALLOCATION_REQUESTS, maximum $maximum)"
	[ "$allocations" -le $(( maximum * ALLOCATION_REQUESTS + ALLOCATION_SLACK )) ] || fail "$method $path allocations"
}

baseline_value () {
	local method="$1" concurrency="$2"
	awk -v architecture="$ARCHITECTURE" -v method="$method" -v concurrency="$concurrency" \
		'$1 == architecture && $2 == method && $3 == concurrency { print $4 }' "$BASELINE"
}

field () {
	echo "$1" | sed -E "s/.* - $2 ([0-9.]+).*/\1/"
}

check_load () {
	local method="$1" path="$2" body="$3" concurrency="$4"

	server_start "$LOGS/load.log" $READY_SECONDS
	local result=$(./benchmarks/bin/load $PORT "$concurrency" $LOAD_REQUESTS $method "$path" "$body")
	server_stop || fail "$method $path concurrency $concurrency: the server ended with an error"

	echo "$method $path - $result"

	load_succeeded "$result" $LOAD_REQUESTS || { fail "$method $path concurrency $concurrency: the load failed"; return; }

	local per_second=$(echo "$result" | sed -E 's/.* - ([0-9]+) req\/s.*/\1/')
	local unavailable=$(field "$result" 503)
	local other=$(field "$result" other)
	local failed=$(field "$result" failed)

	[ "$unavailable" -eq 0 ] && [ "$other" -eq 0 ] && [ "$failed" -eq 0 ] || fail "$method $path concurrency $concurrency errors"

	local baseline=$(baseline_value "$method" "$concurrency")
	if [ -z "$baseline" ]; then
		echo "no baseline for $ARCHITECTURE $method $concurrency, measured: $ARCHITECTURE $method $concurrency $per_second"
		return
	fi

	local minimum=$(( baseline * MINIMUM_PERCENT / 100 ))
	[ "$per_second" -ge "$minimum" ] || fail "$method $path concurrency $concurrency: $per_second req/s < $minimum"
}

count_web_socket_allocations () {
	local messages="$1"
	local log="$LOGS/allocations.log"

	server_start "$log" $VALGRIND_READY_SECONDS valgrind --tool=memcheck --leak-check=no

	local result=$(./benchmarks/bin/messages $PORT $WEB_SOCKET_ALLOCATION_CONNECTIONS "$messages" $WEB_SOCKET_SIZE $WEB_SOCKET_PATH)
	local echoed=0
	[ "$(field "$result" failed)" = "0" ] || echoed=1

	server_stop || return 1
	[ $echoed -eq 0 ] || { echo "the messages of the WebSocket connections failed: $result" >&2; return 1; }

	grep "total heap usage" "$log" | sed -E 's/.*usage: ([0-9,]+) allocs.*/\1/' | tr -d ','
}

# the same connections with one message and with many, so only the messages are counted
check_web_socket_allocations () {
	local base total
	base=$(count_web_socket_allocations 1)
	total=$(count_web_socket_allocations $(( WEB_SOCKET_ALLOCATION_MESSAGES + 1 )))
	local messages=$(( WEB_SOCKET_ALLOCATION_CONNECTIONS * WEB_SOCKET_ALLOCATION_MESSAGES ))

	if ! [[ "$base" =~ ^[0-9]+$ ]] || ! [[ "$total" =~ ^[0-9]+$ ]]; then
		fail "WebSocket allocations were not counted"
		return
	fi

	local per_message=$(awk -v total="$total" -v base="$base" -v messages="$messages" 'BEGIN { printf "%.3f", (total - base) / messages }')

	echo "WebSocket $WEB_SOCKET_PATH - $per_message allocations per message ($(( total - base )) for $messages, maximum $WEB_SOCKET_MAX_ALLOCATIONS)"
	awk -v value="$per_message" -v maximum=$WEB_SOCKET_MAX_ALLOCATIONS 'BEGIN { exit !(value <= maximum) }' \
		|| fail "WebSocket allocations"
}

check_web_socket_load () {
	local connections="$1"

	server_start "$LOGS/load.log" $READY_SECONDS
	local result=$(./benchmarks/bin/messages $PORT "$connections" $(( WEB_SOCKET_LOAD_MESSAGES / connections )) $WEB_SOCKET_SIZE $WEB_SOCKET_PATH)
	server_stop || fail "WebSocket connections $connections: the server ended with an error"

	echo "WebSocket $WEB_SOCKET_PATH - $result"

	local per_second=$(echo "$result" | sed -E 's/.* - ([0-9]+) msg\/s.*/\1/')
	local failed=$(field "$result" failed)

	[ "$failed" -eq 0 ] || fail "WebSocket connections $connections errors"

	local baseline=$(baseline_value WS "$connections")
	if [ -z "$baseline" ]; then
		echo "no baseline for $ARCHITECTURE WS $connections, measured: $ARCHITECTURE WS $connections $per_second"
		return
	fi

	local minimum=$(( baseline * MINIMUM_PERCENT / 100 ))
	[ "$per_second" -ge "$minimum" ] || fail "WebSocket connections $connections: $per_second msg/s < $minimum"
}

check_tree () {
	local routes="$1"

	local result=$(./benchmarks/bin/tree "$routes" $TREE_ROUNDS)
	echo "$result"

	local speedup=$(echo "$result" | sed -nE 's/^speedup: ([0-9.]+)x/\1/p')
	awk -v speedup="$speedup" -v minimum=$TREE_MINIMUM_SPEEDUP 'BEGIN { exit !(speedup >= minimum) }' \
		|| fail "$routes speedup ${speedup}x < ${TREE_MINIMUM_SPEEDUP}x"
}

echo "Allocations..."
check_allocations GET $GET_PATH "" $GET_MAX_ALLOCATIONS
check_allocations POST $POST_PATH "$JSON_BODY" $POST_MAX_ALLOCATIONS
check_web_socket_allocations

echo "Load on $ARCHITECTURE..."
for concurrency in "${CONCURRENCIES[@]}"; do
	check_load GET $GET_PATH "" "$concurrency"
	check_load POST $POST_PATH "$JSON_BODY" "$concurrency"
	check_web_socket_load "$concurrency"
done

echo "Routes tree..."
for routes in "$ROUTES"/*.txt; do
	check_tree "$routes"
done

printf "\n%d checks failed\n" "${#FAILED[@]}"

if [ ${#FAILED[@]} -gt 0 ]; then
	printf "failed: %s\n" "${FAILED[@]}"
	exit 1
fi
