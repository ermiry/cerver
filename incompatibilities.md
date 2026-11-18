# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

## 2.1b-24 (from 2.1b-23)

The remote authentication (F7 of `roadmap.md` §4.7): a custom authentication can ask another service by HTTP with
`http_auth_request ()` (`cerver/http/security/remote.h`). A cerver whose authentications do not call it behaves as
before; no public struct changes its size.

**The build**

- `CURL` of the `makefile` is now the switch of libcurl (`CURL=1`, 0 by default) and its flags are `CURLLIB`: a build
  that passed its own `CURL=-l curl` passes `CURLLIB` instead.
- With `CURL=1`, `libcerver.so` links libcurl: the images of cerver (every `Dockerfile`) and the CI build with it, so
  an image that runs them needs the runtime of libcurl (`libcurl4-openssl-dev` in the images of cerver).
- Without `CURL=1`, `http_auth_request ()` exists and returns 1: an authentication that calls it rejects its
  requests.

**The authentication handler**

- A handler of `HTTP_ROUTE_AUTH_TYPE_CUSTOM` can return `HTTP_AUTHENTICATION_PENDING` (2), what
  `http_auth_request ()` returns with an event loop; any other value that is not 0 is still a failure (401). A
  handler that returned 2 before this version without calling `http_auth_request ()` is still a failure.
- With an event loop, the request of an authentication before the body or of a request without one waits in the loop
  without a thread, and its `on_response ()` runs in a thread of the pool of the endpoint, maybe another one than the
  thread of the authentication.

## 2.1b-23 (from 2.1b-22)

TLS in the event loop (F8b of `roadmap.md` §4.8.9): a web cerver with `CERVER_HANDLER_TYPE_EVENTS` can use TLS. A
cerver without TLS, and one with TLS and threads, behave as before; no public struct changes its size.

**A cerver with an event loop and TLS**

- `cerver_set_tls ()` takes its settings and returns 0 (it returned 1 and the cerver had no TLS), and
  `cerver_start ()` starts it.
- The loop runs each handshake without a thread: a client that is slow or silent in its handshake only holds its
  place in the poller until the deadline of the handshake (`tls_settings_set_handshake_timeout_milliseconds ()`,
  2^13 ms), counted as `TLS_FAILURE_TIMEOUT`. The clock of the first request (its headers and request timeouts)
  starts when the handshake ends.
- At its limit of connections the cerver stops accepting, as without TLS: `TLS_FAILURE_REJECTED` only counts the
  connections that a full cerver with threads closes.
- The loop sends the `close_notify` of a connection that it closes without waiting for its socket: a socket that
  does not take it now (or, with kTLS, one with records that still wait) is closed without it. A handler in the pool
  and a cerver with threads still wait for it up to 2^8 ms.

**HTTP**

- `HttpRouteSettings.client_certificate` starts as `HTTP_CLIENT_CERTIFICATE_NONE` in every group and endpoint: in the
  2.1b-22 it was not initialized, and an endpoint could answer 403 at random to a request without a certificate
  (most often the upgrade of a WebSocket).

## 2.1b-22 (from 2.1b-21)

TLS over threads and in the clients (F8a of `roadmap.md` §4.8): every TCP cerver (`CUSTOM`, `GAME`, `WEB`, `FILES`,
`BALANCER`) and every client (TCP, `wss://`, the balancer towards its services) can use TLS 1.2 and 1.3 through the
provider of the cryptography (OpenSSL 3). A cerver or a client without TLS behaves as before; the structs below change
their size, so a binary built with the 2.1b-21 is built again.

**The structs**

- `Socket` has `tls`, `Cerver` has `tls`, `Client` has `tls` and `HttpRequest` has `tls` (an `HttpRequestTls` after
  its `method`); `HttpRouteSettings` has `client_certificate` and `HttpWebSocketSettings` has `loop_callbacks`.
- The tables of the X-macros have new rows: `HTTP_STATUS_TOO_EARLY` (425), `HTTP_ROUTE_AUTH_TYPE_CERTIFICATE` (3) and
  `HTTP_WEB_SOCKET_ERROR_TLS` (9), and a code that switches over them without a `default` gets a warning.

**A cerver with TLS**

- `cerver_set_tls ()` takes its `TlsSettings` (also when it fails, a cerver with `CERVER_HANDLER_TYPE_EVENTS` can not
  have TLS until F8b). `cerver_start ()` reads and validates the certificates and the keys, and it fails with a
  certificate that expired or is not valid yet, a key that does not match, a chain out of order, a name in two
  certificates, a cipher or a group that is not known.
- It only listens TLS on its port: a plain client is closed when the deadline of its handshake ends (2^13 ms).
- **A cerver that is not a web one runs each handshake in the pool of its TLS** (`tls_settings_set_handshake_threads ()`,
  2^2 threads), and its info packet, `CERVER_EVENT_CLIENT_CONNECTED` and the registration of the connection come after
  it, in that thread instead of the one that accepts.
- **A web cerver runs the handshake in the thread of its thpool that receives**, and when it is full a connection is
  closed without its handshake (instead of its 503) and counted as `TLS_FAILURE_REJECTED`.
- The close of a socket with TLS sends its `close_notify` first and waits for it up to 2^8 ms; a cerver that closes a
  connection after its response sends it before its `shutdown (SHUT_WR)`.
- With a reload signal (`cerver_tls_set_reload_signal ()`) the threads that cerver creates block that signal too, and
  cerver sets its handler with `sigaction ()`.

**HTTP**

- An endpoint or a group with `HTTP_CLIENT_CERTIFICATE_REQUIRED` answers 403 to a request without a certificate that
  verified; `HTTP_ROUTE_AUTH_TYPE_CERTIFICATE` answers 401 without one.
- With early data (`tls_settings_set_early_data ()`, off by default) a request of the early data that is not a GET, a
  HEAD or an OPTIONS without a body is answered with 425.
