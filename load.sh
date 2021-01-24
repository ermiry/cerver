#!/bin/bash

# remove any active container
sudo docker kill $(sudo docker ps -q)

# compile soucres & tests
make TYPE=test -j4 || { exit 1; }
make TYPE=test -j4 test || { exit 1; }

# compile docker
sudo docker build -t ermiry/cerver:test -f Dockerfile.test . || { exit 1; }

# create network
sudo docker network create cerver

# service 1
sudo docker run \
	-d \
	--name service-1 --rm \
	-p 7001:7001 --net cerver \
	ermiry/cerver:test ./bin/cerver/service -p 7001

# service 2
sudo docker run \
	-d \
	--name service-2 --rm \
	-p 7002:7002 --net cerver \
	ermiry/cerver:test ./bin/cerver/service -p 7002

# balancer
sudo docker run \
	-d \
	--name load --rm \
	-p 7000:7000 --net cerver \
	ermiry/cerver:test ./bin/cerver/load

sleep 2

sudo docker inspect load --format='{{.State.ExitCode}}' || { exit 1; }
sudo docker inspect service-1 --format='{{.State.ExitCode}}' || { exit 1; }
sudo docker inspect service-2 --format='{{.State.ExitCode}}' || { exit 1; }

# test
./test/bin/client/load || { exit 1; }

sudo docker kill $(sudo docker ps -q)