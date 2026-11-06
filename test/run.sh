#!/bin/bash

RUNNER=${RUNNER:-}

run () {
	$RUNNER "$@" || { echo "$1 failed"; exit 1; }
}

run ./test/bin/types

run ./test/bin/cerver/test

run ./test/bin/cerver/core

run ./test/bin/client/test

run ./test/bin/client/lifecycle

run ./test/bin/connection

run ./test/bin/collections --quiet

run ./test/bin/files

run ./test/bin/game

run ./test/bin/packets

run ./test/bin/receive

run ./test/bin/system

run ./test/bin/threads

run ./test/bin/utils

run ./test/bin/http

run ./test/bin/json

run ./test/bin/jwt

run ./test/bin/version