- The `100 Continue` of a request that a thread reads goes through `socket_send_buffer ()` (a send that waits with
  `poll ()` until the send timeout) instead of a blocking `send ()`.

**WebSocket**

- `http_web_socket_client_set_url ()` takes `wss://` (it returned 1), on 443 without a port, and the client connects
  with TLS: with the settings of `http_web_socket_client_set_tls ()` or the default ones (the store of the system). A
  handshake that fails is `HTTP_WEB_SOCKET_ERROR_TLS`.
- With TLS the queue of a connection counts the encrypted bytes that the socket did not take: its limit, its
  backpressure and `http_web_socket_get_buffered_size ()` see them.

**The clients**

- `client_set_tls ()`: every TCP connection of the client runs its handshake in `connection_connect ()`, and a
  handshake that fails fails the connect. The certificate of the cerver is always verified (the store of the system or
  `tls_settings_set_ca ()`), with the host of the connection (or `tls_settings_set_server_name ()`) in its SAN.

## 2.1b-21 (from 2.1b-20)

The WebSocket connections over the event loop (F6b of `roadmap.md` §4.6), their bounded queue and the API of the
backlog §31.2. The renames are direct, without aliases: code that uses the old names does not compile, and every
struct of the WebSocket connections and of the parser changes its size (a binary built with the 2.1b-20 is built
again).

**The connections and their queue (F6b, A5)**

- **With `CERVER_HANDLER_TYPE_EVENTS` a connection has no thread of its own:** its loop reads and parses its frames,
  answers its pongs and its closing handshake, and writes its queue; its methods run in a job of the pool of its
  endpoint, one job at a time for each connection and in order (the loop does not read it while one runs). A method
  that blocks holds a thread of the pool, not of the connection. With `CERVER_HANDLER_TYPE_THREADS` each connection
  keeps its thread
- **A send never waits:** it takes the frame in the queue of its connection (or writes it at once when nothing waits
  before it) and the owner of the connection writes the rest. The sends return `HttpWebSocketSendResult` (they
  returned `u8`, 0 when it was sent): `HTTP_WEB_SOCKET_SEND_QUEUED` and `BACKPRESSURE` are taken, `DROPPED`,
  `CLOSED` and `FAILED` are not; `http_web_socket_send_result_is_taken ()` says which. This is the result of
  `http_web_socket_send ()`, `_send_text ()`, `_send_binary ()`, `_send_begin ()`, `_send_write ()`, `_send_end ()`
  and `http_web_socket_client_send ()`, `_send_text ()`, `_send_binary ()`; a code that checks `!send (...)` sees
  `BACKPRESSURE` (1) as a failure
- **The queue is bounded:** 2^20 bytes by default (`HTTP_WEB_SOCKET_DEFAULT_MAX_QUEUE`), with
  `http_web_socket_settings_set_queue ()` (inherited from the groups) or `http_cerver_set_web_socket_queue ()`. A
  message that does not fit is dropped (`HTTP_WEB_SOCKET_OVERFLOW_DROP`, the default) or closes the connection with
  1008 (`HTTP_WEB_SOCKET_OVERFLOW_CLOSE`); a message larger than the queue fits only when the queue is empty; the
  control frames and the pieces of a stream are always taken. Over half of the queue a send returns
  `BACKPRESSURE`, and `on_drain ()` runs once the queue is empty again
- **`HttpWebSocketHandlers` has `on_drain` at its end:** a positional initializer of the 2.1b-20 still compiles (the
  field is NULL), but `-Wmissing-field-initializers` warns about it
- **The send timeout is the time that a queue can wait without writing a byte:** then the connection fails with 1006
  and `HTTP_WEB_SOCKET_ERROR_SEND` (a send waited up to it); it is the same in both models
- **The broadcast timeout is removed:** `http_cerver_set_web_socket_broadcast_timeout_milliseconds ()` and
  `HTTP_WEB_SOCKET_DEFAULT_BROADCAST_TIMEOUT`; a broadcast takes the message in the queue of each connection, with its
  policy and its compression, and returns the connections that took it
- **A connection is registered after its `on_open ()`** (S7): a broadcast, a count or a for each does not reach it
  while it runs (a broadcast of its own `on_open ()` reaches the others, not it); its own sends and its joins work,
  and its rooms reach it once it is registered
- **The ids are unique in the process** (A9), for the servers and the clients: a connection of a second cerver does
  not start again from 1
- **`HttpWebSocketStats` has `n_dropped`, `n_drains` and `n_queued_bytes`** after `n_slow`; the metrics
  `cerver_websocket_dropped_messages_total`, `cerver_websocket_drains_total` and `cerver_websocket_queued_bytes`, and
  `n_dropped`, `n_drains` and `queued_bytes` in the `websockets` of the admin stats
- **A connection that ended releases zlib, the buffer of its messages and its empty queue** after its `on_close ()`
  (W7 / S10), not with its last reference; `http_web_socket_is_compressed ()` still says what was negotiated

**The rooms and the broadcasts (A14)**

- **`HttpWebSocketTarget {room, endpoint, except}`** selects the connections of `http_cerver_web_socket_count ()`,
  `http_cerver_web_socket_broadcast ()` and `http_cerver_web_socket_for_each ()` (they took a room, and the broadcast
  the connection that does not receive it); NULL is every connection. **Removed:**
  `http_endpoint_web_socket_broadcast ()`, the target with its endpoint
- **The rooms are an index of the registry** without the limit of 16 rooms of 63 bytes: **removed**
  `HTTP_WEB_SOCKET_MAX_ROOMS` and `HTTP_WEB_SOCKET_ROOM_NAME_SIZE`; a broadcast to a room only goes through its
  members
- **`http_web_socket_join ()` returns `HttpWebSocketJoin`** (`HTTP_WEB_SOCKET_JOIN_MAP`: JOINED, ALREADY, INVALID,
  UNREGISTERED, FAILED; it returned 0 for both of the first ones); an empty name is INVALID
