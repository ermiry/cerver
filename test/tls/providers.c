#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/ssl.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/tls/cerver.h>
#include <cerver/tls/tls.h>

#include "../test.h"
#include "tls.h"

// the ports after the ones of the client test
#define PROVIDERS_PORT_BASE				(TLS_TEST_PORT_BASE + (1 << 8) + (1 << 6))

#define PROVIDERS_THREADS				2

#define PROVIDERS_COMMAND_SIZE			(1 << 11)
#define PROVIDERS_PATH_SIZE				(TEST_CERTIFICATE_PATH_SIZE + 32)

// the responder of OCSP answers these requests before it ends
#define PROVIDERS_OCSP_REQUESTS			8

// the microseconds that the responder takes to listen, and the times that it is asked again
#define PROVIDERS_RESPONDER_WAIT		(1 << 18)
#define PROVIDERS_OCSP_ATTEMPTS			8

// the tools that a part of the test needs, it is skipped without them
#define PROVIDERS_OPENSSL				"/usr/bin/openssl"
#define PROVIDERS_SOFTHSM_UTIL			"/usr/bin/softhsm2-util"
#define PROVIDERS_SOFTHSM_MODULE		"/usr/lib/softhsm/libsofthsm2.so"

// pkcs11-provider opens the module of the token with RTLD_DEEPBIND, and ASan and TSan end the process on it
#if defined (__SANITIZE_THREAD__) || defined (__SANITIZE_ADDRESS__)
	#define PROVIDERS_SANITIZER			true
#elif defined (__has_feature)
	#if __has_feature (thread_sanitizer) || __has_feature (address_sanitizer)
		#define PROVIDERS_SANITIZER		true
	#endif
#endif

#ifndef PROVIDERS_SANITIZER
	#define PROVIDERS_SANITIZER			false
#endif

#define PROVIDERS_PIN					"123456"

static const char *providers_hello_request = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";

typedef struct ProvidersServer {

	Cerver *cerver;
	pthread_t thread;
	u16 port;

} ProvidersServer;

static void providers_hello (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;
	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, "hello");

}

static void *providers_thread (void *cerver_ptr) {

	(void) cerver_start ((Cerver *) cerver_ptr);

	return NULL;

}

