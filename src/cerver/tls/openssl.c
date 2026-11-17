#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/crypto/provider.h"

#include "cerver/tls/internal.h"
#include "cerver/tls/tls.h"

#include "cerver/utils/log.h"

#ifdef CERVER_CRYPTO_OPENSSL

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/http.h>
#include <openssl/ocsp.h>
#include <openssl/params.h>
#include <openssl/pem.h>
#include <openssl/provider.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/store.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

// the ctrls of a BIO that OpenSSL uses for kTLS (internal/bio.h): the key, and the type of the next record
#define CRYPTO_TLS_BIO_SET_KTLS					72
#define CRYPTO_TLS_BIO_SET_KTLS_RECORD_TYPE		74
#define CRYPTO_TLS_BIO_CLEAR_KTLS_RECORD_TYPE	75

// the defaults of RFC 9325: ECDHE with AEAD in TLS 1.2, the ciphers of RFC 8446 in TLS 1.3
#define CRYPTO_TLS_CIPHERS12_DEFAULT	"ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"	\
										"ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"	\
										"ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305"
#define CRYPTO_TLS_CIPHERS13_DEFAULT	"TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256"
#define CRYPTO_TLS_GROUPS_DEFAULT		"X25519MLKEM768:X25519:P-256:P-384"
#define CRYPTO_TLS_GROUPS_CLASSIC		"X25519:P-256:P-384"

// the cipher list that has every cipher, to find a cipher by its IANA name
#define CRYPTO_TLS_CIPHERS_ALL			"ALL:COMPLEMENTOFALL"
// the separator of a list of ciphers
#define CRYPTO_TLS_LIST_SEPARATOR		":"
// the separator of the names of a certificate
#define CRYPTO_TLS_NAMES_SEPARATOR		","

// a name with a wildcard of one level: "*." and the rest of the name, that starts with its dot
#define CRYPTO_TLS_WILDCARD_PREFIX			"*."
#define CRYPTO_TLS_WILDCARD_PREFIX_LENGTH	(sizeof (CRYPTO_TLS_WILDCARD_PREFIX) - 1)
#define CRYPTO_TLS_WILDCARD_LENGTH			1
#define CRYPTO_TLS_LABEL_SEPARATOR			'.'

// the mode of the file of the keys of the tickets, and the provider that loading another one stops
#define CRYPTO_TLS_KEYS_FILE_MODE			"rb"
#define CRYPTO_TLS_DEFAULT_PROVIDER			"default"

// the ALPN of a web cerver without one in its settings
#define CRYPTO_TLS_WEB_ALPN				"http/1.1"
// the most bytes of the protocols of ALPN in the wire format
#define CRYPTO_TLS_ALPN_WIRE_SIZE		1024

// the context of the session ids of the cache of a cerver
#define CRYPTO_TLS_SESSION_ID_CONTEXT	"cerver"

// the sessions that a client keeps for their destinations: 2^6
#define CRYPTO_TLS_CLIENT_CACHE_SIZE	64

// the keys of the tickets: the current one and the previous ones that still decrypt
#define CRYPTO_TLS_TICKET_KEYS_MAX		8
// the keys of cerver that are kept: the current one and the previous one
#define CRYPTO_TLS_TICKET_KEYS_KEPT		2
#define CRYPTO_TLS_TICKET_NAME_SIZE		16
#define CRYPTO_TLS_TICKET_HMAC_SIZE		32
#define CRYPTO_TLS_TICKET_AES_SIZE		32
#define CRYPTO_TLS_TICKET_KEY_SIZE		80
#define CRYPTO_TLS_TICKET_IV_SIZE		16
// the digest of the HMAC of the tickets
#define CRYPTO_TLS_TICKET_DIGEST		"SHA256"

// what the callback of the tickets returns
#define CRYPTO_TLS_TICKET_NONE			0
#define CRYPTO_TLS_TICKET_CURRENT		1
#define CRYPTO_TLS_TICKET_RENEW			2

// the seconds of a day
#define CRYPTO_TLS_SECONDS_PER_DAY		86400

// the seconds of the slack of an OCSP answer, and the times between its refreshes
#define CRYPTO_TLS_OCSP_SLACK_SECONDS	300
#define CRYPTO_TLS_OCSP_RETRY_SECONDS	64
#define CRYPTO_TLS_OCSP_DEFAULT_SECONDS	4096
// an OCSP answer is renewed at half of its validity
#define CRYPTO_TLS_OCSP_RENEW_DIVISOR	2
// what a refresh returns for a context that does not staple
#define CRYPTO_TLS_OCSP_NOT_STAPLED		2
// the request to a responder: its content types, its deadline (2^2 s) and the most bytes of its answer (2^16)
#define CRYPTO_TLS_OCSP_REQUEST_TYPE	"application/ocsp-request"
#define CRYPTO_TLS_OCSP_RESPONSE_TYPE	"application/ocsp-response"
#define CRYPTO_TLS_OCSP_TIMEOUT_SECONDS	4
#define CRYPTO_TLS_OCSP_MAX_RESPONSE	65536

// the result of the verification of a certificate that verified
#define CRYPTO_TLS_VERIFY_OK			X509_V_OK

// the alerts that are counted as a failure of a certificate or of ALPN (RFC 8446 section 6)
#define CRYPTO_TLS_ALERT_BAD_CERTIFICATE			42
#define CRYPTO_TLS_ALERT_UNKNOWN_CA					48
#define CRYPTO_TLS_ALERT_CERTIFICATE_REQUIRED		116
#define CRYPTO_TLS_ALERT_NO_APPLICATION_PROTOCOL	120

// the size of the hex of a byte, with its NUL
#define CRYPTO_TLS_HEX_BYTE_SIZE		3
// the size of an IP address as text
#define CRYPTO_TLS_ADDRESS_SIZE			64
// the size of the bytes of an IPv6 address
#define CRYPTO_TLS_IPV6_SIZE			16
// the size of the bytes of an IPv4 address
#define CRYPTO_TLS_IPV4_SIZE			4

#pragma region types

// a key of the tickets
typedef struct CryptoTlsTicketKey {

	u8 name[CRYPTO_TLS_TICKET_NAME_SIZE];
	u8 hmac[CRYPTO_TLS_TICKET_HMAC_SIZE];
	u8 aes[CRYPTO_TLS_TICKET_AES_SIZE];

} CryptoTlsTicketKey;

// a session that a client keeps for a destination
typedef struct CryptoTlsCachedSession {

	char destination[TLS_DESTINATION_SIZE];
	SSL_SESSION *session;
	u64 used;

} CryptoTlsCachedSession;

// a certificate of a cerver with its own SSL_CTX, the names it answers to and its OCSP answer
typedef struct CryptoTlsCertificate {

	struct CryptoTlsContext *context;

	SSL_CTX *ctx;
	X509 *leaf;
	X509 *issuer;

	char **names;
	size_t n_names;

	u8 *ocsp;
	size_t ocsp_size;
	time_t ocsp_next_update;

} CryptoTlsCertificate;

struct CryptoTlsContext {

	unsigned int references;
	bool server;

	pthread_mutex_t mutex;

	// a cerver: one per certificate, the first is the default
	CryptoTlsCertificate *certificates;
	size_t n_certificates;

	// a client
	SSL_CTX *client_ctx;
	CryptoTlsCachedSession cache[CRYPTO_TLS_CLIENT_CACHE_SIZE];
	u64 cache_clock;

	u8 alpn[CRYPTO_TLS_ALPN_WIRE_SIZE];
	unsigned int alpn_size;

	TlsClientVerify verify;
	u32 early_data;

	bool tickets;
	bool shared_ticket_keys;
	CryptoTlsTicketKey ticket_keys[CRYPTO_TLS_TICKET_KEYS_MAX];
	size_t n_ticket_keys;
	time_t ticket_keys_rotated;

	bool ocsp_stapling;
	char *ocsp_responder;

	OSSL_PROVIDER *default_provider;
	OSSL_PROVIDER *key_store;
	char *key_store_properties;

};

struct CryptoTlsSession {

	CryptoTlsContext *context;
	TlsConnection *connection;

	SSL *ssl;
	BIO *bio;

	int ktls_record_type;

	char destination[TLS_DESTINATION_SIZE];

};

#pragma endregion

#pragma region bio

static BIO_METHOD *crypto_tls_bio_method = NULL;
static pthread_once_t crypto_tls_bio_once = PTHREAD_ONCE_INIT;

static int crypto_tls_bio_write (BIO *bio, const char *data, int size) {

	CryptoTlsSession *session = (CryptoTlsSession *) BIO_get_data (bio);
	BIO_clear_retry_flags (bio);

	int retval = -1;

	if (session->connection->ktls_send) {
		const ssize_t sent = tls_connection_ktls_send (
			session->connection, session->ktls_record_type, data, (size_t) size
		);

		// like the socket BIO: the type of a record applies to one write
		if (sent >= 0) {
			session->ktls_record_type = 0;
			retval = (int) sent;
		}
	}

	else if (tls_connection_output_append (session->connection, data, (size_t) size)) {
		retval = size;
	}

	return retval;

}

