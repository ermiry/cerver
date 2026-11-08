#!/bin/bash

DOCKER=${DOCKER:-docker}
SANITIZER=${SANITIZER:-}
BUILD=${BUILD:-1}
RUN_SECONDS=${RUN_SECONDS:-120}
LOGS=${LOGS:-/tmp/repeat}

TARGET="$1"
COUNT="${2:-100}"

if [ -z "$TARGET" ]; then
	echo "usage: test/repeat.sh <test binary | pair> [count]"
	exit 2
fi

if [ ! -f /.dockerenv ]; then
	if [ -n "$SANITIZER" ]; then
		IMAGE=${IMAGE:-ermiry/cerver:test-$(echo "$SANITIZER" | tr ',' '-')}
	else
		IMAGE=${IMAGE:-ermiry/cerver:test}
	fi

	if [ "$BUILD" -ne 0 ]; then
		$DOCKER build -t "$IMAGE" --build-arg SANITIZER="$SANITIZER" -f Dockerfile.test . || exit 1
	fi

	exec $DOCKER run --rm \
		--cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
		--add-host service-1:127.0.0.1 --add-host service-2:127.0.0.1 \
		-e ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
		-e UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}" \
		-e TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1}" \
		-e RUN_SECONDS="$RUN_SECONDS" \
		"$IMAGE" bash test/repeat.sh "$TARGET" "$COUNT"
fi

source test/process.sh

mkdir -p "$LOGS"

run_binary () {
	local log="$LOGS/run.log"

	"./$TARGET" > "$log" 2>&1 &
	local pid=$!

	if ! wait_until "$RUN_SECONDS" is_finished $pid; then
		echo "$TARGET did not end after $RUN_SECONDS seconds"
		stacks $pid "$TARGET"
		kill -KILL $pid 2> /dev/null
		wait $pid
		tail -n 64 "$log"
		return 1
	fi

	wait $pid
	local code=$?

	if [ $code -ne 0 ]; then
		tail -n 64 "$log"
	fi

	return $code
}

run_pair () {
	CLIENT_SECONDS="$RUN_SECONDS" bash test/pair.sh "$TARGET" > "$LOGS/pair.log" 2>&1
	local code=$?

	if [ $code -ne 0 ]; then
		cat "$LOGS/pair.log"
	fi

	return $code
}

for (( run = 1; run <= COUNT; run++ )); do
	if [ -x "./$TARGET" ]; then
		run_binary
	else
		run_pair
	fi

	if [ $? -ne 0 ]; then
		echo "$TARGET failed in run $run of $COUNT"
		exit 1
	fi
done

echo "$TARGET passed $COUNT runs"
