## General
- Added thpool_has_queued () to ask for jobs with no mutex
- Added the events handler type & the setters of the cervers
- Added the poller & the timer wheel of the web event loops
- Added the event loop, the HTTP connection & the pool job
- Added the group of event loops with the drain in two parts
- Updated roadmap & CLAUDE.md with F3, F4, F5 of nightly
- Added the changes of the 3.0b-5 to the incompatibility file

## HTTP
- Added keep-alive settings with their setters to HttpCerver
- Added the pools of threads of the HttpCerver & their lanes
- Added a pool setting to the groups, endpoints & old routes
- Added pools, connections & health series into the metrics
- Added pools, connections & health into admin routes stats
- Measuring the time & the bytes of the files of every upload

## Tests
- Added the test handler type to run the tests in two models
- Added the tests of the poller & of the timer wheel of loops
- Feeding the requests of the receive tests through the loop
- Running the server tests in both models & the events ones
- Added the keep-alive tests of a real cerver in both models
- Added HTTP tests of pools, their drain & the health routes
- Testing thpool_has_queued () with jobs in a queue & none
- Running WebSocket, faults & autobahn tests in the models
- Taking the handler type of the web pairs from environment
- Registered the web pools pair in the makefile & integration
- Added the loop fuzzer with its corpus & its scheduled jobs
- Testing nginx with kept connections & cervers that restart
- Added kept connections to load client & benchmark server
- Requiring 150 % of the req/s with a connection that is kept
