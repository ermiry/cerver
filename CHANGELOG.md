## General
- Using the since of the core in the events & thpool headers
- Reading a cerver info without freeing the client connection
- Keeping the connection of a request whose response fails
- Updated the roadmap with the beta & the core of the 1.9.1
- Updated the CLAUDE.md with the core & faults of the 1.9.1
- Added the changes of the 2.0b-78 to the incompatibilities

## HTTP
- Counting the limited & the timed out requests with atomics
- Counting the multipart stats of the sizes only with the files
- Computing the uptime of the admin info & of all its metrics
- Sending the default headers of a cerver in all its responses

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