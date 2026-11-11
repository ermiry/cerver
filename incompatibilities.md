# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

## 1.8b-9 (from 1.8b-8)

The wire of the game packets changes: their requests and answers are serializer objects, a 1.8b-9 game cerver does not talk with 1.8b-8 game clients. The other packets do not change.

- The game requests are the objects `LobbyCreate`, `LobbyJoin` (without a lobby id it searches a lobby of the game type with a place, and creates one when none has it) and `LobbyRequest` (init, start, leave, update and destroy), and every answer is a `LobbySummary` with the players (`LobbyPlayerSummary`) and the settings (`LobbySettingsSummary`) of the lobby; `game_request_send ()` sends a request
- Removed: `SLobby`, `SGameSettings`, `lobby_serialize ()`, the raw `LobbyJoin` with `SStringS`, `SStringSize`, `SStringS`, `SStringM`, `SStringL`, `SStringXL` and `str_selialize ()`, `checkPlayerTimeouts ()` and `lobby_init ()` (it is static); `Lobby` lost `sock_fd_player_map`
- Changed fields (ABI): `Lobby` (new `references`, `poll_connections`, `wake_fds`, `initialized`, `ended`, `handler_started`, `update_started`, `stop_mutex` and `stop_cond`; `max_players_fds` and `current_players_fds` are `u32`), `GameCerver` (new `lock`, `lobby_threads`, `lobby_threads_mutex` and `lobby_threads_cond`), `GameType` (new `init`, `player_timeout` and `fps`), `Player` (new `last_activity`), `Connection` (new `lobby`), `CerverEventData` (new `lobby`), `ClientEventData` (new `owns_response_data`)
- A lobby is released by its references: the game cerver, its threads and each packet and event of the lobby keep one, so `lobby_teardown ()` releases the one of its creator and the lobby is deleted when the last one is released (`lobby_reference ()`, `lobby_release ()`); `lobby_delete ()` releases it at once
- The lobbies, their players, their owner and the lobby of each connection are read and written with the recursive lock of the game cerver (`game_cerver_lock ()`), and the callbacks of the game types run with it
- `lobby_join ()` fails for a full lobby, a lobby in game, a lobby that ended, and a client that is already a player of a lobby; `lobby_create ()` takes `LOBBY_DEFAULT_MAX_PLAYERS` with 0 max players
- `lobby_leave ()` gives the owner to the next player in every case (also when a connection ends), releases the player and tears down the empty lobby; `player_unregister_from_lobby ()` no longer tears down an empty lobby
- The packets of a player carry its lobby (`packet->lobby`, it was always `NULL`), and its app packets go to the packet handler of the lobby (`lobby_set_packet_handler ()`) while the lobby runs
- The lobby poll receives the players with the receive of the cerver, only with `CERVER_HANDLER_TYPE_POLL`; with `CERVER_HANDLER_TYPE_THREADS` the lobby does not take the connections
- The update of a lobby (`lobby_set_update ()`) is called every frame (`game_type_set_fps ()`, `GAME_TYPE_DEFAULT_FPS`) while the lobby runs, it used to be called once; a player that does not send in `game_type_set_player_timeout ()` seconds leaves the lobby
- Only the owner inits (`GameType.init`), starts and destroys a game, a game is not started twice, and `start` and `end` can be `NULL`; `start` runs before the lobby threads start, and `end` only for a game that started
- A game cerver tears down its lobbies when it stops, before it ends its connections, and waits for their threads
- A cerver that is not a game cerver answers the game packets with a `CERVER_ERROR_PACKET_ERROR` (it crashed)
- The lobby ids of `lobby_default_id_generator ()` have a counter, the lobbies of the same second have different ids
- New map values: `CERVER_EVENT_LOBBY_DESTROY` (24, `UNKNOWN` is 25), `CERVER_ERROR_DESTROY_LOBBY` and `CLIENT_ERROR_DESTROY_LOBBY` (13, `UNKNOWN` is 14), `CLIENT_EVENT_LOBBY_UPDATE`, `CLIENT_EVENT_LOBBY_DESTROY` and `CLIENT_EVENT_GAME_INIT` (17 to 19, `UNKNOWN` is 20); the lobby events of the cerver and of the client are triggered now, the client ones with the `LobbySummary` as their response data
- The `action_args` of the `ClientErrorData` are the arguments of `client_error_register ()`, they were the `ClientError`
- `game_score_create ()` takes `unsigned int` counts (a `u8` before the variable arguments is undefined behaviour), and `game_score_add_lobby_players ()` takes the lobby and adds its players with an id
- The headers of game declare their functions with `CERVER_EXPORT`, `CERVER_PUBLIC` or `CERVER_PRIVATE`; the guard of `gametype.h` is `_CERVER_GAME_GAMETYPE_H_`
- Only a cerver with `CERVER_HANDLER_TYPE_THREADS` creates its thpool; with the other types, a cerver configured with threads still shuts its clients down when it drains
- A signal that interrupts the receive of a connection (`EINTR`) does not end it, with the threads and the poll handler types
- `thread_set_name ()`, `cerver_log* ()`, `str_create ()`, `str_set* ()`, `str_replace ()` and `c_string_create ()` are checked by the compiler as `printf` formats (`CERVER_PRINTF_FORMAT`), a call with arguments that do not match its format is a warning
- `CERVER_ZERO_INITIALIZER` (`config.h`) initializes a struct to zero both in C and in C++
- The makefile builds with g++ without `-fpermissive`, and with `WERROR=1` every warning is an error (the CI uses it); the tests, the examples, the fuzzers and the benchmarks build with the warnings of the library
- New: `include/cerver/game` lobby objects, `lobby_is_running ()`, `lobby_player_of_client ()`, `lobby_summary_set ()`, `lobby_send_to_players ()`, `game_type_set_init ()`, `game_type_set_player_timeout ()`, `game_type_set_fps ()`, `player_activity_now ()`, the `examples/client/game` example, the `cerver/game` and `examples/game` pairs and the `game` fuzzer

