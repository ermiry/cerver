#!/bin/bash

# sudo apt-get update && sudo apt-get install -y make wget

cd /opt
sudo wget -O cerver.tar.gz https://github.com/ermiry/cerver/archive/1.5.2.tar.gz
sudo tar xzf cerver.tar.gz

cd /opt/cerver-1.5.2
sudo make CC=gcc -j4 && sudo make install && sudo ldconfig
