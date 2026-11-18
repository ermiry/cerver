#!/bin/bash

DOCKER=${DOCKER:-docker}
IMAGE=${IMAGE:-ermiry/cerver:test}
BUILD=${BUILD:-1}
TESTSSL=${TESTSSL:-drwetter/testssl.sh:3.2@sha256:64bab3c1fd8eee440f68d5e020dd633c1f2d375fe0ae593bdfec2725110a7e29}
PYTHON_IMAGE=${PYTHON_IMAGE:-gcc:16.2}
REPORTS=${REPORTS:-testssl-reports}
NETWORK=${NETWORK:-cerver-testssl}
WAIT_SECONDS=${WAIT_SECONDS:-32}
SEVERITY=${SEVERITY:-MEDIUM}
IGNORED=${IGNORED:-cert_revocation security_headers}
MODES=(${MODES:-plain mtls})

SERVER=cerver-testssl-server
SCANNER=cerver-testssl-scanner
CERVER_HOST=cerver-tls
CERVER_PORT=8443

if [ "$BUILD" -ne 0 ]; then
	$DOCKER build -t "$IMAGE" -f Dockerfile.test . || exit 1
fi

cleanup () {
	$DOCKER rm -f "$SERVER" "$SCANNER" > /dev/null 2>&1
	$DOCKER network rm "$NETWORK" > /dev/null 2>&1
}

trap cleanup EXIT

certificates_create () {
	$DOCKER run --rm -v "$(pwd)/$REPORTS/certificates:/certificates" "$IMAGE" \
		bash test/tls/certificates.sh /certificates "$CERVER_HOST" localhost 127.0.0.1 ::1
}

mode_authority () {
	case "$1" in
		mtls) echo "/certificates/authority.pem" ;;
		*) echo "" ;;
	esac
}

mode_client_certificate () {
	case "$1" in
		mtls) echo "--mtls /certificates/client-bundle.pem" ;;
		*) echo "" ;;
	esac
}

server_start () {
	$DOCKER run -d --name "$SERVER" --network "$NETWORK" --network-alias "$CERVER_HOST" \
		-v "$(pwd)/$REPORTS/certificates:/certificates:ro" -w /opt/cerver/test/web \
		-e CERVER_TEST_ADDRESS=0.0.0.0 \
		-e TLS_CERTIFICATE=/certificates/server.pem -e TLS_KEY=/certificates/server.key \
		-e TLS_CLIENT_AUTHORITY="$(mode_authority "$1")" \
		"$IMAGE" ../bin/web/tls > /dev/null || return 1

	local port_hex
	port_hex=$(printf '%04X' "$CERVER_PORT")
	for _ in $(seq 1 "$WAIT_SECONDS"); do
		$DOCKER exec "$SERVER" grep -q ":$port_hex 00000000:0000 0A" /proc/net/tcp 2> /dev/null && return 0
		sleep 1
	done

	echo "the cerver never listened"
	$DOCKER logs "$SERVER" | tail -n 64
	return 1
}

server_stop () {
	$DOCKER stop -t 16 "$SERVER" > /dev/null
	local status
	status=$($DOCKER wait "$SERVER")
	$DOCKER logs "$SERVER" > "$REPORTS/$1-cerver.log" 2>&1
	$DOCKER rm "$SERVER" > /dev/null
	[ "$status" -eq 0 ] || { echo "the cerver ended with $status"; return 1; }
}

scan () {
	$DOCKER run --name "$SCANNER" --platform linux/amd64 --network "$NETWORK" \
		-v "$(pwd)/$REPORTS:/reports" -v "$(pwd)/$REPORTS/certificates:/certificates:ro" \
		"$TESTSSL" --quiet --color 0 --warnings off --ip one --add-ca /certificates/authority.pem \
		$(mode_client_certificate "$1") --severity "$SEVERITY" \
		--jsonfile "/reports/$1.json" --logfile "/reports/$1.log" \
		"$CERVER_HOST:$CERVER_PORT" > /dev/null 2>&1
	local result=$?
	$DOCKER rm -f "$SCANNER" > /dev/null
	[ -s "$REPORTS/$1.json" ] || { echo "testssl.sh did not write a report (exit $result)"; return 1; }
}

findings () {
	$DOCKER run --rm -v "$(pwd)/$REPORTS:/reports" -e IGNORED="$IGNORED" "$PYTHON_IMAGE" python3 -c '
import json, os, sys

severities = ("MEDIUM", "HIGH", "CRITICAL")
ignored = set (os.environ.get ("IGNORED", "").split ())

findings = [
	finding for finding in json.load (open (sys.argv[1]))
	if finding.get ("severity") in severities and finding.get ("id") not in ignored
]

for finding in findings:
	print ("%s %s: %s" % (finding.get ("severity"), finding.get ("id"), finding.get ("finding")))

sys.exit (1 if findings else 0)
' "/reports/$1.json"
}

run_mode () {
	local failed=0

	echo "testssl.sh against the cerver - $1..."

	server_start "$1" || return 1
	scan "$1" || failed=1
	server_stop "$1" || failed=1

	[ $failed -eq 0 ] && findings "$1" || failed=1

	return $failed
}

cleanup
rm -rf "$REPORTS"
mkdir -p "$REPORTS/certificates"
chmod 777 "$REPORTS"

certificates_create || { echo "the certificates could not be made"; exit 1; }
$DOCKER run --rm -v "$(pwd)/$REPORTS/certificates:/certificates" "$IMAGE" \
	bash -c 'cat /certificates/client.pem && openssl pkcs8 -topk8 -nocrypt -in /certificates/client.key' \
	> "$REPORTS/certificates/client-bundle.pem" || { echo "the bundle of the client could not be made"; exit 1; }

$DOCKER network create "$NETWORK" > /dev/null || exit 1

FAILED=()
for mode in "${MODES[@]}"; do
	run_mode "$mode" || FAILED+=("$mode")
done

if [ ${#FAILED[@]} -gt 0 ]; then
	echo "testssl.sh failed: ${FAILED[*]}"
	exit 1
fi

echo "testssl.sh passed"