## 1.8b-8 (from 1.8b-7)

The wire does not change: a 1.8b-8 talks with a 1.8b-7.

- Changed fields (ABI): `Cerver` (`current_n_fds` is `u32`, it was `u16`; new `poll_max_connections`, `request_stats`, `request_handlers` and `request_handlers_size`); `Handler` (new `stats`); `Packet` (new `data_class` and `recycle_next`); `Balancer` (new `select_service` and `select_service_args`)
- `BALANCER_TYPE_MAP` has `XX(3, CUSTOM, Custom)`: a `BALANCER_TYPE_CUSTOM` balancer selects the service of each packet with the method of `balancer_set_select_service ()`, that gets the header and the body; without a method, or with an index that has no working service, the packet goes to the unavailable services
- The packets and their buffers of data up to 4096 bytes are recycled (a cache in each thread and shared lists): a `Packet` and its `data` must be released with `packet_delete ()`, and the data of a packet must not be replaced or released by hand; the builds with sanitizers do not recycle them
- `packet_set_data ()` makes the packet the owner of its new data, also when it had a reference before (`data_ref` is `false` after it)
- `file_move_to ()` does not run `mv` with `system ()`: it uses `rename ()` and, between file systems, copies and removes; it returns 0 on success and 1 on error (it returned the result of `system ()`), and it moves inside `saved_path` only when that directory exists
- A client that the main poll can not take (its max of `cerver_set_poll_fds ()`, or a failed realloc) is not dropped by `client_register_to_cerver ()` anymore: the caller releases it with its connections (`client_register_connections_to_cerver ()` and `client_register_connections_to_cerver_poll ()` do not drop it either)
- `cerver_set_poll_fds ()` and `cerver_set_on_hold_poll_fds ()` take the sizes before the cerver starts; the on hold poll uses `max_on_hold_connections` (`CERVER_DEFAULT_ON_HOLD_POLL_FDS`), it was replaced by `CERVER_DEFAULT_POLL_FDS / 2` when the auth started
- The handlers of the request types (`cerver_set_request_handler ()`) take the packets of their request type of `PACKET_TYPE_APP`, `PACKET_TYPE_APP_ERROR` and `PACKET_TYPE_CUSTOM` before the handler of the packet type and the multiple handlers by id
- A `WorkerManager` does not remove an idle worker while jobs wait in its queue
- Renamed defaults: `CERVER_DEFAULT_SOCKET_RECV_TIMEOUT` is `CERVER_DEFAULT_SOCKET_RECEIVE_TIMEOUT` and moved from `handler.h` to `cerver.h`; `CERVER_DEFAULT_UPDATE_INTERVAL_SECS` and `ADMIN_CERVER_DEFAULT_UPDATE_INTERVAL_SECS` end in `_SECONDS`; `DEFAULT_PLAYER_TIMEOUT`, `DEFAULT_FPS`, `DEFAULT_MIN_PLAYERS` and `DEFAULT_MAX_PLAYERS` start with `GAME_`; `DEFAULT_SCORE_SIZE` is `SCORE_DEFAULT_SIZE`
- New: `include/cerver/utils/histogram.h`, `HandlerStats` (`handler_get_stats ()`, `handler_stats_print ()`), `RequestStats` (`cerver_get_request_stats ()`, `cerver_request_stats_print ()`, also in `cerver_stats_print ()`), the memory, load, cpu, host name and process values of `system.h`, `files_copy ()`, `files_copy_recursive ()` and `files_remove_recursive ()`
- The client prints the type of a cerver with its name from `CERVER_TYPE_MAP` (`Cerver type: Balancer`); the names were in upper case
- `test/allocations.sh` expects at most 4 allocations per connection in poll, 5 in threads and 0 per packet in handlers; `test/stress.sh` is new, and `load.sh` was removed
- Every example uses the app library of the examples (`examples/app/events.{h,c}` registers the events that the example did not register)

