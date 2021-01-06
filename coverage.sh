#!/bin/bash

# ensure a clean build
make clean

# gcc
printf "gcc make\n\n"
make TYPE=test -j8
printf "\n\ngcc examples\n\n"
make TYPE=test examples -j8
printf "\n\ngcc test\n\n"
make test -j8

# run
bash test/run.sh

# clean up
make clean