- **The rooms of a connection are kept while its `on_close ()` runs** and removed after it (they were removed before
  it); **new:** `http_web_socket_leave_all ()`, `http_web_socket_get_rooms ()`; `http_web_socket_in_room ()` takes a
  `const` connection
- **New:** `http_cerver_web_socket_get_with_id ()`, with a reference

**The settings as an object (A7, A13, A17)**

- **The settings of a WebSocket endpoint, group or client are set with `http_web_socket_settings_set_* ()`** on
  `http_endpoint_get_web_socket_settings ()`, `http_group_get_web_socket_settings ()` and
  `http_web_socket_client_get_settings ()`. **Removed:** `http_endpoint_set_web_socket_max_message ()`,
  `_protocols ()`, `_compression ()`, `_compression_options ()`, `_origin_check ()`, `_ping_milliseconds ()`,
  `_close_timeout_milliseconds ()`, `_send_timeout_milliseconds ()`, the same eight of `http_group_set_web_socket_*`,
  and `http_web_socket_client_set_protocols ()`, `_set_compression ()`, `_set_compression_options ()`,
  `_set_max_message ()`, `_set_ping_milliseconds ()`, `_set_close_timeout_milliseconds ()`,
  `_set_send_timeout_milliseconds ()`, `_set_timeouts_milliseconds ()`; **new:** the shortcut
  `http_web_socket_settings_enable_compression ()`
- **Renamed:** `http_endpoint_set_web_socket ()` → `http_endpoint_set_web_socket_handlers ()`,
  `http_group_set_web_socket ()` → `http_group_set_web_socket_handlers ()`
- **`origin_check` is `HttpWebSocketOriginCheck`** (`NONE`, `REQUIRED`, `IF_PRESENT`; it was a `bool`, true is
  `REQUIRED`): `IF_PRESENT` lets a request without an Origin through
- **The client:** `http_web_socket_client_set_data ()` takes the method that releases its data (it is released with
  the client or with the next data); the timeouts of the connect and of the handshake are set apart
  (`_set_connect_timeout_milliseconds ()`, `_set_handshake_timeout_milliseconds ()`);
  `http_web_socket_client_get_connections ()` → `http_web_socket_client_get_connections_count ()`; **new:**
  `http_web_socket_client_ping ()`, `http_web_socket_client_get_state ()`
- `HttpWebSocketSettings` has `max_queue` and `overflow`, and `HTTP_WEB_SOCKET_SETTING_QUEUE` marks them

**The names (A8, A18, A19, A21, A23)**

- **The strings of the enums are `*_to_string ()`:** `http_status_string ()`, `http_stage_string ()`,
  `http_abort_reason_string ()`, `http_client_state_string ()`, `http_header_string ()`,
  `http_range_type_string ()`, `http_trusted_proxies_string ()`, `http_parser_lenient_string ()`,
  `http_request_method_str ()` and `http_receive_status_str ()` are `*_to_string ()`; a value out of a table is
  "Undefined" in every one of them
- **The close codes are `HTTP_WEB_SOCKET_CLOSE_CODE_<name>`** of `HTTP_WEB_SOCKET_CLOSE_CODE_MAP` (they were
  `HTTP_WEB_SOCKET_CLOSE_<name>` of `HTTP_WEB_SOCKET_CLOSE_MAP`), and the size of the code of a close frame is
  `HTTP_WEB_SOCKET_CLOSE_FRAME_CODE_SIZE` (`HTTP_WEB_SOCKET_CLOSE_CODE_SIZE`)
- **The results of the receiver are `HTTP_WEB_SOCKET_RECEIVE_RESULT_<name>`** of
  `HTTP_WEB_SOCKET_RECEIVE_RESULT_MAP` (`HTTP_WEB_SOCKET_RECEIVE_<name>` of `HTTP_WEB_SOCKET_RECEIVE_MAP`)
- `http_web_socket_get_endpoint_data ()` → `http_web_socket_get_endpoint_custom_data ()`, that returns `const void *`
- **New:** `http_request_method_from_string ()`, `http_web_socket_role_to_string ()`,
  `http_web_socket_parser_state_to_string ()`, `http_web_socket_get_error ()`, `http_web_socket_get_buffered_size ()`,
  and the `_to_string ()` of each new map (`HttpWebSocketSendResult`, `HttpWebSocketOverflow`,
  `HttpWebSocketOriginCheck`, `HttpWebSocketJoin`, `HttpWebSocketInflateResult` and the result of the response of the
  client), with the `_description ()` of the overflow, the origin check and the join;
  `HTTP_WEB_SOCKET_REJECTIONS` is counted from its table

**The protocol (A20, W4, W11)**

- **A close without a payload:** `http_web_socket_close ()` takes `HTTP_WEB_SOCKET_CLOSE_WITHOUT_CODE` (0) without a
  reason, and returns the result of its send (1 when a close was already sent)
- **A peer that asks for a window of 2^8 is accepted** (W11): the server answers `server_max_window_bits=8` and the
  client takes `client_max_window_bits=8` (they were declined), and cerver compresses for it with 2^9, whose
  distances fit in 2^8
- **`http_web_socket_deflate_decompress ()` returns `HttpWebSocketInflateResult`** (`HTTP_WEB_SOCKET_INFLATE_MAP`:
  DONE, INVALID, STOPPED, FAILED; they were three defines with the same values and 0): after a stop it keeps what is
  left and gives it in the next call (W4), so a receiver that is stopped inside a compressed message loses nothing
  and gets the last piece of it; `HttpWebSocketDeflate` and `HttpWebSocketReceiver` have new fields

**The parser (A22 / V6)**

