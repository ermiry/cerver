## General
- Updated the README, CLAUDE.md & the roadmap for F8a
- Added the changes of the 2.1b-22 to the incompatibility file

## TLS
- Added TLS to every socket & client, provided by OpenSSL

## HTTP
- Added TLS to the web cervers, mTLS by group & early data
- Running WebSockets over TLS with a client of wss:// URLs
- Running the short methods of WebSockets in their loops

## Tests
- Added the tests of TLS, OCSP, PKCS#11, kTLS & its fuzzer
- Added the TLS pairs with nginx, testssl.sh & autobahn wss
- Analyzing the sources that include the OpenSSL provider
