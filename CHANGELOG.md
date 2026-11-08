## General
- Using gcc 16.2 instead of 13.4 in the test Dockerfile image
- Using gcc 16.2 & trixie slim images in the other Dockerfiles
- Fixed file_read_internal () reading every piece at the start
- Removed unused fps counter from admin_cerver_update ()
- Updated tests roadmap with the 2.0b-61 new server tests
- Updated roadmap with the 2.0b-61 fixes before the 2.1 line
- Updated CLAUDE.md with the send waits, 500 & sequence

## HTTP
- Using poll () & a send deadline when a socket buffer is full
- Answering with 500 the requests a handler did not answer
- Added the request sequence in the default uploads names

## Tests
- Added server tests of unanswered, full socket & fd 0 sends
- Added receive test of the uploads dirnames in one second
- Added an unanswered route in the features integration pair