#!/bin/bash

# ensure a clean build
make clean

# build sources with test flags
make TYPE=test -j8

# install cerver
make TYPE=test install
