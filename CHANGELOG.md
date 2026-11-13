## General
- Including stdarg.h in config & not a va_copy with memcpy
- Added the hidden visibility macro for the internal methods
- Added a canonical base64url encode & decode in base64
- Updating the UDP peers stats before releasing their count
- Fixed the comments of the sessions & handlers of the port
- Added Codecov coverage targets & ignoring the tests files
- Sorted the gitignore & added the coverage & fuzz artifacts
- Cleaning only the tests after the gcc tests, before the g++
- Removed the trailing spaces of the g++ checks in makefile
- Updated the docs submodule with base64url & UDP stats
- Added the changes of the 1.8.2 to the incompatibilities file

## Tests
- Added tests of a canonical base64url encode & its decode
- Waiting for the on hold stat after a UDP peer failed its auth