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
- Added the changes of the 1.9b-2 to the incompatibility file

## Tests
- Added the tests of the faults of allocations to the core too
- Updated the fuzz job with one target per job & its recovery
- Added the static analyzers with their suppressions & a job
