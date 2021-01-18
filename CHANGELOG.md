## General
- Updated cerver default definitions in main cerver header
- Moved definitions from auth & handler headers to main cerver header
- Refactored cerver_new () & cerver init methods to use new default values
- Refactored main cerver methods definitions structure
- Removed poll type as the default handler when creating a new cerver
- Cerver's handle type must be manually specified before calling cerver_start ()
- Added dedicated development & test Dockerfiles
- Added dedicated Dockerfile to be used by docker hub's automated builds
- Added base network_hostname_to_ip ()
- Added base sock_set_reusable () to set socket's reusable flags
- Added new implementations of base64 methods using avx2 instructions
- Updated sha256 implementation and added user friendly methods
- Added Work type which takes and returns a void pointer
- Refactored cerver & client events & errors to use Work instead Action
- Added extern "C" modifiers in headers witch check for __cplusplus
- Added cerver documentation submodule
- Added base documentation workflow
- Added base codecov configuration
- Updated makefile to compile sources & tests with coverage flags
- Added dedicated version file
- Added CHANGELOG.md to keep track of the latest changes
- Added dedicated beta & production Dockerfiles
- Added base cerver-cmongo integration Dockerfiles and workflows
- Adedd more cerver log methods
- Removed HTTP header & source

## Clients
- Refactored client header & sources organization
- Added base client connections status definitions
- Refactored client_remove_connection () to use ClientConnectionsStatus

## Connections
- Refactored connection custom receive to take buffer & buffer size
- Refactored connection default values definitions
- Updated connection sources organization

## Handler
- Removed original cerver_receive () as it will not be needed anymore
- Refactored cerver_receive_handle_buffer () to be used in one thread
- Removed sock receive check & mutex locks in receive_handle_buffer ()
- Removed extra check in cerver_receive_handle_spare_packet ()
- Passing received size to cerver_receive related methods & structures
- Removed checks for buffer & size in cerver_receive_handle_buffer ()
- Replaced buffer_size with received_size in receive_handle_buffer ()
- Added base main handler errors definitions
- Refactored main cerver handler methods to use CerverHandlerError
- Refactored cerver_receive_handle_failed () to be used in one thread

## Auth
- Added ability to set cerver's on hold receive buffer size
- Refactored on hold poll to use a constant buffer to handle receives
- Added auth errors definitions to be used in internal auth methods

## Admin
- Refactored admin cerver default values definitions
- Refactored admin poll methods to be used in just one thread
- Added base admin cerver handler errors definitions
- Added base admin connections status definitions

## Collections
- Updated dlist with latest available methods
- Updated avl & htab sources with latest methods

## Examples
- Updated examples to manually specify their handler type
- Refactored makefile example & removed mongo dependency
- Updated examples event methods to be of the correct type

## Tests
- Added check macros in dedicated test header
- Added dedicated script to run tests
- Added base tests actions in build workflow
- Added instructions in make to compile tests
- Added dlist test methods
- Added base cerver & client integration tests
- Added test app sources to be used for integration tests

## Benchmarks
- Refactored bench script to compile sources with TYPE=test
- Updated makefile to correctly build base web benchmark
- Added dedicated script to build sources to be used in benchmarks
- Added base64 benchmark - compile sources with optimization flags