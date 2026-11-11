## General
- Building with all the warnings of the library & as errors too
- Running the CI with every warning as an error & with clang
- Updated the actions of the workflows to the Node 24 ones
- Documented the handler types, their limits & UDP cervers
- Added incompatibilities of the 2.0b-66 with the port of 1.8
- Updated roadmap with the port of the 1.8b-7 up to the b-9
- Updated CLAUDE.md with the game, serializer & its stress

## Core
- Ported the zero initializer & printf format checker of config
- Ported the formats & final newlines in all the base modules
- Ported the histograms of the values & of their distributions
- Ported the system values of the memory, load & processes
- Ported the sends of the sockets that share their deadlines
- Keeping the idle workers of the manager while its jobs wait
- Ported the portable header, the serializer & game requests

## HTTP
- Ported the OpenSSL 3 key bits & the JSON & JWT formats
- Fixed the formats & the void casts of all the HTTP modules
- Sharing one send deadline among the sends of a response
- Authenticating the files of the static paths like their routes

## Examples
- Ported the examples with logs of every one of their events
- Ported the benchmarks with all the warnings of the library

## Tests
- Evaluating only once the values of the checks in each test
- Fixed the formats, unused arguments & mains of the tests
- Ported the tests of the portable headers & of the serializer
- Added a schemas pair of objects that an older peer sends
- Ported the core tests of the system, histograms & threads
- Added tests of the recursive directories & of the deadlines
- Ported balancer & routing tests with the route generations
- Ported the in memory game tests of lobbies & of requests
- Added a game pair of two players in another process each
- Running six examples as integration pairs in IPv4 loopback
- Added a protocol stress of concurrent clients in schedules
- Added fuzzers of the serializer & game objects with seeds
- Scheduling repeats of game & peers & the TSan unit tests
- Added disconnect waits for valgrind & a probe for the recv
- Added server tests of the response deadline & web thpool
- Added receive tests of static files that need authentication