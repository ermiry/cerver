## General
- Building tests & examples with every warning of the library
- Compiling every source without the libraries that it links to
- Added a zero initializer & a printf format checker to config
- Initializing the structs to zero both in the C & in C++ builds
- Casting the void pointers that g++ does not convert for us
- Comparing the recycle limits & the floats with no warnings
- Fixed the formats of the stats & the logs of the cerver core
- Checking the formats of the logs & the strings like in printf
- Added a final newline to the headers & sources of the core
- Running the CI with every warning as an error & with clang
- Not ending the connections that a signal interrupts in recv
- Creating the thpool only in the cervers of the threads type
- Documented the handler types, their limits & UDP cervers
- Removed the serialized strings that none of the code used
- Fixed the benchmark warnings with the flags of the library
- Added the Beta 1.8b-9 changes to the incompatibilities file

## Connection
- Passing the user arguments to the error actions of a client
- Triggering the client events with a response that they own
- Added a destroy lobby error & the new lobby client events

## Game
- Added the init, the player timeout & the fps of game types
- Rewrote the lobbies & the game requests with the objects
- Taking the lobby of a received packet from the connection
- Counting the stats of the lobbies with the atomic additions
- Adding the players of a lobby to a score board by their ids

## Examples
- Fixed the formats & the unused arguments of all examples
- Rewrote the game example with the objects of the lobbies
- Added a game client example that joins & plays in its lobby

## Tests
- Fuzzing the lobby objects with the serializer schemas now
- Evaluating only once the values of the checks in each test
- Fixed the formats, unused arguments & mains of the tests
- Added core tests of the interrupted receive & of the thpool
- Added in memory game tests of the lobbies & their players
- Added game tests of requests with both the poll & threads
- Registering the lobby & requests tests in the main of game
- Added a game pair of two players in another process each
- Running the game example as an integration pair with IPv4
- Added a fuzzer of the game requests with its seed corpus
- Running the game tests with thread sanitizer in schedules