- **llhttp is not installed anymore:** `include/cerver/http/parser/api.h`, `helpers.h` and `llhttp.h` are in `src/`;
  `HttpReceive` keeps the parser and its settings in `HttpParserState` and `HttpParserSettings`, opaque bytes of a
  fixed size. `include/cerver/http/parser/parser.h` has `HTTP_PARSER_LENIENT_MAP` with the values of llhttp,
  `HttpParserLenient`, `HTTP_PARSER_LENIENT_ALL` and their strings; `HTTP_RECEIVE_PARSER_STOP` is internal; **new:**
  `http_cerver_get_parser_lenient ()`

## 2.1b-20 (from 2.1b-19)

The sync with the Release 1.9 of `core` and the 2.0b-77 of `http`: the wire only adds a header, but some structs grow
(a binary that allocates them itself is built again) and some functions change what they do.

- **A send that times out ends its connection:** a send to a peer that does not read in all its send timeout (or the
  deadline of an HTTP response) leaves it with a part of what was sent, so its socket is shut down: the thread that
  receives the connection ends it, and the next sends fail at once (EPIPE) instead of waiting each one for its own
  timeout. **New:** `socket_end_on_send_timeout ()` (`CERVER_PRIVATE`)
- **A response without its length says `Connection: close`:** its body ends with the close of the connection, that
  was already closed after it; now the response announces it (`keep_alive` of its `HttpReceive` is false)
- **The events and errors of a client share their arguments** like the ones of the cerver: `ClientEvent` and
  `ClientEventData` have `shared_args` and `shared_response` at their end, `ClientError` and `ClientErrorData` have
  `shared_args`; an event or an error that is replaced or unregistered while a trigger still runs keeps its arguments
  (and its response) until that trigger releases its data
- **`files_create_dir ()` of a directory that exists returns 0** (it returned 1); `file_n_read ()` sets `n_read` (it
  added to it) and takes a NULL one; `file_receive_actual ()` removes a file that fails after it was opened; a BMP is
  found by its bytes
- **The JSON rejects a NUL byte after a number or a literal:** `[1\0, 2]` and `{"a": 1\0}` were read as `[1, 2]` and
  `{"a": 1}`
- **`http_cerver_static_path_add ()` returns NULL** when its path, its characters or its place in the list can not
  be allocated (it returned a static path with a NULL path, that was dereferenced when it was resolved, or one that
  was not in the list)

## 2.1b-19 (from 2.1b-18)

The whole form of keep-alive (F4b of `roadmap.md` §4.4b) and the backlog of performance and of tests. Some structs
change their fields (a binary built with the 2.1b-18 is built again), and two fields of a request are not set when
it is parsed anymore.

**Keep-alive**

- **The threads of the pools read the next request of an idle connection:** with `CERVER_HANDLER_TYPE_EVENTS` and
  connections that are kept (the default), the threads of each pool wait for the sockets of the idle connections
  whose last request was of the pool when the queue of the pool is empty, and the one that gets a socket reads,
  parses and handles its request without the event loop. The loop reads it when no thread of its pool waits (in its
  idle check, every 2^8 ms at most), when requests of that pool wait in the loop or when the loop rejects them, so a
  request still waits for a place and is refused like any other. `http_cerver_set_keep_alive_pool_read (false)`
  gives back the reads of the 2.1b-18 (every request is read by its loop)
- **The authentication before the body of the next request of a kept connection runs in the thread that reads it**
  (or that follows its connection), when the request is of the pool of that thread: it went back to its loop, that
  gave it to the pool as another job
- **A thpool can wait on something else than its queue:** `Thpool` has `waiter`, `num_threads_waiting` and
  `waiter_wakes` at its end (`ThpoolWaiter`, `ThpoolWaited`, `thpool_set_waiter ()`,
  `thpool_get_num_threads_waiting ()`, `CERVER_PRIVATE`); a work of the waiter is counted in `total_jobs` and
  `completed_jobs` and in the run histogram, not in the wait one
- **The memory of a connection of an event loop is released with its loop:** one that its recycled connections do not
  keep (beyond 2^10) loses its arena and its buffers but keeps its struct, so a thread that still has its pointer
  reads that it is not idle
- `HttpCerver` has `keep_alive_pool_read` and `n_pool_read_requests` at its end; the metric
  `cerver_http_pool_read_requests_total` and `n_pool_read` in the `connections` of the admin stats count those
  requests

**HTTP**

- **The query params and the values of a form live in the arena of the request:** `HttpRequest::query_params` and
  `body_values` are NULL until `http_request_get_query_params ()` and `http_request_get_body_values ()` create them (a
  copy of each pair), so code that read the fields reads the getters; `http_request_query_params_get_value ()` and
  `http_request_body_get_value ()` read the pairs of the arena without allocating (`HttpRequest::query_pairs`,
  `n_query_pairs`, `body_pairs` and `n_body_pairs` at its end). A value of a form is parsed with the length of the
  body (it stopped at its first `'\0'`)
- `cerver_http_pool_parked_seconds{pool}`: the time that the requests of each pool waited in the event loops for a
  place (the struct of a pool has its histogram); `parked_microseconds` in the pools of the admin stats

**The event loops**

- **The pause of the accept is 2^4 ms** (`EVENT_LOOP_ACCEPT_BACKOFF`): without fds or at the limit of connections a
  loop waits that time in its poller, not a tick of its timers (2^8 ms); `EventLoop` has `accept_resume` instead of
  `accept_timer`
- `EventSource` has `poller`: modify and remove act where the source is registered, whatever poller is named

**The tests**

- The macros `test_check_ptr_eq`, `_ptr_ne`, `_unsigned_ne`, `_unsigned_gt`, `_int_ne` and `_int_gt` of `test/test.h`
  put their arguments in parentheses
- The send timeout of `test/http/disconnect.c` scales with `test_time_scale ()`
- The routing test of the balancer (`test/load/routing.c`) waits for the counter of the packets that it sent, that the
  balancer adds after its send returns (under valgrind the client had the last echo first)

## 2.1b-18 (from 2.1b-17)

