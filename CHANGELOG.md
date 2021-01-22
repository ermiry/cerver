## General
- Updated main sources with latest methods & definitions
- Added latest packet methods & refactored packet header
- Refactored makefile development definitions for sources & tests

## Handler
- Refactored handler do related methods organization
- Removed SockReceive structure definition and methods
- Removed unused cerver_receive_http () handler method
- Added new cerver_receive_handle_buffer () implementation

## Balancer
- Updated internal balancer methods to use connection values
- Refactored balancer receive handler methods
- Added check for __cplusplus definition in balancer header