# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

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
