#!/bin/bash

RUNNER=${RUNNER:-}

if [ -n "$RUNNER" ]; then
	export TEST_TIME_SCALE=${TEST_TIME_SCALE:-4}
fi

run () {
	$RUNNER "$@" || { echo "$1 failed"; exit 1; }
}

run ./test/bin/types

run ./test/bin/cerver/test

run ./test/bin/cerver/core

run ./test/bin/cerver/lobby

run ./test/bin/cerver/peers

run ./test/bin/client/test

run ./test/bin/client/lifecycle

run ./test/bin/client/features

run ./test/bin/client/files

run ./test/bin/connection

run ./test/bin/balancer

run ./test/bin/routing

run ./test/bin/collections --quiet

run ./test/bin/files

run ./test/bin/game

run ./test/bin/network

run ./test/bin/packets

run ./test/bin/receive

run ./test/bin/socket

run ./test/bin/system

run ./test/bin/threads

run ./test/bin/udp

run ./test/bin/utils

run ./test/bin/crypto

if [ -z "$RUNNER" ] && [ -x ./test/bin/faults ]; then
	run ./test/bin/faults
fi

run ./test/bin/http

run ./test/bin/websockets

run ./test/bin/json

run ./test/bin/jwt

run ./test/bin/version
