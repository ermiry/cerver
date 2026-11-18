# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

## 3.0b-11 (from 3.0b-10)

The first part of L4 of `roadmap.md`: the I/O of the event loops (`CERVER_HANDLER_TYPE_EVENTS`) is new. A loop asks
for operations that complete, over io_uring (with the system calls of cerver, without liburing) or over epoll where
io_uring is not available, and **only the loop uses the socket of a connection**: it reads, it sends and it closes.
The handlers can run in the loop itself. The other handler types (`THREADS`, `POLL`) did not change.

**The event loops**

- **io_uring by default:** the loops of a cerver use io_uring when the kernel has what they need (Linux 6.1) and the
  process is allowed to use it, and epoll otherwise, with a warning that tells why (the default seccomp profile of a
  container blocks io_uring: `EPERM`). **New:** `<cerver/events/events.h>` with `CerverEventsBackend`
  (`CERVER_EVENTS_BACKEND_MAP`: `AUTO`, `URING`, `EPOLL`), `cerver_set_events_backend ()`,
  `cerver_get_events_backend ()`, `cerver_events_backend_to_string ()` and `_description ()`. A cerver that asks for
  `CERVER_EVENTS_BACKEND_URING` does not start where io_uring is not available
- **cerver builds with the header of io_uring of Linux 6.1 or newer** (`<linux/io_uring.h>`): an older one stops
  the build
- **The response of a handler that runs in a pool of threads is sent by its event loop:** `http_response_send ()` and
  the other sends copy what they are given into the output of the connection and return; the loop sends it while the
  handler goes on. They return an error only when a send of the connection already failed (or without memory): a
  send that fails later ends the connection and is counted in its request (`n_failed_responses`), but the handler
  that wrote it already got a success. A handler that writes more than 2^20 bytes that its client has not read waits
  there until the loop sent some, at most the send timeout, and its send fails when the loop gave up meanwhile
- **A response is counted when its event loop sent it whole:** its status (`n_responses_by_status`, the metrics by
  status) and its bytes (`total_bytes_sent`) are counted then, not when its handler wrote it. A response that failed
  is never counted with its status (a thread of a connection counts the status of a response of many pieces when it
  sent its header), and its client left in the response stage only when some of its bytes were sent
  (`n_aborted_requests[HTTP_STAGE_RESPONSE]`), in the one of its handler when none was. `HttpReceive` has
  `response_written` and `response_written_bytes` at its end
- **All the sends of a response of an event loop share its send timeout** (`http_cerver_set_send_timeout_milliseconds ()`):
  the time that its output waits for its client is counted once, whatever the parts that its handler writes
- **`HttpCerver::n_timeout_requests` and `n_limited_requests` are atomic like the other counters:** they were added
  with the mutex of the HTTP cerver and read without it by the stats and the metrics (a data race); code that locked
  `HttpCerver::mutex` to read them reads them with `CERVER_STATS_GET`
- **A request ends in its event loop, when its response was sent:** the request is logged and counted and its data
  is released (the methods that delete the data of a request, the files of its uploads) by the thread of the loop,
  not by the thread of the pool that handled it
- **The files are sent by the loop** with `sendfile ()` without waiting: a file that is not in the memory of the
  system keeps its I/O thread while it is read
- **The connections of an event loop have `TCP_NODELAY`** (from the socket that accepts them): a loop sends each
  response whole
- **The threads of the pools do not read the idle connections anymore** (the F4b of `nightly`, in the 3.0b-10), and
  no thread follows its connection. **Removed:** `http_cerver_set_keep_alive_pool_read ()`,
  `http_cerver_set_keep_alive_follow ()`, `HttpCerver::keep_alive_pool_read`, `keep_alive_follow`,
  `n_pool_read_requests` and `n_followed_requests`, `HTTP_CERVER_DEFAULT_KEEP_ALIVE_POOL_READ`,
  `HTTP_CERVER_DEFAULT_KEEP_ALIVE_FOLLOW`, `HTTP_CERVER_KEEP_ALIVE_FOLLOW_SHORT`, `n_followed` and `n_pool_read` in
  the `connections` of the admin stats, and the metrics `cerver_http_followed_requests_total` and
  `cerver_http_pool_read_requests_total`. `Thpool` keeps its waiter (it is a file of the core), without a use here
- **An idle connection has its own timer:** one whose time ended is closed at most a tick of the timers (2^8 ms)
  after it, the loop does not check its idle connections every 2^8 ms anymore

**The handlers in the event loop**

- **New:** the pool `HTTP_POOL_LOOP_NAME` (`"loop"`), that every HTTP cerver has and that has no threads: the event
  loop that read a request of that pool handles it and sends its response in the same thread. A group, an endpoint or
  a route chooses it like any pool (`http_group_set_pool ()`, `http_endpoint_set_pool ()`, `http_route_set_pool ()`),
  and `http_cerver_set_handlers_pool ()` sets the pool of every request that did not choose one. **A handler of that
  pool must never wait:** while it runs, its I/O thread handles no other connection. `"loop"` can not be the name of a
  pool that is created anymore
- **The requests of a pipeline that the loop handles leave in one send:** each one ends when its response is
  written, up to 2^5 in a row, and a send that fails then is counted once

**The I/O threads**

- **New:** `CERVER_IO_THREADS_PER_CORE` for `cerver_set_io_threads ()` (a loop for each processor that the process
  can use) and `cerver_set_io_threads_affinity ()` (each loop in its processor)
- `Cerver` has `events_backend` and `io_threads_affinity` at its end, and `HttpCerver` has `loop_pool` and
  `handlers_pool` after `pools_started`: a binary that allocates them itself is built again

## 3.0b-10 (from 3.0b-9)

The sync of `labs` before L4 (L0.5 of `roadmap.md`): the whole form of keep-alive of `nightly` (its 2.1b-19, F4b, over
the parser of the 3.0b-9), its 2.1b-20 and with it the Release 1.9 of `core` and the 2.0b-77 of `http`, and the policy
of the versions and the methods that HTTP Garden showed. Some structs grow or change their fields (a binary that
allocates them itself is built again), and some functions change what they do.

**HTTP**

- **Every HTTP/1.x is served:** a minor above 1 is read as 1 (RFC 9112 section 2.3), so `HTTP/1.2` gets the response
  of an `HTTP/1.1` request (it was 400); `HTTP/0.9` and `HTTP/2.0` are still 505, another major still 400
- **A method that cerver does not implement is 501** (RFC 9110 section 9.1): a token that is not one of
  `REQUEST_METHOD_MAP` (the methods of RTSP, a method in lower case) is the new error
  `CERVER_HTTP_PARSER_ERROR_METHOD_UNKNOWN` (19) of the parser, that was `CERVER_HTTP_PARSER_ERROR_METHOD` and 400; what
  is not a token or is longer than the longest method is still `METHOD` and 400
- **A response without its length says `Connection: close`:** its body ends with the close of the connection, that
  was already closed after it; now the response announces it
- **`http_cerver_static_path_add ()` returns NULL** when its path, its characters or its place in the list can not
  be allocated (it returned a static path with a NULL path, or one that was not in the list)
- **The query params and the values of a form live in the arena of the request:** `HttpRequest::query_params` and
  `body_values` are NULL until `http_request_get_query_params ()` and `http_request_get_body_values ()` create them (a
  copy of each pair), so code that read the fields reads the getters; `http_request_query_params_get_value ()` and
  `http_request_body_get_value ()` read the pairs of the arena without allocating (`HttpRequest::query_pairs`,
  `n_query_pairs`, `body_pairs` and `n_body_pairs` at its end)
- `HttpCerver` has `keep_alive_pool_read` and `n_pool_read_requests` at its end; `n_pool_read` in the `connections`
  and `parked_microseconds` in the pools of the admin stats, the metrics `cerver_http_pool_read_requests_total` and
  `cerver_http_pool_parked_seconds{pool}`

**Keep-alive and the event loops**

- **The threads of the pools read the next request of an idle connection:** with `CERVER_HANDLER_TYPE_EVENTS` and
  connections that are kept (the default), the threads of each pool wait for the sockets of the idle connections whose
  last request was of the pool when the queue of the pool is empty, and the one that gets a socket reads, parses and
  handles its request without the event loop. The loop reads it when no thread of its pool waits, when requests of
  that pool wait in the loop or when the loop rejects them. `http_cerver_set_keep_alive_pool_read (false)` gives back
  the reads of the 3.0b-9
- **The authentication before the body of the next request of a kept connection runs in the thread that reads it**
  (or that follows its connection), when the request is of the pool of that thread
- **A thpool can wait on something else than its queue:** `Thpool` has `waiter`, `num_threads_waiting` and
  `waiter_wakes` at its end (`ThpoolWaiter`, `ThpoolWaited`, `thpool_set_waiter ()`,
  `thpool_get_num_threads_waiting ()`, `CERVER_PRIVATE`)
- **The memory of a connection of an event loop is released with its loop:** one that its recycled connections do not
  keep loses its arena and its head buffer but keeps its struct
- **The pause of the accept is 2^4 ms** (`EVENT_LOOP_ACCEPT_BACKOFF`); `EventLoop` has `accept_resume` instead of
  `accept_timer`, and `EventSource` has `poller`

**The core (the Release 1.9)**

- **A send that times out ends its connection:** a send to a peer that does not read in all its send timeout (or the
  deadline of an HTTP response) shuts down its socket, so the next sends fail at once (EPIPE) instead of waiting each
  one for its own timeout. **New:** `socket_end_on_send_timeout ()` (`CERVER_PRIVATE`)
- **The events and errors of a client share their arguments** like the ones of the cerver: `ClientEvent` and
  `ClientEventData` have `shared_args` and `shared_response` at their end, `ClientError` and `ClientErrorData` have
  `shared_args`; an event or an error that is replaced or unregistered while a trigger still runs keeps its arguments
  (and its response) until that trigger releases its data
- **`files_create_dir ()` of a directory that exists returns 0** (it returned 1); `file_n_read ()` sets `n_read` (it
  added to it) and takes a NULL one; `file_receive_actual ()` removes a file that fails after it was opened; a BMP is
  found by its bytes

**The tests**

- The macros `test_check_ptr_eq`, `_ptr_ne`, `_unsigned_ne`, `_unsigned_gt`, `_int_ne` and `_int_gt` of `test/test.h`
  put their arguments in parentheses
- The send timeout of `test/http/disconnect.c` scales with `test_time_scale ()`, and the routing test of the balancer
  waits for the counter of the packets that it sent
- The oracle of the comparisons with llhttp (`test/conformance/outcome.h`) gives llhttp any version and applies the
  policy of cerver in its callbacks

## 3.0b-9 (from 3.0b-8)

The parsers of cerver (L3 of `roadmap.md`): `cerver/http/parser/` reads HTTP/1.1 and the multipart bodies with code
of its own, in place of llhttp 9.4.3 and of the copy of multipart-parser-c, that leave the library. A request gets the
same response as before, except the two cases below; the structs of the receive and the API of the parser change.

**The structs and the API**

- **New:** `cerver/http/parser/{parser,head,body,multipart}.h`, in the umbrella `cerver/http/http.h`: the types
  `CerverHttpParser`, `CerverHttpParserLimits`, `CerverHttpHead`, `CerverHttpField`, `CerverHttpSpan`,
  `CerverHttpMultipart` and `CerverHttpMultipartCallbacks`, the errors of `CERVER_HTTP_PARSER_ERROR_MAP`, the results
  `CERVER_HTTP_PARSER_RESULT_*` and the methods `cerver_http_parser_*`, `cerver_http_head_*` and
  `cerver_http_multipart_*`
- **Removed:** `cerver/http/parser/{api,helpers,llhttp}.h` with every `llhttp_*` and the types of llhttp, and the
  group `http_multipart_parser` of `cerver/http/requests/multipart.h` (`multipart_parser`,
  `multipart_parser_settings`, `multipart_parser_init ()`, `multipart_parser_execute ()`, `multipart_parser_free ()`)
- **`HttpReceive`:** `parser` is a `CerverHttpParser` (it was an `llhttp_t`), without `settings`, `protocol_length`,
  `protocol_other`, `url_buffer`, `field_buffer`, `value_buffer`, `header_size` and `n_headers`; new `fields`,
  `fields_capacity` and `fields_inline` (the array of the fields of its requests: 32 of its own, one that doubles for
  a request with more), `head_buffer` (the head that does not arrive whole) and `body_handler`; `mpart_parser` is a
  `CerverHttpMultipart *` that points to the new `multipart` (it was allocated for each request), without
  `mpart_settings`
- **`HttpReceiveResources`** has `arena` and `head_buffer` (it had `url_buffer`, `field_buffer` and `value_buffer`),
  and the context of a handler thread lends one `head_buffer` instead of those three
- **`HttpRequest::strings_in_arena`:** the url, the query and the headers of a request that the receive read are views
  of the copy of its head in the arena of the request (one copy instead of one `String` for each piece that llhttp
  gave)
- **`HttpParserLenient`** and `HTTP_PARSER_LENIENT_*` keep their names and their values, on a table of their own
  (`HTTP_PARSER_LENIENT_MAP` in `cerver/http/parser/parser.h`, not on the one of llhttp);
  `http_parser_lenient_string ()` and `http_parser_lenient_description ()` live there too.
  `HTTP_RECEIVE_PARSER_STOP` is not public any more (it was the `HPE_USER` of llhttp)

**What the parser does that llhttp did not** (the only responses that change)

- **A request line without a version** (the form of HTTP/0.9) is answered with 505: llhttp took it without calling
  the callback where cerver checked the version, so it reached the handlers
- **The name of a field never has the spaces** that `HTTP_PARSER_LENIENT_HEADERS` takes before its colon, so
  `Content-Type :` is the known header Content-Type

