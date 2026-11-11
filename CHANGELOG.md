## General
- Running the test jobs in containers that have IPv6 enabled
- Described the disconnect matrix & the scheduled jobs too
- Planned llhttp as the 2.1b-8 & WebSocket on threads next
- Removed http_parser & now compiling the parser of llhttp
- Added the third-party code of cerver & its licenses to repo
- Added incompatibilities of the 2.1b-8 with the llhttp parser
- Updated CLAUDE.md with llhttp, its methods & the lenient

## HTTP
- Added the split headers of llhttp 9.4.3 with its typed spans
- Seeded llhttp.c of llhttp 9.4.3 restyled with a verified script
- Updated the api & http sources of the parser to llhttp 9.4.3
- Using one table of methods with the same values as llhttp
- Fitting every method of llhttp in the methods of the routes
- Parsing the requests with llhttp embedded in each receive
- Using the version & the errors of llhttp in streams & in logs

## Examples
- Measuring llhttp alone in the benchmark of the http parser

## Tests
- Testing every method of llhttp in the unit tests of methods
- Checking the values of llhttp in the request methods tests
- Testing the parser in the receive & its lenient flags in HTTP
- Added receive tests of the strict mode & methods of llhttp
- Added a fuzzer of the HTTP requests with the lenient flags
- Added request seeds of QUERY, WebDAV, upgrades & PRI