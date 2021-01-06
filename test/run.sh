#!/bin/bash

LD_LIBRARY_PATH=bin ./test/bin/collections --quiet

LD_LIBRARY_PATH=bin ./test/bin/utils

LD_LIBRARY_PATH=bin ./test/bin/json

LD_LIBRARY_PATH=bin ./test/bin/jwt