**Tests and scripts:** `make REFERENCES=<folder>` builds the conformance of HTTP (`make conformance`,
`test/bin/conformance/http`) and the differential fuzzers `llhttp` and `parts` with the upstream llhttp 9.4.3 and
multipart-parser-c `772639cf`, that `Dockerfile.conformance` and `Dockerfile.fuzz` download; without it they are not
built (the `differential` of the 3.0b-8, against the libjwt of the system, is another target). `bash
test/conformance/http.sh` runs the corpus of llhttp (`test/conformance/http.txt` has the verdicts).
`CERVER_HTTP_PARSER_SCALAR` builds the parser without its kernels of NEON, SSE2 and SSSE3. `benchmarks/http-parser.c`
is gone: `benchmarks/competitors/` measures the parser against llhttp, picohttpparser and http_parser 2.9.4 (the new
cases `cookies`, `chunked` and `pipeline`), and the new module `multipart` against multipart-parser-c.
`bash test/conformance/garden.sh` runs the HTTP Garden with cerver as one of its origins (`Dockerfile.garden`,
`test/web/garden.c`).

## 3.0b-8 (from 3.0b-7)

The JWT of cerver (L2 of `roadmap.md`): `cerver/jwt/` signs and verifies the tokens (RFC 7515, 7518 and 7519) over
the JSON of cerver, with the keys and the signatures of `cerver/crypto/`, that only the provider of OpenSSL
computes. The JWT that came from libjwt and the copy of Jansson are gone, with their headers, their 56 `jwt_*`
methods and their rows of `thirdparty.md`. A program that used the `jwt_*` of cerver moves to the `cerver_jwt_*`
(the table below); there is no layer of compatibility. **The tokens are the same bytes:** a token that the 3.0b-7
or the 2.x signed is verified, and the same values with the same key give the same token (HS256, HS384, HS512,
RS256, RS384 and RS512; an ES token has a random part).

**The JWT of libjwt left the API**

- **`cerver/http/jwt/{jwt,alg,key}.h` do not exist**, and `libcerver.so` does not export the 56 `jwt_*` methods
  (`jwt_new ()`, `jwt_decode ()`, `jwt_encode_str ()`, `jwt_add_grant ()`, `jwt_get_grant ()`, `jwt_validate ()`,
  `jwt_key_create ()`, …): a binary that calls one does not link. The names are free again for the libjwt of the
  system. `cerver/jwt/{jwt,key,sign,verify,claims}.h` are in `cerver/cerver.h`
- **`jwt_alg_t` is `CerverJwtAlgorithm`** (`cerver/jwt/jwt.h`): `JWT_ALG_HS256` … `JWT_ALG_ES512` are
  `CERVER_JWT_ALGORITHM_HS256` … `CERVER_JWT_ALGORITHM_ES512` with the same numbers (1 to 9), and
  `CERVER_JWT_ALGORITHM_NONE` is 0. **10 was `JWT_ALG_TERM` (`JWT_ALG_INVAL`) and is `CERVER_JWT_ALGORITHM_PS256`**:
  11 and 12 are PS384 and PS512, 13 is `CERVER_JWT_ALGORITHM_EDDSA`, and what is not an algorithm is
  `CERVER_JWT_ALGORITHM_NONE`. `JWT_DEFAULT_ALG` is `HTTP_CERVER_DEFAULT_JWT_ALGORITHM`
- **`http_cerver_auth_set_jwt_algorithm ()`** takes a `CerverJwtAlgorithm`; `HttpCerver::jwt_alg` has that type,
  `HttpCerver::jwt_private_key` and `jwt_public_key` are `CerverJwtKey *`, and the struct has
  `HttpCerver::jwt_subject`. **`HttpJwt` lost its `jwt` field** (the `struct jwt *` of libjwt) and **`HttpRequest`
  has `claims`**: the three structs changed their layout, so what uses them is built again
- **The token of `http_cerver_auth_generate_jwt ()` is released with `free ()`**, it was released with
  `jwt_free_str ()`. `http_cerver_auth_generate_jwt_actual ()` and `http_cerver_auth_generate_bearer_jwt_actual ()`
  (`CERVER_PRIVATE`) take a `const CerverJwtKey *`

| The JWT of libjwt | The JWT of cerver |
|---|---|
| `jwt_t`, `jwt_new ()`, `jwt_free ()`, `jwt_dup ()` | None: a token is a text; its claims are a `const CerverJsonValue *` that `cerver_jwt_verify ()` reads in the arena of the caller |
| `jwt_alg_t`, `jwt_alg_str ()`, `jwt_str_alg ()`, `jwt_get_alg ()`, `jwt_set_alg ()` | `CerverJwtAlgorithm`, `cerver_jwt_algorithm_to_string ()`, `cerver_jwt_algorithm_from_string ()`, `cerver_jwt_key_algorithm ()`: the algorithm is the one of the key, a token never sets it |
| `JwtKey`, `jwt_key_create ()`, `jwt_key_check_strength ()`, `jwt_key_delete ()` | `CerverJwtKey`: `cerver_jwt_key_create_secret ()`, `cerver_jwt_key_create_pem ()`, `cerver_jwt_key_create_jwk ()` or `cerver_jwt_key_create ()` with a `CerverCryptoKey`; they check the strength; `cerver_jwt_key_delete ()` |
| `JwtKeyError`, `jwt_key_error_to_string ()`, `jwt_key_type_to_string ()` | `CerverJwtError` and `cerver_jwt_error_to_string ()`; `cerver_jwt_key_is_private ()`, and `cerver_crypto_key_type ()` of `cerver_jwt_key_crypto ()` |
| `jwt_key_sign ()`, `jwt_key_verify ()` | `cerver_crypto_sign ()`, `cerver_crypto_verify ()` (`cerver/crypto/signature.h`) with a `CerverCryptoKey` |
| `jwt_add_grant ()`, `jwt_add_grant_int ()`, `jwt_add_grant_bool ()`, `jwt_add_grants_json ()`, `jwt_del_grants ()` | The claims are the text of a JSON object, written with a `CerverJsonWriter` |
| `jwt_encode_str ()`, `jwt_encode_with_key ()`, `jwt_encode_fp ()`, `jwt_free_str ()` | `cerver_jwt_sign ()` into a buffer of the caller of `cerver_jwt_sign_size ()` bytes; nothing to release |
| `jwt_decode ()`, `jwt_decode_with_key ()` | `cerver_jwt_verify ()`: the size, the three parts, the header, the signature and then the claims with their validation, in one call |
| `jwt_get_grant ()`, `jwt_get_grant_int ()`, `jwt_get_grant_bool ()`, `jwt_get_grants_json ()` | `cerver_json_object_get ()` and the getters of `cerver/json/value.h` on the claims; `cerver_json_write ()` for their text; `http_request_claims ()` for the ones of the bearer of a request |
| `jwt_add_header ()`, `jwt_add_header_int ()`, `jwt_add_header_bool ()`, `jwt_add_headers_json ()`, `jwt_del_headers ()`, `jwt_get_header ()`, `jwt_get_header_int ()`, `jwt_get_header_bool ()`, `jwt_get_headers_json ()` | None: the header that cerver writes is `{"alg":"<algorithm>","typ":"JWT"}`, and the header of a token that it verifies is not given |
| `jwt_valid_t`, `jwt_valid_new ()`, `jwt_valid_free ()`, `jwt_valid_set_now ()`, `jwt_valid_set_headers ()`, `jwt_validate ()`, `jwt_valid_get_status ()` | A `CerverJwtValidation` for each call (`now`, `leeway`, `require_expiration`, `issuer`, `subject`, `audience`, `max_size`), for `cerver_jwt_verify ()` or `cerver_jwt_claims_validate ()`; the claims that a header repeats are always checked |
| `jwt_valid_add_grant ()`, `jwt_valid_add_grant_int ()`, `jwt_valid_add_grant_bool ()`, `jwt_valid_add_grants_json ()`, `jwt_valid_del_grants ()`, `jwt_valid_get_grant ()`, `jwt_valid_get_grant_int ()`, `jwt_valid_get_grant_bool ()`, `jwt_valid_get_grants_json ()` | `CerverJwtValidation::issuer`, `subject` and `audience`; any other claim is compared by the program with its getter |
| `jwt_error_t` (`JWT_VALIDATION_*`), `jwt_error_str ()` | `CerverJwtError` (`CERVER_JWT_ERROR_*`: one value, not a mask), `cerver_jwt_error_to_string ()` |
| `jwt_dump_fp ()`, `jwt_dump_str ()` | None |

**The tokens that a cerver takes**

- **`iat` is checked:** a token that was issued in the future (after the clock plus the leeway of
  `http_cerver_auth_set_jwt_leeway_seconds ()`) is not valid; `iat` was never read. Like `exp` and `nbf`, an `iat`
  that is not an integer is not valid. **The leeway is 0 by default:** a cerver that verifies the tokens that
  another host just signed refuses them while the clock of that host is ahead of its own, so the cervers that
  share tokens set a leeway of the seconds that their clocks can differ
- **`http_cerver_auth_set_jwt_subject ()` is new:** with it, the `sub` of a token must be that string. Without it
  nothing changes
- **A size of 0 in `http_cerver_auth_set_jwt_max_size ()` is `CERVER_JWT_SIZE_MAX` (2^20)**, the limit of every
  token; it was no limit. The default is still 2^13
- **The header of a token that cerver did not write is read with limits:** at most `CERVER_JWT_HEADER_SIZE_MAX`
  (2^10) characters and `CERVER_JWT_HEADER_DEPTH` (2^2) levels, without a key more than once and without a null
  character; with a `crit` member it is refused (no extension is understood); and an `iss`, a `sub` or an `aud`
  that it repeats must be equal to the one of the claims, whatever their type is (an `iss` or a `sub` that was
  not a string was not compared)
- **An integer between 2^63 and 2^64 - 1 in the claims or in the header is read** (the JSON of cerver has it as an
  unsigned): the copy of Jansson refused the whole token. As `exp`, `nbf` or `iat` it is not a time, and the token
  is not valid
- **The algorithms PS256, PS384, PS512 (RSA of at least 2048 bits) and EdDSA (Ed25519 or Ed448) are new.** The rules
  of the keys are the ones of before: a secret with at least the bytes of its digest that is not the text of a PEM,
  RSA of at least 2048 bits, the curve of its ES algorithm, a PEM with a passphrase is never read. A key of RSA of
  more than `CERVER_CRYPTO_RSA_BITS_MAX` (2^14) bits is refused
- **The file of a key that starts with `{` is a JWK** (RFC 7517; the spaces before it do not count), also for an
  HS algorithm: **a secret whose first character is `{` was the secret of its bytes and is now read as a JWK**, and
  the cerver does not start if it is not one. A JWK is an `oct`, an `RSA`, an `EC` (P-256, P-384, P-521) or an
  `OKP` (Ed25519, Ed448) key; one with a `use` that is not `sig`, or with `key_ops` without `sign` and without
  `verify`, is refused
- With `HTTP_AUTH_DEBUG`, a token that is not valid logs its `cerver_jwt_error_to_string ()`

**`cerver/crypto` has keys and signatures** (`cerver/crypto/key.h`, `cerver/crypto/signature.h`, both in
`cerver/cerver.h`): `CerverCryptoKey` (a secret, a PEM or a JWK), `cerver_crypto_sign ()` and
`cerver_crypto_verify ()` with the 14 algorithms of `CERVER_CRYPTO_SIGNATURE_MAP`, and `cerver_crypto_equal ()` in
`cerver/crypto/memory.h`. `src/cerver/crypto/openssl.c` is the only file of cerver that includes OpenSSL.
`libcerver.so` exports 1815 symbols, it exported 1837: the 56 `jwt_*` are gone and 34 are new (17
`cerver_crypto_*`, 15 `cerver_jwt_*`, `http_cerver_auth_set_jwt_subject ()` and `http_request_claims ()`).

**Tests and scripts:** `test/bin/jansson`, `test/jansson/`, the keys of `test/jwt/keys/` (the ones of the tests are
in `test/crypto/keys/`) and the fuzz targets `jansson` and `compare` are gone. `test/bin/crypto` and `test/bin/jwt`
have the examples of RFC 4231, 7515, 7517, 7520, 8032 and 8037; the fuzz targets `jwk` and `differential` are new,
and `differential` (`make TYPE=test CC=clang differential`, `test/fuzz/differential/jwt.c`) needs the libjwt of the
system, that `Dockerfile.fuzz` installs. `make conformance` also builds `test/bin/conformance/jwt`, that
`test/conformance/jwt.sh` runs with the vectors of Wycheproof of `Dockerfile.conformance`.
`benchmarks/competitors/` measures the JWT against libjwt, l8w8jwt and jwt-cpp, that `Dockerfile.competitors`
builds from their sources, and its backend `legacy` of the JSON is gone.

## 3.0b-7 (from 3.0b-6)

HTTP over the JSON of cerver (the second beta of L1 of `roadmap.md`): the stats of the admin routes, the routes
json, the json bodies of the responses and the claims of a token are written and read with `cerver/json`, and the
copy of Jansson is not part of the API anymore. A program that used the `json_*` of cerver moves to the
`cerver_json_*` (the table below); there is no layer of compatibility. The copy of Jansson stays inside the library,
without its headers and without its symbols, only for the JWT, until the JWT of cerver (L2) removes both.

**The JSON of Jansson left the API**

- **`cerver/http/json/*.h` are not installed** (`json.h`, `value.h`, `config.h`, `types.h`, `internal.h`,
  `private.h`, `hashtable.h`, `utf.h`: they are in `src/cerver/http/json/`), and the umbrella `cerver/http/http.h`
  does not include `cerver/http/json/json.h`. `cerver/json/json.h` and its parts are in `cerver/cerver.h`