static int crypto_tls_bio_read (BIO *bio, char *data, int size) {

	CryptoTlsSession *session = (CryptoTlsSession *) BIO_get_data (bio);
	BIO_clear_retry_flags (bio);

	const size_t taken = tls_connection_input_take (session->connection, data, (size_t) size);
	if (!taken) BIO_set_retry_read (bio);

	return taken ? (int) taken : -1;

}

static long crypto_tls_bio_ctrl_ktls (CryptoTlsSession *session, const int command, const long number, void *pointer) {

	long retval = 0;

	switch (command) {
		case CRYPTO_TLS_BIO_SET_KTLS:
			retval = tls_connection_ktls_start (session->connection, pointer, number != 0) ? 1 : 0;
			break;

		case CRYPTO_TLS_BIO_SET_KTLS_RECORD_TYPE: session->ktls_record_type = (int) number; retval = 1; break;
		case CRYPTO_TLS_BIO_CLEAR_KTLS_RECORD_TYPE: session->ktls_record_type = 0; retval = 1; break;
		case BIO_CTRL_GET_KTLS_SEND: retval = session->connection->ktls_send ? 1 : 0; break;

		default: break;
	}

	return retval;

}

static long crypto_tls_bio_ctrl (BIO *bio, int command, long number, void *pointer) {

	CryptoTlsSession *session = (CryptoTlsSession *) BIO_get_data (bio);

	long retval = 0;

	switch (command) {
		// the connection sends the output, never the BIO
		case BIO_CTRL_FLUSH: retval = 1; break;
		case BIO_CTRL_PENDING: retval = (long) (session->connection->input.end - session->connection->input.start); break;
		case BIO_CTRL_WPENDING: retval = (long) (session->connection->output.end - session->connection->output.start); break;
		default: retval = crypto_tls_bio_ctrl_ktls (session, command, number, pointer); break;
	}

	return retval;

}

static int crypto_tls_bio_create (BIO *bio) {

	BIO_set_init (bio, 1);

	return 1;

}

static void crypto_tls_bio_method_create (void) {

	BIO_METHOD *method = BIO_meth_new (BIO_get_new_index () | BIO_TYPE_SOURCE_SINK, "cerver");
	if (method) {
		(void) BIO_meth_set_write (method, crypto_tls_bio_write);
		(void) BIO_meth_set_read (method, crypto_tls_bio_read);
		(void) BIO_meth_set_ctrl (method, crypto_tls_bio_ctrl);
		(void) BIO_meth_set_create (method, crypto_tls_bio_create);
	}

	crypto_tls_bio_method = method;

}

static BIO_METHOD *crypto_tls_bio_get_method (void) {

	(void) pthread_once (&crypto_tls_bio_once, crypto_tls_bio_method_create);

	return crypto_tls_bio_method;

}

#pragma endregion

#pragma region errors

static void crypto_tls_error (char *error, const size_t error_size, const char *what, const char *where) {

	char reason[TLS_ERROR_SIZE] = { 0 };
	const unsigned long code = ERR_peek_last_error ();
	if (code) ERR_error_string_n (code, reason, sizeof (reason));

	(void) snprintf (error, error_size, "%s %s%s%s", what, where ? where : "", code ? " - " : "", reason);

	ERR_clear_error ();

}

// the reason of an alert, of the peer or of this side
static TlsFailure crypto_tls_failure_of_alert (const int alert) {

	TlsFailure failure = TLS_FAILURE_PROTOCOL;

	if (alert == CRYPTO_TLS_ALERT_NO_APPLICATION_PROTOCOL) failure = TLS_FAILURE_ALPN;
	else if (
		((alert >= CRYPTO_TLS_ALERT_BAD_CERTIFICATE) && (alert <= CRYPTO_TLS_ALERT_UNKNOWN_CA))
		|| (alert == CRYPTO_TLS_ALERT_CERTIFICATE_REQUIRED)
	) failure = TLS_FAILURE_CERTIFICATE;

	return failure;

}

static TlsFailure crypto_tls_failure_of_reason (const int reason) {

	TlsFailure failure = TLS_FAILURE_PROTOCOL;

	switch (reason) {
		case SSL_R_CERTIFICATE_VERIFY_FAILED:
		case SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE:
		case SSL_R_NO_CERTIFICATES_RETURNED:
			failure = TLS_FAILURE_CERTIFICATE;
			break;

		case SSL_R_NO_APPLICATION_PROTOCOL: failure = TLS_FAILURE_ALPN; break;

		default:
			if (reason > SSL_AD_REASON_OFFSET) failure = crypto_tls_failure_of_alert (reason - SSL_AD_REASON_OFFSET);
			break;
	}

	return failure;

}

// the reason of a failed handshake from the error queue
static TlsFailure crypto_tls_failure (const int ssl_error) {

	TlsFailure failure = TLS_FAILURE_PROTOCOL;

	if (ssl_error == SSL_ERROR_ZERO_RETURN) failure = TLS_FAILURE_CLOSED;
	else if (ssl_error == SSL_ERROR_SSL) {
		const unsigned long code = ERR_peek_last_error ();
		if (ERR_GET_LIB (code) == ERR_LIB_SSL) failure = crypto_tls_failure_of_reason (ERR_GET_REASON (code));
	}

	ERR_clear_error ();

	return failure;

}

static CryptoTlsResult crypto_tls_result (const int ssl_error) {

	CryptoTlsResult result = CRYPTO_TLS_RESULT_FAILED;

	if ((ssl_error == SSL_ERROR_WANT_READ) || (ssl_error == SSL_ERROR_WANT_WRITE)) result = CRYPTO_TLS_RESULT_WANT_READ;
	else if (ssl_error == SSL_ERROR_ZERO_RETURN) result = CRYPTO_TLS_RESULT_CLOSED;

	return result;

}

#pragma endregion

#pragma region names

// the DNS names of the subject alternative name of a certificate
static bool crypto_tls_certificate_names (CryptoTlsCertificate *certificate) {

	bool retval = true;

	GENERAL_NAMES *names = (GENERAL_NAMES *) X509_get_ext_d2i (certificate->leaf, NID_subject_alt_name, NULL, NULL);
	const int count = names ? sk_GENERAL_NAME_num (names) : 0;

	if (count > 0) {
		certificate->names = (char **) calloc ((size_t) count, sizeof (char *));
		retval = (certificate->names != NULL);
	}

	for (int index = 0; retval && (index < count); index++) {
		const GENERAL_NAME *name = sk_GENERAL_NAME_value (names, index);
		if (name->type == GEN_DNS) {
			const ASN1_IA5STRING *dns = name->d.dNSName;
			char *copy = strndup ((const char *) ASN1_STRING_get0_data (dns), (size_t) ASN1_STRING_length (dns));
			if (copy) certificate->names[certificate->n_names++] = copy;
			else retval = false;
		}
	}

	GENERAL_NAMES_free (names);

	return retval;

}

// a name matches exactly or by a wildcard of one level (*.example.com, RFC 9525 section 6.3)
static bool crypto_tls_name_matches (const char *pattern, const char *name, const bool wildcard) {

	bool retval = false;

	if (!wildcard) {
		retval = !strcasecmp (pattern, name);
	}

	// the label that the wildcard takes ends at the first dot of the name
	else if (!strncmp (pattern, CRYPTO_TLS_WILDCARD_PREFIX, CRYPTO_TLS_WILDCARD_PREFIX_LENGTH)) {
		const char *rest = strchr (name, CRYPTO_TLS_LABEL_SEPARATOR);
		retval = rest && (rest != name) && !strcasecmp (pattern + CRYPTO_TLS_WILDCARD_LENGTH, rest);
	}

	return retval;

}

static CryptoTlsCertificate *crypto_tls_certificate_with_name (
	CryptoTlsContext *context, const char *name, const bool wildcard
) {

	CryptoTlsCertificate *found = NULL;

	for (size_t index = 0; !found && (index < context->n_certificates); index++) {
		CryptoTlsCertificate *certificate = &context->certificates[index];

		for (size_t name_index = 0; !found && (name_index < certificate->n_names); name_index++) {
			if (crypto_tls_name_matches (certificate->names[name_index], name, wildcard)) found = certificate;
		}
	}

	return found;

}

