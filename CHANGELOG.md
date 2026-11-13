## General
- Keeping the corpus of the fuzzers only, in the gitattributes
- Naming the labs betas & building the labs image in CI jobs
- Licensing cerver under the Apache License with a NOTICE
- Added sha-2 & fastbase64 to the list of the code of others
- Added cerver crypto, with OpenSSL as its default provider
- Using cerver crypto in place of the direct calls to OpenSSL
- Added benchmarks of the JSON & parser with competitors
- Added the first results against the competitors in aarch64
- Replaced the roadmap with the roadmap of the 3.0 in labs
- Updated README with the 3.0 line & the licenses of cerver
- Updated CLAUDE.md with the labs line, its crypto & faults
- Added the incompatibilities of the 3.0b-1 from the 2.1b-13

## Tests
- Added the units of the random bytes, SHA-1 & the cleanse
- Added faults of memory & threads for the WebSocket path