#!/bin/bash

TCP_PORT=7000
FIRST_SERVICE_PORT=7001
SECOND_SERVICE_PORT=7002
READY_SECONDS=16
CLIENT_SECONDS=${CLIENT_SECONDS:-120}
STOP_SECONDS=16
LOGS=${LOGS:-/tmp/pair}

ROOT=$(pwd)
PAIR="$1"

source "$ROOT/test/process.sh"

SERVERS=()
PORTS=()

balancer_servers () {
	SERVERS=(
		"test/bin/cerver/service -p $FIRST_SERVICE_PORT"
		"test/bin/cerver/service -p $SECOND_SERVICE_PORT"
		"test/bin/cerver/load --type $1"
	)
	PORTS=($FIRST_SERVICE_PORT $SECOND_SERVICE_PORT $TCP_PORT)
}

case "$PAIR" in
	cerver/*)
		SERVERS=("test/bin/cerver/${PAIR#cerver/}")
		PORTS=($TCP_PORT)
		CLIENT="test/bin/client/${PAIR#cerver/}"
		;;
	handler)
		SERVERS=("test/bin/handler/cerver")
		PORTS=($TCP_PORT)
		CLIENT="test/bin/handler/client"
		;;
	balancer/round_robin)
		balancer_servers round_robin
		CLIENT="test/bin/client/load/simple"
		;;
	balancer/handler_id)
		balancer_servers handler_id
		CLIENT="test/bin/client/load/id"
		;;
	*)
		echo "unknown pair: $PAIR"
		exit 2
		;;
esac

mkdir -p "$LOGS"
NAME=$(echo "$PAIR" | tr '/' '-')
CLIENT_LOG="$LOGS/$NAME-client.log"

SERVER_PIDS=()
SERVER_LOGS=()

report () {
	for index in "${!SERVER_LOGS[@]}"; do
		echo "---- $PAIR server $index log ----"
		tail -n 64 "${SERVER_LOGS[$index]}"
	done
	echo "---- $PAIR client log ----"
	tail -n 64 "$CLIENT_LOG"
}

servers_stacks () {
	for index in "${!SERVER_PIDS[@]}"; do
		stacks "${SERVER_PIDS[$index]}" "server $index"
	done
}

servers_kill () {
	for pid in "${SERVER_PIDS[@]}"; do
		kill -KILL "$pid" 2> /dev/null
	done
}

for index in "${!SERVERS[@]}"; do
	SERVER_LOG="$LOGS/$NAME-server-$index.log"
	SERVER_LOGS+=("$SERVER_LOG")

	$ROOT/${SERVERS[$index]} > "$SERVER_LOG" 2>&1 &
	SERVER_PIDS+=($!)

	if ! wait_until $READY_SECONDS is_listening "${PORTS[$index]}"; then
		echo "$PAIR: the server $index is not listening on ${PORTS[$index]} after $READY_SECONDS seconds"
		servers_stacks
		servers_kill
		report
		exit 1
	fi
done

"$ROOT/$CLIENT" > "$CLIENT_LOG" 2>&1 &
CLIENT_PID=$!

CLIENT_HUNG=0
if ! wait_until "$CLIENT_SECONDS" is_finished $CLIENT_PID; then
	CLIENT_HUNG=1
	echo "$PAIR: the client did not end after $CLIENT_SECONDS seconds"
	stacks $CLIENT_PID "client"
	servers_stacks
	kill -KILL $CLIENT_PID 2> /dev/null
fi

wait $CLIENT_PID
CLIENT_CODE=$?

SERVER_HUNG=0
SERVER_CODES=()
for (( index = ${#SERVER_PIDS[@]} - 1; index >= 0; index-- )); do
	pid="${SERVER_PIDS[$index]}"
	if is_alive "$pid"; then
		kill -INT "$pid"
		if ! wait_until $STOP_SECONDS is_finished "$pid"; then
			SERVER_HUNG=1
			echo "$PAIR: the server $index did not stop after $STOP_SECONDS seconds"
			stacks "$pid" "server $index"
			kill -KILL "$pid" 2> /dev/null
		fi
	fi

	wait "$pid"
	SERVER_CODES[$index]=$?
done

SERVER_FAILED=0
for code in "${SERVER_CODES[@]}"; do
	if [ "$code" -ne 0 ]; then
		SERVER_FAILED=1
	fi
done

if [ $CLIENT_HUNG -ne 0 ] || [ $SERVER_HUNG -ne 0 ] || [ $CLIENT_CODE -ne 0 ] || [ $SERVER_FAILED -ne 0 ]; then
	echo "$PAIR: failed - client $CLIENT_CODE - servers ${SERVER_CODES[*]}"
	report
	exit 1
fi

echo "$PAIR: ok"