// SNI: the certificate whose name matches, exactly first and by a wildcard after; the default without one
static int crypto_tls_server_name_callback (SSL *ssl, int *alert, void *argument) {

	(void) alert;

	CryptoTlsContext *context = (CryptoTlsContext *) argument;

	const char *name = SSL_get_servername (ssl, TLSEXT_NAMETYPE_host_name);
	if (name) {
		CryptoTlsCertificate *certificate = crypto_tls_certificate_with_name (context, name, false);
		if (!certificate) certificate = crypto_tls_certificate_with_name (context, name, true);

		if (certificate && (certificate->ctx != SSL_get_SSL_CTX (ssl))) (void) SSL_set_SSL_CTX (ssl, certificate->ctx);
	}

	return SSL_TLSEXT_ERR_OK;

}

#pragma endregion

#pragma region alpn

// the first protocol of the cerver that the client offers; without one the handshake fails (RFC 7301 section 3.2)
static int crypto_tls_alpn_select_callback (
	SSL *ssl, const unsigned char **out, unsigned char *out_size,
	const unsigned char *in, unsigned int in_size, void *argument
) {

	(void) ssl;

	CryptoTlsContext *context = (CryptoTlsContext *) argument;

	unsigned char *selected = NULL;
	const int result = SSL_select_next_proto (
		&selected, out_size, context->alpn, context->alpn_size, in, in_size
	);

	*out = selected;

	return (result == OPENSSL_NPN_NEGOTIATED) ? SSL_TLSEXT_ERR_OK : SSL_TLSEXT_ERR_ALERT_FATAL;

}

#pragma endregion

#pragma region tickets

static void crypto_tls_ticket_keys_rotate_locked (CryptoTlsContext *context, const time_t now) {

	if (context->n_ticket_keys == CRYPTO_TLS_TICKET_KEYS_MAX) context->n_ticket_keys -= 1;

	// the previous ones still decrypt
	(void) memmove (
		&context->ticket_keys[1], &context->ticket_keys[0], context->n_ticket_keys * sizeof (CryptoTlsTicketKey)
	);

	if (RAND_bytes ((unsigned char *) &context->ticket_keys[0], (int) sizeof (CryptoTlsTicketKey)) == 1) {
		context->n_ticket_keys += 1;
		context->ticket_keys_rotated = now;
	}

	else {
		(void) memmove (
			&context->ticket_keys[0], &context->ticket_keys[1], context->n_ticket_keys * sizeof (CryptoTlsTicketKey)
		);
	}

	// only the current key and the previous one are kept
	if (context->n_ticket_keys > CRYPTO_TLS_TICKET_KEYS_KEPT) context->n_ticket_keys = CRYPTO_TLS_TICKET_KEYS_KEPT;

}

static bool crypto_tls_ticket_hmac (EVP_MAC_CTX *hmac, const CryptoTlsTicketKey *key) {

	OSSL_PARAM parameters[] = {
		OSSL_PARAM_construct_octet_string (OSSL_MAC_PARAM_KEY, (void *) key->hmac, CRYPTO_TLS_TICKET_HMAC_SIZE),
		OSSL_PARAM_construct_utf8_string (OSSL_MAC_PARAM_DIGEST, (char *) CRYPTO_TLS_TICKET_DIGEST, 0),
		OSSL_PARAM_construct_end ()
	};

	return EVP_MAC_CTX_set_params (hmac, parameters) == 1;

}

static int crypto_tls_ticket_encrypt (
	CryptoTlsContext *context, unsigned char *name, unsigned char *iv, EVP_CIPHER_CTX *cipher, EVP_MAC_CTX *hmac
) {

	int retval = -1;

	const time_t now = time (NULL);
	if (!context->shared_ticket_keys && ((now - context->ticket_keys_rotated) >= TLS_TICKET_KEYS_ROTATION_SECONDS)) {
		crypto_tls_ticket_keys_rotate_locked (context, now);
	}

	if (context->n_ticket_keys && (RAND_bytes (iv, CRYPTO_TLS_TICKET_IV_SIZE) == 1)) {
		const CryptoTlsTicketKey *key = &context->ticket_keys[0];
		(void) memcpy (name, key->name, CRYPTO_TLS_TICKET_NAME_SIZE);

		if (
			(EVP_EncryptInit_ex (cipher, EVP_aes_256_cbc (), NULL, key->aes, iv) == 1)
			&& crypto_tls_ticket_hmac (hmac, key)
		) {
			retval = CRYPTO_TLS_TICKET_CURRENT;
		}
	}

	return retval;

}

static int crypto_tls_ticket_decrypt (
	CryptoTlsContext *context, const unsigned char *name, const unsigned char *iv,
	EVP_CIPHER_CTX *cipher, EVP_MAC_CTX *hmac
) {

	int retval = CRYPTO_TLS_TICKET_NONE;

	for (size_t index = 0; !retval && (index < context->n_ticket_keys); index++) {
		const CryptoTlsTicketKey *key = &context->ticket_keys[index];

		if (!memcmp (name, key->name, CRYPTO_TLS_TICKET_NAME_SIZE)) {
			const bool ready = (EVP_DecryptInit_ex (cipher, EVP_aes_256_cbc (), NULL, key->aes, iv) == 1)
				&& crypto_tls_ticket_hmac (hmac, key);

			// a ticket of an older key is renewed with the current one
			retval = !ready ? -1 : (index ? CRYPTO_TLS_TICKET_RENEW : CRYPTO_TLS_TICKET_CURRENT);
		}
	}

	return retval;

}

static int crypto_tls_ticket_callback (
	SSL *ssl, unsigned char *name, unsigned char *iv,
	EVP_CIPHER_CTX *cipher, EVP_MAC_CTX *hmac, int encrypt
) {

	CryptoTlsSession *session = (CryptoTlsSession *) SSL_get_app_data (ssl);
	CryptoTlsContext *context = session->context;

	(void) pthread_mutex_lock (&context->mutex);

	const int retval = encrypt ?
		crypto_tls_ticket_encrypt (context, name, iv, cipher, hmac) :
		crypto_tls_ticket_decrypt (context, name, iv, cipher, hmac);

	(void) pthread_mutex_unlock (&context->mutex);

	return retval;

}

// the shared keys of the tickets: 80 bytes each, the first encrypts
static bool crypto_tls_ticket_keys_read (
	CryptoTlsContext *context, const char *path, char *error, const size_t error_size
) {

	bool retval = false;

	FILE *file = fopen (path, CRYPTO_TLS_KEYS_FILE_MODE);
	if (file) {
		u8 bytes[CRYPTO_TLS_TICKET_KEY_SIZE];
		while (
			(context->n_ticket_keys < CRYPTO_TLS_TICKET_KEYS_MAX)
			&& (fread (bytes, 1, sizeof (bytes), file) == sizeof (bytes))
		) {
			CryptoTlsTicketKey *key = &context->ticket_keys[context->n_ticket_keys++];
			(void) memcpy (key->name, bytes, CRYPTO_TLS_TICKET_NAME_SIZE);
			(void) memcpy (key->hmac, bytes + CRYPTO_TLS_TICKET_NAME_SIZE, CRYPTO_TLS_TICKET_HMAC_SIZE);
			(void) memcpy (
				key->aes, bytes + CRYPTO_TLS_TICKET_NAME_SIZE + CRYPTO_TLS_TICKET_HMAC_SIZE, CRYPTO_TLS_TICKET_AES_SIZE
			);
		}

		(void) fclose (file);

		retval = (context->n_ticket_keys > 0);
		OPENSSL_cleanse (bytes, sizeof (bytes));
	}

	if (!retval) (void) snprintf (error, error_size, "the keys of the tickets %s could not be read", path);

	return retval;

}

#pragma endregion

#pragma region ocsp

// the status callback: the answer that was fetched, without a staple when there is none
static int crypto_tls_ocsp_status_callback (SSL *ssl, void *argument) {

	CryptoTlsCertificate *certificate = (CryptoTlsCertificate *) argument;
	CryptoTlsContext *context = certificate->context;

	int retval = SSL_TLSEXT_ERR_NOACK;

	// SNI can change the certificate after the callback was chosen
	SSL_CTX *ctx = SSL_get_SSL_CTX (ssl);
	for (size_t index = 0; index < context->n_certificates; index++) {
		if (context->certificates[index].ctx == ctx) certificate = &context->certificates[index];
	}

	(void) pthread_mutex_lock (&context->mutex);

	if (certificate->ocsp && certificate->ocsp_size) {
		unsigned char *copy = (unsigned char *) OPENSSL_memdup (certificate->ocsp, certificate->ocsp_size);
		if (copy && (SSL_set_tlsext_status_ocsp_resp (ssl, copy, (long) certificate->ocsp_size) == 1)) {
			retval = SSL_TLSEXT_ERR_OK;
		}

		else {
			OPENSSL_free (copy);
		}
	}

	(void) pthread_mutex_unlock (&context->mutex);

	return retval;

}

