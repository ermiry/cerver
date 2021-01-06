## General
- Added extern "C" modifiers in headers witch check for __cplusplus
- Added clear makefile instruction to remove all the objects
- Changed beta & production workflows tests to use clear instead of clean
- Added cerver documentation submodule
- Added base documentation workflow
- Added base codecov configuration
- Updated makefile to compile sources & tests with coverage flags
- Added dedicated version file
- Added CHANGELOG.md to keep track of the latest changes
- Added dedicated coverage workflow to run on every pull request
- Added pre-release workflow to be executed on beta branches
- Added base release workflow to create new releases from master
- Added TYPE=beta to compile sources and examples for beta tests
- Added dedicated beta & production Dockerfiles
- Added base cerver-cmongo integration Dockerfiles and workflows

## Examples
- Updated examples cerver configuration and better main methods
- Added dedicated collections test sources

## Tests
- Added more tests definitions & macros
- Added base htab unit tests