## General
- Split timer methods to create representations in custom buffer
- Added cerver_create_web () wrapper method

## Handler
- Added receive related methods in dedicated sources
- Added the ability to set a custom cerver max received packet size
- Handling max received packet size in main handle_buffer ()
- Added dedicated field for client max received packet size
- Handling EINTR errno in cerver_accept ()

## Threads
- Added dedicated THREADS_DEBUG definition
- Added base JobHandler structure definitions & methods
- Added extra checks in bsem_delete () and refactored sources
- Added dedicated methods to create & add to job queues
- Refactored thread_set_name () to handle variable arguments

## HTTP
- Added dedicated method to get HttpCerver reference
- Handling stats in http_response_send_file ()
- Added HTTP receive types to handle files & routes
- Added bad_user_error dedicated HTTP response

## Tests
- Added base bsem related methods unit tests
- Added based jobs structures methods unit tests
- Added base job queue methods unit tests
- Added base thpool methods unit tests