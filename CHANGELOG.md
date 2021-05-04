## HTTP
- Added HTTP status to response render methods
- Refactored HTTP response render methods headers
- Returning HTTP status none string if no match
- Refactored http_method_str () to handle none type
- Added more HTTP content types definitions
- Refactored HTTP methods definitions names
- Refactored HTTP request to use common headers
- Updated HTTP response to use headers definitions

## Fixes
- Added extra reference in handler to fix Issue #308

## Tests
- Running unit tests directly from test script
- Updated tests to handle new render methods
- Updated HTTP requests & response unit tests
- Updated methods in HTTP content unit tests
- Added dedicated HTTP headers & methods tests