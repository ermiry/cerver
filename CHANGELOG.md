## General
- Updated roadmap & CLAUDE.md with the sync of the lines
- Added the changes of the 3.0b-10 to the incompatibility file

## Core
- Added a waiter to the thpool for the threads that lack jobs
- Ending a connection when one of its sends ran out of time
- Using the shared args for the events & the errors of clients
- Fixed BMP images, existing dirs, n_read & each partial file
- Fixed the docs of the job queue, the file send & unregister

## HTTP
- Parsing the query & the form values into the request arena
- Reading the next request of every idle connection in a pool
- Checking the strings of a static path & its insert in the lists
- Saying Connection: close in a response that has no length
- Checking each static path of the list for the protected one
- Serving HTTP/1.x as 1.1 & 501 to a method that is unknown

## Tests
- Using parentheses in the arguments of the checks of tests
- Scaling the send timeout of the disconnect cerver by load
- Waiting for the cerver & the connect timeout of the clients
- Waiting for the packets the balancer sent in its routing test
- Added a test of a second admin of the core cerver at once
- Testing a NUL after a value in a JSON & in the fuzz corpus
- Testing the idle connection which a thread gives to its loop
