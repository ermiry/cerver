## General
- Added a bounded copy to the utils & fixed the limits of ints
- Using localtime_r in the log & reentrant times in the timers
- Removed the defines of glibc from config.h with an #error
- Fixed the str_set that grows & str_split without a delimiter
- Fixed the locks of the collections & their new safe variants
- Fixed the requests of jobs, the semaphores & each worker
- Added the references to the arguments of events & errors
- Fixed the stop from a signal, the stats & the core handlers
- Added the limits of the admins & its own check of packets
- Fixed the drop of the poll of the clients & of their requests
- Releasing the old received data that a connection replaces
- Keeping the data of packets that can't grow & their copies
- Rejecting uploads without a path & taking the plain names
- Added the references of lobbies & the checks of the score
- Using the bounded copy in sessions & in the load services
- Updated CLAUDE.md with the analyzers & the faults tests
- Added the changes of the 2.0b-75 to the incompatibilities

## HTTP
- Answering the videos with RFC 9110 & checking the pools
- Releasing the custom data the endpoints & groups replace
- Keeping the last character of the dirname of a request too
- Removed the warning of timeouts that are not powers of 2
- Checking the memory of the query & of the multipart pairs
- Added the MIT notice of the multipart parser to the source

## Tests
- Added the tests of the faults of allocations to the core too
- Added the static analyzers with their suppressions & a job