- **`libcerver.so` does not export the 87 `json_*` methods** (`json_loads ()`, `json_dumps ()`, `json_object_get ()`,
  `json_pack ()`, `json_unpack ()`, `json_decref ()`, …): a binary that calls one does not link. The names are free
  again for the Jansson of the system
- **The grants and the headers of a `jwt_t`** (`jwt_t::grants`, `jwt_t::headers`, `jwt_valid_t::required_grants`)
  are still a `struct json_t *` of the copy, that a program can not read anymore: `jwt_get_grant ()`,
  `jwt_get_grant_int ()`, `jwt_get_grant_bool ()` and `jwt_get_grants_json ()` give them

| Jansson | The JSON of cerver |
|---|---|
| `json_t *` | `const CerverJsonValue *`, a value of a `CerverJsonDocument` |
| `json_error_t` | `CerverJsonError` (its code and the byte of the error), `cerver_json_error_to_string ()`, `cerver_json_error_position ()` |
| `json_loads ()`, `json_loadb ()` | `cerver_json_parse ()` and `cerver_json_document_root ()`; `http_request_json ()` for the body of a request |
| `json_loadf ()`, `json_loadfd ()`, `json_load_file ()`, `json_load_callback ()` | None: the text is read first; `cerver_json_read ()` gives a text as events |
| `json_decref ()`, `json_incref ()` | None: a value lives as long as its document, `cerver_json_document_delete ()` or the arena of the document |
| `json_typeof ()`, `json_is_object ()`, `json_is_array ()`, `json_is_string ()`, `json_is_boolean ()`, `json_is_null ()`, `json_is_number ()` | `cerver_json_type ()`, `cerver_json_is_object ()`, `cerver_json_is_array ()`, `cerver_json_is_string ()`, `cerver_json_is_boolean ()`, `cerver_json_is_null ()`, `cerver_json_is_number ()` |
| `json_is_integer ()`, `json_is_real ()` | `cerver_json_type ()` is `CERVER_JSON_TYPE_INTEGER` (or `CERVER_JSON_TYPE_UNSIGNED`, over 2^63 - 1), `CERVER_JSON_TYPE_REAL` |
| `json_is_true ()`, `json_is_false ()` | `cerver_json_is_boolean ()` with `cerver_json_boolean_value ()` |
| `json_object_get ()` | `cerver_json_object_get ()`, `cerver_json_object_find ()` with the length of the key, `cerver_json_pointer_get ()` |
| `json_object_size ()` | `cerver_json_object_size ()` |
| `json_object_foreach ()`, `json_object_iter ()` and the rest of the iterators | `cerver_json_object_iterator ()` and `cerver_json_object_iterator_next ()` |
| `json_array_size ()`, `json_array_get ()`, `json_array_foreach ()` | `cerver_json_array_size ()`, `cerver_json_array_get ()`, `cerver_json_array_iterator ()` and `cerver_json_array_iterator_next ()` |
| `json_string_value ()`, `json_string_length ()` | `cerver_json_string_value ()`, `cerver_json_string_length ()` |
| `json_integer_value ()` | `cerver_json_integer_value ()`, `cerver_json_unsigned_value ()` |
| `json_real_value ()`, `json_number_value ()` | `cerver_json_real_value ()`, `cerver_json_number_value ()` |
| `json_boolean_value ()` | `cerver_json_boolean_value ()` |
| `json_equal ()` | `cerver_json_equal ()` |
| `json_unpack ()`, `json_unpack_ex ()`, `json_vunpack_ex ()` | None: each value with its getter, that takes a NULL value and one of another type |
| `json_object ()`, `json_array ()`, `json_string ()`, `json_integer ()`, `json_real ()`, `json_true ()`, `json_false ()`, `json_boolean ()`, `json_null ()`, `json_pack ()`, `json_object_set_new ()`, `json_array_append_new ()` | A `CerverJsonWriter`: `cerver_json_writer_object_begin ()`, `_key ()`, `_string ()`, `_integer ()`, `_unsigned ()`, `_real ()`, `_boolean ()`, `_null ()`, `_array_begin ()`, `_array_end ()`, `_object_end ()` and `cerver_json_writer_end ()` |
| `json_dumps ()`, `json_dumpb ()`, `json_dumpf ()`, `json_dump_callback ()` | `cerver_json_writer_init ()` or `cerver_json_writer_init_arena ()`, `cerver_json_writer_text ()` and `cerver_json_writer_length ()`; `cerver_json_write ()` and `cerver_json_writer_value ()` for a value of a document |
| `JSON_COMPACT`, `JSON_INDENT ()`, `JSON_ESCAPE_SLASH`, `JSON_ENSURE_ASCII` | `CERVER_JSON_WRITE_COMPACT`, `cerver_json_writer_set_indent ()`, `CERVER_JSON_WRITE_ESCAPE_SLASH`, `CERVER_JSON_WRITE_ENSURE_ASCII` |
| `JSON_SORT_KEYS`, `JSON_PRESERVE_ORDER`, `JSON_REAL_PRECISION ()` | None: the members are written in the order of the calls, and a real with the least digits that read it back |
| `JSON_REJECT_DUPLICATES`, `JSON_ALLOW_NUL`, `JSON_DECODE_ANY` | `CERVER_JSON_PARSE_REJECT_DUPLICATES`; the default (`CERVER_JSON_PARSE_REJECT_NUL` rejects it); the default |
| `JSON_DISABLE_EOF_CHECK`, `JSON_DECODE_INT_AS_REAL` | None (`CERVER_JSON_PARSE_BIG_AS_REAL` only for an integer that does not fit in 64 bits) |
| `json_object_seed ()`, `json_set_parser_memory_factor ()` | None: nothing is a state of the process; the `CerverJsonLimits` and the arena of each call |
| `json_object_set ()`, `json_object_del ()`, `json_object_update ()`, `json_object_clear ()`, `json_array_append ()`, `json_array_insert ()`, `json_array_remove ()`, `json_copy ()`, `json_deep_copy ()`, `json_string_set ()`, `json_integer_set ()`, … | None: a document is not changed; another text is written with a writer |

**`decode_data` takes the claims of a token as a value of the JSON of cerver**

- **`HttpDecodeData`** (`cerver/http/requests/request.h`) is `void *(*) (const CerverJsonValue *claims)`: the method
  that a bearer authentication calls with the claims of its token got them as a `json_t *` behind a `void *`. It is
  the type of `HttpRoute::decode_data`, `HttpRouteSettings::decode_data`, `HttpStaticPath::decode_data` and
  `HttpCerver::admin_decode_data` (the structs keep their size), and of the argument of `http_route_set_decode_data ()`,
  `http_endpoint_set_decode_data ()`, `http_group_set_decode_data ()`, `http_static_path_set_decode_data ()`,
  `http_cerver_admin_routes_auth_set_decode_data ()` and `http_cerver_auth_validate_jwt ()`. A method with the old
  type is an incompatible pointer for them (an error since gcc 14 and in C++), and one that is forced with a cast
  reads a value of cerver as a `json_t`
- **The claims are valid only during the call** (they are read in the arena of the request, or in one of the call
  of `http_cerver_auth_validate_jwt ()`): a `decode_data` copies what it keeps, as before. A method that used
  `json_unpack (claims, "{s:i, s:s}", "iat", &iat, "id", &id)` reads each claim with `cerver_json_object_get ()`
  and its getter
- **`http_route_set_decode_data_into_json ()`, `http_endpoint_set_decode_data_into_json ()`,
  `http_group_set_decode_data_into_json ()` and `http_cerver_admin_routes_auth_decode_to_json ()`** keep their names
  and what they give: the claims as a json string, released with `free ()`, with the same bytes (the members in the
  order of the token, `", "` and `": "`) but for a claim that is a real (below)
- **The claims of a bearer have at most `HTTP_JWT_CLAIMS_DEPTH` (2^5) levels** of nested arrays and objects: a token
  with more is not valid (the limit was the 2^11 of the parser of Jansson)
- **`http_cerver_auth_jwt_verify ()`** (`CERVER_PRIVATE`) takes the arena where the claims are read and returns
  them, it returned the `jwt_t`; **`http_decode_data_into_json ()`** (`CERVER_PRIVATE`) takes a
  `const CerverJsonValue *`

**What is written with other bytes**

- **A real has the least digits that read it back:** `{"value": 18.123}` where `http_response_json_real_value ()`
  wrote `{"value": 18.123000000000001}`; the same in the reals of the stats of the admin routes
  (`min_process_time`, `max_process_time`, `mean_process_time`, `mean_n_files`, `mean_file_size`, and the `total`,
  `available`, `used` and `used_percentage` of a file system) and in a real claim of
  `http_decode_data_into_json ()`. A real that has few digits (`1.5`, `0.25`, `1e100`) is written as it was
- **Everything else has the same bytes:** the stats, the info, the file systems and the workers of the admin routes,
  the routes json of `http_cerver_routes_generate_json ()`, the bodies of `http_response_json_*` and the json of
  `http_cerver_auth_generate_bearer_jwt_json ()`. A member whose string is not UTF-8 or whose real is not a number
  is still left out of the stats, and a body or a token json with a value that is not UTF-8 is still an error

**Tests and scripts:** `make install` and the `Dockerfile` do not install `cerver/http/json/`. `test/bin/jansson`,
`test/bin/json` (the bytes of the writer against the ones of Jansson), the fuzzers `jansson`, `compare` and `jwt`,
and the backend `legacy` of `benchmarks/competitors/` use the copy of Jansson from its objects and its headers in
`src/`. `benchmarks/server` has `GET /bench/auth`, a route with a bearer, when `BENCH_AUTH` names the file of its
HS256 secret, and `benchmarks/load` sends the value of `LOAD_AUTHORIZATION` in its `Authorization` header;
`test/performance.sh` counts the allocations of a request with a bearer (`AUTH_MAX_ALLOCATIONS`, 39 with the JWT of
this beta).

## 3.0b-6 (from 3.0b-5)

The JSON of cerver (the first beta of L1 of `roadmap.md`): `cerver/json`, a new module written from scratch, next to
the copy of Jansson of `cerver/http/json`, that nothing loses yet. The beta adds and breaks little: one struct is
bigger and some files of the tests have other names. The JSON of Jansson leaves the API in the 3.0b-7.

**The structs and the API**

- **`HttpRequest` is bigger:** it has `json`, `json_error` and `json_read` before `keep_files`, so `keep_files`,
  `id`, `client_ip`, `arena` and `own_arena` moved; a binary built with the 3.0b-5 that reads them is built again.
  `cerver/http/requests/request.h` includes `cerver/json/json.h`
- **New:** `cerver/json/{json,document,value,pointer,events,writer}.h`, in the umbrella `cerver/cerver.h`: the types
  `CerverJsonDocument`, `CerverJsonValue`, `CerverJsonError`, `CerverJsonLimits`, `CerverJsonEvents` and
  `CerverJsonWriter`, and the methods `cerver_json_*`; `http_request_json ()`. The names do not collide with the
  ones of Jansson (`json_*`, `JSON_*`), so a file can include both
- **`libcerver.so` exports three methods for the tests of the JSON,** `cerver_json_scan_string_stop ()`,
  `cerver_json_number_power ()` and `cerver_json_object_table_probes_set ()` (`CERVER_PRIVATE`, declared in
  `src/cerver/json/internal.h`): they are not part of the API

**What the JSON of cerver does that the one of Jansson does not** (for the code that moves to it, the 3.0b-7 moves
the one of cerver)

- **A text is any value,** not only an array or an object (`JSON_DECODE_ANY` is the default)
- **An object keeps every member,** also the ones with a key that is more than once: a search answers the last one,
  as Jansson does, and `cerver_json_object_size ()` and the iterators count all of them.
  `CERVER_JSON_PARSE_REJECT_DUPLICATES` is the `JSON_REJECT_DUPLICATES` of Jansson
- **A string and a key can have an escaped NUL** (the `JSON_ALLOW_NUL` of Jansson is the default, and it also takes
  it in a key): a string has its length. `CERVER_JSON_PARSE_REJECT_NUL` rejects it
- **An integer from 2^63 to 2^64 - 1 is a value,** `CERVER_JSON_TYPE_UNSIGNED`; one that does not fit in 64 bits is
  an error, or a real with `CERVER_JSON_PARSE_BIG_AS_REAL` (the `JSON_DECODE_INT_AS_REAL` of Jansson makes every
  integer a real: it has no equivalent)
- **A NUL byte outside a string is always an error:** `json_loadb ()` skips the one that follows a number or a
  literal (`[1\0, 2]` is `[1, 2]` for it)
- **A real is written with the least digits that read it back:** `0.1` and `1.5e-7`, where Jansson writes
  `0.10000000000000001` and `1.4999999999999999e-7`; everything else is written with the same bytes, with the same
  flags (`CERVER_JSON_WRITE_COMPACT`, `_ESCAPE_SLASH`, `_ENSURE_ASCII`, and `cerver_json_writer_set_indent ()` for
  `JSON_INDENT ()`). The keys are written in the order of their text; there is no `JSON_SORT_KEYS`
- **A document is not changed and has no references:** it lives in its arena, and there is no `json_pack ()`,
  `json_unpack ()`, `json_object_set ()` nor `json_decref ()`; a text is written value by value with a
  `CerverJsonWriter`
- **The limits are arguments of each call** (`CerverJsonLimits`: levels, bytes and values), not a state of the
  process like `json_set_parser_memory_factor ()`. No limit takes a text over `CERVER_JSON_SIZE_MAX` (`1 << 30`
  bytes): the values of a document know each other by distances of 32 bits
- **The hash of the keys is only of the objects with more than 32 members,** with a seed that the process takes by
  itself the first time (the random bytes of the kernel), not in `http_cerver_global_init ()`; a table that chosen
  keys fill is not used, and its object is searched member by member

