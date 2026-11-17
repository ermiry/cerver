#!/bin/bash

DOCKER=${DOCKER:-docker}
SANITIZER=${SANITIZER:-}
IMAGE=${IMAGE:-ermiry/cerver:conformance${SANITIZER:+-${SANITIZER//,/-}}}
BUILD=${BUILD:-1}
SUITE=${SUITE:-/opt/conformance/http}

if [ ! -f /.dockerenv ]; then
	if [ "$BUILD" -ne 0 ]; then
		$DOCKER build -t "$IMAGE" -f Dockerfile.conformance --build-arg SANITIZER="$SANITIZER" . || exit 1
	fi

	exec $DOCKER run --rm "$IMAGE" bash test/conformance/http.sh
fi

./test/bin/conformance/http test/conformance/http.txt "$SUITE"/request/*.md "$SUITE"/response/*.md
