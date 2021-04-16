## General
- Added new packets init requests & send methods
- Added base file-system methods in dedicated sources
- Added dedicated Dockerfile to run local tests
- Handling queue & requests integration tests in workflow
- Added update apt cache in version workflow

## Client / Connection
- Added ability too send packets using a queue
- Refactored client connection methods to handle queue

## Threads
- Added extra checks in bsem_delete () and refactored sources
- Refactored JobQueue methods to handle different types
- Refactored thpool methods organization in sources
- Refactored thread_set_name () to handle variable arguments

## Tests
- Added dedicated client & cerver queue integration tests
- Added base bsem related methods unit tests
- Added based jobs structures methods unit tests
- Added base job queue methods unit tests
- Added base dedicated system methods unit tests