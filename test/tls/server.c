#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cerver/connection/connection.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>
#include <cerver/network/socket.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/tls/cerver.h>
#include <cerver/tls/tls.h>

#include "cerver/crypto/provider.h"
#include "cerver/tls/internal.h"

#include "../test.h"
#include "tls.h"

#define SERVER_THREADS						4

// one event loop, so a client that blocked it would block every other one
#define SERVER_IO_THREADS					1

// the requests that a client writes one after the other, each one in its own record, before it reads
#define SERVER_BURST_REQUESTS				(1 << 3)
#define SERVER_BURST_BUFFER_SIZE			(1 << 14)

// the send buffer of the socket of the tracked response, far smaller than the response
#define SERVER_TRACKED_SEND_BUFFER			(1 << 14)
// the looks at what waits while the client does not read
#define SERVER_MARK_SAMPLES					(1 << 6)
// the mark and two records with their overhead
#define SERVER_MARK_LIMIT					(TLS_OUTPUT_FLUSH_MARK + (2 * (TLS_RECORD_PLAINTEXT_SIZE + (1 << 9))))

// where the log of the start of a cerver is read
#define SERVER_LOG_TEMPLATE					"/tmp/cerver-tls-log-XXXXXX"
#define SERVER_LOG_SIZE						(1 << 14)

#define SERVER_SECONDS_PER_DAY				(60L * 60L * 24L)
#define SERVER_EXPIRY_WARNING_SECONDS		(TLS_CERTIFICATE_EXPIRY_WARNING_DAYS * SERVER_SECONDS_PER_DAY)

// a full cerver: one thread for a handshake and one place in its queue, and the pause between its connections
#define SERVER_FULL_THREADS					1
#define SERVER_FULL_QUEUE					1
#define SERVER_FULL_PAUSE_MILLISECONDS		(1 << 6)

// the bytes of the large response, more than a few records and more than the flush mark
#define SERVER_LARGE_SIZE					((1 << 20) + 333)

// the early data that a client sends
#define SERVER_EARLY_DATA_SIZE				(1 << 14)

// the deadline of the handshake of the test of the timeout
#define SERVER_SHORT_HANDSHAKE				(1 << 8)

#define SERVER_BODY_SIZE					(1 << 10)

#define SERVER_NAME_SIZE					(1 << 7)

// the milliseconds that a reload waits for the files to settle
#define SERVER_WATCH_DEBOUNCE				(1 << 7)
// the key is renamed a quarter of the debounce after its certificate
#define SERVER_WATCH_SPLIT					4
// the debounces that the test waits after the reload, for a reload that should not come
#define SERVER_WATCH_SETTLE					2
#define SERVER_MICROSECONDS_PER_MILLISECOND	1000
#define SERVER_MILLISECONDS_PER_SECOND		1000

// the clients with kTLS whose flight of the handshake does not fit in their sockets, for each unit of the time scale
#define SERVER_STUCK_CLIENTS				(1 << 3)
// the smallest receive buffer and segments of the kernel: the client takes almost nothing of the flight
#define SERVER_STUCK_RECEIVE_BUFFER			(1 << 10)
#define SERVER_STUCK_SEGMENT				(1 << 9)
// the pause after the hellos of the stuck clients, for the loop to take them before the request
#define SERVER_STUCK_PAUSE_MILLISECONDS		(1 << 5)
// the request is answered in less than this part of what the loop would wait for all the stuck clients
#define SERVER_STUCK_WAIT_PART				2

static const unsigned char server_alpn_h2[] = { 2, 'h', '2' };
static const unsigned char server_alpn_both[] = { 2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1' };

static const char *server_hello_request = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
static const char *server_close_request = "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";

static const char *server_ok_status = "HTTP/1.1 200";

static const char *server_expiry_warning = "expires in";

static const char *server_tracked_request = "GET /tracked HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";

// a path without a route, the default route of cerver answers it
static const char *server_default_request = "GET /nothing HTTP/1.1\r\nHost: localhost\r\n\r\n";

// the first bytes of the header of a record of a handshake, never the whole header
static const unsigned char server_partial_record[] = { 0x16, 0x03 };

static u16 server_port = TLS_TEST_PORT_BASE;

static char *server_large = NULL;

// every test of the server runs with threads and with the event loop
static CerverHandlerType server_handler_type = CERVER_HANDLER_TYPE_THREADS;

// the TLS of the request of the tracked response while its handler sends it
static TlsConnection *server_tracked_tls = NULL;

typedef struct TlsServer {

	Cerver *cerver;
	pthread_t thread;
	u16 port;

} TlsServer;

#pragma region routes

static void server_hello (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;
	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, "hello");

}

static void server_echo (const HttpReceive *receive, const HttpRequest *request) {

	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, request->body ? request->body->str : "");

}

// the name that the client asked for
static void server_name (const HttpReceive *receive, const HttpRequest *request) {

	(void) http_response_json_msg_send (
		receive, HTTP_STATUS_OK, request->tls.server_name ? request->tls.server_name : "none"
	);

}

// the subject of the certificate of the client
static void server_certificate (const HttpReceive *receive, const HttpRequest *request) {

	const TlsCertificate *certificate = http_request_get_client_certificate (request);

	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, certificate ? certificate->subject : "none");

}

static void server_large_handler (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;
	(void) http_response_render_text (receive, HTTP_STATUS_OK, server_large, SERVER_LARGE_SIZE);

}

// the large response with a small buffer of its socket, so most of it waits: the test reads what its TLS keeps
static void server_tracked_handler (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;

	Socket *socket = receive->cr->connection->socket;

	const int size = SERVER_TRACKED_SEND_BUFFER;
	(void) setsockopt (socket->sock_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof (size));

	__atomic_store_n (&server_tracked_tls, socket->tls, __ATOMIC_SEQ_CST);

	(void) http_response_render_text (receive, HTTP_STATUS_OK, server_large, SERVER_LARGE_SIZE);

	__atomic_store_n (&server_tracked_tls, NULL, __ATOMIC_SEQ_CST);

}

