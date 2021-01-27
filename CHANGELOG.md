## General
- Handling requests integration test in dedicated test script
- Added request integration test in test workflow
- Added CLIENT_STATS & CONNECTION_STATS compilation definitions
- Updated & added more packets types & methods

## Client / Connection
- Removed sock receive & full packet fields from connection
- Updated client get_next_packet () & receive related methods

## Handler
- Added handler receive error definitions & methods
- Refactored handler_do () related methods and removed extra checks
- Removed SockReceive structure definition & methods
- Refactored main cerver packet handler related methods
- Removed old cerver_receive_handle_buffer () implementation

## Tests
- Checking packet's data integrity in test app handlers
- Updated send packet methods in client packets integration test