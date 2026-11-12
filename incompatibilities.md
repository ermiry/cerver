# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

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
