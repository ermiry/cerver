## Threads
- Added base worker structure definition & methods
- Added methods to start, stop & end single worker
- Added method to push job to worker job queue
- Added job queue wait and signal methods
- Added method to resume an stopped worker

## HTTP
- Added ability to register a worker to HTTP admin
- Added base HTTP admin workers route & methods

## Examples
- Added dedicated example data structure & methods
- Added base dedicated HTTP worker example

## Tests
- Updated test curl methods to take an expected status
- Updated web clients tests with new curl methods
- Removed extra logs from web cerver integration tests
- Added base dedicated data in test app sources
- Added curl full with auth & post two form values methods
- Added base web worker client & service integration tests
- Updated test workflow with new worker integration test
- Handling admin workers in dedicated integration test
- Added base HTTP admin workers methods unit tests