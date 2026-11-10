## General
- Closing & shutting down the sockets with their close mutex
- Shutting the inactive sockets down without the clients lock
- Taking the unique id of every new handler with atomic add
- Closing a socket only after the send of other threads ends
- Making the worker delete method public as in the http line
- Added the Beta 1.8b-6 changes to the incompatibilities file

## Connection
- Giving its synchronization to the connection that connects
- Closing the sockets of failed & unused client connections

## Examples
- Stopping examples in their signals & releasing them later
- Releasing the events data of balancer & service examples
- Zeroing app responses of the examples & checking sizes
- Ended the sources of the benchmarks with their newlines

## Tests
- Printing the name of each test of the UDP peers as it runs
- Keeping the logs of the repeated runs that fail as artifacts
- Added core tests of inactive churn, synchronization & fds
- Added an address fuzzer of zones & of the text round trips
- Counting allocations per connection & per queued packet
- Added a job queue test of the jobs pool in its steady state
- Added an upstream service & the clients of balancer pairs
- Running balancer pairs of restarts, large packets & clients
- Stopping the supervised servers of the pairs with SIGTERM