// the URL of the responder: the one of the settings or the first of the AIA of the certificate
static char *crypto_tls_ocsp_url (const CryptoTlsContext *context, X509 *leaf) {

	char *url = NULL;

	if (context->ocsp_responder) {
		url = strdup (context->ocsp_responder);
	}

	else {
		STACK_OF (OPENSSL_STRING) *urls = X509_get1_ocsp (leaf);
		if (urls && (sk_OPENSSL_STRING_num (urls) > 0)) url = strdup (sk_OPENSSL_STRING_value (urls, 0));
		X509_email_free (urls);
	}

	return url;

}

// a POST of the request to the responder, with its deadline: a responder that does not answer never holds the
// start of the cerver nor its maintenance
static OCSP_RESPONSE *crypto_tls_ocsp_request (const char *url, OCSP_REQUEST *request) {

	OCSP_RESPONSE *response = NULL;

	char *host = NULL;
	char *port = NULL;
	char *path = NULL;
	int secure = 0;

	if (OCSP_parse_url (url, &host, &port, &path, &secure) && !secure) {
		BIO *request_bio = ASN1_item_i2d_mem_bio (ASN1_ITEM_rptr (OCSP_REQUEST), (const ASN1_VALUE *) request);

		BIO *response_bio = request_bio ? OSSL_HTTP_transfer (
			NULL, host, port, path, 0, NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL,
			CRYPTO_TLS_OCSP_REQUEST_TYPE, request_bio, CRYPTO_TLS_OCSP_RESPONSE_TYPE, 1,
			CRYPTO_TLS_OCSP_MAX_RESPONSE, CRYPTO_TLS_OCSP_TIMEOUT_SECONDS, 0
		) : NULL;

		// the item form, the macro of d2i_OCSP_RESPONSE_bio () casts its functions
		if (response_bio) {
			response = (OCSP_RESPONSE *) ASN1_item_d2i_bio (ASN1_ITEM_rptr (OCSP_RESPONSE), response_bio, NULL);
		}

		BIO_free (response_bio);
		BIO_free (request_bio);
	}

	OPENSSL_free (host);
	OPENSSL_free (port);
	OPENSSL_free (path);

	return response;

}

// verifies an answer: its signature with the issuer, the status of the certificate and its validity
static bool crypto_tls_ocsp_verify (
	const CryptoTlsCertificate *certificate, OCSP_RESPONSE *response, OCSP_CERTID *id, time_t *next_update
) {

	bool retval = false;

	OCSP_BASICRESP *basic = (OCSP_response_status (response) == OCSP_RESPONSE_STATUS_SUCCESSFUL) ?
		OCSP_response_get1_basic (response) : NULL;

	X509_STORE *store = basic ? X509_STORE_new () : NULL;
	STACK_OF (X509) *chain = store ? sk_X509_new_null () : NULL;

	if (chain && (X509_STORE_add_cert (store, certificate->issuer) == 1) && sk_X509_push (chain, certificate->issuer)) {
		int status = 0;
		int reason = 0;
		ASN1_GENERALIZEDTIME *revoked = NULL;
		ASN1_GENERALIZEDTIME *this_update = NULL;
		ASN1_GENERALIZEDTIME *next = NULL;

		retval = (OCSP_basic_verify (basic, chain, store, OCSP_TRUSTOTHER) > 0)
			&& OCSP_resp_find_status (basic, id, &status, &reason, &revoked, &this_update, &next)
			&& (status == V_OCSP_CERTSTATUS_GOOD)
			&& OCSP_check_validity (this_update, next, CRYPTO_TLS_OCSP_SLACK_SECONDS, -1);

		int days = 0;
		int seconds = 0;
		*next_update = 0;
		if (retval && next && ASN1_TIME_diff (&days, &seconds, NULL, next)) {
			*next_update = time (NULL) + ((time_t) days * CRYPTO_TLS_SECONDS_PER_DAY) + seconds;
		}
	}

	sk_X509_free (chain);
	X509_STORE_free (store);
	OCSP_BASICRESP_free (basic);

	return retval;

}

// keeps the DER of an answer that verified
static bool crypto_tls_ocsp_keep (
	CryptoTlsContext *context, CryptoTlsCertificate *certificate, OCSP_RESPONSE *response, const time_t next_update
) {

	unsigned char *der = NULL;
	const int size = i2d_OCSP_RESPONSE (response, &der);

	if (size > 0) {
		(void) pthread_mutex_lock (&context->mutex);

		OPENSSL_free (certificate->ocsp);
		certificate->ocsp = der;
		certificate->ocsp_size = (size_t) size;
		certificate->ocsp_next_update = next_update;

		(void) pthread_mutex_unlock (&context->mutex);
	}

	return (size > 0);

}

static bool crypto_tls_ocsp_refresh_certificate (CryptoTlsContext *context, CryptoTlsCertificate *certificate) {

	bool retval = false;

	char *url = certificate->issuer ? crypto_tls_ocsp_url (context, certificate->leaf) : NULL;
	OCSP_REQUEST *request = url ? OCSP_REQUEST_new () : NULL;
	OCSP_CERTID *id = request ? OCSP_cert_to_id (NULL, certificate->leaf, certificate->issuer) : NULL;
	OCSP_CERTID *request_id = id ? OCSP_CERTID_dup (id) : NULL;

	if (request_id && OCSP_request_add0_id (request, request_id)) {
		OCSP_RESPONSE *response = crypto_tls_ocsp_request (url, request);

		time_t next_update = 0;
		if (response && crypto_tls_ocsp_verify (certificate, response, id, &next_update)) {
			retval = crypto_tls_ocsp_keep (context, certificate, response, next_update);
		}

		OCSP_RESPONSE_free (response);
	}

	else {
		OCSP_CERTID_free (request_id);
	}

	OCSP_CERTID_free (id);
	OCSP_REQUEST_free (request);
	free (url);

	ERR_clear_error ();

	return retval;

}

// the seconds until an answer is renewed: half of what is left of it
static time_t crypto_tls_ocsp_renew_seconds (const CryptoTlsCertificate *certificate, const time_t now) {

	time_t seconds = CRYPTO_TLS_OCSP_DEFAULT_SECONDS;

	if (certificate->ocsp_next_update > now) {
		seconds = (certificate->ocsp_next_update - now) / CRYPTO_TLS_OCSP_RENEW_DIVISOR;
	}

	return (seconds < CRYPTO_TLS_OCSP_RETRY_SECONDS) ? CRYPTO_TLS_OCSP_RETRY_SECONDS : seconds;

}

u8 cerver_crypto_provider_tls_context_refresh_ocsp (
	CryptoTlsContext *context, time_t *next_seconds
) {

	u8 retval = CRYPTO_TLS_OCSP_NOT_STAPLED;
	*next_seconds = 0;

	if (context && context->server && context->ocsp_stapling) {
		retval = 0;
		time_t next = 0;
		const time_t now = time (NULL);

		for (size_t index = 0; index < context->n_certificates; index++) {
			CryptoTlsCertificate *certificate = &context->certificates[index];

			const bool refreshed = crypto_tls_ocsp_refresh_certificate (context, certificate);
			if (!refreshed) retval = 1;

			const time_t seconds = refreshed ?
				crypto_tls_ocsp_renew_seconds (certificate, now) : CRYPTO_TLS_OCSP_RETRY_SECONDS;

			if (!next || (seconds < next)) next = seconds;
		}

		*next_seconds = next;
	}

	return retval;

}

#pragma endregion

#pragma region keys

// the key in a key store, by its URI (pkcs11:...)
static EVP_PKEY *crypto_tls_key_from_store (const CryptoTlsContext *context, const char *uri) {

	EVP_PKEY *key = NULL;

	OSSL_STORE_CTX *store = OSSL_STORE_open_ex (
		uri, NULL, context->key_store_properties, NULL, NULL, NULL, NULL, NULL
	);

	while (store && !key && !OSSL_STORE_eof (store)) {
		OSSL_STORE_INFO *info = OSSL_STORE_load (store);
		if (info && (OSSL_STORE_INFO_get_type (info) == OSSL_STORE_INFO_PKEY)) key = OSSL_STORE_INFO_get1_PKEY (info);
		OSSL_STORE_INFO_free (info);

		if (!info) break;
	}

	if (store) (void) OSSL_STORE_close (store);

	return key;

}

static bool crypto_tls_use_key (
	const CryptoTlsContext *context, SSL_CTX *ctx, const char *key_path, char *error, const size_t error_size
) {

	bool retval = false;

	if (tls_settings_key_is_uri (key_path)) {
		EVP_PKEY *key = crypto_tls_key_from_store (context, key_path);
		retval = key && (SSL_CTX_use_PrivateKey (ctx, key) == 1);
		EVP_PKEY_free (key);
	}

	else {
		retval = (SSL_CTX_use_PrivateKey_file (ctx, key_path, SSL_FILETYPE_PEM) == 1);
	}

	if (!retval) crypto_tls_error (error, error_size, "the key could not be loaded:", key_path);
	else if (SSL_CTX_check_private_key (ctx) != 1) {
		crypto_tls_error (error, error_size, "the key does not match its certificate:", key_path);
		retval = false;
	}

	return retval;

}

