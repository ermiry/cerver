#!/bin/bash

PORT=8099
THREADS=4
READY_SECONDS=16
VALGRIND_READY_SECONDS=120
STOP_SECONDS=32

ALLOCATION_REQUESTS=1024
ALLOCATION_BASE_REQUESTS=256
ALLOCATION_CONCURRENCY=8
ALLOCATIONS_PER_CONNECTION=11
ALLOCATION_SLACK=$(( ALLOCATION_CONCURRENCY * ALLOCATIONS_PER_CONNECTION ))
GET_MAX_ALLOCATIONS=0
POST_MAX_ALLOCATIONS=0

HANDLER=events

BACKENDS=(${BACKENDS:-uring epoll})
BACKEND=${BACKENDS[0]}

LOOP=0
LOOP_MODES=(0 1)

KEEP_ALIVE=0

LOGS=${LOGS:-/tmp/performance}
BUILD=${BUILD:-1}

GET_PATH=/bench
GET_QUERY_PATH='/bench?first=one&second=two%20words'
POST_PATH=/bench/json
JSON_BODY='{"name": "cerver", "values": [1, 2, 3]}'

AUTH_PATH=/bench/auth
AUTH_SECRET=0123456789abcdef0123456789abcdef
AUTH_TOKEN=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpZCI6ImJlbmNoIn0.3TAmk5kUhCB7ekKomaB0Lj3TleEkd8jvyTzPU72RMT8
AUTH_MAX_ALLOCATIONS=10

WEB_SOCKET_PATH=/ws/echo
WEB_SOCKET_SIZE=64
WEB_SOCKET_ALLOCATION_CONNECTIONS=8
WEB_SOCKET_ALLOCATION_MESSAGES=1024
WEB_SOCKET_MAX_ALLOCATIONS=0.01

source test/process.sh

export LD_LIBRARY_PATH=./bin

FAILED=()

mkdir -p "$LOGS"

printf '%s' "$AUTH_SECRET" > "$LOGS/secret"
export BENCH_AUTH="$LOGS/secret"

if [ "$BUILD" -ne 0 ]; then
	make clean > /dev/null
	{ make TYPE=production -j4 && make TYPE=production load; } > "$LOGS/build.log" 2>&1 || { tail -n 32 "$LOGS/build.log"; exit 1; }
fi

fail () {
	echo "FAILED: $1"
	FAILED+=("$1")
}

mode_name () {
	local name="$BACKEND"

	[ "$LOOP" -ne 0 ] && name="$name loop" || name="$name pool"

	echo "$name"
}

server_start () {
	local log="$1" ready="$2" handlers=pool
	shift 2

	[ "$LOOP" -ne 0 ] && handlers=loop

	BENCH_BACKEND=$BACKEND BENCH_HANDLERS=$handlers "$@" ./benchmarks/bin/server $PORT $THREADS $HANDLER > "$log" 2>&1 &
	SERVER_PID=$!

	wait_until "$ready" is_listening $PORT || { echo "the bench server with $(mode_name) did not start"; tail -n 8 "$log"; exit 1; }
}

# stops the server, a server that had to be killed or that ended with an error fails its check
server_stop () {
	kill -INT $SERVER_PID
	wait_until $STOP_SECONDS is_finished $SERVER_PID || kill -KILL $SERVER_PID
	wait $SERVER_PID
}

field () {
	echo "$1" | sed -E "s/.* - $2 ([0-9.]+).*/\1/"
}

# the requests of a load that the server answered with 200, all of them or the load failed
load_succeeded () {
	local result="$1" requests="$2"

	[ "$(field "$result" 200)" = "$requests" ] && [ "$(field "$result" failed)" = "0" ]
}

allocations_snapshot () {
	local file="$1"

	vgdb --pid=$SERVER_PID xtmemory "$file" > /dev/null 2>&1 || return 1
	grep -m 1 "^totals:" "$file" | awk '{ print $5 }'
}

# the load with a connection for each request, or with the connections kept between requests
load () {
	if [ "$KEEP_ALIVE" -ne 0 ]; then
		LOAD_KEEP_ALIVE=1 ./benchmarks/bin/load "$@"
	else
		./benchmarks/bin/load "$@"
	fi
}

