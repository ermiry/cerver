#!/bin/bash

set -e

IMAGE=${IMAGE:-ermiry/cerver:competitors}

make clean

docker build -f Dockerfile.competitors -t "$IMAGE" .

ARCHITECTURE=$(docker run --rm "$IMAGE" uname -m)
RESULTS=benchmarks/competitors/results/$ARCHITECTURE.txt

mkdir -p benchmarks/competitors/results
docker run --rm "$IMAGE" > "$RESULTS"

cat "$RESULTS"
