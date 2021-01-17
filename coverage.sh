#!/bin/bash

# ensure a clean build
make clean

# gcc
printf "gcc make\n\n"
make TYPE=test COVERAGE=1 -j8
printf "\n\ngcc test\n\n"
make TYPE=test COVERAGE=1 test -j8

# run
bash test/run.sh

make coverage

# clean up
make clean