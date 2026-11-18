#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cerver/types/types.h>

#include <cerver/tls/tls.h>

#include "cerver/crypto/provider.h"
#include "cerver/tls/internal.h"

// the records of a client for the session of a cerver, in pieces whose sizes the first byte selects, or the
// strings of the settings (ALPN, the URI of a key, the ciphers and the groups); nothing touches a socket

#define FUZZ_TLS_HEADER_SIZE				2

#define FUZZ_TLS_MODE_RECORDS				0
#define FUZZ_TLS_MODE_SETTINGS				1
#define FUZZ_TLS_MODES						2

// the sizes of the pieces that the records arrive in
#define FUZZ_TLS_PIECE_SIZES				8

// the most bytes of a piece of a string of the settings
#define FUZZ_TLS_STRING_SIZE				512

// the bytes of the plaintext that a read takes
#define FUZZ_TLS_READ_SIZE					(1 << 12)

// the days of the certificate of the fuzzer
#define FUZZ_TLS_VALID_SECONDS				(60L * 60L * 24L * 365L)

#define FUZZ_TLS_PATH_SIZE					256

static const size_t fuzz_tls_piece_sizes[FUZZ_TLS_PIECE_SIZES] = { 1, 2, 5, 7, 64, 512, 4096, 1 << 15 };

static CryptoTlsContext *fuzz_tls_context = NULL;
static CryptoTlsContext *fuzz_tls_early_context = NULL;

static char fuzz_tls_chain[FUZZ_TLS_PATH_SIZE];
static char fuzz_tls_key[FUZZ_TLS_PATH_SIZE];

#pragma region certificate

static void fuzz_tls_write_certificate (void) {

	EVP_PKEY *key = EVP_EC_gen ("P-256");
	X509 *certificate = X509_new ();

	(void) X509_set_version (certificate, 2);
	(void) ASN1_INTEGER_set (X509_get_serialNumber (certificate), 1);
	(void) X509_gmtime_adj (X509_getm_notBefore (certificate), 0);
	(void) X509_gmtime_adj (X509_getm_notAfter (certificate), FUZZ_TLS_VALID_SECONDS);
	(void) X509_set_pubkey (certificate, key);

	X509_NAME *name = X509_get_subject_name (certificate);
	(void) X509_NAME_add_entry_by_txt (name, "CN", MBSTRING_ASC, (const unsigned char *) "localhost", -1, -1, 0);
	(void) X509_set_issuer_name (certificate, name);

	X509V3_CTX context;
	X509V3_set_ctx_nodb (&context);
	X509V3_set_ctx (&context, certificate, certificate, NULL, NULL, 0);
	X509_EXTENSION *extension = X509V3_EXT_conf_nid (NULL, &context, NID_subject_alt_name, "DNS:localhost");
	(void) X509_add_ext (certificate, extension, -1);
	X509_EXTENSION_free (extension);

	(void) X509_sign (certificate, key, EVP_sha256 ());

	(void) snprintf (fuzz_tls_chain, sizeof (fuzz_tls_chain), "/tmp/cerver-fuzz-tls-%d.pem", (int) getpid ());
	(void) snprintf (fuzz_tls_key, sizeof (fuzz_tls_key), "/tmp/cerver-fuzz-tls-%d.key", (int) getpid ());

	FILE *file = fopen (fuzz_tls_chain, "w");
	if (file) { (void) PEM_write_X509 (file, certificate); (void) fclose (file); }

	file = fopen (fuzz_tls_key, "w");
	if (file) { (void) PEM_write_PrivateKey (file, key, NULL, NULL, 0, NULL, NULL); (void) fclose (file); }

	X509_free (certificate);
	EVP_PKEY_free (key);

}

static CryptoTlsContext *fuzz_tls_context_create (const u32 early_data) {

	TlsSettings *settings = tls_settings_create ();
	(void) tls_settings_add_certificate (settings, fuzz_tls_chain, fuzz_tls_key);
	tls_settings_set_early_data (settings, early_data);

	char error[TLS_ERROR_SIZE];
	CryptoTlsContext *context = cerver_crypto_provider_tls_context_create (settings, true, true, error, sizeof (error));

	tls_settings_delete (settings);

	return context;

}

int LLVMFuzzerInitialize (int *argc, char ***argv) {

	(void) argc;
	(void) argv;

	fuzz_tls_write_certificate ();

	fuzz_tls_context = fuzz_tls_context_create (0);
	fuzz_tls_early_context = fuzz_tls_context_create (1 << 14);

	(void) unlink (fuzz_tls_chain);
	(void) unlink (fuzz_tls_key);

	return (fuzz_tls_context && fuzz_tls_early_context) ? 0 : 1;

}

#pragma endregion

#pragma region records

// the bytes that the cerver would send are dropped, so the output never grows
static void fuzz_tls_drop_output (TlsConnection *connection) {

	connection->output.start = 0;
	connection->output.end = 0;

}

static void fuzz_tls_feed (TlsConnection *connection, const uint8_t *data, const size_t size) {

	if (connection->input.start) {
		const size_t used = connection->input.end - connection->input.start;
		(void) memmove (connection->input.bytes, connection->input.bytes + connection->input.start, used);
		connection->input.start = 0;
		connection->input.end = used;
	}

	const size_t room = connection->input.capacity - connection->input.end;
	const size_t taken = (size < room) ? size : room;

	(void) memcpy (connection->input.bytes + connection->input.end, data, taken);
	connection->input.end += taken;

}

