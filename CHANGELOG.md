## General
- Updated roadmap & CLAUDE.md with the async part of L5
- Added all latest 3.0b-13 changes to the incompatibility file

## Core
- Waiting for the deferred requests of a cerver in each drain

## Events
- Added the sources of an event loop & loops without cerver
- Counting deferred requests & opening the loops to clients

## HTTP
- Added the public header of the HTTP client on event loops
- Added the structures that files of the HTTP client all share
- Added the HTTP client with its own loop or one of a cerver
- Added the requests of an HTTP client and their responses
- Added the exchange of a client request over its event loop
- Keeping the loops of each cerver alive for its HTTP clients
- Added deferred responses & verdicts to the HTTP context
- Deferring the response of a request without using a thread
- Deferring the verdict of the authentication of each request
- Documented that handlers of a loop defer their responses
- Added a drained callback for the stream of each response
- Added limits, compression & part handlers to any endpoint
- Parsing multipart bodies in the pools & handing their parts

## Examples
- Added an example of the async handlers & an HTTP client

## Benchmarks
- Running the web benchmark with an HTTP client of cerver

## Tests
- Testing the sources of the event loops with both backends
- Testing the HTTP client against a server that plays a script
- Testing deferred responses, verdicts & clients in the loops
- Testing the body job of each multipart request in a receive
- Testing endpoint limits & one event loop in the server tests
- Testing the settings that the endpoints inherit from groups
- Fuzzing the responses that an HTTP client of cerver reads
- Testing what the HTTP client does when its allocations fail
