## General
- Failing a fuzzer at the first undefined behavior UBSan finds
- Ignoring the autobahn reports in git & in the Docker builds
- Timing only the messages of a WebSocket benchmark run
- Updated roadmap with the fixes betas & F3 as the 2.1b-14
- Updated CLAUDE.md with the hidden parser & its streams
- Added incompatibilities of the 2.1b-12 with the hidden API

## HTTP
- Reading a Range only when it starts at a position of the file
- Splitting the query & the form pairs before decoding them
- Holding nothing when a WebSocket stream does not begin
- Creating a send deadline after the wait for other messages
- Counting bytes of the WebSocket streams & of fragments
- Rejecting the client urls with spaces & non token protocols
- Hiding the symbols of the parser & of WebSocket internals

## Examples
- Sending only what fits of a text in the WebSocket example
- Releasing the WebSocket cervers that fail with a teardown

## Tests
- Added tests of the Range header & of the values it ignores
- Testing the escaped separators in a query & in form values
- Encoding only the values of the forms of the api test client
- Added tests of WebSocket sends when the close was sent
- Testing WebSocket client urls, protocols & bytes counters