## General
- Updated roadmap with F1 implemented as the 2.1b-11 beta
- Updated CLAUDE.md with structured responses & a reset
- Added incompatibilities of the 2.1b-11 with the headers list

## HTTP
- Added a list of headers with repeated ones & of any name
- Structured HTTP responses with a single header serializer
- Separated what belongs to a request in the HTTP receives
- Marking the responses that end without a close as framed

## Tests
- Added unit tests of the headers list with its growth & limits
- Testing the headers of responses & their compiled header
- Checking Connection close once in every kind of response
- Testing framed responses & resets of a receive by request
- Added the pipeline fuzzer that resets between its requests