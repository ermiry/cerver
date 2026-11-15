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
- Moved the tests of the pairs & the app to the layout's path
- Updated the includes of the fuzzers to all the layout paths
- Updated the examples & the benchmarks to the new paths
- Updated the makefile with the folders & a test per module
- Using the random bytes of the kernel of crypto in sessions
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
- Added CLAUDE.md with the build, the tests & the modules
- Added the changes of the 1.9b-1 to the incompatibility file

## Documentation
- Documented the core, the config & the version in Doxygen
- Documented the clients & the connections in Doxygen too
- Documented the packets, handler & network with Doxygen
- Documented the auth, admin, files, load & system headers
- Documented the collections & the types with Doxygen too
- Documented the utils, threads, crypto & game in Doxygen

## Tests
- Waiting for the client of an async connection to be running