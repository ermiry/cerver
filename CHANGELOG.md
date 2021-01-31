## General
- Refactored test Dockerfile to use external sources
- Refactored tests to mount working directory into Dockerfile
- Refactored cerver integration tests paths in test workflow
- Split timer methods to create representations in custom buffer

## Fixes
- Fixed wrong address reference in cerver_connection_create ()
- Fixed string related warnings in connection_update ()