#!/bin/bash

LD_LIBRARY_PATH=bin ./test/bin/cerver/test || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/client/test || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/connection || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/collections --quiet || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/packets || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/threads || { exit 1; }

LD_LIBRARY_PATH=bin ./test/bin/utils || { exit 1; }
