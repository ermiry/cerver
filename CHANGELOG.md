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
- Building the tests of each of the jobs in the CI with zlib too
- Added the sha-2 & fastbase64 code to the third-party file
- Updated the CLAUDE.md with the core & faults in the 1.9.2
- Added the changes of the 2.0b-79 to the incompatibilities

## HTTP
- Adding the default headers of the cerver to each response
- Checking each static path of the list for the protected one
- Returning no HTTP receive without its parser or its request
- Answering 500 to a request whose url was never allocated
- Releasing the multipart parts & each attribute not inserted
- Releasing every error of OpenSSL after signing & verifying
- Returning the values of the getters without const qualifiers
- Using constant pointers for the static strings of HTTP files

## Tests
- Failing an allocation after some calls & sweeping the paths
- Added the sweeps of four core paths into the faults binary
- Added a faults test of a thread group while its clock jumps
- Added a faults test of the strings that lose their characters
- Registering the sweeps & the clock tests in the faults main
- Added a drain test of a thpool & a connection in its thread
- Added a script that compiles every header of cerver alone
- Logging how the supervised servers ended in their last run
- Added a test of the default headers of compiled responses
- Added a faults test for the HTTP receive without its parser
- Added the sweeps of four HTTP requests to the faults test
- Registering the HTTP request sweeps in the main of faults