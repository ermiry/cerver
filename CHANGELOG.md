## General
- Added cerver_is_stopping () to read the stop of any cerver
- Keeping connections of threads only when a cerver asks it
- Updated roadmap & CLAUDE.md with the keep-alive of F4
- Added 2.1b-16 keep-alive changes to the incompatibilities

## HTTP
- Added keep-alive settings with their setters to HttpCerver
- Added the Connection header of a response by keep-alive
- Deciding the keep-alive of each request before its handler
- Announcing the close of the connection of every rejection
- Added one shot interest & silent sources to an event poller
- Keeping connections of event loops idle between requests
- Added a reused connections counter to the cerver metrics

## Tests
- Added the keep-alive tests of a real cerver in both models
- Added kept connections of the event loop to receive tests
- Registered keep-alive tests with a run alone in HTTP tests
- Disabled the keep-alive in HTTP tests that read until close
- Added CERVER_TEST_KEEP_ALIVE for features & its client
- Handling many requests by connection in the loop fuzzers
- Added kept connections to load client & benchmark server
- Requiring 150 % of the req/s with a connection that is kept
- Testing nginx with kept connections & cervers that restart