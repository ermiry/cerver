## General
- Replaced the serializer with CBOR objects by their schema
- Creating packets with objects & reading them by schemas
- Sending the info of the cerver as an object of its summary
- Sending the error packets as objects with i64 timestamps
- Sending the file headers as objects & validating the names
- Renamed the isRunning field of the cerver as plain running
- Answering test packets with only the size of their headers
- Added the Beta 1.8b-7 changes to the incompatibilities file

## Connection
- Sending the packet headers field by field in network order
- Authenticating with session tokens & credentials schemas
- Handling auth failures of dropped connections & of clients
- Releasing new connections that no handler takes & clients
- Answering an error to auth packets of authenticated peers

## Balancer
- Routing the balancer responses with route ids of own slots

## Examples
- Encoding headers of the example app & of client requests

## Tests
- Added header tests of wire bytes, of rejects & of requests
- Added serializer tests with the CBOR vectors of RFC 8949
- Encoding the headers in tests of receive, routing & clients
- Using schemas in cerver, peers, files, session & UDP tests
- Added features tests of auth by schema & pings with data
- Added balancer tests of the route generations & their limit
- Added a schemas pair of an older client & of a new cerver
- Added a serializer fuzzer & encoded headers in the corpus