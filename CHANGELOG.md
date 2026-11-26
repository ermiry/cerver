## General
- Renamed the RequestMethod type to HttpRequestMethod
- Updated the roadmap with the betas of HTTP/2 & this one
- Updated the CLAUDE.md with the client faults & the frees
- Added the changes of the 2.1b-25 to the incompatibilities

## Client
- Reading a cerver info without freeing the client connection
- Keeping the connection of a request whose response fails

## HTTP
- Counting the limited & the timed out requests with atomics
- Counting the multipart stats of the sizes only with the files
- Computing the uptime of the admin info & of all its metrics
- Sending the default headers of a cerver in all its responses
- Serving every HTTP/1.x & reading a later minor as HTTP/1.1
- Using a curl handle of every thread for its remote requests

## TLS
- Never waiting in an event loop to give the TLS key to kTLS

## Tests
- Added tests of the connects & requests that cervers close
- Watching the allocations & their frees in the faults binaries
- Added a faults test for the client update thread that leaked
- Registering the client tests in the main of the faults binary
- Checking the sizes of the files of a multipart in its receives
- Testing the uptime of the admin info & of the cerver metric
- Testing the counters, the default headers & send timeouts
- Scaling the idle timeout of disconnect & emptying uploads
- Setting a headers timeout in the api cerver of the web pair
- Requesting the api cerver of the web pairs after it was idle
- Testing the HTTP/1.x versions & the ones that are rejected
- Testing the connection that a thread keeps with its service
- Added a larger certificate to the ones of the TLS unit tests
- Testing an event loop with clients stuck in their handshake
- Waiting for a closed WebSocket to leave its room in its test
- Running autobahn with wss in events & the long fuzz of tls
