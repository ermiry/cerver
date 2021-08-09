## Threads
- Added methods to push job to worker with & without work
- Refactored existing pthreads cond & mutex methods names
- Also added dedicated mutex lock & unlock wrapper methods
- Updated sources with new thread related wrapper methods
- Added ability to set reference and remove ref () in worker

## Examples
- Updated HTTP worker example to use new worker_push_job ()

## Tests
- Handling new thread related methods in existing tests
- Added unit tests for new worker reference related methods