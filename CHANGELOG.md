## General
- Updated test Dockerfile to use tests instead of examples
- Added simple web integration test to test workflow

## HTTP
- Added global responses headers in HTTP cerver structure
- Moved HTTP content types definitions to dedicated sources
- Added methods to add content related headers to responses
- Added base HTTP responses pool & added related public methods
- Handling global headers in http_response_compile_header ()
- Added new methods to create HTTP responses with a json body

## Tests
- Added base integration tests compilation rules in makefile
- Added base test app definitions & methods
- Refactored test users methods to be part of the test app sources
- Added base web cerver integration tests in dedicated directory
- Added base HTTP responses methods unit tests