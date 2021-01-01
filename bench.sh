#!/bin/bash

# ensure a clean build
make clean

printf "sources\n\n"
make TYPE=test -j8

printf "\n\nexamples\n\n"
make TYPE=test examples -j8

printf "\n\ntest\n\n"
make TYPE=test test -j8

printf "\n\nbench\n\n"
make TYPE=test bench -j8