static void server_routes (HttpCerver *http_cerver) {

	HttpGroup *root = http_cerver_group_create (http_cerver, "");
	(void) http_group_get (root, "hello", server_hello);
	(void) http_group_get (root, "tracked", server_tracked_handler);
	(void) http_group_post (root, "echo", server_echo);
	(void) http_group_get (root, "name", server_name);
	(void) http_group_get (root, "certificate", server_certificate);
	(void) http_group_get (root, "large", server_large_handler);

	HttpGroup *mtls = http_cerver_group_create (http_cerver, "mtls");
	http_group_set_client_certificate (mtls, HTTP_CLIENT_CERTIFICATE_REQUIRED);
	(void) http_group_get (mtls, "secret", server_hello);

	HttpGroup *auth = http_cerver_group_create (http_cerver, "auth");
	http_group_set_auth (auth, HTTP_ROUTE_AUTH_TYPE_CERTIFICATE);
	(void) http_group_get (auth, "secret", server_certificate);

}

#pragma endregion

#pragma region server

static void *server_thread (void *server_ptr) {

	TlsServer *server = (TlsServer *) server_ptr;
	(void) cerver_start (server->cerver);

	return NULL;

}

static Cerver *server_create (const u16 port, TlsSettings *settings) {

	Cerver *cerver = cerver_create_web ("tls-test", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, server_handler_type);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, SERVER_THREADS);
	cerver_set_io_threads (cerver, SERVER_IO_THREADS);

	server_routes ((HttpCerver *) cerver->cerver_data);

	if (settings) test_check_unsigned_eq (cerver_set_tls (cerver, settings), 0, NULL);

	return cerver;

}

// the server listens once a TCP connection is accepted
static void server_wait_listen (const u16 port) {

	bool listening = false;
	for (unsigned int attempt = 0; !listening && (attempt < (1 << 9)); attempt++) {
		const int fd = socket (AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in address;
		(void) memset (&address, 0, sizeof (address));
		address.sin_family = AF_INET;
		address.sin_port = htons (port);
		address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

		listening = !connect (fd, (struct sockaddr *) &address, sizeof (address));
		(void) close (fd);

		if (!listening) (void) usleep (1 << 12);
	}

	test_check_true (listening);

}

// a cerver whose connections are kept in the two handler types
static TlsServer *server_start_kept (TlsSettings *settings, const bool keep_alive) {

	TlsServer *server = (TlsServer *) calloc (1, sizeof (TlsServer));
	test_check_ptr (server);

	server->port = server_port++;
	server->cerver = server_create (server->port, settings);

	if (keep_alive) http_cerver_set_keep_alive ((HttpCerver *) server->cerver->cerver_data, true);

	test_check_int_eq (pthread_create (&server->thread, NULL, server_thread, server), 0, NULL);
	server_wait_listen (server->port);

	return server;

}

static TlsServer *server_start (TlsSettings *settings) {

	return server_start_kept (settings, false);

}

static void server_stop (TlsServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

static TlsSettings *server_settings (const TestCertificates *certificates) {

	TlsSettings *settings = tls_settings_create ();
	test_check_ptr (settings);
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->server_chain, certificates->server_key), 0, NULL);

	return settings;

}

static RawTlsOptions server_options (const TestCertificates *certificates) {

	RawTlsOptions options;
	(void) memset (&options, 0, sizeof (RawTlsOptions));
	options.authority = certificates->authority;

	return options;

}

static TlsStats server_stats (const TlsServer *server) {

	TlsStats stats;
	test_check_unsigned_eq (cerver_tls_get_stats (server->cerver, &stats), 0, NULL);

	return stats;

}

static u64 server_reloads (const TlsStats *stats) { return stats->reloads; }

static u64 server_timeouts (const TlsStats *stats) { return stats->failures[TLS_FAILURE_TIMEOUT]; }

// waits until a stat of the cerver reaches the expected value
static u64 server_wait_stat (const TlsServer *server, u64 (*field) (const TlsStats *), const u64 expected) {

	struct timespec deadline;
	(void) clock_gettime (CLOCK_MONOTONIC, &deadline);
	deadline.tv_sec += (time_t) (TEST_WAIT_SECONDS * test_time_scale ());

	TlsStats stats = server_stats (server);
	struct timespec now = deadline;
	while ((field (&stats) < expected) && !clock_gettime (CLOCK_MONOTONIC, &now) && (now.tv_sec < deadline.tv_sec)) {
		(void) usleep (1 << 12);
		stats = server_stats (server);
	}

	return field (&stats);

}

// a request in a new connection, its status
static unsigned int server_status (const TlsServer *server, const RawTlsOptions *options, const char *request, char *body) {

	RawTls *raw = raw_tls_connect (server->port, options);
	const unsigned int status = raw_tls_request (raw, request, body, body ? SERVER_BODY_SIZE : 0);
	raw_tls_close (raw);

	return status;

}

#pragma endregion

#pragma region tests

static void test_server_versions (const TestCertificates *certificates) {

	TlsServer *server = server_start (server_settings (certificates));

	RawTlsOptions options = server_options (certificates);
	test_check_unsigned_eq (server_status (server, &options, server_hello_request, NULL), 200, NULL);

	options.max_version = TLS1_2_VERSION;
	test_check_unsigned_eq (server_status (server, &options, server_hello_request, NULL), 200, NULL);

	// TLS 1.1 is never accepted (RFC 8996)
	options.max_version = TLS1_1_VERSION;
	options.min_version = TLS1_VERSION;
	RawTls *old = raw_tls_connect (server->port, &options);
	test_check_false (old->connected);
	raw_tls_close (old);

	const TlsStats stats = server_stats (server);
	test_check_true (stats.handshakes >= 2);
	test_check_true (stats.failures[TLS_FAILURE_PROTOCOL] >= 1);

	server_stop (server);

	// a cerver of TLS 1.3 only refuses TLS 1.2
	TlsSettings *settings = server_settings (certificates);
	tls_settings_set_min_version (settings, TLS_VERSION_1_3);
	server = server_start (settings);

	options = server_options (certificates);
	options.max_version = TLS1_2_VERSION;
	RawTls *refused = raw_tls_connect (server->port, &options);
	test_check_false (refused->connected);
	raw_tls_close (refused);

	options.max_version = 0;
	test_check_unsigned_eq (server_status (server, &options, server_hello_request, NULL), 200, NULL);

	server_stop (server);

}

