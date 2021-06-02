## General
- Added method to recursively create directories
- Added files_sanitize_complete_filename ()

## HTTP
- Refactored HTTP request's dirname definition
- Refactored HTTP multi-part dirname generator
- Changed HTTP uploads path type and methods
- Added the ability to set file & dir creation modes
- Refactored HTTP multi-part related definitions
- Added dedicated multi-part structure getters
- Added methods to traverse request's multi-parts
- Refactored HTTP headers enum type definition
- Added new HttpHeader structure definition
- Added HTTP multi-part types definitions
- Refactored uploads_filename_generator () signature
- Added multi-parts pool and updated internal methods
- Changed buffer size when handling header field
- Refactored http_mpart_get_boundary () to use static buffer
- Added is_multi_part flag in HttpReceive structure

## Examples
- Added multi-parts iter handlers in uploads example
- Updated jobs example with new request methods
- Added multiple files handler in upload example
- Added dedicated example to test uploads dirs

## Tests
- Added test_check_str_empty () in test header
- Added recursive mkdir and sanitize unit tests
- Added base dedicated multi-parts unit tests
- Added TEST_DEBUG definition to print extra info
- Updated HTTP jobs integration test with new methods
- Added curl method to upload two files in one request
- Updated upload integration test with new handlers
- Added base HTTP multiple integration test sources