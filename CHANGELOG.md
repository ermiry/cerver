## General
- Updated roadmap with the fixes of the 72 as implemented
- Updated CLAUDE.md with trailers, targets & suffix ranges
- Added incompatibilities of the 2.0b-72 limits & its defaults

## HTTP
- Limiting the trailers of a chunked body like the headers are
- Joining the values of a repeated header without copying it
- Routing absolute targets by their path, without a fragment
- Added the setter of the max size of custom headers, 8 KiB
- Removing the spaces & the tabs after the value of headers
- Closing the requests that are not received in 300 seconds
- Added the suffix range that sends the last bytes of a video

## Tests
- Added tests of the trailers & the custom headers size limit
- Testing header values, repeated headers & absolute forms
- Added tests of the suffix range in requests & in a video file
- Rebuilding the tests & benchmarks if their headers change
- Failing the performance script when its server or load fails
- Running the long fuzzing of every target in a job of its own