static void test_server_ciphers (const TestCertificates *certificates) {

	TlsServer *server = server_start (server_settings (certificates));

	// without forward secrecy and without AEAD
	RawTlsOptions options = server_options (certificates);
	options.max_version = TLS1_2_VERSION;
	options.ciphers12 = "AES128-SHA:AES256-SHA:ECDHE-ECDSA-AES128-SHA";
	RawTls *weak = raw_tls_connect (server->port, &options);
	test_check_false (weak->connected);
	raw_tls_close (weak);

	options.ciphers12 = "ECDHE-ECDSA-CHACHA20-POLY1305";
	test_check_unsigned_eq (server_status (server, &options, server_hello_request, NULL), 200, NULL);

	server_stop (server);

}

static void server_check_name (const TlsServer *server, const TestCertificates *certificates, const char *asked, const char *expected) {

	RawTlsOptions options = server_options (certificates);
	options.authority = NULL;
	options.server_name = asked;

	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);

	char name[SERVER_NAME_SIZE];
	raw_tls_peer_name (raw, name, sizeof (name));
	test_check_str_eq (name, expected, NULL);

	raw_tls_close (raw);

}

static void test_server_sni (const TestCertificates *certificates) {

	TlsSettings *settings = server_settings (certificates);
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->other_chain, certificates->other_key), 0, NULL);
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->wildcard_chain, certificates->wildcard_key), 0, NULL);

	TlsServer *server = server_start (settings);

	server_check_name (server, certificates, "other.test", "other.test");
	server_check_name (server, certificates, "any.wild.test", "wild.test");
	server_check_name (server, certificates, "unknown.test", "localhost");
	server_check_name (server, certificates, NULL, "localhost");

	// the name is in the request
	RawTlsOptions options = server_options (certificates);
	options.authority = NULL;
	options.server_name = "other.test";
	char body[SERVER_BODY_SIZE];
	test_check_unsigned_eq (server_status (server, &options, "GET /name HTTP/1.1\r\nHost: other.test\r\n\r\n", body), 200, NULL);
	test_check_ptr (strstr (body, "other.test"));

	server_stop (server);

}

static void test_server_alpn (const TestCertificates *certificates) {

	TlsServer *server = server_start (server_settings (certificates));

	RawTlsOptions options = server_options (certificates);

	// a client that offers ALPN without http/1.1 is closed with no_application_protocol
	options.alpn = server_alpn_h2;
	options.alpn_size = sizeof (server_alpn_h2);
	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_false (raw->connected);
	raw_tls_close (raw);

	options.alpn = server_alpn_both;
	options.alpn_size = sizeof (server_alpn_both);
	raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);

	const unsigned char *selected = NULL;
	unsigned int selected_size = 0;
	SSL_get0_alpn_selected (raw->ssl, &selected, &selected_size);
	test_check_unsigned_eq (selected_size, 8, NULL);
	test_check_int_eq (memcmp (selected, "http/1.1", 8), 0, NULL);

	raw_tls_close (raw);

	test_check_true (server_stats (server).failures[TLS_FAILURE_ALPN] >= 1);

	server_stop (server);

}

static void test_server_mtls_required (const TestCertificates *certificates) {

	TlsSettings *settings = server_settings (certificates);
	test_check_unsigned_eq (tls_settings_set_ca (settings, certificates->authority, NULL), 0, NULL);
	tls_settings_set_client_verify (settings, TLS_CLIENT_VERIFY_REQUIRED);

	TlsServer *server = server_start (settings);

	// without a certificate the handshake fails
	RawTlsOptions options = server_options (certificates);
	test_check_unsigned_eq (server_status (server, &options, server_hello_request, NULL), 0, NULL);

	// a certificate of another authority fails
	options.client_chain = certificates->stranger_chain;
	options.client_key = certificates->stranger_key;
	test_check_unsigned_eq (server_status (server, &options, server_hello_request, NULL), 0, NULL);

	options.client_chain = certificates->client_chain;
	options.client_key = certificates->client_key;
	char body[SERVER_BODY_SIZE];
	test_check_unsigned_eq (server_status (server, &options, "GET /certificate HTTP/1.1\r\nHost: localhost\r\n\r\n", body), 200, NULL);
	test_check_ptr (strstr (body, "cerver test client"));

	test_check_true (server_stats (server).failures[TLS_FAILURE_CERTIFICATE] >= 2);

	server_stop (server);

}

static void test_server_mtls_optional (const TestCertificates *certificates) {

	TlsSettings *settings = server_settings (certificates);
	test_check_unsigned_eq (tls_settings_set_ca (settings, certificates->authority, NULL), 0, NULL);
	tls_settings_set_client_verify (settings, TLS_CLIENT_VERIFY_OPTIONAL);

	TlsServer *server = server_start (settings);

	RawTlsOptions options = server_options (certificates);
	char body[SERVER_BODY_SIZE];
	test_check_unsigned_eq (server_status (server, &options, "GET /certificate HTTP/1.1\r\nHost: localhost\r\n\r\n", body), 200, NULL);
	test_check_ptr (strstr (body, "none"));

	// an endpoint that requires the certificate answers 403, the authentication by certificate 401
	test_check_unsigned_eq (server_status (server, &options, "GET /mtls/secret HTTP/1.1\r\nHost: localhost\r\n\r\n", NULL), 403, NULL);
	test_check_unsigned_eq (server_status (server, &options, "GET /auth/secret HTTP/1.1\r\nHost: localhost\r\n\r\n", NULL), 401, NULL);

	options.client_chain = certificates->client_chain;
	options.client_key = certificates->client_key;
	test_check_unsigned_eq (server_status (server, &options, "GET /mtls/secret HTTP/1.1\r\nHost: localhost\r\n\r\n", NULL), 200, NULL);
	test_check_unsigned_eq (server_status (server, &options, "GET /auth/secret HTTP/1.1\r\nHost: localhost\r\n\r\n", body), 200, NULL);
	test_check_ptr (strstr (body, "cerver test client"));

	server_stop (server);

}

