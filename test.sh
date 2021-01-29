#!/bin/bash

# ensure a clean build
make clean

# remove any active container
sudo docker kill $(sudo docker ps -q)

# compile soucres & tests
make TYPE=test -j4 || { exit 1; }
make TYPE=test -j4 test || { exit 1; }

# compile docker
sudo docker build -t ermiry/cerver:test -f Dockerfile.test . || { exit 1; }

# web
sudo docker run \
	-d \
	--name test --rm \
	-p 8080:8080 \
	ermiry/cerver:test ./bin/web/web

sleep 2

sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

./test/bin/client/web || { exit 1; }

sudo docker kill $(sudo docker ps -q)

# api
# sudo docker run \
# 	-d \
# 	--name test --rm \
# 	-p 8080:8080 \
# 	ermiry/cerver:test ./bin/web/api

# sleep 2

# sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

# ./test/bin/client/api || { exit 1; }

# sudo docker kill $(sudo docker ps -q)

# # upload
# sudo docker run \
# 	-d \
# 	--name test --rm \
# 	-p 8080:8080 \
# 	ermiry/cerver:test ./bin/web/upload

# sleep 2

# sudo docker inspect test --format='{{.State.ExitCode}}' || { exit 1; }

# ./test/bin/client/upload || { exit 1; }

# sudo docker kill $(sudo docker ps -q)