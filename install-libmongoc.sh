#!/bin/bash

MONGOC_VERSION=1.17.3

sudo apt-get update && sudo apt-get install -y cmake wget

sudo mkdir /opt/mongoc
cd /opt/mongoc

sudo wget -q https://github.com/mongodb/mongo-c-driver/releases/download/$MONGOC_VERSION/mongo-c-driver-$MONGOC_VERSION.tar.gz
sudo tar xzf mongo-c-driver-$MONGOC_VERSION.tar.gz

cd mongo-c-driver-$MONGOC_VERSION
sudo mkdir cmake-build && cd cmake-build
sudo cmake -D ENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF -D CMAKE_BUILD_TYPE=Release -D ENABLE_SSL=OPENSSL -D ENABLE_SRV=ON ..
sudo make -j4 && sudo make install