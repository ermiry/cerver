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
sudo docker build -t ermiry/cerver:local -f Dockerfile.local . || { exit 1; }

# ping
echo "Ping integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:local ./bin/cerver/ping

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
	ermiry/cerver:local ./bin/cerver/packets

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
	ermiry/cerver:local ./bin/cerver/requests

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
	ermiry/cerver:local ./bin/cerver/auth

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
	ermiry/cerver:local ./bin/cerver/sessions

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
	ermiry/cerver:local ./bin/cerver/threads

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/threads || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# queue
echo "Queue integration test..."
sudo docker run \
	-d \
	--name test --rm \
	-p 7000:7000 \
	ermiry/cerver:local ./bin/cerver/queue

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/queue || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# load - Round Robin
echo "Load Round Robin integration test..."
sudo docker network create cerver

# service 1
sudo docker run \
	-d \
	--name service-1 --rm \
	-p 7001:7001 --net cerver \
	ermiry/cerver:local ./bin/cerver/service -p 7001

# service 2
sudo docker run \
	-d \
	--name service-2 --rm \
	-p 7002:7002 --net cerver \
	ermiry/cerver:local ./bin/cerver/service -p 7002

# balancer
sudo docker run \
	-d \
	--name load --rm \
	-p 7000:7000 --net cerver \
	ermiry/cerver:local ./bin/cerver/load --type round_robin

sleep 2

sudo docker inspect load --format='{{.State.ExitCode}}' || { exit 1; }
sudo docker inspect service-1 --format='{{.State.ExitCode}}' || { exit 1; }
sudo docker inspect service-2 --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/load/simple || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# load - Handler ID
echo "Load Handler ID integration test..."

# service 1
sudo docker run \
	-d \
	--name service-1 --rm \
	-p 7001:7001 --net cerver \
	ermiry/cerver:local ./bin/cerver/service -p 7001

# service 2
sudo docker run \
	-d \
	--name service-2 --rm \
	-p 7002:7002 --net cerver \
	ermiry/cerver:local ./bin/cerver/service -p 7002

# balancer
sudo docker run \
	-d \
	--name load --rm \
	-p 7000:7000 --net cerver \
	ermiry/cerver:local ./bin/cerver/load --type handler_id

sleep 2

sudo docker inspect load --format='{{.State.ExitCode}}' || { exit 1; }
sudo docker inspect service-1 --format='{{.State.ExitCode}}' || { exit 1; }
sudo docker inspect service-2 --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/load/id || { exit 1; }

sudo docker kill $(sudo docker ps -q)

printf "\n\nDone\n\n"
