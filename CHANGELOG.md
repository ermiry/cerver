## General
- Updated main sources & examples compilation organization
- Removed openssl dependency from sources compilation
- Removed thpool name log in thread_do () internal method
- Setting libcerver library path when compiling examples & tests
- Updated test Dockerfile to compile tests and handle test app
- Refactored client_connection_get_next_packet () to allocate buffer
- Removed cerver-cmongo Dockerfiles & documentation
- Removed cerver-cmongo related test & deploy workflows
- Added new base test workflow to run unit & integration tests
- Removed dedicated coverage workflow

## Packets
- Added packet_send_ping () to send a test packet
- Added packet_send_request () to send a packet with request type
- Added packet_create_request () & refactored packets methods

## Examples
- Deleting event data references in test example

## Tests
- Split tests compilation in units & integrations
- Added base main threads methods unit tests
- Added base cerver & client integration tests
- Added base connection methods unit tests
- Refactored test script to run integration tests
- Added test app sources to be used for integration tests