## Files
- Added file_move_to () using "mv" command

## HTTP
- Added http_response_add_json_headers ()
- Added dedicated HTTP origin structure definition
- Added an origins white-list to be used for CORS
- Added dedicated HTTP responses CORS methods
- Added ability to set CORS headers in HTTP admin
- Added method to create and send admin responses
- Added ability to set custom auth for admin routes
- Added methods to move multi-part files
- Added method to print moved request files

## Tests
- Added dedicated HTTP origin methods unit tests
- Added base HTTP admin methods unit tests
- Added HTTP origins white-list integration test