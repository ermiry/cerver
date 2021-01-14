## General
- Added NATIVE option to add  -march=native compilation flag
- Enabling NATIVE option when performing benchmarks
- Updated jwt key generation instructions
- Updated libmongoc installation instructions
- Refactored json headers to use cerver export definitions
- Added base cerver types identifiers definitions

## HTTP
- Refactored jwt validation errors into a map
- Added new structures & methods to increase jwt generation performance
- Added methods to generate bearer tokens using a HttpJwt structure
- Added http jwt pool & public methods to interact with it
- Added dedicated method to load a private or public key

## Tests
- Refactored run tests scripts to exit on any error
- Added test_check_bool_eq () & test_check_unsigned_eq ()  macros definitions
- Added dedicated keys to be used in http jwt unit tests
- Added base avl collection unit tests
- Added base main cerver methods unit tests

## Fixes
- Fixed broken benchmarks instructions due to missing values
- Fixed wrong float check in jsonp_strtod ()
- Fixes in avl_is_not_empty () & avl_insert_node ()
- Fixed buffer overflows in json error methods