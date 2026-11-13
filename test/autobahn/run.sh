#!/bin/bash

DOCKER=${DOCKER:-docker}
SANITIZER=${SANITIZER:-}
BUILD=${BUILD:-1}
AUTOBAHN=${AUTOBAHN:-crossbario/autobahn-testsuite:25.10.1}
PYTHON_IMAGE=${PYTHON_IMAGE:-gcc:16.2}
REPORTS=${REPORTS:-autobahn-reports}
MODES=(${MODES:-server client})
NETWORK=${NETWORK:-cerver-autobahn}
WAIT_SECONDS=${WAIT_SECONDS:-60}

AUTOBAHN_PORT=9001
SERVER=cerver-autobahn-server
FUZZER=cerver-autobahn-fuzzer

TAG=test
if [ -n "$SANITIZER" ]; then
	TAG="$TAG-$(echo "$SANITIZER" | tr ',' '-')"
fi

IMAGE=${IMAGE:-ermiry/cerver:$TAG}

if [ "$BUILD" -ne 0 ]; then
	echo "Building $IMAGE..."
	$DOCKER build -t "$IMAGE" --build-arg SANITIZER="$SANITIZER" -f Dockerfile.test . || exit 1
fi

RUN_FLAGS=(
	--network "$NETWORK"
	--security-opt seccomp=unconfined
	-e ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=0}"
	-e UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
	-e TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1}"
)

cleanup () {
	$DOCKER rm -f "$SERVER" "$FUZZER" > /dev/null 2>&1
	$DOCKER network rm "$NETWORK" > /dev/null 2>&1
}

wait_listening () {
	local container=$1
	local port_hex
	port_hex=$(printf '%04X' "$AUTOBAHN_PORT")
	for _ in $(seq 1 "$WAIT_SECONDS"); do
		if $DOCKER exec "$container" grep -q ":$port_hex 00000000:0000 0A" /proc/net/tcp 2> /dev/null; then
			return 0
		fi
		sleep 1
	done
	echo "$container never listened"
	return 1
}

fuzzer_report () {
	echo "wstest: $($DOCKER inspect --format 'exit {{.State.ExitCode}} - out of memory {{.State.OOMKilled}} - running {{.State.Running}} {{.State.Error}}' "$FUZZER" 2>&1)"
	free -m 2> /dev/null
	(sudo -n dmesg 2> /dev/null || dmesg 2> /dev/null) | grep -i "out of memory\|killed process\|oom-kill" | tail -n 8
}

run_server () {
	echo "Autobahn fuzzingclient against the server of cerver..."

	$DOCKER run -d --name "$SERVER" --network-alias cerver "${RUN_FLAGS[@]}" \
		-e CERVER_TEST_ADDRESS=0.0.0.0 -w /opt/cerver/test/web \
		"$IMAGE" ../bin/web/autobahn > /dev/null || return 1

	wait_listening "$SERVER" || { $DOCKER logs "$SERVER" | tail -n 64; return 1; }

	$DOCKER run --name "$FUZZER" --platform linux/amd64 --network "$NETWORK" \
		-v "$(pwd)/test/autobahn:/config" -v "$(pwd)/$REPORTS:/reports" \
		"$AUTOBAHN" wstest -m fuzzingclient -s /config/fuzzingclient.json > "$REPORTS/server.log" 2>&1
	local result=$?

	if [ "$result" -ne 0 ]; then
		fuzzer_report
	fi

	$DOCKER rm -f "$FUZZER" > /dev/null

	$DOCKER stop -t 30 "$SERVER" > /dev/null
	local status
	status=$($DOCKER wait "$SERVER")
	$DOCKER logs "$SERVER" > "$REPORTS/server-cerver.log" 2>&1
	$DOCKER rm "$SERVER" > /dev/null

	if [ "$result" -ne 0 ] || [ "$status" -ne 0 ]; then
		echo "the server failed: wstest $result, cerver $status"
		tail -n 64 "$REPORTS/server-cerver.log"
		return 1
	fi
}

run_client () {
	echo "Autobahn fuzzingserver against the client of cerver..."

	$DOCKER run -d --name "$FUZZER" --platform linux/amd64 --network "$NETWORK" --network-alias autobahn \
		-v "$(pwd)/test/autobahn:/config" -v "$(pwd)/$REPORTS:/reports" \
		"$AUTOBAHN" wstest -m fuzzingserver -s /config/fuzzingserver.json > /dev/null || return 1

	wait_listening "$FUZZER" || { $DOCKER logs "$FUZZER" | tail -n 64; return 1; }

	$DOCKER run --rm "${RUN_FLAGS[@]}" -e AUTOBAHN_URL="ws://autobahn:$AUTOBAHN_PORT" \
		"$IMAGE" ./test/bin/client/web/autobahn > "$REPORTS/client-cerver.log" 2>&1
	local result=$?

	$DOCKER logs "$FUZZER" > "$REPORTS/client.log" 2>&1

	if [ "$result" -ne 0 ]; then
		fuzzer_report
	fi

	$DOCKER rm -f "$FUZZER" > /dev/null

	if [ "$result" -ne 0 ]; then
		echo "the client failed: $result"
		tail -n 64 "$REPORTS/client-cerver.log"
		return 1
	fi
}

cleanup
rm -rf "$REPORTS"
mkdir -p "$REPORTS"
chmod 777 "$REPORTS"

$DOCKER network create "$NETWORK" > /dev/null || exit 1

FAILED=()
INDEXES=()
for mode in "${MODES[@]}"; do
	if "run_$mode"; then
		INDEXES+=("$REPORTS/$mode/index.json")
	else
		FAILED+=("$mode")
	fi
done

cleanup

if [ ${#INDEXES[@]} -gt 0 ]; then
	if ! $DOCKER run --rm -v "$(pwd):/src" -w /src "$PYTHON_IMAGE" python3 test/autobahn/check.py "${INDEXES[@]}"; then
		FAILED+=("reports")
	fi
fi

if [ ${#FAILED[@]} -gt 0 ]; then
	echo "autobahn failed: ${FAILED[*]}"
	exit 1
fi

echo "autobahn passed"
