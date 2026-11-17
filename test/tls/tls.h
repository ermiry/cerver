#ifndef _CERVER_TESTS_TLS_H_
#define _CERVER_TESTS_TLS_H_

#include <stdbool.h>
#include <stddef.h>

#include <cerver/types/types.h>

// the types of OpenSSL, without its headers (they use names that test.h defines as macros)
struct ssl_ctx_st;
struct ssl_st;
struct ssl_session_st;

// the ports of the servers of the suite, after the ones of the WebSocket suites
#define TLS_TEST_PORT_BASE					(12288)

#define TEST_CERTIFICATE_PATH_SIZE			256

// the certificates of a run, made in a folder of /tmp
typedef struct TestCertificates {

	char directory[TEST_CERTIFICATE_PATH_SIZE];

	char authority[TEST_CERTIFICATE_PATH_SIZE];
	char authority_key[TEST_CERTIFICATE_PATH_SIZE];

	char server_chain[TEST_CERTIFICATE_PATH_SIZE];
	char server_key[TEST_CERTIFICATE_PATH_SIZE];

	char other_chain[TEST_CERTIFICATE_PATH_SIZE];
	char other_key[TEST_CERTIFICATE_PATH_SIZE];

	char wildcard_chain[TEST_CERTIFICATE_PATH_SIZE];
	char wildcard_key[TEST_CERTIFICATE_PATH_SIZE];

	char client_chain[TEST_CERTIFICATE_PATH_SIZE];
	char client_key[TEST_CERTIFICATE_PATH_SIZE];

	char expired_chain[TEST_CERTIFICATE_PATH_SIZE];
	char expired_key[TEST_CERTIFICATE_PATH_SIZE];

	char mismatch_key[TEST_CERTIFICATE_PATH_SIZE];

	char stranger_chain[TEST_CERTIFICATE_PATH_SIZE];
	char stranger_key[TEST_CERTIFICATE_PATH_SIZE];

} TestCertificates;

extern void test_certificates_create (TestCertificates *certificates);

extern void test_certificates_delete (TestCertificates *certificates);

extern void test_certificates_replace (const char *source, const char *target);

// what a raw client of OpenSSL offers
typedef struct RawTlsOptions {

	int min_version;
	int max_version;
	const char *ciphers12;
	const char *server_name;
	const unsigned char *alpn;
	unsigned int alpn_size;
	const char *authority;
	const char *client_chain;
	const char *client_key;
	struct ssl_session_st *session;
	const char *early_data;

} RawTlsOptions;

typedef struct RawTls {

	int fd;
	struct ssl_ctx_st *ctx;
	struct ssl_st *ssl;
	bool connected;
	bool early_accepted;

} RawTls;

extern RawTls *raw_tls_connect (const u16 port, const RawTlsOptions *options);

// sends a request and returns the status of its response, 0 on an error
extern unsigned int raw_tls_request (RawTls *raw, const char *request, char *body, size_t body_size);

// the status of the response to the early data that was sent with the handshake
extern unsigned int raw_tls_response (RawTls *raw, char *body, size_t body_size);

extern void raw_tls_close (RawTls *raw);

// the common name of the certificate of the cerver
extern void raw_tls_peer_name (const RawTls *raw, char *name, size_t size);

extern void tls_tests_settings (void);

extern void tls_tests_server (const TestCertificates *certificates);

extern void tls_tests_websocket (const TestCertificates *certificates);

extern void tls_tests_client (const TestCertificates *certificates);

extern void tls_tests_providers (const TestCertificates *certificates);

extern void tls_tests_cervers (const TestCertificates *certificates);

extern void tls_tests_disconnect (const TestCertificates *certificates);

#endif
