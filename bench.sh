#!/bin/bash

# ensure a clean build
make clean

make DEVELOPMENT='' -j8
make DEVELOPMENT='' examples -j8
make DEVELOPMENT='' test -j8
make DEVELOPMENT='' bench -j8