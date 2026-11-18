/**
 * @file crypto/provider.h
 * @internal
 * @brief What the provider of the cryptography implements; the build selects it with CRYPTO (only openssl).
 */

#ifndef _CERVER_CRYPTO_PROVIDER_H_
#define _CERVER_CRYPTO_PROVIDER_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/crypto/hash.h"

#include "cerver/tls/tls.h"

#if !defined (CERVER_CRYPTO_OPENSSL)
#error "cerver needs a provider of the cryptography: CRYPTO=openssl"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief The SHA-1 of the provider of the cryptography; cerver_crypto_sha1 () already checked the arguments.
 *
 * @param[in] data the bytes, NULL is taken as an empty input
 * @param[in] size the number of bytes
 * @param[out] digest where the #CERVER_CRYPTO_SHA1_SIZE bytes of the digest are written
 * @return true on success
 */
CERVER_HIDDEN bool cerver_crypto_provider_sha1 (
	const void *data, const size_t size,
	u8 digest[CERVER_CRYPTO_SHA1_SIZE]
);

/** @internal @brief The context of the provider for a cerver or a client, opaque. */
typedef struct CryptoTlsContext CryptoTlsContext;

/** @internal @brief The session of the provider of a connection, opaque. */
typedef struct CryptoTlsSession CryptoTlsSession;

struct _TlsConnection;

/**
 * @internal
 * @brief The X-macro table of the results of a step of a session: the number and the name of each one.
 */
#define CRYPTO_TLS_RESULT_MAP(XX)		\
	XX(0,	DONE)						\
	XX(1,	WANT_READ)					\
	XX(2,	CLOSED)						\
	XX(3,	FAILED)

/**
 * @internal
 * @brief The result of a step of a session, one `CRYPTO_TLS_RESULT_<name>` for each row of #CRYPTO_TLS_RESULT_MAP.
 */
typedef enum CryptoTlsResult {

	#define XX(num, name) CRYPTO_TLS_RESULT_##name = num,
	CRYPTO_TLS_RESULT_MAP (XX)
	#undef XX

} CryptoTlsResult;

/**
 * @internal
 * @brief Makes a context: it reads and validates the certificates, the keys and the settings.
 *
 * @param[in] settings the settings
 * @param[in] server true for a cerver, false for a client
 * @param[in] web true for a web cerver, that offers `http/1.1` when the settings have no ALPN
 * @param[out] error the reason when it fails, with its NUL
 * @param[in] error_size the size of error
 * @return the context with one reference, NULL on an error
 */
CERVER_HIDDEN CryptoTlsContext *cerver_crypto_provider_tls_context_create (
	const TlsSettings *settings, const bool server, const bool web,
	char *error, const size_t error_size
);

/**
 * @internal
 * @brief Takes a reference to a context.
 *
 * @param[in,out] context the context
 * @return the same context
 */
CERVER_HIDDEN CryptoTlsContext *cerver_crypto_provider_tls_context_reference (CryptoTlsContext *context);

/**
 * @internal
 * @brief Releases a reference to a context; it is deleted with the last one.
 *
 * @param[in] context the context, it can be NULL
 */
CERVER_HIDDEN void cerver_crypto_provider_tls_context_release (CryptoTlsContext *context);

/**
 * @internal
 * @brief Returns the seconds until the first certificate of the context expires.
 *
 * @param[in] context the context of a cerver
 * @return the seconds, negative when it already expired, 0 without certificates
 */
CERVER_HIDDEN i64 cerver_crypto_provider_tls_context_expiry_seconds (const CryptoTlsContext *context);

/**
 * @internal
 * @brief Asks the OCSP responder for the status of each certificate of a context that staples, and keeps the
 * answers that verified.
 *
 * @param[in,out] context the context of a cerver
 * @param[out] next_seconds the seconds until the next refresh
 * @retval 0 every answer was refreshed
 * @retval 1 at least one failed (the previous answer stays while it is valid)
 * @retval 2 the context does not staple
 */
CERVER_HIDDEN u8 cerver_crypto_provider_tls_context_refresh_ocsp (
	CryptoTlsContext *context, time_t *next_seconds
);

/**
 * @internal
 * @brief Makes the session of a connection.
 *
 * @param[in] context the context, the session takes a reference to it
 * @param[in] connection the connection whose buffers carry the records
 * @param[in] server_name the name that a client asks for and verifies, NULL in a cerver
 * @param[in] destination the host and port of a client, the key of its cache of sessions; NULL in a cerver
 * @return the session, NULL without memory
 */
CERVER_HIDDEN CryptoTlsSession *cerver_crypto_provider_tls_session_create (
	CryptoTlsContext *context, struct _TlsConnection *connection,
	const char *server_name, const char *destination
);

/**
 * @internal
 * @brief Deletes a session, without sending its close_notify.
 *
 * @param[in] session the session, it can be NULL
 */
CERVER_HIDDEN void cerver_crypto_provider_tls_session_delete (CryptoTlsSession *session);

