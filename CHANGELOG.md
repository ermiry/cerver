## HTTP
- Refactored http_cerver_print_origins_whitelist ()
- Added more responses CORS related headers methods
- Handling "Access-Control-Allow-Credentials" header in admin responses
- Added ability to set custom data in HTTP request
- Added HTTP requests data related get and set methods

## Fixes
- Fixed error when adding CORS based on request

## Tests
- Added HTTP request data methods unit tests
- Added HTTP response CORS methods unit tests