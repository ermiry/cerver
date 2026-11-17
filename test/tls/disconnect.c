#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/tls/cerver.h>
#include <cerver/tls/tls.h>

#include "../test.h"
#include "tls.h"

// the TLS variants of test/http/disconnect.c: raw clients of a web cerver with TLS leave in the middle of
// the handshake (cut at each place of the hello) and in the middle of the first record of a request, in four ways
// (a close, a reset, a half-close and vanishing); after each batch the failures of the TLS change exactly
// (a client that vanishes in the handshake is a TIMEOUT, every other one is not), no request is handled,
// the fds are the ones that the cerver had, and a whole request is still answered

// the ports after the ones of the cervers test
#define DISCONNECT_TLS_PORT_BASE			(TLS_TEST_PORT_BASE + (1 << 9) + (1 << 6))

#define DISCONNECT_TLS_THREADS				8

// the deadlines of the cerver, short so a client that vanishes is closed by them
#define DISCONNECT_TLS_HANDSHAKE_TIMEOUT	(1 << 9)
#define DISCONNECT_TLS_IDLE_TIMEOUT			(1 << 9)

#define DISCONNECT_TLS_FD_DIRECTORY			"/proc/self/fd"

#define DISCONNECT_TLS_HELLO_SIZE			(1 << 12)
#define DISCONNECT_TLS_RECORD_HEADER_SIZE	5
#define DISCONNECT_TLS_HANDSHAKE_HEADER_SIZE	4

// how long a client that vanishes or half-closed waits for the cerver to close it
#define DISCONNECT_TLS_CLOSE_SECONDS		8

#define DISCONNECT_TLS_BODY_SIZE			(1 << 8)

static const char *disconnect_tls_request = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";

typedef enum DisconnectTlsForm {

	DISCONNECT_TLS_FORM_CLOSE		= 0,
	DISCONNECT_TLS_FORM_RESET		= 1,
	DISCONNECT_TLS_FORM_HALF_CLOSE	= 2,
	DISCONNECT_TLS_FORM_VANISH		= 3,

	DISCONNECT_TLS_FORMS			= 4

} DisconnectTlsForm;

static const char *disconnect_tls_form_names[DISCONNECT_TLS_FORMS] = { "close", "reset", "half-close", "vanish" };

typedef struct DisconnectTlsServer {

	Cerver *cerver;
	pthread_t thread;
	u16 port;

} DisconnectTlsServer;

static u16 disconnect_tls_port = DISCONNECT_TLS_PORT_BASE;

static unsigned int disconnect_tls_handled = 0;

#pragma region server

static void disconnect_tls_hello (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;

	test_counter_add (&disconnect_tls_handled);
	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, "hello");

}

static void *disconnect_tls_server_thread (void *server_ptr) {

	DisconnectTlsServer *server = (DisconnectTlsServer *) server_ptr;
	(void) cerver_start (server->cerver);

	return NULL;

}

static TlsSettings *disconnect_tls_settings (const TestCertificates *certificates) {

	TlsSettings *settings = tls_settings_create ();
	test_check_ptr (settings);

	test_check_unsigned_eq (
		tls_settings_add_certificate (settings, certificates->server_chain, certificates->server_key), 0, NULL
	);

	tls_settings_set_handshake_timeout_milliseconds (settings, test_milliseconds (DISCONNECT_TLS_HANDSHAKE_TIMEOUT));

	return settings;

}

static void disconnect_tls_wait_listen (const u16 port);