**Tests and scripts:** the tests of the copy of Jansson are `test/jansson/` (`test/bin/jansson`) and its fuzzer is
the target `jansson`; `test/json/`, `test/bin/json` and the target `json` are the ones of `cerver/json`, with the new
targets `compare`, `number`, `writer` and `scan`. `make conformance` builds `test/bin/conformance/json`, that
`bash test/conformance/json.sh` runs over JSONTestSuite in the image of `Dockerfile.conformance`.
`test/performance.sh` expects 0 allocations for a POST JSON (they were 14). `benchmarks/competitors/` measures
`cerver` (the new JSON) and `legacy` (the copy of Jansson), with the slowest and the fastest run of each measure, a
`search` operation and the case `texts` (long strings without escapes).

## 3.0b-5 (from 3.0b-4)

The event loop, keep-alive and the named pools of `nightly` (its F3, F4 and F5, the 2.1b-15 to the 2.1b-17), ported
by hand with the stop of the 3.0b-4: `CERVER_HANDLER_TYPE_EVENTS`, a new handler type that is opt-in and only for a
web cerver. Some structs are bigger (a binary built with the 3.0b-4 is built again); the entries «2.1b-15», «2.1b-16»
and «2.1b-17» of the `incompatibilities.md` of `nightly` have every detail.

**The structs and the API**

- **The structs are bigger, and some fields moved:** `Cerver` ends with `n_io_threads`, `max_connections`,
  `overload_wait_timeout` and `event_loops`; `HttpReceive` with `connection`, `pause` (the new enum
  `HttpReceivePause`), `authorize_pending` and `n_requests`; `HttpCerver` with the keep-alive settings, the pools,
  the health and ready routes and their counters; `Thpool` with `queue_awaited`; `CerverReceive` has `n_requests`
  and `idle_deadline` before `recycle_next`. `HttpRouteSettings` has `pool_name` and `pool` before its middlewares,
  so `middlewares` and `web_socket` moved in it, and with them the fields of `HttpEndpoint` and `HttpGroup` after
  their settings; `HttpRoute` has `pool_name`
- **`CERVER_HANDLER_TYPE_MAP` has a fourth row,** `CERVER_HANDLER_TYPE_EVENTS` = 3, with `cerver_set_io_threads ()`
  (`CERVER_DEFAULT_IO_THREADS`, 1), `cerver_set_max_connections ()` (`CERVER_DEFAULT_MAX_CONNECTIONS`, 2^14) and
  `cerver_set_overload_wait_timeout ()` (`CERVER_DEFAULT_OVERLOAD_WAIT_TIMEOUT`, 2^5 secs); the other handler types
  ignore the three. `cerver_start ()` fails with EVENTS in a cerver that is not `CERVER_TYPE_WEB` or that has no
  thpool threads
- **`HttpReceiveDispatch` has two more values,** `HTTP_RECEIVE_DISPATCH_HEALTH` (6) and `HTTP_RECEIVE_DISPATCH_READY`
  (7), and `HTTP_RECEIVE_DISPATCH_MAX` is 8
- **New:** `http_cerver_set_keep_alive ()`, `http_cerver_set_keep_alive_timeout ()` and its `_milliseconds`,
  `http_cerver_set_keep_alive_requests ()`, `http_cerver_set_keep_alive_follow ()`,
  `HTTP_RESPONSE_CONNECTION_KEEP_ALIVE`; `cerver/http/core/pool.h` (in the umbrella `cerver/http/http.h`) with
  `HttpPool`, `http_cerver_pool_create ()` and its setters, `http_group_set_pool ()`, `http_endpoint_set_pool ()`
  and `http_route_set_pool ()`; `http_cerver_set_health_route ()`, `http_cerver_set_ready_route ()` and
  `http_cerver_is_ready ()`; `thpool_has_queued ()`
- **The metrics and the stats of the admin routes have new series and keys,** none changed: the pools, the
  connections, the health and ready requests, the reused and the followed requests and the time of the writes of
  the uploads. A client that rejects a key that it does not know has to accept them

**With `CERVER_HANDLER_TYPE_EVENTS`** (what is different from THREADS in the same cerver)

- `decode_data` of a route with a Bearer token and the uploads generators run in an I/O thread, so they must not
  wait; the custom authentication, the handlers, the middlewares and the static files run in the thpool or in the
  pool of their route
- `CERVER_OVERLOAD_POLICY_WAIT` parks a request in its loop up to `cerver_set_overload_wait_timeout ()` and then
  answers 503 with `Retry-After`, the accept is not stopped; the requests go to the thpool in the order that they
  are complete; `SO_REUSEPORT` is set with more than one I/O thread
- **The stop wakes up the loops with the stop pipe of the 3.0b-4:** the thread in `cerver_start ()` waits on its
  read end too, so `cerver_stop_request ()` from a signal handler stops a cerver with event loops; a cerver whose
  pipe could not be created does not start its event loops
- **Keep-alive is on:** a response has `Connection: close` only when its connection is closed after it (an HTTP/1.1
  client that is kept gets no header, an HTTP/1.0 one `Connection: keep-alive`), pipelined requests are answered
  in their order, an idle connection is closed after `keep_alive_timeout` without a response, and at
  `cerver_set_max_connections ()` a new connection takes the place of the one that has been idle the longest. A
  client that reads a response until the end of its connection has to use its `Content-Length` or send
  `Connection: close`; a cerver that wants the close of before calls `http_cerver_set_keep_alive (http_cerver, false)`
- **The named pools only start with EVENTS;** with `THREADS` or `POLL` a cerver logs a warning and every request
  runs in the thread of its connection. A route with a pool that the cerver does not have stops the cerver when it
  starts. A path set as the health or the ready route is answered by cerver for GET and HEAD before the routes tree
  (the ready route answers 503 without closing its connection)

**With `THREADS` or `POLL`,** only after `http_cerver_set_keep_alive (http_cerver, true)`: the thread that handled a
request stays with its connection until its next request while no other connection waits for a thread.

**Tests and scripts:** the web servers of the tests take their handler type from `CERVER_TEST_HANDLER` (`threads`
by default, `events`) and their I/O threads from `CERVER_TEST_IO_THREADS`; `test/integration.sh` and
`test/nginx/run.sh` run the web pairs once per handler (`HANDLERS`); `test/performance.sh` also fails when EVENTS is
below 90 % of the req/s of THREADS, and below 150 % with a connection that is kept; `benchmarks/load` takes
`LOAD_KEEP_ALIVE` and `benchmarks/server` the handler type, its I/O threads and `BENCH_KEEP_ALIVE`. `make
DEFINES="…"` keeps the `-D CERVER_CRYPTO_OPENSSL` of the provider (`override`).

## 3.0b-4 (from 3.0b-3)

The backlog of the review of the code: the fixes of the 1.9b-2 (`core`) and of the 2.0b-75 (`http`), what had no
use is removed, and the owner of a stream of WebSocket. The wire of HTTP changes for the videos (RFC 9110), some
structs change their fields (a binary built with the 3.0b-3 is built again) and some functions go away.

- **Removed** (they had no use; `core` and `http` deprecated them): `cerver_set_sockets_pool_init ()` and
  `CERVER_DEFAULT_SOCKETS_INIT`; `lobby_stats_set_threshold_time ()`; `lobby_get ()` and `lobby_search_with_id ()`
  (`lobby_get_reference ()` and `lobby_search_with_id_reference ()` take the reference with the lock);
  `timer_get_gmt_time ()` and `timer_get_local_time ()` (`timer_get_gmt_time_r ()` and `timer_get_local_time_r ()`);
  `game_set_player_comparator ()` and `GameCerver::player_comparator`; the field `threshold_time` of
  `ConnectionStats`, `ClientStats`, `AdminCerverStats` and `LobbyStats` (only `CerverStats` has it, and it works);
  `CONNECTION_DEFAULT_BAD_PACKETS`, `SERVICE_CONNECTION_NAME_SIZE`, `SERVICE_DEFAULT_MAX_SLEEP`, the
  `GAME_DEFAULT_*` macros and `ClientEvent::request_type`
- **The videos follow RFC 9110:** `http_response_handle_video ()` answers a request without a Range (or with an
  invalid one) with 200 and the whole video, the last byte of `bytes=N-M` is included and a range never goes after
  the end of the video, and a range that starts after its end gets 416 with the size of the video
- **A stream of WebSocket belongs to the thread that began it:** `http_web_socket_send_write ()` and
  `http_web_socket_send_end ()` from another thread return 1 and the stream goes on (the end unlocked the mutex of
  the owner)
- **The core of the 1.9b-2:** `Cerver` has `stop_pipe[2]` instead of `stop_mutex` / `stop_cond` and no `auth_packet`;
  `CerverStats` has `threshold_start`; `JobQueue` has `requests`; the events and the errors have `shared_args`;
  **the admin limits are enforced and `ADMIN_CERVER_DEFAULT_MAX_ADMINS` is 1**; replacing a handler, a received data
  or a custom data of an endpoint or a group deletes the old one when it is another pointer; a files cerver without
  an uploads path rejects an upload and ends its connection; `str_set ()` grows, `str_split ()` without the delimiter
  gives 1 token; `config.h` needs `-D _GNU_SOURCE` (an `#error` without it); `"Timeout"` in `RECEIVE_ERROR_MAP`;
  what fails without memory returns NULL or keeps what it had (the list is in the `incompatibilities.md` of `core`,
  «1.9b-2»)
- **The HTTP of the 2.0b-75:** `http_response_get ()` is NULL without an HTTP cerver, `http_response_return ()` and
  `http_cerver_auth_jwt_delete ()` release what the pool does not take, `http_request_set_dirname ()` keeps its last
  character, the timeouts do not warn when they are not a power of 2, and `thirdparty.md` has multipart-parser-c
- **New:** `c_string_copy_bounded ()`, `dlist_get_apply ()`, `htab_get_apply ()`, `avl_get_node_data_apply ()`,
  `lobby_get_reference ()`, `lobby_search_with_id_reference ()`, `timer_get_gmt_time_r ()` and
  `timer_get_local_time_r ()`; the static analyzers (`test/analyze.sh`)

## 3.0b-3 (from 3.0b-2)

The documentation of the headers: the symbols of `libcerver.so` and the wire are the ones of the 3.0b-2, only the
comments of the headers change, with the fixes below.

- `cerver_start (NULL)` returns 1, it dereferenced the NULL cerver; `cerver_set_admin_enable ()` called again keeps
  the admin cerver that the cerver already has (and returns 0), it created another one and lost the first
- **The fixes that the documentation found** (each one with its test), that change what a call returns:
  - with a NULL argument they fail or keep the current value instead of crashing: `connection_set_name ()`,
    `connection_set_values ()`, `client_set_name ()`, `client_get_with_session_id ()`, `packet_send ()`,
    `packet_send_unsafe ()`, `packet_send_split ()`, `packet_send_pieces ()`, `str_contains ()` (-1),
    `c_string_concat ()`, `bool_value_from_string ()`, `xtoi ()`, `cerver_log_with_date ()`, `job_reset ()`,
    `lobby_summary_packet_create ()`, `admin_get_with_session_id ()`, `admin_cerver_get_n_handlers_alive ()` (before
    the admin cerver starts), `balancer_get_type ()` (`BALANCER_TYPE_NONE`), `balancer_service_get_status ()`
    (`SERVICE_STATUS_NONE`), `avl_is_not_empty ()` (false), the HTTP custom data and receive getters,
    `http_content_type_*` and the CORS, Content-Range and send methods of the responses (1);
    `packet_send_to_socket ()`, `packet_generate ()` and `game_type_add_lobby_config ()` return 1 with a NULL argument
    (they returned 0)
  - `sock_set_timeout ()` returns 1 on error (it returned -1); `random_int_in_range ()` includes both limits (also
    when they are equal or reversed); `c_string_remove_sub_after_token_with_idx ()` returns NULL without the token
  - `str_set ()` and `str_set_with_args ()` do not change a String without `max_len` (they overflowed it);
    `str_n_copy ()` and `str_n_replace_with ()` keep the real length of a shorter source; `str_split ()` counts its
    tokens and returns NULL for a String of only delimiters
  - `arena_alloc ()` fails with the sizes that wrap around; `packet_append_data ()` fails when the size wraps;
    `packet_set_packet ()` releases the copy of a packet that was a reference; `c_string_create_with_ptrs ()` and
    `c_string_n_copy ()` (with 0) do not write out of their buffers; `receive_handle_new ()` starts without received
    bytes
  - the lengths of `http_cerver_generate_uploads_path ()`, `http_header_set ()` and
    `http_multi_part_set_generated_filename ()` are the ones of the truncated value (they were the ones that the format
    needed); `http_origin_init ()` ends a truncated origin; `http_response_set_data ()` does not free the data of a
    reference; `http_cerver_register_admin_file_system ()` fails without a path; the pairs without a key or a value
    are skipped
- The headers are documented with Doxygen: `make docs` writes the reference in `reference/` (HTML and XML), with
  `Doxyfile`, `Dockerfile.docs` (Doxygen 1.9.8) and the `docs` job of `build.yml`, that fails with any warning
- Every clang build adds `-Wdocumentation -Wdocumentation-unknown-command -fcomment-block-commands=threadsafety`
  (`-Wno-documentation-deprecated-sync`): with `WERROR=1`, a comment of a header that does not match its declaration
  fails the build; `cerver/http/json/hashtable.h` and `cerver/http/jwt/jwt.h` turn them off for their upstream
  comments with `#pragma clang diagnostic`
- The parts of `src/cerver/core/` that did not use HTTP do not include its headers

## 3.0b-2 (from 3.0b-1)

The structure of the tree: every header changes its path and the functions with `_by_` are named with `_with_`;
the rest of the symbols of `libcerver.so` are the same. The table with every old and new path is in `roadmap.md`
§11.

