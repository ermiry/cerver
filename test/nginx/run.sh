#!/bin/bash

DOCKER=${DOCKER:-docker}
IMAGE=${IMAGE:-ermiry/cerver:test}
BUILD=${BUILD:-1}

NGINX_IMAGES=(${NGINX_IMAGES:-nginx:1.31.6-alpine nginx:1.30.6-alpine})

PROJECT=cerver-nginx
COMPOSE="$DOCKER compose -p $PROJECT -f test/nginx/docker-compose.yml"

NGINX_PORT=80
READY_SECONDS=32
PATHS_UNDER_TEST=(/features/small /plain/small)
UNBUFFERED_PREFIX=/unbuffered

THREADS=8
REQUESTS_PER_THREAD=160
ROUNDS=3
PAUSE_SECONDS=10

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

nginx_features () {
	COMMAND="./test/bin/client/web/features"
	run_in_nginx \
		-e FEATURES_ADDRESS="http://127.0.0.1:$NGINX_PORT" \
		-e FEATURES_PROXY=1 \
		-e FEATURES_UNBUFFERED_PREFIX="$UNBUFFERED_PREFIX"
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

nginx_run () {
	local failed=0

	echo "---- $NGINX_IMAGE ----"

	IMAGE="$IMAGE" NGINX_IMAGE="$NGINX_IMAGE" $COMPOSE up -d || return 1

	NGINX=$($COMPOSE ps -q nginx)

	nginx_wait || { echo "nginx is not listening"; $COMPOSE logs; return 1; }

	nginx_features || { echo "$NGINX_IMAGE: the features client failed"; failed=1; }

	nginx_load || failed=1

	resets=$($COMPOSE logs nginx 2>&1 | grep -c -E "Connection reset by peer|upstream prematurely closed")
	echo "$NGINX_IMAGE: upstream errors: $resets"
	[ "$resets" -eq 0 ] || failed=1

	[ $failed -eq 0 ] || $COMPOSE logs --tail 64

	cleanup

	return $failed
}

FAILED=0
for NGINX_IMAGE in "${NGINX_IMAGES[@]}"; do
	nginx_run || { echo "$NGINX_IMAGE: failed"; FAILED=1; }
done

if [ $FAILED -ne 0 ]; then
	echo "nginx: failed"
	exit 1
fi

echo "nginx: ok"
