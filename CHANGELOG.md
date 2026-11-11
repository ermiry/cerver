## General
- Moving only the reports that gcov writes in make coverage
- Not removing the idle workers while jobs wait in the queue
- Added histograms of values with atomic counts in the utils
- Added the memory, load, cpu & process values of the host
- Copying, removing & moving files without calling system ()
- Recycling packets & their data with a cache in each thread
- Added the stats of each request type that cervers handled
- Adding the stats of the packets that each handler handles
- Handling the packets of each request type with its handler
- Logging the type of the cerver with the name from its map
- Renamed the defaults with full words & their module prefix
- Added the Beta 1.8b-8 changes to the incompatibilities file

## Connection
- Releasing only once a client that the main poll can not take
- Setting the initial sizes & the max connections of the polls

## Balancer
- Selecting the services of a custom balancer with a method

## Game
- Logging the lobby players without an id in the debug build

## Examples
- Added events to the example app & linking every example
- Registering & releasing all the events in all of the examples
- Releasing the app data of the requests example only once
- Reconnecting the client example when the cerver closes it
- Initializing each field of the test packet of the example app

## Tests
- Added a manager test of idle workers while the jobs queue
- Added histogram unit tests with values from many threads
- Registering the histogram unit tests in the main of the utils
- Checking the ranges of the values in the system unit tests
- Added files tests of copies, removals & the moves of trees
- Added packets tests of recycling in threads & data classes
- Added core tests of the poll max, request stats & handlers
- Added a balancer test of the custom type & of the method
- Added routing tests of the custom balancer & the info type
- Added a balancer pair that selects services by message id
- Added a stress cerver that sends every request body back
- Added a stress client of concurrent connections & latency
- Running the protocol stress in Docker & in scheduled runs
- Running five of the examples as integration pairs with IPv4
- Lowering the max allocations per connection & per packet
- Removed the load script that killed every docker container