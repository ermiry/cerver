## General
- Added the sends with a deadline of the 2.0b-66 in sockets
- Running autobahn in the tests & with sanitizers scheduled
- Renamed the third-party file of the repository to lowercase
- Updated roadmap with WebSocket as 2.1b-9 & later betas
- Added incompatibilities of the 2.1b-9 with the WebSockets
- Updated CLAUDE.md with WebSocket, its tests & its client

## HTTP
- Sending the HTTP responses with the deadline of a cerver
- Added an incremental UTF-8 validator of WebSocket texts
- Added WebSocket frame encoder & its incremental parser
- Added permessage-deflate of RFC 7692 with zlib streams
- Added a WebSocket handshake of servers & of the clients
- Added the rules of WebSocket messages, without sockets
- Added the WebSocket settings that the groups also inherit
- Added WebSocket endpoints & their groups to routes tree
- Added WebSocket connections & their registry with rooms
- Upgrading the requests to WebSocket endpoints in cerver
- Closing the WebSocket connections with 1001 in the drain
- Added WebSocket metrics & its section of the admin stats
- Added a public WebSocket client with retries & its backoff

## Examples
- Added WebSocket benchmarks & checks of its allocations
- Added a WebSocket example of an echo, rooms & its page

## Tests
- Added unit tests for WebSocket texts, frames & messages
- Added unit tests of the WebSocket handshake & of deflate
- Added WebSocket server & client tests in a single process
- Testing upgrades of the WebSocket endpoints in a receive
- Added the fuzzers of the WebSocket frames & handshake
- Running autobahn-testsuite against the server & the client
- Added a WebSocket server & client pair to the integrations
- Running the WebSocket pair behind nginx with its upgrade