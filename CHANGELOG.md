## General
- Added thread groups to count & wait for detached threads
- Joining handler threads & freeing packets with no handler
- Waiting handlers, events & connections in cerver teardown
- Counting cerver events & errors threads in an event group
- Joining admin handlers & sleeping frames in the condition
- Validating the file headers of received packets before use
- Using a portable clock in the benchmarks when out of x86
- Added the Beta 1.8b-3 changes to the incompatibilities file

## Connection
- Stopping the connect backoff with a connection condition
- Recycling clients & reading next packets in shared receive
- Reading the session id with its lock to send the auth token

## Tests
- Added client fuzz target that reads packets of socket pairs
- Added file fuzz target with the headers & the paths search
- Added cerver teardown tests of handlers, events and drain
- Added client tests of connect backoff and the next packet
- Added client session id, its threads & recycled values tests
- Added file header unit tests & a request outside the paths
- Added cerver info unit test of unterminated strings & flags
- Removed thpool test warnings of unused sanitizer helpers
- Lowering allocations per connection in performance script