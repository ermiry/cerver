## General
- Added the switch of libcurl & its flags to the main makefile
- Building cerver with libcurl in all the Dockerfiles of images
- Building the beta & the release of cerver using libcurl in CI
- Updated the README with the remote authentication of F7
- Updated CLAUDE.md with libcurl & remote authentication
- Updated the roadmap with what F7 did in the beta 2.1b-24
- Added the changes of the 2.1b-24 to the incompatibility file

## Events
- Added the sockets & deadline of libcurl to the event loops

## HTTP
- Added the remote authentication of the requests with curl
- Waiting for the remote authentication without pool threads
- Splitting the authorize of the headers around remote auths

## Tests
- Added the tests of the remote authentication & its service
- Registering the tests of remote auth & running them apart
- Building cerver with libcurl in the tests & in the test images
