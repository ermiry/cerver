#!/bin/bash

DOCKER=${DOCKER:-docker}
IMAGE=${IMAGE:-ermiry/cerver:test}
BUILD=${BUILD:-1}

NGINX_IMAGES=(${NGINX_IMAGES:-nginx:1.31.6-alpine nginx:1.30.6-alpine})
CONFIGS=(${CONFIGS:-direct services pair restart})
BACKENDS=(${BACKENDS:-uring epoll})

PROJECT=${PROJECT:-cerver-nginx}
COMPOSE="$DOCKER compose -p $PROJECT -f test/nginx/docker-compose.yml"

NGINX_PORT=80
READY_SECONDS=32
PATHS_UNDER_TEST=(/features/small /plain/small)
UNBUFFERED_PREFIX=/unbuffered
WEBSOCKET_PREFIX=/websocket

THREADS=8
REQUESTS_PER_THREAD=160
ROUNDS=3
PAUSE_SECONDS=10

METRICS_PATH=/features/cerver/metrics
REUSED_METRIC=cerver_http_reused_connections_total
REUSED_MINIMUM_PERCENT=90

RESTART_SERVICES=(cerver cerver-2)
RESTART_PATH=/features/small
RESTART_ROUNDS=2
RESTART_TIMEOUT=16
RESTART_PAUSE_SECONDS=2
RESTART_REQUESTS_PER_THREAD=64

if [ "$BUILD" -ne 0 ]; then
	$DOCKER build -t "$IMAGE" -f Dockerfile.test . || exit 1
fi

cleanup () {
	$COMPOSE down --timeout 8 > /dev/null 2>&1
}

trap cleanup EXIT

run_in_nginx () {
	$DOCKER run --rm --network "container:$NGINX" "$@" "$IMAGE" bash -c "$COMMAND"
}

nginx_wait () {
	COMMAND="for i in \$(seq $(( READY_SECONDS * 10 ))); do (exec 3<>/dev/tcp/127.0.0.1/$NGINX_PORT) 2> /dev/null && exit 0; sleep 0.1; done; exit 1"
	run_in_nginx
}

nginx_features_at () {
	COMMAND="./test/bin/client/web/features"
	run_in_nginx \
		-e CERVER_TEST_ADDRESS="$1" \
		-e FEATURES_ADDRESS="http://$2:$NGINX_PORT" \
		-e FEATURES_PROXY=1 \
		-e FEATURES_UNBUFFERED_PREFIX="$UNBUFFERED_PREFIX"
}

nginx_features () {
	nginx_features_at 127.0.0.1 127.0.0.1 && nginx_features_at ::1 "[::1]"
}

nginx_load () {
	local failed=0
	local requests=$(( THREADS * REQUESTS_PER_THREAD ))

	for (( round = 1; round <= ROUNDS; round++ )); do
		for path in "${PATHS_UNDER_TEST[@]}"; do
			COMMAND="./benchmarks/bin/load $NGINX_PORT $THREADS $requests GET $path"
			result=$(run_in_nginx)
			echo "round $round - $path - $result"

			other=$(echo "$result" | sed -E 's/.* - other ([0-9]+).*/\1/')
			errors=$(echo "$result" | sed -E 's/.* - failed ([0-9]+).*/\1/')
			[ "$other" -eq 0 ] && [ "$errors" -eq 0 ] || failed=1
		done

		[ $round -lt $ROUNDS ] && sleep $PAUSE_SECONDS
	done

	return $failed
}

nginx_metric () {
	COMMAND="exec 3<>/dev/tcp/127.0.0.1/$NGINX_PORT; printf 'GET $METRICS_PATH HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' >&3; cat <&3"
	run_in_nginx | tr -d '\r' | sed -nE "s/^$1 ([0-9]+)$/\1/p"
}

