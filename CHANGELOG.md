## General
- Updated the README with the TLS that runs in their loops
- Updated CLAUDE.md with the TLS of the loops & its tests
- Updated the roadmap with F8b & the new order of F7 & F9
- Added the changes of the 2.1b-23 to the incompatibility file

## TLS
- Added a handshake of TLS & its close that never wait at all
- Rotating the keys of TLS tickets on demand from the tests
- Attaching TLS to sockets & allowing it with the event loops

## HTTP
- Fixed the default of the client certificate in group & routes
- Added TLS to the handshakes, reads & writes of the loops
- Running WebSockets over TLS in their loops without waits

## Tests
- Testing the defaults of the settings of the routes & groups
- Added a certificate expiring soon to the TLS tests & types
- Running every test of the TLS server in both handler types
- Running the WebSocket tests of TLS with & without a park
- Running the TLS disconnects in threads & the event loops
- Testing that the handshakes of clients never hold accept ()
- Testing that the routes of packets refuse sockets with TLS
- Fixed the mTLS client of testssl.sh & the findings it ignores
- Lowered the resumption nginx needs in loads over TLS 1.3
