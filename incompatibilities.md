# Incompatibilities

The changes of each version that can break code, binaries or peers built with the version before it, the newest first.
Unlike `CHANGELOG.md`, which only has the current version, every entry stays here.

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
