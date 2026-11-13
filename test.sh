#!/bin/bash

make clean

make TYPE=test -j4 || { exit 1; }
make TYPE=test -j4 test || { exit 1; }

bash test/run.sh || { exit 1; }

bash test/integration.sh || { exit 1; }

bash test/allocations.sh || { exit 1; }

printf "\n\nDone\n\n"