// one step of the session: the early data, the handshake, and once it ended the reads
static bool fuzz_tls_step (TlsConnection *connection, bool *established, bool *early_ended) {

	bool going = true;
	TlsFailure failure = TLS_FAILURE_NONE;

	u8 buffer[FUZZ_TLS_READ_SIZE];
	size_t read = 0;

	if (!*early_ended) {
		const CryptoTlsResult result = cerver_crypto_provider_tls_session_read_early (connection->session, buffer, sizeof (buffer), &read);
		*early_ended = (result == CRYPTO_TLS_RESULT_DONE) || (result == CRYPTO_TLS_RESULT_FAILED);
		going = (result != CRYPTO_TLS_RESULT_FAILED);
	}

	else if (!*established) {
		const CryptoTlsResult result = cerver_crypto_provider_tls_session_handshake (connection->session, &failure);
		*established = (result == CRYPTO_TLS_RESULT_DONE);
		going = (result == CRYPTO_TLS_RESULT_DONE) || (result == CRYPTO_TLS_RESULT_WANT_READ);
	}

	else {
		CryptoTlsResult result = CRYPTO_TLS_RESULT_DONE;
		while (result == CRYPTO_TLS_RESULT_DONE) {
			result = cerver_crypto_provider_tls_session_read (connection->session, buffer, sizeof (buffer), &read);
		}

		going = (result == CRYPTO_TLS_RESULT_WANT_READ);
	}

	fuzz_tls_drop_output (connection);

	return going;

}

static void fuzz_tls_records (const uint8_t *data, size_t size, const uint8_t selector) {

	CryptoTlsContext *context = (selector & 1) ? fuzz_tls_early_context : fuzz_tls_context;
	TlsConnection *connection = tls_connection_create (context, -1, true, false, NULL, NULL, NULL);
	if (connection) {
		const size_t piece = fuzz_tls_piece_sizes[(selector >> 1) % FUZZ_TLS_PIECE_SIZES];

		bool established = false;
		bool early_ended = false;
		bool going = true;

		while (going && size) {
			const size_t taken = (size < piece) ? size : piece;
			fuzz_tls_feed (connection, data, taken);
			data += taken;
			size -= taken;

			// every record that arrived is taken before the next piece
			for (unsigned int step = 0; going && (step < FUZZ_TLS_PIECE_SIZES); step++) {
				going = fuzz_tls_step (connection, &established, &early_ended);
			}
		}

		if (established) {
			(void) cerver_crypto_provider_tls_session_server_name (connection->session);
			(void) tls_connection_peer_certificate (connection);
			cerver_crypto_provider_tls_session_close (connection->session);
		}

		tls_connection_delete (connection);
	}

}

#pragma endregion

#pragma region settings

// the strings are separated by a NUL: ALPN, the key, the ciphers of TLS 1.2 and the groups
static const char *fuzz_tls_next_string (const uint8_t **data, size_t *size, char *string) {

	size_t length = 0;
	while (length < *size && (*data)[length] && (length < (FUZZ_TLS_STRING_SIZE - 1))) length += 1;

	(void) memcpy (string, *data, length);
	string[length] = '\0';

	const size_t consumed = (length < *size) ? length + 1 : length;
	*data += consumed;
	*size -= consumed;

	return string;

}

static void fuzz_tls_settings (const uint8_t *data, size_t size) {

	char alpn[FUZZ_TLS_STRING_SIZE];
	char key[FUZZ_TLS_STRING_SIZE];
	char ciphers[FUZZ_TLS_STRING_SIZE];
	char groups[FUZZ_TLS_STRING_SIZE];

	(void) fuzz_tls_next_string (&data, &size, alpn);
	(void) fuzz_tls_next_string (&data, &size, key);
	(void) fuzz_tls_next_string (&data, &size, ciphers);
	(void) fuzz_tls_next_string (&data, &size, groups);

	u8 wire[FUZZ_TLS_STRING_SIZE];
	(void) tls_settings_alpn_wire (alpn, wire, sizeof (wire));
	(void) tls_settings_key_is_uri (key);

	TlsSettings *settings = tls_settings_create ();
	(void) tls_settings_add_certificate (settings, fuzz_tls_chain, fuzz_tls_key);
	(void) tls_settings_set_alpn (settings, alpn);
	if (ciphers[0]) (void) tls_settings_set_ciphers (settings, ciphers, NULL);
	if (groups[0]) (void) tls_settings_set_groups (settings, groups);

	// the context of a client parses the strings without reading a file
	char error[TLS_ERROR_SIZE];
	CryptoTlsContext *context = cerver_crypto_provider_tls_context_create (settings, false, true, error, sizeof (error));
	cerver_crypto_provider_tls_context_release (context);

	tls_settings_delete (settings);

}

#pragma endregion

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size >= FUZZ_TLS_HEADER_SIZE) {
		const uint8_t mode = data[0] % FUZZ_TLS_MODES;
		const uint8_t selector = data[1];

		data += FUZZ_TLS_HEADER_SIZE;
		size -= FUZZ_TLS_HEADER_SIZE;

		if (mode == FUZZ_TLS_MODE_RECORDS) fuzz_tls_records (data, size, selector);
		else fuzz_tls_settings (data, size);
	}

	return 0;

}
