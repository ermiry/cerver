#!/bin/bash

# ensure a clean build
make clean

# build sources with development flags
make -j8
