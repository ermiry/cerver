#!/bin/bash

# ensure a clean build
make clean

printf "sources\n\n"
make DEVELOPMENT='' -j8

printf "\n\nexamples\n\n"
make DEVELOPMENT='' EXADEBUG='' examples -j8

printf "\n\ntest\n\n"
make DEVELOPMENT='' test -j8

printf "\n\nbench\n\n"
make DEVELOPMENT='' bench -j8