static void providers_wait_listen (const u16 port) {

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

static ProvidersServer providers_start (const u16 port, TlsSettings *settings) {

	ProvidersServer server;
	(void) memset (&server, 0, sizeof (ProvidersServer));
	server.port = port;

	server.cerver = cerver_create_web ("tls-providers", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (server.cerver);
	cerver_set_handler_type (server.cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (server.cerver, true);
	cerver_set_thpool_n_threads (server.cerver, PROVIDERS_THREADS);

	HttpGroup *root = http_cerver_group_create ((HttpCerver *) server.cerver->cerver_data, "");
	(void) http_group_get (root, "hello", providers_hello);

	test_check_unsigned_eq (cerver_set_tls (server.cerver, settings), 0, NULL);

	test_check_int_eq (pthread_create (&server.thread, NULL, providers_thread, server.cerver), 0, NULL);
	providers_wait_listen (port);

	return server;

}

static void providers_stop (ProvidersServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

}

static bool providers_has (const char *path) {

	return !access (path, X_OK) || !access (path, R_OK);

}

#pragma region ocsp

// the index of the responder: the certificate of the server (serial 2) is valid
static void providers_ocsp_index (const char *path) {

	FILE *file = fopen (path, "w");
	test_check_ptr (file);
	(void) fprintf (file, "V\t491231235959Z\t\t02\tunknown\t/CN=localhost\n");
	(void) fclose (file);

}

static pid_t providers_ocsp_responder (const TestCertificates *certificates, const char *index, const u16 port) {

	char address[PROVIDERS_COMMAND_SIZE];
	(void) snprintf (address, sizeof (address), "%u", (unsigned int) port);

	char requests[PROVIDERS_COMMAND_SIZE];
	(void) snprintf (requests, sizeof (requests), "%d", PROVIDERS_OCSP_REQUESTS);

	const pid_t pid = fork ();
	test_check (pid >= 0, NULL);

	if (!pid) {
		// a test that fails never leaves the responder listening on its port
		(void) prctl (PR_SET_PDEATHSIG, SIGTERM);

		const int null_fd = open ("/dev/null", 0);
		if (null_fd >= 0) { (void) dup2 (null_fd, STDOUT_FILENO); (void) dup2 (null_fd, STDERR_FILENO); }

		(void) execl (
			PROVIDERS_OPENSSL, "openssl", "ocsp", "-index", index, "-port", address,
			"-rsigner", certificates->authority, "-rkey", certificates->authority_key,
			"-CA", certificates->authority, "-nrequest", requests, (char *) NULL
		);

		_exit (1);
	}

	return pid;

}

// waits until the first request of OCSP of the cerver ended, with an answer or with a failure
static TlsStats providers_wait_ocsp (const ProvidersServer *server, const bool answered) {

	const double deadline = test_wait_now () + (double) (TEST_WAIT_SECONDS * test_time_scale ());

	TlsStats stats;
	test_check_unsigned_eq (cerver_tls_get_stats (server->cerver, &stats), 0, NULL);
	while (!(answered ? stats.ocsp_refreshes : stats.ocsp_failures) && (test_wait_now () < deadline)) {
		(void) usleep (1 << 14);
		test_check_unsigned_eq (cerver_tls_get_stats (server->cerver, &stats), 0, NULL);
	}

	return stats;

}

// a client that asks for the status of the certificate gets the answer that the cerver stapled
static bool providers_stapled (const u16 port, const TestCertificates *certificates) {

	SSL_CTX *ctx = SSL_CTX_new (TLS_client_method ());
	test_check_ptr (ctx);
	test_check_int_eq (SSL_CTX_load_verify_locations (ctx, certificates->authority, NULL), 1, NULL);

	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	test_check_int_eq (connect (fd, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	SSL *ssl = SSL_new (ctx);
	(void) SSL_set_fd (ssl, fd);
	(void) SSL_set_tlsext_status_type (ssl, TLSEXT_STATUSTYPE_ocsp);

	test_check_int_eq (SSL_connect (ssl), 1, NULL);

	const unsigned char *answer = NULL;
	const long size = SSL_get_tlsext_status_ocsp_resp (ssl, &answer);

	bool good = false;
	if ((size > 0) && answer) {
		OCSP_RESPONSE *response = d2i_OCSP_RESPONSE (NULL, &answer, size);
		good = response && (OCSP_response_status (response) == OCSP_RESPONSE_STATUS_SUCCESSFUL);
		OCSP_RESPONSE_free (response);
	}

	(void) SSL_shutdown (ssl);
	SSL_free (ssl);
	SSL_CTX_free (ctx);
	(void) close (fd);
	ERR_clear_error ();

	return good;

}

static void test_providers_ocsp (const TestCertificates *certificates, const u16 port) {

	if (!providers_has (PROVIDERS_OPENSSL)) {
		(void) printf ("\tskipped: OCSP stapling, without %s\n", PROVIDERS_OPENSSL);
		return;
	}

	char index[PROVIDERS_PATH_SIZE];
	(void) snprintf (index, sizeof (index), "%s/index.txt", certificates->directory);
	providers_ocsp_index (index);

	// the responder answers one connection at a time: a probe would hold it, so the test only waits for it
	const u16 responder_port = (u16) (port + 1);
	const pid_t responder = providers_ocsp_responder (certificates, index, responder_port);
	(void) usleep (PROVIDERS_RESPONDER_WAIT * test_time_scale ());

	char url[PROVIDERS_COMMAND_SIZE];
	(void) snprintf (url, sizeof (url), "http://127.0.0.1:%u", (unsigned int) responder_port);

	TlsSettings *settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->server_chain, certificates->server_key), 0, NULL);
	tls_settings_set_ocsp_stapling (settings, true);
	test_check_unsigned_eq (tls_settings_set_ocsp_responder (settings, url), 0, NULL);

	ProvidersServer server = providers_start (port, settings);

	// the thread of the TLS of the cerver asks for the first answer when it starts; a responder that was not up yet
	// is asked again with a reload
	TlsStats stats = providers_wait_ocsp (&server, true);
	for (unsigned int attempt = 0; !stats.ocsp_refreshes && (attempt < PROVIDERS_OCSP_ATTEMPTS); attempt++) {
		(void) usleep (PROVIDERS_RESPONDER_WAIT);
		(void) cerver_tls_reload (server.cerver);
		test_check_unsigned_eq (cerver_tls_get_stats (server.cerver, &stats), 0, NULL);
	}

	test_check_true (stats.ocsp_refreshes >= 1);
	test_check_true (providers_stapled (port, certificates));

	providers_stop (&server);

	(void) kill (responder, SIGTERM);
	(void) waitpid (responder, NULL, 0);
	(void) unlink (index);

	// a responder that does not answer leaves the handshakes without a staple, and counts the failure
	settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->server_chain, certificates->server_key), 0, NULL);
	tls_settings_set_ocsp_stapling (settings, true);
	test_check_unsigned_eq (tls_settings_set_ocsp_responder (settings, url), 0, NULL);

	server = providers_start ((u16) (port + 2), settings);
	stats = providers_wait_ocsp (&server, false);
	test_check_true (stats.ocsp_failures >= 1);
	test_check_false (providers_stapled (server.port, certificates));
	providers_stop (&server);

}

#pragma endregion

#pragma region pkcs11

static int providers_run (const char *command) {

	const int status = system (command);

	return WIFEXITED (status) ? WEXITSTATUS (status) : -1;

}

// a token of SoftHSM2 in the folder of the run with the key of the server
static bool providers_token (const TestCertificates *certificates) {

	char tokens[PROVIDERS_PATH_SIZE];
	(void) snprintf (tokens, sizeof (tokens), "%s/tokens", certificates->directory);
	(void) mkdir (tokens, S_IRWXU);

	char configuration[PROVIDERS_PATH_SIZE];
	(void) snprintf (configuration, sizeof (configuration), "%s/softhsm2.conf", certificates->directory);

	FILE *file = fopen (configuration, "w");
	test_check_ptr (file);
	(void) fprintf (file, "directories.tokendir = %s\nobjectstore.backend = file\n", tokens);
	(void) fclose (file);

	(void) setenv ("SOFTHSM2_CONF", configuration, 1);
	(void) setenv ("PKCS11_PROVIDER_MODULE", PROVIDERS_SOFTHSM_MODULE, 1);

	char command[PROVIDERS_COMMAND_SIZE];
	(void) snprintf (
		command, sizeof (command),
		"%s --init-token --free --label cerver --pin %s --so-pin %s > /dev/null 2>&1 && "
		"%s --import %s --token cerver --label server --id 01 --pin %s > /dev/null 2>&1",
		PROVIDERS_SOFTHSM_UTIL, PROVIDERS_PIN, PROVIDERS_PIN,
		PROVIDERS_SOFTHSM_UTIL, certificates->server_key, PROVIDERS_PIN
	);

	return !providers_run (command);

}

static void test_providers_pkcs11 (const TestCertificates *certificates, const u16 port) {

	if (!providers_has (PROVIDERS_SOFTHSM_UTIL) || !providers_has (PROVIDERS_SOFTHSM_MODULE)) {
		(void) printf ("\tskipped: the key in PKCS#11, without SoftHSM2\n");
		return;
	}

	if (PROVIDERS_SANITIZER) {
		(void) printf ("\tskipped: the key in PKCS#11, its module is opened with RTLD_DEEPBIND\n");
		return;
	}

	test_check_true (providers_token (certificates));

	// cerver never reads the key: the provider of the token signs the handshake
	TlsSettings *settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_add_certificate (
		settings, certificates->server_chain, "pkcs11:token=cerver;object=server;type=private?pin-value=" PROVIDERS_PIN
	), 0, NULL);
	test_check_unsigned_eq (tls_settings_set_key_store (settings, "pkcs11", NULL), 0, NULL);

	ProvidersServer server = providers_start (port, settings);

	RawTlsOptions options;
	(void) memset (&options, 0, sizeof (RawTlsOptions));
	options.authority = certificates->authority;

	RawTls *raw = raw_tls_connect (port, &options);
	test_check_true (raw->connected);
	test_check_unsigned_eq (raw_tls_request (raw, providers_hello_request, NULL, 0), 200, NULL);
	raw_tls_close (raw);

	providers_stop (&server);

}

#pragma endregion

void tls_tests_providers (const TestCertificates *certificates) {

	(void) printf ("Testing TLS providers...\n");

	test_providers_ocsp (certificates, PROVIDERS_PORT_BASE);
	test_providers_pkcs11 (certificates, (u16) (PROVIDERS_PORT_BASE + (1 << 3)));

	(void) printf ("Done!\n");

}
