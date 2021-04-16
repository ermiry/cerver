#!/bin/bash

./test/bin/cerver/test || { exit 1; }

./test/bin/client/test || { exit 1; }

./test/bin/connection || { exit 1; }

./test/bin/balancer || { exit 1; }

./test/bin/collections --quiet || { exit 1; }

./test/bin/packets || { exit 1; }

./test/bin/system || { exit 1; }

./test/bin/threads || { exit 1; }

./test/bin/utils || { exit 1; }
