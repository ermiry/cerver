## General
- Using gcc 16.2 in all of the Dockerfiles, the CI & schedules
- Running sanitizers, valgrind, fuzz & clang in the workflows
- Failing the CI on any warning after leaving zero in all builds
- Added arenas, free lists & a growing htab in the collections
- Refactored the thpool with a bounded queue, drain & stats
- Added job queue priorities & managers that scale workers
- Added thread groups & joined handlers without the sleeps
- Added overload & drain policies & stopping in a fixed order
- Added socket references, a close mutex & send deadlines
- Supporting IPv6 in every address with zones, lists & binds
- Added UDP cervers with peers, tokens & with the sessions
- Sending the portable headers in network order & route ids
- Replaced the serializer with CBOR objects read by schema
- Recycling the sockets, connections, clients & their packets
- Added the stats of requests, handlers & of the host values
- Copying, removing & moving files without calling system ()
- Not ending the receives that a signal interrupts with EINTR
- Creating the thpool only in the cervers of the threads type
- Creating the recursive directories that exist or are too long
- Ending the sends with the deadline in blocking sockets too
- Added the incompatibilities file with an entry for each beta

## Connection
- Added the connection states, send queues & reconnection
- Trying each address that resolves with its connect timeout
- Reading the packets of clients in the receive of the cervers
- Authenticating with session tokens & credentials schemas
- Closing the sockets of any connections that fail or are idle

## Balancer
- Refactored the balancer to receive & route the full packets
- Routing the balancer responses with route ids of own slots
- Selecting the services of a custom balancer with a method

## Game
- Rewrote the lobbies & the game requests with the objects
- Added the lobby references, locks & the lobby poll receive
- Added the init, the player timeout & the fps of game types

## Examples
- Stopping examples in their signals & releasing their events
- Rewrote the game example & added a client for its lobbies

## Tests
- Added integration pairs of every example in IPv4 & in IPv6
- Added scripts to repeat tests, fuzz & count the allocations
- Added unit tests of collections, threads, sockets & buffers
- Added UDP, serializer, balancer & game tests & their pairs
- Added fuzzers of the receive, headers, objects & of games
- Added a protocol stress of a cerver & of concurrent clients