static bool crypto_tls_load_key_store (
	CryptoTlsContext *context, const TlsSettings *settings, char *error, const size_t error_size
) {

	bool retval = true;

	if (settings->key_store_name) {
		// loading another provider stops the default from loading by itself
		context->default_provider = OSSL_PROVIDER_load (NULL, CRYPTO_TLS_DEFAULT_PROVIDER);
		context->key_store = OSSL_PROVIDER_load (NULL, settings->key_store_name);

		if (settings->key_store_properties) context->key_store_properties = strdup (settings->key_store_properties);

		retval = (context->default_provider != NULL) && (context->key_store != NULL);
		if (!retval) crypto_tls_error (error, error_size, "the key store could not be loaded:", settings->key_store_name);
	}

	return retval;

}

#pragma endregion

#pragma region validation

// the dates of a certificate: an expired one or one that is not valid yet stops the start
static bool crypto_tls_validate_dates (X509 *leaf, const char *path, char *error, const size_t error_size) {

	bool retval = true;

	if (X509_cmp_current_time (X509_get0_notAfter (leaf)) <= 0) {
		(void) snprintf (error, error_size, "the certificate %s expired", path);
		retval = false;
	}

	else if (X509_cmp_current_time (X509_get0_notBefore (leaf)) > 0) {
		(void) snprintf (error, error_size, "the certificate %s is not valid yet", path);
		retval = false;
	}

	else {
		int days = 0;
		int seconds = 0;
		if (ASN1_TIME_diff (&days, &seconds, NULL, X509_get0_notAfter (leaf)) && (days < TLS_CERTIFICATE_EXPIRY_WARNING_DAYS)) {
			cerver_log_warning ("The certificate %s expires in %d days", path, days);
		}
	}

	return retval;

}

// each certificate of the chain is issued by the next one
static bool crypto_tls_validate_chain (
	SSL_CTX *ctx, X509 *leaf, X509 **issuer, const char *path, char *error, const size_t error_size
) {

	bool retval = true;

	STACK_OF (X509) *chain = NULL;
	(void) SSL_CTX_get0_chain_certs (ctx, &chain);

	X509 *subject = leaf;
	const int count = chain ? sk_X509_num (chain) : 0;
	for (int index = 0; retval && (index < count); index++) {
		X509 *next = sk_X509_value (chain, index);
		if (X509_check_issued (next, subject) != X509_V_OK) {
			(void) snprintf (error, error_size, "the chain of %s is not in order", path);
			retval = false;
		}

		if (!index) *issuer = next;
		subject = next;
	}

	return retval;

}

// a certificate that must staple (RFC 7633) without stapling leaves a warning
static void crypto_tls_check_must_staple (X509 *leaf, const bool stapling, const char *path) {

	if (!stapling && (X509_get_ext_by_NID (leaf, NID_tlsfeature, -1) >= 0)) {
		cerver_log_warning ("The certificate %s must staple but the OCSP stapling is off", path);
	}

}

static bool crypto_tls_validate_names (const CryptoTlsContext *context, char *error, const size_t error_size) {

	bool retval = true;

	for (size_t index = 0; retval && (index < context->n_certificates); index++) {
		const CryptoTlsCertificate *certificate = &context->certificates[index];

		for (size_t name = 0; retval && (name < certificate->n_names); name++) {
			for (size_t other = index + 1; retval && (other < context->n_certificates); other++) {
				const CryptoTlsCertificate *another = &context->certificates[other];

				for (size_t other_name = 0; retval && (other_name < another->n_names); other_name++) {
					if (!strcasecmp (certificate->names[name], another->names[other_name])) {
						(void) snprintf (error, error_size, "the name %s is in two certificates", certificate->names[name]);
						retval = false;
					}
				}
			}
		}
	}

	return retval;

}

#pragma endregion

#pragma region ciphers

// a cipher of TLS 1.2 without forward secrecy or without AEAD lowers the security
static void crypto_tls_check_ciphers (SSL_CTX *ctx) {

	STACK_OF (SSL_CIPHER) *ciphers = SSL_CTX_get_ciphers (ctx);
	const int count = ciphers ? sk_SSL_CIPHER_num (ciphers) : 0;

	for (int index = 0; index < count; index++) {
		const SSL_CIPHER *cipher = sk_SSL_CIPHER_value (ciphers, index);
		const int key_exchange = SSL_CIPHER_get_kx_nid (cipher);
		const bool forward = (key_exchange == NID_kx_ecdhe) || (key_exchange == NID_kx_dhe) || (key_exchange == NID_kx_any);

		if (!forward || !SSL_CIPHER_is_aead (cipher)) {
			cerver_log_warning ("The cipher %s lowers the security of TLS", SSL_CIPHER_standard_name (cipher));
		}
	}

}

// appends the OpenSSL name of a cipher of TLS 1.2 by its IANA name
static bool crypto_tls_cipher_name (
	STACK_OF (SSL_CIPHER) *all, const char *iana, char *names, const size_t names_size
) {

	bool found = false;

	const int count = sk_SSL_CIPHER_num (all);
	for (int index = 0; !found && (index < count); index++) {
		const SSL_CIPHER *cipher = sk_SSL_CIPHER_value (all, index);
		const char *standard = SSL_CIPHER_standard_name (cipher);

		if (standard && !strcmp (standard, iana)) {
			if (*names) (void) strncat (names, CRYPTO_TLS_LIST_SEPARATOR, names_size - strlen (names) - 1);
			(void) strncat (names, SSL_CIPHER_get_name (cipher), names_size - strlen (names) - 1);
			found = true;
		}
	}

	return found;

}

// the ciphers of TLS 1.2 by their IANA names, as a list of OpenSSL
static bool crypto_tls_ciphers12 (SSL_CTX *ctx, const char *iana, char *error, const size_t error_size) {

	bool retval = false;

	SSL_CTX *every = SSL_CTX_new (TLS_method ());
	char *copy = strdup (iana);
	const size_t names_size = strlen (iana) + 1;
	char *names = (char *) calloc (1, names_size);

	if (every && copy && names && (SSL_CTX_set_cipher_list (every, CRYPTO_TLS_CIPHERS_ALL) == 1)) {
		retval = true;

		char *saveptr = NULL;
		for (char *token = strtok_r (copy, CRYPTO_TLS_LIST_SEPARATOR, &saveptr); retval && token; token = strtok_r (NULL, CRYPTO_TLS_LIST_SEPARATOR, &saveptr)) {
			retval = crypto_tls_cipher_name (SSL_CTX_get_ciphers (every), token, names, names_size);
			if (!retval) (void) snprintf (error, error_size, "the cipher %s is not known", token);
		}

		if (retval && (SSL_CTX_set_cipher_list (ctx, names) != 1)) {
			crypto_tls_error (error, error_size, "the ciphers of TLS 1.2 were not accepted:", iana);
			retval = false;
		}
	}

	free (names);
	free (copy);
	SSL_CTX_free (every);

	return retval;

}

static bool crypto_tls_set_ciphers (
	SSL_CTX *ctx, const TlsSettings *settings, char *error, const size_t error_size
) {

	bool retval = settings->ciphers12 ?
		crypto_tls_ciphers12 (ctx, settings->ciphers12, error, error_size) :
		(SSL_CTX_set_cipher_list (ctx, CRYPTO_TLS_CIPHERS12_DEFAULT) == 1);

	if (retval) {
		retval = (SSL_CTX_set_ciphersuites (ctx, settings->ciphers13 ? settings->ciphers13 : CRYPTO_TLS_CIPHERS13_DEFAULT) == 1);
		if (!retval) crypto_tls_error (error, error_size, "the ciphers of TLS 1.3 were not accepted:", settings->ciphers13);
	}

	if (retval && settings->ciphers12) crypto_tls_check_ciphers (ctx);

	if (retval) {
		if (settings->groups) {
			retval = (SSL_CTX_set1_groups_list (ctx, settings->groups) == 1);
			if (!retval) crypto_tls_error (error, error_size, "the groups were not accepted:", settings->groups);
		}

		// a provider without ML-KEM keeps the classic groups
		else if (SSL_CTX_set1_groups_list (ctx, CRYPTO_TLS_GROUPS_DEFAULT) != 1) {
			ERR_clear_error ();
			retval = (SSL_CTX_set1_groups_list (ctx, CRYPTO_TLS_GROUPS_CLASSIC) == 1);
		}
	}

	return retval;

}

#pragma endregion

#pragma region context

static int crypto_tls_min_version (const TlsVersion version) {

	return (version == TLS_VERSION_1_3) ? TLS1_3_VERSION : TLS1_2_VERSION;

}

