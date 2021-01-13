#!/bin/bash

LD_LIBRARY_PATH=bin ./test/bin/collections --quiet || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/utils || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/http || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/json || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/jwt || { exit 1; }