static void disconnect_tls_server_start (DisconnectTlsServer *server, const TestCertificates *certificates) {

	server->port = disconnect_tls_port++;

	server->cerver = cerver_create_web ("tls-disconnect", server->port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (server->cerver);

	cerver_set_handler_type (server->cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (server->cerver, true);
	cerver_set_thpool_n_threads (server->cerver, DISCONNECT_TLS_THREADS);

	HttpCerver *http_cerver = (HttpCerver *) server->cerver->cerver_data;
	http_cerver_set_idle_timeout_milliseconds (http_cerver, test_milliseconds (DISCONNECT_TLS_IDLE_TIMEOUT));

	HttpGroup *root = http_cerver_group_create (http_cerver, "");
	(void) http_group_get (root, "hello", disconnect_tls_hello);

	test_check_unsigned_eq (cerver_set_tls (server->cerver, disconnect_tls_settings (certificates)), 0, NULL);

	test_check_int_eq (pthread_create (&server->thread, NULL, disconnect_tls_server_thread, server), 0, NULL);
	disconnect_tls_wait_listen (server->port);

}

static void disconnect_tls_server_stop (DisconnectTlsServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

}

static u64 disconnect_tls_failures (const DisconnectTlsServer *server, const TlsFailure failure) {

	TlsStats stats;
	test_check_unsigned_eq (cerver_tls_get_stats (server->cerver, &stats), 0, NULL);

	return stats.failures[failure];

}

static u64 disconnect_tls_failures_total (const DisconnectTlsServer *server) {

	TlsStats stats;
	test_check_unsigned_eq (cerver_tls_get_stats (server->cerver, &stats), 0, NULL);

	u64 total = 0;
	for (unsigned int index = 0; index < TLS_FAILURE_COUNT; index++) total += stats.failures[index];

	return total;

}

static u64 disconnect_tls_handshakes (const DisconnectTlsServer *server) {

	TlsStats stats;
	test_check_unsigned_eq (cerver_tls_get_stats (server->cerver, &stats), 0, NULL);

	return stats.handshakes;

}

// the handshake of a client of TLS 1.3 ends when it sent its Finished, the one of the cerver when it read it:
// a reset before that discards the Finished, and the handshake of the cerver fails (SYSTEM)
static void disconnect_tls_wait_handshakes (const DisconnectTlsServer *server, const u64 expected) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	u64 handshakes = disconnect_tls_handshakes (server);
	while ((handshakes < expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		handshakes = disconnect_tls_handshakes (server);
	}

	test_check_unsigned_eq ((unsigned int) handshakes, (unsigned int) expected, "the cerver did not end the handshake");

}

#pragma endregion

#pragma region sockets

static int disconnect_tls_connect (const u16 port) {

	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check_int_gt (fd, -1);

	const struct timeval timeout = { DISCONNECT_TLS_CLOSE_SECONDS * (time_t) test_time_scale (), 0 };
	(void) setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	test_check_int_eq (connect (fd, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	return fd;

}

static void disconnect_tls_wait_listen (const u16 port) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	bool listening = false;
	while (!listening && (test_wait_now () < deadline)) {
		const int fd = socket (AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in address;
		(void) memset (&address, 0, sizeof (address));
		address.sin_family = AF_INET;
		address.sin_port = htons (port);
		address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

		listening = !connect (fd, (struct sockaddr *) &address, sizeof (address));
		(void) close (fd);

		if (!listening) (void) nanosleep (&interval, NULL);
	}

	test_check_true (listening);

}

static void disconnect_tls_send (const int fd, const unsigned char *data, const size_t size) {

	size_t sent = 0;
	while (sent < size) {
		const ssize_t result = send (fd, data + sent, size - sent, MSG_NOSIGNAL);
		test_check_int_gt ((int) result, 0);
		sent += (size_t) result;
	}

}

// a client that half-closed or vanished waits for the close of the cerver (its deadline or its idle timeout)
static void disconnect_tls_wait_close (const int fd) {

	unsigned char byte = 0;
	ssize_t result = 1;
	while (result > 0) result = recv (fd, &byte, sizeof (byte), 0);

	test_check_int_eq ((int) result, 0, "the cerver did not close the connection");

}

static void disconnect_tls_reset (const int fd) {

	const struct linger linger = { 1, 0 };
	(void) setsockopt (fd, SOL_SOCKET, SO_LINGER, &linger, sizeof (struct linger));
	(void) close (fd);

}

static void disconnect_tls_leave (const int fd, const DisconnectTlsForm form) {

	switch (form) {
		case DISCONNECT_TLS_FORM_CLOSE: (void) close (fd); break;
		case DISCONNECT_TLS_FORM_RESET: disconnect_tls_reset (fd); break;

		case DISCONNECT_TLS_FORM_HALF_CLOSE:
			(void) shutdown (fd, SHUT_WR);
			disconnect_tls_wait_close (fd);
			(void) close (fd);
			break;

		case DISCONNECT_TLS_FORM_VANISH:
			disconnect_tls_wait_close (fd);
			(void) close (fd);
			break;

		default: break;
	}

}

static size_t disconnect_tls_open_fds (void) {

	DIR *directory = opendir (DISCONNECT_TLS_FD_DIRECTORY);
	test_check_ptr (directory);

	size_t count = 0;
	const struct dirent *entry = NULL;
	while ((entry = readdir (directory))) {
		if (strcmp (entry->d_name, ".") && strcmp (entry->d_name, "..")) count += 1;
	}

	(void) closedir (directory);

	return count;

}

// the cerver closes its sockets after the clients left, in its own threads; the fds that were counted before
// can include the one of the last request, that the cerver closes when it wants
static void disconnect_tls_wait_fds (const size_t expected) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	size_t fds = disconnect_tls_open_fds ();
	while ((fds > expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		fds = disconnect_tls_open_fds ();
	}

	test_check (fds <= expected, "the cerver kept fds of the clients that left");

}

static void disconnect_tls_wait_failures (
	const DisconnectTlsServer *server, const u64 expected
) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	u64 failures = disconnect_tls_failures_total (server);
	while ((failures < expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		failures = disconnect_tls_failures_total (server);
	}

	test_check_unsigned_eq ((unsigned int) failures, (unsigned int) expected, "the handshakes that were cut");

}

#pragma endregion

#pragma region records

// the first flight of a client of OpenSSL, made with memory BIOs: a record with the hello
static size_t disconnect_tls_hello_create (unsigned char *hello, const size_t size) {

	SSL_CTX *ctx = SSL_CTX_new (TLS_client_method ());
	test_check_ptr (ctx);

	SSL *ssl = SSL_new (ctx);
	test_check_ptr (ssl);

	BIO *input = BIO_new (BIO_s_mem ());
	BIO *output = BIO_new (BIO_s_mem ());
	test_check_ptr (input);
	test_check_ptr (output);
	SSL_set_bio (ssl, input, output);

	(void) SSL_set_tlsext_host_name (ssl, "localhost");
	SSL_set_connect_state (ssl);
	(void) SSL_do_handshake (ssl);

	const int length = BIO_read (output, hello, (int) size);
	test_check_int_gt (length, DISCONNECT_TLS_RECORD_HEADER_SIZE + DISCONNECT_TLS_HANDSHAKE_HEADER_SIZE);

	SSL_free (ssl);
	SSL_CTX_free (ctx);
	ERR_clear_error ();

	return (size_t) length;

}

// the record of a request of a client that made its handshake, written to a memory BIO instead of its socket
static size_t disconnect_tls_request_record (RawTls *raw, unsigned char *record, const size_t size) {

	BIO *output = BIO_new (BIO_s_mem ());
	test_check_ptr (output);

	// the BIO of the socket stays as the one that is read: SSL_set_fd () gave the session a reference for each side,
	// and SSL_set0_wbio () releases the one of the writes
	SSL_set0_wbio (raw->ssl, output);

	size_t written = 0;
	test_check_int_eq (
		SSL_write_ex (raw->ssl, disconnect_tls_request, strlen (disconnect_tls_request), &written), 1, NULL
	);

	const int length = BIO_read (output, record, (int) size);
	test_check_int_gt (length, DISCONNECT_TLS_RECORD_HEADER_SIZE);

	return (size_t) length;

}

#pragma endregion

#pragma region tests

static void disconnect_tls_check_answers (const DisconnectTlsServer *server, const TestCertificates *certificates) {

	const RawTlsOptions options = { 0, 0, NULL, NULL, NULL, 0, certificates->authority, NULL, NULL, NULL, NULL };

	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);

	char body[DISCONNECT_TLS_BODY_SIZE];
	test_check_unsigned_eq (
		raw_tls_request (raw, disconnect_tls_request, body, sizeof (body)), (unsigned int) HTTP_STATUS_OK, NULL
	);

	raw_tls_close (raw);

}

// cuts the hello at the offset and leaves: every one is a failure of its handshake
static void disconnect_tls_cut_hello (
	const DisconnectTlsServer *server,
	const unsigned char *hello, const size_t offset, const DisconnectTlsForm form
) {

	const u64 failures = disconnect_tls_failures_total (server);
	const u64 timeouts = disconnect_tls_failures (server, TLS_FAILURE_TIMEOUT);

	const int fd = disconnect_tls_connect (server->port);
	if (offset) disconnect_tls_send (fd, hello, offset);
	disconnect_tls_leave (fd, form);

	disconnect_tls_wait_failures (server, failures + 1);

	const u64 expected_timeouts = timeouts + ((form == DISCONNECT_TLS_FORM_VANISH) ? 1 : 0);
	test_check_unsigned_eq (
		(unsigned int) disconnect_tls_failures (server, TLS_FAILURE_TIMEOUT), (unsigned int) expected_timeouts,
		disconnect_tls_form_names[form]
	);

}

static void test_disconnect_tls_handshake (const TestCertificates *certificates) {

	DisconnectTlsServer server;
	disconnect_tls_server_start (&server, certificates);
	disconnect_tls_check_answers (&server, certificates);

	const size_t fds = disconnect_tls_open_fds ();

	unsigned char hello[DISCONNECT_TLS_HELLO_SIZE];
	const size_t hello_size = disconnect_tls_hello_create (hello, sizeof (hello));

	// nothing, a part of the header of the record, the header, the header of the hello, half and all but a byte
	const size_t offsets[] = {
		0, 1, DISCONNECT_TLS_RECORD_HEADER_SIZE,
		DISCONNECT_TLS_RECORD_HEADER_SIZE + DISCONNECT_TLS_HANDSHAKE_HEADER_SIZE,
		hello_size / 2, hello_size - 1
	};

	const unsigned int handled = __atomic_load_n (&disconnect_tls_handled, __ATOMIC_SEQ_CST);
	for (size_t index = 0; index < (sizeof (offsets) / sizeof (offsets[0])); index++) {
		for (unsigned int form = 0; form < DISCONNECT_TLS_FORMS; form++) {
			disconnect_tls_cut_hello (&server, hello, offsets[index], (DisconnectTlsForm) form);
		}
	}

	disconnect_tls_wait_fds (fds);
	test_check_unsigned_eq (__atomic_load_n (&disconnect_tls_handled, __ATOMIC_SEQ_CST), handled, NULL);

	disconnect_tls_check_answers (&server, certificates);

	disconnect_tls_server_stop (&server);

}

// the handshake ended, the request is cut inside its record: the cerver never decrypts a part of a record,
// so no request is handled, and the connection ends with the client (or with the idle timeout)
static void disconnect_tls_cut_record (
	const DisconnectTlsServer *server, const TestCertificates *certificates,
	const size_t cut, const DisconnectTlsForm form
) {

	const RawTlsOptions options = { 0, 0, NULL, NULL, NULL, 0, certificates->authority, NULL, NULL, NULL, NULL };

	const u64 handshakes = disconnect_tls_handshakes (server);

	RawTls *raw = raw_tls_connect (server->port, &options);
	test_check_true (raw->connected);

	disconnect_tls_wait_handshakes (server, handshakes + 1);

	unsigned char record[DISCONNECT_TLS_HELLO_SIZE];
	const size_t record_size = disconnect_tls_request_record (raw, record, sizeof (record));

	// the places of the cut are given from the end of the record, so they fit in any size
	const size_t offset = (cut < record_size) ? (record_size - cut) : 1;
	disconnect_tls_send (raw->fd, record, offset);
	disconnect_tls_leave (raw->fd, form);

	// the leave closed the fd, only the memory of OpenSSL is released
	SSL_free (raw->ssl);
	SSL_CTX_free (raw->ctx);
	ERR_clear_error ();
	free (raw);

}

static void test_disconnect_tls_record (const TestCertificates *certificates) {

	DisconnectTlsServer server;
	disconnect_tls_server_start (&server, certificates);
	disconnect_tls_check_answers (&server, certificates);

	const size_t fds = disconnect_tls_open_fds ();
	const u64 failures = disconnect_tls_failures_total (&server);

	// all but a byte, all but the tag, half of a small record and only the header of the record
	const size_t cuts[] = { 1, 16, 32, 48 };

	const unsigned int handled = __atomic_load_n (&disconnect_tls_handled, __ATOMIC_SEQ_CST);
	for (size_t index = 0; index < (sizeof (cuts) / sizeof (cuts[0])); index++) {
		for (unsigned int form = 0; form < DISCONNECT_TLS_FORMS; form++) {
			disconnect_tls_cut_record (&server, certificates, cuts[index], (DisconnectTlsForm) form);
		}
	}

	disconnect_tls_wait_fds (fds);
	test_check_unsigned_eq (__atomic_load_n (&disconnect_tls_handled, __ATOMIC_SEQ_CST), handled, NULL);

	// the handshakes ended, a cut after them is not a failure of the TLS
	test_check_unsigned_eq ((unsigned int) disconnect_tls_failures_total (&server), (unsigned int) failures, NULL);

	disconnect_tls_check_answers (&server, certificates);

	disconnect_tls_server_stop (&server);

}

#pragma endregion

void tls_tests_disconnect (const TestCertificates *certificates) {

	(void) printf ("Testing TLS disconnect...\n");

	(void) printf ("\ttest_disconnect_tls_handshake\n");
	test_disconnect_tls_handshake (certificates);

	(void) printf ("\ttest_disconnect_tls_record\n");
	test_disconnect_tls_record (certificates);

	(void) printf ("Done!\n");

}