## 1.8b-7 (from 1.8b-6)

The wire changes: a 1.8b-7 does not talk with a 1.8b-6, clients, services and balancers are updated together.

- The `PacketHeader` travels in `PACKET_HEADER_SIZE` (32) bytes in network byte order, written field by field with `packet_header_encode ()` and read with `packet_header_decode ()`, it used to be sent as it is in memory (40 bytes in LP64, in the order of the host); a header with another `PACKET_HEADER_FORMAT`, unknown flags or reserved bytes that are not zero is a bad packet that closes the connection (a datagram is dropped), and the cerver answers it with an error that says the format it expects
- Changed fields (ABI): `PacketHeader` (`packet_size` is `u64`, `sock_fd` is now the `u32` `route_id`, new `flags`); `Packet` (new `encoded_header`, that a request packet sends: it is encoded again from `header` when it is sent); `ReceiveHandle` (`header` is the bytes of a split header)
- `sizeof (PacketHeader)` is not the size of the header in the wire anymore, `PACKET_HEADER_SIZE` is
- A service answers a request of a load balancer with its `route_id` (`packet_set_route_id ()` sets it in a generated packet); the balancer gives each client connection a route id with a slot of its own, that does not depend on its sock fd, and a generation that changes when the connection ends, so the response for a connection that ended is discarded even when its slot or its sock fd were given to another connection; up to `BALANCER_ROUTES_MAX` (2^20) connections are routed at the same time, `balancer_connection_start ()` returns the route id and `balancer_connection_end ()` takes it
- `packet_set_header_values ()` takes a `u32 route_id` (it was `u16 sock_fd`)
- `serializer.h` is new: the schemas, `SERIALIZER_DECLARE ()` / `SERIALIZER_DEFINE ()`, `serializer_encode ()` / `serializer_decode ()`, `packet_create_serialized ()` and `packet_get_object ()`; the old `SRelativePtr`, `SArray` and `s_*` functions were removed
- The info of a cerver is a `CerverSummary` object (`SCerver`, `S_CERVER_NAME_LENGTH` and `S_CERVER_WELCOME_LENGTH` were removed), `cerver_deserialize ()` takes it
- The session token is a `SessionToken` object (`SToken` was removed), sent with its own auth request types, `AUTH_PACKET_TYPE_CLIENT_TOKEN` and `AUTH_PACKET_TYPE_ADMIN_TOKEN`; a client sets it with `connection_set_auth_token ()` (the size of the auth data no longer tells a token from credentials)
- The error packets have an `ErrorMessage` object (`SError` was removed), its `timestamp` is an `i64` of seconds
- `FileHeader` is an object of its schema: `len` is `u64`, `file_header_get ()` takes the packet, a filename that does not fit in the header is not sent (it was cut), and the callbacks of the file uploads take a `struct FileHeader *` (it was `struct _FileHeader *`)
- New: `cerver_set_auth_schema ()`, the credentials are decoded and validated with it and the authenticate method gets the object; `connection_set_auth_object ()` sends them; the credentials as bytes still work without a schema
- New fields (ABI): `Cerver` (`auth_schema`), `Connection` (`auth_serialized`, `auth_token`, `route_id`), `BalancerRoute` (`generation`, `next_free`), `Balancer` (`routes_used`, `free_route`; `routes_size` is `u32`)
- An auth packet in an authenticated connection is answered with an error
- A connection that no handler could take is dropped and its client is removed from the cerver; the failed auth events of a connection that reached its max auth tries do not have the connection (it was deleted)
- Renamed field: `Cerver` (`isRunning` is now `running`); it is still read with `cerver_is_running ()`

