## General
- Added thpool_has_queued () to ask for jobs with no mutex
- Fixed the docs of the stop of the cerver with its stop pipe
- Added the changes of the 2.0b-76 to the incompatibilities

## HTTP
- Compiling a response without its header with only its data
- Using the full names in json.c & in the parameters of json.h

## Tests
- Draining the half-closed clients of the batch in disconnect
