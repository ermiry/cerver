## General
- Linking a test binary for each folder of the units in makefile
- Sorting the includes of the types & the collections by path
- Sorting the includes of the threads, utils & crypto in blocks
- Renamed the gametype to types & the search with a name
- Split the cerver by the parts of its regions in its core folder
- Split the admin cerver into its folder with its lifecycle & poll
- Moved auth, sessions & the on hold connections into auth
- Split the client in its own folder by its events & its requests
- Split the connection in its folder with its lifecycle & registry
- Split the files into its folder with the cerver, send & receive
- Moved the handler, the receive, events & errors to handler
- Split the balancer in the load folder by service & its routes
- Moved the network, the socket & udp to its network folder
- Split the packets into the header & moved serializer with it
- Moved system, timer & input to system, version to the top
- Using cerver.h as the umbrella header of the parts of core
- Using the includes of the parts in benchmarks & its drivers
- Updated thirdparty file with the path of the method header
- Updated roadmap with the structure & the documentation
- Updated CLAUDE.md with the layout, the internal headers
- Added the changes of the 3.0b-2 into the incompatibilities

## HTTP
- Moved the custom, metrics & the utils of http into the core
- Split the http cerver into the parts of its regions in its core
- Split the http headers into its fields, types, header, buffers
- Moved the request, method, url & multipart to the requests
- Moved the response, the status & stream to the responses
- Moved the tree, the route & the middlewares to the routes
- Moved the auth, the origin & the proxy to the http security
- Sorting the includes of the json, the jwt & the http parsers
- Moving the hidden settings of websockets to their internal
- Using http.h as the umbrella header of all the parts of http

## Examples
- Using the includes of the parts of the core in the examples
- Using the includes of the parts of http in all web examples

## Tests
- Moved the units of the connection, handler, packets & files
- Moved the units of network, socket & udp to network tests
- Moved the balancer & routing units to the load tests folder
- Moved the units of system to its folder & version at the top
- Moved the handler pair into the cerver & the client of pairs
- Moved the http units of core, headers & requests to folder
- Moved the http units of responses & routes to their folders
- Moved the http units of the security & the jwt to the folder
- Using the new includes in http suites of the same process
- Using the includes of the parts in the test app & its header
- Using the includes of the parts in the integration of cerver
- Using the includes of the parts in the integration of clients
- Using the includes of the parts in the web integration tests
- Using the includes of the parts in fuzzers & the faults tests
- Sorting the includes in the json & the jwt tests of upstream
- Sorting the includes of the collections, thread & type tests
- Using the includes of the parts in the utils & in game tests
- Using the includes of all the parts in the websockets tests
- Running the units of each folder & a load test in scheduled