## 1.8b-6 (from 1.8b-5)

The changes were made first in `http` (2.0b-64) and were brought back here.

- New field (ABI): `Socket` (`close_mutex`); the fd of a connection is closed with `socket_close ()`, replaced with `socket_replace ()` and shut down from other threads with `socket_shutdown ()`, all with that mutex, so a shutdown never reaches an fd that was closed and given to another connection; `socket_close ()` shuts the fd down and waits for a send of another thread (the write mutex) before it closes it, so it must not be called with the write mutex
- The check of the inactive clients shuts their sockets down after it releases the clients lock, with a reference to each socket
- `connection_connect ()` gives a connection without its mutex or its condition the ones that it has embedded, so its backoff can always be interrupted: the `sleep ()` of a connection without synchronization was removed
- A client connection whose connect failed, or that never connected, closes its socket: the failed connect closes it and `client_connection_end ()` closes it too (before, the fd stayed open after the teardown of the client)
- `worker_delete ()` is public
- `test/performance.sh` is now `test/allocations.sh`, that also counts the allocations per packet of a handler with its own queue
- The examples stop their cerver in the signal handler and release it after `cerver_start ()` returns, their event actions release their data, and the target of the balancer and service examples is `services` (it was `load`)

## 1.8b-5 (from 1.8b-4)

