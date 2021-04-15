## General
- Added base file-system methods in dedicated sources

## Client
- Split client_connection_start () into dedicated connection methods

## Connection
- Added base methods to send packets using connection's queue
- Added dedicated method to en-queue a packet in connection

## Packets
- Refactored packets stats methods to use the correct types
- Added base packet_send_actual () to send a tcp packet
- Added dedicated packets init requests methods

## Threads
- Added dedicated THREADS_DEBUG definition
- Refactored JobQueue methods to handle different types
- Refactored thread_set_name () to handle variable arguments

## Tests
- Added dedicated client & cerver queue integration tests
- Added base bsem related methods unit tests
- Added based jobs structures methods unit tests
- Added base job queue methods unit tests
- Added base dedicated system methods unit tests