The fixes of the audit of the code that the other lines made: the core of the 1.9b-2 (`core`), the HTTP of the
2.0b-75 (`http`) and the owner of a stream of WebSocket of the 3.0b-4 (`labs`). The wire of HTTP changes for the
videos (RFC 9110), some structs change their fields (a binary built with the 2.1b-17 is built again) and some
functions change what they do.

**The event loops**

- **The stop has one pipe:** `Cerver` has `stop_pipe[2]` instead of `stop_mutex`, `stop_cond` and the eventfd
  `stop_fd`; the stop writes a byte in it (async-signal-safe, it never takes a lock), the sleeps of the cerver poll
  it, and with `CERVER_HANDLER_TYPE_EVENTS` the thread in `cerver_start ()` waits on it too. A cerver whose pipe could
  not be created does not start its event loops
- **`make DEFINES="…"` keeps the provider of the cryptography:** the `-D CERVER_CRYPTO_OPENSSL` of `CRYPTO=openssl` is
  added with `override`, so a `DEFINES` from the command line (like the one of `Dockerfile.nightly`) does not lose it
  (it stopped with the `#error` of the provider)

**HTTP and WebSocket**

- **The videos follow RFC 9110:** `http_response_handle_video ()` answers a request without a Range (or with an
  invalid one, like `bytes=9-1`) with 200 and the whole video (it answered 206 with an end of -1), the last byte of
  `bytes=N-M` is included (it sent one byte less) and a range never goes after the end of the video, and a range that
  starts after its end gets 416 with a Content-Range of the size of the video (it got a 206)
- **A stream of WebSocket belongs to the thread that began it:** `http_web_socket_send_write ()` and
  `http_web_socket_send_end ()` from another thread return 1 and the stream goes on (the end unlocked the mutex of
  the owner)
- `http_endpoint_set_custom_data ()` and `http_group_set_custom_data ()` delete the custom data that they replace,
  when it is another pointer
- `http_response_get ()` is NULL without an HTTP cerver (it crashed), and `http_response_return ()` and
  `http_cerver_auth_jwt_delete ()` release what the pool does not take (they lost it); `http_response_compile ()`
  of a response without its header copies only its data
- `http_request_set_dirname ()` keeps a directory of `REQUEST_DIRNAME_SIZE - 1` characters whole (it cut its last
  one)
- The timeouts of the HTTP cerver do not warn when they are not a power of 2 (a request timeout of 300 s warned)
- A pair of a query or an attribute of a multipart header without memory is not added (they wrote in NULL)
- `thirdparty.md` has multipart-parser-c (MIT), the base of the parser of `http/requests/multipart.c`

**The core (the 1.9b-2 of `core`)**

- **The structs change:** `Cerver` has no `auth_packet` (it was not used); `CerverStats` has `threshold_start`;
  `JobQueue` has `requests` instead of `waiting`, `requested_id` and `requested_job` (each thread in
  `job_queue_request ()` has its own request); `CerverEvent`, `CerverEventData`, `CerverErrorEvent` and
  `CerverErrorEventData` have `shared_args` at their end
- **The admins have their limits:** `admin_cerver_register_admin ()` rejects an admin beyond `max_admins` or a client
  with more than `max_admin_connections`, and **the default `ADMIN_CERVER_DEFAULT_MAX_ADMINS` is 1**: a second admin
  is rejected unless `admin_cerver_set_max_admins ()` raises the limit (0 is no limit). The admin cerver checks the
  packets of its admins with its own `admin_cerver_set_check_packets ()`, not with the one of the cerver
- **Replacing frees:** `cerver_set_app_handlers ()`, `cerver_set_custom_handler ()`, the ones of the admin and of the
  client and `connection_set_received_data ()` delete the handler (or the data) that they replace, when it is another
  pointer; a program that kept the old one to delete it later deletes it twice
- **The upload without an uploads path is rejected:** a files cerver without `file_cerver_set_uploads_path ()` sends
  `CERVER_ERROR_SEND_FILE` and ends the connection (it read the bytes of the file as packets)
- `client_unregister_connections_from_cerver_poll ()` does not drop the client, the caller does; `client_request_to_cerver ()`
  returns the result of the response (it always returned 0 after the send); `file_send ()` and `client_file_send ()`
  take a name without a `/` (they rejected it)
- **`str_set ()` grows** a string of `str_new ()` / `str_create ()` (it did nothing without a max len); `str_split ()`
  of a string without the delimiter gives 1 token, the whole string (it gave none)
- **`config.h` needs `_GNU_SOURCE`:** it does not define the `__USE_*` of glibc; a program built without
  `-D _GNU_SOURCE` stops with an `#error` that says so, even when it includes cerver first; g++ defines it, gcc and
  clang do not
- The text of `RECEIVE_ERROR_TIMEOUT` is `"Timeout"` (it was `"Timeoout"`)
- **What fails without memory returns NULL:** `handler_create ()` without its job queue, `game_score_create ()` without
  its table or its types, `dlist_init ()` and `avl_init ()` without their mutex, `packet_create ()` without the copy
  of its data (it returned the packet empty); `bsem_init ()` leaves the semaphore without its mutex and its
  condition, and its post and its wait do nothing; a cerver, an admin cerver or a client without the lock of its
  handlers does not start them
- **What fails without memory keeps what it had:** `str_copy ()`, `str_n_copy ()`, `str_replace_with ()` (it left
  the String empty) and `str_n_replace_with ()` keep their characters, `packet_append_data ()` keeps the data of the
  packet (it lost it)
- `str_compare ()`, `str_comparator ()`, `str_copy ()` and `str_remove_char ()` take a String with NULL characters;
  `worker_get_state ()`, the setters of a worker and `worker_push_job*` () take a NULL worker
- `abs_int (INT_MIN)` is `INT_MAX`, `itoa ()` writes the digits of `INT_MIN`, `random_int_in_range ()` takes any two
  limits; `c_string_split ()` of a string of only delimiters returns NULL with 0 tokens (it returned an array with a
  NULL token)
