#!/bin/bash

RUNNER=${RUNNER:-}
BACKENDS=(${BACKENDS:-uring epoll})

if [ -n "$RUNNER" ]; then
	export TEST_TIME_SCALE=${TEST_TIME_SCALE:-4}
fi

run () {
	$RUNNER "$@" || { echo "$1 failed"; exit 1; }
}

run_backends () {
	for backend in "${BACKENDS[@]}"; do
		CERVER_TEST_BACKEND=$backend run "$@"
	done
}

run_models () {
	run "$@"

	CERVER_TEST_HANDLER=events run_backends "$@"
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

run ./test/bin/load

run ./test/bin/collections --quiet

run ./test/bin/files

run ./test/bin/game

run ./test/bin/handler

run ./test/bin/network

run ./test/bin/packets

run ./test/bin/system

run ./test/bin/threads

run ./test/bin/utils

run ./test/bin/crypto

run ./test/bin/events

if [ -z "$RUNNER" ] && [ -x ./test/bin/faults ]; then
	run_models ./test/bin/faults
fi

run_backends ./test/bin/http

run_models ./test/bin/websockets

run ./test/bin/json

run ./test/bin/jwt

run ./test/bin/version
