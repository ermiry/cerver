#!/bin/bash

DOCKER=${DOCKER:-docker}
BUILD=${BUILD:-1}
LOGS=${LOGS:-/tmp/stress}

CONNECTIONS=${CONNECTIONS:-64}
REQUESTS=${REQUESTS:-1024}
BODY_SIZE=${BODY_SIZE:-64}
MODES=(${MODES:-poll threads handlers})
ADDRESSES=(${ADDRESSES:-127.0.0.1 ::1})

PORT=7600
START_SECONDS=30
RUN_SECONDS=600

SERVER=./test/bin/cerver/stress
CLIENT=./test/bin/client/stress

if [ ! -f /.dockerenv ]; then
	IMAGE=${IMAGE:-ermiry/cerver:test}

	if [ "$BUILD" -ne 0 ]; then
		$DOCKER build -t "$IMAGE" -f Dockerfile.test . || exit 1
	fi

	exec $DOCKER run --rm \
		--sysctl net.ipv6.conf.all.disable_ipv6=0 \
		-e CONNECTIONS="$CONNECTIONS" \
		-e REQUESTS="$REQUESTS" \
		-e BODY_SIZE="$BODY_SIZE" \
		-e MODES="${MODES[*]}" \
		-e ADDRESSES="${ADDRESSES[*]}" \
		"$IMAGE" bash test/stress.sh
fi

source test/process.sh

mkdir -p "$LOGS"

run_mode () {
	local mode="$1" address="$2"
	local log="$LOGS/$mode-$(echo "$address" | tr ':.' '--').log"

	CERVER_TEST_ADDRESS="$address" "$SERVER" "$mode" > "$log" 2>&1 &
	local pid=$!

	if ! wait_until $START_SECONDS is_listening $PORT; then
		echo "the $mode server did not start in $address" >&2
		kill -KILL $pid 2> /dev/null
		tail -n 32 "$log" >&2
		return 1
	fi

	echo "$mode in $address:"

	local result=0
	CERVER_TEST_ADDRESS="$address" timeout $RUN_SECONDS "$CLIENT" "$CONNECTIONS" "$REQUESTS" "$BODY_SIZE" || result=1

	kill -TERM $pid 2> /dev/null
	if ! wait_until $START_SECONDS is_finished $pid; then
		echo "the $mode server did not stop in $address" >&2
		kill -KILL $pid 2> /dev/null
		result=1
	fi

	wait $pid 2> /dev/null || result=1

	if [ "$result" -ne 0 ]; then
		tail -n 32 "$log" >&2
	fi

	return $result
}

FAILED=0
RUNS=0
for address in "${ADDRESSES[@]}"; do
	for mode in "${MODES[@]}"; do
		RUNS=$(( RUNS + 1 ))
		run_mode "$mode" "$address" || FAILED=$(( FAILED + 1 ))
	done
done

echo "$RUNS runs - $FAILED failed"

[ "$FAILED" -eq 0 ]