- `admin_remove_connection_with_sock_fd ()` only removes the only connection of an admin when it has the socket (it
  removed it with any socket)
- `CerverStats::threshold_time` and `unique_clients` work: every `threshold_time` seconds the counters that grow are
  reset; `GameCerverStats::lobbys_created` and the stats of the admin cerver are written with `__atomic`
- **New:** `c_string_copy_bounded ()`, `dlist_get_apply ()`, `htab_get_apply ()`, `avl_get_node_data_apply ()`,
  `lobby_get_reference ()`, `lobby_search_with_id_reference ()`, `timer_get_gmt_time_r ()` and
  `timer_get_local_time_r ()`
- **Deprecated** (with `DEPRECATED ()`, they go away in the 3.0): `cerver_set_sockets_pool_init ()`,
  `game_set_player_comparator ()`, `lobby_stats_set_threshold_time ()`, `lobby_get ()` and `lobby_search_with_id ()`
  (they return a lobby without a reference), `timer_get_gmt_time ()` and `timer_get_local_time ()` (not reentrant);
  the docs mark the `threshold_time` of the connections, the admins and the lobbies, `CONNECTION_DEFAULT_BAD_PACKETS`,
  the `SERVICE_*` and `GAME_DEFAULT_*` macros and `ClientEvent::request_type`

## 2.1b-17 (from 2.1b-16)

Named pools, and the health and ready routes (F5 of `roadmap.md` §4.5). Nothing changes for a cerver that does not
create a pool nor sets one of the routes: every request runs in its thpool like before.

- **The structs are bigger, and some fields moved:** `HttpRouteSettings` has `pool_name` and `pool` before its
  middlewares, so `middlewares` and `web_socket` moved in it, and with them the fields of `HttpEndpoint` after its
  `settings` and the ones of `HttpGroup` after its own. `HttpCerver` ends with `pools`, `n_pools`, `pools_started`,
  `health_route`, `ready_route`, `n_health_requests`, `n_ready_requests`, `n_not_ready_requests`,
  `connection_requests`, `n_connection_requests`, `uploads_write_microseconds`, `uploads_write_bytes`,
  `keep_alive_follow` and `n_followed_requests`, `Thpool` with `queue_awaited`, and
  `HttpRoute` with `pool_name`. Everything that includes these headers has to be built again
- **`HttpReceiveDispatch` has two more values,** `HTTP_RECEIVE_DISPATCH_HEALTH` (6) and
  `HTTP_RECEIVE_DISPATCH_READY` (7), and `HTTP_RECEIVE_DISPATCH_MAX` is 8: a `switch` over it has two more cases,
  both without an endpoint (`HttpReceive::endpoint` is NULL)
- **The API:** `cerver/http/core/pool.h` (in the umbrella `cerver/http/http.h`) with `HttpPool`,
  `HTTP_POOL_DEFAULT_NAME`, `http_cerver_pool_create ()`, `http_cerver_pool_get_with_name ()`,
  `http_pool_get_name ()`, `http_pool_set_queue_size ()`, `http_pool_set_overload_policy ()`,
  `http_pool_set_wait_timeout ()`, `http_cerver_set_health_route ()`, `http_cerver_set_ready_route ()` and
  `http_cerver_is_ready ()`; `HTTP_ROUTE_SETTING_POOL`, `HTTP_POOL_NAME_SIZE`, `HTTP_CERVER_POOLS_SIZE`,
  `http_group_set_pool ()`, `http_endpoint_set_pool ()` and `http_route_set_pool ()`;
  `http_cerver_set_keep_alive_follow ()` with `HTTP_CERVER_DEFAULT_KEEP_ALIVE_FOLLOW` (0) and
  `HTTP_CERVER_KEEP_ALIVE_FOLLOW_SHORT`, `HTTP_CERVER_CONNECTION_REQUESTS_BUCKETS`, and `thpool_has_queued ()`
- **A thread that follows its connection is off by default,** nothing changes without
  `http_cerver_set_keep_alive_follow ()`. With it, a thread of a pool reads and parses the next request of the
  connection whose request it just answered: a handler can run for several requests of a connection in a row in
  the same thread, without its event loop between them; and that thread uses its processor while it looks for
  the request (at most the time that was set, and never while another request waits for a thread of its pool)
- **A route with a pool that the cerver does not have stops the cerver when it starts,** like any other error of
  its routes: the pools are created before the routes are compiled, and before `cerver_start ()`
- **The pools only start with `CERVER_HANDLER_TYPE_EVENTS`.** With `THREADS` or `POLL` a cerver logs a warning,
  starts no named pool and handles every request with the thread of its connection, like before: a route keeps the
  name of its pool, and it only isolates its handlers when the cerver has event loops
- **A path that is set as the health or the ready route is answered by cerver** for GET and HEAD, before the routes
  tree: an endpoint with the same path and method is not reached, its middlewares and its authentication do not
  run, and the request is not counted in the stats of any route. The other methods of that path are requests like
  any other
- **The ready route answers 503 without closing the connection:** its response has `Retry-After` and no
  `Connection: close` (unlike the 503 of a request that was rejected), so a client can ask again in the same
  connection
- **The metrics have new series,** none changed: `cerver_http_pool_threads`, `cerver_http_pool_threads_working`,
  `cerver_http_pool_queued`, `cerver_http_pool_parked`, `cerver_http_pool_jobs_total`,
  `cerver_http_pool_parked_total` and `cerver_http_pool_unavailable_total` with the label `pool` (the default pool
  is `pool="default"`, also reported without a label by the `cerver_thpool_*` series of before);
  `cerver_http_connections`, `cerver_http_connections_idle` and `cerver_http_connections_accepted_total` with event
  loops; `cerver_http_health_requests_total`, `cerver_http_ready_requests_total` and
  `cerver_http_not_ready_requests_total`; the histograms `cerver_http_pool_wait_seconds` and
  `cerver_http_pool_run_seconds` with the label `pool`, and `cerver_http_connection_requests`;
  `cerver_http_uploads_write_seconds_total` and `cerver_http_uploads_write_bytes_total`;
  `cerver_http_followed_requests_total`
