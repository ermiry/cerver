#!/bin/bash

sudo docker run \
  -it \
  --name api --rm \
  -p 5000:5000 --net cerver \
  -v /home/ermiry/Documents/ermiry/cApps/cerver/data:/home/cerver/data \
  -e RUNTIME=development \
  -e PORT=5000 \
  -e CERVER_RECEIVE_BUFFER_SIZE=4096 -e CERVER_TH_THREADS=4 \
  -e CERVER_CONNECTION_QUEUE=4 \
  ermiry/cerver:local /bin/bash
