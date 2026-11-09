## General
- Added the bounded free list of the core for deleted objects
- Added thread groups to count & wait for detached threads
- Added thpool stats with wait & run time histograms per job
- Added socket send methods with deadline & sendfile loop
- Resolving with getaddrinfo () & writing mapped ips as IPv4
- Sending packets of the core until their deadline & in pieces
- Taking the trailing bytes of a packet in the shared receive
- Ported the 1.x connection states, send queue & reconnects
- Ported the 1.x client with async connect & recycled clients
- Added the load balancer of the 1.x rewritten on a new core
- Ported the handler with recycled receives & joined threads
- Ported cerver with its bind address, balancer & drain grace
- Ported admin, auth, errors and events changes of the core
- Validating the file headers and sending files until deadline
- Ported the game fixes of the core in lobbies and in players
- Ported log file name, player log type & base64 AVX2 guard
- Updated makefile with the core tests, defines & recycling
- Using gcc 16.2 containers & each line's names in workflow
- Renamed nightly workflow to scheduled, with its own gate
- Added incompatibilities file with the 2.0b-63 core changes
- Updated roadmaps with the port of the core of the 1.8 line
- Updated CLAUDE.md with balancer, free lists & teardowns

## HTTP
- Sending responses with the socket send methods of core
- Added the thpool counters & histograms to cerver metrics
- Unmapping the IPv4 proxies with the core's network offset

## Tests
- Updated the tests scripts with balancer pairs & the fuzzers
- Added core unit tests of the balancer, sockets & free lists
- Added cerver core tests of teardowns, lobbies & balancers
- Added client tests of the 1.x features, files and its lifecycle
- Added core fuzz targets for the receive, packets & clients
- Added the thpool metrics checks to the features unit tests
- Lowering allocations per request in the performance script