- **The stats of the admin routes have new keys,** none changed: `pools`, `connections`, `n_health_requests`,
  `n_ready_requests`, `n_not_ready_requests`, `uploads_write_microseconds` and `uploads_write_bytes`. A client that
  rejects a key that it does not know has to accept them
- **A write of a file of an upload reads the monotonic clock twice,** to count its time: nothing else changes in
  the uploads
- **Inside cerver** (`src/cerver/events/internal.h`, for the tests that include it): a loop has lanes instead of
  one dispatcher (`EventLoop::lanes`, `event_loop_lane_set ()`; `EventLoop::dispatcher`, `parked_head` and
  `parked_tail` are in its first lane), `event_loop_space ()` takes the lane of the job, and `EventLoopGroup` has
  a dispatcher for each pool. `EventDispatcher` has `is_awaited` (NULL when its jobs never wait): a dispatcher
  that is filled by hand sets it or leaves it NULL, and its struct has to be zeroed. `HttpConnectionOutcome` has
  `READING` (5), `PAUSED` (6) and `FAILED` (7), and `HttpConnection` ends with `follow_misses` and `follow_skip`

## 2.1b-16 (from 2.1b-15)

Keep-alive (F4 of `roadmap.md` §4.4): a connection handles more than one request. A cerver with
`CERVER_HANDLER_TYPE_EVENTS` keeps its connections **by default**; one with `THREADS` or `POLL` closes each one
after its response like before, unless it is asked to keep them.

- **`HttpCerver` and `HttpReceive` are bigger:** `HttpCerver` ends with `keep_alive`, `keep_alive_configured`,
  `keep_alive_timeout`, `keep_alive_requests` and `n_reused_connections`, and `HttpReceive` with `n_requests`. The
  fields of before keep their offsets; a binary of the 2.1b-15 that allocates one of them by itself has to be
  built again
- **`HttpReceive::keep_alive` is a value of each request,** false until the request is complete: the cerver decides
  it before the request is handled (so its response says it) and `http_receive_reset ()` clears it. Code that set
  it by hand to keep a connection open sets it again after each reset, and it only holds for a request that the
  cerver rejects (whose response says `Connection: close` anyway): a complete request follows the decision of the
  cerver
- **The API:** `http_cerver_set_keep_alive ()`, `http_cerver_set_keep_alive_timeout ()` and its `_milliseconds`
  (`HTTP_CERVER_DEFAULT_KEEP_ALIVE_TIMEOUT`, 2^7 secs), `http_cerver_set_keep_alive_requests ()`
  (`HTTP_CERVER_DEFAULT_KEEP_ALIVE_REQUESTS`, 2^10, 0 disables the limit), `HTTP_RESPONSE_CONNECTION_KEEP_ALIVE`
  and the metric `cerver_http_reused_connections_total`

**With `CERVER_HANDLER_TYPE_EVENTS`** (a cerver that wants the behavior of the 2.1b-15 calls
`http_cerver_set_keep_alive (http_cerver, false)`):

- **A response does not always have `Connection: close`:** it has it when the connection is closed after it (the
  client asked for it or is HTTP/1.0 without `keep-alive`, the cerver rejected the request, the connection reached
  its requests, the cerver stops, or keep-alive is disabled); a connection that is kept says nothing to an HTTP/1.1
  client and `Connection: keep-alive` to an HTTP/1.0 one. A response with its own `Connection: close` closes its
  connection
- **A client that reads a response until the end of the connection waits** for the time that the connection is
  kept: it has to use the `Content-Length` of the response, or to send `Connection: close`
- **Pipelined requests are answered,** one at a time and in their order; before, everything after the first
  request of a connection was dropped
- **A connection is closed anyway** after a response whose end the client can not find (without `Content-Length`
  and not a complete chunked stream), after one that could not be sent, and after an upgrade
- **An idle connection** that does not send its next request in `keep_alive_timeout` is closed without a response,
  like one whose cerver stops: its writes are shut down and it ends when its client closes it too. A connection
  that ends between two requests is not logged nor counted as a request
- **At `cerver_set_max_connections ()`** a new connection takes the place of the connection that has been idle the
  longest; the accept only stops when no connection is idle
- **The methods of the application that release the data of a request** (the deleters of `custom_data` and of
  `decoded_data`) still run in the thread of the thpool that handled it, also when its connection is kept
- **Behind a proxy that keeps its connections,** `keep_alive_timeout` has to be more than the time that the proxy
  keeps an idle one, and `keep_alive_requests` more than the requests that the proxy sends through one: the proxy
  is the one that closes them, a proxy that uses a connection that the cerver is closing answers 502

**With `THREADS` or `POLL`,** only after `http_cerver_set_keep_alive (http_cerver, true)` (a warning is logged when
the cerver starts): the thread that handled a request stays with its connection until its next request, so
a connection is only kept while no other one waits for a thread (its response says `Connection: close` otherwise).
An idle connection is never closed for one that waits: its thread gives it back to the queue of the thpool and
takes the one that waits, so a cerver with threads keeps one idle connection for each thread. `CerverReceive` has
two more fields for that, `n_requests` and `idle_deadline`, before `recycle_next`.

**The tools:** `benchmarks/load` sends `Connection: close` with each request, and keeps a connection for each of
its threads with `LOAD_KEEP_ALIVE` in its environment (it prints the connections that it opened);
`benchmarks/server` takes `BENCH_KEEP_ALIVE`.

## 2.1b-15 (from 2.1b-14)

