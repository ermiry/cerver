#!/bin/bash

DOCKER=${DOCKER:-docker}
IMAGE=${IMAGE:-ermiry/cerver:test}
BUILD=${BUILD:-1}

NGINX_IMAGES=(${NGINX_IMAGES:-nginx:1.31.6-alpine nginx:1.30.6-alpine})
HANDLERS=(${HANDLERS:-threads keep})
KTLS_MODES=(${KTLS_MODES:-off on})

PROJECT=${PROJECT:-cerver-nginx-tls}
CERTIFICATES=${CERTIFICATES:-$(pwd)/nginx-tls-certificates}
COMPOSE="$DOCKER compose -p $PROJECT -f test/nginx/tls/docker-compose.yml"

NGINX_PORT=80
CERVER_HOST=cerver-tls
CERVER_PORT=8443
CLIENT_NAME=nginx
READY_SECONDS=32
PREFIXES=("" /tls12 /unbuffered)

LARGE_SIZE=1048909
LARGE_LETTERS=26
LARGE_FIRST=97
ECHO_BODY=cerver-tls-echo

THREADS=8
REQUESTS_PER_THREAD=160
ROUNDS=3
PAUSE_SECONDS=4
LOAD_PATH=/tls/hello

STATS_PATH=/tls/stats
METRICS_PATH=/tls/cerver/metrics
REUSED_METRIC=cerver_http_reused_connections_total
REUSED_MINIMUM_PERCENT=1
RESUMED_MINIMUM_PERCENT=90

if [ "$BUILD" -ne 0 ]; then
	$DOCKER build -t "$IMAGE" -f Dockerfile.test . || exit 1
fi

cleanup () {
	$COMPOSE down --timeout 8 > /dev/null 2>&1
}

trap cleanup EXIT

certificates_create () {
	rm -rf "$CERTIFICATES"
	mkdir -p "$CERTIFICATES" || return 1

	$DOCKER run --rm -v "$CERTIFICATES:/certificates" -e CLIENT_NAME="$CLIENT_NAME" "$IMAGE" \
		bash test/tls/certificates.sh /certificates "$CERVER_HOST" localhost 127.0.0.1 ::1
}

run_in_nginx () {
	$DOCKER run --rm --network "container:$NGINX" -v "$CERTIFICATES:/certificates:ro" "$@" "$IMAGE" bash -c "$COMMAND"
}

nginx_wait () {
	COMMAND="for i in \$(seq $(( READY_SECONDS * 10 ))); do curl -s -o /dev/null http://127.0.0.1:$NGINX_PORT$LOAD_PATH && exit 0; sleep 0.1; done; exit 1"
	run_in_nginx
}

nginx_get () {
	COMMAND="curl -s -f http://127.0.0.1:$NGINX_PORT$1"
	run_in_nginx
}

large_expected () {
	COMMAND="python3 -c 'import hashlib; print (hashlib.sha256 (bytes ($LARGE_FIRST + (i % $LARGE_LETTERS) for i in range ($LARGE_SIZE))).hexdigest ())'"
	run_in_nginx
}

nginx_large () {
	COMMAND="curl -s -f http://127.0.0.1:$NGINX_PORT$1/tls/large | sha256sum | cut -d ' ' -f 1"
	run_in_nginx
}

nginx_echo () {
	COMMAND="curl -s -f -X POST --data '$ECHO_BODY' http://127.0.0.1:$NGINX_PORT$1/tls/echo"
	run_in_nginx
}

# each location: the versions of nginx towards the cerver, buffered and not, the certificate of nginx in the cerver
nginx_requests () {
	local failed=0 prefix expected
	expected=$(large_expected)

	for prefix in "${PREFIXES[@]}"; do
		nginx_get "$prefix/tls/hello" | grep -q hello || { echo "$prefix/tls/hello failed"; failed=1; }
		nginx_get "$prefix/tls/certificate" | grep -q "CN=$CLIENT_NAME" || { echo "$prefix/tls/certificate is not the one of nginx"; failed=1; }
		nginx_echo "$prefix" | grep -q "$ECHO_BODY" || { echo "$prefix/tls/echo failed"; failed=1; }
		[ "$(nginx_large "$prefix")" = "$expected" ] || { echo "$prefix/tls/large has other bytes"; failed=1; }
	done

	return $failed
}

nginx_load () {
	local failed=0 round result other errors
	local requests=$(( THREADS * REQUESTS_PER_THREAD ))

	for (( round = 1; round <= ROUNDS; round++ )); do
		COMMAND="./benchmarks/bin/load $NGINX_PORT $THREADS $requests GET $LOAD_PATH"
		result=$(run_in_nginx)
		echo "round $round - $LOAD_PATH - $result"

		other=$(echo "$result" | sed -E 's/.* - other ([0-9]+).*/\1/')
		errors=$(echo "$result" | sed -E 's/.* - failed ([0-9]+).*/\1/')
		[ "$other" = "0" ] && [ "$errors" = "0" ] || failed=1

		[ $round -lt $ROUNDS ] && sleep $PAUSE_SECONDS
	done

	return $failed
}

