## General
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
- Added incompatibilities of the 2.0b-73 fixes in the headers

## HTTP
- Not finding a NULL mime or extension in the content types
- Checking NULL values of the admin, the pairs & its receive
- Keeping the real length of truncated filenames & of origins
- Checking NULL arguments & data references in responses

## Tests
- Waiting for the client of an async connection to be running
- Measuring the requests allocations inside the same server