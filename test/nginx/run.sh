#!/bin/bash

DOCKER=${DOCKER:-docker}
IMAGE=${IMAGE:-ermiry/cerver:test}
BUILD=${BUILD:-1}

PROJECT=cerver-nginx
COMPOSE="$DOCKER compose -p $PROJECT -f test/nginx/docker-compose.yml"

NGINX_PORT=80
READY_SECONDS=32
PATH_UNDER_TEST=/legacy

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

IMAGE="$IMAGE" $COMPOSE up -d || exit 1

NGINX=$($COMPOSE ps -q nginx)

run_in_nginx () {
	$DOCKER run --rm --network "container:$NGINX" "$IMAGE" bash -c "$1"
}

run_in_nginx "for i in \$(seq $(( READY_SECONDS * 10 ))); do (exec 3<>/dev/tcp/127.0.0.1/$NGINX_PORT) 2> /dev/null && exit 0; sleep 0.1; done; exit 1" \
	|| { echo "nginx is not listening"; $COMPOSE logs; exit 1; }

FAILED=0
REQUESTS=$(( THREADS * REQUESTS_PER_THREAD ))
for (( round = 1; round <= ROUNDS; round++ )); do
	result=$(run_in_nginx "./benchmarks/bin/load $NGINX_PORT $THREADS $REQUESTS GET $PATH_UNDER_TEST")
	echo "round $round - $result"

	other=$(echo "$result" | sed -E 's/.* - other ([0-9]+).*/\1/')
	failed=$(echo "$result" | sed -E 's/.* - failed ([0-9]+).*/\1/')
	[ "$other" -eq 0 ] && [ "$failed" -eq 0 ] || FAILED=1

	[ $round -lt $ROUNDS ] && sleep $PAUSE_SECONDS
done

RESETS=$($COMPOSE logs nginx 2>&1 | grep -c -E "Connection reset by peer|upstream prematurely closed")
echo "nginx upstream errors: $RESETS"
[ "$RESETS" -eq 0 ] || FAILED=1

if [ $FAILED -ne 0 ]; then
	$COMPOSE logs --tail 64
	echo "nginx: failed"
	exit 1
fi

echo "nginx: ok"
