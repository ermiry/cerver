## General
- Refactored custom String type methods implementations
- Added method to append a c_str to a String structure
- Refactored c_string_create () utility implementation

## HTTP
- Correctly handling multiple calls to HTTP receive body
- Added new methods to set HTTP response headers
- Refactored HTTP response to use HttpHeader
- Changed how some response headers are set
- Added JSON value types HTTP responses methods
- Updated global HTTP responses headers types

## Examples
- Small types updates when logging in examples
- Removed JSON handlers from main web example
- Added base web example to showcase JSON methods

## Tests
- Added math utilities methods unit tests sources
- Added base dedicated string type methods unit tests
- Updated HTTP headers tests with latest methods
- Refactored HTTP tests to handle responses headers
- Removed JSON methods from main web tests
- Added base dedicated web JSON integration tests
- Handling types unit tests in dedicated run script
- Added JSON data files to be used in web tests
- Added dedicated curl method to post JSON
- Handling JSON web tests in script and workflow