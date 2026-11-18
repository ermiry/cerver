#!/bin/bash

DOCKER=${DOCKER:-docker}
IMAGE=${IMAGE:-ermiry/cerver:fuzz}
BUILD=${BUILD:-1}
SECONDS_PER_TARGET=${SECONDS_PER_TARGET:-30}
MAX_LENGTH=${MAX_LENGTH:-16384}
ARTIFACTS=${ARTIFACTS:-fuzz-artifacts}
BACKENDS=${BACKENDS:-uring epoll}
BACKEND_TARGETS=(loop)

TARGETS=(request lenient pipeline loop multipart llhttp parts url json number writer scan jwt jwk differential tree receive packet client file datagram address serializer game frame handshake)

if [ $# -gt 0 ]; then
	TARGETS=("$@")
fi

if [ ! -f /.dockerenv ]; then
	if [ "$BUILD" -ne 0 ]; then
		$DOCKER build -t "$IMAGE" -f Dockerfile.fuzz . || exit 1
	fi

	mkdir -p "$ARTIFACTS"

	exec $DOCKER run --rm \
		--security-opt seccomp=unconfined \
		-v "$(pwd)/$ARTIFACTS:/opt/cerver/$ARTIFACTS" \
		-e SECONDS_PER_TARGET="$SECONDS_PER_TARGET" \
		-e MAX_LENGTH="$MAX_LENGTH" \
		-e ARTIFACTS="$ARTIFACTS" \
		-e BACKENDS="$BACKENDS" \
		-e UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}" \
		"$IMAGE" bash test/fuzz.sh "${TARGETS[@]}"
fi

RUNS=()
for target in "${TARGETS[@]}"; do
	if [[ " ${BACKEND_TARGETS[*]} " == *" $target "* ]]; then
		for backend in $BACKENDS; do
			RUNS+=("$target:$backend")
		done
	else
		RUNS+=("$target")
	fi
done

FAILED=()
for run in "${RUNS[@]}"; do
	target=${run%%:*}
	backend=""
	[ "$run" != "$target" ] && backend=${run#*:}
	name=${run/:/-}

	echo "Fuzzing $name for $SECONDS_PER_TARGET seconds..."

	corpus="/tmp/fuzz-corpus/$name"
	mkdir -p "$corpus" "$ARTIFACTS"
	cp test/fuzz/corpus/$target/* "$corpus/" 2> /dev/null

	if ! CERVER_TEST_BACKEND=$backend ./test/bin/fuzz/$target "$corpus" \
		-max_total_time="$SECONDS_PER_TARGET" \
		-max_len="$MAX_LENGTH" \
		-timeout=10 \
		-print_final_stats=1 \
		-artifact_prefix="$ARTIFACTS/$name-" > "/tmp/fuzz-$name.log" 2>&1; then
		FAILED+=("$name")
		tail -n 64 "/tmp/fuzz-$name.log"
	else
		grep -E "stat::number_of_executed_units|stat::new_units_added" "/tmp/fuzz-$name.log"
	fi
done

printf "\n%d targets - %d failed\n" "${#RUNS[@]}" "${#FAILED[@]}"

if [ ${#FAILED[@]} -gt 0 ]; then
	printf "failed: %s\n" "${FAILED[*]}"
	exit 1
fi
