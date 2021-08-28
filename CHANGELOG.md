## General
- Added ability to allocate a fixed size String structure
- Added base Dockerfile to be used for local tests
- Added dedicated script to build local image
- Added base development nginx configuration
- Added base local environment compose configuration
- Added script to run cerver local image

## HTTP
- HTTP response headers are allocated with a fixed size
- Refactored HTTP response compile header methods
- Added methods to add response videos related headers
- Added base dedicated HTTP utils structures & methods
- Added method to get content type value by extension
- Added HTTP request method to parse range header
- Added base HTTP response handle video implementation
- Added dedicated method to print full request headers
- Disabled HTTP headers print in receive handle methods

## Examples
- Added dedicated example public video html source
- Added base example to showcase HTTP video stream

## Tests
- Updated HTTP content types unit tests methods
- Added base dedicated HTTP utils methods unit tests