## General
- Added sanitizer option in makefile to compile with fsanitize
- Using gcc 13.4 & bookworm slim images in test Dockerfile
- Added sanitizers matrix job in build workflow for unit tests
- Added handler integration test step & codecov v4 in tests
- Updated beta & production workflows to use latest actions
- Updated builder & version workflows to use newer actions
- Getting submodules with checkout v4 in docs workflow job
- Using gh-release action in both pre-release & release jobs
- Defining CERVER_INLINE as empty when compiling in C++
- Added size definition for errno description buffers in errors
- Added methods to create and append bytes in String type
- Compiling base64 AVX methods only if they are supported
- Updated network, input and serializer methods code style
- Updated collections, threads and utils methods code style
- Fixed bsem_reset() creating again its mutex and condition
- Setting job queue handler thread id when creating a queue
- Using dlist_insert_at_end() when pushing data into a pool
- Fixed connection delete method removing socket too early
- Added option to ignore SIGPIPE signals when cerver starts
- Updated benchmarks code style with spaced method calls
- Added roadmap file with the main framework work phases
- Added CLAUDE.md with project build, tests & conventions

## HTTP
- Replaced typographic quotes in HTTP headers map values
- Added missing header include in HTTP URL dedicated file
- Including JSON utf header in its sources to build with g++
- Including JWT openssl header in sources to build with g++
- Updated vendored HTTP parser sources with spaced calls
- Updated vendored JSON headers and sources code style
- Updated HTTP auth sources code style using spaced calls
- Handling send errors & retries in HTTP responses sources
- Added MultiPart error value to check if a file failed to save
- Added option for routes to authenticate with request body
- Handling HTTP tokens split between reads & receive limits
- Added HTTP receive diagnostics & rejected requests logs
- Fixed JWT base64 decode writing out of its buffer bounds

## Examples
- Updated examples sources with spaced method calls style
- Setting web example auth route to authenticate with body

## Tests
- Authenticating custom web test route with multi-part body
- Added pool collection unit tests using concurrent threads
- Fixed dlist unit tests race condition when reading dlist end
- Fixed htab unit tests double free when inserting final value
- Added String create and append bytes methods unit tests
- Added HTTP receive unit tests with requests split by bytes
- Updated HTTP responses unit tests with Connection close
- Fixed packets unit tests checking binary data with strlen()
- Removed an overflowing case from c string copy unit tests
- Checking random range inclusive values in math unit tests
- Updated JSON unit tests sources with spaced calls styling
- Updated JWT unit tests sources with spaced method calls
- Running version unit test in the dedicated unit tests script