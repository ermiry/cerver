## General
- Planned TLS & HTTP/2 as phases F8 & F9 of the roadmap
- Using checkout v6 & upload artifact v7 in WebSocket jobs
- Updated roadmap with the merge of the 2.0b-70 in nightly
- Added incompatibilities of the 2.1b-10 with the http merge

## HTTP
- Stopping the parser when the static path rejects a request

## Tests
- Scaling disconnect waits & its send timeout under valgrind
- Using a reference UTF-8 validator in WebSocket text tests