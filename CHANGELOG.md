## General
- Reading a cerver info without freeing the client connection
- Keeping the connection of a request whose response fails
- Updated the README & the CLAUDE.md for the 1.9.1 plans
- Added the changes of the 1.9.1 to the incompatibilities file

## Tests
- Added tests of the connects & requests that cervers close
- Watching the allocations & their frees in the faults binaries
- Added a faults test for the client update thread that leaked
- Registering the client tests in the main of the faults binary