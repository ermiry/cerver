/**
 * @file cerver/tls/tls.h
 * @brief TLS: the settings of a cerver or a client, the certificate of a peer and the stats of the handshakes.
 *
 * TLS 1.2 and 1.3 (RFC 8446, RFC 9325) through the provider of the cryptography of cerver; the API has no names or
 * types of the provider.
 */

#ifndef _CERVER_TLS_H_
#define _CERVER_TLS_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup tls TLS
 * @brief TLS for the TCP cervers and their clients.
 */

/**
 * @defgroup tls_types Types
 * @ingroup tls
 * @brief The versions, the verification of a client certificate and the reasons of a failed handshake.
 * @{
 */

/** @brief The milliseconds that a handshake can take by default: 2^13. */
#define TLS_HANDSHAKE_TIMEOUT_MILLISECONDS_DEFAULT		8192

/** @brief The threads of the handshakes of a cerver by default: 2^2. */
#define TLS_HANDSHAKE_THREADS_DEFAULT					4

/** @brief The seconds between the rotations of the keys of the session tickets: 2^12. */
#define TLS_TICKET_KEYS_ROTATION_SECONDS				4096

/** @brief The sessions of TLS 1.2 without tickets that a cerver keeps: 2^12. */
#define TLS_SESSION_CACHE_SIZE							4096

/** @brief The milliseconds that the watch of the certificates waits for the files to settle: 2^10. */
#define TLS_RELOAD_DEBOUNCE_MILLISECONDS_DEFAULT		1024

/** @brief A certificate that expires before these days leaves a warning at the start: 2^4. */
#define TLS_CERTIFICATE_EXPIRY_WARNING_DAYS				16

/** @brief The bytes of the SHA-256 fingerprint of a certificate as hex, with its NUL. */
#define TLS_CERTIFICATE_FINGERPRINT_SIZE				65

/**
 * @brief The X-macro table of the versions of TLS: the number, the name and the string of each one.
 */
#define TLS_VERSION_MAP(XX)					\
	XX(0,	NONE,		None)				\
	XX(1,	1_2,		TLS 1.2)			\
	XX(2,	1_3,		TLS 1.3)

/**
 * @brief A version of TLS, one `TLS_VERSION_<name>` for each row of #TLS_VERSION_MAP.
 */
typedef enum TlsVersion {

	#define XX(num, name, string) TLS_VERSION_##name = num,
	TLS_VERSION_MAP (XX)
	#undef XX

} TlsVersion;

/**
 * @brief Returns the string of a version.
 *
 * @param[in] version the version
 * @return the string of its row in #TLS_VERSION_MAP, or the one of TLS_VERSION_NONE for a value out of the table;
 * a static string that must not be freed
 */
CERVER_EXPORT const char *tls_version_to_string (const TlsVersion version);

/**
 * @brief The X-macro table of what a cerver asks of the certificate of a client: the number, the name and the
 * description of each one.
 */
#define TLS_CLIENT_VERIFY_MAP(XX)																	\
	XX(0,	NONE,		The cerver does not ask for a certificate)									\
	XX(1,	OPTIONAL,	The cerver asks for a certificate and verifies it if the client sends one)	\
	XX(2,	REQUIRED,	The handshake fails without a valid certificate)

/**
 * @brief What a cerver asks of the certificate of a client, one `TLS_CLIENT_VERIFY_<name>` for each row of
 * #TLS_CLIENT_VERIFY_MAP.
 */
typedef enum TlsClientVerify {

	#define XX(num, name, description) TLS_CLIENT_VERIFY_##name = num,
	TLS_CLIENT_VERIFY_MAP (XX)
	#undef XX

} TlsClientVerify;

/**
 * @brief Returns the description of a verification mode.
 *
 * @param[in] verify the mode
 * @return the description of its row in #TLS_CLIENT_VERIFY_MAP, or the one of TLS_CLIENT_VERIFY_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_EXPORT const char *tls_client_verify_description (const TlsClientVerify verify);

/**
 * @brief The X-macro table of the reasons why a handshake failed: the number, the name and the description of
 * each one.
 */
#define TLS_FAILURE_MAP(XX)																		\
	XX(0,	NONE,			The handshake did not fail)											\
	XX(1,	TIMEOUT,		The handshake did not end before its deadline)						\
	XX(2,	CLOSED,			The peer closed the connection in the handshake)					\
	XX(3,	PROTOCOL,		A version or a cipher that is not allowed or a malformed message)	\
	XX(4,	CERTIFICATE,	The certificate of the peer is missing or did not verify)			\
	XX(5,	ALPN,			No application protocol in common)									\
	XX(6,	SYSTEM,			An error of the socket)												\
	XX(7,	MEMORY,			An allocation failed)												\
	XX(8,	REJECTED,		The connection was closed before its handshake (the cerver was full))

