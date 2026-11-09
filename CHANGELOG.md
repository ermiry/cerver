## General
- Added network address lists, zones & normalized ip equal
- Taking the cerver socket family from its own bind address
- Logging the address & the port where each cerver is ready
- Replaced the dlist pool with a growing array stack & a limit
- Added job queue priorities, timeouts, requests & the stats
- Handlers taking pooled jobs with atomic working counters
- Workers that wait for their state in a condition & are joined
- Waiting for the alive handlers in a condition, with no sleeps
- Added a worker manager that scales its workers with load
- Joining the log file thread & resetting its path when it ends
- Logging the inactive clients after releasing the clients lock
- Ended the auth, client & the packets sources with newlines
- Added the Beta 1.8b-5 changes to the incompatibilities file

## Connection
- Trying each resolved address with its own connect timeout
- Resolving the host again in each reconnect of connections
- Pooling the jobs of the send queues of client connections

## Tests
- Fixed the g++ build of the UDP peers test with zeroed info
- Added network tests of zones, address lists & the ip equal
- Added core tests of address fallbacks, timeouts & of binds
- Added a features test that reconnects to the other address
- Running all the integration pairs in IPv4 & in IPv6 loopback
- Added the pool, job queue, worker & worker manager tests
- Added a log file test where the end stops its thread quickly