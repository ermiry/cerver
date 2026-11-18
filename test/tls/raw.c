#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "../test.h"
#include "tls.h"

// the seconds that a raw client waits for its cerver
#define RAW_TLS_TIMEOUT_SECONDS				8

// the most bytes of a response that is read
#define RAW_TLS_RESPONSE_SIZE				(1 << 16)

// the status after "HTTP/1.1 "
#define RAW_TLS_STATUS_OFFSET				9

static int raw_tls_socket (const u16 port) {

	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd >= 0, NULL);

	struct timeval timeout = { RAW_TLS_TIMEOUT_SECONDS, 0 };
	(void) setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));
	(void) setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	test_check_int_eq (connect (fd, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	return fd;

}

static void raw_tls_configure (SSL_CTX *ctx, const RawTlsOptions *options) {

	if (options->min_version) (void) SSL_CTX_set_min_proto_version (ctx, options->min_version);
	if (options->max_version) (void) SSL_CTX_set_max_proto_version (ctx, options->max_version);

	// a raw client can offer what cerver must refuse
	SSL_CTX_set_security_level (ctx, 0);
	if (options->ciphers12) (void) SSL_CTX_set_cipher_list (ctx, options->ciphers12);

	if (options->authority) {
		test_check_int_eq (SSL_CTX_load_verify_locations (ctx, options->authority, NULL), 1, NULL);
		SSL_CTX_set_verify (ctx, SSL_VERIFY_PEER, NULL);
	}

	if (options->client_chain) {
		test_check_int_eq (SSL_CTX_use_certificate_chain_file (ctx, options->client_chain), 1, NULL);
		test_check_int_eq (SSL_CTX_use_PrivateKey_file (ctx, options->client_key, SSL_FILETYPE_PEM), 1, NULL);
	}

	if (options->alpn) (void) SSL_CTX_set_alpn_protos (ctx, options->alpn, options->alpn_size);

}

// the early data goes before the handshake ends
static void raw_tls_write_early (RawTls *raw, const char *early_data) {

	size_t written = 0;
	if (SSL_write_early_data (raw->ssl, early_data, strlen (early_data), &written) != 1) ERR_clear_error ();

}

RawTls *raw_tls_connect (const u16 port, const RawTlsOptions *options) {

	RawTls *raw = (RawTls *) calloc (1, sizeof (RawTls));
	test_check_ptr (raw);

	raw->fd = raw_tls_socket (port);
	raw->ctx = SSL_CTX_new (TLS_client_method ());
	test_check_ptr (raw->ctx);

	raw_tls_configure (raw->ctx, options);

	raw->ssl = SSL_new (raw->ctx);
	test_check_ptr (raw->ssl);
	(void) SSL_set_fd (raw->ssl, raw->fd);

	if (options->server_name) (void) SSL_set_tlsext_host_name (raw->ssl, options->server_name);
	if (options->session) (void) SSL_set_session (raw->ssl, options->session);

	if (options->early_data && options->session) raw_tls_write_early (raw, options->early_data);

	// TLS 1.3 tells a failed certificate of the client after the handshake: the first read of a request sees it
	raw->connected = (SSL_connect (raw->ssl) == 1);

	raw->early_accepted = (SSL_get_early_data_status (raw->ssl) == SSL_EARLY_DATA_ACCEPTED);

	ERR_clear_error ();

	return raw;

}

// reads until the headers end, the status of the response
static unsigned int raw_tls_read_status (RawTls *raw, char *body, size_t body_size) {

	char *response = (char *) calloc (1, RAW_TLS_RESPONSE_SIZE);
	test_check_ptr (response);

	size_t size = 0;
	char *end = NULL;
	while (!end && (size < (RAW_TLS_RESPONSE_SIZE - 1))) {
		size_t read = 0;
		if (SSL_read_ex (raw->ssl, response + size, RAW_TLS_RESPONSE_SIZE - 1 - size, &read) != 1) break;
		size += read;
		response[size] = '\0';
		end = strstr (response, "\r\n\r\n");
	}

	unsigned int status = 0;
	if (end && (size > RAW_TLS_STATUS_OFFSET)) status = (unsigned int) atoi (response + RAW_TLS_STATUS_OFFSET);

	if (body && body_size) {
		(void) snprintf (body, body_size, "%s", end ? end + 4 : "");
	}

	free (response);
	ERR_clear_error ();

	return status;

}

unsigned int raw_tls_request (RawTls *raw, const char *request, char *body, size_t body_size) {

	unsigned int status = 0;

	size_t written = 0;
	if (raw->connected && (SSL_write_ex (raw->ssl, request, strlen (request), &written) == 1)) {
		status = raw_tls_read_status (raw, body, body_size);
	}

	ERR_clear_error ();

	return status;

}

unsigned int raw_tls_response (RawTls *raw, char *body, size_t body_size) {

	return raw->connected ? raw_tls_read_status (raw, body, body_size) : 0;

}

void raw_tls_peer_name (const RawTls *raw, char *name, size_t size) {

	name[0] = '\0';

	X509 *peer = SSL_get0_peer_certificate (raw->ssl);
	if (peer) {
		(void) X509_NAME_get_text_by_NID (X509_get_subject_name (peer), NID_commonName, name, (int) size);
	}

}

void raw_tls_close (RawTls *raw) {

	if (raw) {
		if (raw->connected) (void) SSL_shutdown (raw->ssl);

		SSL_free (raw->ssl);
		SSL_CTX_free (raw->ctx);
		(void) close (raw->fd);

		ERR_clear_error ();

		free (raw);
	}

}
