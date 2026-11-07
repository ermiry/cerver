## General
- Added balancer cerver type & its reference in every cerver
- Added CLIENT_STATS & CONNECTION_STATS definitions
- Added sock fd in the packet header to route the responses
- Added packet_route_between_connections() using a pipe
- Added bad packets type & a method to print packet types
- Added packet_send_unsafe() & updating the sender stats
- Added dedicated script to remove the libcerver installation
- Fixed stats print format warnings in game & lobby sources
- Fixed cast function type warnings in cerver update threads
- Added base64 & handler benchmarks with its make target
- Added cerver documentation submodule in docs directory
- Updated workflows with the latest actions & release action
- Fixed bsem_reset() creating again its mutex and condition
- Using dlist_insert_at_end() when pushing data into a pool
- Fixed string append methods overflow & their missing NUL
- Fixed dlist prev links, insert positions, merge sort & halves
- Fixed AVL double rotations & removing nodes with children
- Fixed connection delete method removing socket too early
- Fixed sock_set_blocking() inverted flags for the poll cerver
- Fixed auth session token copy reading past session id end
- Sending the packet header padding zeroed in each packet
- Sending zeroed error & file packets without heap garbage
- Checking a NULL filename before reading the image type
- Fixed game cerver unregister lobby removing the list start
- Fixed game types search & removal with the player id leak
- Using a clock name instead of a number in timer timespec
- Fixed cerver inactive check recursing into the NULL nodes
- Fixed lobby threads casts and the ignored custom handler
- Fixed game scores update clamp & strings lengths in keys
- Fixed input line offset reading the pointer freed by realloc()
- Fixed gcc 15 unused params & sha256 parameter warning
- Updated workflows with the actions versions for Node 24

## Balancer
- Added a load balancer structure & its services registration
- Added balancer_receive() to route packets to the services
- Added services status, reconnect wait & a status callback
- Added balancer and service stats with unhandled packets
- Consuming data from connections if no service is available
- Consuming the data from services on any bad packet type
- Added routing to the services by round robin or handler id
- Added BALANCER_DEBUG & SERVICE_DEBUG definitions
- Sending a packet error when there is no service to route to

## Examples
- Added dedicated balancer & service examples with an app

## Tests
- Added balancer & services methods unit tests & their stats
- Added load balancer integration tests by type & handler id
- Fixed htab unit tests double free when inserting final value
- Fixed dlist unit tests race condition when reading dlist end
- Removed an overflowing case from c string copy unit tests
- Fixed packets unit tests checking binary data with strlen ()
- Added collections unit tests helper to check the dlist links
- Added dlist unit tests checking invariants after any change
- Added AVL unit tests with random values & balance check
- Registering the new collections unit tests in tests main file
- Added packet header padding unit test with zeroed bytes
- Checking the NUL at the end in all string append unit tests
- Added network unit tests checking socket blocking modes
- Registering the network unit tests in makefile & tests script
- Added game unit tests with game types, players & lobbies
- Registering the game unit tests in makefile & the run script
- Removed unused bmp & gif extensions from files unit tests