// what every SSL_CTX of cerver has: RFC 9325 and RFC 8996
static bool crypto_tls_ctx_common (
	SSL_CTX *ctx, const TlsSettings *settings, char *error, const size_t error_size
) {

	(void) SSL_CTX_set_min_proto_version (ctx, crypto_tls_min_version (settings->min_version));

	uint64_t options = SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION;
	if (settings->ktls) options |= SSL_OP_ENABLE_KTLS;
	(void) SSL_CTX_set_options (ctx, options);

	(void) SSL_CTX_set_mode (ctx, SSL_MODE_RELEASE_BUFFERS);

	return crypto_tls_set_ciphers (ctx, settings, error, error_size);

}

static bool crypto_tls_ctx_trust (SSL_CTX *ctx, const TlsSettings *settings, const bool server) {

	bool retval = true;

	if (settings->ca_file || settings->ca_path) {
		retval = (SSL_CTX_load_verify_locations (ctx, settings->ca_file, settings->ca_path) == 1);

		if (retval && server && settings->ca_file) {
			STACK_OF (X509_NAME) *names = SSL_load_client_CA_file (settings->ca_file);
			if (names) SSL_CTX_set_client_CA_list (ctx, names);
		}
	}

	// a client without its own authorities trusts the store of the system
	else if (!server) {
		retval = (SSL_CTX_set_default_verify_paths (ctx) == 1);
	}

	return retval;

}

static int crypto_tls_verify_mode (const TlsClientVerify verify) {

	int mode = SSL_VERIFY_NONE;

	if (verify == TLS_CLIENT_VERIFY_OPTIONAL) mode = SSL_VERIFY_PEER;
	else if (verify == TLS_CLIENT_VERIFY_REQUIRED) mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

	return mode;

}

// the macros of OpenSSL for these two callbacks cast them to void (*) (void), that the callback ctrl takes
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-strict"

static void crypto_tls_ctx_set_server_name_callback (SSL_CTX *ctx, CryptoTlsContext *context) {

	(void) SSL_CTX_set_tlsext_servername_callback (ctx, crypto_tls_server_name_callback);
	(void) SSL_CTX_set_tlsext_servername_arg (ctx, context);

}

static void crypto_tls_ctx_set_status_callback (SSL_CTX *ctx, CryptoTlsCertificate *certificate) {

	(void) SSL_CTX_set_tlsext_status_cb (ctx, crypto_tls_ocsp_status_callback);
	(void) SSL_CTX_set_tlsext_status_arg (ctx, certificate);

}

#pragma clang diagnostic pop

// the sessions, the tickets, the early data and the callbacks of an SSL_CTX of a cerver
static void crypto_tls_ctx_server_sessions (CryptoTlsContext *context, SSL_CTX *ctx, const TlsSettings *settings) {

	(void) SSL_CTX_set_session_id_context (
		ctx, (const unsigned char *) CRYPTO_TLS_SESSION_ID_CONTEXT, sizeof (CRYPTO_TLS_SESSION_ID_CONTEXT) - 1
	);

	(void) SSL_CTX_set_session_cache_mode (ctx, SSL_SESS_CACHE_SERVER);
	(void) SSL_CTX_sess_set_cache_size (ctx, TLS_SESSION_CACHE_SIZE);

	if (settings->tickets) (void) SSL_CTX_set_tlsext_ticket_key_evp_cb (ctx, crypto_tls_ticket_callback);
	else (void) SSL_CTX_set_options (ctx, SSL_OP_NO_TICKET);

	(void) SSL_CTX_set_max_early_data (ctx, settings->early_data);
	(void) SSL_CTX_set_recv_max_early_data (ctx, settings->early_data);

	crypto_tls_ctx_set_server_name_callback (ctx, context);

	if (context->alpn_size) SSL_CTX_set_alpn_select_cb (ctx, crypto_tls_alpn_select_callback, context);

	SSL_CTX_set_verify (ctx, crypto_tls_verify_mode (settings->client_verify), NULL);

}

static bool crypto_tls_certificate_create (
	CryptoTlsContext *context, CryptoTlsCertificate *certificate,
	const TlsSettings *settings, const TlsCertificatePaths *paths,
	char *error, const size_t error_size
) {

	certificate->context = context;
	certificate->ctx = SSL_CTX_new (TLS_server_method ());

	bool retval = (certificate->ctx != NULL) && crypto_tls_ctx_common (certificate->ctx, settings, error, error_size);

	if (retval && (SSL_CTX_use_certificate_chain_file (certificate->ctx, paths->chain) != 1)) {
		crypto_tls_error (error, error_size, "the certificate could not be loaded:", paths->chain);
		retval = false;
	}

	if (retval) {
		certificate->leaf = SSL_CTX_get0_certificate (certificate->ctx);
		retval = crypto_tls_use_key (context, certificate->ctx, paths->key, error, error_size)
			&& crypto_tls_validate_dates (certificate->leaf, paths->chain, error, error_size)
			&& crypto_tls_validate_chain (certificate->ctx, certificate->leaf, &certificate->issuer, paths->chain, error, error_size)
			&& crypto_tls_certificate_names (certificate);
	}

	if (retval && !crypto_tls_ctx_trust (certificate->ctx, settings, true)) {
		crypto_tls_error (error, error_size, "the authorities could not be loaded:", settings->ca_file);
		retval = false;
	}

	if (retval) {
		crypto_tls_check_must_staple (certificate->leaf, settings->ocsp_stapling, paths->chain);
		crypto_tls_ctx_server_sessions (context, certificate->ctx, settings);

		if (settings->ocsp_stapling) crypto_tls_ctx_set_status_callback (certificate->ctx, certificate);
	}

	return retval;

}

// the sessions of a client: kept by their destination when a ticket arrives
static int crypto_tls_client_new_session (SSL *ssl, SSL_SESSION *ssl_session) {

	int retval = 0;

	CryptoTlsSession *session = (CryptoTlsSession *) SSL_get_app_data (ssl);
	if (session && session->destination[0]) {
		CryptoTlsContext *context = session->context;

		(void) pthread_mutex_lock (&context->mutex);

		// the entry of the destination, or the one used the longest time ago
		CryptoTlsCachedSession *entry = &context->cache[0];
		for (size_t index = 0; index < CRYPTO_TLS_CLIENT_CACHE_SIZE; index++) {
			CryptoTlsCachedSession *candidate = &context->cache[index];
			if (!strcmp (candidate->destination, session->destination)) { entry = candidate; break; }
			if (candidate->used < entry->used) entry = candidate;
		}

		SSL_SESSION_free (entry->session);
		entry->session = ssl_session;
		entry->used = ++context->cache_clock;
		(void) snprintf (entry->destination, sizeof (entry->destination), "%s", session->destination);

		(void) pthread_mutex_unlock (&context->mutex);

		// the cache keeps the reference
		retval = 1;
	}

	return retval;

}

