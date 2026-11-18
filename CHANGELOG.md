## General
- Running the containers of the workflows without seccomp
- Updated roadmap & CLAUDE.md with the new event loops
- Added all latest 3.0b-11 changes to the incompatibility file

## Events
- Added a ring of io_uring that makes the system calls itself
- Added the abstraction of the backends of every event loop
- Added the backend of io_uring with the buffers it provides
- Added the backend of epoll that emulates the completions
- Added the settings of the backend of loops & their affinity
- Added the backend & the affinity of its loops to the cerver
- Using the backends in every loop & a queue without locks
- Setting the processor of each loop in a group without idles
- Running each job of the inline dispatcher in the event loop
- Removed the poller of epoll & the idles of the pool threads

## HTTP
- Refactored the connection to send only from its event loop
- Sending each response of a loop through its output queue
- Counting the status of a response when it was sent at last
- Added the pool loop to run the handlers in the event loops
- Removed the follow & the read of the pools of idle sockets
- Counting all limited requests with CERVER_STATS_ADD ()

## Benchmarks
- Added a load generator with pipelines in kept connections
- Added the backends, the loop & JSON to the bench server
- Added the case of a server with nginx & h2o for the bench
- Building nginx, h2o & wrk inside the image of competitors
- Running the server bench in competitors without seccomp
- Updated the results of the competitors with its server case

## Tests
- Added the backend of the event loops to the test handlers
- Added the tests of the ring of io_uring with its system calls
- Added the tests of the two backends of all the event loops
- Added the tests of the settings of the backend of the loop
- Registering the tests of the ring, its backends & its setters
- Suppressing the #error of the ring of io_uring in cppcheck
- Failing the system calls of io_uring using syscall () in faults
- Testing every system call of io_uring that fails in the faults
- Added the tests of the handlers in the loop & the pipelines
- Registering the tests of the loop & running the suites apart
- Testing the receive of requests without the reads of a pool
- Testing the pools & keep-alive without the follow of a pool
- Testing an idle cerver & the timeout of the whole response
- Testing disconnects counting each status once it was sent
- Testing the features & the admin stats without any follows
- Setting the backend of the event loops in each web server
- Setting the backend of the event loops in WebSocket tests
- Fuzzing the event loop with every backend & handlers in it
- Running the fuzz of the loop for each backend of the loops
- Measuring each backend & handlers in the loop with flood
- Added the lines of the loop & the pipelines to the baseline
- Running the tests & repeats with each backend of the loop
- Running the integration pairs with every backend of a loop
- Running the disconnects with each backend of every loop
- Running autobahn with each one of the backends of a loop
- Running nginx in front of each backend of the event loops
