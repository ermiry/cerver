#!/bin/bash

PORT=8099
THREADS=4
READY_SECONDS=16
VALGRIND_READY_SECONDS=120
STOP_SECONDS=32

ALLOCATION_REQUESTS=1024
ALLOCATION_CONCURRENCY=8
GET_MAX_ALLOCATIONS=8
POST_MAX_ALLOCATIONS=28

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

server_stop () {
	kill -INT $SERVER_PID
	wait_until $STOP_SECONDS is_finished $SERVER_PID || kill -KILL $SERVER_PID
	wait $SERVER_PID
}

# the total allocations of the server after the requests
count_allocations () {
	local requests="$1" method="$2" path="$3" body="$4"
	local log="$LOGS/allocations.log"

	server_start "$log" $VALGRIND_READY_SECONDS valgrind --tool=memcheck --leak-check=no

	if [ "$requests" -gt 0 ]; then
		./benchmarks/bin/load $PORT $ALLOCATION_CONCURRENCY "$requests" $method "$path" "$body" > /dev/null
	fi

	server_stop

	grep "total heap usage" "$log" | sed -E 's/.*usage: ([0-9,]+) allocs.*/\1/' | tr -d ','
}

check_allocations () {
	local method="$1" path="$2" body="$3" maximum="$4"

	local base=$(count_allocations 0 "$method" "$path" "$body")
	local total=$(count_allocations $ALLOCATION_REQUESTS "$method" "$path" "$body")
	local per_request=$(( (total - base) / ALLOCATION_REQUESTS ))

	echo "$method $path - $per_request allocations per request (maximum $maximum)"
	[ "$per_request" -le "$maximum" ] || fail "$method $path allocations"
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
	server_stop

	echo "$method $path - $result"

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

echo "Load on $ARCHITECTURE..."
for concurrency in "${CONCURRENCIES[@]}"; do
	check_load GET $GET_PATH "" "$concurrency"
	check_load POST $POST_PATH "$JSON_BODY" "$concurrency"
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