/**
 * @internal
 * @brief Runs the handshake as far as the received records allow.
 *
 * @param[in,out] session the session
 * @param[out] failure the reason when it fails
 * @return DONE when it ended, WANT_READ when it needs more records, CLOSED or FAILED
 */
CERVER_HIDDEN CryptoTlsResult cerver_crypto_provider_tls_session_handshake (
	CryptoTlsSession *session, TlsFailure *failure
);

/**
 * @internal
 * @brief Reads the early data of a cerver, before its handshake ends.
 *
 * @param[in,out] session the session of a cerver with early data
 * @param[out] buffer where the bytes are written
 * @param[in] size the size of the buffer
 * @param[out] read the bytes that were read
 * @return DONE when the early data ended (or there was none), WANT_READ when it needs more records or more room
 * (with read > 0), FAILED
 */
CERVER_HIDDEN CryptoTlsResult cerver_crypto_provider_tls_session_read_early (
	CryptoTlsSession *session, void *buffer, const size_t size, size_t *read
);

/**
 * @internal
 * @brief Decrypts what the received records allow.
 *
 * @param[in,out] session the session
 * @param[out] buffer where the bytes are written
 * @param[in] size the size of the buffer
 * @param[out] read the bytes that were read
 * @return DONE with bytes, WANT_READ when it needs more records, CLOSED after the close_notify of the peer, FAILED
 */
CERVER_HIDDEN CryptoTlsResult cerver_crypto_provider_tls_session_read (
	CryptoTlsSession *session, void *buffer, const size_t size, size_t *read
);

/**
 * @internal
 * @brief Encrypts bytes into the output of the connection (or, with kTLS, gives them to the kernel).
 *
 * @param[in,out] session the session
 * @param[in] data the bytes
 * @param[in] size how many
 * @return DONE or FAILED
 */
CERVER_HIDDEN CryptoTlsResult cerver_crypto_provider_tls_session_write (
	CryptoTlsSession *session, const void *data, const size_t size
);

/**
 * @internal
 * @brief Returns whether the session has decrypted bytes that were not read yet.
 *
 * @param[in] session the session
 * @return true when a read gives bytes without receiving
 */
CERVER_HIDDEN bool cerver_crypto_provider_tls_session_pending (const CryptoTlsSession *session);

/**
 * @internal
 * @brief Writes the close_notify of the session into the output of the connection.
 *
 * @param[in,out] session the session
 */
CERVER_HIDDEN void cerver_crypto_provider_tls_session_close (CryptoTlsSession *session);

/**
 * @internal
 * @brief Returns whether the handshake resumed a session.
 *
 * @param[in] session the session
 * @return true when it was resumed
 */
CERVER_HIDDEN bool cerver_crypto_provider_tls_session_resumed (const CryptoTlsSession *session);

/**
 * @internal
 * @brief Returns whether the early data of a cerver was accepted.
 *
 * @param[in] session the session
 * @return true when the client sent early data and the cerver took it
 */
CERVER_HIDDEN bool cerver_crypto_provider_tls_session_early_data_accepted (const CryptoTlsSession *session);

/**
 * @internal
 * @brief Returns the name that the client asked for (SNI).
 *
 * @param[in] session the session of a cerver
 * @return the name, owned by the session; NULL when the client did not send it
 */
CERVER_HIDDEN const char *cerver_crypto_provider_tls_session_server_name (const CryptoTlsSession *session);

/**
 * @internal
 * @brief Returns the application protocol that was negotiated (ALPN).
 *
 * @param[in] session the session
 * @param[out] size the bytes of the protocol
 * @return the protocol, owned by the session and without a NUL; NULL without ALPN
 */
CERVER_HIDDEN const u8 *cerver_crypto_provider_tls_session_alpn (
	const CryptoTlsSession *session, unsigned int *size
);

/**
 * @internal
 * @brief Returns the version that was negotiated.
 *
 * @param[in] session the session
 * @return TLS_VERSION_1_2 or TLS_VERSION_1_3, TLS_VERSION_NONE before the handshake
 */
CERVER_HIDDEN TlsVersion cerver_crypto_provider_tls_session_version (const CryptoTlsSession *session);

/**
 * @internal
 * @brief Makes the certificate of the peer, when it sent one and it verified.
 *
 * @param[in] session the session
 * @return a new certificate (deleted with tls_certificate_delete ()), NULL without one or without memory
 */
CERVER_HIDDEN TlsCertificate *cerver_crypto_provider_tls_session_peer_certificate (
	const CryptoTlsSession *session
);

/**
 * @internal
 * @brief Writes a certificate of the provider in PEM.
 *
 * @param[in] provider_certificate the certificate of the provider
 * @return the PEM (freed with free ()), NULL without memory
 */
CERVER_HIDDEN char *cerver_crypto_provider_tls_certificate_pem (const void *provider_certificate);

/**
 * @internal
 * @brief Releases a certificate of the provider.
 *
 * @param[in] provider_certificate the certificate, it can be NULL
 */
CERVER_HIDDEN void cerver_crypto_provider_tls_certificate_release (void *provider_certificate);

#ifdef __cplusplus
}
#endif

#endif
