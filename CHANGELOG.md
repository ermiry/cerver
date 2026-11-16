## General
- Updated roadmap & CLAUDE.md with L1: the cerver JSON
- Added the changes of the 3.0b-6 to the incompatibility file

## JSON
- Added the types, the errors & the flags of the cerver JSON
- Added the scan of the JSON strings, the escapes & UTF-8
- Added the numbers of the JSON with their table of powers
- Added the JSON document with its values inside an arena
- Added the getters of the JSON values & the search by key
- Added the JSON pointer of RFC 6901 over the documents
- Added the JSON writer into a buffer or an arena with flags
- Added the JSON reader by events & json into the umbrella

## HTTP
- Added http_request_json () to read the body into its arena

## Tests
- Moved the Jansson tests & its fuzzer to the jansson target
- Added the tests of the JSON document: the errors & limits
- Added the tests of the JSON values & of the search by key
- Added the tests of the JSON strings: the escapes & UTF-8
- Added the tests of the JSON numbers against the C library
- Added the tests of the JSON pointers & of the JSON writer
- Added the tests of the JSON events & the json test binary
- Added the JSON conformance runner over JSONTestSuite
- Added the json, compare, number, writer and scan fuzzers
- Testing the allocations of the JSON that fail with the faults
- Testing http_request_json () in units & inside a real cerver
- Reading the POST JSON of benchmarks with 0 allocations
- Measuring the cerver JSON against Jansson & in a search