connections_name () {
	[ "$KEEP_ALIVE" -ne 0 ] && echo "kept connections" || echo "a connection by request"
}

allocations_load () {
	local concurrency="$1" requests="$2" method="$3" path="$4" body="$5"

	local result=$(load $PORT $concurrency "$requests" $method "$path" "$body")
	load_succeeded "$result" "$requests" || { echo "the load of $requests requests failed: $result" >&2; return 1; }
}

check_allocations () {
	local method="$1" path="$2" body="$3" maximum="$4"
	local log="$LOGS/allocations.log"

	server_start "$log" $VALGRIND_READY_SECONDS valgrind --tool=memcheck --leak-check=no --vgdb=yes --xtree-memory=full --xtree-memory-file="$LOGS/allocations-end.kcg"

	local base total loaded=0
	allocations_load $ALLOCATION_CONCURRENCY $ALLOCATION_BASE_REQUESTS $method "$path" "$body" || loaded=1
	base=$(allocations_snapshot "$LOGS/allocations-base.kcg")
	allocations_load $ALLOCATION_CONCURRENCY $ALLOCATION_REQUESTS $method "$path" "$body" || loaded=1
	total=$(allocations_snapshot "$LOGS/allocations-total.kcg")

	server_stop || loaded=1

	if [ $loaded -ne 0 ] || ! [[ "$base" =~ ^[0-9]+$ ]] || ! [[ "$total" =~ ^[0-9]+$ ]]; then
		fail "$(mode_name) $method $path allocations with $(connections_name) were not counted"
		return
	fi

	local allocations=$(( total - base ))
	local per_request=$(awk -v allocations="$allocations" -v requests=$ALLOCATION_REQUESTS 'BEGIN { printf "%.3f", allocations / requests }')

	echo "$method $path with $(connections_name) - $per_request allocations per request ($allocations for $ALLOCATION_REQUESTS, maximum $maximum)"
	[ "$allocations" -le $(( maximum * ALLOCATION_REQUESTS + ALLOCATION_SLACK )) ] || fail "$(mode_name) $method $path allocations with $(connections_name)"
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
		fail "$(mode_name) WebSocket allocations were not counted"
		return
	fi

	local per_message=$(awk -v total="$total" -v base="$base" -v messages="$messages" 'BEGIN { printf "%.3f", (total - base) / messages }')

	echo "WebSocket $WEB_SOCKET_PATH - $per_message allocations per message ($(( total - base )) for $messages, maximum $WEB_SOCKET_MAX_ALLOCATIONS)"
	awk -v value="$per_message" -v maximum=$WEB_SOCKET_MAX_ALLOCATIONS 'BEGIN { exit !(value <= maximum) }' \
		|| fail "$(mode_name) WebSocket allocations"
}

for BACKEND in "${BACKENDS[@]}"; do
	for LOOP in "${LOOP_MODES[@]}"; do
		KEEP_ALIVE=0

		echo "Allocations with $(mode_name) and $(connections_name)..."
		check_allocations GET $GET_PATH "" $GET_MAX_ALLOCATIONS
		check_allocations GET "$GET_QUERY_PATH" "" $GET_MAX_ALLOCATIONS
		check_allocations POST $POST_PATH "$JSON_BODY" $POST_MAX_ALLOCATIONS
		LOAD_AUTHORIZATION="Bearer $AUTH_TOKEN" check_allocations GET $AUTH_PATH "" $AUTH_MAX_ALLOCATIONS

		KEEP_ALIVE=1

		echo "Allocations with $(mode_name) and $(connections_name)..."
		check_allocations GET $GET_PATH "" $GET_MAX_ALLOCATIONS
		check_allocations POST $POST_PATH "$JSON_BODY" $POST_MAX_ALLOCATIONS
		LOAD_AUTHORIZATION="Bearer $AUTH_TOKEN" check_allocations GET $AUTH_PATH "" $AUTH_MAX_ALLOCATIONS
	done

	LOOP=0
	KEEP_ALIVE=0

	echo "Allocations of the WebSocket messages with $BACKEND..."
	check_web_socket_allocations
done

printf "\n%d checks failed\n" "${#FAILED[@]}"

if [ ${#FAILED[@]} -gt 0 ]; then
	printf "failed: %s\n" "${FAILED[@]}"
	exit 1
fi
