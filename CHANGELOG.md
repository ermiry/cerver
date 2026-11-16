## General
- Added a waiter to the thpool for the threads that lack jobs
- Updated roadmap & CLAUDE.md with the F4b keep-alives
- Added the changes of the 2.1b-19 to the incompatibility file

## HTTP
- Parsing the query & the form values into the request arena
- Reading the next request of every idle connection in a pool

## Tests
- Using parentheses in the arguments of the checks of tests
- Scaling the send timeout of the disconnect cerver by load
- Waiting for the cerver & the connect timeout of the clients
- Waiting for the packets the balancer sent in its routing test
