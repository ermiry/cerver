## General
- Added the events handler type & the setters of the cervers
- Moved the reserve fd of the accept to the handler internal
- Added the poller & the timer wheel of the web event loops
- Added the event loop, the HTTP connection & the pool job
- Added the group of event loops with the drain in two parts
- Updated roadmap & CLAUDE.md with the event loop of F3
- Added 2.1b-15 event loop changes to the incompatibilities

## HTTP
- Added the warnings of the setters that run in an I/O thread

## Tests
- Added the test handler type to run the tests in two models
- Added the tests of the poller & of the timer wheel of loops
- Feeding the requests of the receive tests through the loop
- Running the server tests in both models & the events ones
- Running the disconnect & the features tests in two models
- Taking the handler type of the web pairs from environment
- Running WebSocket, faults & autobahn tests in the models
- Added the loop fuzzer with its corpus & its scheduled jobs
- Running the integration pairs & nginx with both the models
- Requiring 90 % of the req/s of threads from the event loop
