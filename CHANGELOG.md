## General
- Moved the core into a folder per module like the 3.0 layout
- Moved the internal headers next to all their sources in src/
- Renamed the methods that find a key to the _with_ names
- Changed the license to Apache-2.0 with a notice & others
- Documented every public header in Doxygen & make docs
- Updated the wiki for the new paths & for the API of the 1.9
- Using the random bytes of the kernel in the crypto module
- Fixed the arguments, the sizes & the limits the docs found
- Removed the defines of glibc from config.h with an #error
- Using localtime_r in the log & reentrant times in the timers
- Fixed the locks of the collections & their new safe variants
- Fixed the requests of jobs, the semaphores & each worker
- Added thpool_has_queued () & a waiter of the idle threads
- Stopping a cerver from a signal handler with the stop pipe
- Added the references to the arguments of events & errors
- Releasing what the setters replace when it is another value
- Added the limits of the admins & a check for their packets
- Rejecting uploads with no path & removing the partial files
- Detecting BMP images & the directories that already exist
- Keeping the data of packets & of strings that cannot grow

## Connection
- Ending a connection only once, if two of the threads end it
- Ending the connections whose sends reach their timeouts

## Game
- Added the references of lobbies & the checks of the score

## Tests
- Added the unit tests in a folder & a binary for each module
- Added the failing allocations & threads that the tests inject
- Added the static analyzers with their suppressions & a job
- Updated the fuzz job with one target per job & its recovery
- Reading the answers of the client in its own fuzz target too
- Waiting for all the packets that the balancer sent in its test