// a session that a first connection got
static SSL_SESSION *server_session (const TlsServer *server, const RawTlsOptions *options) {

	RawTls *raw = raw_tls_connect (server->port, options);
	test_check_true (raw->connected);

	// TLS 1.3 sends its tickets after the handshake: a request reads them
	test_check_unsigned_eq (raw_tls_request (raw, server_hello_request, NULL, 0), 200, NULL);

	SSL_SESSION *session = SSL_get1_session (raw->ssl);
	test_check_ptr (session);

	raw_tls_close (raw);

	return session;

}

static void server_check_resumption (const TlsServer *server, const RawTlsOptions *base) {

	SSL_SESSION *session = server_session (server, base);

	RawTlsOptions options = *base;
	options.session = session;

	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);
	test_check_int_eq (SSL_session_reused (raw->ssl), 1, NULL);
	test_check_unsigned_eq (raw_tls_request (raw, server_hello_request, NULL, 0), 200, NULL);
	raw_tls_close (raw);

	SSL_SESSION_free (session);

}

static void test_server_resumption (const TestCertificates *certificates) {

	TlsServer *server = server_start (server_settings (certificates));

	RawTlsOptions options = server_options (certificates);
	server_check_resumption (server, &options);

	options.max_version = TLS1_2_VERSION;
	server_check_resumption (server, &options);

	test_check_true (server_stats (server).resumed >= 2);

	server_stop (server);

	// without tickets TLS 1.2 resumes from the cache of the cerver
	TlsSettings *settings = server_settings (certificates);
	tls_settings_set_tickets (settings, false);
	server = server_start (settings);

	options.max_version = TLS1_2_VERSION;
	server_check_resumption (server, &options);

	server_stop (server);

}

// returns whether a connection with the session resumed it
static bool server_resumes (const TlsServer *server, const RawTlsOptions *base, SSL_SESSION *session) {

	RawTlsOptions options = *base;
	options.session = session;

	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);

	const bool resumed = (SSL_session_reused (raw->ssl) == 1);
	test_check_unsigned_eq (raw_tls_request (raw, server_hello_request, NULL, 0), 200, NULL);
	raw_tls_close (raw);

	return resumed;

}

// a ticket of the key before the current one still resumes after a rotation, the cerver keeps two keys: one that
// is two rotations old is not resumed
static void test_server_ticket_rotation (const TestCertificates *certificates) {

	TlsServer *server = server_start (server_settings (certificates));

	// the response of a request orders the start of the cerver (its context) before the read of the test
	RawTlsOptions options = server_options (certificates);
	SSL_SESSION *session = server_session (server, &options);

	CryptoTlsContext *context = ((const CerverTls *) server->cerver->tls)->context;

	cerver_crypto_provider_tls_context_rotate_ticket_keys (context);
	test_check_true (server_resumes (server, &options, session));

	cerver_crypto_provider_tls_context_rotate_ticket_keys (context);
	test_check_false (server_resumes (server, &options, session));

	SSL_SESSION_free (session);

	server_stop (server);

}

static void test_server_early_data (const TestCertificates *certificates) {

	TlsSettings *settings = server_settings (certificates);
	tls_settings_set_early_data (settings, SERVER_EARLY_DATA_SIZE);

	TlsServer *server = server_start (settings);

	RawTlsOptions options = server_options (certificates);
	SSL_SESSION *session = server_session (server, &options);
	test_check_true (SSL_SESSION_get_max_early_data (session) > 0);

	// an idempotent request is answered from the early data
	options.session = session;
	options.early_data = server_hello_request;
	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);
	test_check_true (raw->early_accepted);
	test_check_unsigned_eq (raw_tls_response (raw, NULL, 0), 200, NULL);
	raw_tls_close (raw);

	SSL_SESSION_free (session);

	// the session of the second request comes from a full handshake, without the one that was released
	options.session = NULL;
	options.early_data = NULL;

	// any other request is answered with 425
	options.session = server_session (server, &options);
	options.early_data = "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\nhi";
	raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);
	test_check_unsigned_eq (raw_tls_response (raw, NULL, 0), 425, NULL);
	raw_tls_close (raw);

	SSL_SESSION_free (options.session);

	// an idempotent method with a body is not replayable either
	options.session = NULL;
	options.early_data = NULL;

	options.session = server_session (server, &options);
	options.early_data = "GET /hello HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\nhi";
	raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);
	test_check_unsigned_eq (raw_tls_response (raw, NULL, 0), 425, NULL);
	raw_tls_close (raw);

	SSL_SESSION_free (options.session);

	const TlsStats stats = server_stats (server);
	test_check_true (stats.early_data_accepted >= 1);
	test_check_true (stats.early_data_rejected >= 2);

	server_stop (server);

}

static void server_check_default_name (const TlsServer *server, const TestCertificates *certificates, const char *expected) {

	server_check_name (server, certificates, NULL, expected);

}

static void test_server_reload (const TestCertificates *certificates) {

	// the cerver reads copies of the files, that the test replaces like a renewal
	char chain[TEST_CERTIFICATE_PATH_SIZE + 16];
	char key[TEST_CERTIFICATE_PATH_SIZE + 16];
	(void) snprintf (chain, sizeof (chain), "%s/live.pem", certificates->directory);
	(void) snprintf (key, sizeof (key), "%s/live.key", certificates->directory);
	test_certificates_replace (certificates->server_chain, chain);
	test_certificates_replace (certificates->server_key, key);

	TlsSettings *settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_add_certificate (settings, chain, key), 0, NULL);

	TlsServer *server = server_start (settings);
	server_check_default_name (server, certificates, "localhost");

	// a connection that is alive keeps its session while the certificate changes
	RawTlsOptions options = server_options (certificates);
	RawTls *alive = raw_tls_connect (server->port, &options);
	test_check_true (alive->connected);

	test_certificates_replace (certificates->other_chain, chain);
	test_certificates_replace (certificates->other_key, key);
	test_check_unsigned_eq (cerver_tls_reload (server->cerver), 0, NULL);
	server_check_default_name (server, certificates, "other.test");

	test_check_unsigned_eq (raw_tls_request (alive, server_hello_request, NULL, 0), 200, NULL);
	raw_tls_close (alive);

	// a key that does not match keeps the previous certificate
	test_certificates_replace (certificates->mismatch_key, key);
	test_check_unsigned_eq (cerver_tls_reload (server->cerver), 1, NULL);
	server_check_default_name (server, certificates, "other.test");

	TlsStats stats = server_stats (server);
	test_check_unsigned_eq ((unsigned int) stats.reloads, 2, NULL);
	test_check_unsigned_eq ((unsigned int) stats.reload_failures, 1, NULL);
	test_check_true (stats.certificate_expiry_seconds > 0);

	server_stop (server);

	(void) unlink (chain);
	(void) unlink (key);

}