- **Every include changes:** the headers are in folders by domain (`core/`, `admin/`, `auth/`, `client/`,
  `connection/`, `files/`, `handler/`, `load/`, `network/`, `packets/`, `system/`; `http/core/`, `http/headers/`,
  `http/requests/`, `http/responses/`, `http/routes/`, `http/security/`), e.g. `cerver/client.h` is
  `cerver/client/client.h`, `cerver/auth.h` is `cerver/auth/auth.h`, `cerver/http/request.h` is
  `cerver/http/requests/request.h`, `cerver/http/headers.h` is `cerver/http/headers/fields.h` and
  `cerver/game/gametype.h` is `cerver/game/types.h`; `cerver/config.h` and `cerver/version.h` stay on top. There are
  no headers with the old paths
- **The big headers are split in parts,** and the main header of a module does not include its parts: a program
  that used `cerver_start ()` with `cerver/cerver.h` now includes `cerver/core/start.h` (or the umbrella). The parts
  are `cerver/core/{types,info,data,handler,start,update,end,report,serialization}.h`,
  `cerver/admin/{cerver,stats,lifecycle,handler,poll}.h`, `cerver/auth/hold.h`,
  `cerver/client/{events,connect,requests,handler}.h`,
  `cerver/connection/{stats,data,lifecycle,end,registry,update,send}.h`,
  `cerver/files/{cerver,images,send,receive}.h`, `cerver/load/{stats,service,start,route,end}.h`,
  `cerver/packets/header.h`, `cerver/http/core/{pairs,public,routes,uploads,limits,settings,stats,admin,handler}.h`
  and `cerver/http/headers/{types,header,buffer}.h`
- **`<cerver/cerver.h>` is now the umbrella of the core** and **`<cerver/http/http.h>` the one of HTTP**: each one
  includes every public header of its part. The `Cerver` is declared in `cerver/core/cerver.h` and the
  `HttpCerver` in `cerver/http/core/http.h`; a program that included `cerver/cerver.h` or `cerver/http/http.h`
  still compiles, with more headers
- **The functions with `_by_` are named with `_with_`:** `admin_comparator_with_id ()`,
  `admin_get_with_session_id ()`, `admin_get_with_sock_fd ()`, `admin_remove_connection_with_sock_fd ()`,
  `cerver_poll_get_idx_with_sock_fd ()`, `client_get_with_session_id ()`, `client_get_with_sock_fd ()`,
  `client_remove_connection_with_sock_fd ()`, `connection_get_with_sock_fd_from_admin ()`,
  `connection_get_with_sock_fd_from_client ()`, `connection_get_with_sock_fd_from_on_hold ()`,
  `dlist_merge_two_with_condition ()`, `dlist_remove_with_condition ()`, `dlist_split_with_condition ()`,
  `file_send_with_fd ()`, `files_image_get_type_with_extension ()`, `game_type_get_with_name ()`,
  `http_cerver_add_responses_header_with_name ()`, `http_content_type_with_extension ()`,
  `http_content_type_with_mime ()`, `http_content_type_mime_with_extension ()`, `http_header_type_with_string ()`,
  `http_headers_{add,count,find,remove,set}_with_name ()`, `http_response_header_{add,count,get,remove,set}_with_name ()`,
  `lobby_search_with_id ()`, `player_comparator_with_id ()` and `player_get_with_sock_fd_list ()`
- **Headers that are not installed anymore:** `cerver/http/access.h`, `cerver/http/admin.h`,
  `cerver/http/compatibility.h` and `cerver/http/recycle.h` (their functions are `CERVER_PRIVATE`, still exported);
  the `CERVER_PRIVATE` declarations of `cerver/http/custom.h` (`http_custom_header_create ()`,
  `http_custom_header_set_value ()`), the `CERVER_HIDDEN` ones of `cerver/http/websockets/settings.h` and
  `http_response_send_pieces_status ()` of `cerver/http/response.h` moved to the internal headers of `src/`
- `HttpAdminFileSystemStats` and the admin methods of the old `cerver/http/admin.h` are internal
  (`src/cerver/http/core/internal.h`); the public `cerver/http/core/admin.h` has the admin settings of the
  `HttpCerver`
- The header guards follow the paths (`_CERVER_CORE_CERVER_H_`, `_CERVER_CLIENT_CLIENT_H_`,
  `_CERVER_HTTP_HEADERS_FIELDS_H_`, …); `_CERVER_H_` and `_CERVER_HTTP_H_` are the umbrellas
- The tests binaries follow the folders: `test/bin/connection`, `handler`, `load` (balancer and routing) and
  `network` (network, socket and udp) replace `test/bin/{receive,balancer,routing,socket,udp}`; the handler pair is
  `cerver/handler`

## 3.0b-1 (from 2.1b-13)

The first beta of the 3.0 line (`labs`), born from the 2.1b-13 of `nightly`: the line, its license and the base
of the modules of its own.

- **The license is Apache-2.0** (`LICENSE.md`, `NOTICE`): it was a copyright without a license. The code of other
  projects keeps its own license (`thirdparty.md`), that now also lists sha-2 (`utils/sha256.c`) and fastbase64
  (`utils/base64.c`)
- **`cerver/crypto`** is new: `cerver_crypto_random_bytes ()`, `cerver_crypto_sha1 ()` with
  `CERVER_CRYPTO_SHA1_SIZE` and `cerver_crypto_cleanse ()`, in `include/cerver/crypto/{random,hash,memory}.h`
- The request ids, the keys of the handshake of a WebSocket client and the masks of its frames are random bytes
  of `getrandom ()`, like the session ids: they came from `RAND_bytes ()` of OpenSSL. A process whose seccomp
  forbids `getrandom ()` gets no request id (it is empty, as when `RAND_bytes ()` failed) and its WebSocket client
  can not connect
- The accept of the handshake is computed by the provider through `cerver_crypto_sha1 ()`, and the material of a
  JWT key is cleared with `explicit_bzero ()` instead of `OPENSSL_cleanse ()`: the same results
- `CRYPTO=openssl` in the `makefile` defines `CERVER_CRYPTO_OPENSSL`: it is the default and the only provider, a
  build with another value stops. `libcerver.so` still links OpenSSL (`-lssl -lcrypto`), the JWT calls it
  directly until L2
- `.gitattributes` has no `merge=ours`: `labs` receives no merges

## 2.1b-13 (from 2.1b-12)

The rest of the fixes of the review of the 2.1b-10, with the ones of the 2.0b-72 applied by hand, and the changes
of the WebSocket API that the review asked for before the event loop.

**The requests (the 2.0b-72 and the parser):**

- The default request timeout is `HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT` = 300 secs, it was 0 (no timeout): a request
  whose body takes longer is answered with 408. `http_cerver_set_request_timeout (http_cerver, 0)` keeps the
  behavior of before
- A header that is not one of the HTTP headers map (a custom header) fits up to the limit of
  `http_cerver_set_max_custom_header_size ()` (new), by default `HTTP_CERVER_DEFAULT_MAX_CUSTOM_HEADER_SIZE`
  (8192 bytes): the limit was 512 bytes without a setting. `HTTP_CUSTOM_HEADER_VALUE_SIZE` was removed
- The trailers of a chunked body and the extensions of its chunks count for `max_header_size`, and each trailer
  for `max_headers`: a request that exceeds them is answered with 431 (they had no limit)
- The spaces and tabs after the value of a header are not part of the value, they were kept
- A target in absolute form (`GET http://host/path?query HTTP/1.1`) is routed by its path, and the fragment of a
  target is not part of its path nor of its query: both went to the catch all handler
- `Range: bytes=-N` is the range of the last N bytes of the file: `http_request_get_bytes_range ()` returns `true`
  with the new `HTTP_RANGE_TYPE_SUFFIX` (4), `start` 0 and the number of bytes in `end`, and
  `http_response_handle_video ()` sends them (the 2.1b-12 ignored the header)
- Only `HTTP/1.0` and `HTTP/1.1` are served. A request line of another protocol that the parser takes (`RTSP/1.0`,
  `ICE/1.0`) is answered with 400, and another version of HTTP (`HTTP/0.9`, `HTTP/2.0`, the preface of HTTP/2
  included, that was a 400) with the new 505: they reached the handlers as if they were HTTP/1.1.
  `HTTP_PARSER_LENIENT_VERSION` still serves any version of HTTP
- A request whose `Transfer-Encoding` does not end with `chunked` is always a 400, also with
  `HTTP_PARSER_LENIENT_TRANSFER_ENCODING` and `HTTP_PARSER_LENIENT_CHUNKED_LENGTH`: with them it was read until its
  client closed and never answered
- A NUL in the value of a header is a 400 also with `HTTP_PARSER_LENIENT_HEADERS`: it was kept, and the value ended
  in it for whoever compared it (the origins whitelist)
- `HttpCerver` (ABI): new `max_custom_header_size` after `max_file_size`. `HttpReceive` (ABI): new
  `protocol_length` and `protocol_other` after `unparsed`

**The WebSocket protocol:**

- A frame whose length is not the shortest encoding of its value (5 bytes in the 16 bits form) fails its connection
  with 1002 (RFC 6455 section 5.2), it was accepted
- A `Sec-WebSocket-Key` that is not the only base64 form of its 16 bytes is a 400
- A compressed message that ends inside a deflate block fails with 1007: it was delivered as it was and the next
  message failed. A block with BFINAL does not end a message, other blocks can follow it and the context goes on
  after it (the example of RFC 7692 section 7.2.3.4 was a 1007)
- An empty compressed message after another message is the `0x00` of RFC 7692 section 7.2.3.6, it was a frame
  without payload that the peer could not inflate. A message of more than `HTTP_WEB_SOCKET_DEFLATE_INPUT_MAX` bytes
  is sent without compression (it was cut to its 32 bits)
- `http_web_socket_deflate_decompress ()` returns `HTTP_WEB_SOCKET_INFLATE_INVALID` (1), `_STOPPED` (2) and the
  new `_FAILED` (3) when the decompressor failed: its connection ends with 1011, it ended with 1007
- `http_web_socket_parser_execute ()` ends a frame whose last bytes stopped the parser in its next call, also
  without bytes: the end waited for the bytes of the next frame
- An upgrade whose accept can not be computed is a 500, it was a 101 with an empty accept

**The WebSocket connections and the client:**

- A message that the connection does not take because it already sent its close returns 1 without `on_error` and
  without aborting the connection (the 2.1b-12 did it for the pieces of a stream)
- A send of a connection that was kept after its cerver ended returns 1 (it read the timeout of the cerver)
- After its close frame, a connection that did not get the close of its peer ends its writes and reads what the peer
  still sends for up to 256 ms and 64 KiB before it closes its socket, so the peer gets the close frame and not a reset
- The bytes that a connection counts are the ones of its messages, without compression and without the headers of
  their frames, like the ones of its cerver (they were the bytes of its socket)
- `http_web_socket_client_delete ()` called by a method of its client only closes it, and
  `http_web_socket_client_wait ()` returns at once there: they joined their own thread and released the client
  that it was using. A client is deleted from another thread
- A client that is deleted or closed while it connects or waits for its handshake ends at once, it waited for their
  timeouts. A `connect ()` or a `recv ()` that a signal interrupts goes on
- The wait before a reconnection never passes its maximum (its jitter was added above it) nor wraps after many
  failures, and a connection that ends before it lasted `reconnect_max` milliseconds counts as a failure: a server
  that accepts and closes was reconnected without a wait
- `http_web_socket_client_set_url ()` that returns 1 leaves the url that the client had (it kept a part of the new
  one). `http_web_socket_client_close ()` with a code or a reason that can not be sent returns 1 and does not stop
  the reconnections. `http_web_socket_client_add_header ()` rejects a name that is not a token, a value with a line
  end and the headers of the handshake
- `http_web_socket_client_connect ()` whose connection opened and could not get its thread reports it with
  `on_error` (`HTTP_WEB_SOCKET_ERROR_MEMORY`) and `on_close`, and it is not one of the connections of the client.
  The first frames of the server that do not fit in memory fail the attempt, they were lost

**The WebSocket API:**

- The timeouts in milliseconds say it in their names:
  `http_endpoint_set_web_socket_ping_milliseconds ()`, `_close_timeout_milliseconds ()` and
  `_send_timeout_milliseconds ()`, the same three of `http_group_set_web_socket_*`,
  `http_cerver_set_web_socket_broadcast_timeout_milliseconds ()`,
  `http_web_socket_client_set_ping_milliseconds ()`, `_close_timeout_milliseconds ()`,
  `_send_timeout_milliseconds ()`, `_timeouts_milliseconds ()` and `_reconnect_milliseconds ()`. The names without
  the suffix were removed
- `HttpWebSocket`, `HttpWebSocketClient`, the registry of an HTTP cerver (`struct _HttpWebSocketRegistry`), the
  handshake of an upgrade (`struct HttpWebSocketHandshake`) and `HttpWebSocketClientResponse` are opaque: their
  definitions and the methods that only cerver calls (also `http_web_socket_client_response_parse ()`, `_check ()`
  and `_backoff_delay ()`) are in `src/cerver/http/websockets/internal.h`, that is not installed. Code that read
  their fields uses the getters; the counters of the registry are read with the new
  `http_cerver_web_socket_get_stats ()` and `http_web_socket_get_stats ()` (`HttpWebSocketStats`)
- `HttpWebSocketUpgrade` (ABI): new `headers` at its end, for the new `http_web_socket_upgrade_add_header ()`, that
  adds a header to the 101 (a name can be repeated; the headers of the handshake are of cerver). An upgrade that is
  initialized by position needs its new field
- New `http_web_socket_get_query ()`: the query of the request of the upgrade
- The methods of a connection are serialized by connection, the thread that runs them is not part of the API
  (`websockets/settings.h`): code that keeps something of a connection in the variables of a thread breaks with the
  event loop

