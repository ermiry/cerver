# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Cerver is a C11 framework for building servers, built as a shared library (`bin/libcerver.so`). It supports four server types (`CERVER_TYPE_CUSTOM`, `GAME`, `WEB`, `FILES`), and a client API for connecting to cervers. It targets Linux with gcc (uses `_GNU_SOURCE`, pthreads, OpenSSL, `-Wl,-rpath`). On macOS, build and test inside the Docker images (see `build.md`) rather than natively.

Dependencies: pthreads, OpenSSL (`-lssl -lcrypto`), libm, and libcurl (only for the web integration test clients).

## Build

The `makefile` is controlled by variables:
- `TYPE=development` (default; enables all `*_DEBUG`/`*_STATS` defines and `-g`), `test`, `beta`, `production`
- `COVERAGE=1` (only with `TYPE=test`), `NATIVE=1` (`-march=native`), `CC=g++` (the code must also compile as C++11 with `-fpermissive`)

```sh
make                    # build bin/libcerver.so
make examples           # build examples/bin/* against bin/libcerver.so
make test               # build all tests: objs, test app lib, units, integration, testhandler
make clean
make install            # copy libcerver.so to /usr/local/lib and headers to /usr/local/include
```

`compile.sh` builds library, examples and tests with both gcc and g++ in production mode. Keep code compiling cleanly under both, with the warning flags in `COMMON` (`-Wpedantic -pedantic-errors` for C).

Run an example: `LD_LIBRARY_PATH=bin/ ./examples/bin/welcome`

## Tests

There is no test framework; `test/test.h` provides `test_check_*` macros that print the location and `exit(1)` on failure. Each unit test binary has a `main` that calls static `test_*` functions.

```sh
make TYPE=test -j4 && make TYPE=test -j4 test
bash test/run.sh                 # run all unit test binaries
./test/bin/http                  # run a single unit test binary (e.g. http, json, jwt, collections, utils, threads)
```

Run test binaries from the repo root — they reference relative paths such as `./test/http/keys/key.key` and `version.txt`.

How tests are linked (see the `units`, `integration-*` and `testhandler` targets in `makefile`):
- Most unit binaries link all objects from a subdirectory, e.g. `test/http/*.o` → `test/bin/http`. Adding a new `.c` file to such a directory automatically includes it; register its test functions from that directory's main file (e.g. `test/http/http.c`). New top-level test binaries need a line in `units` and in `test/run.sh`.
- `test/app/` is built into `test/bin/app/libapp.so`, a shared test application (handlers, routes, users, auth) linked by the HTTP unit tests and the integration servers.
- Integration tests are server/client pairs: servers in `test/cerver/` (TCP, port 7000) and `test/web/` (HTTP, port 8080), clients in `test/client/` and `test/client/web/` (libcurl). `test.sh` and `.github/workflows/test.yml` run each server in the `ermiry/cerver:test` Docker image (`Dockerfile.test`) and then the matching client binary on the host.
- `test/bin/version` checks that `CERVER_VERSION` in `include/cerver/version.h` matches `version.txt` (run by CI on `master`/`*beta`).

Coverage: `bash coverage.sh` (builds with `TYPE=test`, runs units, then `make coverage`).

## Architecture

Headers live in `include/cerver/` and mirror `src/cerver/` one-to-one (`src/cerver/http/route.c` ↔ `include/cerver/http/route.h`). Public functions are marked `CERVER_EXPORT` (`config.h`) and declarations are wrapped in `extern "C"`. Enums and their string conversions are generated with X-macro `*_MAP(XX)` tables — extend the map rather than the enum directly.

Core flow:
- `cerver.c` — `Cerver` lifecycle (create, configure, start, teardown) and its handler type: `CERVER_HANDLER_TYPE_POLL` (single thread with `poll()`) or `THREADS` (thread per connection). A thread pool is also used for handlers.
- `client.c` / `connection.c` — a `Client` owns one or more `Connection`s; the same structures are used server-side for connected clients and in the client API for connecting to a cerver.
- `receive.c` → `packets.c` → `handler.c` — bytes are received per socket, assembled into `Packet`s (typed by `PacketType`), and dispatched to cerver-level handlers (`app_packet_handler`, `app_error_packet_handler`, `custom_packet_handler`). `Handler`s can run in dedicated threads with their own job queue.
- `auth.c`, `sessions.c`, `admin.c` — optional authentication, session ids, and an admin cerver for managing clients.
- `game/` — lobbies, players, game types for the game server template.
- `threads/` — `thpool` (bounded ring buffer queue, joinable threads, `thpool_drain`, overload `wait`/`reject`), `jobs` (job queues), `worker`, `bsem`, thread helpers (every thread created by cerver blocks SIGINT/SIGTERM).
- `collections/` — `dlist`, `avl`, `htab`, `pool`, `queue`, `arena` used throughout instead of external containers.
- Lifecycle: `cerver_stop_request` (EINTR in accept/poll = stop), `cerver_teardown` drains the thpool before releasing anything; web worker threads keep a `HandlerThreadContext` (arena + buffers) and the request data lives only during its handler.
- `receive.c` — `receive_handle_buffer` assembles TCP packets split between `recv ()` calls, shared by the cerver and the client.
- `types/string.c`, `utils/` (log, base64, sha256, misc) — shared helpers.

HTTP layer (`http/`), used when the cerver type is `CERVER_TYPE_WEB`:
- `http.c` — `HttpCerver` attached to a cerver: routes, static paths, uploads config, JWT keys/alg, origins whitelist, default response headers, custom data, stats.
- `route.c` — `HttpRoute` trees with per-method handlers, children routes, auth modes, and custom data; registered via `http_cerver_route_register`.
- `request.c` / `response.c` / `headers.c` / `multipart.c` / `url.c` / `origin.c` / `admin.c` / `auth.c` — request parsing, responses, multipart uploads, CORS origins, HTTP admin routes, JWT auth.
- Vendored third-party code: `http_parser.c` (Joyent/Node http-parser), `http/json/` (Jansson-derived JSON), `http/jwt/` (libjwt-derived JWT over OpenSSL). They follow the cerver call style (space before call parentheses) but keep their upstream structure otherwise.

## Conventions

- Code style: tabs, a space before call parentheses (`foo (bar)`) in every file, vendored code included, `#pragma region` blocks to group sections. Exceptions: a function-like macro name in its `#define NAME(x)` can not have a space, and the X-macro tables keep their existing form (`XX(...)` entries, `*_MAP(XX)` inside functions); X-macro arguments are stringified with `#`, so their text must not be changed.
- No literal values inside functions: sizes, lengths, prefixes, separators and timeouts are named constants (`#define` in the header or a `static const` at the top of the source file), e.g. `HTTP_ERROR_BUFFER_SIZE` instead of `char error_buffer[64]`. Applied to new code now; the rest of the codebase is migrated later.
- Keep `if` / `case` bodies short: when a branch needs its own variables or more than a couple of statements, move it to a dedicated `static` function and call it from the branch. Applied to new code now; the rest of the codebase is migrated later.
- `CHANGELOG.md` holds only the current version's changes, grouped under headings (`## HTTP`, `## Tests`, …), one line per commit message.
- Version bumps are a dedicated commit ("Updated version to Beta 2.0b-N") that updates `version.txt`, `include/cerver/version.h` (version, name, date, time), `ARG CERVER_VERSION` in `Dockerfile`, `cerver-install.sh`, and replaces `CHANGELOG.md` with that version's commit messages. `version.txt` and `CERVER_VERSION` must match or the version CI check fails.
- `docs/` is a git submodule (`ermiry/cerver-docs`).