// the name of the default certificate of a new connection
static void server_default_name (const TlsServer *server, char *name, const size_t size) {

	RawTlsOptions options;
	(void) memset (&options, 0, sizeof (RawTlsOptions));

	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);
	raw_tls_peer_name (raw, name, size);
	raw_tls_close (raw);

}

// waits until the new connections get the certificate of the name
static bool server_wait_name (const TlsServer *server, const TestCertificates *certificates, const char *expected) {

	(void) certificates;

	char name[SERVER_NAME_SIZE] = { 0 };
	const double deadline = test_wait_now () + (double) (TEST_WAIT_SECONDS * test_time_scale ());

	server_default_name (server, name, sizeof (name));
	while (strcmp (name, expected) && (test_wait_now () < deadline)) {
		(void) usleep (1 << 14);
		server_default_name (server, name, sizeof (name));
	}

	return !strcmp (name, expected);

}

static void test_server_reload_signal_and_watch (const TestCertificates *certificates) {

	char chain[TEST_CERTIFICATE_PATH_SIZE + 16];
	char key[TEST_CERTIFICATE_PATH_SIZE + 16];
	(void) snprintf (chain, sizeof (chain), "%s/watched.pem", certificates->directory);
	(void) snprintf (key, sizeof (key), "%s/watched.key", certificates->directory);
	test_certificates_replace (certificates->server_chain, chain);
	test_certificates_replace (certificates->server_key, key);

	TlsSettings *settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_add_certificate (settings, chain, key), 0, NULL);

	TlsServer *server = (TlsServer *) calloc (1, sizeof (TlsServer));
	test_check_ptr (server);
	server->port = server_port++;
	server->cerver = server_create (server->port, settings);
	cerver_tls_set_reload_signal (server->cerver, SIGUSR1);
	cerver_tls_set_reload_watch (server->cerver, true, test_milliseconds (SERVER_WATCH_DEBOUNCE));
	test_check_int_eq (pthread_create (&server->thread, NULL, server_thread, server), 0, NULL);
	server_wait_listen (server->port);

	// the signal reloads in the thread of the TLS of the cerver
	test_check_int_eq (raise (SIGUSR1), 0, NULL);
	test_check_true (server_wait_stat (server, server_reloads, 1) >= 1);

	// a renewal of the files is reloaded once, after the debounce: every change of the folder (the temporary files,
	// the certificate that is renamed before its key) starts the debounce again, so the reload sees both files; a
	// reload at each change would read the new certificate with the old key, and fail
	test_certificates_replace (certificates->other_chain, chain);
	(void) usleep ((useconds_t) (test_milliseconds (SERVER_WATCH_DEBOUNCE) * SERVER_MICROSECONDS_PER_MILLISECOND / SERVER_WATCH_SPLIT));
	test_certificates_replace (certificates->other_key, key);
	test_check_true (server_wait_stat (server, server_reloads, 2) >= 2);
	test_check_true (server_wait_name (server, certificates, "other.test"));

	(void) usleep ((useconds_t) (test_milliseconds (SERVER_WATCH_DEBOUNCE) * SERVER_MICROSECONDS_PER_MILLISECOND * SERVER_WATCH_SETTLE));
	const TlsStats stats = server_stats (server);
	test_check_unsigned_eq ((unsigned int) stats.reloads, 2, NULL);
	test_check_unsigned_eq ((unsigned int) stats.reload_failures, 0, NULL);

	server_stop (server);

	(void) unlink (chain);
	(void) unlink (key);

}

// a cerver whose certificates do not validate never starts
static void server_check_start_fails (TlsSettings *settings) {

	Cerver *cerver = server_create (server_port++, settings);
	test_check_unsigned_eq (cerver_start (cerver), 1, NULL);
	(void) cerver_teardown (cerver);

}

static void test_server_start_errors (const TestCertificates *certificates) {

	TlsSettings *expired = tls_settings_create ();
	(void) tls_settings_add_certificate (expired, certificates->expired_chain, certificates->expired_key);
	server_check_start_fails (expired);

	TlsSettings *mismatch = tls_settings_create ();
	(void) tls_settings_add_certificate (mismatch, certificates->server_chain, certificates->mismatch_key);
	server_check_start_fails (mismatch);

	TlsSettings *duplicate = server_settings (certificates);
	(void) tls_settings_add_certificate (duplicate, certificates->server_chain, certificates->server_key);
	server_check_start_fails (duplicate);

	server_check_start_fails (tls_settings_create ());

	TlsSettings *missing = tls_settings_create ();
	(void) tls_settings_add_certificate (missing, "/nothing/here.pem", "/nothing/here.key");
	server_check_start_fails (missing);

	TlsSettings *ciphers = server_settings (certificates);
	(void) tls_settings_set_ciphers (ciphers, "TLS_NOT_A_CIPHER", NULL);
	server_check_start_fails (ciphers);

	// a cerver with an event loop has TLS since 2.1b-23
	Cerver *events = cerver_create_web ("tls-events", server_port++, CERVER_DEFAULT_CONNECTION_QUEUE);
	cerver_set_handler_type (events, CERVER_HANDLER_TYPE_EVENTS);
	test_check_unsigned_eq (cerver_set_tls (events, server_settings (certificates)), 0, NULL);
	test_check_true (cerver_has_tls (events));
	(void) cerver_teardown (events);

}

