#!/bin/bash

TCP_PORT=7000
WEB_PORT=8080
READY_SECONDS=16
CLIENT_SECONDS=${CLIENT_SECONDS:-120}
STOP_SECONDS=16
LOGS=${LOGS:-/tmp/pair}

ROOT=$(pwd)
PAIR="$1"

source "$ROOT/test/process.sh"

case "$PAIR" in
	cerver/*)
		SERVER="test/bin/cerver/${PAIR#cerver/}"
		CLIENT="test/bin/client/${PAIR#cerver/}"
		PORT=$TCP_PORT
		DIRECTORY="."
		;;
	handler)
		SERVER="test/bin/handler/cerver"
		CLIENT="test/bin/handler/client"
		PORT=$TCP_PORT
		DIRECTORY="."
		;;
	web/*)
		SERVER="test/bin/web/${PAIR#web/}"
		CLIENT="test/bin/client/web/${PAIR#web/}"
		PORT=$WEB_PORT
		DIRECTORY="test/web"
		;;
	*)
		echo "unknown pair: $PAIR"
		exit 2
		;;
esac

mkdir -p "$LOGS"
NAME=$(echo "$PAIR" | tr '/' '-')
SERVER_LOG="$LOGS/$NAME-server.log"
CLIENT_LOG="$LOGS/$NAME-client.log"

report () {
	echo "---- $PAIR server log ----"
	tail -n 64 "$SERVER_LOG"
	echo "---- $PAIR client log ----"
	tail -n 64 "$CLIENT_LOG"
}

(cd "$DIRECTORY" && exec "$ROOT/$SERVER") > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!

if ! wait_until $READY_SECONDS is_listening $PORT; then
	echo "$PAIR: the server is not listening on $PORT after $READY_SECONDS seconds"
	stacks $SERVER_PID "server"
	kill -KILL $SERVER_PID 2> /dev/null
	report
	exit 1
fi

"$ROOT/$CLIENT" > "$CLIENT_LOG" 2>&1 &
CLIENT_PID=$!

CLIENT_HUNG=0
if ! wait_until "$CLIENT_SECONDS" is_finished $CLIENT_PID; then
	CLIENT_HUNG=1
	echo "$PAIR: the client did not end after $CLIENT_SECONDS seconds"
	stacks $CLIENT_PID "client"
	stacks $SERVER_PID "server"
	kill -KILL $CLIENT_PID 2> /dev/null
fi

wait $CLIENT_PID
CLIENT_CODE=$?

SERVER_HUNG=0
if is_alive $SERVER_PID; then
	kill -INT $SERVER_PID
	if ! wait_until $STOP_SECONDS is_finished $SERVER_PID; then
		SERVER_HUNG=1
		echo "$PAIR: the server did not stop after $STOP_SECONDS seconds"
		stacks $SERVER_PID "server"
		kill -KILL $SERVER_PID 2> /dev/null
	fi
fi

wait $SERVER_PID
SERVER_CODE=$?

if [ $CLIENT_HUNG -ne 0 ] || [ $SERVER_HUNG -ne 0 ] || [ $CLIENT_CODE -ne 0 ] || [ $SERVER_CODE -ne 0 ]; then
	echo "$PAIR: failed - client $CLIENT_CODE - server $SERVER_CODE"
	report
	exit 1
fi

echo "$PAIR: ok"
