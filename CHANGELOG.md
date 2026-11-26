## General
- Using one deadline for both the thpool & threads in a drain
- Waiting for each thread group until its monotonic deadline
- Releasing a new cerver that is missing its info or stats now
- Checking the connection list of new clients & the registers
- Returning no new connection that has no allocated socket
- Returning no string when its characters were not allocated
- Deprecated cerver_update_uptime () since nothing calls it
- Using constant pointers for the static strings of the cerver
- Building the library without recycling with RECYCLE=0 flag
- Building the benchmarks, the faults & the headers in its CI
- Uploading the artifacts of all the repeats of scheduled jobs
- Enabling IPv6 in the containers of the production tests job
- Updated the README & the CLAUDE.md for the 1.9.2 plan
- Added the changes of the 1.9.2 to the incompatibilities file

## Tests
- Failing an allocation after some calls & sweeping the paths
- Added the sweeps of four core paths into the faults binary
- Added a faults test of a thread group while its clock jumps
- Added a faults test of the strings that lose their characters
- Registering the sweeps & the clock tests in the faults main
- Added a drain test of a thpool & a connection in its thread
- Added a script that compiles every header of cerver alone
- Logging how the supervised servers ended in their last run