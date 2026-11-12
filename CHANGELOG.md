## General
- Updated roadmap with the hot objects that HTTP recycles
- Updated tests roadmap with mutants of the HTTP recycler
- Updated CLAUDE.md with the recycled responses & body
- Added the incompatibilities of 2.0b-68 & recycled objects

## HTTP
- Added a recycler for HTTP responses & buffers per thread
- Keeping stats of the files of each request in thread arenas
- Recycling responses with their own headers written inside
- Reserving the bodies in recycled buffers & request arenas

## Tests
- Added tests of the recycled responses & the body buffers
- Registering the recycle tests in the main file of HTTP tests
- Expecting bodies reserved in the classes of reused buffers
- Lowering allocations per request for the performance tests