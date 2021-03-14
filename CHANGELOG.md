## Threads
- Passing data reference instead of handler in jobqueue

## HTTP
- Added base HTTP admin methods in dedicated sources
- Added extra check for base route in http_route_child_add ()
- Checking for base route when searching children routes
- Added option to enable admin routes in HTTP sources
- Checking for HTTP routes size in http_cerver_init ()

## Examples
- Handling json error in user_parse_from_json ()
- Added dedicated HTTP example to enable admin routes

## Tests
- Added more methods in test client curl sources
- Requesting to multiple endpoints in api integration test
- Added dedicated jwt keys for web api integration test
- Added an exit code in base web integration test
- Added dedicated image to be used for upload tests
