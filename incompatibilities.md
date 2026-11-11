# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

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
