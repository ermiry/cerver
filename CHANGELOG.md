## General
- Ported the UDP cervers & clients of the core with the peers
- Ported the network address lists, zones & the ip equalities
- Connecting to each resolved address with its own timeout
- Ported the array pool, the job queue priorities & their stats
- Ported the joinable workers & added their worker manager
- Ported the handlers with pooled jobs & the alive condition
- Ported cerver with its UDP start, the bind family & address
- Joining the log file thread & logging the inactive ones later
- Closing & shutting down the sockets with their close mutex
- Shutting the inactive sockets down without the clients lock
- Giving its synchronization to the connection that connects
- Closing the sockets of failed & unused client connections
- Taking the unique id of every new handler with atomic add
- Closing a socket only after the send of other threads ends
- Added incompatibilities of the 2.0b-64 with core changes
- Updated roadmaps with the port of the 1.8b-4 up to 1.8b-6
- Updated CLAUDE.md with the balancer pairs & allocations

## HTTP
- Keeping the zones in the client ips & matching the proxies
- Exporting the response & worker deletes, multipart checks

## Examples
- Ported the balancer & service examples with an app library
- Ported the portable bench clock & the handler benchmark
- Ended the sources of the common examples with newlines

## Tests
- Ported the core tests of UDP, network, threads & the pools
- Added a test address to the pairs & to the web test cervers
- Running every integration pair in IPv4 & the IPv6 loopback
- Running the nginx features client in IPv4 & IPv6 loopback
- Added proxy tests of zones in trusted proxies & client ips
- Lowering allocations per request with the array pool stack
- Printing the name of each test of the server & of the peers
- Retrying the first request of a cerver that ran out of its fds
- Keeping the logs of the repeated runs that fail as artifacts
- Added core tests of inactive churn, synchronization & fds
- Added an address fuzzer of zones & of the text round trips
- Counting allocations per connection & per queued packet
- Added a job queue test of the jobs pool in its steady state
- Added an upstream service & the clients of balancer pairs
- Running balancer pairs of restarts, large packets & clients
- Stopping the supervised servers of the pairs with SIGTERM