- New fields (ABI): `Connection` (`connect_timeout`), `Cerver` (`handlers_alive_cond`), `Worker` (`joinable`, `idle_since`, `owns_queue`, `state_changed`); `Pool` and `JobQueue` changed completely
- `Pool` is a stack on an array with its own mutex, without the `dlist`: the last element that was pushed is the first one that `pool_pop ()` takes (it was the first one pushed), and `pool_set_max_elements ()` limits it (an element pushed to a full pool is destroyed)
- `JobQueue` keeps its jobs in a ring per priority (`JOB_QUEUE_DEFAULT_PRIORITIES`, the default one in the middle), with one mutex and conditions: `queue`, `rwmutex` and the `bsem` `has_jobs` were removed; a thread waits with `job_queue_pull_timeout ()`, `job_queue_pull_interruptible ()` or `job_queue_wait ()`, and is woken with `job_queue_signal ()`
- A lower priority job is taken after `JOB_QUEUE_DEFAULT_STARVATION_LIMIT` jobs were taken ahead of it (`job_queue_set_starvation_limit ()`)
- The handlers take their jobs from the pool of their queue (`job_get ()` / `job_return ()`), a job of a handler queue must not be released with `job_delete ()`
- `cerver_get_n_handlers_working ()` and `admin_cerver_get_n_handlers_working ()` read an atomic counter, the handlers count themselves without the handlers lock
- `thpool.h` no longer includes `jobs.h`
- Workers have joinable threads: `worker_end ()` waits for the job that is running and joins the thread, `worker_delete ()` ends a running worker, `WORKER_SLEEP_TIME` was removed and `worker_stop ()` / `worker_end ()` wait in a condition instead of polling every 100 ms; `worker_get_state ()` is public
- A client connection tries every address that its host resolves to, alternating the families and starting with the preferred one (`use_ipv6`), each with a non blocking connect of up to `CONNECTION_DEFAULT_CONNECT_TIMEOUT` ms (`connection_set_connect_timeout ()`); `use_ipv6` stays as the preference, it is no longer replaced by the family of the resolved address
- The reconnection resolves the host again and tries each address, it used to connect again to the first address resolved when the connection was created
- `network_parse_address ()` takes a zone in a link local IPv6 address (`fe80::1%eth0` or `%2`), and a zone in any other address is not valid
- `sock_ip_to_string_actual ()` writes the zone of an address with a scope id, its buffer must be of `NETWORK_ADDRESS_STRING_SIZE` bytes (it was `INET6_ADDRSTRLEN`); `CERVER_BIND_ADDRESS_SIZE` and `CERVER_UDP_PEER_KEY_SIZE` grew with it
- `sock_ip_equal ()` takes an IPv4 mapped address (`::ffff:a.b.c.d`) as equal to the IPv4 and does not compare the flow info
- The bind address decides the family of the cerver's socket, even if `cerver_set_network_values ()` was called after `cerver_set_bind_address ()`
- The ready log of a cerver has its address: "Cerver <name> is ready in <address> port <port>!"
- `cerver_log_end ()` joins the thread that syncs the log file before it closes the file

## 1.8b-4 (from 1.8b-3)

- New fields (ABI): `Packet` (`release`, `release_args`), `Connection` (`shut_down`, `datagram_token`, `datagram_token_set`) and `Cerver` (`udp`, `udp_peer_timeout`, `udp_max_peers`, `udp_peers_count`)
- `packet_delete ()` calls the `release` of the packet before it releases it, a packet that is filled by hand must have it as NULL
- `MAX_UDP_PACKET_SIZE` is 65507 (the payload of an IPv4 UDP datagram), it was 65515: it is the default max received packet size of a cerver and of a client, so a TCP packet of 65508 to 65515 bytes is now too big by default
- `PROTOCOL_UDP` works: a cerver receives datagrams of a single packet each, one with another size is a bad packet, and `packet_send ()` sends a packet in a single datagram and fails when it does not fit
- An UDP cerver with authentication: a client sends its auth packet when it connects (the cerver does not ask for it), its success has the datagram token (after the session token with sessions), and every datagram after it carries the token after its packet, in both ways; the token stops a forged address but not someone who sees the traffic
- An UDP cerver does not take admins: `cerver_start ()` fails when it has one, and an admin auth packet is a bad packet
- An UDP cerver only sends its info to a peer that asks for it, and only answers a peer that did not authenticate up to `CERVER_UDP_AMPLIFICATION_FACTOR` times the bytes that it sent
- The request (files) and game packets are bad packets in UDP: they need the socket of a connection, a peer does not have one
- The auth packet of a cerver is sent by a TCP client when it gets the cerver info, an UDP client already sent it when it connected
- The on hold poll thread only runs in a TCP cerver
- An auth packet with a token of `sizeof (SToken)` bytes without its NUL is taken as credentials, it was read past the packet

## 1.8b-3 (from 1.8b-2)

