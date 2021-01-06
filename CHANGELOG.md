## General
- Added extern "C" modifiers in headers witch check for __cplusplus
- Added more instructions in makefile to remove specific directories
- Added base codecov configuration
- Added dedicated coverage workflow to run on every pull request
- Added pre-release workflow to be executed on beta branches
- Added base release workflow to create new releases from master
- Added dedicated beta & production Dockerfiles
- Updated compile & docker jobs in beta & production workflows
- Added dedicated coverage script
- Added base cerver-cmongo integration Dockerfiles
- Added mongo workflow to build & push cerver-cmongo images
- Added cerver documentation submodule & dedicated workflow
- Added dedicated version file & CHANGELOG.md

## HTTP
- Moved json internal definitions to dedicated source
- Moved inline json definitions from headers to sources

## Examples
- Updated examples cerver configuration and better main methods

## Tests
- Added more tests definitions & macros
- Added base htab unit tests
- Updated existing test script & c string unit tests