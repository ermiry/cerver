# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is the branch `core`, the 1.x line (betas `1.9b-N` in `beta`, releases to `master`): the core of cerver without HTTP. Since the 1.9b-1 it has the layout of the core of the 3.0 (`labs` 3.0b-2) and the same documentation, so the ports between the lines are direct; `labs` and `core` share the files of the core except the web parts of `core/{cerver,start,end}.c`, `core/cerver.h` (`cerver_create_web ()`), `handler/handler.{h,c}` (the HTTP receive) and the umbrella `cerver.h`, and `crypto/` only has `random` here. The license is Apache-2.0 (`LICENSE.md`, `NOTICE`); the code of other projects is in `thirdparty.md`.

Cerver is a C11 framework for building servers, built as a shared library (`bin/libcerver.so`). It supports the server types `CERVER_TYPE_CUSTOM`, `GAME`, `FILES` and `BALANCER` (`WEB` is the HTTP of the 2.x), and a client API for connecting to cervers.

## Build

The `makefile` is controlled by variables:
- `TYPE=development` (default; enables all `*_DEBUG`/`*_STATS` defines and `-g`), `test`, `beta`, `production`
- `COVERAGE=1` (only with `TYPE=test`), `NATIVE=1` (`-march=native`), `CC=g++` (the code must also compile as C++11), `CC=clang`, `SANITIZER=address|thread|undefined`
- `WERROR=1` makes every warning an error (the CI sets it): the library, the tests, the examples, the fuzzers and the benchmarks build with zero warnings in gcc, g++ and clang

```sh
make                    # build bin/libcerver.so
make examples           # build examples/bin/* against bin/libcerver.so
make test               # build all tests: objs, test app lib, units, integration
make clean
make install            # copy libcerver.so to /usr/local/lib and headers to /usr/local/include
make docs               # the reference of the headers with Doxygen, in reference/ (HTML and XML)
```

`make clean` goes right before every build. The Dockerfiles and the CI use `gcc:16.2`. `make docs` runs Doxygen 1.9.8 with the `Doxyfile` (`Dockerfile.docs` has it: `docker build -f Dockerfile.docs -t cerver-docs .` and `docker run --rm -v $PWD:/cerver cerver-docs`), and any warning fails it; its `INPUT` lists the documented folders. Every clang build adds `-Wdocumentation -Wdocumentation-unknown-command -fcomment-block-commands=threadsafety` (and `-Wno-documentation-deprecated-sync`), so with `WERROR=1` a comment that does not match its declaration fails. The `docs` job of `build.yml` uploads the reference as an artifact.

The static analyzers (1.9b-2): `bash test/analyze.sh` runs gcc `-fanalyzer` (`make ANALYZER=1`), scan-build, clang-tidy (its checks are in `.clang-tidy`) and cppcheck, writes each finding as `<tool> <file> <check>` (without its line) and fails on one that is not in `test/suppressions.txt`; `Dockerfile.analyze` has the tools (`docker build -f Dockerfile.analyze -t cerver-analyze .` and `docker run --rm -v $PWD:/cerver cerver-analyze`), and the `analyzers` job of `build.yml` runs it. A finding is fixed with its test; only a false positive goes to the suppressions, and a suppression of a file hides a new finding of the same check in that file.

## Tests

There is no test framework; `test/test.h` provides `test_check_*` macros that print the location and `exit(1)` on failure. Each unit test binary has a `main` that calls static `test_*` functions.

```sh
make TYPE=test -j4 && make TYPE=test -j4 test
bash test/run.sh                 # run all unit test binaries
bash test/integration.sh         # the cerver / client pairs, in a container
```

- The unit tests mirror `src/cerver/`: a folder for each module with one binary (`test/bin/connection`, `files`, `handler`, `load`, `network`, `packets`, `system`, `crypto`, …); `test/version.c` stays on top, like `version.h`.
- Integration tests are server/client pairs: servers in `test/cerver/` (TCP, port 7000), clients in `test/client/`; `test/integration.sh` runs each pair with `test/pair.sh` in `Dockerfile.test` (`SANITIZER=address,undefined` or `thread`). The handler pair is `cerver/handler`.
- `test/test.h` scales the short timeouts with `TEST_TIME_SCALE` (`RUNNER` sets it under valgrind, the sanitizer builds use 4); `bash test/repeat.sh <binary | pair> [count]` is the flaky hunter; `test/fuzz/` are the libFuzzer targets of the core (`make TYPE=test CC=clang fuzz`).
- `test/faults/` (1.9b-2) defines `malloc ()`, `calloc ()`, `realloc ()` and `pthread_create ()` to make one call fail (`faults_arm ()` by its function and its size): a check of an allocation gets its test there. It is built only without a sanitizer and `test/run.sh` runs it only without `RUNNER`. gcc can merge a `malloc ()` and its `memset ()` into a `calloc ()`, so a test arms both; the size of the call that fails must not be the one of another allocation of the same path.
- Every bug is fixed with its test, seen failing against the version before it.

## Architecture

Headers live in `include/cerver/` and mirror `src/cerver/` one-to-one. Public functions are marked `CERVER_EXPORT` (`config.h`) and declarations are wrapped in `extern "C"`.

