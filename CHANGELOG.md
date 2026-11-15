## General
- Removed the http includes that the parts of core don't use
- Fixed arena_alloc () with sizes that wrap & a NULL avl tree
- Keeping the real length of strings & their NULL arguments
- Fixed the copies of C strings, converters & random ranges
- Checking a NULL format in the logs of cerver with the date
- Taking a NULL job in job_reset () & fixed bsem_wait () doc
- Fixing a NULL game type config & a NULL lobby summary
- Returning 1 when the receive timeout of a socket is not set
- Checking NULL packets & the sizes of the appended data
- Starting new receive handles with their received bytes at 0
- Keeping the names & the ips of connections set with NULL
- Keeping the client names & the session lookups with NULL
- Fixed cerver_start () with a NULL cerver & the admin twice
- Fixed the admin lookups of a NULL session & the handlers
- Fixed the type & the status of a NULL balancer & a service
- Ending a connection only once, if two of the threads end it
- Added Doxygen with make docs, its Dockerfile & its CI job
- Updated CLAUDE.md & roadmap with the comment guide
- Added the changes of the 3.0b-3 to the incompatibility file

## HTTP
- Not finding a NULL mime or extension in the content types
- Checking NULL values of the admin, the pairs & its receive
- Keeping the real length of truncated filenames & of origins
- Checking NULL arguments & data references in responses

## Documentation
- Documented the cerver of the core, its start, update & end
- Documented the config & the version of cerver in Doxygen
- Documented the client with its connect, requests & events
- Documented the connections & their parts in Doxygen too
- Documented the packets & the serializer with Doxygen too
- Documented the handler, events & errors with its Doxygen
- Documented the network, its sockets & udp with Doxygen
- Documented in Doxygen the auth, the sessions & the hold
- Documented the admin cerver, stats & poll in Doxygen too
- Documented the files, the images & sends in Doxygen too
- Documented the balancer, services & routes with Doxygen
- Documented in Doxygen the system, its timers & the input
- Documented the collections & the lists with their Doxygen
- Documented in Doxygen the types & the Strings of cerver
- Documented the utils, the logs & base64 with its Doxygen
- Documented the threads, jobs & workers with its Doxygen
- Documented the crypto with its hash, random & a provider
- Documented the game, its lobbies & players with Doxygen
- Documented the http cerver with all of its core in Doxygen
- Documented the http headers, fields & buffers in Doxygen
- Documented the http requests & content with its Doxygen
- Documented in Doxygen the http security & all its jwt keys
- Documented the http responses & streams in the Doxygen
- Documented the http routes & the tree with their Doxygen
- Documented the websockets & their frames with Doxygen

## Tests
- Fixed the race of websockets metrics tests with sent bytes
- Waiting for the client of an async connection to be running
- Measuring the requests allocations inside the same server