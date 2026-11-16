## General
- Updated roadmap & CLAUDE.md with pools & health of F5
- Added 2.1b-17 pools & health changes to incompatibilities

## HTTP
- Added the pools of threads of the HttpCerver & their lanes
- Added a pool setting to the groups, endpoints & old routes
- Answering health & ready routes in the loop without a pool
- Added pools, connections & health series into the metrics
- Counting requests of a connection of threads when it ends
- Measuring the time & the bytes of the files of every upload
- Added pools, connections & health into admin routes stats
- Added thpool_has_queued () to ask for jobs with no mutex
- Following a kept connection in the thread that answered it
- Added followed requests counter to metrics & admin stats

## Tests
- Reading the parked requests of its lane in the receive tests
- Added HTTP tests of pools, their drain & the health routes
- Registered the pools tests & a run alone in the HTTP tests
- Testing that an idle connection that yields is counted once
- Added HTTP tests of the thread that follows its connection
- Testing thpool_has_queued () with jobs in a queue & none
- Added web pools server that answers with its thread name
- Added the client of web pools pair that expects its threads
- Registered the web pools pair in the makefile & integration
- Choosing a pool for each endpoint in the route tree fuzzer
- Looking once for the next request in the loop fuzzer's jobs
- Added a slow route with its pool to the benchmarks server
- Sizing the allocation slack with a new connection of a loop