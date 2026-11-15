## General
- Moved the cerver to core with its types, the start & its end
- Moved the admin into its own folder with its stats & its poll
- Moved the auth & the sessions into a folder with their hold
- Moved the client into a folder with its connect & its events
- Moved the connection into its own folder & split it by parts
- Moved the files into a folder with images, sends & receives
- Moved the handler, the receive, events & errors to a folder
- Moved the balancer into load with its services & its routes
- Moved the network, the sockets & the udp into their folder
- Moved the packets & serializer into a folder with its header
- Moved the system, the timers & the input to its own folder
- Updated the includes of collections & types to their layout
- Updated the includes of threads & game to the new layout
- Updated the includes of the utils & their tests to the layout
- Added the crypto of 3.0 with its hash, random & a provider
- Moved the tests of the pairs & the app to the layout's path
- Updated the includes of the fuzzers to all the layout paths
- Updated the examples & the benchmarks to the new paths
- Updated the makefile with the folders & a test per module
- Removed the http includes that the parts of core don't use
- Changed the license to Apache-2.0, its notice & the others
- Fixed arena_alloc () with sizes that wrap & a NULL avl tree
- Keeping the real length of strings & their NULL arguments
- Fixed the copies of C strings, converters & random ranges
- Taking a NULL job in job_reset () & fixed bsem_wait () doc
- Fixing a NULL game type config & a NULL lobby summary
- Returning 1 when the receive timeout of a socket is not set
- Checking NULL packets & the sizes of the appended data
- Starting new receive handles with their received bytes at 0
- Keeping the names & the ips of connections set with NULL
- Keeping the client names & the session lookups with NULL
- Fixed cerver_start () of a NULL cerver & the admin lookup
- Fixed the type & the status of a NULL balancer & a service
- Ending a connection only once, if two of the threads end it
- Added Doxygen with make docs, its Dockerfile & its CI job
- Updated CLAUDE.md with the new layout & the comments
- Added the changes of the 2.1b-14 to its incompatibility file

## HTTP
- Moved the http cerver into http/core split by all of its parts
- Moved the headers of http into a folder split in all its parts
- Moved the http requests into a folder with content & forms
- Moved the http responses into a folder with all their sends
- Moved the http routes into a folder with its tree & a match
- Moved the jwt keys, the origins & the security into a folder
- Moved the websockets into http with frames & the registry
- Moved the json of http into a folder with all of its own tests
- Moved the jwt of http into its own folder with all of its tests
- Moved the parser of http to its own folder in the layout too
- Moved the tests of the web pairs & autobahn to the layout
- Not finding a NULL mime or extension in the content types
- Checking NULL values of the admin, the pairs & its receive
- Keeping the real length of truncated filenames & of origins
- Checking NULL arguments & data references in responses

## Documentation
- Documented the core, the config & the version in Doxygen
- Documented the clients & the connections in Doxygen too
- Documented the packets, handler & network with Doxygen
- Documented the auth, admin, files, load & system headers
- Documented the collections & the types with Doxygen too
- Documented the utils, threads, crypto & game in Doxygen
- Documented the http core, its headers, requests & jwt key
- Documented the http responses, routes & the websockets

## Tests
- Fixed the race of websockets metrics tests with sent bytes
- Waiting for the client of an async connection to be running
- Measuring the requests allocations inside the same server