**Building and testing:**

- The library and its tests are compiled with `-I src` for the internal header
- `NATIVE=1` builds the benchmarks with `-march=native` only (`-mavx2` did not compile in aarch64);
  `benchmarks/bin/http-parser` and `benchmarks/bin/frames` exit with 1 when what they measure failed
- `build.yml` builds g++, clang, valgrind and the sanitizers with `ZLIB=1` (gcc without it), and the benchmarks
  with the three compilers. The tests multiply their short timeouts by `TEST_TIME_SCALE` (4 with a sanitizer and
  under the `RUNNER` of `test/run.sh`)
- The input of the `frame` fuzzer has a second byte (the compression that was negotiated), a corpus of the versions
  before is read with other settings. `test/autobahn/check.py` fails a report that does not have its 517 cases
- `test/performance.sh` counts the allocations of 1024 requests and of the WebSocket messages without rounding them,
  and fails when its server or its load fail; the long fuzzing of `scheduled.yml` is a job for each target

## 2.1b-12 (from 2.1b-11)

The first fixes of the review of the 2.1b-10, with the ones of the 2.0b-71 applied by hand.

- The query and the values of a `application/x-www-form-urlencoded` body are split by `&` and `=` before each key and
  each value is decoded: `?key=a%26other%3D1` is one value, `a&other=1`, it was `key=a` and `other=1`; an escaped NUL
  ends its own value and not the rest of the query. A client that escapes the whole body of a form, with its `=` and
  its `&` (`name%3Dvalue%26other%3D1`), is not understood anymore: only the keys and the values are escaped
- `http_request_get_bytes_range ()` returns `false` and does not change the bytes range for a `Range` that does not
  start at a position (`bytes=-500`, `bytes=-`, another unit, a value without numbers): they crashed or were read as a
  range. A range needs its `-`: `bytes=5` is ignored, it was read as `bytes=5-`
- `http_web_socket_send_begin ()` that returns 1 holds nothing: the stream must not be ended, and its
  `http_web_socket_send_write ()` and `http_web_socket_send_end ()` return 1 without a change. It returned 1 with the
  messages of the connection locked until `http_web_socket_send_end ()`, that aborted the connection
- A piece or the end of a stream that the connection does not take because it already sent its close returns 1
  without `on_error` and without aborting the connection, that ends with its closing handshake (it ended with
  `HTTP_WEB_SOCKET_ERROR_SEND` and the code 1006)
- The send timeout of a message starts when the message can be written, it started before the wait for the other
  messages of the connection
- `cerver_websocket_sent_bytes_total` counts the bytes of the messages that are sent in pieces, and
  `cerver_websocket_received_bytes_total` the bytes of every fragment of a message that is handled with
  `on_fragment` (only the bytes of its last fragment were counted)
- `http_web_socket_client_set_url ()` rejects a url with a space, a control character or a delete (0x7F)
- The subprotocols of `http_web_socket_client_set_protocols ()`, `http_endpoint_set_web_socket_protocols ()` and
  `http_group_set_web_socket_protocols ()` must be tokens (RFC 9110): a list with a `NULL`, an empty name, a
  separator or a line end is rejected, and a rejected list does not change the settings (it removed the list that
  was set and left a part of the new one)
- Not exported anymore (`CERVER_HIDDEN`, they were `CERVER_PUBLIC` or `CERVER_PRIVATE`): the API of the parser in
  `http/parser/api.h` (`llhttp_init ()`, `llhttp_execute ()`, the getters, `llhttp_set_lenient_*` …) with every
  symbol of `http/parser/`, the methods of `websockets/websocket.h` and `websockets/registry.h` that only cerver
  calls (`http_web_socket_create ()`, `_start ()`, `_run ()`, `_abort ()`, `_send_timeout ()`,
  `http_web_socket_registry_*` …), `http_web_socket_settings_*` and `http_response_send_pieces_status ()`. The fields
  of the parser are read from `HttpReceive.parser` (`parser.type`, `parser.error`)
- `HttpWebSocket` (ABI): new `stream_size`
- `benchmarks/bin/messages` measures from the first message to the last echo, its messages per second are not the
  ones of the versions before; `test/performance.sh` sends 1048576 messages in each WebSocket load

## 2.1b-11 (from 2.1b-10)

A response is its status and a list of headers that is written for its protocol when it is sent, and what belongs
to a request is separated from what belongs to its connection. A cerver still handles one request in each
connection and closes it after its response.

- The headers of a response arrive in the order they were added to it (they arrived in the order of `http_header`),
  after `Server`, and the headers that cerver adds for the request go after them: `Connection: close` first (it was
  the third line of every response), then `Date`, `X-Request-Id`, the CORS ones, `Vary` and `ETag`. The responses
  of `http_response_json_*` and `http_response_render_*` keep `Content-Type` before `Content-Length`
- `Connection: close` is not part of a compiled header (`HttpResponse.header`, `HttpResponse.res`), it is added when
  the response is sent, and a response with its own `Connection` header is not given another (it was sent twice).
  The response of `HTTP_RESPONSE_SERVICE_UNAVAILABLE` keeps it
- The headers of the cerver (`http_cerver_add_responses_header ()`) are also added to the responses of
  `http_response_json_*` and `http_response_render_*`, they only reached the responses with their own headers
- A header whose value has a CR, a LF or a NUL is rejected (`http_response_add_header ()` and the other methods
  return 1, they wrote it as it was), so a value of a request can not add headers to a response; the header
  `HTTP_HEADER_UNDEFINED` is rejected too (it was sent as `Undefined: value`)
- The value of a header is not cut: it was cut at 256 bytes with its name (127 bytes in
  `http_response_add_custom_header ()`), and a longer one sent the bytes after it to the client. A value written
  with a format can have up to `HTTP_RESPONSE_HEADER_FORMAT_SIZE` (1024) bytes, a longer one is not added
- A header or the status that changes after the response was compiled discards its `header` and its `res`, they
  are compiled again when it is sent (the compiled ones were sent without the change)
- `HttpResponse` (ABI): `HttpHeaders headers` and `header_is_raw` replace `n_headers` and
  `HttpHeader headers[HTTP_HEADERS_SIZE]` (a response is 16 KiB smaller); `HttpCerver.response_headers` is a
  `HttpHeaders` and `n_response_headers` is removed. `HttpHeader` is only used by the parts of a multi-part body
- New in `headers.h`: `HttpHeaderField`, `HttpHeaders` and `http_headers_*`. New in `response.h`:
  `http_response_header_set ()`, `_add ()`, `_remove ()`, `_count ()`, `_get ()` and the same with `_by_name`
  (any name that is a token, a header can be repeated), `HTTP_RESPONSE_CONNECTION_CLOSE` and
  `HTTP_RESPONSE_HEADER_FORMAT_SIZE`. New in `http.h`: `http_cerver_add_responses_header_by_name ()`
- Removed: `http_response_header_set_with_type ()` and `http_response_header_set_with_type_and_args ()`
  (`headers.h`), `HTTP_RESPONSE_SEND_FILE_HEADER_SIZE`, `HTTP_RESPONSE_RENDER_TEXT_HEADER_SIZE`,
  `HTTP_RESPONSE_RENDER_JSON_HEADER_SIZE` and `HTTP_RESPONSE_EXTRA_HEADERS_SIZE`;
  `HTTP_RESPONSE_CONTENT_LENGTH_SIZE` is 32 (it was 16)
- `http_response_send_message ()` takes the headers of the response before its compiled header (private)
- `HttpReceive` (ABI): new fields `framed`, `sock_fd`, `start_time`, `received`, `diagnostics`, `headers_deadline`
  and `request_deadline`; `HandlerThreadContext.http_receive`. `http_receive_prepare ()` replaces
  `http_receive_create_arena ()`: the receive lives in the memory of its owner, not in the arena of its request.
  New: `HttpReceiveResources`, `HttpReceiveDiagnostics`, `http_receive_reset ()` and `http_access_request ()`
  (`http/access.h`)
- The fuzz target `pipeline` and its corpus

## 2.1b-10 (from 2.1b-9)

The merge of the 2.0b-66 to the 2.0b-70 of `http`: their entries below apply to code built with a 2.1b-9, except
the sends with a deadline of the 2.0b-66, that the 2.1b-9 already had. HTTP and WebSocket do not change in the wire.

- The wire of the protocol of cerver changes with the portable header of the 2.0b-66: a 2.1b-10 does not talk with a
  2.1b-9 through TCP or UDP packets, its clients, services and balancers are updated together
- The internal methods of the JSON (`utf8_*`, `hashtable_*`, `jsonp_*`) are not exported since the 2.0b-67: a text
  of a WebSocket message is validated with `http_web_socket_text_check ()`, not with `utf8_check_string ()`
- The JWT keys are loaded when the cerver starts (2.0b-67): a WebSocket endpoint with bearer authentication stops the
  cerver when its key does not match the algorithm, like any other endpoint

## 2.1b-9 (from 2.1b-8)

WebSocket (RFC 6455 and permessage-deflate, RFC 7692) on the threads model: each connection runs in its own thread
after its upgrade, and a client connects to a server. The send deadlines of the 2.0b-66 arrived before its merge.

- An endpoint with `HTTP_ROUTE_MODIFIER_WEB_SOCKET` needs its handlers (`http_endpoint_set_web_socket ()`, in it or in
  one of its groups) and the GET method, without them the routes tree does not compile (it answered 501); a request
  to it with an upgrade is answered with 101, one without an upgrade goes to its `HttpHandler` or it is answered with
  426 (it was 501). A route of the first API (`HttpRoute`) with the modifier is still answered with 501, with a
  warning when the tree compiles
- An upgrade that is not valid is answered before its body: 400 (a key that is not 16 bytes, HTTP/1.0, a body),
  426 with `Sec-WebSocket-Version: 13` (another version), 403 (an Origin out of the whitelist with
  `http_endpoint_set_web_socket_origin_check ()`), 503 (the most connections of the cerver)
- `Sec-WebSocket-Key` and `Sec-WebSocket-Version` must appear once in every request, a repeated one is answered with 400
- New fields (ABI): `HttpRouteSettings.web_socket` (`HttpWebSocketSettings`), `HttpCerver.send_timeout` and
  `HttpCerver.web_sockets`, `HttpReceive.send_deadline`, `send_deadline_set`, `web_socket` and `upgraded`
- New headers in `include/cerver/http/websockets/`: `websocket.h` (`HttpWebSocket`), `client.h`
  (`HttpWebSocketClient`), `settings.h`, `registry.h`, `frame.h`, `message.h`, `deflate.h`, `handshake.h`, `text.h`
- New in `socket.h` (from the 2.0b-66): `socket_deadline_create_milliseconds ()`, `socket_send_vector_until ()`,
  `socket_send_buffer_until ()`, `socket_send_file_until ()` (with the offset of the file); every send of a socket uses
  `MSG_DONTWAIT` and polls until its deadline, also in a blocking socket. New in `http.h`:
  `http_cerver_set_send_timeout_milliseconds ()`, all the sends of a response share one deadline. New in `config.h`:
  `CERVER_ZERO_INITIALIZER` and `CERVER_PRINTF_FORMAT`
- The drain of a web cerver closes its WebSocket connections with 1001, and cuts the ones that did not answer before
  its grace; a WebSocket connection is a thread of `connection_threads` until it ends
- `/cerver/metrics` has the `cerver_websocket_*` metrics, and the admin stats have a `websockets` section; the routes
  json has a `websocket` object in the WebSocket endpoints

## 2.1b-8 (from 2.1b-7)

The parser is llhttp 9.4.3 instead of http_parser 2.x, in strict mode. The entries of the 2.0b-62 to 2.0b-65 below
arrived with the merges of `http`.

- Without `include/cerver/http/http_parser.h` and its symbols (`http_parser_*`, `http_errno_name ()`,
  `http_errno_description ()`, `http_should_keep_alive ()`, `enum http_errno`, `HTTP_PARSER_ERRNO`): the parser is in
  `include/cerver/http/parser/` (`api.h`, `helpers.h`, `llhttp.h`) and its symbols are `llhttp_*`
- `HttpReceive.parser` is an embedded `llhttp_t` (it was an `http_parser *`) and `HttpReceive.settings` an
  `llhttp_settings_t`; without `HttpReceiveToken` and `HttpReceive.token`; new fields `keep_alive_requested` and
  `unparsed` (ABI)
- One table of methods, `REQUEST_METHOD_MAP` in `cerver/http/method.h` (included by `request.h`), with the values of
  llhttp: the 34 HTTP methods (0 to 33) and `QUERY` (46), all of them can be registered. `REQUEST_METHOD_SIZE` is 47
  (it was 29), so `HttpRouteNode.endpoints` and `HTTP_HANDLERS_COUNT` (`HttpRoute.handlers`) have 47 entries;
  `REQUEST_METHOD_UNDEFINED` is 255 (it was 34, the value of `PRI` in llhttp) and it is a value of `RequestMethod`.
  Without `enum http_method`, `http_method`, `HTTP_METHOD_*`, `HTTP_METHOD_NONE`, `method_strings` and
  `http_method_str ()`: `http_request_method_str ()` names every method
- `HTTP_ROUTE_METHODS_STRING_SIZE` (and `HTTP_TREE_ALLOW_SIZE`) is 512 (it was 128), so `HttpRouteNode.allow` too
- Strict mode: a request line or a header that ends with LF without CR, an obsolete line folding, a version that is
  not 0.9, 1.0, 1.1 or 2.0, spaces after a chunk size and an invalid quoted chunk extension are answered with 400; a
  `QUERY` request is accepted (it was 400); headers of more than 80 KiB are not rejected by the parser (the limit of
  the cerver answers 431). The cases are in `roadmap.md` §4.2
