#!/bin/bash

# ensure a clean build
make clean

# main sources
printf "gcc make\n\n"
make TYPE=production -j8

# examples
printf "\n\ngcc examples\n\n"
make TYPE=production examples -j8