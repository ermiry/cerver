#!/bin/bash

LD_LIBRARY_PATH=bin ./test/bin/dlist --quiet

LD_LIBRARY_PATH=bin ./test/bin/utils

LD_LIBRARY_PATH=bin ./test/bin/json

LD_LIBRARY_PATH=bin ./test/bin/jwt