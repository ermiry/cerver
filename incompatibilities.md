# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

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
