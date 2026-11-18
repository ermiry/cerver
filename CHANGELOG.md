## General
- Updated README, roadmap & CLAUDE.md to the new API
- Added all latest 3.0b-12 changes to the incompatibility file

## Core
- Removed the threads of every connection of a web cerver
- Using only event loops for web cervers, one for every core

## HTTP
- Added the context of a request that each handler receives
- Using the context in the receive & moved its header to src
- Added the getters of a request that return views of its data
- Moved the structure of every request to an internal header
- Moved the parts of a multipart request to internal headers
- Moved the key value pairs of HTTP into an internal header
- Added the writer of a response in the context of a request
- Moved all the prepared responses to their internal headers
- Moved the stream of any response to the internal headers
- Removed the first routes API with its translation to the tree
- Renamed groups & endpoints, their structures are internal
- Running the middlewares & the validators with the context
- Renamed HttpCerver to CerverHttp that is an opaque type
- Removed the main route & the register of all the old routes
- Renamed the settings of HTTP & hid the global responses
- Renamed all the limits of HTTP with the prefix cerver_http
- Running the handlers of HTTP in its event loops by default
- Serving every file of the static paths in the pool of threads
- Added a group of their own with the admin routes of HTTP
- Reporting the stats & metrics of endpoints without a route
- Renamed the static paths of HTTP & the removal of a path
- Renamed the uploads settings of HTTP & their generators
- Renamed the bytes range of a request & its type of ranges
- Using the new names of HTTP in the internals of their core
- Renamed the enum of HTTP headers to CerverHttpHeader
- Using the new types of headers and methods in the parser
- Renamed content types, methods & the url of any request
- Renamed the status, the date & the compression for HTTP
- Renamed the JWT, the origins & the proxies for HTTP auth
- Taking the context of a request in on_upgrade () callbacks
- Updated the umbrella of HTTP with only installed headers

## Examples
- Updated the API example with the context of each request
- Updated the admin, auth, jobs & JSON examples for HTTP
- Updated the upload & videos examples with views of data
- Updated the web, WebSocket & worker examples of HTTP

## Benchmarks
- Updated the bench server & trees using the new HTTP API

## Tests
- Removed the handler type variable of the web test servers
- Updated the routes of the test application with the context
- Updated the API, auth, JSON, router & web servers of test
- Updated the admin, jobs, workers & the pools test servers
- Updated the uploads test servers with their parts as views
- Updated the features, disconnection & garden test servers
- Updated the WebSockets test servers with the new names
- Testing the admin stats, an idle cerver & pools in the pairs
- Using the new statuses & method types in the web clients
- Testing an HTTP cerver & all its admin stats without routes
- Testing the routes tree & the admin groups without routes
- Testing requests, headers & the parser with all new names
- Testing the responses without the headers of their cervers
- Testing the JWT, the origins & proxies with the new names
- Testing the receives of requests with handlers of a context
- Testing the headers of each cerver & its only handler type
- Testing views, a single response & features with a context
- Testing disconnects in event loops & the uploads cleanups
- Testing keep-alive, pools & the loop as the default handler
- Testing WebSocket upgrades with the context of requests
- Testing the faults of HTTP using groups & internal headers
- Fuzzing HTTP using the internal headers & the new names
- Testing the I/O threads that any web cerver has by default
- Running pairs, repeats & autobahn only within event loops
- Running nginx only in front of the cervers with event loops
- Counting just the allocations within the performance script