/** @brief How many reasons #TLS_FAILURE_MAP has. */
#define TLS_FAILURE_COUNT			9

/**
 * @brief The reason why a handshake failed, one `TLS_FAILURE_<name>` for each row of #TLS_FAILURE_MAP.
 */
typedef enum TlsFailure {

	#define XX(num, name, description) TLS_FAILURE_##name = num,
	TLS_FAILURE_MAP (XX)
	#undef XX

} TlsFailure;

/**
 * @brief Returns the description of a failure.
 *
 * @param[in] failure the failure
 * @return the description of its row in #TLS_FAILURE_MAP, or the one of TLS_FAILURE_NONE for a value out of the
 * table; a static string that must not be freed
 */
CERVER_EXPORT const char *tls_failure_description (const TlsFailure failure);

/** @} */

/**
 * @defgroup tls_settings Settings
 * @ingroup tls
 * @brief The settings of TLS of a cerver or a client.
 *
 * The defaults follow RFC 9325: TLS 1.2 and 1.3; in 1.2 only ECDHE with AES-GCM or ChaCha20-Poly1305; in 1.3 the
 * three ciphers of RFC 8446; the groups X25519MLKEM768 (when the provider has it), X25519, P-256 and P-384; without
 * compression or renegotiation; with the extended master secret; tickets on; 0-RTT, OCSP stapling and kTLS off.
 * @{
 */

/** @brief The settings of TLS, opaque; a cerver or a client owns them once they are set. */
typedef struct TlsSettings TlsSettings;

/**
 * @brief Creates the settings with the defaults.
 *
 * @return the new settings, NULL if they could not be allocated
 */
CERVER_EXPORT TlsSettings *tls_settings_create (void);

/**
 * @brief Deletes the settings.
 *
 * @param[in] settings_ptr the settings, it can be NULL
 * @warning Never delete the settings that were given to a cerver or a client: they own them.
 */
CERVER_EXPORT void tls_settings_delete (void *settings_ptr);

/**
 * @brief Adds a certificate with its private key; the first one is the default, and the others are chosen by the
 * name that the client asks for (SNI, RFC 6066).
 *
 * @param[in,out] settings the settings
 * @param[in] certificate_chain the path of a PEM file with the certificate and its chain
 * @param[in] private_key the path of a PEM file with its key, or the URI of a key in a key store (like
 * `pkcs11:token=...;object=...`, see tls_settings_set_key_store ())
 * @retval 0 the paths were kept, they are read and validated when the cerver starts
 * @retval 1 a NULL argument or no memory
 */
CERVER_EXPORT u8 tls_settings_add_certificate (
	TlsSettings *settings, const char *certificate_chain, const char *private_key
);

/**
 * @brief Loads a key store, like the PKCS#11 one, for the keys given as an URI; cerver never reads those keys.
 *
 * @param[in,out] settings the settings
 * @param[in] name the name of the key store of the provider (`pkcs11`)
 * @param[in] properties the properties of the key store, it can be NULL
 * @retval 0 kept, the store is loaded when the cerver starts
 * @retval 1 a NULL argument or no memory
 */
CERVER_EXPORT u8 tls_settings_set_key_store (
	TlsSettings *settings, const char *name, const char *properties
);

/**
 * @brief Sets the oldest version that is accepted.
 *
 * @param[in,out] settings the settings
 * @param[in] version TLS_VERSION_1_2 (the default) or TLS_VERSION_1_3; older versions are never accepted (RFC 8996)
 */
CERVER_EXPORT void tls_settings_set_min_version (TlsSettings *settings, const TlsVersion version);

/**
 * @brief Sets the ciphers, with their IANA names separated by `:`.
 *
 * @param[in,out] settings the settings
 * @param[in] tls12 the ciphers of TLS 1.2 (like `TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256`), NULL keeps the default
 * @param[in] tls13 the ciphers of TLS 1.3 (like `TLS_AES_128_GCM_SHA256`), NULL keeps the default
 * @retval 0 kept, they are validated when the cerver starts
 * @retval 1 a NULL settings or no memory
 * @note A cipher of TLS 1.2 without forward secrecy or without AEAD leaves a warning at the start.
 */
CERVER_EXPORT u8 tls_settings_set_ciphers (
	TlsSettings *settings, const char *tls12, const char *tls13
);

/**
 * @brief Sets the groups of the key exchange, separated by `:`.
 *
 * @param[in,out] settings the settings
 * @param[in] groups like `X25519MLKEM768:X25519:P-256`
 * @retval 0 kept
 * @retval 1 a NULL argument or no memory
 */