stats_value () {
	echo "$1" | sed -nE "s/.*\"$2\": ([0-9]+).*/\1/p"
}

# nginx keeps its sessions (proxy_ssl_session_reuse): every handshake after the first one resumes,
# and the certificate of nginx always verifies
nginx_stats () {
	local stats handshakes resumed failures ktls
	stats=$(nginx_get "$STATS_PATH")
	handshakes=$(stats_value "$stats" handshakes)
	resumed=$(stats_value "$stats" resumed)
	failures=$(stats_value "$stats" certificate_failures)
	ktls=$(stats_value "$stats" ktls_send)

	echo "$NGINX_IMAGE - $HANDLER - kTLS $KTLS: $stats"

	[[ "$handshakes" =~ ^[0-9]+$ ]] && [[ "$resumed" =~ ^[0-9]+$ ]] || return 1
	[ "$failures" = "0" ] || { echo "certificate failures: $failures"; return 1; }
	[ "$KTLS" != "on" ] || [ "$ktls" -gt 0 ] || echo "the kernel did not take kTLS, cerver encrypted"
	[ "$handshakes" -le 1 ] || [ "$resumed" -ge $(( (handshakes - 1) * RESUMED_MINIMUM_PERCENT / 100 )) ]
}

nginx_metric () {
	nginx_get "$METRICS_PATH" | tr -d '\r' | sed -nE "s/^$1 ([0-9]+)$/\1/p"
}

nginx_reuse () {
	local reused requests
	reused=$(nginx_metric $REUSED_METRIC)
	requests=$(( THREADS * REQUESTS_PER_THREAD * ROUNDS ))

	echo "$NGINX_IMAGE: $reused requests through connections that the cerver kept, the load sent $requests"

	[[ "$reused" =~ ^[0-9]+$ ]] || return 1

	if [ "$HANDLER" = "keep" ]; then
		[ "$reused" -ge $(( requests * REUSED_MINIMUM_PERCENT / 100 )) ]
	else
		[ "$reused" -eq 0 ]
	fi
}

# the cerver requires a certificate: a client without one never gets a response, one with the certificate does
cerver_mtls () {
	local url="https://$CERVER_HOST:$CERVER_PORT/tls/hello"

	COMMAND="curl -s -f --cacert /certificates/authority.pem $url"
	run_in_nginx > /dev/null && { echo "a client without a certificate was answered"; return 1; }

	COMMAND="curl -s -f --cacert /certificates/authority.pem --cert /certificates/client.pem --key /certificates/client.key $url"
	run_in_nginx | grep -q hello || { echo "a client with the certificate was not answered"; return 1; }
}

nginx_errors () {
	local errors
	errors=$($COMPOSE logs nginx 2>&1 | grep -c -E "SSL_|upstream SSL|Connection reset by peer|upstream prematurely closed")
	echo "$NGINX_IMAGE: upstream errors: $errors"
	[ "$errors" -eq 0 ]
}

ktls_value () {
	case "$1" in
		on) echo "1" ;;
		*) echo "" ;;
	esac
}

handler_keep_alive () {
	case "$1" in
		keep) echo "1" ;;
		*) echo "0" ;;
	esac
}

nginx_run () {
	local failed=0

	echo "---- $NGINX_IMAGE - $HANDLER - kTLS $KTLS ----"

	export IMAGE NGINX_IMAGE CERTIFICATES
	export CERVER_TEST_KEEP_ALIVE="$(handler_keep_alive "$HANDLER")"
	export TLS_KTLS="$(ktls_value "$KTLS")"

	$COMPOSE up -d || return 1

	NGINX=$($COMPOSE ps -q nginx)

	nginx_wait || { echo "nginx does not answer"; $COMPOSE logs; cleanup; return 1; }

	nginx_requests || { echo "$NGINX_IMAGE: a request through nginx failed"; failed=1; }
	nginx_load || { echo "$NGINX_IMAGE: the load failed"; failed=1; }
	nginx_stats || { echo "$NGINX_IMAGE: the TLS of the cerver is not the expected one"; failed=1; }
	nginx_reuse || { echo "$NGINX_IMAGE: the connections that the cerver kept are not the expected ones"; failed=1; }
	nginx_errors || failed=1
	cerver_mtls || failed=1

	[ $failed -eq 0 ] || $COMPOSE logs --tail 64

	cleanup

	return $failed
}

certificates_create || { echo "the certificates could not be made"; exit 1; }

FAILED=0
for NGINX_IMAGE in "${NGINX_IMAGES[@]}"; do
	for HANDLER in "${HANDLERS[@]}"; do
		for KTLS in "${KTLS_MODES[@]}"; do
			nginx_run || { echo "$NGINX_IMAGE - $HANDLER - kTLS $KTLS: failed"; FAILED=1; }
		done
	done
done

rm -rf "$CERTIFICATES"

if [ $FAILED -ne 0 ]; then
	echo "nginx tls: failed"
	exit 1
fi

echo "nginx tls: ok"
