#!/bin/bash

# the HTTP Garden (https://github.com/narfindustries/http-garden, at a fixed commit) with cerver as one of its
# origins (Dockerfile.garden, test/web/garden.c): the cases of test/conformance/garden.txt go to every origin, and
# the ones that cerver does not agree with must be the ones of the file
# MODE=check | record | fuzz (FUZZ_ITERATIONS), ORIGINS the other origins, GARDEN_DIR where the Garden is kept

DOCKER=${DOCKER:-docker}
GARDEN_COMMIT=${GARDEN_COMMIT:-b417e806c1b15e8ea0b9312f81a91fbdcbc7a83e}
GARDEN_DIR=${GARDEN_DIR:-/tmp/cerver-garden}
ORIGINS=${ORIGINS:-nginx h2o libevent gunicorn}
MODE=${MODE:-check}
FUZZ_ITERATIONS=${FUZZ_ITERATIONS:-1000}
BUILD=${BUILD:-1}
PYTHON_IMAGE=${PYTHON_IMAGE:-python:3.13-slim}

GARDEN=$GARDEN_DIR/http-garden
CASES=$(pwd)/test/conformance

if [ ! -d "$GARDEN" ]; then
	mkdir -p "$GARDEN_DIR" || exit 1
	curl -fsSL "https://github.com/narfindustries/http-garden/archive/$GARDEN_COMMIT.tar.gz" | tar xz -C "$GARDEN_DIR" || exit 1
	mv "$GARDEN_DIR/http-garden-$GARDEN_COMMIT" "$GARDEN" || exit 1
fi

if ! grep -q '^  cerver:' "$GARDEN/docker-compose.yml"; then
	cat >> "$GARDEN/docker-compose.yml" << EOF
  cerver:
    image: http-garden-cerver
    x-props:
      role: origin
EOF
fi

if ! grep -q '^cerver:' "$GARDEN/quirks.yml"; then
	printf 'cerver:\n  allows-missing-host-header: true\n' >> "$GARDEN/quirks.yml"
fi

if [ "$BUILD" -ne 0 ]; then
	$DOCKER build -t http-garden-soil "$GARDEN/images/http-garden-soil" || exit 1
	(cd "$GARDEN" && $DOCKER compose build $ORIGINS) || exit 1
	make clean > /dev/null
	$DOCKER build -t http-garden-cerver -f Dockerfile.garden . || exit 1
fi

(cd "$GARDEN" && $DOCKER compose up -d cerver $ORIGINS) || exit 1

$DOCKER run --rm --network http-garden_default --label com.docker.compose.service=tools \
	-v /var/run/docker.sock:/var/run/docker.sock -v "$GARDEN:/garden" -v "$CASES:/cases:ro" \
	-e GARDEN_TOOLS=/garden/tools -e GARDEN_SEED="${GARDEN_SEED:-0}" \
	"$PYTHON_IMAGE" bash -c "pip install -q docker pyyaml tqdm > /dev/null 2>&1 && python3 -I /cases/garden.py /cases/garden.txt $MODE $FUZZ_ITERATIONS"
code=$?

(cd "$GARDEN" && $DOCKER compose down) > /dev/null 2>&1

exit $code