CERVER_EXPORT u8 tls_settings_set_groups (TlsSettings *settings, const char *groups);

/**
 * @brief Sets the application protocols (ALPN, RFC 7301), separated by `,`, the preferred first.
 *
 * A cerver chooses the first of its list that the client offers, and closes with `no_application_protocol` when
 * the client offers ALPN without any of them; a client offers them in its hello.
 *
 * @param[in,out] settings the settings
 * @param[in] protocols like `http/1.1`; the web cervers use `http/1.1` when it is not set
 * @retval 0 kept
 * @retval 1 a NULL argument, a protocol longer than 255 bytes or no memory
 */
CERVER_EXPORT u8 tls_settings_set_alpn (TlsSettings *settings, const char *protocols);

/**
 * @brief Sets the certificates of the authorities that are trusted: by a cerver for the certificates of its
 * clients, by a client for the one of its cerver (instead of the store of the system).
 *
 * @param[in,out] settings the settings
 * @param[in] ca_file a PEM file with the certificates, it can be NULL
 * @param[in] ca_path a folder of certificates by their hash, it can be NULL
 * @retval 0 kept
 * @retval 1 a NULL settings, both NULL or no memory
 */
CERVER_EXPORT u8 tls_settings_set_ca (
	TlsSettings *settings, const char *ca_file, const char *ca_path
);

/**
 * @brief Sets what a cerver asks of the certificates of its clients (mTLS).
 *
 * @param[in,out] settings the settings
 * @param[in] verify TLS_CLIENT_VERIFY_NONE (the default), TLS_CLIENT_VERIFY_OPTIONAL or TLS_CLIENT_VERIFY_REQUIRED
 */
CERVER_EXPORT void tls_settings_set_client_verify (TlsSettings *settings, const TlsClientVerify verify);

/**
 * @brief Sets the certificate that a client presents to its cerver (mTLS).
 *
 * @param[in,out] settings the settings of a client
 * @param[in] certificate_chain a PEM file with the certificate and its chain
 * @param[in] private_key a PEM file with its key, or the URI of a key in a key store
 * @retval 0 kept
 * @retval 1 a NULL argument or no memory
 */
CERVER_EXPORT u8 tls_settings_set_client_certificate (
	TlsSettings *settings, const char *certificate_chain, const char *private_key
);

/**
 * @brief Sets the name that a client asks for (SNI) and that the certificate of its cerver must have, instead of
 * the host that it connects to.
 *
 * @param[in,out] settings the settings of a client
 * @param[in] server_name a DNS name or an IP address
 * @retval 0 kept
 * @retval 1 a NULL argument or no memory
 */
CERVER_EXPORT u8 tls_settings_set_server_name (TlsSettings *settings, const char *server_name);

/**
 * @brief Turns the session tickets on (the default) or off; without them a cerver keeps the sessions of TLS 1.2 in
 * a bounded cache (#TLS_SESSION_CACHE_SIZE).
 *
 * The keys of the tickets are made by cerver in memory and rotate every #TLS_TICKET_KEYS_ROTATION_SECONDS, and the
 * previous one still decrypts for one more period.
 *
 * @param[in,out] settings the settings
 * @param[in] tickets true to issue tickets
 */
CERVER_EXPORT void tls_settings_set_tickets (TlsSettings *settings, const bool tickets);

/**
 * @brief Sets a file with the keys of the tickets, to share them between processes behind a balancer.
 *
 * Each key has 80 bytes: 16 of its name, 32 of the HMAC and 32 of AES-256; the first one encrypts and every key of
 * the file decrypts. The application rotates the file and reloads the cerver.
 *
 * @param[in,out] settings the settings
 * @param[in] keys_file the path of the file
 * @retval 0 kept, the file is read when the cerver starts and in every reload
 * @retval 1 a NULL argument or no memory
 */
CERVER_EXPORT u8 tls_settings_set_ticket_keys (TlsSettings *settings, const char *keys_file);

/**
 * @brief Turns on the early data of TLS 1.3 (0-RTT), off by default.
 *
 * In a web cerver only a GET, HEAD or OPTIONS without a body is answered from early data; any other request that
 * arrives as early data is answered with 425 Too Early (RFC 8470).
 *
 * @param[in,out] settings the settings
 * @param[in] max_bytes the most bytes of early data, 0 turns it off
 * @warning Early data can be replayed by an attacker (RFC 8446 section 8): a handler that runs from it must be
 * idempotent.
 */
CERVER_EXPORT void tls_settings_set_early_data (TlsSettings *settings, const u32 max_bytes);

