#!/bin/bash

# ensure a clean build
make clean

# compile docker
echo "Building local docker image..."
sudo docker build -t ermiry/cerver:local -f Dockerfile.local . || { exit 1; }
