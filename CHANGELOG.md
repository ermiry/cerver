## General
- Lowered the max UDP packet size to the IPv4 payload size
- Sending each packet in one datagram with its token if any
- Shared the auth core & taking unterminated tokens as data
- Added UDP cerver peers with references, token & sessions
- Not answering bad packets of UDP peers without a token
- Starting UDP cervers & keeping errno in the stop request
- Added the Beta 1.8b-4 changes to the incompatibilities file

## Connection
- Added datagram tokens to connections & a shut down flag
- Added UDP client connect, datagram receive & auth token

## Tests
- Added UDP unit tests of datagrams, tokens & packet sends
- Added UDP cerver peers tests with auth, sessions & limits
- Added UDP integration pair with an auth & sessions client
- Added datagram fuzz target with checks & the auth data