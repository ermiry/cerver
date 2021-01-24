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
- Handling EAGAIN as timeout in balancer_client_receive ()
- Fixed in internal balancer & service handler methods

## Tests
- Added base balancer methods unit tests
- Added dedicated balancer cerver & client integration tests
- Handling load balancer integration tests in dedicated test script
- Handling sock fd in responses header in example & test apps
- Replaced time-stamp for id in example & test app message structure
- Checking app message integrity in test app handler methods