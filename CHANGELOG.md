## General
- Changed packet's header field from a pointer to a static value
- Updated handler methods to use new packet header field
- Refactored cerver_test_packet_handler () to send a ping packet
- Changed packet version from a reference to a static field
- Added SOCKET_DEBUG definition for extra receive information

## Client
- Added new base client_receive_handle_buffer () implementation
- Added CLIENT_RECEIVE_DEBUG definition for extra client receive logs
- Added base client handler error definitions & methods
- Added client handler error return values to packet handlers
- Split client_packet_handler () to better check for errors
- Changed client name field from a String into a buffer
- Removed unused method client_get_identifier ()

## Connection
- Added ReceiveHandle into connection structure
- Refactored connection_update () to use receive handle structure
- Changed connection's name & ip from a String into static buffers

## Handler
- Added a new cerver_receive_handle_buffer () implementation
- Added spare fields to ReceiveHandle structure
- Added RECEIVE_DEBUG definition to enable extra logs in receive methods
- Removed receive handle allocation from internal receive methods
- Moved ReceiveHandle structure & state definitions to dedicated source

## Tests
- Added base packet create & generate methods unit tests
- Added base packet header methods unit tests
- Added base cerver & client tests to test handler methods
- Added more development flags when compiling test app

## Benchmarks
- Added base benchmark to test handler performance