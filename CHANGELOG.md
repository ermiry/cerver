## General
- Updated roadmap & CLAUDE.md with L3: the HTTP parser
- Added the changes of the 3.0b-9 to the incompatibility file
- Removed llhttp & the multipart copy from the third parties

## HTTP Parser
- Added the types, tables & the kernels of the cerver parser
- Added the head of the parser: the start line & all the fields
- Added the body of the parser: its length, chunks & trailers
- Added the multipart parser of cerver that lives in a receive

## HTTP
- Migrated the receive of every request to the cerver parser
- Using the parser for the response of the WebSocket client
- Removed llhttp & the multipart copy from the HTTP layers

## Tests
- Added the units of the HTTP parser in the http tests binary
- Added the smuggling suite to the requests of receive tests
- Added the conformance runner of HTTP on the llhttp tests
- Added differential fuzzers against llhttp & multipart-parser
- Added an origin of cerver for the HTTP Garden & its cases
- Testing the fields of a receive that fail to grow in the faults
- Measuring the parser against http_parser & in more cases
- Measuring the cerver multipart against multipart-parser-c
