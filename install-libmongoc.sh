#!/bin/bash

sudo apt-get update && sudo apt-get install -y cmake wget

wget https://github.com/mongodb/mongo-c-driver/releases/download/1.15.1/mongo-c-driver-1.15.1.tar.gz
tar xzf mongo-c-driver-1.15.1.tar.gz

cd mongo-c-driver-1.15.1
mkdir cmake-build
cd cmake-build
cmake -D ENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF -D CMAKE_BUILD_TYPE=Release ..
sudo make -j8 && sudo make install && sudo ldconfig