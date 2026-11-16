## General
- Added a waiter to the thpool to know when its threads wait
- Ending a connection when one of its sends ran out of time
- Using the shared args for the events & the errors of clients
- Fixed BMP images, existing dirs, n_read & each partial file
- Fixed the docs of the job queue, the file send & unregister
- Updated the roadmap with the beta & the link to its history
- Added the changes of the 2.0b-77 to the incompatibilities

## HTTP
- Checking the strings of a static path & its insert in the lists
- Reading again a NUL that follows a value in a JSON buffer

## Tests
- Wrapping every argument of the checks of test.h in parens
- Waiting for all the packets sent in routing before its checks
- Draining the fuzz target of the client with its send timeouts
- Added a test of a second admin of the core cerver at once
- Scaling the send timeout of disconnect with the time scale