The layout (1.9b-1, the one of `labs` 3.0b-2, whose `roadmap.md` §11 has the table of the old paths): on top only `config.h`, `version.h` and the umbrella `cerver.h`; `core/` (the `Cerver`: `cerver`, `types`, `info`, `data`, `handler`, `start`, `update`, `end`, `report`, `serialization`), `admin/` (`admin`, `cerver`, `stats`, `lifecycle`, `handler`, `poll`), `auth/` (`auth`, `hold` with the connections on hold, `sessions`), `client/` (`client`, `events`, `connect`, `requests`, `handler`), `connection/` (`connection`, `stats`, `recycle`, `data`, `lifecycle`, `end`, `registry`, `update`, `send`), `files/` (`files`, `cerver`, `images`, `send`, `receive`), `handler/` (handler, receive, events, errors), `load/` (the balancer: `balancer`, `stats`, `service`, `events`, `start`, `route`, `end`), `network/` (network, socket, udp), `packets/` (`packets`, `header`, serializer), `system/` (system, timer, input), plus `collections/`, `crypto/` (`random`), `game/` (`types` is the game types), `threads/`, `types/` and `utils/`. The files have one word. The main header of a module has its types and its parts include it; it never includes its parts, so a file includes the parts that it uses. `<cerver/cerver.h>` is the umbrella, it only includes. The functions that find or act on something by a key are named with `_with_`, never `_by_`.

Inside the library (`include/` and `src/`) every include of cerver uses quotes (`"cerver/core/cerver.h"`); the tests, the examples and the benchmarks use `<cerver/...>` and quotes for the internal headers that they read. The includes are at the start of a file, in blocks separated by an empty line and sorted by their path inside each block: the C standard library; the system; the specialized libraries; `cerver/types/`; `cerver/collections/`; the files on top (`config.h`, `version.h`); the core of cerver (with the `internal.h`); a block for each package (`crypto`, `game`, `threads`); `cerver/utils/`; and the local ones of the tests and the examples.

`include/` has only what is installed. What is internal lives next to its `.c` in `src/` (found with `-I src`): the `internal.h` of a folder with what its files share (`admin/`, `auth/`, `client/`, `connection/`, `core/`, `files/`, `load/`, `packets/`). A method that the parts of a split file share is `CERVER_HIDDEN` there, so `libcerver.so` exports the same symbols.

Enums and their string conversions are generated with X-macro `*_MAP(XX)` tables — extend the map rather than the enum directly.

## Conventions

- Comments (1.9b-1): the public headers are documented with Doxygen in its Javadoc syntax and in English, and the `.c` keep `//` for the why. Each header starts with `/** @file cerver/<path>.h @brief … */` before its guard (without `@file` Doxygen does not check its members); each module defines its group in its main header (`@defgroup core Core`), and each region of a header is a subgroup (`@defgroup core_start Start`, `@ingroup core`, `@{ … @}`) instead of its `#pragma region`. A function has `@brief`, `@param[in|out|in,out]` for every parameter, `@return` or `@retval` for every value, and `@note`, `@warning`, `@threadsafety` (an alias), `@since` (only for what is new or renamed) and `@deprecated` where they apply; a `CERVER_PRIVATE` one says «For the internal methods of cerver». Structs, their typedefs and every field have their `/** @brief */`; an enum of an X-macro documents its `*_MAP (XX)` and the enum, the table is not touched. The headers of `src/` are `@internal`. **What a comment says is verified in the code**; what the code does today that can surprise is a `@warning`. The headers of the core are the same as the ones of `labs`: a comment changes in both.
- Code style: tabs, a space before call parentheses (`foo (bar)`), `#pragma region` blocks to group the sections of the `.c`. The X-macro tables keep their existing form, their arguments are stringified.
- No literal values inside functions (named constants), short `if` / `case` bodies (a `static` function for a branch), full words in the names, every `.c` / `.h` that is touched ends with a newline.
- `CHANGELOG.md` holds only the current version's changes, one line per commit message; `incompatibilities.md` has an entry for each version, the newest first.
- Version bumps are a dedicated commit ("Updated version to Beta 1.9b-N") that updates `version.txt`, `include/cerver/version.h`, `ARG CERVER_VERSION` in `Dockerfile` and `cerver-install.sh`.
- `docs/` is a git submodule (`ermiry/cerver-docs`), the wiki of the 1.x, with the paths and the API of the 1.9.

## Branches

- `core` / `beta` is the 1.x line (the 1.9: 1.9b-1 the structure, the documentation and the license; 1.9b-2 the audit; 1.9b-3 the port of `thpool_has_queued ()` of the 2.1b-17, the last beta; Release 1.9 to `master` with the backlog of the core and the ports of the other lines to the files of the core; then 1.9.1 the direct reads of the client that free their connection and the free () count of `test/faults/`, seeded in the 2.0b-78 so the core of `http` is the same; macOS in a later 1.9.x and a 1.9.x for the ports between `core` and `http` before the 2.0; anything else of the 1.x is a 1.9.x, each one a release straight to `master`), `http` / `http-beta` the 2.0 line, `nightly` / `nightly-beta` the 2.1 line and `labs` / `labs-beta` the 3.0 line. The fixes of the core land in `core` first and are seeded by hand in the other lines.
