## General
- Added latest dedicated main cerver logo & banner images
- Updated cerver project description to match main branch

## HTTP
- Added custom temp header in multi-part to be used as buffer
- Added dedicated method to check if MultiPart instance is empty
- Handling when a multi-part header field is split between reads
- Refactored how mpart header values are handled when split
- Added possible fix if mpart actual value is split between calls
- Moved HTTP URL methods to dedicated sources from main

## Tests
- Added custom web integration test to check if mparts are empty
- Added more custom HTTP multi-part methods unit tests