static void server_check_large (const TlsServer *server, const RawTlsOptions *options) {

	RawTls *raw = raw_tls_connect (server->port, options);
	size_t written = 0;
	test_check_int_eq (SSL_write_ex (raw->ssl, "GET /large HTTP/1.1\r\nHost: localhost\r\n\r\n", 40, &written), 1, NULL);

	// the headers and the body
	char *buffer = (char *) malloc (SERVER_LARGE_SIZE + (1 << 12));
	test_check_ptr (buffer);

	size_t size = 0;
	size_t read = 0;
	char *body = NULL;
	while ((!body || ((size_t) (buffer + size - body) < SERVER_LARGE_SIZE)) && (SSL_read_ex (raw->ssl, buffer + size, SERVER_LARGE_SIZE + (1 << 12) - size, &read) == 1)) {
		size += read;
		if (!body) {
			buffer[size] = '\0';
			char *end = (size > 4) ? (char *) memmem (buffer, size, "\r\n\r\n", 4) : NULL;
			if (end) body = end + 4;
		}
	}

	test_check_ptr (body);
	test_check_unsigned_eq ((unsigned int) (buffer + size - body), SERVER_LARGE_SIZE, NULL);
	test_check_int_eq (memcmp (body, server_large, SERVER_LARGE_SIZE), 0, NULL);

	free (buffer);
	raw_tls_close (raw);

}

static void test_server_large (const TestCertificates *certificates, const bool ktls) {

	TlsSettings *settings = server_settings (certificates);
	tls_settings_set_ktls (settings, ktls);
	TlsServer *server = server_start (settings);

	RawTlsOptions options = server_options (certificates);
	server_check_large (server, &options);

	options.max_version = TLS1_2_VERSION;
	server_check_large (server, &options);

	if (!ktls) test_check_unsigned_eq ((unsigned int) server_stats (server).ktls_send, 0, NULL);

	server_stop (server);

}

static void test_server_close_notify (const TestCertificates *certificates) {

	TlsServer *server = server_start (server_settings (certificates));

	RawTlsOptions options = server_options (certificates);
	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_unsigned_eq (raw_tls_request (raw, server_close_request, NULL, 0), 200, NULL);

	// the cerver closes with its close_notify: a clean end of the stream
	char byte = 0;
	size_t read = 0;
	int result = 0;
	do {
		result = SSL_read_ex (raw->ssl, &byte, 1, &read);
	} while (result == 1);
	test_check_int_eq (SSL_get_error (raw->ssl, result), SSL_ERROR_ZERO_RETURN, NULL);
	ERR_clear_error ();

	raw_tls_close (raw);

	server_stop (server);

}

