#!/bin/bash

DOCKER=${DOCKER:-docker}
SANITIZER=${SANITIZER:-}
BUILD=${BUILD:-1}
CLIENT_SECONDS=${CLIENT_SECONDS:-120}

COVERAGE=${COVERAGE:-0}
COVERAGE_VOLUME=${COVERAGE_VOLUME:-cerver-coverage}
COVERAGE_OUTPUT=${COVERAGE_OUTPUT:-$(pwd)/coverage-integration}
COVERAGE_DATA=/coverage-data
COVERAGE_DESTINATION=/coverage-output
IMAGE_ROOT=/opt/cerver

PAIRS=(
	cerver/ping
	cerver/packets
	cerver/requests
	cerver/schemas
	cerver/auth
	cerver/sessions
	cerver/threads
	cerver/queue
	cerver/udp
	cerver/game
	handler
	balancer/round_robin
	balancer/handler_id
	balancer/custom
	balancer/reconnect
	balancer/large
	balancer/unavailable
	balancer/concurrent
	balancer/abandon
)

EXAMPLE_PAIRS=(
	examples/welcome
	examples/test
	examples/handlers
	examples/requests
	examples/threads
)

EXAMPLES_ADDRESS=127.0.0.1

ADDRESSES=(${ADDRESSES:-127.0.0.1 ::1})

TAG=test

if [ -n "$SANITIZER" ]; then
	TAG="$TAG-$(echo "$SANITIZER" | tr ',' '-')"
fi

if [ "$COVERAGE" -ne 0 ]; then
	TAG="$TAG-coverage"
fi

IMAGE=${IMAGE:-ermiry/cerver:$TAG}

if [ $# -gt 0 ]; then
	PAIRS=("$@")
	EXAMPLE_PAIRS=()
fi

if [ "$BUILD" -ne 0 ]; then
	echo "Building $IMAGE..."
	$DOCKER build -t "$IMAGE" --build-arg SANITIZER="$SANITIZER" --build-arg COVERAGE="$COVERAGE" -f Dockerfile.test . || exit 1
fi

RUN_FLAGS=(
	--rm
	--cap-add=SYS_PTRACE
	--security-opt seccomp=unconfined
	--sysctl net.ipv6.conf.all.disable_ipv6=0
	-e CLIENT_SECONDS="$CLIENT_SECONDS"
	-e ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=0}"
	-e UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
	-e TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1}"
)

coverage_init () {
	$DOCKER volume rm -f "$COVERAGE_VOLUME" > /dev/null
	$DOCKER volume create "$COVERAGE_VOLUME" > /dev/null || exit 1

	RUN_FLAGS+=(
		-e GCOV_PREFIX="$COVERAGE_DATA"
		-v "$COVERAGE_VOLUME:$COVERAGE_DATA"
	)
}

coverage_report () {
	echo "Generating integration coverage in $COVERAGE_OUTPUT..."

	mkdir -p "$COVERAGE_OUTPUT"

	$DOCKER run --rm \
		-v "$COVERAGE_VOLUME:$COVERAGE_DATA" \
		-v "$COVERAGE_OUTPUT:$COVERAGE_DESTINATION" \
		"$IMAGE" bash -c "
			cp -r $COVERAGE_DATA$IMAGE_ROOT/. $IMAGE_ROOT/ \
			&& { make TYPE=test COVERAGE=1 coverage > /tmp/coverage.log 2>&1 || { tail -n 32 /tmp/coverage.log; exit 1; }; } \
			&& rm -rf $COVERAGE_DESTINATION/* \
			&& cp -r coverage/. $COVERAGE_DESTINATION/
		" || exit 1

	$DOCKER volume rm -f "$COVERAGE_VOLUME" > /dev/null
}

if [ "$COVERAGE" -ne 0 ]; then
	coverage_init
fi

FAILED=()
RUNS=0
for address in "${ADDRESSES[@]}"; do
	ADDRESS_PAIRS=("${PAIRS[@]}")
	if [ "$address" = "$EXAMPLES_ADDRESS" ]; then
		ADDRESS_PAIRS+=("${EXAMPLE_PAIRS[@]}")
	fi

	for pair in "${ADDRESS_PAIRS[@]}"; do
		echo "Integration test $pair in $address..."
		RUNS=$(( RUNS + 1 ))
		if ! $DOCKER run "${RUN_FLAGS[@]}" \
			--add-host "service-1:$address" --add-host "service-2:$address" \
			-e CERVER_TEST_ADDRESS="$address" \
			"$IMAGE" bash test/pair.sh "$pair"; then
			FAILED+=("$pair@$address")
		fi
	done
done

if [ "$COVERAGE" -ne 0 ]; then
	coverage_report
fi

printf "\n%d pairs - %d failed\n" "$RUNS" "${#FAILED[@]}"

if [ ${#FAILED[@]} -gt 0 ]; then
	printf "failed: %s\n" "${FAILED[*]}"
	exit 1
fi