/**
 * @brief Turns on the OCSP stapling (RFC 6960, RFC 6066 section 8), off by default.
 *
 * A thread asks the responder of the certificate (its AIA, or the one of tls_settings_set_ocsp_responder ()) for its
 * status, verifies the answer and renews it at half of its validity; without an answer the handshakes go on without
 * a staple.
 *
 * @param[in,out] settings the settings
 * @param[in] stapling true to staple
 */
CERVER_EXPORT void tls_settings_set_ocsp_stapling (TlsSettings *settings, const bool stapling);

/**
 * @brief Sets the URL of the OCSP responder, instead of the one of the certificate.
 *
 * @param[in,out] settings the settings
 * @param[in] url like `http://ocsp.example.com:8080`
 * @retval 0 kept
 * @retval 1 a NULL argument or no memory
 */
CERVER_EXPORT u8 tls_settings_set_ocsp_responder (TlsSettings *settings, const char *url);

/**
 * @brief Turns on kTLS for what is sent (Linux, with the module `tls`), off by default: the kernel encrypts, and the
 * files are sent with sendfile () without copies.
 *
 * When the kernel does not accept it for a session, it is logged once and cerver encrypts.
 *
 * @param[in,out] settings the settings
 * @param[in] ktls true to ask for it
 */
CERVER_EXPORT void tls_settings_set_ktls (TlsSettings *settings, const bool ktls);

/**
 * @brief Sets how long a handshake can take.
 *
 * @param[in,out] settings the settings
 * @param[in] milliseconds the deadline of each handshake, the default is #TLS_HANDSHAKE_TIMEOUT_MILLISECONDS_DEFAULT
 */
CERVER_EXPORT void tls_settings_set_handshake_timeout_milliseconds (
	TlsSettings *settings, const unsigned int milliseconds
);

/**
 * @brief Sets the threads that run the handshakes of a cerver that is not a web one.
 *
 * @param[in,out] settings the settings
 * @param[in] threads the threads, the default is #TLS_HANDSHAKE_THREADS_DEFAULT
 */
CERVER_EXPORT void tls_settings_set_handshake_threads (TlsSettings *settings, const unsigned int threads);

/** @} */

/**
 * @defgroup tls_certificate Certificate
 * @ingroup tls
 * @brief The certificate that a peer presented and that was verified.
 * @{
 */

/**
 * @brief The certificate of a peer, verified in its handshake.
 */
typedef struct TlsCertificate {

	/** @brief The subject, in the one line form of RFC 2253. */
	char *subject;
	/** @brief The issuer, in the one line form of RFC 2253. */
	char *issuer;
	/** @brief The serial number, as hex. */
	char *serial;
	/** @brief The names of its subject alternative name (DNS, IP, email or URI), separated by `,`. */
	char *alternative_names;
	/** @brief The SHA-256 of its DER, as hex. */
	char fingerprint[TLS_CERTIFICATE_FINGERPRINT_SIZE];
	/** @brief When it starts to be valid. */
	time_t not_before;
	/** @brief When it expires. */
	time_t not_after;
	/** @brief The certificate in PEM, made by tls_certificate_get_pem () when it is asked for. */
	char *pem;

	/** @brief The certificate of the provider of the cryptography, that tls_certificate_get_pem () writes. */
	void *provider_certificate;

} TlsCertificate;

/**
 * @brief Returns the certificate in PEM, made the first time that it is asked for.
 *
 * @param[in,out] certificate the certificate
 * @return the PEM, owned by the certificate; NULL without memory
 */
CERVER_EXPORT const char *tls_certificate_get_pem (TlsCertificate *certificate);

/** @} */

/**
 * @defgroup tls_stats Stats
 * @ingroup tls
 * @brief The stats of TLS of a cerver.
 * @{
 */

/**
 * @brief The stats of TLS of a cerver.
 */
typedef struct TlsStats {

	/** @brief The handshakes that ended well. */
	u64 handshakes;
	/** @brief The handshakes that resumed a session. */
	u64 resumed;
	/** @brief The handshakes that failed, by their reason (#TLS_FAILURE_MAP). */
	u64 failures[TLS_FAILURE_COUNT];
	/** @brief The connections that send with kTLS. */
	u64 ktls_send;
	/** @brief The requests that were answered from early data. */
	u64 early_data_accepted;
	/** @brief The requests from early data that were answered with 425. */
	u64 early_data_rejected;
	/** @brief The reloads that were tried. */
	u64 reloads;
	/** @brief The reloads that failed and kept the previous certificates. */
	u64 reload_failures;
	/** @brief The OCSP answers that were stapled after a refresh. */
	u64 ocsp_refreshes;
	/** @brief The refreshes of an OCSP answer that failed. */
	u64 ocsp_failures;
	/** @brief The seconds until the first certificate of the cerver expires, negative once it expired. */
	i64 certificate_expiry_seconds;

} TlsStats;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
