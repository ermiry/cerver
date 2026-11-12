## General
- Removed the docs workflow, cerver-docs updates the wiki
- Ending every Dockerfile & every build script with a newline
- Running the coverage with its flag & the allocations in test
- Added the uninstall script of the installed library & headers
- Linking the benchmarks with the rpath of the cerver library
- Running the TSan & the balancer flaky hunters of the core
- Rewrote the README of 2.0 with HTTP, its goal & the start
- Updated roadmap with the port of the 1.8.1 as the 2.0b-70
- Updated CLAUDE.md with the core of the 1.8.1 & the start
- Added the incompatibilities of the 2.0b-70 in the 1.8.1 port

## Core
- Closing the socket only once when the cerver fails to start
- Releasing the data of the handlers that never had a thread
- Running the direct multiple handlers in the receiver thread
- Checking both the ids & the count of the multiple handlers
- Checking the welcome, the buffer sizes & the id generator
- Generating the default session ids from the kernel random
- Checking the event & error types before using their arrays
- Giving the args of a dropped event to the data of a trigger
- Sending the teardown of a cerver before closing its clients
- Triggering the cerver info & the on hold disconnect events
- Giving the data of a successful authentication to the client
- Ignoring admin polls of 0 fds & the changes after the start
- Deleting the packets a connection can not put in its queue
- Taking the size of a file read from the bytes that were read
- Writing into a mutable buffer with c_string_concat_safe ()
- Fixed the packet_check () comment about packet versions
- Fixed the comments of the sessions & handlers of the port
- Updating the UDP peers stats before releasing their count

## HTTP
- Leaving the HTTP connections out of the cerver teardown

## Examples
- Releasing the example cervers that fail with their teardown
- Added the echo cerver & client example of the get started
- Releasing the web example cervers that fail with teardown
- Moved the services examples target before the client ones

## Tests
- Running the echo example as a pair, with client arguments
- Added tests of failed starts, handlers data, events & auths
- Added tests of cerver setters, session ids & dropped ones
- Added tests of the connection buffer & unqueued packets
- Added a test of the file read size with the number of bytes
- Releasing the web test cervers that fail with their teardown
- Waiting for the on hold stat after a UDP peer failed its auth