The event loop (F3 of `roadmap.md` §4.3): `CERVER_HANDLER_TYPE_EVENTS`, a new handler type that is opt-in and only
for a web cerver. A cerver that keeps `CERVER_HANDLER_TYPE_THREADS` or `POLL` behaves like in the 2.1b-14; what can
break is the size of two structs and, for a cerver that chooses EVENTS, where some methods of the application run.

- **`Cerver` and `HttpReceive` are bigger:** `Cerver` ends with `n_io_threads`, `max_connections`,
  `overload_wait_timeout`, `stop_fd` and `event_loops`, and `HttpReceive` with `connection`, `pause` (the new enum
  `HttpReceivePause`) and `authorize_pending`. The fields of before keep their offsets; a binary of the 2.1b-14 that
  allocates one of them by itself has to be built again
- **`CERVER_HANDLER_TYPE_MAP` has a fourth row,** `CERVER_HANDLER_TYPE_EVENTS` = 3, with
  `cerver_set_io_threads ()` (`CERVER_DEFAULT_IO_THREADS`, 1), `cerver_set_max_connections ()`
  (`CERVER_DEFAULT_MAX_CONNECTIONS`, 2^14) and `cerver_set_overload_wait_timeout ()`
  (`CERVER_DEFAULT_OVERLOAD_WAIT_TIMEOUT`, 2^5 secs); the other handler types ignore the three. `cerver_start ()`
  fails with EVENTS in a cerver that is not `CERVER_TYPE_WEB` or that has no thpool threads

**Only with `CERVER_HANDLER_TYPE_EVENTS`** (what is different from THREADS in the same cerver):

- **The methods of the application that run in an I/O thread:** `decode_data` of a route with a Bearer token, and
  the uploads filename and dirname generators. While one runs its loop reads no other connection, so they must not
  wait. The custom authentication, the handlers, the middlewares and the static files run in the thpool, like before
- **`CERVER_OVERLOAD_POLICY_WAIT` does not stop the accept:** a request without a place in the thpool waits in its
  loop, in the order that it arrived, up to `cerver_set_overload_wait_timeout ()`; then it is answered with 503 and
  `Retry-After` (with THREADS it waits in the listen backlog without a limit). With 0 seconds it is answered at once
- **`CERVER_OVERLOAD_POLICY_REJECT`** answers 503 when the request is complete and the thpool has no place; a request
  with a body is answered when its headers are complete, and its body is not received
- **The limit of connections:** at `cerver_set_max_connections ()` the cerver stops accepting until one ends, the new
  ones wait in the listen backlog
- **The requests go to the thpool in the order that they are complete,** not in the order that their connections
  were accepted: a connection that was accepted first and sends its request later is handled later (with THREADS
  each connection takes its place in the queue of the thpool when it is accepted)
- **A custom authentication of a request without a body** runs in the same job as its handler; with a body it runs
  before the body is received, like with THREADS, and the body is read by the loop while no thread waits for it
- **A request whose job the thpool dropped** (a drain that rejects) is answered with 503 by its loop, and the files
  that it uploaded are removed: no handler saw them
- **`SO_REUSEPORT`** is set in the listening sockets when the cerver has more than one I/O thread, also when the
  application did not ask for it with `cerver_set_reusable_port ()`
- **A WebSocket** leaves its loop after the 101 and gets its own thread, like with THREADS
- The connection still closes after its response (`Connection: close`); keep-alive is the next phase

**Tests and scripts:**

- The web servers of the tests take their handler type from `CERVER_TEST_HANDLER` (`threads` by default, `events`)
  and their I/O threads from `CERVER_TEST_IO_THREADS`; `test/integration.sh` and `test/nginx/run.sh` run the web
  pairs once per handler (`HANDLERS`), so they take twice as long
- `benchmarks/bin/server` takes the handler type and the I/O threads after the threads of its pool
- `test/performance.sh` also fails when EVENTS is below 90 % of the req/s of THREADS
- `nightly` does not merge `http` anymore: its fixes are applied by hand (`roadmap.md` §4)

## 2.1b-14 (from 2.1b-13)

The structure, the documentation and the license of the 3.0b-3 of `labs`, seeded here: the code of `nightly` and of
`labs` is the same until `labs` replaces a module. The entries «3.0b-1 (from 2.1b-13)», «3.0b-2 (from 3.0b-1)» and
«3.0b-3 (from 3.0b-2)» of the `incompatibilities.md` of `labs` have the details.

- **The license is Apache-2.0** (`LICENSE.md`, `NOTICE`, `thirdparty.md`)
- **Every include changes:** the headers are in folders by domain (`core/`, `admin/`, `auth/`, `client/`,
  `connection/`, `files/`, `handler/`, `load/`, `network/`, `packets/`, `system/`; `http/core/`, `http/headers/`,
  `http/requests/`, `http/responses/`, `http/routes/`, `http/security/`), the big headers are split in parts that the
  main header does not include, `<cerver/cerver.h>` is the umbrella of the core and `<cerver/http/http.h>` the one of
  HTTP; there are no headers with the old paths (the table in the `roadmap.md` of `labs` §11)
- **The functions with `_by_` are named with `_with_`** (43, 35 of them exported)
- **`cerver/crypto`:** the random bytes, SHA-1 and the cleanse go through the OpenSSL provider (`CRYPTO=openssl`, the
  only one); the session ids and the WebSocket handshake use it
- **The fixes of the documentation** (the NULL arguments, the lengths of the truncated values, `str_set ()`,
  `str_split ()`, `arena_alloc ()`, `packet_append_data ()`, `http_response_set_data ()`, …): the list in the entry of
  the 3.0b-3 of `labs`
- The headers are documented with Doxygen (`make docs`, `Doxyfile`, `Dockerfile.docs`, the `docs` job of
  `build.yml`), and every clang build adds `-Wdocumentation`
- The tests binaries follow the folders (`test/bin/connection`, `handler`, `load`, `network`, …, `test/bin/crypto` and
  `test/bin/faults`)

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
