![Cerver Banner](./img/banner.png)

![Cerver Production Integration](https://github.com/ermiry/cerver/actions/workflows/production.yml/badge.svg)
![Cerver Continuous Integration](https://github.com/ermiry/cerver/actions/workflows/build.yml/badge.svg)
![Cerver Tests](https://github.com/ermiry/cerver/actions/workflows/test.yml/badge.svg)

**Cerver** is a framework, created by Ermiry, to build fast and reliable servers in C, also usable from C++. It is a single shared library, `libcerver.so`, with everything that a server needs out of the box: sockets and connections, the packets of its own protocol, handlers and threads, authentication and sessions, a load balancer, file transfers, game lobbies, an HTTP server and the collections and utilities that all of them are built with.

This is the 2.1 line, the experimental one: the HTTP server of the 2.0 with an event loop, real keep alive, named pools, WebSocket and TLS. It can break its API for a better design, and every break is in [incompatibilities.md](./incompatibilities.md). It depends on pthreads and OpenSSL, and optionally on zlib for the compression of the HTTP responses and of the WebSocket messages.

## Goal

An easy to use framework that any developer can take and deploy with confidence: software that is healthy, scalable and fast at the same time. Cerver takes care of the networking, the threads and the lifecycle of every connection, so an application only writes what to do with each packet or each request.

## Motivation

Cerver started in 2018 as the multiplayer server of a game. Linux exposes a socket as a plain file descriptor, a number to read from and write into, and it is the job of the developer to turn those bytes into packets and give them a meaning. Learning how much a C program can do with that, it became clear that the same code could be the base of any kind of server, not only one for games. So the code was rewritten with speed, simplicity and scalability in mind: the packets were split in types that are handled independently, then came the clients and their connections, and on top of them the features that saved every server from reinventing them, like lobbies and players for games, authentication, sessions, a load balancer and an HTTP server for web services.

## Capabilities

### The core

- **TCP and UDP** cervers and clients, in **IPv4 and IPv6**: bind addresses, IPv6 only or dual stack sockets, and link local zones
- **Handler types** for TCP: a single thread with `poll ()`, or a thread per connection, from a thread pool or detachable
- **A portable protocol**: a 32 bytes header in network byte order, packet and request types, and the version of the protocol of the application
- **A serializer by schema**, with objects encoded as CBOR, required and optional fields, and limits for untrusted input
- **Handlers** for the packets of the application, by packet type, by request type or with multiple handlers by id, each one with its own queue and stats
- **Authentication and sessions**, with credentials or session tokens, and an on hold stage for the connections that have not authenticated
- **Admins**, a dedicated connection for monitoring and controlling a cerver
- **A load balancer** that routes the packets of its clients to its services, round robin, by handler id or with a custom selection, and reconnects to the services that go down; with TLS in front of its clients and to its services
- **Files**: send and receive files with their headers, using `sendfile ()`
- **TLS** 1.2 and 1.3 (RFC 9325) in every TCP cerver and client, with OpenSSL 3 as the provider: SNI with many certificates, ALPN, mTLS, session tickets that rotate, 0-RTT with 425, OCSP stapling, keys in a PKCS#11 token, kTLS for what is sent, and reloads of the certificates by the API, a signal or a watch of their files without a restart
- **Games**: game types, lobbies with their players, scores and the requests to create, join, leave and start a game
- **Threads**: thread groups, a bounded thread pool with stats, job queues with priorities, workers and a worker manager that scales them
- **Collections**: lists, trees, hash tables, queues, pools, arenas and free lists
- **Events and errors** to run your actions when something happens in a cerver or a client
- **Stats** of every cerver, client, connection, handler and request type, with histograms of their timings
- **Graceful shutdowns** that drain the work in progress before releasing anything

### HTTP

- **An HTTP/1.1 server** in a `CERVER_TYPE_WEB` cerver, with keep alive, pipelining and limits for the headers and the body, parsed with llhttp in its strict mode
- **Two models**: `CERVER_HANDLER_TYPE_THREADS`, a thread for each connection that is read, or `CERVER_HANDLER_TYPE_EVENTS`, event loops with epoll that read and parse every connection without waiting and give the requests to the pools, with the connections that are kept waiting in their loop and a wait or a 503 when the pools are full
- **Named pools** for the endpoints that must not wait for the others, with health and ready routes
- **WebSocket** (RFC 6455) with permessage-deflate (RFC 7692), in endpoints and groups, with subprotocols, the origins, ping and pong, rooms, broadcasts, a bounded output queue with backpressure in each connection, and a client with reconnection and `wss://`; with the event loop a connection lives in its loop and its methods run in the pool of its endpoint (or in its loop, for short methods)
- **TLS for HTTP** with the threads model and with the event loop, whose handshakes never take a thread: the name that the client asked for, the certificate of the client in each request, endpoints and groups that require it, and the authentication by certificate
- **A remote authentication** (`CURL=1`): a custom authentication asks another service by HTTP with libcurl, and with the event loop its request waits in the loop without a thread of the pool
- **Routes** in a radix tree, with groups that pass their settings to their subgroups and endpoints, path parameters with validators, wildcards, middlewares, and automatic `OPTIONS`, 405 and 308 responses
- **JWT authentication** with HMAC, RSA or ECDSA keys (HS, RS and ES of 256, 384 and 512 bits), validated against their algorithm when the cerver starts
- **Uploads** of multipart forms, with their own limits and a path that never keeps the files of a request that failed
- **JSON** to parse the bodies and to build the responses
- **Responses** with files, streams and chunked bodies, ETag and 304, gzip with zlib, CORS by origin and the headers of every response
- **Behind a proxy**: the real address of the client from the trusted proxies, and a request id
- **Metrics** of the routes, the requests and their responses, and the requests that their clients abandoned at each stage
- **Admin routes** to inspect a running cerver

### Quality

Zero warnings with gcc, g++ and clang, unit and integration tests in IPv4 and IPv6, AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, valgrind and fuzzing in every change, the HTTP server tested behind nginx and with clients that leave in every part of a request.

## Versions

| Line | Branch | What it is |
|---|---|---|
| **1.x** | `master` (releases), `core` (development), `beta` | The core of cerver, without HTTP |
| **2.0** | `http` (development), `http-beta` | Everything in the core, plus the HTTP server. It reaches `master` with the release 2.0 |
| **2.1** | `nightly` (development), `nightly-beta` | Experimental: an event loop, real keep alive, named pools, WebSocket and TLS over threads and in the loop; then the asynchronous authentication and HTTP/2 (its [roadmap](./roadmap.md)) |
| **3.0** | `labs` (development), `labs-beta` | The next major line, with cerver's own code in place of the code of other projects |

The changes between versions are in [CHANGELOG.md](./CHANGELOG.md) and [incompatibilities.md](./incompatibilities.md).

## Get started

Cerver builds in Linux with gcc 16.2 and `make`:

``` bash
make -j4                         # bin/libcerver.so
make ZLIB=1 -j4                  # with gzip in the HTTP responses and permessage-deflate in WebSocket
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

### A web cerver

[examples/web/web.c](./examples/web/web.c) is a web cerver with static files and a few routes. A web cerver is created with `CERVER_TYPE_WEB`, and its routes are registered in its `HttpCerver`:

``` c
web_cerver = cerver_create (
	CERVER_TYPE_WEB, "web-cerver", 8080, PROTOCOL_TCP, false, 2
);

if (web_cerver) {
	cerver_set_thpool_n_threads (web_cerver, 4);
	cerver_set_handler_type (web_cerver, CERVER_HANDLER_TYPE_THREADS);

	HttpCerver *http_cerver = (HttpCerver *) web_cerver->cerver_data;

	http_cerver_static_path_add (http_cerver, "./examples/web/public");

	// GET /test
	HttpRoute *test_route = http_route_create (REQUEST_METHOD_GET, "test", test_handler);
	http_cerver_route_register (http_cerver, test_route);

	if (!cerver_start (web_cerver)) {
		finish ();
	}

	else {
		(void) cerver_teardown (web_cerver);
	}
}
```

Each handler gets the request and sends its response:

``` c
static void test_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) request;

	HttpResponse *res = http_response_json_msg (
		HTTP_STATUS_OK, "Test route works!"
	);

	if (res) {
		(void) http_response_send (res, http_receive);
		http_response_delete (res);
	}

}
```

``` bash
LD_LIBRARY_PATH=bin ./examples/bin/web/web
curl http://localhost:8080/test
```

To install a released version, `cerver-install.sh` downloads it, builds it and installs it in `/usr/local`.

## Documentation

The [wiki](https://github.com/ermiry/cerver/wiki) has the guides of every module of the core and the reference of its whole API: the concepts, the lifecycle of a cerver, the protocol, building, the examples and the tests. The HTTP server is documented in its headers, in [include/cerver/http](./include/cerver/http), until its pages arrive to the wiki with the release 2.0.

## Examples

[examples](./examples) has a program for each feature: handlers, requests, threads, authentication, sessions, admins, files, the load balancer and its services, games and the clients for them. [examples/web](./examples/web) has the web cervers: routes, JSON, JWT authentication, an API, uploads, multiple handlers, jobs, workers, videos and the admin routes.

## Tests

The tests run in Docker with gcc 16.2:

``` bash
docker build -t ermiry/cerver:test -f Dockerfile.test .
docker run --rm ermiry/cerver:test bash test/run.sh     # the unit tests
bash test/integration.sh                                # cervers with their clients, in IPv4 and IPv6
bash test/nginx/run.sh                                  # the web cervers behind nginx
bash test/disconnect.sh                                 # the clients that leave in every part of a request
bash test/performance.sh                                # the allocations, the requests per second and the WebSocket messages in both models
bash test/autobahn/run.sh                               # autobahn-testsuite against the WebSocket server and client
SCHEME=wss bash test/autobahn/run.sh                    # the same over TLS
bash test/nginx/tls/run.sh                              # nginx in front of a web cerver with TLS
bash test/tls/testssl.sh                                # testssl.sh against the defaults of TLS
```

`CERVER_TEST_HANDLER=events` runs the web tests with the event loop, and `test/run.sh` runs the WebSocket ones in both models. `test/bin/tls` makes its certificates in each run and tests TLS against raw clients of OpenSSL, its web cervers with threads and with the event loop, with OCSP and a PKCS#11 token of SoftHSM2 when the image has them.

Build the image with `--build-arg SANITIZER=address,undefined` (or `thread`) to run them with the sanitizers.

## License

Copyright 2018-2026 Ermiry. Licensed under the Apache License, Version 2.0, see [LICENSE.md](./LICENSE.md) and [NOTICE](./NOTICE); the code of other projects that cerver still includes keeps its own license, see [thirdparty.md](./thirdparty.md). Find more about cerver at [cerver.ermiry.com](https://cerver.ermiry.com).
