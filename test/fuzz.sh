#!/bin/bash

DOCKER=${DOCKER:-docker}
IMAGE=${IMAGE:-ermiry/cerver:fuzz}
BUILD=${BUILD:-1}
SECONDS_PER_TARGET=${SECONDS_PER_TARGET:-30}
MAX_LENGTH=${MAX_LENGTH:-16384}
ARTIFACTS=${ARTIFACTS:-fuzz-artifacts}

TARGETS=(request multipart url json jwt tree receive packet client file datagram address serializer game)

if [ $# -gt 0 ]; then
	TARGETS=("$@")
fi

if [ ! -f /.dockerenv ]; then
	if [ "$BUILD" -ne 0 ]; then
		$DOCKER build -t "$IMAGE" -f Dockerfile.fuzz . || exit 1
	fi

	mkdir -p "$ARTIFACTS"

	exec $DOCKER run --rm \
		-v "$(pwd)/$ARTIFACTS:/opt/cerver/$ARTIFACTS" \
		-e SECONDS_PER_TARGET="$SECONDS_PER_TARGET" \
		-e MAX_LENGTH="$MAX_LENGTH" \
		-e ARTIFACTS="$ARTIFACTS" \
		"$IMAGE" bash test/fuzz.sh "${TARGETS[@]}"
fi

FAILED=()
for target in "${TARGETS[@]}"; do
	echo "Fuzzing $target for $SECONDS_PER_TARGET seconds..."

	corpus="/tmp/fuzz-corpus/$target"
	mkdir -p "$corpus" "$ARTIFACTS"
	cp test/fuzz/corpus/$target/* "$corpus/" 2> /dev/null

	if ! ./test/bin/fuzz/$target "$corpus" \
		-max_total_time="$SECONDS_PER_TARGET" \
		-max_len="$MAX_LENGTH" \
		-timeout=10 \
		-print_final_stats=1 \
		-artifact_prefix="$ARTIFACTS/$target-" > "/tmp/fuzz-$target.log" 2>&1; then
		FAILED+=("$target")
		tail -n 64 "/tmp/fuzz-$target.log"
	else
		grep -E "stat::number_of_executed_units|stat::new_units_added" "/tmp/fuzz-$target.log"
	fi
done

printf "\n%d targets - %d failed\n" "${#TARGETS[@]}" "${#FAILED[@]}"

if [ ${#FAILED[@]} -gt 0 ]; then
	printf "failed: %s\n" "${FAILED[*]}"
	exit 1
fi
