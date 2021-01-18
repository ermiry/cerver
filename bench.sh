#!/bin/bash

# ensure a clean build
make clean

printf "sources\n\n"
make TYPE=test NATIVE=1 -j8

printf "\n\nexamples\n\n"
make TYPE=test NATIVE=1 examples -j8

printf "\n\ntest\n\n"
make TYPE=test NATIVE=1 test -j8

printf "\n\nbench\n\n"
make TYPE=test NATIVE=1 bench -j8