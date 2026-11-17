## General
- Ending a connection when one of its sends ran out of time
- Using the shared args for the events & the errors of clients
- Fixed BMP images, existing dirs, n_read & each partial file
- Fixed the docs of the job queue, the file send & unregister
- Added the changes of the 2.1b-20 to the incompatibility file

## HTTP
- Checking the strings of a static path & its insert in the lists
- Reading again a NUL that follows a value in a JSON buffer
- Saying Connection: close in a response that has no length
- Checking each static path of the list for the protected one

## Tests
- Draining the fuzz target of the client with its send timeouts
- Added a test of a second admin of the core cerver at once
