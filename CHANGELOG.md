## General
- Updated roadmap & CLAUDE.md with L2: the JWT & keys
- Added the changes of the 3.0b-8 to the incompatibility file

## Crypto
- Added the keys & the signatures to the crypto of OpenSSL
- Added the crypto keys from a secret and the PEM of a key
- Added the crypto keys from the JWK with the cerver JSON
- Added the signatures of crypto over the buffers of a caller

## JWT
- Added the algorithms, the errors and the limits of the JWT
- Added the keys of the JWT: the key decides the algorithm
- Added cerver_jwt_sign () to sign some claims into a buffer
- Added the validation of the claims of any token of the JWT
- Added cerver_jwt_verify () and the jwt inside the umbrella

## HTTP
- Signing & verifying the tokens of HTTP on the cerver JWT
- Removed the includes of the old jwt headers of HTTP core
- Removed the JWT of libjwt & the internal copy of Jansson

## Examples
- Using the cerver JWT in the web examples of auth & users

## Tests
- Added the tests of the crypto keys with their own key files
- Added the tests of the keys of a JWK of each type & curve
- Added the tests of the signatures against the RFC vectors
- Replaced the tests of libjwt with the ones of the JWT keys
- Added the tests of signing tokens and of validating claims
- Added the tests of verifying tokens with the RFC examples
- Testing the tokens of HTTP byte by byte with the new JWT
- Using the cerver JWT in the test app, and in the web pairs
- Testing the allocations of crypto & the JWT when one fails
- Added the jwk fuzzer and the modes of the jwt fuzz target
- Added a differential fuzzer against the libjwt of the system
- Added the conformance runner of the JWT on Wycheproof
- Removed the tests and the fuzzers of the copy of Jansson
- Counting 10 allocations for a request with its bearer token
- Measuring the cerver JWT against libjwt, l8w8jwt, jwt-cpp