- `http_cerver_set_max_url_size ()` and `http_cerver_set_max_header_size ()` with 0 set the default value, the url and
  the headers limits are always active (0 disabled them)
- New: `HttpParserLenient`, `HTTP_PARSER_LENIENT_ALL`, `http_parser_lenient_string ()`,
  `http_parser_lenient_description ()`, `HttpCerver.parser_lenient` (ABI) and `http_cerver_set_parser_lenient ()`;
  the header value relaxed flag of llhttp (0x400) is not available, it loops forever with a NUL in a header value
- A method that the parser accepts and cerver does not know is answered with 501
- `cerver_http_requests_incompleted_total` counts the requests that llhttp could not parse

## 2.0b-70 (from 2.0b-69)

The port of the core of the 1.8.1. The wire does not change: a 2.0b-70 talks with a 2.0b-69.

- The default session ids of `cerver_set_sessions ()` are `SESSION_ID_LENGTH` (64) hex characters of
  `SESSION_ID_RANDOM_BYTES` random bytes of the kernel (`getrandom ()`); they were the SHA-256 of the time and a
  random number, that could be guessed
- `cerver_set_sessions (cerver, NULL)` uses the default session id generator, it returned an error
- The data that an authenticate method sets in `AuthData.data` becomes the data of the new client
  (`client_set_data ()`), and without a new client it is released with its `delete_data`: it was never released, a
  method that released it by itself must stop doing it
- The data of a handler is released with its `data_delete` also when the handler never had a thread, like a direct
  handler or a handler of a cerver that never started; the handler releases it once
- With `cerver_set_multiple_handlers ()`, a direct handler (`handler_set_direct_handle ()`) runs in the thread that
  received the packet, its packets went to a queue that no thread took; the packets follow
  `cerver_set_app_handler_delete ()`
- `cerver_set_multiple_handlers ()` with 0 handlers or a second time is an error; `cerver_handlers_add ()` with an id
  that is not below the number of handlers or that already has a handler is an error, and the handler still belongs
  to the caller
- `cerver_get_n_handlers ()` can be called before the cerver starts (it crashed)
- `cerver_set_welcome_msg ()` with a `NULL` message is an error (it crashed); `cerver_set_receive_buffer_size ()`,
  `cerver_set_on_hold_receive_buffer_size ()`, `cerver_set_max_received_packet_size ()`,
  `connection_set_receive_buffer_size ()` and `admin_cerver_set_max_fds ()` ignore 0, and
  `admin_cerver_set_max_fds ()` ignores a call after the cerver started
- The registers, unregisters and triggers of the events and errors of a cerver and of a client fail with a type that
  is not below `CERVER_MAX_EVENTS`, `CERVER_MAX_ERRORS`, `CLIENT_MAX_EVENTS` or `CLIENT_MAX_ERRORS`: they wrote and
  read outside of their arrays
- An event or an error with `drop_after_trigger` gives its arguments to the data of its only trigger, that releases
  them with its `*_data_delete ()`; they were released when the event was dropped, while an action in its own thread
  still used them
- Changed fields (ABI): `CerverEventData` and `ClientEventData` (new `owns_action_args`), `CerverErrorEventData` and
  `ClientErrorData` (new `delete_action_args` and `owns_action_args`)
- A cerver that tears down sends `CERVER_PACKET_TYPE_TEARDOWN` to its clients before it shuts their connections down
  (it was sent after, so it never arrived), and a client that gets it triggers `CLIENT_EVENT_CERVER_TEARDOWN` and
  closes or reconnects only that connection, it used to stop the whole client; a web cerver never sends it into its
  HTTP connections
- A client triggers `CLIENT_EVENT_CERVER_INFO` when it gets the info of its cerver; an on hold connection that its
  client closes triggers `CERVER_EVENT_ON_HOLD_DISCONNECTED`, it triggered `CERVER_EVENT_ON_HOLD_DROPPED`
- A cerver that fails to bind, to listen or to set its socket options closes its socket once: the teardown closed the
  same fd number again, that could belong to another connection by then
- `connection_send_packet ()` deletes a packet that it can not queue, like with a connection without a send queue
- `file_read ()` sets the size to the bytes that it read, it set the size of the file, and it takes a `NULL` size
- `c_string_concat_safe ()` takes a `char *` destination, it was `const char *`
- A cerver that failed to start is released with `cerver_teardown ()`: the examples, the web examples and the web
  integration servers called the private `cerver_delete ()`
- A removed UDP peer is out of the stats of the cerver before `cerver_udp_peers_count ()` stops counting it (the count
  is released and acquired): the count dropped first, and a thread that waited for it could read the old stats
- New: `examples/start.c`, the echo cerver and client of the README, `CLIENT_ARGUMENTS` in `test/pair.sh` and
  `uninstall.sh`; removed `.github/workflows/docs.yml`, `start.md` and `build.md`, the documentation is the wiki

## 2.0b-69 (from 2.0b-68)

The style of cerver in the vendored JWT and JSON. The exported symbols of `libcerver.so` do not change.

- Renamed fields (API and ABI): `jwt_t.key_len` is `key_length`; `jwt_valid_t.hdr` is `headers` and
  `jwt_valid_t.req_grants` is `required_grants`
- Removed: the `jwt_malloc_t`, `jwt_realloc_t` and `jwt_free_t` types (nothing could set them, there was no
  `jwt_set_alloc ()`), `jwt/config.h`, and the configuration macros of Jansson `JSON_HAVE_LOCALECONV`,
  `JSON_HAVE_ATOMIC_BUILTINS`, `JSON_HAVE_SYNC_BUILTINS` and `JANSSON_THREAD_SAFE_REFCOUNT` (the reference counts are
  always atomic); `LOOP_KEY_LEN` is `JSON_LOOP_KEY_LENGTH` (private)
- The strings of `jwt_dump_str ()`, `jwt_encode_str ()` and the other ones of the JWT are allocated with `malloc ()`:
  `free ()` or `jwt_free_str ()` releases them

## 2.0b-68 (from 2.0b-67)

The hot objects of HTTP are reused between requests. The API and the wire do not change.

- Changed fields (ABI): `HttpResponse` has `header_buffer` (2^8 bytes, where its header is written when it fits) and
  `recycle_next`; `response->header` can point to `header_buffer`, so it is never released with `free ()` by the
  caller: `http_response_set_header ()` and `http_response_delete ()` do it
- `http_response_delete ()` keeps the response in a cache of its thread (and a shared list) to be reused,
  `http_response_new ()` takes it from there: a response is never used after it was deleted
- The body of a request (`http_request_get_body ()`) is reserved in a buffer of a power of 2 of at least 2^8 bytes
  (`max_len` is that size), and its `String` lives in the arena of the request: it is never released by the handler
- The stats of the uploads of a request live in the arena of its thread
- A request without uploads or a body does not allocate (GET 1 → 0); a POST with a JSON body only allocates what its
  handler parses (19 → 14 in `benchmarks/server.c`)
- New: `http/recycle.h` (private)

## 2.0b-67 (from 2.0b-66)

The security of JWT and JSON. The wire of HTTP does not change, but tokens that a 2.0b-66 accepted can be rejected.

**JWT:**

- The keys are loaded when the cerver starts as a `JwtKey` (`jwt/key.h`, new): a key that does not match the
  algorithm stops the cerver: an HS secret shorter than its hash or that is a PEM, an RSA key of less than 2048 bits,
  an ES key of another curve (ES256 is P-256, ES384 P-384, ES512 P-521), an empty key or an encrypted PEM
- `http_cerver_auth_set_jwt_algorithm ()` returns `u8` and does not accept `JWT_ALG_NONE`
- Changed fields (ABI): `HttpCerver` (`jwt_private_key` and `jwt_public_key` are `JwtKey *`; new `jwt_max_size`,
  `jwt_require_expiration`, `jwt_leeway_seconds`, `jwt_issuer`, `jwt_audience`), `HttpJwtValue` (`value_int` is
  `int64_t`, its type `CERVER_TYPE_INT64`), `HttpJwt` (`HTTP_JWT_BEARER_SIZE` is 2^13 and `HTTP_JWT_TOKEN_SIZE` 2^14),
  `jwt_t` (`key_len` is `size_t`); `http_cerver_auth_jwt_add_value_int ()` takes an `int64_t`; `jwt_decode ()` and
  `jwt_set_alg ()` take a `size_t` length; `http_cerver_auth_generate_jwt_actual ()` and
  `http_cerver_auth_generate_bearer_jwt_actual ()` take a `JwtKey`
- A bearer token is verified with the algorithm of the cerver (`jwt_decode_with_key ()`, new): the header must name
  it exactly, as RFC 7518 writes it; tokens longer than `jwt_max_size` (2^13 by default) are rejected; an `exp` or a
  `nbf` that is not an integer is rejected
- Every token is parsed strictly: its three parts in canonical base64url (without `=`, `+` or `/`), the header and
  the payload json objects without repeated keys; `jwt_str_alg ()` compares as it is (`"hs256"` and `"None"` are
  invalid) and an unsigned token is written with `"alg":"none"`; a raw HMAC key that is a PEM is not used to verify
- An HMAC signature is compared in constant time; an ES signature is created with a low S
- `jwt_add_grant* ()` and `jwt_add_header* ()` never replace a key of any type; `jwt_del_grant ()` (declared but never
  defined) was removed; `jwt/openssl.h` was removed (replaced by `jwt/key.h`)
- A bearer or a json of `http_cerver_auth_generate_bearer_jwt* ()` that does not fit is an error, it was truncated
- A key or a text value of `http_cerver_auth_jwt_add_value* ()` longer than 127 bytes is cut at a complete UTF-8
  character (with a warning in the log): cut in the middle of one, the token was generated without that value (a long
  username with accents had no `username` claim)
- New: `http_cerver_auth_set_jwt_max_size ()`, `_require_expiration ()`, `_leeway_seconds ()`, `_issuer ()`,
  `_audience ()`; `base64url_encode ()` and `base64url_decode ()` in `utils/base64.h`

**JSON:**

- The seed of the hash of the objects is random (`getentropy ()`), it was the second the first object was created
- `http_response_json_*` escape their keys and values (a message with quotes was able to add keys), write the reals
  with their shortest exact form (`1.5`, before `1.500000`), and a `NaN` or a message that is not UTF-8 gives no
  response; the responses own their body
- A parse can allocate `JSON_PARSER_DEFAULT_MEMORY_FACTOR` (64) bytes of values for each byte it reads, plus
  `JSON_PARSER_MEMORY_MINIMUM`: a document that needs more (`[{}, {}, ...]` takes about 67) fails with
  `json_error_out_of_memory`; `json_set_parser_memory_factor ()` (new) changes it, 0 has no limit
- The numbers are read and written without the locale of the process; an underflow is accepted (`1e-400` is 0); an
  `i` of `json_unpack ()` that does not fit in an `int` is an error; a tree deeper than `JSON_DUMP_MAX_DEPTH` is not
  dumped; `json_dumpfd ()` works (it always failed); an invalid UTF-8 byte is `json_error_invalid_utf8`
- `hashtable_*`, `utf8_*`, `jsonp_*` and the other internal methods of the json are not exported by `libcerver.so`
  (`CERVER_HIDDEN`); the `json_*` methods keep the names of Jansson, so a process can not load cerver and the
  libjansson of the system at the same time

**General:**

- `cerver/config.h` includes `<stdarg.h>` and no longer defines a `va_copy` with `memcpy ()`, which broke every
  `json_pack ()` and `json_unpack ()` of a clang build in x86-64

## 2.0b-66 (from 2.0b-65)

The port of the core of the 1.8 (the 1.8b-7, 1.8b-8 and 1.8b-9 betas and the fixes of the release): the core of
`http` is the one of `core`. The wire of the protocol of cerver changes: a 2.0b-66 does not talk with a 2.0b-65
through TCP or UDP packets, its clients, services and balancers are updated together. HTTP does not change in the
wire.

**The protocol (1.8b-7):**

- The `PacketHeader` travels in `PACKET_HEADER_SIZE` (32) bytes in network byte order (`packet_header_encode ()`,
  `packet_header_decode ()`), it was sent as it is in memory (40 bytes in LP64); a header with another
  `PACKET_HEADER_FORMAT`, unknown flags or reserved bytes that are not zero closes the connection (a datagram is
  dropped)
- Changed fields (ABI): `PacketHeader` (`packet_size` is `u64`, `sock_fd` is the `u32` `route_id`, new `flags`),
  `Packet` (new `encoded_header`, `data_class` and `recycle_next`), `ReceiveHandle` (`header` is the bytes of a split
  header); `sizeof (PacketHeader)` is not the size of the header in the wire
- The balancer routes with a `route_id` of a slot and a generation (`packet_set_route_id ()`,
  `balancer_connection_start ()` returns it, `balancer_connection_end ()` takes it), the response for a connection
  that ended is discarded; `packet_set_header_values ()` takes a `u32 route_id`
- `serializer.h` replaces `SRelativePtr`, `SArray` and the `s_*` functions: `CerverSummary` replaces `SCerver`,
  `SessionToken` replaces `SToken` (sent with `AUTH_PACKET_TYPE_CLIENT_TOKEN` and `AUTH_PACKET_TYPE_ADMIN_TOKEN`),
  `ErrorMessage` replaces `SError`, `FileHeader` is an object (`len` is `u64`), `SStringSize`, `SStringS/M/L/XL` and
  `str_selialize ()` were removed; new `cerver_set_auth_schema ()`, `connection_set_auth_object ()`,
  `connection_set_auth_token ()`
- `Cerver.isRunning` is `running`; an auth packet in an authenticated connection is answered with an error; a
  connection that no handler could take is dropped with its client

**The issues of the 1.x (1.8b-8):**

- Changed fields (ABI): `Cerver` (`current_n_fds` is `u32`; new `poll_max_connections`, `request_stats`,
  `request_handlers`, `request_handlers_size`), `Handler` (new `stats`), `Balancer` (new `select_service`,
  `select_service_args`)