# the requests of the load that reached the cerver through a connection that it had kept: most of them
nginx_reuse () {
	local reused=$(nginx_metric $REUSED_METRIC)
	local requests=$(( THREADS * REQUESTS_PER_THREAD * ROUNDS * ${#PATHS_UNDER_TEST[@]} ))

	echo "$NGINX_IMAGE: $reused requests through connections that the cerver kept, the load sent $requests"

	[[ "$reused" =~ ^[0-9]+$ ]] || return 1

	[ "$reused" -ge $(( requests * REUSED_MINIMUM_PERCENT / 100 )) ]
}

nginx_restarts () {
	for (( round = 0; round < RESTART_ROUNDS; round++ )); do
		for service in "${RESTART_SERVICES[@]}"; do
			$COMPOSE restart --timeout $RESTART_TIMEOUT "$service" > /dev/null 2>&1
			sleep $RESTART_PAUSE_SECONDS
		done
	done
}

nginx_restart_ready () {
	COMMAND="for i in \$(seq $READY_SECONDS); do ./benchmarks/bin/load $NGINX_PORT 1 ${#RESTART_SERVICES[@]} GET $RESTART_PATH > /dev/null && exit 0; sleep 1; done; exit 1"
	run_in_nginx
}

# the cervers restart one at a time while a load goes through nginx: each one drains what it has
# and nginx sends the rest to the other one, so every request is answered
nginx_restart () {
	local failed=0 loads=0
	local requests=$(( THREADS * RESTART_REQUESTS_PER_THREAD ))

	nginx_restart_ready || { echo "$NGINX_IMAGE: the cervers did not answer through nginx"; return 1; }

	nginx_restarts &
	local restarts=$!

	while kill -0 $restarts 2> /dev/null; do
		COMMAND="./benchmarks/bin/load $NGINX_PORT $THREADS $requests GET $RESTART_PATH"
		result=$(run_in_nginx)
		loads=$(( loads + 1 ))

		other=$(echo "$result" | sed -E 's/.* - other ([0-9]+).*/\1/')
		errors=$(echo "$result" | sed -E 's/.* - failed ([0-9]+).*/\1/')
		if [ "$other" != "0" ] || [ "$errors" != "0" ]; then
			echo "load $loads - $result"
			failed=1
		fi
	done

	wait $restarts

	resets=$($COMPOSE logs nginx 2>&1 | grep -c "Connection reset by peer")
	closed=$($COMPOSE logs nginx 2>&1 | grep -c "upstream prematurely closed")
	echo "$NGINX_IMAGE: $loads loads of $requests requests while the cervers restarted - upstream resets: $resets - upstream closes before a response: $closed"
	[ "$resets" -eq 0 ] || failed=1

	return $failed
}

nginx_config_file () {
	case "$1" in
		services) echo "services.conf" ;;
		pair) echo "pair.conf" ;;
		restart) echo "restart.conf" ;;
		*) echo "nginx.conf" ;;
	esac
}

nginx_config_instances () {
	case "$1" in
		pair) echo "cerver-disconnect,cerver-disconnect-2" ;;
		*) echo "cerver-disconnect" ;;
	esac
}

nginx_config_profiles () {
	case "$1" in
		pair) echo "pair" ;;
		restart) echo "restart" ;;
		*) echo "" ;;
	esac
}

nginx_disconnect_at () {
	COMMAND="./test/bin/client/web/disconnect"
	run_in_nginx \
		-e CERVER_TEST_ADDRESS="$1" \
		-e DISCONNECT_CONFIG="$CONFIG" \
		-e DISCONNECT_INSTANCES="$(nginx_config_instances "$CONFIG")"
}

nginx_disconnect () {
	nginx_disconnect_at 127.0.0.1 && nginx_disconnect_at ::1
}

nginx_websocket_at () {
	COMMAND="./test/bin/client/web/websocket"
	run_in_nginx \
		-e WEBSOCKET_HOST="$1" \
		-e WEBSOCKET_PORT="$NGINX_PORT" \
		-e WEBSOCKET_PREFIX="$WEBSOCKET_PREFIX"
}

nginx_websocket () {
	nginx_websocket_at 127.0.0.1 && nginx_websocket_at ::1
}

nginx_direct () {
	local failed=0

	nginx_features || { echo "$NGINX_IMAGE: the features client failed"; failed=1; }

	nginx_websocket || { echo "$NGINX_IMAGE: the websocket client failed"; failed=1; }

	nginx_load || failed=1

	nginx_reuse || { echo "$NGINX_IMAGE: the connections that the cerver kept are not the expected ones"; failed=1; }

	resets=$($COMPOSE logs nginx 2>&1 | grep -c -E "Connection reset by peer|upstream prematurely closed")
	echo "$NGINX_IMAGE: upstream errors: $resets"
	[ "$resets" -eq 0 ] || failed=1

	return $failed
}

nginx_run () {
	local failed=0

	echo "---- $NGINX_IMAGE - $CONFIG - $BACKEND ----"

	export IMAGE NGINX_IMAGE
	export CERVER_TEST_BACKEND="$BACKEND"
	export NGINX_CONF="$(nginx_config_file "$CONFIG")"
	export COMPOSE_PROFILES="$(nginx_config_profiles "$CONFIG")"

	$COMPOSE up -d || return 1

	NGINX=$($COMPOSE ps -q nginx)

	nginx_wait || { echo "nginx is not listening"; $COMPOSE logs; cleanup; return 1; }

	case "$CONFIG" in
		direct)
			nginx_direct || failed=1
			nginx_disconnect || { echo "$NGINX_IMAGE - $CONFIG: the disconnect client failed"; failed=1; }
			;;
		restart)
			nginx_restart || { echo "$NGINX_IMAGE - $CONFIG: a request failed while the cervers restarted"; failed=1; }
			;;
		*)
			nginx_disconnect || { echo "$NGINX_IMAGE - $CONFIG: the disconnect client failed"; failed=1; }
			;;
	esac

	[ $failed -eq 0 ] || $COMPOSE logs --tail 64

	cleanup

	return $failed
}

FAILED=0
for NGINX_IMAGE in "${NGINX_IMAGES[@]}"; do
	for CONFIG in "${CONFIGS[@]}"; do
		for BACKEND in "${BACKENDS[@]}"; do
			nginx_run || { echo "$NGINX_IMAGE - $CONFIG - $BACKEND: failed"; FAILED=1; }
		done
	done
done

if [ $FAILED -ne 0 ]; then
	echo "nginx: failed"
	exit 1
fi

echo "nginx: ok"
