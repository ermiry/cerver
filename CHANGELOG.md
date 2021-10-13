## General
- Added SOCKET_DEBUG definition for extra receive information
- Added CLIENT_STATS & CONNECTION_STATS compilation definitions
- Added the ability to set a custom cerver max received packet size
- Added base file-system methods in dedicated sources
- Moved port & udp related definitions to network header
- Added cerver info alias definition & method
- Updated custom string type methods implementations

## Client
- Added new base client_receive_handle_buffer () implementation
- Added CLIENT_RECEIVE_DEBUG definition for extra client receive logs
- Added base client handler error definitions & methods
- Added client handler error return values to packet handlers
- Updated client get_next_packet () & receive related methods
- Split client_connection_start () into dedicated connection methods

## Connection
- Added ReceiveHandle into connection structure
- Removed sock receive & full packet fields from connection
- Added base methods to send packets using connection's queue
- Added dedicated method to en-queue a packet in connection
- Added base connection state definitions & methods
- Added dedicated connection state mutex

## Packets
- Changed packet's header field from a pointer to a static value
- Changed packet version from a reference to a static field
- Added base packet_send_actual () to send a tcp packet
- Added dedicated packets init requests methods

## Handler
- Refactored cerver_test_packet_handler () to send a ping packet
- Added a new cerver_receive_handle_buffer () implementation
- Added RECEIVE_DEBUG definition to enable extra logs in receive methods
- Added handler receive error definitions & methods

## Threads
- Added dedicated THREADS_DEBUG definition
- Refactored JobQueue methods to handle different types
- Refactored thread_set_name () to handle variable arguments
- Added ability to request jobs from queue by id
- Added latest available worker structure & methods
- Added dedicated methods to wait & signal a job queue
- Refactored custom thread mutex & cond methods

## Files
- Renamed custom filename sizes related definitions
- Added latest files & images types definitions & methods

## Utilities
- Removed obsolete json utilities methods & sources
- Small updates in custom log types & internal methods
- Updated custom math & c string related utilities

## Tests
- Checking packet's data integrity in test app handlers
- Updated send packet methods in client packets integration test
- Added dedicated client & cerver queue integration tests
- Added custom thread structures & methods unit tests
- Added base dedicated math utilities unit tests sources
- Added string type methods custom tests methods
- Added worker unit tests & update threads tests