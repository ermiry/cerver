## General
- Building the disconnect web cerver & its raw client in tests
- Running the disconnect matrix of each image in scheduled
- Updated roadmaps with the stage J of the disconnections
- Updated CLAUDE.md with the disconnect matrix & its pair
- Added incompatibilities of the 2.0b-65 with their counters

## HTTP
- Added request stages, client states & millisecond timeouts
- Ending a stream whose piece failed, without sending more
- Marking requests whose responses could not be sent fully
- Counting the clients that left by their stage & how they left
- Added metrics of the aborted, vanished & failed responses
- Sending all the requests counters in the admin stats JSON

## Tests
- Added disconnect tests of clients that leave on every byte
- Registering the disconnect tests in the main of HTTP tests
- Added receive tests of the uploads that can not be written
- Added a script to run the disconnect matrix in each image
- Added a disconnect web cerver to test behind every nginx
- Added a raw disconnect client for the nginx configurations
- Added nginx configurations of services & of two instances
- Running the disconnect client in every nginx configuration