static bool crypto_tls_client_create (
	CryptoTlsContext *context, const TlsSettings *settings, char *error, const size_t error_size
) {

	context->client_ctx = SSL_CTX_new (TLS_client_method ());
	SSL_CTX *ctx = context->client_ctx;

	bool retval = (ctx != NULL) && crypto_tls_ctx_common (ctx, settings, error, error_size);

	if (retval && !crypto_tls_ctx_trust (ctx, settings, false)) {
		crypto_tls_error (error, error_size, "the authorities could not be loaded:", settings->ca_file);
		retval = false;
	}

	if (retval && settings->client_certificate.chain) {
		retval = (SSL_CTX_use_certificate_chain_file (ctx, settings->client_certificate.chain) == 1);
		if (!retval) crypto_tls_error (error, error_size, "the certificate could not be loaded:", settings->client_certificate.chain);
		else retval = crypto_tls_use_key (context, ctx, settings->client_certificate.key, error, error_size);
	}

	if (retval) {
		SSL_CTX_set_verify (ctx, SSL_VERIFY_PEER, NULL);

		(void) SSL_CTX_set_session_cache_mode (ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
		SSL_CTX_sess_set_new_cb (ctx, crypto_tls_client_new_session);

		if (context->alpn_size && SSL_CTX_set_alpn_protos (ctx, context->alpn, context->alpn_size)) {
			(void) snprintf (error, error_size, "the protocols of ALPN were not accepted");
			retval = false;
		}
	}

	return retval;

}

static bool crypto_tls_context_alpn (
	CryptoTlsContext *context, const TlsSettings *settings, const bool web, char *error, const size_t error_size
) {

	bool retval = true;

	const char *protocols = settings->alpn ? settings->alpn : (web ? CRYPTO_TLS_WEB_ALPN : NULL);
	if (protocols) {
		const size_t size = tls_settings_alpn_wire (protocols, context->alpn, sizeof (context->alpn));
		context->alpn_size = (unsigned int) size;

		if (!size) {
			(void) snprintf (error, error_size, "the protocols of ALPN %s are not valid", protocols);
			retval = false;
		}
	}

	return retval;

}

static bool crypto_tls_server_create (
	CryptoTlsContext *context, const TlsSettings *settings, char *error, const size_t error_size
) {

	bool retval = (settings->n_certificates > 0);
	if (!retval) (void) snprintf (error, error_size, "a cerver with TLS needs a certificate");

	if (retval) {
		context->certificates = (CryptoTlsCertificate *) calloc (settings->n_certificates, sizeof (CryptoTlsCertificate));
		retval = (context->certificates != NULL);
	}

	for (size_t index = 0; retval && (index < settings->n_certificates); index++) {
		retval = crypto_tls_certificate_create (
			context, &context->certificates[index], settings, &settings->certificates[index], error, error_size
		);

		context->n_certificates = index + 1;
	}

	if (retval) retval = crypto_tls_validate_names (context, error, error_size);

	if (retval && settings->ticket_keys_file) {
		context->shared_ticket_keys = true;
		retval = crypto_tls_ticket_keys_read (context, settings->ticket_keys_file, error, error_size);
	}

	else if (retval && settings->tickets) {
		crypto_tls_ticket_keys_rotate_locked (context, time (NULL));
	}

	return retval;

}

CryptoTlsContext *cerver_crypto_provider_tls_context_create (
	const TlsSettings *settings, const bool server, const bool web,
	char *error, const size_t error_size
) {

	CryptoTlsContext *context = (CryptoTlsContext *) calloc (1, sizeof (CryptoTlsContext));
	if (context) {
		context->references = 1;
		context->server = server;
		context->verify = settings->client_verify;
		context->early_data = settings->early_data;
		context->tickets = settings->tickets;
		context->ocsp_stapling = settings->ocsp_stapling;
		if (settings->ocsp_responder) context->ocsp_responder = strdup (settings->ocsp_responder);

		(void) pthread_mutex_init (&context->mutex, NULL);

		ERR_clear_error ();

		bool created = (crypto_tls_bio_get_method () != NULL)
			&& crypto_tls_load_key_store (context, settings, error, error_size)
			&& crypto_tls_context_alpn (context, settings, web, error, error_size);

		if (created) {
			created = server ?
				crypto_tls_server_create (context, settings, error, error_size) :
				crypto_tls_client_create (context, settings, error, error_size);
		}

		if (!created) {
			cerver_crypto_provider_tls_context_release (context);
			context = NULL;
		}
	}

	else {
		(void) snprintf (error, error_size, "no memory for the context of TLS");
	}

	return context;

}

CryptoTlsContext *cerver_crypto_provider_tls_context_reference (CryptoTlsContext *context) {

	if (context) (void) __atomic_add_fetch (&context->references, 1, __ATOMIC_ACQ_REL);

	return context;

}

static void crypto_tls_certificate_end (CryptoTlsCertificate *certificate) {

	for (size_t index = 0; index < certificate->n_names; index++) free (certificate->names[index]);
	free (certificate->names);

	OPENSSL_free (certificate->ocsp);

	SSL_CTX_free (certificate->ctx);

}

void cerver_crypto_provider_tls_context_release (CryptoTlsContext *context) {

	if (context && !__atomic_sub_fetch (&context->references, 1, __ATOMIC_ACQ_REL)) {
		for (size_t index = 0; index < context->n_certificates; index++) {
			crypto_tls_certificate_end (&context->certificates[index]);
		}

		free (context->certificates);

		for (size_t index = 0; index < CRYPTO_TLS_CLIENT_CACHE_SIZE; index++) {
			SSL_SESSION_free (context->cache[index].session);
		}

		SSL_CTX_free (context->client_ctx);

		OPENSSL_cleanse (context->ticket_keys, sizeof (context->ticket_keys));

		if (context->key_store) (void) OSSL_PROVIDER_unload (context->key_store);
		if (context->default_provider) (void) OSSL_PROVIDER_unload (context->default_provider);

		free (context->key_store_properties);
		free (context->ocsp_responder);

		(void) pthread_mutex_destroy (&context->mutex);

		free (context);
	}

}

i64 cerver_crypto_provider_tls_context_expiry_seconds (const CryptoTlsContext *context) {

	i64 expiry = 0;

	for (size_t index = 0; context && (index < context->n_certificates); index++) {
		int days = 0;
		int seconds = 0;

		if (ASN1_TIME_diff (&days, &seconds, NULL, X509_get0_notAfter (context->certificates[index].leaf))) {
			const i64 left = ((i64) days * CRYPTO_TLS_SECONDS_PER_DAY) + seconds;
			if (!index || (left < expiry)) expiry = left;
		}
	}

	return expiry;

}

#pragma endregion

#pragma region session

static bool crypto_tls_is_address (const char *name) {

	u8 address[CRYPTO_TLS_IPV6_SIZE];

	return (inet_pton (AF_INET, name, address) == 1) || (inet_pton (AF_INET6, name, address) == 1);

}

// a client asks for the name with SNI and verifies it in the certificate (RFC 9525), an address by its IP
static bool crypto_tls_session_client (CryptoTlsSession *session, const char *server_name, const char *destination) {

	bool retval = true;

	if (server_name && crypto_tls_is_address (server_name)) {
		retval = (X509_VERIFY_PARAM_set1_ip_asc (SSL_get0_param (session->ssl), server_name) == 1);
	}

	else if (server_name) {
		retval = (SSL_set_tlsext_host_name (session->ssl, server_name) == 1) && (SSL_set1_host (session->ssl, server_name) == 1);
	}

	if (retval && destination) {
		(void) snprintf (session->destination, sizeof (session->destination), "%s", destination);

		CryptoTlsContext *context = session->context;
		(void) pthread_mutex_lock (&context->mutex);

		for (size_t index = 0; index < CRYPTO_TLS_CLIENT_CACHE_SIZE; index++) {
			CryptoTlsCachedSession *entry = &context->cache[index];
			if (entry->session && !strcmp (entry->destination, destination)) {
				(void) SSL_set_session (session->ssl, entry->session);
				entry->used = ++context->cache_clock;
			}
		}

		(void) pthread_mutex_unlock (&context->mutex);
	}

	SSL_set_connect_state (session->ssl);

	return retval;

}

CryptoTlsSession *cerver_crypto_provider_tls_session_create (
	CryptoTlsContext *context, TlsConnection *connection,
	const char *server_name, const char *destination
) {

	CryptoTlsSession *session = (CryptoTlsSession *) calloc (1, sizeof (CryptoTlsSession));
	if (session) {
		session->context = cerver_crypto_provider_tls_context_reference (context);
		session->connection = connection;

		SSL_CTX *ctx = context->server ? context->certificates[0].ctx : context->client_ctx;
		session->ssl = SSL_new (ctx);
		session->bio = session->ssl ? BIO_new (crypto_tls_bio_get_method ()) : NULL;

		bool created = (session->bio != NULL);
		if (created) {
			BIO_set_data (session->bio, session);
			SSL_set_bio (session->ssl, session->bio, session->bio);
			(void) SSL_set_app_data (session->ssl, session);

			if (context->server) SSL_set_accept_state (session->ssl);
			else created = crypto_tls_session_client (session, server_name, destination);
		}

		if (!created) {
			if (!session->ssl) BIO_free (session->bio);
			cerver_crypto_provider_tls_session_delete (session);
			session = NULL;
		}
	}

	return session;

}

void cerver_crypto_provider_tls_session_delete (CryptoTlsSession *session) {

	if (session) {
		// the SSL owns its BIO
		SSL_free (session->ssl);

		cerver_crypto_provider_tls_context_release (session->context);

		free (session);
	}

}

CryptoTlsResult cerver_crypto_provider_tls_session_handshake (
	CryptoTlsSession *session, TlsFailure *failure
) {

	ERR_clear_error ();

	CryptoTlsResult result = CRYPTO_TLS_RESULT_DONE;

	const int value = SSL_do_handshake (session->ssl);
	if (value != 1) {
		const int ssl_error = SSL_get_error (session->ssl, value);
		result = crypto_tls_result (ssl_error);

		if (result != CRYPTO_TLS_RESULT_WANT_READ) *failure = crypto_tls_failure (ssl_error);
	}

	return result;

}

CryptoTlsResult cerver_crypto_provider_tls_session_read_early (
	CryptoTlsSession *session, void *buffer, const size_t size, size_t *read
) {

	CryptoTlsResult result = CRYPTO_TLS_RESULT_DONE;
	*read = 0;

	if (session->context->early_data) {
		ERR_clear_error ();

		const int value = SSL_read_early_data (session->ssl, buffer, size, read);
		if (value == SSL_READ_EARLY_DATA_SUCCESS) {
			result = CRYPTO_TLS_RESULT_WANT_READ;
		}

		else if (value == SSL_READ_EARLY_DATA_ERROR) {
			result = crypto_tls_result (SSL_get_error (session->ssl, 0));
			if (result != CRYPTO_TLS_RESULT_WANT_READ) result = CRYPTO_TLS_RESULT_FAILED;
		}
	}

	return result;

}

CryptoTlsResult cerver_crypto_provider_tls_session_read (
	CryptoTlsSession *session, void *buffer, const size_t size, size_t *read
) {

	ERR_clear_error ();

	CryptoTlsResult result = CRYPTO_TLS_RESULT_DONE;

	const int value = SSL_read_ex (session->ssl, buffer, size, read);
	if (value != 1) {
		result = crypto_tls_result (SSL_get_error (session->ssl, value));
		ERR_clear_error ();
	}

	return result;

}

CryptoTlsResult cerver_crypto_provider_tls_session_write (
	CryptoTlsSession *session, const void *data, const size_t size
) {

	ERR_clear_error ();

	size_t written = 0;
	const int value = SSL_write_ex (session->ssl, data, size, &written);

	if (value != 1) ERR_clear_error ();

	return ((value == 1) && (written == size)) ? CRYPTO_TLS_RESULT_DONE : CRYPTO_TLS_RESULT_FAILED;

}

bool cerver_crypto_provider_tls_session_pending (const CryptoTlsSession *session) {

	return SSL_pending (session->ssl) > 0;

}

void cerver_crypto_provider_tls_session_close (CryptoTlsSession *session) {

	ERR_clear_error ();

	(void) SSL_shutdown (session->ssl);

	ERR_clear_error ();

}

bool cerver_crypto_provider_tls_session_resumed (const CryptoTlsSession *session) {

	return SSL_session_reused (session->ssl) == 1;

}

bool cerver_crypto_provider_tls_session_early_data_accepted (const CryptoTlsSession *session) {

	return SSL_get_early_data_status (session->ssl) == SSL_EARLY_DATA_ACCEPTED;

}

const char *cerver_crypto_provider_tls_session_server_name (const CryptoTlsSession *session) {

	return SSL_get_servername (session->ssl, TLSEXT_NAMETYPE_host_name);

}

const u8 *cerver_crypto_provider_tls_session_alpn (
	const CryptoTlsSession *session, unsigned int *size
) {

	const unsigned char *protocol = NULL;
	SSL_get0_alpn_selected (session->ssl, &protocol, size);

	return protocol;

}

TlsVersion cerver_crypto_provider_tls_session_version (const CryptoTlsSession *session) {

	TlsVersion version = TLS_VERSION_NONE;

	const int value = SSL_version (session->ssl);
	if (value == TLS1_3_VERSION) version = TLS_VERSION_1_3;
	else if (value == TLS1_2_VERSION) version = TLS_VERSION_1_2;

	return version;

}

#pragma endregion

#pragma region certificate

// a name of the certificate in the one line form of RFC 2253
static char *crypto_tls_name_string (const X509_NAME *name) {

	char *string = NULL;

	BIO *memory = BIO_new (BIO_s_mem ());
	if (memory && (X509_NAME_print_ex (memory, name, 0, XN_FLAG_RFC2253) >= 0)) {
		char *data = NULL;
		const long size = BIO_get_mem_data (memory, &data);
		string = strndup (data ? data : "", (size_t) size);
	}

	BIO_free (memory);

	return string;

}

static char *crypto_tls_serial_string (X509 *certificate) {

	char *string = NULL;

	BIGNUM *number = ASN1_INTEGER_to_BN (X509_get0_serialNumber (certificate), NULL);
	char *hex = number ? BN_bn2hex (number) : NULL;
	if (hex) string = strdup (hex);

	OPENSSL_free (hex);
	BN_free (number);

	return string;

}

// appends a name of the subject alternative name to the list
static bool crypto_tls_alternative_append (char **list, const char *value, const size_t size) {

	const size_t used = *list ? strlen (*list) : 0;
	const size_t separator = used ? strlen (CRYPTO_TLS_NAMES_SEPARATOR) : 0;

	char *grown = (char *) realloc (*list, used + separator + size + 1);
	if (grown) {
		if (separator) (void) memcpy (grown + used, CRYPTO_TLS_NAMES_SEPARATOR, separator);
		(void) memcpy (grown + used + separator, value, size);
		grown[used + separator + size] = '\0';
		*list = grown;
	}

	return (grown != NULL);

}

static bool crypto_tls_alternative_address (char **list, const ASN1_OCTET_STRING *octets) {

	char address[CRYPTO_TLS_ADDRESS_SIZE] = { 0 };

	const int length = ASN1_STRING_length (octets);
	const int family = (length == CRYPTO_TLS_IPV6_SIZE) ? AF_INET6 : AF_INET;

	bool retval = true;
	if (((length == CRYPTO_TLS_IPV6_SIZE) || (length == CRYPTO_TLS_IPV4_SIZE))
		&& inet_ntop (family, ASN1_STRING_get0_data (octets), address, sizeof (address))) {
		retval = crypto_tls_alternative_append (list, address, strlen (address));
	}

	return retval;

}

static char *crypto_tls_alternative_names (X509 *certificate) {

	char *list = NULL;
	bool valid = true;

	GENERAL_NAMES *names = (GENERAL_NAMES *) X509_get_ext_d2i (certificate, NID_subject_alt_name, NULL, NULL);
	const int count = names ? sk_GENERAL_NAME_num (names) : 0;

	for (int index = 0; valid && (index < count); index++) {
		const GENERAL_NAME *name = sk_GENERAL_NAME_value (names, index);

		if ((name->type == GEN_DNS) || (name->type == GEN_EMAIL) || (name->type == GEN_URI)) {
			const ASN1_IA5STRING *text = name->d.ia5;
			valid = crypto_tls_alternative_append (
				&list, (const char *) ASN1_STRING_get0_data (text), (size_t) ASN1_STRING_length (text)
			);
		}

		else if (name->type == GEN_IPADD) {
			valid = crypto_tls_alternative_address (&list, name->d.iPAddress);
		}
	}

	GENERAL_NAMES_free (names);

	return list;

}

static void crypto_tls_fingerprint (X509 *certificate, char fingerprint[TLS_CERTIFICATE_FINGERPRINT_SIZE]) {

	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;

	fingerprint[0] = '\0';
	if (X509_digest (certificate, EVP_sha256 (), digest, &size) == 1) {
		for (unsigned int index = 0; index < size; index++) {
			(void) snprintf (fingerprint + (index * (CRYPTO_TLS_HEX_BYTE_SIZE - 1)), CRYPTO_TLS_HEX_BYTE_SIZE, "%02x", digest[index]);
		}
	}

}

static time_t crypto_tls_time (const ASN1_TIME *asn1) {

	struct tm when;
	(void) memset (&when, 0, sizeof (struct tm));

	return (ASN1_TIME_to_tm (asn1, &when) == 1) ? timegm (&when) : 0;

}

TlsCertificate *cerver_crypto_provider_tls_session_peer_certificate (
	const CryptoTlsSession *session
) {

	TlsCertificate *certificate = NULL;

	X509 *peer = SSL_get0_peer_certificate (session->ssl);
	if (peer && (SSL_get_verify_result (session->ssl) == CRYPTO_TLS_VERIFY_OK)) {
		certificate = (TlsCertificate *) calloc (1, sizeof (TlsCertificate));
	}

	if (certificate) {
		certificate->subject = crypto_tls_name_string (X509_get_subject_name (peer));
		certificate->issuer = crypto_tls_name_string (X509_get_issuer_name (peer));
		certificate->serial = crypto_tls_serial_string (peer);
		certificate->alternative_names = crypto_tls_alternative_names (peer);
		crypto_tls_fingerprint (peer, certificate->fingerprint);
		certificate->not_before = crypto_tls_time (X509_get0_notBefore (peer));
		certificate->not_after = crypto_tls_time (X509_get0_notAfter (peer));

		if (X509_up_ref (peer) == 1) certificate->provider_certificate = peer;

		if (!certificate->subject || !certificate->issuer || !certificate->serial) {
			tls_certificate_delete (certificate);
			certificate = NULL;
		}
	}

	return certificate;

}

char *cerver_crypto_provider_tls_certificate_pem (const void *provider_certificate) {

	char *pem = NULL;

	BIO *memory = BIO_new (BIO_s_mem ());
	if (memory && (PEM_write_bio_X509 (memory, (X509 *) provider_certificate) == 1)) {
		char *data = NULL;
		const long size = BIO_get_mem_data (memory, &data);
		pem = strndup (data ? data : "", (size_t) size);
	}

	BIO_free (memory);

	return pem;

}

void cerver_crypto_provider_tls_certificate_release (void *provider_certificate) {

	X509_free ((X509 *) provider_certificate);

}

#pragma endregion

#endif