static void test_server_handshake_timeout (const TestCertificates *certificates) {

	TlsSettings *settings = server_settings (certificates);
	tls_settings_set_handshake_timeout_milliseconds (settings, test_milliseconds (SERVER_SHORT_HANDSHAKE));
	TlsServer *server = server_start (settings);

	// a client that never sends its hello is closed after the deadline
	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (server->port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	test_check_int_eq (connect (fd, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	char byte = 0;
	test_check_true (recv (fd, &byte, 1, 0) <= 0);
	(void) close (fd);

	test_check_true (server_wait_stat (server, server_timeouts, 1) >= 1);

	server_stop (server);

}

// reads until the responses of the expected requests arrived, returns how many arrived
static unsigned int server_read_responses (RawTls *raw, const char *status_line, const unsigned int expected) {

	char *buffer = (char *) calloc (1, SERVER_BURST_BUFFER_SIZE);
	test_check_ptr (buffer);

	unsigned int found = 0;
	size_t size = 0;
	size_t read = 0;
	while ((found < expected) && (size < (SERVER_BURST_BUFFER_SIZE - 1))
		&& (SSL_read_ex (raw->ssl, buffer + size, SERVER_BURST_BUFFER_SIZE - 1 - size, &read) == 1)) {
		size += read;
		buffer[size] = '\0';

		found = 0;
		for (const char *status = strstr (buffer, status_line); status; status = strstr (status + 1, status_line)) {
			found += 1;
		}
	}

	free (buffer);
	ERR_clear_error ();

	return found;

}

// writes the same request many times, each one in its own record, before reading any response
static void server_write_burst (RawTls *raw, const char *request) {

	const size_t length = strlen (request);
	for (unsigned int index = 0; index < SERVER_BURST_REQUESTS; index++) {
		size_t written = 0;
		test_check_int_eq (SSL_write_ex (raw->ssl, request, length, &written), 1, NULL);
	}

}

// like server_write_burst (), with each request split in two records: the first one leaves its headers incomplete
static void server_write_split_burst (RawTls *raw, const char *request) {

	const size_t length = strlen (request);
	const size_t half = length / 2;
	for (unsigned int index = 0; index < SERVER_BURST_REQUESTS; index++) {
		size_t written = 0;
		test_check_int_eq (SSL_write_ex (raw->ssl, request, half, &written), 1, NULL);
		test_check_int_eq (SSL_write_ex (raw->ssl, request + half, length - half, &written), 1, NULL);
	}

}

// a response that never comes fails the read instead of waiting for the alarm
static void server_read_timeout (const RawTls *raw) {

	struct timeval timeout = { (time_t) (TEST_WAIT_SECONDS * test_time_scale ()), 0 };
	test_check_int_eq (setsockopt (raw->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout)), 0, NULL);

}

// requests that come together, each one in its own record, are all answered: with the event loop the records
// that one read brought wait in the TLS of the connection, and its poller does not report them again;
// the loop answers the ones of the default route by itself, the pool the ones of a handler
static void test_server_burst (const TestCertificates *certificates) {

	TlsServer *server = server_start_kept (server_settings (certificates), true);

	RawTlsOptions options = server_options (certificates);
	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);
	server_read_timeout (raw);

	server_write_burst (raw, server_default_request);
	test_check_unsigned_eq (server_read_responses (raw, server_ok_status, SERVER_BURST_REQUESTS), SERVER_BURST_REQUESTS, NULL);

	server_write_burst (raw, server_hello_request);
	test_check_unsigned_eq (server_read_responses (raw, server_ok_status, SERVER_BURST_REQUESTS), SERVER_BURST_REQUESTS, NULL);

	server_write_split_burst (raw, server_default_request);
	test_check_unsigned_eq (server_read_responses (raw, server_ok_status, SERVER_BURST_REQUESTS), SERVER_BURST_REQUESTS, NULL);

	server_write_split_burst (raw, server_hello_request);
	test_check_unsigned_eq (server_read_responses (raw, server_ok_status, SERVER_BURST_REQUESTS), SERVER_BURST_REQUESTS, NULL);

	// and the connection is still kept
	test_check_unsigned_eq (raw_tls_request (raw, server_hello_request, NULL, 0), 200, NULL);

	raw_tls_close (raw);

	server_stop (server);

}

// a client that never ends its handshake does not hold the others: the loop waits for its records in its poller
static void test_server_handshake_waits (const TestCertificates *certificates) {

	TlsServer *server = server_start (server_settings (certificates));

	const int silent = socket (AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (server->port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	test_check_int_eq (connect (silent, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	// half of a record header
	test_check_int_eq ((int) send (silent, server_partial_record, sizeof (server_partial_record), 0), (int) sizeof (server_partial_record), NULL);

	RawTlsOptions options = server_options (certificates);
	test_check_unsigned_eq (server_status (server, &options, server_hello_request, NULL), 200, NULL);

	(void) close (silent);

	server_stop (server);

}

// a client of TLS 1.3 that sends its hello and never reads, with the smallest socket: the flight of the cerver with
// the large certificate does not fit in it, it still waits in the output when the provider gives the key to the kernel
static RawTls *server_stuck_connect (const u16 port) {

	RawTls *raw = (RawTls *) calloc (1, sizeof (RawTls));
	test_check_ptr (raw);

	raw->fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (raw->fd >= 0, NULL);

	const int buffer = SERVER_STUCK_RECEIVE_BUFFER;
	test_check_int_eq (setsockopt (raw->fd, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof (buffer)), 0, NULL);
	const int segment = SERVER_STUCK_SEGMENT;
	test_check_int_eq (setsockopt (raw->fd, IPPROTO_TCP, TCP_MAXSEG, &segment, sizeof (segment)), 0, NULL);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	test_check_int_eq (connect (raw->fd, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	raw->ctx = SSL_CTX_new (TLS_client_method ());
	test_check_ptr (raw->ctx);
	test_check_int_eq (SSL_CTX_set_min_proto_version (raw->ctx, TLS1_3_VERSION), 1, NULL);
	raw->ssl = SSL_new (raw->ctx);
	test_check_ptr (raw->ssl);
	test_check_int_eq (SSL_set_fd (raw->ssl, raw->fd), 1, NULL);

	// the hello goes, and the handshake waits for an answer that is never read
	test_check_int_eq (fcntl (raw->fd, F_SETFL, fcntl (raw->fd, F_GETFL) | O_NONBLOCK), 0, NULL);
	test_check_int_eq (SSL_get_error (raw->ssl, SSL_connect (raw->ssl)), SSL_ERROR_WANT_READ, NULL);
	ERR_clear_error ();

	return raw;

}

// with kTLS, the records of the handshake that wait when the provider gives the key to the kernel are sent first:
// the event loop never waits for the socket to take them, so the clients that do not read do not hold the others
// (it waited up to TLS_CLOSE_TIMEOUT_MILLISECONDS for each one)
static void test_server_ktls_waits (const TestCertificates *certificates) {

	TlsSettings *settings = tls_settings_create ();
	test_check_ptr (settings);
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->large_chain, certificates->large_key), 0, NULL);
	tls_settings_set_ktls (settings, true);
	TlsServer *server = server_start (settings);

	const unsigned int count = SERVER_STUCK_CLIENTS * test_time_scale ();
	RawTls **stuck = (RawTls **) calloc (count, sizeof (RawTls *));
	test_check_ptr (stuck);
	for (unsigned int index = 0; index < count; index++) stuck[index] = server_stuck_connect (server->port);

	(void) usleep ((useconds_t) (test_milliseconds (SERVER_STUCK_PAUSE_MILLISECONDS) * SERVER_MICROSECONDS_PER_MILLISECOND));

	const double start = test_wait_now ();
	RawTlsOptions options = server_options (certificates);
	test_check_unsigned_eq (server_status (server, &options, server_hello_request, NULL), 200, NULL);
	const unsigned int elapsed = (unsigned int) ((test_wait_now () - start) * SERVER_MILLISECONDS_PER_SECOND);

	(void) printf ("\t\tkTLS with %u stuck clients: answered in %u ms\n", count, elapsed);
	test_check_true (elapsed < ((count * TLS_CLOSE_TIMEOUT_MILLISECONDS) / SERVER_STUCK_WAIT_PART));

	for (unsigned int index = 0; index < count; index++) raw_tls_close (stuck[index]);
	free (stuck);

	server_stop (server);

}

// a TCP connection that sends nothing: with threads it holds the thread of its handshake until the deadline
static int server_connect_silent (const u16 port) {

	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	test_check_int_eq (connect (fd, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	return fd;

}

static u64 server_rejected (const TlsStats *stats) { return stats->failures[TLS_FAILURE_REJECTED]; }

static u64 server_closed (const TlsStats *stats) { return stats->failures[TLS_FAILURE_CLOSED]; }

// the encrypted bytes that wait in the output of a connection
static size_t server_tls_waiting (TlsConnection *tls) {

	(void) pthread_mutex_lock (&tls->mutex);
	const size_t waiting = tls->output.end - tls->output.start;
	(void) pthread_mutex_unlock (&tls->mutex);

	return waiting;

}

// a sender encrypts up to the mark of TLS_OUTPUT_FLUSH_MARK before it sends: with a client that does not read, what
// waits encrypted never passes the mark by more than a record, the rest of the response is not encrypted yet
static void test_server_flush_mark (const TestCertificates *certificates) {

	TlsServer *server = server_start (server_settings (certificates));

	RawTlsOptions options = server_options (certificates);
	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);
	server_read_timeout (raw);

	size_t written = 0;
	test_check_int_eq (SSL_write_ex (raw->ssl, server_tracked_request, strlen (server_tracked_request), &written), 1, NULL);

	TlsConnection *tls = NULL;
	while (!(tls = __atomic_load_n (&server_tracked_tls, __ATOMIC_SEQ_CST))) (void) usleep (1 << 10);

	size_t most = 0;
	for (unsigned int sample = 0; sample < SERVER_MARK_SAMPLES; sample++) {
		const size_t waiting = server_tls_waiting (tls);
		if (waiting > most) most = waiting;
		(void) usleep (1 << 12);
	}

	test_check_true (most <= SERVER_MARK_LIMIT);

	// the whole response still arrives
	char *buffer = (char *) malloc (SERVER_LARGE_SIZE);
	test_check_ptr (buffer);
	size_t total = 0;
	size_t read = 0;
	while ((total < SERVER_LARGE_SIZE) && (SSL_read_ex (raw->ssl, buffer, SERVER_LARGE_SIZE, &read) == 1)) total += read;
	test_check_true (total >= SERVER_LARGE_SIZE);
	free (buffer);

	raw_tls_close (raw);

	server_stop (server);

}

// a certificate that expires before TLS_CERTIFICATE_EXPIRY_WARNING_DAYS is a warning of the start of its cerver,
// that goes to stderr: the test reads it from a file while the cerver starts
static void test_server_expiry_warning (const TestCertificates *certificates) {

	char path[] = SERVER_LOG_TEMPLATE;
	const int log = mkstemp (path);
	test_check_int_gt (log, -1);

	const int saved = dup (STDERR_FILENO);
	test_check_int_eq (dup2 (log, STDERR_FILENO), STDERR_FILENO, NULL);

	TlsSettings *settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->expiring_chain, certificates->expiring_key), 0, NULL);
	TlsServer *server = server_start (settings);

	test_check_int_eq (dup2 (saved, STDERR_FILENO), STDERR_FILENO, NULL);
	(void) close (saved);

	char logged[SERVER_LOG_SIZE] = { 0 };
	const ssize_t logged_size = pread (log, logged, sizeof (logged) - 1, 0);
	test_check_true (logged_size > 0);
	test_check_ptr (strstr (logged, server_expiry_warning));

	(void) close (log);
	(void) unlink (path);

	TlsStats stats = server_stats (server);
	test_check_true ((stats.certificate_expiry_seconds > 0) && (stats.certificate_expiry_seconds < SERVER_EXPIRY_WARNING_SECONDS));

	server_stop (server);

}

// a full web cerver with threads closes a connection without its handshake: a 503 in plain would be the first bytes
// of a TLS stream; the thread runs the handshake of a silent client, and another one waits in the queue
static void test_server_full (const TestCertificates *certificates) {

	TlsServer *server = (TlsServer *) calloc (1, sizeof (TlsServer));
	test_check_ptr (server);
	server->port = server_port++;
	server->cerver = server_create (server->port, server_settings (certificates));
	cerver_set_thpool_n_threads (server->cerver, SERVER_FULL_THREADS);
	cerver_set_thpool_queue_size (server->cerver, SERVER_FULL_QUEUE);
	cerver_set_overload_policy (server->cerver, CERVER_OVERLOAD_POLICY_REJECT);
	test_check_int_eq (pthread_create (&server->thread, NULL, server_thread, server), 0, NULL);
	server_wait_listen (server->port);

	// the thread is free again once the connection of the wait for the listen ended its handshake (it closed); the
	// test never reads the thpool, that the thread of the cerver made
	test_check_true (server_wait_stat (server, server_closed, 1) >= 1);

	// one handshake in the thread, one in the queue: whatever place each one takes, the third one has none
	const int working = server_connect_silent (server->port);
	(void) usleep ((useconds_t) (test_milliseconds (SERVER_FULL_PAUSE_MILLISECONDS) * SERVER_MICROSECONDS_PER_MILLISECOND));

	const int queued = server_connect_silent (server->port);
	(void) usleep ((useconds_t) (test_milliseconds (SERVER_FULL_PAUSE_MILLISECONDS) * SERVER_MICROSECONDS_PER_MILLISECOND));

	const int rejected = server_connect_silent (server->port);
	char response[SERVER_NAME_SIZE] = { 0 };
	test_check_true (recv (rejected, response, sizeof (response) - 1, 0) <= 0);
	test_check_null_ptr (strstr (response, "503"));
	(void) close (rejected);

	test_check_true (server_wait_stat (server, server_rejected, 1) >= 1);

	(void) close (queued);
	(void) close (working);

	server_stop (server);

}

#pragma endregion

static void server_tests (const TestCertificates *certificates) {

	test_server_versions (certificates);
	test_server_ciphers (certificates);
	test_server_sni (certificates);
	test_server_alpn (certificates);
	test_server_mtls_required (certificates);
	test_server_mtls_optional (certificates);
	test_server_resumption (certificates);
	test_server_ticket_rotation (certificates);
	test_server_early_data (certificates);
	test_server_reload (certificates);
	test_server_reload_signal_and_watch (certificates);
	test_server_start_errors (certificates);
	test_server_large (certificates, false);
	test_server_large (certificates, true);
	test_server_close_notify (certificates);
	test_server_handshake_timeout (certificates);
	test_server_burst (certificates);
	test_server_handshake_waits (certificates);

	// with threads, the thread of each handshake waits for its own socket
	if (server_handler_type == CERVER_HANDLER_TYPE_EVENTS) test_server_ktls_waits (certificates);

	// an event loop stops accepting at its limit of connections, it never rejects one
	if (server_handler_type == CERVER_HANDLER_TYPE_THREADS) test_server_full (certificates);

	test_server_expiry_warning (certificates);
	test_server_flush_mark (certificates);

}

void tls_tests_server (const TestCertificates *certificates) {

	(void) printf ("Testing TLS server...\n");

	server_large = (char *) malloc (SERVER_LARGE_SIZE);
	test_check_ptr (server_large);
	for (size_t index = 0; index < SERVER_LARGE_SIZE; index++) server_large[index] = (char) ('a' + (index % 26));

	server_handler_type = CERVER_HANDLER_TYPE_THREADS;
	server_tests (certificates);

	server_handler_type = CERVER_HANDLER_TYPE_EVENTS;
	server_tests (certificates);

	free (server_large);

	(void) printf ("Done!\n");

}
