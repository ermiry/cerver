#!/bin/bash

DOCKER=${DOCKER:-docker}
BUILD=${BUILD:-1}
IMAGE_PREFIX=${IMAGE_PREFIX:-ermiry/cerver:test}

VARIANTS=(${VARIANTS:-normal address,undefined thread valgrind})
ADDRESSES=(${ADDRESSES:-127.0.0.1 ::1})

DISCONNECT_LEVEL=${DISCONNECT_LEVEL:-full}
DISCONNECT_SEED=${DISCONNECT_SEED:-}
VARIANT_SECONDS=${VARIANT_SECONDS:-3600}

VALGRIND="valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=definite,indirect --quiet"

RUN_FLAGS=(
	--rm
	--security-opt seccomp=unconfined
	--sysctl net.ipv6.conf.all.disable_ipv6=0
	-e DISCONNECT_ONLY=1
	-e DISCONNECT_SEED="$DISCONNECT_SEED"
	-e ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=0}"
	-e UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
	-e TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1}"
)

variant_sanitizer () {
	case "$1" in
		normal|valgrind) echo "" ;;
		*) echo "$1" ;;
	esac
}

variant_image () {
	local sanitizer=$(variant_sanitizer "$1")

	if [ -n "$sanitizer" ]; then
		echo "$IMAGE_PREFIX-$(echo "$sanitizer" | tr ',' '-')"
	else
		echo "$IMAGE_PREFIX"
	fi
}

variant_level () {
	case "$1" in
		valgrind) echo "sample" ;;
		*) echo "$DISCONNECT_LEVEL" ;;
	esac
}

variant_runner () {
	case "$1" in
		valgrind) echo "$VALGRIND" ;;
		*) echo "" ;;
	esac
}

variant_build () {
	local image=$(variant_image "$1")

	echo "Building $image..."
	$DOCKER build -t "$image" --build-arg SANITIZER="$(variant_sanitizer "$1")" -f Dockerfile.test . > /dev/null || return 1
}

variant_install () {
	echo "apt-get update > /dev/null && apt-get install -y valgrind > /dev/null"
}

variant_run () {
	local variant=$1
	local address=$2
	local image=$(variant_image "$variant")
	local command="timeout $VARIANT_SECONDS $(variant_runner "$variant") ./test/bin/http"

	if [ "$variant" = "valgrind" ]; then
		command="$(variant_install) && $command"
	fi

	echo "---- disconnect - $variant - $address - $(variant_level "$variant") ----"

	local start=$SECONDS

	$DOCKER run "${RUN_FLAGS[@]}" \
		-e CERVER_TEST_ADDRESS="$address" \
		-e DISCONNECT_LEVEL="$(variant_level "$variant")" \
		"$image" bash -c "$command"

	local result=$?

	echo "disconnect - $variant - $address: $([ $result -eq 0 ] && echo ok || echo failed) in $(( SECONDS - start ))s"

	return $result
}

FAILED=0
for variant in "${VARIANTS[@]}"; do
	if [ "$BUILD" -ne 0 ]; then
		variant_build "$variant" || { FAILED=1; continue; }
	fi

	for address in "${ADDRESSES[@]}"; do
		variant_run "$variant" "$address" || FAILED=1
	done
done

if [ $FAILED -ne 0 ]; then
	echo "disconnect: failed"
	exit 1
fi

echo "disconnect: ok"
