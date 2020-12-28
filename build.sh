#!/bin/bash

# ensure a clean build
make clean

# gcc
make -j8
make examples -j8
make test -j8

# g++
make CC=g++ -j8
make CC=g++ examples -j8
make CC=g++ test -j8

# clean up
make clean