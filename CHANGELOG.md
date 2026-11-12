## General
- Added the licenses of the JSON & the JWT the library has
- Updated roadmap with the security of JWT & of the JSON
- Updated tests roadmap with the tokens & the JSON safety
- Updated CLAUDE.md with the JWT keys & the JSON limits
- Added the incompatibilities of the 2.0b-67 in JWT & JSON

## Core
- Including stdarg.h in config & not a va_copy with memcpy
- Added a canonical base64url encode & decode in base64

## HTTP
- Added JWT keys that are checked against their algorithms
- Signing & verifying the JWT with its keys & a strict parsing
- Matching names of the JWT algorithms as they are in RFC
- Verifying bearer tokens with the keys & the claims of HTTP
- Seeding the hash of JSON objects once with getentropy ()
- Limiting the memory of a JSON parse by its document size
- Reading & writing the JSON reals with a C locale of its own
- Fixed JSON dumps to a fd, its depth, UTF-8 errors & sizes
- Hiding the internal methods of the JSON from libcerver.so
- Building the JSON responses with the library & its escapes

## Tests
- Added tests of a canonical base64url encode & its decode
- Signing the ES tests with the right curve of each algorithm
- Updated JWT tests with their keys & the names of the RFC
- Added HTTP tests of the keys, the claims & bearer tokens
- Registering the tokens tests in the main file for HTTP tests
- Added safety JSON tests of seeds, limits, reals & escapes
- Registering JSON safety tests & the dumps to a descriptor
- Added tests of the JSON responses with escape & bodies
- Using bearers of up to their new size in the HTTP test files
- Waiting the whole response in the valgrind disconnect test
- Fuzzing the JWT with HMAC, ES, round trips & the bearers
- Fuzzing JSON with its flags, binary dumps, pack & unpack