- The packets and their buffers of up to 4096 bytes are recycled: a `Packet` and its `data` are released only with
  `packet_delete ()`; `packet_set_data ()` makes the packet the owner of its new data
- `file_move_to ()` uses `rename ()` (or copies and removes between file systems) and returns 0 or 1; a client that
  the main poll can not take is released by the caller; `cerver_set_poll_fds ()`, `cerver_set_on_hold_poll_fds ()`
  and `cerver_set_request_handler ()` are new; a `WorkerManager` keeps its idle workers while jobs wait
- `BALANCER_TYPE_MAP` has `CUSTOM` (`balancer_set_select_service ()`)
- Renamed defaults: `CERVER_DEFAULT_SOCKET_RECV_TIMEOUT` is `CERVER_DEFAULT_SOCKET_RECEIVE_TIMEOUT` (in `cerver.h`),
  `*_UPDATE_INTERVAL_SECS` end in `_SECONDS`, the defaults of game start with `GAME_`, `DEFAULT_SCORE_SIZE` is
  `SCORE_DEFAULT_SIZE`
- New: `utils/histogram.h`, `HandlerStats`, `RequestStats`, the values of `system.h`, `files_copy ()`,
  `files_copy_recursive ()`, `files_remove_recursive ()`

**Game, zero warnings and the handler types (1.8b-9):**

- The game requests and answers are serializer objects (`LobbyCreate`, `LobbyJoin`, `LobbyRequest`, `LobbySummary`);
  `SLobby`, `SGameSettings` and `lobby_serialize ()` were removed; a lobby is released by its references and its data
  is read with the recursive lock of the game cerver
- Changed fields (ABI): `Lobby`, `GameCerver`, `GameType`, `Player`, `Connection` (new `lobby`), `CerverEventData`
  (new `lobby`), `ClientEventData` (new `owns_response_data`); new map values in the events and errors of the cerver
  and of the client (`*_DESTROY_LOBBY`, `*_LOBBY_UPDATE`, `*_LOBBY_DESTROY`, `*_GAME_INIT`), so their `UNKNOWN` values
  moved
- The `action_args` of a `ClientErrorData` are the arguments of `client_error_register ()`
- A signal that interrupts the receive of a connection (`EINTR`) does not end it
- A cerver of the protocol of cerver creates its thpool only with `CERVER_HANDLER_TYPE_THREADS`; a web cerver creates
  it with any handler type, because it handles each request in it
- `cerver_log* ()`, `str_create ()`, `str_set* ()`, `str_replace ()`, `c_string_create ()`, `thread_set_name ()` and
  the HTTP functions with a format (`http_cerver_generate_uploads_path ()`, `http_multi_part_set_generated_filename ()`,
  `http_multi_part_move_file_generate_destination ()`, `http_request_set_dirname ()`,
  `http_response_send_file_generate ()`, `http_response_handle_video_generate ()`,
  `http_response_add_custom_header ()`, `jsonp_error_set ()` and `jsonp_error_vset ()`) are checked by the compiler as
  `printf` formats (`CERVER_PRINTF_FORMAT`)
- `CERVER_ZERO_INITIALIZER` initializes a struct to zero both in C and in C++; the makefile builds with g++ without
  `-fpermissive`, `WERROR=1` makes every warning an error (the CI uses it), and the tests, the examples, the fuzzers and
  the benchmarks build with the warnings of the library
- `jwt/openssl.c` reads the degree of an EC key with `EVP_PKEY_bits ()`, without the `EC_KEY` API that OpenSSL 3
  deprecated

**The sends (release 1.8 and this beta):**

- The sends of the core end with their deadline also in a blocking socket (`MSG_DONTWAIT` and `poll ()`, and
  `sendfile ()` waits in `poll ()` and sends only what fits in the send buffer, with its `SO_SNDTIMEO` limited to
  the deadline); new `socket_deadline_create_milliseconds ()`,
  `socket_send_vector_until ()`, `socket_send_buffer_until ()` and `socket_send_file_until ()`
- All the sends of an HTTP response share one deadline (`HttpReceive.send_deadline`, set by its first send): a client
  that stops reading costs the send timeout in total, it used to cost it in each `sendmsg ()` and `sendfile ()`; each
  piece of a stream has its own deadline. `http_cerver_set_send_timeout_milliseconds ()` sets it in milliseconds
  (new `HttpCerver.send_timeout`), 0 uses the send timeout of the cerver in secs
- `files_create_recursive_dir ()` returns 0 for a path that already is a directory (also when another thread creates
  it at the same time), and 1 for an empty path, `NULL` or one that does not fit in `DIRNAME_DEFAULT_SIZE`

**HTTP:**

- The files of a static path with authentication (`http_static_path_set_auth ()`) are authenticated before they are
  looked for, with the static path that has the file or the first protected one when none has it, so a missing file
  is answered with 401 like a protected one; the authentication of a static path was never applied, its files were
  served to anyone. New fields (ABI): `HttpStaticPath` (`decode_data`, `delete_decoded_data`,
  `authentication_handler`), set with `http_static_path_set_decode_data ()` and
  `http_static_path_set_authentication_handler ()`
- New fields (ABI): `HttpReceive` (`send_deadline`, `send_deadline_set`)
- The logs of the requests write the status with `%u` and the sizes with `%zu`

## 2.0b-65 (from 2.0b-64)

The clients that leave in every part of the HTTP process. The port of the 1.8b-7 waits for a later beta.

- New fields (ABI): `HttpCerver` (`idle_timeout`, `half_close_is_gone`, `n_aborted_requests`, `n_vanished_requests`,
  `n_failed_responses`), `HttpReceive` (`upload_directory_created`, `stage`, `abort_reason`, `response_failed`,
  `response_error`, `client_state`)
- `HttpCerver.headers_timeout` and `HttpCerver.request_timeout` are in milliseconds (they were secs);
  `http_cerver_set_headers_timeout ()` and `http_cerver_set_request_timeout ()` still take secs, the new
  `*_milliseconds ()` setters take milliseconds, and `http_cerver_set_idle_timeout_milliseconds ()` replaces the fixed
  `CERVER_DEFAULT_SOCKET_RECV_TIMEOUT` (5 secs, still the default) of the HTTP receive
- `n_incompleted_requests` (`cerver_http_requests_incompleted_total`) only counts the requests that http_parser could
  not parse: a client that left is counted in `n_aborted_requests` (`cerver_http_aborted_requests_total{stage,reason}`),
  one that stopped sending until a timeout ended it in `n_vanished_requests`
  (`cerver_http_vanished_requests_total{stage}`), and a 408 is no longer counted as incompleted
- A response that could not be sent is `HTTP_RECEIVE_STATUS_RESPONSE_FAILED` (it was `COMPLETED` with status 0), is
  counted in `n_failed_responses` (`cerver_http_failed_responses_total`) and where its client left, it is not counted
  in `n_responses_by_status`, and its route counts it with the status that it tried to send
- `HttpStreamState` has `HTTP_STREAM_STATE_FAILED`: a piece that fails ends the stream, the next ones fail without
  being sent and cerver does not send the last chunk; a stream whose header failed is `FAILED` (it was `ENDED`)
- The files and the generated directory of a request that was not completed are always removed, even if
  `uploads_delete_when_done` is false (it only applies to the completed requests); a generated directory that ends
  empty is removed
- A filename generator that does not set a name saves the file with its sanitized filename, a path that does not fit
  in `HTTP_MULTI_PART_SAVED_FILENAME_SIZE` and a directory that can not be created are answered with 500
  (`UPLOAD_ERROR`); a part that could not open its file never removes a file with its name
- A `100 Continue` that can not be sent ends the request as a client that left, without a response
- New: `http_receive_get_stage ()`, `http_receive_get_client_state ()`, `http_receive_is_client_gone ()`,
  `http_cerver_set_half_close_is_gone ()`, `HttpStage`, `HttpAbortReason`, `HttpClientState` and their strings
- The admin stats JSON and `http_cerver_all_stats_print ()` have every counter of the requests and the responses

## 2.0b-64 (from 2.0b-63)

The port of the core of the 1.8b-4 and the 1.8b-5, so the entries of `incompatibilities.md` in `core` for those betas
also apply here, and the changes of the core that `core` gets after it as the 1.8b-6. The ones that change the most:

- UDP cervers and clients (`udp.h`), with an auth token in every datagram, sessions and peers with references
- New fields (ABI): `Packet` (`release`, `release_args`), `Connection` (`shut_down`, `datagram_token`,
  `datagram_token_set`, `connect_timeout`), `Cerver` (`udp`, `udp_peer_timeout`, `udp_max_peers`, `udp_peers_count`,
  `handlers_alive_cond`), `Worker`; `Pool` and `JobQueue` changed completely
- `packet_delete ()` calls the `release` of the packet, a packet that is filled by hand must have it as NULL
- `MAX_UDP_PACKET_SIZE` is 65507, the default max received packet size of a cerver and of a client
- `Pool` is a stack on an array without a `dlist`, the last element pushed is the first one popped, and it can be
  limited with `pool_set_max_elements ()`; the pools of responses, multiparts and JWTs no longer allocate a node in
  each push
- `JobQueue` has priorities, timeouts and stats, without `queue`, `rwmutex` nor the `bsem` `has_jobs`; workers are
  joinable and wait for their states in a condition (`WORKER_SLEEP_TIME` was removed); `WorkerManager` is new
- A client tries each address that its host resolves to, with `connection_set_connect_timeout ()`, and resolves it
  again when it reconnects
- `network_parse_address ()` takes a zone in a link local address, and `sock_ip_to_string_actual ()` writes it, with a
  buffer of `NETWORK_ADDRESS_STRING_SIZE` bytes (it was `INET6_ADDRSTRLEN`)
- `HTTP_CLIENT_IP_SIZE` and `HTTP_REQUEST_CLIENT_IP_SIZE` are `NETWORK_ADDRESS_STRING_SIZE`, so the client ip of a
  request keeps the zone of a link local address, and a trusted proxy with a zone (`fe80::1%eth0`) is matched without
  its zone
- `sock_ip_equal ()` takes an IPv4 mapped address as equal to the IPv4
- The ready log of a cerver has its address, and `cerver_log_end ()` joins the thread that syncs the log file
- New field (ABI): `Socket` (`close_mutex`); the fd of a connection is closed with `socket_close ()`, replaced with
  `socket_replace ()` and shut down from other threads with `socket_shutdown ()`, all with that mutex, so a shutdown
  never reaches an fd that was closed and given to another connection; `socket_close ()` shuts the fd down and waits
  for a send of another thread (the write mutex) before it closes it, so it must not be called with the write mutex
- The check of the inactive clients shuts their sockets down after it releases the clients lock, with a reference to
  each socket
- `connection_connect ()` gives a connection without its mutex or its condition the ones that it has embedded, so its
  backoff can always be interrupted: the `sleep ()` of a connection without synchronization was removed
- A client connection whose connect failed, or that never connected, closes its socket: the failed connect closes it
  and `client_connection_end ()` closes it too (before, the fd stayed open after the teardown of the client)

## 2.0b-63 (from 2.0b-62)

The port of the core of the 1.8 (from 1.8b-1 to 1.8b-3), so the entries of `incompatibilities.md` in `core` for those
betas also apply here. The ones that change the most:

- `CERVER_TYPE_BALANCER` and the balancer (`balancer.h`) are part of the library, with the 1.x client features: async
  connect, connection states, send queue and reconnection
- New fields (ABI): `Cerver` (`bind_address`, `ipv6_only`, `balancer`, `connection_threads`, `event_threads`),
  `Client`, `Connection`, `Socket`, `CerverReceive`, `Handler` (`joinable`), `Thpool` (`stats`), `ThpoolJob`
  (`queued`), `ClientStats` and `ConnectionStats` (the storages of their packets per type)
- Connections, sockets, receives and clients are recycled: a pointer to one of them is never valid after it was
  deleted; their stats, mutexes and conds live inside them and must not be freed nor replaced
- Every send of the core uses `MSG_NOSIGNAL` and waits on a full non blocking socket up to the send timeout;
  `packet_send_split_tcp`, `packet_send_to_socket`, `file_send` and `file_send_by_fd` report the bytes that were really
  sent and fail when the data was not sent completely
- The HTTP responses are sent with `socket_send_vector ()`: `HTTP_RESPONSE_MILLISECONDS_PER_SECOND` and
  `HTTP_RESPONSE_NANOSECONDS_PER_MILLISECOND` were removed from `response.h`
- An IPv4 mapped address (`::ffff:a.b.c.d`) is written as `a.b.c.d`, and an IPv6 cerver always sets `IPV6_V6ONLY`
  (by default `false`); a client resolves its host with `getaddrinfo ()` and fails instead of connecting to
  `in6addr_any`
- The handlers threads are joinable and the teardowns join them, and they wait for the event and error actions that
  run in their own threads and for the detachable connection threads; a drain that times out waits
  `CERVER_DRAIN_GRACE_TIMEOUT` more seconds
- `client->session_id` is read with `client_get_session_id ()` from other threads
- A `FileHeader` whose filename is not a single NUL terminated name gets the "Missing or invalid file header" error
- The packets that no handler takes are released, and `client_connection_get_next_packet ()` accepts packets up to
  the client's max received packet size
- `cerver_deserialize ()` takes a `const SCerver *`
- `/cerver/metrics` has the thpool counters (`cerver_thpool_jobs_total`, `_jobs_completed_total`, `_rejected_total`,
  `_dropped_total`, `_waited_total`, `_queued_max`) and histograms (`cerver_thpool_wait_seconds`, `_run_seconds`)
- The workflows run in `container: gcc:16.2`, and `nightly.yml` is now `scheduled.yml`
