#!/bin/bash

# ensure a clean build
make clean

# gcc
printf "gcc make\n\n"
make TYPE=production -j8
printf "\n\ngcc examples\n\n"
make TYPE=production examples -j8
printf "\n\ngcc test\n\n"
make test -j8

# g++
printf "\n\ng++ make\n\n"
make TYPE=production CC=g++ -j8
printf "\n\ng++ examples\n\n"
make TYPE=production CC=g++ examples -j8
printf "\n\ng++ test\n\n"
make CC=g++ test -j8

# clean up
make clean