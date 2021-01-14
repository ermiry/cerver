## General
- Added dedicated makefile instructions to clean each directory
- Added development & production builder image Dockerfile
- Added dedicated coverage script
- Updated docker images build documentation
- Added builders workflow to test builders docker images build
- Added NATIVE option to add  -march=native compilation flag
- Added base cerver types identifiers definitions
- Added workflow to check matching versions in source & file

## Tests
- Refactored test run script to exit on any error
- Added more check macros definitions in test header
- Added test data structure and methods in dedicated source
- Added base main cerver methods unit tests
- Added base avl methods unit tests
- Added dedicated test to check for version match

## Fixes
- Updated avl methods to fix return value errors
- Fixed string truncation warnings in main sources