- New fields (ABI): `Handler` (`joinable`), `Client` (`session_lock`, `teardown_requested`, `event_threads`, the storages of its stats, file stats and lock, `recycle_next`), `ClientStats` (the storages of its packets per type) and `Cerver` (`connection_threads`, `event_threads`)
- The stats, the file stats and the lock of a client live inside it: they must not be freed nor replaced
- Clients are recycled like connections: a pointer to a client is never valid after it was deleted
- `client->session_id` is set and read with the client's session lock: read it with `client_get_session_id ()`, not directly from another thread
- The handlers threads are joinable and the teardowns join them: a handler that never returns from its method blocks the teardown, it used to be polled every second forever
- The cerver and client teardowns wait for the event and error actions that run in their own threads (`create_thread`), and the cerver drain also waits for its detachable connection threads
- A drain that times out waits `CERVER_DRAIN_GRACE_TIMEOUT` more seconds before giving up
- The connect backoff of a connection stops when its client is torn down or the connection is ended, `connection_connect ()` returns 1 instead of waiting it
- A `FileHeader` of a packet is used only if its filename is a single NUL terminated name (no `/`, `.` or `..`): any other gets the "Missing or invalid file header" error
- The packets that are not taken by a handler are released: APP, APP_ERROR and CUSTOM without a handler or with a full queue, NONE and CERVER in a cerver, NONE in a client, and GAME after its handler
- `client_connection_get_next_packet ()` accepts packets up to the client's max received packet size, not `MAX_UDP_PACKET_SIZE`, and a packet with an invalid size closes the connection
- A cerver info packet smaller than `SCerver` is ignored, and `cerver_deserialize ()` takes a `const SCerver *`

## 1.8b-2 (from 1.8b-1)

- New fields (ABI): `Cerver` (`bind_address`, `ipv6_only`), `Connection` (`send_timeout`, the storages of its stats and synchronization values, `recycle_next`), `ConnectionStats` (the storages of its packets per type), `Socket` (`recycle_next`), `CerverReceive` (`recycle_next`), `Thpool` (`stats`) and `ThpoolJob` (`queued`)
- The stats, the state mutex, the cond and the mutex of a connection live inside it: they must not be freed nor replaced
- Connections, sockets and receives are recycled: a pointer to one of them is never valid after it was deleted, even if it still points to readable memory
- Every send uses `MSG_NOSIGNAL`: a closed peer returns `EPIPE` instead of raising `SIGPIPE`
- A send to a full non blocking socket waits up to the connection's send timeout (`CERVER_DEFAULT_SEND_TIMEOUT`) instead of failing with `EAGAIN`
- `packet_send_split_tcp` fails when the data was not sent completely, it used to succeed after sending the header
- `packet_send_to_socket` sets `total_sent` to every byte that was sent, it used to set only the bytes of the last `send ()`
- `file_send` and `file_send_by_fd` return -1 when the file was not sent completely
- `sock_ip_to_string_actual` and `sock_ip_to_string` write an IPv4 mapped address (`::ffff:a.b.c.d`) as `a.b.c.d`, so a connection's `ip` is the IPv4 of an IPv4 client of a dual stack cerver
- A client connection resolves its ip or host with `getaddrinfo ()` and the family of the socket is the one of the resolved address; an ip that does not resolve fails instead of connecting to `in6addr_any`
- An IPv6 cerver always sets `IPV6_V6ONLY`, by default to `false` (dual stack), instead of depending on the `net.ipv6.bindv6only` sysctl

## 1.8b-1 (from 1.7)

- `PacketHeader` has a `version` (40 bytes in LP64), the version is no longer sent in the data: a 1.8 cerver does not talk with a 1.7 client, service or balancer
- `cerver_sockets_pool_push` / `_pop` were removed, `cerver_set_sockets_pool_init` does nothing
- `ELEM_AT` is now `ARRAY_ELEM_AT`
- `thread_create_detachable` returns `unsigned int`
- `thpool_add_work` fails with a full queue, `thpool_is_full` means a full queue, the `Thpool` structure changed
- `sock_set_reusable` no longer sets `SO_REUSEPORT` (`sock_set_reusable_port`)
- `receive_handle_reset` was removed
- New fields (ABI): `Cerver`, `Client`, `Connection`, `Socket` and `ScoreBoard`
