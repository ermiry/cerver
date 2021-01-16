#!/bin/bash

# compile docker
sudo docker build -t ermiry/cerver:test -f Dockerfile.test .

# ping
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./examples/test

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/client/ping

sudo docker kill $(sudo docker ps -q)

# packets
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./examples/packets

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/client/packets

sudo docker kill $(sudo docker ps -q)

# requests
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./examples/requests

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/client/requests

sudo docker kill $(sudo docker ps -q)

# auth
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./examples/auth

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/client/auth

sudo docker kill $(sudo docker ps -q)

# sessions
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./examples/sessions

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/client/sessions

sudo docker kill $(sudo docker ps -q)