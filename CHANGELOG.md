## General
- Checking what the frames & parser benchmarks measured
- Rebuilding the tests & benchmarks if their headers change
- Running the long fuzzing of every target in a job of its own
- Building the benchmarks & units with zlib in each build job
- Updated roadmap with the 2.1b-13 implemented & its rest
- Updated CLAUDE.md with the opaque structs & the filters
- Added incompatibilities of the 2.1b-13 with its API changes

## HTTP
- Limiting the trailers of a chunked body like the headers are
- Joining the values of a repeated header without copying it
- Routing absolute targets by their path, without a fragment
- Added the setter of the max size of custom headers, 8 KiB
- Removing the spaces & the tabs after the value of headers
- Closing the requests that are not received in 300 seconds
- Added the suffix range that sends the last bytes of a video
- Serving only HTTP/1.0 & 1.1, with 400 or 505 for the others
- Rejecting bodies without an end & NULs with lenient flags
- Choosing the headers of gzip & 304 responses in their list
- Hiding the WebSocket structs behind getters & their stats
- Fixed deflate messages that end in a block or with BFINAL
- Failing the WebSocket frames whose length is not minimal
- Rejecting WebSocket keys that are not in a canonical form
- Fixed the refused sends & the end of a closing WebSocket
- Deleting a WebSocket client in its methods & in an attempt
- Fixed the WebSocket client backoff, its url, header & close
- Added the _milliseconds suffix to the WebSocket timeouts
- Added the 101 headers of an on_upgrade & a query getter

## Tests
- Scaling short test timeouts under valgrind & the sanitizers
- Added tests of the trailers & the custom headers size limit
- Testing header values, repeated headers & absolute forms
- Added tests of the suffix range in requests & in a video file
- Testing the protocols, the versions & every lenient flag left
- Testing filtered headers of a gzip & of a 304 in both forms
- Added tests of deflate blocks, frame lengths & of the keys
- Testing upgrades, settings, rooms & the clients gone silent
- Testing the ends of the client reconnection & its messages
- Fuzzing the frames with all the negotiated deflate settings
- Failing an autobahn report that is missing any of 517 cases
- Reporting how wstest ended when an autobahn mode fails
- Checking that the WebSocket pair agreed its compression
- Failing the performance script when its server or load fails