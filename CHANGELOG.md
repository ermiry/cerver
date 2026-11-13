## General
- Updated roadmap with the fixes of the 2.0b-71 & of the 72
- Updated CLAUDE.md with rejected headers & split queries
- Added the incompatibilities of the 2.0b-71 responses fixes

## HTTP
- Rejecting the headers of a response that do not fit in a slot
- Reading a Range only when it starts at a position of the file
- Splitting the query & the form pairs before decoding them

## Tests
- Failing a fuzzer at the first undefined behavior UBSan finds
- Added tests of long response headers & of a long preflight
- Added tests of the Range header & of the values it ignores
- Testing the escaped separators in a query & in form values
- Encoding only the values of the forms of the api test client