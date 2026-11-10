# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

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
