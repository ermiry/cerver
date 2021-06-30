## General
- Added script to clean and compile sources
- Added dedicated script to compile & install cerver
- Removed dedicated script to install libmongoc

## HTTP
- Added method to generate HTTP uploads paths
- Changed http_cerver_set_uploads_path () to only take a static path
- Added default HTTP dirnames & filenames generators
- Added ability to set route custom auth handler
- Added ability to enable auth in admin routes

## Examples
- Added dedicated HTTP routes authentication example

## Tests
- Updated curl methods used for integration tests
- Added dedicated HTTP routes methods unit tests
- Updated curl_post_form_value () to handle data
- Added base curl_perform_request () to better handle responses status codes
- Added base dedicated HTTP auth integration test