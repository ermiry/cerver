## General
- Moved port & udp related definitions to network header
- Refactored cerver's name & welcome fields definitions
- Updated sources to handle new cerver name value
- Added cerver info alias definition & method
- Refactored THREAD_NAME_BUFFER_SIZE definition
- Refactored sources to use new thread_set_name ()

## Client
- Refactored client connect & start related methods
- Refactored client request to cerver methods

## Connection
- Added base connection state definitions & methods
- Added dedicated connection state mutex
- Added base connection attempt reconnect fields
- Refactored connection_connect () to update state
- Added client reference directly inside connection
- Moved connection start methods to connection sources
- Added ability to set connection's receive flags
- Added dedicated methods to reset connection values

## Threads
- Added ability to request jobs from queue by id
- Changed jobs methods return values types

## Examples
- Updated examples to handle new cerver fields

## Tests
- Updated cerver integration tests name checks
- Updated integration tests to handle welcome meesage
- Updated jobs unit tests with new fields