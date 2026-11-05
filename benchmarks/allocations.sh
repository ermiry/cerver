#!/bin/bash

# counts the allocations per request of the bench server with valgrind
# and measures its requests per second and latency without valgrind
# usage: bash benchmarks/allocations.sh (from the repo root, after make load)

PORT=8099
THREADS=4
REQUESTS=1024
CONCURRENCY=8
JSON_BODY='{"name": "cerver", "values": [1, 2, 3]}'

export LD_LIBRARY_PATH=./bin

# the total allocations of the server after the requests
count_allocations () {
	local requests="$1" method="$2" path="$3" body="$4"

	valgrind --tool=memcheck --leak-check=no ./benchmarks/bin/server $PORT $THREADS > /tmp/allocations.log 2>&1 &
	local pid=$!
	sleep 4

	if [ "$requests" -gt 0 ]; then
		./benchmarks/bin/load $PORT $CONCURRENCY "$requests" $method "$path" "$body" > /dev/null
	fi

	kill -INT $pid
	wait $pid

	grep "total heap usage" /tmp/allocations.log | sed -E 's/.*usage: ([0-9,]+) allocs.*/\1/' | tr -d ','
}

per_request () {
	local method="$1" path="$2" body="$3"

	local base=$(count_allocations 0 "$method" "$path" "$body")
	local total=$(count_allocations $REQUESTS "$method" "$path" "$body")

	echo "$method $path - $(( (total - base) / REQUESTS )) allocations per request"
}

throughput () {
	local method="$1" path="$2" body="$3"

	./benchmarks/bin/server $PORT $THREADS > /dev/null 2>&1 &
	local pid=$!
	sleep 1

	./benchmarks/bin/load $PORT $CONCURRENCY $(( REQUESTS * 8 )) $method "$path" "$body"

	kill -INT $pid
	wait $pid
}

per_request GET /bench
per_request POST /bench/json "$JSON_BODY"

throughput GET /bench
throughput POST /bench/json "$JSON_BODY"
