![Cerver Banner](./img/banner.png)

![Cerver Production Integration](https://github.com/ermiry/cerver/actions/workflows/production.yml/badge.svg)
![Cerver Continuous Integration](https://github.com/ermiry/cerver/actions/workflows/build.yml/badge.svg)
![Cerver Tests](https://github.com/ermiry/cerver/actions/workflows/test.yml/badge.svg)

**Cerver** is a framework, created by Ermiry, to build fast and reliable servers in C, also usable from C++. It is a single shared library, `libcerver.so`, that only depends on pthreads, with everything that a server needs out of the box: sockets and connections, the packets of its own protocol, handlers and threads, authentication and sessions, a load balancer, file transfers, game lobbies and the collections and utilities that all of them are built with.

## Goal

An easy to use framework that any developer can take and deploy with confidence: software that is healthy, scalable and fast at the same time. Cerver takes care of the networking, the threads and the lifecycle of every connection, so an application only writes what to do with each packet.

## Motivation

Cerver started in 2018 as the multiplayer server of a game. Linux exposes a socket as a plain file descriptor, a number to read from and write into, and it is the job of the developer to turn those bytes into packets and give them a meaning. Learning how much a C program can do with that, it became clear that the same code could be the base of any kind of server, not only one for games. So the code was rewritten with speed, simplicity and scalability in mind: the packets were split in types that are handled independently, then came the clients and their connections, and on top of them the features that saved every server from reinventing them, like lobbies and players for games, authentication, sessions and a load balancer.

## Capabilities

- **TCP and UDP** cervers and clients, in **IPv4 and IPv6**: bind addresses, IPv6 only or dual stack sockets, and link local zones
- **Handler types** for TCP: a single thread with `poll ()`, or a thread per connection, from a thread pool or detachable
- **A portable protocol**: a 32 bytes header in network byte order, packet and request types, and the version of the protocol of the application
- **A serializer by schema**, with objects encoded as CBOR, required and optional fields, and limits for untrusted input
- **Handlers** for the packets of the application, by packet type, by request type or with multiple handlers by id, each one with its own queue and stats
- **Authentication and sessions**, with credentials or session tokens, and an on hold stage for the connections that have not authenticated
- **Admins**, a dedicated connection for monitoring and controlling a cerver
- **A load balancer** that routes the packets of its clients to its services, round robin, by handler id or with a custom selection, and reconnects to the services that go down
- **Files**: send and receive files with their headers, using `sendfile ()`
- **Games**: game types, lobbies with their players, scores and the requests to create, join, leave and start a game
- **Threads**: thread groups, a bounded thread pool with stats, job queues with priorities, workers and a worker manager that scales them
- **Collections**: lists, trees, hash tables, queues, pools, arenas and free lists
- **Events and errors** to run your actions when something happens in a cerver or a client
- **Stats** of every cerver, client, connection, handler and request type, with histograms of their timings
- **Graceful shutdowns** that drain the work in progress before releasing anything
- **Quality**: zero warnings with gcc, g++ and clang, unit and integration tests in IPv4 and IPv6, AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, valgrind and fuzzing in every change

## Versions

| Line | Branch | What it is |
|---|---|---|
| **1.x** | `master` (releases), `core` (development), `beta` | The core of cerver: everything above, without HTTP |
| **2.0** | `http` (development), `http-beta` | Everything in the core, plus an HTTP server: routes, JWT authentication, uploads, JSON. It reaches `master` with the release 2.0 |
| **2.1** | `nightly` (development), `nightly-beta` | Experimental: an event loop, real keep alive and WebSocket |

The changes between versions are in [CHANGELOG.md](./CHANGELOG.md) and [incompatibilities.md](./incompatibilities.md).

## Get started

Cerver builds in Linux with gcc 16.2 and `make`:

``` bash
make -j4                         # bin/libcerver.so
make examples -j4                # examples/bin
sudo make install                # /usr/local/lib/libcerver.so and /usr/local/include/cerver
```

[examples/start.c](./examples/start.c) is an echo cerver and a client that sends it a message. The cerver sends every `PACKET_TYPE_APP` packet back:

``` c
static void echo_handler (void *data) {

	const Packet *packet = (const Packet *) data;

	Packet *response = packet_create (
		PACKET_TYPE_APP, packet->header.request_type,
		packet->data, packet->data_size
	);

	if (!response) return;

	if (!packet_generate (response)) {
		size_t sent = 0;
		(void) packet_send_to (
			response, &sent, false,
			packet->cerver, packet->client, packet->connection, NULL
		);
	}

	packet_delete (response);

}

static int echo_server (void) {

	echo_cerver = cerver_create (
		CERVER_TYPE_CUSTOM, "echo",
		START_PORT, PROTOCOL_TCP, false, START_CONNECTION_QUEUE
	);

	if (!echo_cerver) return EXIT_FAILURE;

	cerver_set_handler_type (echo_cerver, CERVER_HANDLER_TYPE_POLL);

	// a cerver that starts again right after it stopped can bind its port
	cerver_set_reusable_address_flags (echo_cerver, true);

	Handler *handler = handler_create (echo_handler);
	handler_set_direct_handle (handler, true);
	cerver_set_app_handlers (echo_cerver, handler, NULL);

	(void) signal (SIGINT, echo_stop);
	(void) signal (SIGTERM, echo_stop);

	// the teardown also releases a cerver that failed to start
	int result = EXIT_SUCCESS;
	if (cerver_start (echo_cerver)) {
		cerver_log_error ("Failed to start the echo cerver!");

		result = EXIT_FAILURE;
	}

	(void) cerver_teardown (echo_cerver);

	return result;

}
```

`cerver_start ()` blocks until `cerver_shutdown ()`, that the signal handler calls, and `cerver_teardown ()` releases the cerver after its work has been drained, also when it failed to start. The client creates a connection, connects and sends the message in a packet of the same type:

``` c
Client *client = client_create ();

Handler *handler = handler_create (echo_client_handler);
handler_set_direct_handle (handler, true);
client_set_app_handlers (client, handler, NULL);

Connection *connection = client_connection_create (
	client, START_ADDRESS, START_PORT, PROTOCOL_TCP, false
);

if (!client_connect_and_start (client, connection)) {
	(void) echo_send (client, connection);
}
```

Run the cerver in a terminal and the client in another one:

``` bash
LD_LIBRARY_PATH=bin ./examples/bin/start server
LD_LIBRARY_PATH=bin ./examples/bin/start client
```

To install a released version, `cerver-install.sh` downloads it, builds it and installs it in `/usr/local`.

## Documentation

The [wiki](https://github.com/ermiry/cerver/wiki) has the guides of every module of the core and the reference of its whole API: the concepts, the lifecycle of a cerver, the protocol, building, the examples and the tests.

## Examples

[examples](./examples) has a program for each feature: handlers, requests, threads, authentication, sessions, admins, files, the load balancer and its services, games and the clients for them. The wiki describes what each one shows.

## Tests

The tests run in Docker with gcc 16.2:

``` bash
docker build -t ermiry/cerver:test -f Dockerfile.test .
docker run --rm ermiry/cerver:test bash test/run.sh     # the unit tests
bash test/integration.sh                                # cervers with their clients, in IPv4 and IPv6
```

Build the image with `--build-arg SANITIZER=address,undefined` (or `thread`) to run them with the sanitizers.

## License

Copyright (C) 2018 Ermiry, see [LICENSE.md](./LICENSE.md). Find more about cerver at [cerver.ermiry.com](https://cerver.ermiry.com).
