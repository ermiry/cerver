#!/bin/bash

CERVER_VERSION=2.0b-26

# sudo apt-get update && sudo apt-get install -y make wget

cd /opt
sudo wget -O cerver.tar.gz https://github.com/ermiry/cerver/archive/$CERVER_VERSION.tar.gz
sudo tar xzf cerver.tar.gz

cd /opt/cerver-$CERVER_VERSION
sudo make CC=gcc -j4 && sudo make install && sudo ldconfig
