## General
- Using gcc 16.2 in a single stage test Dockerfile & its ignore
- Using gcc 16.2 & trixie slim images in the other Dockerfiles
- Added fuzz Dockerfile to build & to run fuzzers with clang
- Added sanitizer & fuzz options in makefile & the new tests
- Running sanitizers & valgrind in the build & test workflows
- Using gcc 16.2 container in beta, production & version job
- Added nightly workflow with thread sanitizer & repetitions
- Defining CERVER_INLINE as empty when compiling in C++
- Added the errno description size definition for strerror_r ()
- Checking the vasprintf () result when creating the c string
- Calling AVX2 base64 methods only when AVX2 is available
- Fixed input password inverted condition & unchecked scan
- Added arena collection to allocate values in reused blocks
- Using FNV-1a hash & growing htab table when it is 3/4 full
- Using dlist read locks & locking two lists in the same order
- Added queue init method & removed repeated pop header
- Checking pool create method exists before producing data
- Added methods to create and append bytes in String type
- Added joinable threads blocking process signals on create
- Joining job queue thread when stopping the queue of jobs
- Refactored thpool with a bounded queue & drain methods
- Added references count to sockets used by many threads
- Added socket send timeout & the dedicated reusable port
- Sending packet version in headers & fixing packet_check()
- Added receive handle buffer method to assemble packets
- Added cerver overload & drain policies with ordered stops
- Handling bad packets & polling a copy of all registered fds
- Fixed on hold packets check & dropping slow connections
- Fixed admin cerver packets checks & polling a copy of fds
- Fixed files receive with the bytes that come with its packet

## Connection
- Added connection states, send queue & real reconnection
- Fixed client end with single owner & added async connect

## Balancer
- Refactored load balancer to receive & to route full packets

## Game
- Fixed game lobby search & join methods using lobby types
- Fixed player unregister from lobby inverted remove results
- Fixed lobby leave & teardown using the lobby after its free
- Refactored game scores with the players names & clamps

## Tests
- Added test output init & counters wait helpers in test.h file
- Added scripts to run cerver & client integration tests pairs
- Added script to repeat a test many times to find flaky ones
- Added script to run each fuzzer with its corpus for a while
- Added arena collection unit tests with new blocks & resets
- Added htab hashing unit tests with FNV-1a & table growth
- Added pool unit tests producing & popping all its elements
- Added queue unit tests with FIFO order & multiple threads
- Added dlist invariants unit tests with threads & self merge
- Registering the new collections unit tests in tests main file
- Added utils unit tests for the serializer & the timer helpers
- Added String bytes methods & its reserve in the unit tests
- Added bounded thpool, drain & joinable threads unit tests
- Added packets unit tests with pieces, routes & the checks
- Added receive buffer unit tests with packets split by bytes
- Added game scores, image files & version in the unit tests
- Added fuzzers & corpus for receive buffer & packet check
- Added cerver core unit tests with drops, on hold & a drain
- Added client lifecycle unit tests ending all the connections
- Added client features unit tests with states & reconnection
- Added client files unit tests using a files cerver in-process
- Added game lobby unit tests with players in a game cerver
- Added routing unit tests with load balancer & two services
- Updated balancer unit tests with the reconnect thread flag
- Using shutdown & atomic counters in the integration tests
- Registering the new unit tests & a runner in the tests script