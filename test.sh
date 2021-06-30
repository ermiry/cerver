#!/bin/bash

# ensure a clean build
make clean

# remove any active container
sudo docker kill $(sudo docker ps -q)

# compile soucres & tests
make TYPE=test -j4 || { exit 1; }
make TYPE=test -j4 test || { exit 1; }

# unit tests
bash test/run.sh || { exit 1; }

# compile docker
echo "Building test docker image..."
sudo docker build -t ermiry/cerver:test -f Dockerfile.test . || { exit 1; }

# ping
echo "Ping integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./bin/cerver/ping

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/ping || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# packets
echo "Packets integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./bin/cerver/packets

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/packets || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# requests
echo "Requests integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./bin/cerver/requests

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/requests

sudo docker kill $(sudo docker ps -q)

# auth
echo "Auth integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./bin/cerver/auth

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/auth || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# sessions
echo "Sessions integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./bin/cerver/sessions

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/sessions || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# threads
echo "Threads integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:test ./bin/cerver/threads

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/threads || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# web
echo "Web integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 8080:8080 \
	ermiry/cerver:test ./bin/web/web

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/web/web || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# auth
echo "Web Auth integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 8080:8080 \
	ermiry/cerver:test ./bin/web/auth

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/web/auth || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# api
echo "API integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 8080:8080 \
	ermiry/cerver:test ./bin/web/api

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/web/api || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# upload
echo "Upload integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 8080:8080 \
	ermiry/cerver:test ./bin/web/upload

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/web/upload || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# multiple
echo "Multiple integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 8080:8080 \
	ermiry/cerver:test ./bin/web/multiple

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/web/multiple || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# jobs
echo "Jobs integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 8080:8080 \
	ermiry/cerver:test ./bin/web/jobs

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/web/jobs || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# admin
echo "Admin integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 8080:8080 \
	ermiry/cerver:test ./bin/web/admin

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/web/admin || { exit 1; }

sudo docker kill $(sudo docker ps -q)

printf "\n\nDone\n\n"
