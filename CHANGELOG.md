## General
- Renamed nightly workflow to scheduled with a dispatcher
- Using each line's release names & image tags in workflows
- Running workflows unit tests containers with IPv6 enabled
- Added bounded intrusive free list to reuse deleted objects
- Disabling the recycling with the sanitizers & fuzzing builds
- Added thpool stats with wait & run time histograms per job
- Added socket send methods with deadline & sendfile loop
- Sending packets, balancer routes & files until the deadline
- Resolving with getaddrinfo () & writing mapped ips as IPv4
- Added cerver bind address & the IPv6 only socket options
- Added incompatibilities file with the entries for every beta

## Connection
- Recycling deleted sockets in the process bounded free list
- Recycling connections with embedded stats & sync values
- Recycling the cerver receives in a bounded free list as well
- Added connection send timeout taken from the cerver one
- Resolving client connection hosts & ips with getaddrinfo ()

## Tests
- Added free list unit tests with limits & with multiple threads
- Added connection storage & the recycled values unit tests
- Added thpool stats unit tests with buckets, counts & times
- Added socket send unit tests with full & with closed peers
- Added client large packet test using a non blocking socket
- Added network unit tests to parse & resolve the addresses
- Added cerver core address tests in IPv6 & with dual stack
- Added connections cerver & client to count the allocations
- Added performance script with allocations per connection
- Registering new unit tests in the makefile & in tests scripts