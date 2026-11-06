#!/bin/bash

DOCKER=${DOCKER:-docker}
SANITIZER=${SANITIZER:-}
BUILD=${BUILD:-1}
CLIENT_SECONDS=${CLIENT_SECONDS:-120}

PAIRS=(
	cerver/ping
	cerver/packets
	cerver/requests
	cerver/auth
	cerver/sessions
	cerver/threads
	handler
	web/web
	web/json
	web/auth
	web/api
	web/upload
	web/multi
	web/multiple
	web/jobs
	web/worker
	web/admin
	web/router
)

if [ -n "$SANITIZER" ]; then
	IMAGE=${IMAGE:-ermiry/cerver:test-$(echo "$SANITIZER" | tr ',' '-')}
else
	IMAGE=${IMAGE:-ermiry/cerver:test}
fi

if [ $# -gt 0 ]; then
	PAIRS=("$@")
fi

if [ "$BUILD" -ne 0 ]; then
	echo "Building $IMAGE..."
	$DOCKER build -t "$IMAGE" --build-arg SANITIZER="$SANITIZER" -f Dockerfile.test . || exit 1
fi

RUN_FLAGS=(
	--rm
	--cap-add=SYS_PTRACE
	--security-opt seccomp=unconfined
	-e CLIENT_SECONDS="$CLIENT_SECONDS"
	-e ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=0}"
	-e UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
	-e TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1}"
)

FAILED=()
for pair in "${PAIRS[@]}"; do
	echo "Integration test $pair..."
	if ! $DOCKER run "${RUN_FLAGS[@]}" "$IMAGE" bash test/pair.sh "$pair"; then
		FAILED+=("$pair")
	fi
done

printf "\n%d pairs - %d failed\n" "${#PAIRS[@]}" "${#FAILED[@]}"

if [ ${#FAILED[@]} -gt 0 ]; then
	printf "failed: %s\n" "${FAILED[*]}"
	exit 1
fi
